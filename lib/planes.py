"""Host-side reference model for the traffic-sprite renderer.

Implements the pipeline specified in `docs/planes.md`: project an aircraft's
five model points, choose a sprite tier and stroke thickness, and rasterise
three lines into a sprite buffer.

Everything from the projection onward runs in the same integer arithmetic the
C64 uses -- `fmul` and `fdiv` reproduce `vec_fastmul8p8` and `vec_div8p8`
including their truncation toward zero -- so the bytes this module produces are
the bytes `ppilot` should produce. The interactive twin is
`docs/planes-prototype.html`; the two implement the same algorithm and are
expected to agree.

Nothing here is compiled into the C64 build. This is a reference for testing
and for working out the rules, in the same spirit as `lib/roll_angle.py`.
"""

import math
from dataclasses import dataclass, field
from typing import List, Optional, Sequence, Tuple

# --------------------------------------------------------------------------
# Viewport geometry (mem.h)
# --------------------------------------------------------------------------

VIEW_W = 320
VIEW_H = 112
CX0 = VIEW_W // 2          # gfx.cc: px = 160 - vec_sx
CY0 = VIEW_H // 2
PROJ = 256                 # pixels per unit of tangent

COLS = 24                  # sprite width, in sprite pixels
ROWS = 21                  # sprite height, in raster lines

# Traffic uses quarter-metre units for the relative position, NOT the 2 m the
# terrain grid uses. The grid needs kilometres of range; a nearby aircraft
# needs precision. At 100 m and a few degrees off the nose the lateral offset
# is only a handful of coarse units, so one unit of rounding swings the
# projected centre by several pixels and the silhouette jumps as the range
# changes. A quarter metre still reaches 4 km inside an int16.
RENDER_UNIT_M = 0.25
EIGHTHS_PER_M = 8          # model dimensions are stored in 1/8 m

MIN_CAM_X = 64             # 16 m, in quarter-metre units
MAX_CAM_X = 16000          # 4 km range cull

# The largest silhouette any tier can hold: two X-expanded sprites, 48 x 42
# screen pixels. A bounding box of *extent* 42 needs 43 rows, so the usable
# extents are one less -- and one less again on the width, because an
# X-expanded column is reached through a divide-by-two that rounds up.
MAX_BBOX_W = COLS * 2 - 2  # 46
MAX_BBOX_H = ROWS * 2 - 1  # 41


# --------------------------------------------------------------------------
# 6502 fixed point (vec_asm.cc)
# --------------------------------------------------------------------------

def fmul(a: int, b: int) -> int:
    """trunc(a * b / 256), rounding toward zero -- `vec_fastmul8p8`.

    The assembly forms the product from the magnitudes and applies the sign at
    the end, so it truncates toward zero rather than flooring. Half of all
    negative products differ by one between the two, which is why this is
    spelled out rather than written as `a * b >> 8`.
    """
    p = a * b
    return -((-p) // 256) if p < 0 else p // 256


def fdiv(a: int, b: int) -> int:
    """trunc((a << 8) / b), rounding toward zero -- `vec_div8p8`."""
    if b == 0:
        return 0
    p = (a * 256) / b
    return -math.floor(-p) if p < 0 else math.floor(p)


def jround(x: float) -> int:
    """Round half away from zero the way the 6502 code and the JavaScript twin
    both do (`floor(x + 0.5)`).

    Python's built-in `round` is banker's rounding -- it breaks ties toward the
    even value -- which disagrees on exactly the half-pixel cases an
    X-expanded sprite produces constantly. Cross-checking the two
    implementations turned up 154 mismatched bitmaps that were all this.
    """
    return math.floor(x + 0.5)


def norm2(x: int, y: int) -> int:
    """|(x, y)| without a square root: the octagonal approximation.

    Within about 4% over the whole circle, and on the 6502 it is a compare, a
    shift and an add instead of a square root.
    """
    x, y = abs(x), abs(y)
    return x + (y >> 1) if x > y else y + (x >> 1)


# --------------------------------------------------------------------------
# Vectors and orientation
# --------------------------------------------------------------------------

Vec3 = Tuple[float, float, float]
Vec2 = Tuple[int, int]


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


@dataclass(frozen=True)
class Mat3:
    """`mat3_t`: three orthonormal axes. x is forward, y is left, z is up."""
    front: Vec3
    left: Vec3
    up: Vec3


def orient(heading_deg: float, pitch_deg: float, bank_deg: float) -> Mat3:
    """Build an orientation from Euler angles. Convenience for callers only --
    the C64 carries a `mat3_t` directly and never converts from angles."""
    h, p, b = map(math.radians, (heading_deg, pitch_deg, bank_deg))
    front = (math.cos(p) * math.cos(h), math.cos(p) * math.sin(h), math.sin(p))
    left0 = (-math.sin(h), math.cos(h), 0.0)
    up0 = cross(front, left0)
    left = tuple(left0[i] * math.cos(b) + up0[i] * math.sin(b) for i in range(3))
    return Mat3(front, left, cross(front, left))


def to_cam(m: Mat3, v: Vec3) -> Vec3:
    """`vec_transform_inv`: world space to camera space."""
    return (dot(v, m.front), dot(v, m.left), dot(v, m.up))


def q88(v: Vec3) -> Tuple[int, int, int]:
    """Quantise a unit vector to 8.8, the way `mat3_t` stores one."""
    return tuple(jround(c * 256) for c in v)


# --------------------------------------------------------------------------
# Aircraft model
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Model:
    """Aircraft dimensions in metres. Defaults are a Cessna 172."""
    span_m: float = 11.0
    length_m: float = 8.3
    nose_frac: float = 0.55       # of length, ahead of the centre
    tail_frac: float = 0.45       # of length, behind it
    wing_fwd_frac: float = 0.20   # wing hub ahead of the centre
    fin_frac: float = 0.13        # fin height, of span
    chord_frac: float = 0.15      # wing chord, of span
    # Size exaggeration. At true scale a plane is under 4 px beyond 700 m,
    # which is most of any encounter; 1.5x makes traffic readable at realistic
    # separations. The default 11 m span therefore draws as 16.5 m.
    scale: float = 1.5

    def _eighths(self, metres: float) -> int:
        return jround(metres * EIGHTHS_PER_M * self.scale)

    @property
    def half_span(self) -> int:
        return self._eighths(self.span_m / 2)

    @property
    def nose(self) -> int:
        return self._eighths(self.length_m * self.nose_frac)

    @property
    def tail(self) -> int:
        return self._eighths(self.length_m * self.tail_frac)

    @property
    def wing_fwd(self) -> int:
        return self._eighths(self.length_m * self.wing_fwd_frac)

    @property
    def fin(self) -> int:
        return self._eighths(self.span_m * self.fin_frac)

    @property
    def chord_over_length(self) -> float:
        return self.chord_frac * self.span_m / self.length_m

    @property
    def max_radius(self) -> int:
        """Furthest any model point can be from the wing hub, in eighths.

        Every projected point lies within this radius of the hub whatever the
        attitude, so it bounds the silhouette in *both* axes at once.
        """
        to_tail = self.tail + self.wing_fwd
        return max(self.half_span,
                   self.nose - self.wing_fwd,
                   jround(math.hypot(to_tail, self.fin)))

    @property
    def k_max(self) -> int:
        """Largest perspective scale that still fits the whole aircraft.

        `2 * max_radius * k / 256 <= MAX_BBOX_H`, solved for k. A constant per
        aircraft type, so the clamp is one comparison at runtime.
        """
        return (MAX_BBOX_H * 128) // self.max_radius


# --------------------------------------------------------------------------
# Sprite buffer and rasteriser
# --------------------------------------------------------------------------

_MASK_FROM: List[List[int]] = []
_MASK_TO: List[List[int]] = []
for _i in range(COLS):
    _f, _t = [0, 0, 0], [0, 0, 0]
    for _x in range(COLS):
        _bit, _by = 0x80 >> (_x & 7), _x >> 3
        if _x >= _i:
            _f[_by] |= _bit
        if _x <= _i:
            _t[_by] |= _bit
    _MASK_FROM.append(_f)
    _MASK_TO.append(_t)


class SpriteBuffer:
    """`rows` raster lines of 24 pixels, three bytes each.

    `touched` counts rows whose run mask had to be built; `repeat` counts rows
    that reused the mask already in hand for stroke thickness. The two are
    weighted differently in the cycle estimate.
    """

    def __init__(self, rows: int):
        self.rows = rows
        self.data = bytearray(3 * rows)
        self.touched = 0
        self.repeat = 0

    def fill_run(self, y: int, a: int, b: int, is_repeat: bool = False) -> None:
        if y < 0 or y >= self.rows:
            return
        if a > b:
            a, b = b, a
        a, b = max(0, a), min(COLS - 1, b)
        if b < a:
            return
        o, mf, mt = 3 * y, _MASK_FROM[a], _MASK_TO[b]
        for i in range(3):
            self.data[o + i] |= mf[i] & mt[i]
        if is_repeat:
            self.repeat += 1
        else:
            self.touched += 1

    def get(self, x: int, y: int) -> int:
        return (self.data[3 * y + (x >> 3)] >> (7 - (x & 7))) & 1

    def ink(self) -> int:
        return sum(self.get(x, y) for y in range(self.rows) for x in range(COLS))

    def rows_as_text(self) -> List[str]:
        return ["".join("#" if self.get(x, y) else "." for x in range(COLS))
                for y in range(self.rows)]


def _build_dot() -> "SpriteBuffer":
    """The far tier's bitmap: a 2 x 2 blob, built once.

    Beyond ~700 m an aircraft is under 4 px and there is no silhouette left to
    draw, so it gets a fixed bitmap rather than three strokes. This is the
    common case by a wide margin -- see the distance table in the docs -- and
    it skips the buffer clear, the rasteriser and the pointer flip entirely.

    On the C64 this is a static block in `$D400`, written once at startup
    alongside the instrument needles. Traffic at that range needs no dynamic
    buffer at all: the sprite pointer is simply aimed at the fixed block.
    """
    buf = SpriteBuffer(ROWS)
    mx, my = jround(COLS / 2), jround(ROWS / 2)
    buf.fill_run(my, mx, mx + 1)
    buf.fill_run(my + 1, mx, mx + 1)
    buf.touched = buf.repeat = 0        # costs nothing at runtime
    return buf


def clip_seg(x0: int, y0: int, x1: int, y1: int,
             w: int, h: int) -> Optional[Tuple[int, int, int, int]]:
    """Liang-Barsky clip against the buffer rectangle.

    Endpoints must be CLIPPED, never clamped. Clamping pins every point of a
    very close aircraft to a corner and the silhouette degenerates into the two
    diagonals of the buffer -- a bare X, with the fin swallowed into the tail.
    """
    t0, t1 = 0.0, 1.0
    dx, dy = x1 - x0, y1 - y0
    p = (-dx, dx, -dy, dy)
    q = (x0, (w - 1) - x0, y0, (h - 1) - y0)
    for pi, qi in zip(p, q):
        if pi == 0:
            if qi < 0:
                return None
            continue
        r = qi / pi
        if pi < 0:
            if r > t1:
                return None
            t0 = max(t0, r)
        else:
            if r < t0:
                return None
            t1 = min(t1, r)
    return (jround(x0 + t0 * dx), jround(y0 + t0 * dy),
            jround(x0 + t1 * dx), jround(y0 + t1 * dy))


def stroke(buf: SpriteBuffer, x0: int, y0: int, x1: int, y1: int,
           tv: int, th: int, xs: int) -> None:
    """One line, drawn as a run per row.

    `tv` is the thickness perpendicular to a shallow stroke (extra rows), `th`
    the thickness perpendicular to a steep one (extra columns). Applying both
    to both would lengthen the line rather than thicken it.
    """
    clipped = clip_seg(x0, y0, x1, y1, COLS, buf.rows)
    if clipped is None:
        return
    x0, y0, x1, y1 = clipped
    if y1 < y0:
        x0, x1, y0, y1 = x1, x0, y1, y0
    dy, dx = y1 - y0, x1 - x0

    # The steep/shallow test decides which axis is thickened, so it must be
    # made in SCREEN space: in an X-expanded sprite one buffer column is two
    # screen pixels, so a stroke measuring 45 degrees in the buffer is really
    # 26 degrees on screen and wants row thickening, not column thickening.
    steep = abs(dx) * xs < dy
    top, side = -(tv >> 1), -(th >> 1)

    if dy == 0:
        for r in range(tv):
            buf.fill_run(y0 + top + r, min(x0, x1), max(x0, x1), r > 0)
        return

    slope = fdiv(dx, dy)
    xa = x0 << 8
    for y in range(y0, y1 + 1):
        if y == y1:
            xa = x1 << 8
            xb = xa
        else:
            xb = xa + slope
        a, b = min(xa, xb) >> 8, max(xa, xb) >> 8
        if steep:
            a += side
            buf.fill_run(y, a, a + th - 1)
        else:
            for r in range(tv):
                buf.fill_run(y + top + r, a, b, r > 0)
        xa = xb


# --------------------------------------------------------------------------
# Tier and thickness, both with hysteresis
# --------------------------------------------------------------------------

HYSTERESIS = 0.87
# Largest bounding-box extent a single sprite can hold. One less than its pixel
# size, for the same reason as MAX_BBOX_*: an extent of 21 spans 22 rows.
TIER_W, TIER_H = COLS - 1, ROWS - 1     # 23, 20
BODY_LIMITS = (12, 48)      # unforeshortened wingspan, screen px
SURFACE_LIMITS = (2, 4)     # projected chord, screen px


@dataclass
class Tier:
    xs: int
    rows: int
    sprites: int
    dot: bool = False

    @property
    def name(self) -> str:
        if self.dot:
            return "dot"
        return "%d sprite%s, %s" % (self.sprites, "s" if self.sprites > 1 else "",
                                    "X-exp" if self.xs == 2 else "1:1")


@dataclass
class State:
    """Everything that persists between frames: the hysteresis latches and the
    endpoint cache. One instance per tracked aircraft."""
    prev_xs: int = 1
    prev_rows: int = ROWS
    prev_body: int = 1
    prev_wing: int = 1
    prev_fin: int = 1
    cache_key: Optional[tuple] = None
    cache_buf: Optional[SpriteBuffer] = None


def pick_tier(state: State, bw: int, bh: int) -> Tier:
    """Width picks X-expansion, height picks the sprite count -- independently.

    A linear ladder gets this wrong for a steeply banked aircraft, which is
    tall and narrow: it needs two sprites for its height and would be
    X-expanded along with them, throwing away horizontal resolution on the axis
    that was never the problem.
    """
    if bw <= 3 and bh <= 3:
        state.prev_xs, state.prev_rows = 1, ROWS
        return Tier(xs=1, rows=ROWS, sprites=1, dot=True)

    xs = 1 if bw <= TIER_W else 2
    if state.prev_xs == 2 and xs == 1 and bw > HYSTERESIS * TIER_W:
        xs = 2
    rows = ROWS if bh <= TIER_H else 2 * ROWS
    if state.prev_rows == 2 * ROWS and rows == ROWS and bh > HYSTERESIS * TIER_H:
        rows = 2 * ROWS
    state.prev_xs, state.prev_rows = xs, rows
    return Tier(xs=xs, rows=rows, sprites=rows // ROWS)


def _rung(px: float, limits: Tuple[int, int], prev: int) -> int:
    """Quantise a screen-pixel thickness onto the 1 / 2 / 4 ladder."""
    t = 1 if px < limits[0] else (2 if px < limits[1] else 4)
    if prev > t and px > HYSTERESIS * limits[0 if t == 1 else 1]:
        t = prev
    return t


def _convert(t: int, xs: int, max_thick: int) -> Tuple[int, int]:
    """Screen thickness to (rows, columns) for this tier.

    `t` is forced even because two continuity constraints demand it: at the
    steep/shallow crossover `th * xs` must equal `tv` exactly, and across an
    X-expansion change `tv` must not move. Both hold only if `tv` is divisible
    by every `xs` in use.
    """
    t = min(max_thick, t)
    if t >= 2:
        t &= ~1
    return t, max(1, t // xs)


def chord_px(f: Vec2, s: Vec2, chord_over_length: float) -> float:
    """Extent of the fuselage vector `f` measured perpendicular to a surface's
    screen direction `s`, scaled to the surface's chord.

    This is what a flat plate's chord projects to: face-on it is the full
    chord, edge-on it is zero. It inherits the parallel-projection
    approximation of the rest of the pipeline, so it responds to the relative
    orientation of aircraft and camera but not to the target's position within
    the field of view.
    """
    n = norm2(s[0], s[1])
    if n == 0:
        return 0.0
    return abs(f[0] * s[1] - f[1] * s[0]) / n * chord_over_length


# --------------------------------------------------------------------------
# The pipeline
# --------------------------------------------------------------------------

@dataclass
class Result:
    visible: bool
    reason: str = ""
    cam: Tuple[int, int, int] = (0, 0, 0)
    k: int = 0
    centre: Vec2 = (0, 0)
    points: dict = field(default_factory=dict)     # screen pixels
    local: dict = field(default_factory=dict)      # buffer coordinates
    bbox: Tuple[int, int] = (0, 0)
    tier: Optional[Tier] = None
    origin: Vec2 = (0, 0)
    fits: bool = True
    thickness: dict = field(default_factory=dict)  # name -> (rows, cols)
    buf: Optional[SpriteBuffer] = None
    cached: bool = False
    cycles: int = 0
    clamped: bool = False    # apparent size held to keep the whole plane in frame


def render(state: State, cam: Mat3, target: Mat3, rel_pos_m: Vec3,
           model: Model = Model(), max_thick: int = 4,
           with_fin: bool = True) -> Result:
    """One aircraft, one frame. `rel_pos_m` is target minus eye, in metres."""

    # 1. to render units (2 m each), int16
    pu = tuple(jround(c / RENDER_UNIT_M) for c in rel_pos_m)
    # 3. camera space
    c = tuple(jround(v) for v in to_cam(cam, pu))

    if c[0] <= MIN_CAM_X:
        return Result(visible=False, reason="behind camera", cam=c)
    if c[0] > MAX_CAM_X:
        return Result(visible=False, reason="out of range", cam=c)

    # 5. centre, 6. perspective scale
    cx = CX0 - fdiv(c[1], c[0])
    cy = CY0 - fdiv(c[2], c[0])
    # px = 256 * (O/8) / (C.x/4), so k = 32768 / C.x
    k = fdiv(128, c[0])

    # 6b. Size clamp. Closer than about 60 m the silhouette outgrows even the
    # largest tier, and cropping it would show the middle of an aircraft rather
    # than an aircraft. Hold the apparent size instead: cap k, which is exactly
    # pretending the target stopped approaching.
    #
    # The cap is a constant of the model, NOT a function of the current
    # bounding box. A bbox-derived cap would shrink by a different factor
    # depending on attitude, which puts the angle dependence back into the body
    # thickness that §6 works to keep out -- the reference caught this
    # immediately when it was tried that way.
    clamped = k > model.k_max
    if clamped:
        k = model.k_max

    # 7. body axes in camera space
    fore, lat, vert = (q88(to_cam(cam, target.front)),
                       q88(to_cam(cam, target.left)),
                       q88(to_cam(cam, target.up)))

    pt = lambda axis, ext: (cx - fmul(axis[1], ext), cy - fmul(axis[2], ext))
    add = lambda a, b: (a[0] + b[0] - cx, a[1] + b[1] - cy)

    def project(k):
        """Steps 8 and 9 for a given perspective scale.

        The doubling in `half` recovers the pixel that fmul's truncation toward
        zero would otherwise lose off every extent.
        """
        half = lambda v: (fmul(2 * v, k) + 1) >> 1
        px = dict(h=half(model.half_span), ln=half(model.nose),
                  lt=half(model.tail), wf=half(model.wing_fwd),
                  fn=half(model.fin))
        hub = pt(fore, px["wf"])               # the wing sits ahead of centre
        pts = {
            "nose": pt(fore, px["ln"]),
            "tail": pt(fore, -px["lt"]),
            "tipL": add(hub, pt(lat, px["h"])),
            "tipR": add(hub, pt(lat, -px["h"])),
        }
        if with_fin:
            pts["fin"] = add(pts["tail"], pt(vert, px["fn"]))
        xs_all = [p[0] for p in pts.values()]
        ys_all = [p[1] for p in pts.values()]
        box = (min(xs_all), max(xs_all), min(ys_all), max(ys_all))
        return px, hub, pts, (box[1] - box[0], box[3] - box[2]), box

    px, hub, pts, (bw, bh), box = project(k)
    minx, maxx, miny, maxy = box

    # 9. tier, then the centring anchor
    tier = pick_tier(state, bw, bh)
    fits = bw <= COLS * tier.xs and bh <= tier.rows
    # Centre on the bounding box while the silhouette fits. Once it does not,
    # centre on the wing hub: the wing carries the shape, so the fuselage is
    # what should run off the ends, not a wingtip.
    anchor = (jround((minx + maxx) / 2), jround((miny + maxy) / 2)) if fits else hub
    origin = (anchor[0] - ((COLS * tier.xs) >> 1), anchor[1] - (tier.rows >> 1))

    # Not clamped -- see clip_seg().
    loc = lambda p: (jround((p[0] - origin[0]) / tier.xs), p[1] - origin[1])
    local = {name: loc(p) for name, p in pts.items()}

    # Thickness. The fuselage is a body of revolution and looks the same width
    # from every angle, so it follows distance alone via the unforeshortened
    # wingspan. The wing and fin are flat plates and genuinely change apparent
    # width as they rotate, so they follow their projected chord.
    fvec = (pts["nose"][0] - pts["tail"][0], pts["nose"][1] - pts["tail"][1])
    wvec = (pts["tipL"][0] - pts["tipR"][0], pts["tipL"][1] - pts["tipR"][1])
    nvec = ((pts["fin"][0] - pts["tail"][0], pts["fin"][1] - pts["tail"][1])
            if with_fin else (0, 0))
    col = model.chord_over_length

    state.prev_body = _rung(2 * px["h"], BODY_LIMITS, state.prev_body)
    state.prev_wing = _rung(chord_px(fvec, wvec, col), SURFACE_LIMITS, state.prev_wing)
    state.prev_fin = _rung(chord_px(fvec, nvec, col), SURFACE_LIMITS, state.prev_fin)
    thickness = {
        "body": _convert(state.prev_body, tier.xs, max_thick),
        "wing": _convert(state.prev_wing, tier.xs, max_thick),
        "fin": _convert(state.prev_fin, tier.xs, max_thick),
    }

    # 10. cache on the local endpoints, the tier and the thickness
    key = (tier.name, tuple(sorted(thickness.items())),
           tuple(sorted((n, tuple(p)) for n, p in local.items())))
    cached = state.cache_key == key and state.cache_buf is not None

    # 11. rasterise -- or, in the far tier, do not
    if tier.dot:
        buf = DOT_BITMAP
        state.cache_key, state.cache_buf = key, buf
        cycles = 2160                  # transform and sprite registers only
    elif cached:
        buf = state.cache_buf
        cycles = 2100
    else:
        buf = SpriteBuffer(tier.rows)
        stroke(buf, *local["tail"], *local["nose"], *thickness["body"], tier.xs)
        stroke(buf, *local["tipL"], *local["tipR"], *thickness["wing"], tier.xs)
        if with_fin:
            stroke(buf, *local["tail"], *local["fin"], *thickness["fin"], tier.xs)
        state.cache_key, state.cache_buf = key, buf
        # 35 cycles for a row whose mask must be built, 15 for a thickness
        # repeat that reuses the mask already in hand; 2100 for the transform
        # and 260 for the buffer clear and the sprite registers.
        cycles = 2360 + buf.touched * 35 + buf.repeat * 15

    return Result(visible=True, cam=c, k=k, centre=(cx, cy), points=pts,
                  clamped=clamped,
                  local=local, bbox=(bw, bh), tier=tier, origin=origin,
                  fits=fits, thickness=thickness, buf=buf, cached=cached,
                  cycles=cycles)


DOT_BITMAP = _build_dot()


def span_pixels(span_m: float, distance_m: float) -> float:
    """The scale relation the whole design rests on: `256 * S / D`."""
    return PROJ * span_m / distance_m


def place(distance_m: float, bearing_deg: float = 0.0,
          elevation_deg: float = 0.0) -> Vec3:
    """A relative position, in metres. Positive bearing is to the right."""
    b, e = math.radians(bearing_deg), math.radians(elevation_deg)
    return (distance_m * math.cos(e) * math.cos(b),
            -distance_m * math.cos(e) * math.sin(b),
            distance_m * math.sin(e))
