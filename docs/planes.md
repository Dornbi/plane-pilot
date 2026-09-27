# Other Aircraft — Traffic Sprites

**Status: designed, not built.** None of §12's phases has landed; there is no
traffic in `ppilot.prg`. The layer underneath it did ship, though — the sprite
stack of [sprite_objects.md](sprite_objects.md) §2 exists in `c64o/sprites.cc`
and serves the sun and the clouds, so §5's "hardware sprite indices" is a
matter of calling `sprites_stack_add()` rather than of writing an allocator —
once the stack has learnt the two things §5 lists.

**Revised: polygons and 2 × 2 X+Y sprites.** The first version of this
document drew an aircraft as three thick strokes in at most two X-expanded
sprites. It now fills a handful of polygons into up to four, and expands along
Y as well as X when a close aircraft is too tall for two sprites. What changed and why is §1; the
numbers throughout are the new design's.

This document specifies how other aircraft are drawn in the viewport. Scope is
**graphics only**: the rendering pipeline, sprite and RAM allocation, the
rasteriser and the caching scheme. Traffic behaviour (canned kinematic paths),
collision and the map screen are named where they touch the renderer but are
not designed here.

### Two reference implementations

| | |
| :--- | :--- |
| [planes-prototype.html](planes-prototype.html) | Interactive. Sliders for range, attitude and the model; the viewport, the sprite blocks and a gallery of attitudes; a 10 Hz approach and flyby. For deciding how things should look. The previous stroke renderer is kept in it as a comparison mode and nowhere else. |
| [`lib/planes.py`](../lib/planes.py) | The same pipeline in Python, covered by [`tests/test_planes.py`](../tests/test_planes.py) (`make test`). For deciding whether they are still right. |

Both run the arithmetic the C64 runs — `fmul` and `fdiv` reproduce
`vec_fastmul8p8` and `vec_div8p8` including their truncation toward zero — so
the bytes they produce are the bytes `ppilot` should produce. They agree bit
for bit over 12,304 frames — 11,880 stateless ones spanning distance, heading,
bank, pitch and position for two models, and two 212-frame approaches through
the hysteresis and the cache — covering every bitmap byte, the level, layout,
origin, slide, clamp and cycle count. The prototype's `twinHashes()` prints one
hash per block of that sweep, and `TestTwin` pins them, so the agreement is
checked by `make test` rather than remembered.

That cross-check has earned its keep before. The first one found 154
mismatched bitmaps caused by nothing more than Python's `round` being banker's
rounding while JavaScript's `Math.round` is not — a discrepancy that would
otherwise have sat undetected in whichever of the two the C64 was written from.

### Relationship to `sprite_objects.md`

[sprite_objects.md](sprite_objects.md) is the layer underneath this document:
the general scheme for putting *any* world object — clouds, aircraft,
projectiles — into the eight hardware sprites, covering the raster band split,
index sharing with the instrument panel, distance sorting and the candidate
scan. **That document owns the sprite engine; this one owns what goes in an
aircraft's bitmap.** Everything here is built on its §1 and §2.

That engine is now real: `sprites_stack_reset()` / `_add()` / `_commit()` in
`c64o/sprites.cc`, designed in detail in [clouds.md](clouds.md) §1 and covered
by `c64o/test/sprites_test.cc`. An aircraft becomes a third client of it — one
`sprites_stack_add()` per plane, with the depth doing the priority work §5
spells out. Three consequences: the stack hands out seven indices rather than
eight ([clouds.md](clouds.md) §1.9 keeps index 7 for the panel band and the
orientation mark); it does not yet wrap a sprite round the left edge (§1.6
there), so a plane leaving the left of the viewport will pop the way a cloud
does; and it has to learn entries two sprites wide and Y-expanded (§5).

Three places where this document supersedes or corrects it:

| `sprite_objects.md` | Here |
| :--- | :--- |
| §0 "never expand along Y", as an absolute rule | Near traffic is the exception: Y as well as X, when a close aircraft is too tall for two sprites — steeply banked, inside ~100 m (§4). Clouds are not. |
| §6.2 proposes 6–8 pre-rendered airframe bitmaps | Superseded — aircraft carry one 64-byte block, the far-tier dot (§1). |
| §5 measures 8 objects against a 19,705-cycle PAL frame and concludes it does not fit | Wrong denominator; the pipeline runs once per *sim* frame, ~98,500 cycles (§11). |

---

## 1. The core idea

Pre-rendered sprites are out: a plane's appearance depends on two viewing
angles plus size, and the combinations do not fit in RAM. But the sprite does
not need to be looked up, because **the silhouette is a few flat plates and a
tube, and every vertex comes straight out of the existing 3D math**.

The **flat surfaces** — wing, tailplane, fin — are polygons in body space.
Project their corners the way any point is projected and fill them. The
**fuselage** is not a polygon at all: it is a body of revolution, and its
silhouette is built in screen space from the projected axis and a radius per
station (§3). Everything is one colour and is ORed into the buffer, so there is
no hidden-surface work of any kind — no depth sort, no back-face test, no draw
order.

Perspective, foreshortening, bank and aspect all fall out of the projection.
There is no angle quantisation and no orientation table. The **only** static
bitmap in the whole feature is a single 64-byte block: the 2 × 2 dot the far
tier uses, where there is no silhouette left to draw (§4).

The feature is therefore code plus scratch RAM, not art.

### What changed from the stroke design, and why

The first version drew three strokes — nose→tail, tip→tip, tail→fin — each
thickened by a 1 / 2 / 4 pixel ladder, into at most 1 × 2 X-expanded sprites
that froze at 46 px from about 93 m. Three things moved it:

- **Strokes read as a cross.** From above, from below and banked, three lines
  are a `+` with an offset, and at knife-edge a bare `+`. Filled plates show
  the planform, and the tailplane — which the strokes never had — is what
  makes a top view unmistakably an aeroplane. The prototype's gallery shows the
  two side by side at ten attitudes.
- **Most of the stroke design was approximating a fill.** The thickness
  machinery — the 1 / 2 / 4 ladder and its even-only rule, the screen-space
  steep/shallow test, the projected-chord estimate, three hysteresis latches
  and the 2 → 4 weight pop — existed to make a line look like a plate. A
  filled polygon *is* the plate, so its projected chord comes out exact and all
  of that goes, along with the Liang–Barsky clipper (§6).
- **Filled shapes can take Y-expansion; lines cannot.** The argument against
  expanding along Y was that a near-horizontal stroke turns into a two-line
  staircase. A silhouette whose weight is carried by its area does not, and a
  2 × 2 sprite pixel is exactly the terrain's 2 × 2 dot. Y-expansion is also
  the only way four sprites hold more than 42 lines, so it is what lets the
  aircraft freeze at **79 px from 54 m** instead of 46 px from 93 m (§4).

The price is cycles: roughly twice the stroke design's at close range (§11).

### Hard constraints

These apply to every sprite in the viewport, aircraft or not, unless noted:

- **Hires only. Never multicolour.** One colour per sprite, full horizontal
  resolution.
- **Y-expansion only together with X, and only when a close aircraft is too
  tall for two sprites.** Clouds and the sun never expand along Y; `$D017`
  carries traffic bits only then (§4), and for the back view's fin, which
  predates this.
- **One fixed colour per aircraft**, chosen once — no background-dependent
  switching (§8).

---

## 2. Scale — how big is a plane, actually

`vec_project()` returns `vec_sx = 256·y/x`, and `gfx.cc` uses it as a pixel
offset directly (`px = 160 − vec_sx`). So the angular scale is **256 pixels per
unit of tangent**, the viewport half-width of 160 px is `tan = 0.625`, and the
horizontal field of view is ~64°.

An object of size `S` at distance `D` therefore spans

```
pixels = 256 · S / D
```

**Traffic is drawn 1.5× oversize**, so an 11 m Cessna spans 16.5 m on screen.
At true scale a plane is under 4 px beyond 700 m, which is most of any
encounter — it would be a dot for the whole approach and only become an
aircraft in the last two seconds. The exaggeration buys back a factor of 1.5 in
every distance below, at the cost of realism, and traffic that cannot be
identified is not worth drawing.

| Span on screen | Distance | Time to close at 50 m/s |
| ---: | ---: | ---: |
| 1 px  | 4224 m | past the 4 km cull |
| 4 px  | 1056 m | 21 s |
| 24 px |  176 m | 3.5 s |
| 48 px |   88 m | 1.8 s |
| 78 px |   54 m | 1.1 s |

Below about **54 m** the aircraft stops growing, and beyond about a kilometre
it is a fixed dot — both in §4. The band where the silhouette actually changes
shape is therefore roughly 55 m to 1 km, and **the dot is still the common
case**, which is why it gets a static bitmap rather than a rasterised one.

### Traffic does not use the terrain's units

`flight_eye_*` is 24.8 fixed point in metres and `world.cc` down-shifts by 9,
so the terrain grid works in **2 m units**. Traffic must not: the grid needs
kilometres of range and can afford metres of slop, while an aircraft a hundred
metres away needs the opposite.

At 100 m and a few degrees off the nose, a lateral offset is only a handful of
2 m units, so one unit of rounding is a large fraction of it — and the
projected centre is a ratio, so the error lands directly on screen. Measured
over a closing sweep from 400 m to 60 m:

| Relative-position unit | Worst frame-to-frame jump of the projected centre |
| :--- | ---: |
| 2 m, as the terrain grid uses | 7 px |
| **¼ m** | **1 px** |

A quarter metre reaches 4 km inside an int16 (16,000 units), which is beyond
the range cull, so it costs nothing but a different shift: `>> 6` instead of
`>> 9`. Since the pixel formula is a ratio of two lengths it is otherwise
unaffected.

---

## 3. Pipeline

Per plane, per frame:

1. **Relative position.** `P = target_world − flight_eye`, three 32-bit
   subtractions, then `>> 6` to **quarter-metre** units (int16) — not the
   terrain's `>> 9`. See §2.
2. **Cheap world-space reject** before spending a transform, per
   [sprite_objects.md](sprite_objects.md) §5.1: sign of `front · P` plus a
   Manhattan distance bound, roughly three multiplies.
3. **To camera space.** `vec_transform_inv(&world_cam, &P, &C)` — 9 multiplies,
   already exists.
4. **Cull.** `C.x <= 64` (16 m — behind or on top of the camera) → skip.
   Range cull at `C.x > 16000` (4 km) → skip. Cull if the sprite would land
   in the message band while a message is up
   ([sprite_objects.md](sprite_objects.md) §7).
5. **Centre.** `vec_project_nocull()` → `cx = 160 − vec_sx`,
   `cy = 56 − vec_sy`.
6. **Perspective scale.** `k = 32768 / C.x` via `vec_div8p8(128, C.x)`. Model
   offsets are in eighths of a metre and `C.x` in quarters, so
   `px = 256·(O/8)/(C.x/4) = O·k/256`. Then **clamp**: `k = min(k, kMax)`
   — see §4.
7. **Pixel size.** `d = R·k/128`, the aircraft's unforeshortened diameter in
   pixels, picks the level of §4. **The dot level stops here**: it needs no
   body axes and no projection, only the static block and step 13.
8. **Body axes in camera space.** `vec_transform3_inv(&world_cam, &R)` on the
   target's orientation — 27 multiplies, one existing call — giving `front`,
   `left` and `up` in camera space.
9. **Vertices.** Every model magnitude becomes pixels through one
   half-rounded multiply by `k`, then one multiply per screen axis against the
   8.8 body axis:

   ```
   px(e)  = sign(e) · ((vec_fastmul8p8(2·|e|, k) + 1) >> 1)
   vertex = (cx − fmul(fore.y, px(f)) − fmul(left.y, px(l)) − fmul(up.y, px(u)),
             cy − fmul(fore.z, px(f)) − fmul(left.z, px(l)) − fmul(up.z, px(u)))
   ```

   Products are shared: each distinct magnitude is multiplied by `k` once and
   each distinct (axis, magnitude) pair once per screen axis. The Cessna below
   has 16 magnitudes and 15 pairs, which with the fuselage's six is 52
   multiplies and two divides.

   **The doubling is not decoration.** `vec_fastmul8p8` truncates toward zero,
   and on a 12 px half-span that systematically loses up to a whole pixel — the
   prototype measured the silhouette rendering ~9% small before this was added.
   The sign is applied last so that a negative offset rounds exactly like its
   positive twin, and the two wingtips stay symmetric.

   This is the first-order approximation — it ignores the change in `C.x`
   across the object. Anything that fits in the buffer is small against its
   distance, and it removes the near-plane clipping problem entirely.
10. **Y-expansion, layout and placement** from the bounding box of every
    vertex (§4): Y-expansion if the silhouette is too tall for two sprites,
    the sprite count, the centring anchor, and the slide above the DMA cut.
11. **Cache check** on the local vertex bytes (§7). Hit → skip to 13.
12. **Fill** into the back buffer: clear, fill each polygon (§6), flip.
13. **Program the sprite(s)**: position, `$D010` MSB, `$D01D` and `$D017`
    expansion, colour (§8), pointer in both screen RAM copies.

### The model

A Cessna 172's size — 11 m span, 8.3 m long — with a low, lightly tapered
wing, which is the shape of a Piper Warrior. In eighths of a metre at the 1.5×
exaggeration, about the fuselage centre — `(fore, left, up)`:

| Part | Vertices | From |
| :--- | :--- | :--- |
| Wing | tips `(27, ±66, −5)`, `(13, ±66, −5)`; root `(30, 0, −5)`, `(9, 0, −5)` | root chord 0.16 · span, tip 0.7 of the root, mid-chord 0.20 · length ahead of centre, just inside the fuselage's underside |
| Tailplane | tips `(−32, ±20, 0)`, `(−43, ±20, 0)`; root `(−30, 0, 0)`, `(−45, 0, 0)` | span 0.31 · span, root chord 0.15 · length, tip 0.7 of the root, root trailing edge on the tail |
| Fin | `(−45, 0, 0)`, `(−25, 0, 0)`, `(−40, 0, 18)`, `(−45, 0, 18)` | height 0.14 · span; root chord with the dorsal 0.20 · length, tip 0.05 |
| Fuselage | stations `55` r `6`, `5` r `6`, `−45` r `1` | nose 0.55 · length ahead of centre, tail cone from 0.05 ahead, tail 0.45 behind; radius 0.06 · length, 0.015 at the tail |

**The wing and the tailplane are trapezoids**, tapered evenly on both edges
about their mid-chord, so each is one convex hexagon — tip, root, tip along
the leading edge and back along the trailing edge. `wing_taper` and
`stab_taper` are the tip chord as a fraction of the root; the prototype has a
slider for each. The root vertices are mostly hidden by the fuselage, but they
cost two edges per surface over a rectangle.

**The wing sits 20% of the length ahead of the centre.** A wing centred on the
fuselage reads as a plus sign rather than an aeroplane; moving it forward is
what puts a tail on the shape. It is only visible from three-quarter angles —
head-on the fuselage is foreshortened to nothing and side-on the wing is — but
those angles are most of an encounter. The **wing hub**, `(20, 0, 0)`, is also
the point the size cap is measured from and the anchor of last resort (§6).

**The low wing sits just inside the fuselage's underside**, 0.8 of its
radius below the axis, so that head-on the fuselage and tailplane stand on it.
Exactly on the underside was tried first: side-on, the edge-on wing then landed
a row below the fuselage, where it read as something slung under the aircraft.
Just inside, it is hidden there, and the test holds it hidden. `wing_height` is
the offset in fuselage radii — +0.8 makes a high wing — and the prototype can
switch it.

### The fuselage is a tube, not a polygon

A fuselage looks the same width from every side; a polygon model of it would
need many faces to say so. Instead:

1. Project each station's point on the axis.
2. Take the projected axis `F = nose − tail` and its length with `norm2`,
   `max(hi, 7/8·hi + 1/2·lo)` — within 3%, no square root.
3. Unit normal `n = (−F.y, F.x) / |F|`: two `vec_div8p8`.
4. Offset each station by `±n · r`, with `r = px(radius)` — a function of
   distance alone.
5. Fill the outline — up one side from nose to tail, back down the other — as
   one polygon.

**Three stations: a cylinder with a tail cone.** The radius holds from the nose
to just behind the wing and then falls to the tail — the simplest outline that
reads as a fuselage. It is convex, and its belly is a flat edge.

**There is no cabin.** One was tried, as a widest station part-way along the
fuselage, and dropped. On a tube a cabin bulges down as far as up, so it read
as something under the belly. Putting it on the roof, where it belongs, meant a
side profile at every station, for a hump of a pixel or two. The first version
of it also exposed the fill rule's lone-pixel corners, which §6 fixes.

End-on, `F` collapses and the outline with it; what should be visible is the
fuselage's cross-section. So while `|F| < 4r` the widest station is also
filled as an octagon of radius `r`. Side-on the octagon would sit inside the outline, so it
is skipped. The octagon's short offset, `0.414·r`, rounds **up**: rounded down,
a disc of radius one or two is a diamond with a lone pixel on each point —
the same dot, from the other direction.

The width therefore does not change as the aircraft rotates — the test sweeps
every heading, bank and pitch and finds the nose's half-width within a pixel
of `r`. This is the old rule "the fuselage is a body of revolution, drive it
by distance alone" (§6 of the stroke design) turned into geometry. The plain
octagonal norm, `hi + lo/2`, is up to 12% long, which would have narrowed the
fuselage by that much at some angles; the second term costs three
instructions.

### Why ⅛ m model units

A Cessna's half-span is 5.5 m. In 2 m render units that is 2.75 — rounding it
to 3 is a 9% error in wingspan. Storing model offsets in ⅛ m keeps the half
span at 66 even at 1.5×, comfortably int8, and `k` absorbs the unit change.

Check: `C.x = 400` quarter-metres (100 m) → `k = 32768/400 = 81`;
`(fmul(132, 81) + 1) >> 1 = 21` px half-span, 42 px across. Direct formula:
`256 · 16.5 / 100 = 42`. ✓

---

## 4. Pixel size and layout

**The horizontal pixel size comes from the distance. Y-expansion comes from
the silhouette's height, as late as it can. The layout comes from the bounding
box.** Three decisions, made independently.

### Horizontal: from distance alone

`d = R·k/128`, where `R` is the furthest any part of the model reaches from
the wing hub (68 eighths for the default model — the top of the fin). Every
projected vertex lies within `R·k/256` pixels of the projected hub whatever the
attitude, so `d` bounds the silhouette in both axes at once, and it is a
function of distance and the model only.

| Level | `d` | Default model at 1.5× | Pixel | Layouts | Sprites |
| :--- | ---: | :--- | :--- | :--- | :---: |
| dot | ≤ 3 | beyond 1,024 m | — | the static block | 1 |
| 1:1 | ≤ 21 | 195 m – 1 km | 1 × 1 | 1 × 1, 1 × 2 | 1–2 |
| X | ≤ 80 | inside 195 m, frozen inside 54 m | 2 × 1, or 2 × 2 when tall | 1 × 1 up to 2 × 2 | 1–4 |

Those are the distances going in. Coming out, each level is held until `d`
falls below 87% of its limit — 228 m and 1,366 m.

**Why distance and not the box.** The stroke design picked X-expansion from
the bounding-box width, so the pixel size changed while the aircraft rotated:
at 120 m and 10° off nose-on it was X-expanded up to 40° of bank and 1:1 from
50°. That is the artefact the stroke design had already written a rule against
for line weight — "a bbox-driven ladder changes stroke weight while the
aircraft rotates, which reads as a glitch" — applied to the one thing it had
missed. With `d` the horizontal pixel size changes exactly once per approach,
at a fixed distance, whatever the attitude.

**Why this limit.** `D_1X` is the largest `d` for which *every* attitude fits
1:1's largest layout, 24 × 42 screen pixels, less two pixels, because rounding
can push a vertex one pixel past `R·k/256` (measured: never more than one).
X-expansion could come in further out, but there is nothing to buy: at 1:1 the
aircraft already fits one sprite wide.

### Vertical: only when it has to

Two unexpanded sprites hold an extent of 41 lines. **Y-expansion comes in only
when the silhouette is taller than that**, and is held until the height falls
below 87% of it. Until then the vertical resolution is the full one line per
pixel — which is the axis that matters: a near-horizontal wing or fuselage edge
is read by its vertical placement, and Y-expansion turns it into a two-line
staircase.

How late that is, for the default model, at the worst heading:

| Attitude | Y-expanded from | Height at the size cap |
| :--- | ---: | ---: |
| Level, pitched to 30°, seen from 20° above or below | never | 15–31 lines |
| Banked 30° | never | 38 lines |
| Banked 40° | 64 m | 50 lines |
| Banked 60° | 85 m | 66 lines |
| Knife-edge | 102 m | 78 lines |

Traffic in level flight, climbing, descending or in a gentle turn is never
Y-expanded, however close it comes.

**The price is the one the horizontal rule avoids.** Y-expansion follows the
attitude, so a close aircraft rolling past ~35–40° of bank changes vertical
resolution mid-roll, and rolling back out changes it again (with the
hysteresis, below 35 lines). That is a deliberate exception: expanding by
distance, as the first version of this document did, put every aircraft
inside 107 m on two-line pixels whatever it was doing, to cover the knife-edge
that almost never happens. It also costs sprites and cycles: an aircraft 21–41
lines tall and wider than 46 pixels needs 2 × 2 unexpanded where it would have
fitted 2 × 1 expanded — at 40 m, a 15–30° bank takes four sprites at two
headings in three — and an unexpanded silhouette fills twice the rows (§11).

**Nothing clips, at any attitude.** The size cap below is taken from the
largest layout's shorter side, 84 lines; Y-expansion comes in before two
unexpanded rows overflow; and 1:1 holds every attitude up to `D_1X`. The
stroke design capped on the buffer's width and let the wingtips clip past 73°
of bank. The test sweeps every heading, bank to ±90° and pitch to ±60° at every
level and finds every vertex inside the buffer.

**Why Y-expansion at all.** Without it four sprites are at most 42 lines tall,
and a cap that has to hold at every attitude is bounded by the shorter side —
the wingspan can point either way. The first version of this document costed
exactly that: 2 × 2 unexpanded gains nothing, and switching layout by attitude
reaches 60 px. With Y-expansion available for tall silhouettes the same four
sprites reach 96 × 84, and the cap is 79 px.

### Layout: from the box, and invisible

```
cols = (bbox_w <= xs · 23) ? 1 : 2
rows = (bbox_h <= ys · 20) ? 1 : 2
```

`n` sprites hold an extent one less than their size — an extent of 20 spans
21 pixels — and the floor onto an expanded pixel costs one more screen pixel
per expansion, hence `xs · (24·cols − 1)` and `ys · (21·rows − 1)`.

A level plane is wide and flat and gets a row; a knife-edge one is tall and
narrow and gets a column; a banked one close in gets all four. The layout is
**invisible**: the anchor is the bounding box's centre and every layout's
width and height in screen pixels is even at the levels where it matters, so
changing the layout moves no pixel. The test holds a bigger layout through the
hysteresis and compares the lit screen pixels with a fresh one's. Its
hysteresis (the same 87%) is there only so that the sprite count does not
flicker and push other objects in and out of the stack.

At the cap, level flight at any heading needs two sprites; a moderate bank or
a steep pitch needs four. Nothing needs more than two until `d` passes 39,
inside ~112 m.

### The DMA cut: slide, don't reject

Sprite DMA is switched off 22 lines above the panel split, and a sprite that
has already begun fetching carries on for 21 lines — 42 if it is Y-expanded
(`mem.h`, `kSpritesOffLead` and `kSpritesOffLeadExpandY`). So the last viewport
line a sprite may **start** on is 89, or **68** if it is Y-expanded.

`sprites_stack_add()` handles this by testing the *last* hardware sprite of an
entry and rejecting the entry if it starts too low: better than drawing a cloud
with its lower half missing. A traffic bitmap is rasterised every frame, so it
can do better: **clamp the sprite's Y so that the last sprite row starts on the
cut, and draw the aircraft lower inside the buffer.** The bottom of the buffer
then sits at line 110 exactly — 89 + 21, or 68 + 42 — so what is lost is the
viewport's last two lines, and everything below the viewport, which was never
visible anyway.

This also fixes the stroke design, which would have gone through the stack's
rejection: at 70 m and 10° below the eye line — centre on line 101, top half
plainly on screen — its 1 × 2 entry was dropped whole.

The static dot cannot slide, so its blob sits on the block's **last two
rows**: the sprite then starts 19 lines above the dot, and the dot stays
drawable down to line 108.

### The size cap

There is no layout past 2 × 2, and cropping an aircraft that outgrows it shows
the *middle* of an aeroplane rather than an aeroplane. So instead of cropping,
**hold the apparent size**: cap `k`, which is exactly pretending the target
stopped approaching. Everything downstream follows `k`, so the whole silhouette
freezes together with no special cases.

```
kMax = 80 · 128 / R            // 150 for the default model: d = 79 px, from 54 m in
```

**Constant, not bounding-box-derived.** Scaling to the box fills the buffer
better, but a box factor changes with attitude, so the aircraft **changes size
as it rotates**, which reads as breathing rather than as a size limit. With the
constant cap the test finds exactly one scale at every range inside it,
whatever the attitude.

The exaggeration moves the freeze point with it: at true scale the same cap
would engage from ~35 m. If the freeze feels too early, the lever is the
exaggeration, not the cap.

### Hysteresis

Per decision: promote past the limit, demote below 87% of it. The level is
clamped between the level the promotion thresholds give and the level the
demotion thresholds give, so a long jump — a far target the first frame after a
near one — lands on the far level. The stroke design's thickness latch compared
against the wrong threshold and could be held two rungs up after such a jump;
`test_a_long_jump_lands_on_the_far_level` keeps it from coming back.

Even with hysteresis each expansion doubles the pixel size and will visibly
pop. That is inherent to sprite expansion and is accepted. The horizontal one
happens once per approach, at a fixed distance, never because the aircraft
rolled; the vertical one only when a close aircraft banks steeply, and follows
the roll.

---

## 5. Memory and sprite allocation

### Sprite buffers must not live under I/O

`$D400–$DFFF` is all allocated — cloud art in 81–94, the orientation mark in
80 — and would be the wrong place for dynamic buffers anyway: it is RAM under
the SID, so every write needs `$01` switched to `MMAP_RAM`, which means
interrupts off. Blocking the raster IRQ for the ~600 cycles of a 63-byte block
would delay the panel split by ten raster lines and glitch the screen edge
every frame. That region is fine for data written once at startup and wrong
for anything written per frame.

Instead, carve the buffers out of the top of the main region, which is plain
RAM inside VIC bank 3 and needs no banking at all.

### The title aircraft's page is free in flight

`$CF00–$CFFF` holds the title screen's aeroplane (`mem.h` `kTitleSpriteData`,
pointers 60–63). It is not permanent: `title_arm()` expands it there from its
compressed copy every time the menu is painted (`menu.cc`, `title.cc`), and
nothing in flight reads it. So traffic can **time-share** it — the first
version of this document assumed the page was lost and planned around 256
bytes less. Phase 1 has to confirm that no screen reachable from flight shows
the title sprites without going through `title_arm()`.

| Region | Blocks | Pointers | For |
| :--- | :---: | :--- | :--- |
| `$CCC0–$CCFF` | 1 | 51 | the static dot |
| `$CD00–$CDFF` | 4 | 52–55 | the second-nearest plane: 2 sprites, double buffered |
| `$CE00–$CEFF` | 4 | 56–59 | the nearest plane, with `$CF00`: 4 sprites, double buffered |
| `$CF00–$CFFF` | 4 | 60–63 | time-shared with the title aircraft |

```c
#pragma section(sprbuf, 0, , , bss)
#pragma region( sprbuf, 0xCCC0, 0xCF00, , , {sprbuf} )
#pragma region( main,   0x0860, 0xCCC0, , , {code, data, data_box, data_compr, bss, heap} )
```

That is 576 bytes out of the free run at `$C360–$CEFF` (2,976 B, of 3,308 B
free in all — [memory_map.md](memory_map.md)). The dot is written into its
block once at startup, which is cheap here because `$CCC0` is plain RAM.

**Block sets go by rank, not by plane.** The nearest aircraft gets the
eight-block set and may use all four sprites; the second gets the four-block
set and is capped at two sprites, which in §4's terms means a `d` limit of 39
— it freezes at ~112 m. When the two swap rank they swap sets, and both redraw
once.

Double buffering is not optional. The VIC fetches sprite data on the lines
where the sprite is displayed; rewriting a block in place tears the image for
one frame, and at ~10 fps single tears are clearly visible. Flipping the
pointers costs one byte per sprite in each of the two screen RAM copies.

### Hardware sprite indices

[sprite_objects.md](sprite_objects.md) §2's rule — sort candidates by
distance, nearest gets the lowest index, sun sorts last — is what the stack
already does. Each plane is one `sprites_stack_add()` carrying its
camera-space depth; `sprites_stack_commit()` sorts and hands out 0 upward.
Planes outrank the sun and occlude it correctly, and when two planes overlap
the near one wins, with no per-frame priority logic.

The stack needs two things it does not have:

- **Entries two sprites wide.** Today an entry is one sprite or a 1 × 2 column
  (`bitmap`, `bitmap2`). Traffic needs 2 × 1 and 2 × 2 as well: up to four
  pointers, the right-hand column at `x + 24·xs`, each with its own `$D010`
  bit because the two columns can straddle 255.
- **Y-expansion per entry.** The committed frame already carries `expand_y`
  and `_switch_to_panel_top` already clears `$D017`, both for the back view's
  fin; the stack only has to set the bits for a Y-expanded entry, and its cull
  has to use the Y-expanded cut for one (§4).

The budget is seven indices. The nearest plane at four, the second at two and
the sun make seven: while a close plane is banked, the clouds get none, and
the stack drops them because they are farther. In the back view the fin holds
indices 0–2, which leaves four.

### Budget

Estimates, except the buffers:

| Item | Bytes |
| :--- | ---: |
| Sprite buffers and the dot, `$CCC0–$CEFF` | 576 |
| Time-shared with the title aircraft, `$CF00–$CFFF` | (256) |
| Edge masks (8 + 8) and the reciprocal table (42 × 2) | 100 |
| Per-plane state — latches, a cache key of up to 30 vertices — 2 planes | ~150 |
| Model data | ~60 |
| Rasteriser code: edge trace and span fill | ~400 |
| Pipeline code: projection, fuselage, level, layout, slide | ~700 |
| **Total** | **~1.9 KB** |

Against the stroke design's ~1.5 KB. The difference is mostly code — the
fuselage, the layout and the slide. The buffers cost only 64 bytes more than
the old plan's 512, because half of the new ones are the title aircraft's, and
the rasteriser's tables shrank from 144 bytes to 100.

---

## 6. The rasteriser

Never plot pixel by pixel. **Fill one span per row**: the buffer is at most
42 rows of `3·cols` bytes, and a span is the natural addressing unit.

```
fill_poly(pts):                          # convex, any winding
    lo[], hi[] = +inf, -inf per row      # 42 entries each
    for each edge (a, b):
        if a.y == b.y:  widen row a.y to cover a.x .. b.x;  continue
        order so a.y < b.y;  dy = b.y - a.y
        slope = (dy == 1) ? (b.x - a.x) << 8
                          : fmul(b.x - a.x, RECIP[dy])   # 65536 / dy
        xa = (a.x << 8) + 128            # vertices are pixel CENTRES: x
        for y in a.y .. b.y:             # rounds, the end rows get half a step
            xb = (y == b.y) ? (b.x << 8) + 128
               : (y == a.y) ? xa + (slope >> 1)
               :              xa + slope
            widen row y to cover xa >> 8 .. xb >> 8
            xa = xb
    for each row y with lo[y] <= hi[y]:
        fill_span(y, lo[y], hi[y])
```

`fill_span(y, a, b)` writes bytes `a >> 3 .. b >> 3` of row `y` — the first
masked by `$FF >> (a & 7)`, the last by `$FF << (7 − (b & 7))`, the ones
between with `$FF`. Byte `i` of a row is byte `i mod 3` of line `y mod 21` in
the block at column `i / 3`, row `y / 21`. Sixteen bytes of mask table, where
the 24-column stroke rasteriser needed 144.

Properties that matter:

- **Edge-inclusive.** Every edge contributes the whole run it covers in each
  row — the trace `poly.cc` already uses for the terrain. So a surface seen
  edge-on degrades into a 1 px line rather than vanishing, and at range, where
  every chord is under a pixel, the model degrades into what the strokes used
  to draw.
- **Vertices are pixel centres, on both axes.** Vertically, each row takes
  the part of an edge within half a row of it, so the first and last rows of
  a shallow edge get half a step each. `poly.cc`'s trace puts vertices on row
  *boundaries*, where an edge's last row gets its end point alone — and
  wherever the lowest or highest vertex of a polygon is a shallow corner, that
  end point is a lone pixel under or over the silhouette. It first showed as a
  dot under the fuselage, on 2,065 of 4,320 frames of a sweep over distance,
  heading, bank, pitch and elevation. Horizontally, x rounds to the nearest
  pixel centre instead of truncating. Truncating put every step of a slanted
  edge half a pixel early, so an edge that leaned by a pixel stepped on its
  first row and left its corner sticking out alone: the fin's top trailing
  corner, the tailplane's tip. Single pixels sticking out sideways past both
  neighbouring rows went from 4,154 to 1,341 over the sweep. What remains of
  both kinds is mostly real: the end of a wing or fin seen nearly edge-on, and
  the corners of the flat nose seen from steeply above or below. The half
  step costs a shift per edge; the rounding is the 128 in each edge's starting
  x, and costs nothing.
- **No divides.** An edge is never taller than the buffer: `d` bounds the box,
  and every level's `d` fits 41 rows. So the slope is a lookup in a 42-entry
  table of `65536 / dy` and one multiply. The test sweeps attitudes to check
  no edge outgrows it.
- **Clamping a span is an exact clip.** A filled polygon cut at the buffer's
  sides is the polygon clamped row by row; there is nothing to preserve but the
  rows, and rows outside the buffer are skipped. The Liang–Barsky clipper the
  strokes needed is gone.
- **Vertices are never clamped.** The lesson from the strokes still stands —
  clamping endpoints into the buffer turned a close aircraft into the two
  diagonals of the buffer, a bare X — and applies to vertices just the same.
  They are only ever placed by the anchor and the slide.
- **No hidden-surface work.** One colour, ORed: overlap is free and order is
  irrelevant.

### Centre on the wing when the silhouette overruns

**A guard, not a path.** With §4's limits nothing overruns, so this is never
taken; it costs one comparison and it is the difference between a wrong
picture and a crash if a model is ever changed without re-measuring the
margins.

```
fits   = bbox_w <= xs·(24·cols − 1)  and  bbox_h <= ys·(21·rows − 1)
anchor = fits ? centre of the bounding box : the wing hub
```

Once the silhouette is larger than the buffer, centring the *box* lets the
fuselage — usually the longest thing on screen — push the frame around and
take a wingtip with it. The wing is what makes the shape readable, so losing
the ends of the fuselage is the right trade.

### Why a tailplane and a fin

The stroke design found that two strokes collapse into one horizontal dash when
both aircraft are level — geometrically correct, and indistinguishable from a
horizon artefact — and added the fin to fix it. The fin also tells upright
from inverted, which nothing else in the silhouette does.

The tailplane is the polygon model's addition. From above, from below and
banked it is what turns a cross into an aeroplane, and edge-on it is a second
short line above the low wing that the eye reads as a tail. As a trapezoid it
costs six vertices, three new products and six edges.

---

## 7. Caching

Cache on the **local vertex bytes and the layout**, not on an orientation.

After step 10 the vertices are in buffer-local coordinates. If they and the
layout match what was drawn last frame, the bitmap is already correct — skip
the clear, the fill and the flip, and only write the sprite registers.

The local vertices depend on `k` and the relative orientation, not on where
the target sits in the view, so a crossing at constant range hits on every
frame (the test checks it), and so does formation flight. A closing target
misses whenever `k` changes, which at 300 m is every other frame.

A hit is ~4,700 cycles, over twice the stroke design's, because the key is the
projected vertices and producing them is most of the cost. A second, earlier
key on `k` and the six axis components the projection reads would skip the
projection too when nothing has moved, for seven bytes per plane (§11).

---

## 8. Colour

**One fixed colour, always.** No background test, no switching, no multicolour.

```c
vic.spr_color[idx] = kColorTraffic;      // kColorLightGray to start with
```

Light grey is the opening choice, medium grey the fallback if it proves too
close to the sky gradient. Both read against blue sky, the cyan/light-blue
gradient band and green ground without being as loud as white, which is the
instrument colour and wants to stay unambiguous.

An earlier draft switched colour on whether the aircraft was above or below the
eye's altitude — a genuinely cheap test, one 32-bit comparison, no projection.
It was dropped anyway: a target crossing the horizon would change colour
mid-manoeuvre, which draws the eye to the wrong thing, and the constant colour
is one less piece of state to keep consistent between the terrain and panel
raster handlers.

Colour is per sprite, so every sprite of a layout — up to four — must be set.

---

## 9. Known limitations

- **No occlusion against terrain.** Sprite-behind-background would hide the
  plane behind any non-background pixel of the dithered horizon, i.e. almost
  all of it. Planes are always drawn in front. Since the sun stays well above
  the horizon and planes outrank it in priority, nothing else needs handling.
- **The viewport's last two lines.** A sliding bitmap ends on line 110 (§4),
  and the static dot on 108.
- **The two pops.** 1:1 → X at ~195 m, at a fixed distance; and Y-expansion
  when a close aircraft banks past ~35–40°, which follows the roll. Both are
  mitigated only by hysteresis.
- **The second plane freezes early.** Capped at two sprites, it holds its size
  from ~112 m (§5).
- **Clouds give way.** A close, banked plane takes four of the seven indices,
  and with a second plane and the sun there are none left for clouds.
- **First-order projection at the freeze.** At 54 m a 16.5 m span has about
  ±15% of depth across it, which true perspective would show as a larger near
  wingtip. The projection ignores it; the result reads as a slightly flat
  view, not as an error.
- **X MSB.** The viewport spans the full screen width, so plane sprites cross
  x = 255 constantly, and the two columns of a 2-wide layout can land on
  different sides of it; `$D010` handling is per sprite.
- **The message strip.** A plane behind an on-screen message must be culled;
  with two planes the box-overlap test that exists for the sun is affordable,
  and the single-comparison shortcut in
  [sprite_objects.md](sprite_objects.md) §7 is the fallback if it is not.

---

## 10. Open questions

1. **Light grey or medium grey?** §8 starts at light grey. This is a look at a
   screenshot, not an argument.
2. **Does Y-expansion look right on a real screen?** The prototype draws it
   over the terrain's 2 × 2 lattice at the 10 Hz sim rate, which is as close as
   a browser gets; a VICE screenshot is the real test.
3. **Is the cycle cost acceptable as it stands**, or should §11's two cheapest
   levers go in from the start?

Settled, and recorded here so they are not reopened: traffic is drawn **1.5×
oversize** (§2), the far tier uses a **static bitmap** (§4), the wing hub sits
**20% of the aircraft length ahead of the centre** (§3), flat surfaces are
**filled polygons and the fuselage a screen-space tube** (§1, §3), the
**horizontal pixel size follows distance, Y-expansion the silhouette's height,
and the layout the bounding box** (§4), **Y-expansion only with X, only when a
close aircraft is too tall for two sprites** (§1), the bitmap **slides above the DMA
cut instead of being rejected** (§4), the colour is **one fixed value with no
background test** (§8), and the update rate is **every sim frame** —
[sprite_objects.md](sprite_objects.md) §5 is right that half-rate projection
reads as jitter, because camera rotation moves a stationary object across the
screen even when it is not moving.

---

## 11. Cycle budget

**The denominator is the sim frame, not the PAL frame.** The viewport is
rebuilt once per `flight_advance` at a wobbling ~10 Hz
([sound.md](sound.md)), which is five PAL frames, ~98,500 cycles.
[sprite_objects.md](sprite_objects.md) §5 compares its 8,000-cycle estimate
against a single 19,705-cycle frame and concludes eight objects do not fit; on
the correct denominator the same estimate is 8%.

**These are estimates**, from a per-operation model in `lib/planes.py`
(`CYC_*`): 45 cycles a multiply, 150 a divide, 25 to trace an edge through a
row, 30 a span plus 8 a byte, 5 to clear a byte. Both implementations compute
it identically, so its *shape* is reliable; its constants want checking
against the first real rasteriser.

| Step | Cycles |
| :--- | ---: |
| Relative position, cull, transform, centre and `k` | ~940 |
| — dot tier, with one sprite's registers | **~970** |
| Two `vec_transform_inv` for body axes | ~900 |
| Projection: 52 multiplies, 2 divides | ~2,640 |
| Cache compare, sprite registers | ~250 |
| — cache hit | **~4,700** |
| Clear, 1–4 blocks | 315–1,260 |
| Fill: edges, slopes, traced rows, spans | 750–10,900 |
| — cache miss | **5,700–16,900** |

Swept over every heading, bank and pitch, a redraw's median is ~6,400 cycles
at 1 km, ~8,700 at 300 m, ~10,500 at 150 m, ~12,500 between 70 and 105 m and
~13,300 inside the cap. The worst single frame is **16,924 cycles** — at 70 m,
banked 45° and pitched 40° down, 2 × 2 at one line per pixel: 197 traced rows
and 92 spans. That is the price of Y-expanding as late as possible: an
unexpanded tall silhouette fills twice the rows, and expanding by distance
instead held the worst frame to ~15,000. The tapered wing and tailplane cost
two more edges each and a few more stations to project; against rectangles
that is ~1% on the worst frame and ~9% on a hit.

| Situation | Cycles | Share of a sim frame | Stroke design |
| :--- | ---: | ---: | ---: |
| Both in the dot tier | 1,940 | 2.0% | 4.4% |
| Both cached | 9,370 | 9.5% | 4.3% |
| One close and redrawing (median), one dot | 14,280 | 14.5% | — |
| One close and redrawing (worst), one cached | 21,610 | 21.9% | 9.1% |
| Both close, both redrawing (worst) | 33,850 | 34.4% | 14.0% |

The dot tier got cheaper, because it is now decided before the body axes are
transformed. Everything else roughly doubled. The last row is a near-collision
with two aircraft at once — momentary, and not worth designing around. The
fourth is the case to hold in mind.

The levers, cheapest first:

| Lever | Saves |
| :--- | ---: |
| Clear only the rows the frame before last drew | up to ~1,000 |
| The two opposite edges of a projected wing, tailplane or fin quad are parallel: one slope for both | ~300 |
| A pre-projection cache key on `k` and the axes (§7) | ~2,300 per hit |
| Fewer distinct stations in the model | ~135 per magnitude |
| Leave the tailplane out at 1:1, where it is a pixel or two | ~1,000 at range |

---

## 12. Phases

| # | Work | Notes |
| --- | --- | --- |
| 1 | `sprbuf` region, block sets, pointer flipping; time-share `$CF00` with the title | Verify VIC reads `$CCC0–$CFFF` correctly, and that no path from flight shows the title sprites without `title_arm()` |
| 2 | Span fill across blocks, edge trace with the reciprocal table, convex fill, driven by hardcoded vertices | Check against `lib/planes.py`'s golden silhouettes |
| 3 | Projection pipeline for one plane at a fixed world position, fuselage tube included | Fly around a parked aircraft and check the silhouette |
| 4 | Levels, layouts, hysteresis, size clamp, slide; the stack's 2-wide and Y-expanded entries | The level comes from `d`, the layout from the box (§4) |
| 5 | Vertex cache, double buffering | Measure the hit rate in level flight |
| 6 | Second plane, block sets by rank, priority ordering | Colour is a constant, so there is nothing to switch |
| 6b | Static dot block, far-tier short circuit | Skips the axes and the fill in the most common case; worth doing early if frames are tight |
| 7 | Canned kinematic paths | Separate document — behaviour, not graphics |
| — | *Optional:* §11's levers | Only once the real rasteriser's cycles are measured |

Throughout: `make test` runs `tests/test_planes.py`, which pins the invariants
this document argues for — a pixel size that ignores rotation, an invisible
layout, a constant size cap, nothing clipping at any attitude, the fuselage's
width, edge-on surfaces as lines, the slide, the cycle budget — and, through
`TestTwin`, the prototype's agreement with all of it.
