"""Host-side reference model for the traffic-sprite renderer.

Implements the pipeline specified in `docs/planes.md`: project an aircraft's
polygon model, choose a pixel size from its distance and a sprite layout from
its bounding box, and fill the polygons into a buffer of up to 2 x 2 sprites.

Everything from the projection onward runs in the same integer arithmetic the
C64 uses -- `fmul` and `fdiv` reproduce `vec_fastmul8p8` and `vec_div8p8`
including their truncation toward zero -- so the bytes this module produces are
the bytes `ppilot` should produce. The interactive twin is
`docs/planes-prototype.html`; the two implement the same algorithm and are
expected to agree bit for bit.

Nothing here is compiled into the C64 build. This is a reference for testing
and for working out the rules, in the same spirit as `lib/roll_angle.py`.
"""

import math
from dataclasses import dataclass, field
from functools import cached_property
from typing import Dict, FrozenSet, List, Optional, Tuple

# --------------------------------------------------------------------------
# Viewport geometry (mem.h)
# --------------------------------------------------------------------------

VIEW_W = 320
VIEW_H = 112
CX0 = VIEW_W // 2          # gfx.cc: px = 160 - vec_sx
CY0 = VIEW_H // 2
PROJ = 256                 # pixels per unit of tangent

COLS = 24                  # sprite width, in sprite pixels
ROWS = 21                  # sprite height, in sprite pixels
BLOCK_BYTES = 63

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

# The last viewport line a hardware sprite may START on (mem.h). Sprite DMA is
# switched off kSpritesOffLead = 22 lines above the panel split, and a sprite
# that has already begun fetching carries on for 21 lines -- 42 if it is
# Y-expanded, which is kSpritesOffLeadExpandY = 43. The sprite stack rejects an
# entry whose last sprite would start below the line; this renderer slides the
# bitmap inside the buffer instead (planes.md section 4).
SPRITE_START_MAX = VIEW_H - 22 - 1          # 89
SPRITE_START_MAX_EXP_Y = VIEW_H - 43 - 1    # 68

# --------------------------------------------------------------------------
# Pixel-size ladder (planes.md section 4)
# --------------------------------------------------------------------------
#
# Chosen from d, the aircraft's unforeshortened diameter in screen pixels --
# a function of distance alone, so the pixel size never changes while the
# aircraft rotates. Each limit is the largest d whose every attitude fits the
# largest layout of that level, less a margin for the rounding of the
# projection; tests/test_planes.py sweeps attitudes to hold that true.

LEVEL_DOT, LEVEL_1X, LEVEL_XEXP, LEVEL_XYEXP = 0, 1, 2, 3
LEVEL_SCALE = {LEVEL_1X: (1, 1), LEVEL_XEXP: (2, 1), LEVEL_XYEXP: (2, 2)}

D_DOT = 3        # at or below: the static dot
D_1X = 21        # at or below: unexpanded, 24 x 42 at most
D_XEXP = 39      # at or below: X-expanded, 48 x 42 at most
D_MAX = 80       # the size cap: X+Y-expanded, 96 x 84 at most

HYSTERESIS = 0.87

# --------------------------------------------------------------------------
# Cycle model -- estimates, not measurements (planes.md section 11)
# --------------------------------------------------------------------------

CYC_POSITION = 940     # relative position, cull, transform, centre and k
CYC_AXES = 900         # two vec_transform_inv for the body axes
CYC_FMUL = 45          # one vec_fastmul8p8
CYC_DIV = 150          # one vec_div8p8
CYC_CACHE_BYTE = 4     # comparing one byte of the cache key
CYC_EDGE = 40          # setting up one polygon edge, slope not included
CYC_SLOPE = 55         # a slope from the reciprocal table: lookup and multiply
CYC_EDGE_ROW = 25      # tracing an edge through one row
CYC_SPAN = 30          # one span: edge masks and the row address
CYC_SPAN_BYTE = 8      # each byte a span writes
CYC_CLEAR_BYTE = 5     # clearing one byte of the back buffer
CYC_SPRITE = 30        # position, pointer and colour of one hardware sprite


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


def smul(a: int, b: int) -> int:
    """a * b / 256 rounded to nearest, from one `vec_fastmul8p8`.

    `vec_fastmul8p8` truncates toward zero, and on a 12 px extent that
    systematically loses up to a whole pixel -- the silhouette rendered ~9%
    small before this was added. Doubling the input and halving the rounded
    result costs a shift and an add. The sign is applied last so that a
    negative model offset rounds exactly like its positive twin, which keeps
    the two wingtips symmetric.
    """
    p = (fmul(2 * abs(a), abs(b)) + 1) >> 1
    return -p if (a < 0) != (b < 0) else p


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
    """|(x, y)| without a square root, within about 3%.

    max(hi, 7/8 hi + 1/2 lo): two shifts, two adds and a compare on the 6502.
    The plain octagonal `hi + lo/2` is up to 12% long, and here it sets the
    fuselage's width -- a body of revolution that must not change width as it
    rotates -- so the second term is worth its three instructions.
    """
    x, y = abs(x), abs(y)
    hi, lo = (x, y) if x > y else (y, x)
    return max(hi, hi - (hi >> 3) + (lo >> 1))


def isqrt_ceil(n: int) -> int:
    """Smallest r with r * r >= n, in integers, so both twins agree."""
    r = math.isqrt(n)
    return r if r * r == n else r + 1


# --------------------------------------------------------------------------
# Vectors and orientation
# --------------------------------------------------------------------------

Vec3 = Tuple[float, float, float]
Vec2 = Tuple[int, int]
Poly = List[Vec2]


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

PARTS = ("wing", "stab", "fin", "body")
ALL_PARTS: FrozenSet[str] = frozenset(PARTS)


@dataclass(frozen=True)
class Model:
    """Aircraft dimensions in metres. Defaults are a Cessna 172.

    Flat surfaces -- wing, tailplane, fin -- are polygons in body space. The
    fuselage is not: it is a body of revolution, given as stations along the
    axis with a radius each, and drawn in screen space (`project_model`).
    """
    span_m: float = 11.0
    length_m: float = 8.3
    nose_frac: float = 0.55        # of length, ahead of the centre
    tail_frac: float = 0.45        # of length, behind it
    wing_fwd_frac: float = 0.20    # wing mid-chord ahead of the centre
    chord_frac: float = 0.14       # wing chord, of span
    stab_span_frac: float = 0.31   # tailplane span, of span
    stab_chord_frac: float = 0.15  # tailplane chord, of length
    fin_frac: float = 0.14         # fin height above the axis, of span
    fin_root_frac: float = 0.20    # fin root chord with the dorsal, of length
    fin_tip_frac: float = 0.05     # fin tip chord, of length
    cabin_frac: float = 0.15       # widest station, of length ahead of centre
    nose_radius_frac: float = 0.045
    cabin_radius_frac: float = 0.075
    tail_radius_frac: float = 0.015
    # Size exaggeration. At true scale a plane is under 4 px beyond 700 m,
    # which is most of any encounter; 1.5x makes traffic readable at realistic
    # separations. The default 11 m span therefore draws as 16.5 m.
    scale: float = 1.5

    def _e(self, metres: float) -> int:
        """Metres to eighths, rounding the magnitude so that the model stays
        symmetric: jround(-66.5) is -66, not -67."""
        v = jround(abs(metres) * EIGHTHS_PER_M * self.scale)
        return -v if metres < 0 else v

    @cached_property
    def geometry(self) -> dict:
        """The model in eighths of a metre, (fore, left, up) about the centre.

        The high wing sits on the cabin's top, at the widest radius. The
        tailplane's trailing edge and the fin's are both on the tail station.
        """
        s, ln = self.span_m, self.length_m
        wf = ln * self.wing_fwd_frac
        c2 = s * self.chord_frac / 2
        h = s / 2
        rc = ln * self.cabin_radius_frac
        t = -ln * self.tail_frac
        sc = ln * self.stab_chord_frac
        hs = s * self.stab_span_frac / 2
        fh = s * self.fin_frac
        e = self._e
        pt = lambda f, l, u: (e(f), e(l), e(u))
        return {
            "wing": [pt(wf + c2, h, rc), pt(wf + c2, -h, rc),
                     pt(wf - c2, -h, rc), pt(wf - c2, h, rc)],
            "stab": [pt(t + sc, hs, 0), pt(t + sc, -hs, 0),
                     pt(t, -hs, 0), pt(t, hs, 0)],
            "fin": [pt(t, 0, 0), pt(t + ln * self.fin_root_frac, 0, 0),
                    pt(t + ln * self.fin_tip_frac, 0, fh), pt(t, 0, fh)],
            "body": [(e(ln * self.nose_frac), e(ln * self.nose_radius_frac)),
                     (e(ln * self.cabin_frac), e(rc)),
                     (e(t), e(ln * self.tail_radius_frac))],
            "hub": (e(wf), 0, 0),
        }

    @cached_property
    def max_radius(self) -> int:
        """Furthest any part of the model reaches from the wing hub, in eighths.

        Every projected point lies within this radius of the projected hub
        whatever the attitude, so it bounds the silhouette in *both* axes at
        once. The fuselage is bounded by a sphere round each station.
        """
        g = self.geometry
        hf = g["hub"][0]
        sq = max((f - hf) ** 2 + l * l + u * u
                 for part in ("wing", "stab", "fin") for f, l, u in g[part])
        return max(isqrt_ceil(sq), max(abs(f - hf) + r for f, r in g["body"]))

    @cached_property
    def k_max(self) -> int:
        """Largest perspective scale: `max_radius * k / 128 <= D_MAX`.

        A constant per aircraft type, so the clamp is one comparison and the
        apparent size does not move as the aircraft rotates -- that stability
        is the whole reason the cap is a constant at all. D_MAX fits the
        largest layout, 96 x 84, on its SHORTER side, so unlike the old
        width-based cap nothing clips at any attitude.
        """
        return (D_MAX * 128) // self.max_radius

    @cached_property
    def projection_cycles(self) -> int:
        """What projecting the model costs on the C64.

        One multiply by k per distinct magnitude, two by the axis per distinct
        (axis, magnitude) pair, two per fuselage station for its offset along
        the normal, and the two divides that make the normal.
        """
        g = self.geometry
        pairs = set()
        for part in ("wing", "stab", "fin"):
            for p in g[part]:
                for axis, v in enumerate(p):
                    if v:
                        pairs.add((axis, abs(v)))
        for f, _ in g["body"]:
            if f:
                pairs.add((0, abs(f)))
        mags = {m for _, m in pairs} | {r for _, r in g["body"]}
        fmuls = len(mags) + 2 * len(pairs) + 2 * len(g["body"])
        return CYC_FMUL * fmuls + 2 * CYC_DIV


# --------------------------------------------------------------------------
# Sprite buffer and rasteriser
# --------------------------------------------------------------------------

class SpriteBuffer:
    """`cols` x `rows` hardware sprites, each a 63-byte block.

    Row y of the buffer is line y % 21 of block row y // 21, and a row is
    3 * cols bytes spread across the blocks of that block row -- which is what
    the C64 rasteriser has to address. The counters feed the cycle estimate.
    """

    def __init__(self, cols: int = 1, rows: int = 1):
        self.cols, self.rows = cols, rows
        self.width, self.height = COLS * cols, ROWS * rows
        self.blocks = [bytearray(BLOCK_BYTES) for _ in range(cols * rows)]
        self.edges = self.slopes = self.edge_rows = 0
        self.spans = self.bytes = 0

    def fill_span(self, y: int, a: int, b: int) -> None:
        """OR pixels a..b of row y. Two 8-entry edge-mask tables and $FF for
        the bytes between them -- 16 bytes of table where the 24-column
        rasteriser needed 144."""
        if y < 0 or y >= self.height:
            return
        if a > b:
            a, b = b, a
        a, b = max(0, a), min(self.width - 1, b)
        if b < a:
            return
        br, line = divmod(y, ROWS)
        i0, i1 = a >> 3, b >> 3
        for i in range(i0, i1 + 1):
            m = 0xFF
            if i == i0:
                m &= 0xFF >> (a & 7)
            if i == i1:
                m &= (0xFF << (7 - (b & 7))) & 0xFF
            bc, byte = divmod(i, 3)
            self.blocks[br * self.cols + bc][3 * line + byte] |= m
        self.spans += 1
        self.bytes += i1 - i0 + 1

    def get(self, x: int, y: int) -> int:
        br, line = divmod(y, ROWS)
        bc, byte = divmod(x >> 3, 3)
        return (self.blocks[br * self.cols + bc][3 * line + byte] >> (7 - (x & 7))) & 1

    def ink(self) -> int:
        return sum(self.get(x, y) for y in range(self.height) for x in range(self.width))

    def rows_as_text(self) -> List[str]:
        return ["".join("#" if self.get(x, y) else "." for x in range(self.width))
                for y in range(self.height)]

    def raster_cycles(self) -> int:
        return (BLOCK_BYTES * self.cols * self.rows * CYC_CLEAR_BYTE
                + self.edges * CYC_EDGE + self.slopes * CYC_SLOPE
                + self.edge_rows * CYC_EDGE_ROW
                + self.spans * CYC_SPAN + self.bytes * CYC_SPAN_BYTE)


# 65536 / dy for every edge height a polygon can have. The size cap bounds the
# bounding box by the diameter d, and every level's d fits 41 buffer rows, so
# no edge is ever taller than that -- and the slope is a lookup and one
# multiply instead of a divide. 42 entries, 84 bytes.
RECIP = [0, 0] + [65536 // dy for dy in range(2, 2 * ROWS)]


def fill_poly(buf: SpriteBuffer, pts: Poly) -> None:
    """Fill a convex polygon, edge-inclusively.

    The same trace `poly.cc` uses for the terrain: every edge contributes the
    whole run it covers in each row, and each row is filled from the leftmost
    to the rightmost of them. So a polygon seen edge-on degrades into a 1 px
    line instead of disappearing, and at range the model falls back to what
    three strokes used to draw.

    Clamping a SPAN to the buffer is an exact clip for a filled polygon, so
    unlike the stroke rasteriser this needs no Liang-Barsky.
    """
    min_y = min(p[1] for p in pts)
    max_y = max(p[1] for p in pts)
    y0, y1 = max(0, min_y), min(buf.height - 1, max_y)
    if y0 > y1 or max(p[0] for p in pts) < 0 or min(p[0] for p in pts) >= buf.width:
        return
    lo = [1 << 30] * buf.height
    hi = [-(1 << 30)] * buf.height

    def put(y, a, b):
        if y0 <= y <= y1:
            lo[y] = min(lo[y], a)
            hi[y] = max(hi[y], b)

    n = len(pts)
    for i in range(n):
        (xa0, ya), (xb0, yb) = pts[i], pts[(i + 1) % n]
        buf.edges += 1
        if ya == yb:
            put(ya, min(xa0, xb0), max(xa0, xb0))
            continue
        if yb < ya:
            xa0, xb0, ya, yb = xb0, xa0, yb, ya
        dy = yb - ya
        if dy > 1:
            buf.slopes += 1
            slope = fmul(xb0 - xa0, RECIP[dy])
        else:
            slope = (xb0 - xa0) * 256
        xa = xa0 * 256
        for y in range(ya, yb + 1):
            if y == yb:
                xa = xb0 * 256
                xb = xa
            else:
                xb = xa + slope
            if y0 <= y <= y1:
                put(y, min(xa, xb) >> 8, max(xa, xb) >> 8)
                buf.edge_rows += 1
            xa = xb
    for y in range(y0, y1 + 1):
        if lo[y] <= hi[y]:
            buf.fill_span(y, lo[y], hi[y])


# The far tier's bitmap: a 2 x 2 blob, built once. Beyond about a kilometre
# there is no silhouette left to draw, and this is the common case by a wide
# margin, so it gets a fixed bitmap: no body axes, no projection, no clear, no
# fill, no pointer flip. On the C64 it is a static block written once at
# startup alongside the other sprite art.
#
# The blob sits on the block's LAST two rows. A static bitmap cannot slide the
# way a rasterised one does (section 4), so this puts the sprite's start as far
# above the dot as it can go: the dot stays drawable down to viewport line 108.
DOT_X, DOT_Y = COLS // 2, ROWS - 2


def _build_dot() -> SpriteBuffer:
    buf = SpriteBuffer()
    buf.fill_span(DOT_Y, DOT_X, DOT_X + 1)
    buf.fill_span(DOT_Y + 1, DOT_X, DOT_X + 1)
    buf.spans = buf.bytes = 0          # costs nothing at runtime
    return buf


DOT_BITMAP = _build_dot()


# --------------------------------------------------------------------------
# Projection
# --------------------------------------------------------------------------

def project_model(model: Model, k: int, centre: Vec2, fore, lat, vert,
                  parts: FrozenSet[str] = ALL_PARTS) -> Tuple[Dict[str, List[Poly]], Vec2]:
    """Every part's polygons in screen pixels, and the projected wing hub.

    First-order projection, as for the old stroke endpoints: each model offset
    becomes pixels through one half-rounded multiply by k, then one multiply
    per screen axis against the 8.8 body axis. It ignores the change in depth
    across the object, which for anything that fits in the buffer is a small
    fraction of its size.
    """
    cx, cy = centre

    def pr(p):
        pf, pl, pu = smul(p[0], k), smul(p[1], k), smul(p[2], k)
        return (cx - fmul(fore[1], pf) - fmul(lat[1], pl) - fmul(vert[1], pu),
                cy - fmul(fore[2], pf) - fmul(lat[2], pl) - fmul(vert[2], pu))

    g = model.geometry
    shapes: Dict[str, List[Poly]] = {}
    for part in ("wing", "stab", "fin"):
        if part in parts:
            shapes[part] = [[pr(p) for p in g[part]]]

    if "body" in parts:
        # A body of revolution looks equally wide from every side, so it is
        # not a 3D polygon: offset each station perpendicular to the
        # PROJECTED axis by its radius -- a radius that depends on distance
        # alone -- and fill the trapezoids between stations.
        st = [(pr((f, 0, 0)), smul(r, k)) for f, r in g["body"]]
        fx = st[0][0][0] - st[-1][0][0]
        fy = st[0][0][1] - st[-1][0][1]
        n = norm2(fx, fy)
        cab = st[0]
        for s in st:
            if s[1] > cab[1]:
                cab = s
        polys: List[Poly] = []
        if n > 0:
            ux, uy = fdiv(-fy, n), fdiv(fx, n)
            off = [(smul(ux, r), smul(uy, r)) for _, r in st]
            for i in range(len(st) - 1):
                (a, oa), (b, ob) = (st[i][0], off[i]), (st[i + 1][0], off[i + 1])
                polys.append([(a[0] + oa[0], a[1] + oa[1]), (b[0] + ob[0], b[1] + ob[1]),
                              (b[0] - ob[0], b[1] - ob[1]), (a[0] - oa[0], a[1] - oa[1])])
        # End-on the axis collapses and the trapezoids with it; what is left
        # is the cabin's cross-section, a disc. Skipped once the axis is long
        # enough to hide it.
        if n < 4 * cab[1]:
            r = max(1, cab[1])
            h = (r * 106) >> 8
            x, y = cab[0]
            polys.append([(x + r, y + h), (x + h, y + r), (x - h, y + r), (x - r, y + h),
                          (x - r, y - h), (x - h, y - r), (x + h, y - r), (x + r, y - h)])
        shapes["body"] = polys

    return shapes, pr(g["hub"])


# --------------------------------------------------------------------------
# Level and layout, both with hysteresis
# --------------------------------------------------------------------------

@dataclass
class Tier:
    level: int
    xs: int = 1
    ys: int = 1
    cols: int = 1
    rows: int = 1

    @property
    def dot(self) -> bool:
        return self.level == LEVEL_DOT

    @property
    def sprites(self) -> int:
        return self.cols * self.rows

    @property
    def name(self) -> str:
        if self.dot:
            return "dot"
        px = {1: "1:1", 2: "X-exp", 3: "X+Y-exp"}[self.level]
        return "%dx%d, %s" % (self.cols, self.rows, px)


@dataclass
class State:
    """Everything that persists between frames: the hysteresis latches and the
    vertex cache. One instance per tracked aircraft."""
    level: int = LEVEL_DOT
    cols: int = 1
    rows: int = 1
    cache_key: Optional[tuple] = None
    cache_buf: Optional[SpriteBuffer] = None


def _ladder(d: float, limits) -> int:
    for i, lim in enumerate(limits):
        if d <= lim:
            return i
    return len(limits)


LIMITS = (D_DOT, D_1X, D_XEXP)


def pick_level(prev: int, d: int) -> int:
    """The pixel size, from the unforeshortened diameter alone.

    Promote past a limit, demote below 87% of it -- and a demotion lands on
    the level the demotion thresholds give, however far that is, rather than
    being held at `prev` as the old thickness latch was.
    """
    up = _ladder(d, LIMITS)
    down = _ladder(d, tuple(HYSTERESIS * lim for lim in LIMITS))
    return min(max(prev, up), down)


def pick_layout(state: State, level: int, bw: int, bh: int) -> Tier:
    """How many sprites, from the bounding box.

    Invisible -- only the sprite count changes, never the pixels -- so it may
    follow the attitude freely. The hysteresis is only there so that the
    count does not flicker and push other objects in and out of the stack.
    """
    xs, ys = LEVEL_SCALE[level]
    same = state.level == level
    cols = 1 if bw <= xs * (COLS - 1) else 2
    if same and state.cols == 2 and cols == 1 and bw > HYSTERESIS * xs * (COLS - 1):
        cols = 2
    rows = 1 if bh <= ys * (ROWS - 1) else 2
    if same and state.rows == 2 and rows == 1 and bh > HYSTERESIS * ys * (ROWS - 1):
        rows = 2
    return Tier(level=level, xs=xs, ys=ys, cols=cols, rows=rows)


# --------------------------------------------------------------------------
# The pipeline
# --------------------------------------------------------------------------

@dataclass
class Result:
    visible: bool
    reason: str = ""
    cam: Tuple[int, int, int] = (0, 0, 0)
    k: int = 0
    d: int = 0
    centre: Vec2 = (0, 0)
    shapes: Dict[str, List[Poly]] = field(default_factory=dict)   # screen px
    local: Dict[str, List[Poly]] = field(default_factory=dict)    # buffer px
    bbox: Tuple[int, int] = (0, 0)
    tier: Optional[Tier] = None
    origin: Vec2 = (0, 0)
    slid: int = 0            # lines the bitmap was moved up inside the buffer
    fits: bool = True
    buf: Optional[SpriteBuffer] = None
    cached: bool = False
    cycles: int = 0
    clamped: bool = False    # apparent size held to keep the whole plane in frame

    def signature(self) -> str:
        """Everything the two twins must agree on, as one string."""
        if not self.visible:
            return "invisible:" + self.reason
        blocks = "".join(b.hex() for b in self.buf.blocks)
        return "%d|%dx%d|%d,%d|%d|%d|%d|%s" % (
            self.tier.level, self.tier.cols, self.tier.rows,
            self.origin[0], self.origin[1], self.slid, int(self.clamped),
            self.cycles, blocks)


def render(state: State, cam: Mat3, target: Mat3, rel_pos_m: Vec3,
           model: Model = Model(), parts: FrozenSet[str] = ALL_PARTS) -> Result:
    """One aircraft, one frame. `rel_pos_m` is target minus eye, in metres."""

    # 1. to render units (quarter metres), int16
    pu = tuple(jround(c / RENDER_UNIT_M) for c in rel_pos_m)
    # 3. camera space
    c = tuple(jround(v) for v in to_cam(cam, pu))

    if c[0] <= MIN_CAM_X:
        return Result(visible=False, reason="behind camera", cam=c)
    if c[0] > MAX_CAM_X:
        return Result(visible=False, reason="out of range", cam=c)

    # 5. centre, 6. perspective scale: px = 256 * (O/8) / (C.x/4) = O * k / 256
    cx = CX0 - fdiv(c[1], c[0])
    cy = CY0 - fdiv(c[2], c[0])
    k = fdiv(128, c[0])

    # 6b. Size clamp. Closer than about 55 m the silhouette would outgrow the
    # largest layout. Hold the apparent size instead: cap k, which is exactly
    # pretending the target stopped approaching. The cap is a constant of the
    # model, so the aircraft never changes size as it rotates.
    clamped = k > model.k_max
    if clamped:
        k = model.k_max

    # 7. pixel size, from distance alone -- before the body axes, so the far
    # tier never pays for them
    d = (model.max_radius * k) // 128
    level = pick_level(state.level, d)

    if level == LEVEL_DOT:
        state.level, state.cols, state.rows = LEVEL_DOT, 1, 1
        state.cache_key, state.cache_buf = None, None
        origin = (cx - DOT_X, cy - DOT_Y)
        if origin[1] > SPRITE_START_MAX:
            return Result(visible=False, reason="below the sprite cut", cam=c)
        return Result(visible=True, cam=c, k=k, d=d, centre=(cx, cy),
                      tier=Tier(LEVEL_DOT), origin=origin, buf=DOT_BITMAP,
                      clamped=clamped, cycles=CYC_POSITION + CYC_SPRITE)

    # 8. body axes in camera space, 9. the polygons in screen pixels
    fore, lat, vert = (q88(to_cam(cam, target.front)),
                       q88(to_cam(cam, target.left)),
                       q88(to_cam(cam, target.up)))
    shapes, hub = project_model(model, k, (cx, cy), fore, lat, vert, parts)
    pts = [p for polys in shapes.values() for poly in polys for p in poly]
    minx, maxx = min(p[0] for p in pts), max(p[0] for p in pts)
    miny, maxy = min(p[1] for p in pts), max(p[1] for p in pts)
    bw, bh = maxx - minx, maxy - miny

    # 10. layout, then the centring anchor
    tier = pick_layout(state, level, bw, bh)
    state.level, state.cols, state.rows = level, tier.cols, tier.rows
    xs, ys = tier.xs, tier.ys
    wpx, hpx = COLS * tier.cols * xs, ROWS * tier.rows * ys
    # The largest extent n sprites hold is one less than their size -- an
    # extent of 20 spans 21 pixels -- and the floor division onto expanded
    # pixels costs one more screen pixel per expansion.
    fits = bw <= xs * (COLS * tier.cols - 1) and bh <= ys * (ROWS * tier.rows - 1)
    # Centre on the bounding box while the silhouette fits. Once it does not,
    # centre on the wing hub: the wing carries the shape, so the fuselage is
    # what should run off the ends. With the constant cap this is a guard.
    anchor = (jround((minx + maxx) / 2), jround((miny + maxy) / 2)) if fits else hub
    ox, oy = anchor[0] - (wpx >> 1), anchor[1] - (hpx >> 1)

    # 11. Slide, don't reject. The last sprite row has to start on or above
    # the DMA cut. Rather than dropping the entry, move the buffer up and draw
    # the aircraft lower inside it; what falls off the bottom of the buffer is
    # at most the viewport's last two lines.
    cut = SPRITE_START_MAX_EXP_Y if ys == 2 else SPRITE_START_MAX
    slid = max(0, oy + (tier.rows - 1) * ROWS * ys - cut)
    oy -= slid

    # Buffer coordinates FLOOR: an expanded pixel covers screen pixels 2c and
    # 2c + 1, so floor is what the hardware does.
    loc = lambda p: ((p[0] - ox) // xs, (p[1] - oy) // ys)
    local = {name: [[loc(p) for p in poly] for poly in polys]
             for name, polys in shapes.items()}
    nverts = sum(len(poly) for polys in local.values() for poly in polys)

    # 12. cache on the local vertices and the layout
    key = (level, tier.cols, tier.rows,
           tuple((name, tuple(tuple(poly) for poly in polys))
                 for name, polys in local.items()))
    cached = state.cache_key == key and state.cache_buf is not None
    cycles = (CYC_POSITION + CYC_AXES + model.projection_cycles
              + CYC_CACHE_BYTE * 2 * nverts + CYC_SPRITE * tier.sprites)

    # 13. fill into the back buffer
    if cached:
        buf = state.cache_buf
    else:
        buf = SpriteBuffer(tier.cols, tier.rows)
        for polys in local.values():
            for poly in polys:
                fill_poly(buf, poly)
        state.cache_key, state.cache_buf = key, buf
        cycles += buf.raster_cycles()

    return Result(visible=True, cam=c, k=k, d=d, centre=(cx, cy), shapes=shapes,
                  local=local, bbox=(bw, bh), tier=tier, origin=(ox, oy), slid=slid,
                  fits=fits, buf=buf, cached=cached, cycles=cycles, clamped=clamped)


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


def fnv1a(s: str) -> int:
    """32-bit FNV-1a, for comparing signatures with the JavaScript twin."""
    h = 0x811C9DC5
    for ch in s.encode("ascii"):
        h = ((h ^ ch) * 0x01000193) & 0xFFFFFFFF
    return h


def twin_hashes() -> Dict[str, str]:
    """One hash per block of a fixed sweep, over every signature in the block.

    `twinHashes()` in `docs/planes-prototype.html` runs the same sweep; paste
    its output into tests/test_planes.py's TestTwin to pin the two together.
    Stateless cases over distance, heading, bank, pitch and position for two
    models, then two stateful approaches that exercise the hysteresis and the
    cache.
    """
    out: Dict[str, str] = {}
    level = orient(0, 0, 0)
    add = lambda name, sigs: out.__setitem__(name, "%08x" % fnv1a("\n".join(sigs)))
    models = [("default", Model()),
              ("big", Model(span_m=15.5, length_m=12.2, scale=2.0, wing_fwd_frac=0.1))]
    for mname, model in models:
        for dist in (18, 30, 55, 80, 110, 150, 210, 300, 600, 1100, 1500):
            sigs = []
            for h in range(-180, 180, 30):
                for b in (0, 30, 60, 89, -45):
                    for p in (-30, 0, 30):
                        for be, el in ((0, 0), (8, 4), (-12, -9)):
                            sigs.append(render(State(), level, orient(h, p, b),
                                               place(dist, be, el), model).signature())
            add("%s %d m" % (mname, dist), sigs)
    for name, cam in (("approach, level", level), ("approach, banked", orient(0, 5, 15))):
        state, sigs = State(), []
        for dist in range(1500, 19, -7):
            sigs.append(render(state, cam, orient(150, 5, 25), place(dist, 3, -2)).signature())
        add(name, sigs)
    return out
