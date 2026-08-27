# Sprite Objects — Design Notes (`sprite_objects.md`)

**Status: partially implemented.** Cloud sprite definitions (15 bitmaps, pointers 80–94) are generated and stored in `spritedef.bin`/`mem.cc`. World placement, projection, and viewport multiplexing remain to be implemented — planned in full in [clouds.md](clouds.md), which supersedes §6.1 the way [planes.md](planes.md) supersedes §6.2, and which turns §2's restructure into a concrete sprite-stack API. This describes the scheme for drawing world objects — clouds, other aircraft, possibly projectiles — with the eight hardware sprites in the viewport band, generalising what `sprites.cc` currently does for the sun alone.

For how sprites are used today see [project.md](project.md); for the raster
band split see `gfx.cc`. For the aircraft case in full — which supersedes §6.2
and §3 — see [planes.md](planes.md).

## 0. Two absolute rules

Everything below is subject to these, and they are not trade-offs to be
revisited per object type:

- **Hires only. Never multicolour.** One colour per sprite, full horizontal
  resolution. `$D01C` stays zero.
- **Never expand along Y.** X-expansion is available. `$D017` stays zero.

Earlier drafts of §3 and §4 proposed both; those sections have been rewritten.

---

## 1. What already exists, and what it buys us

The sprite multiplexer in this project is **per raster band, not per object**.
`gfx_init_raster_irqs()` installs three handlers, and each one re-points all
eight sprites for the band below it:

| Handler | Raster line | Sprites used |
| :--- | :--- | :--- |
| `_switch_to_terrain` | `50 + 200` (latches for the next frame's viewport) | 1 — the sun, index 4 |
| `_switch_to_panel_top` | `50 + 112 - 1` | 1 — vertical speed, index 7 |
| `_switch_to_panel_bottom` | `50 + 112 + 24` | 7 instrument needles, indices 0–6 |

**Seven of the eight sprites are idle for the entire viewport.** So up to eight
simultaneous objects need no new multiplexing at all: no sorting by Y, no
mid-band re-latching, no raster splits inside the viewport. That is the
expensive part of a general sprite engine and this design gets to skip it
outright, at the price of a hard cap of eight.

Two further properties fall out of the existing code for free:

### 1.1. Bottom clipping already works

`_switch_to_panel_top` parks sprites at `x = 0` at the viewport edge. The VIC
compares the X register **per raster line**, so an object straddling the panel
boundary is cut cleanly at that line rather than having to be culled whole.
(`x = 0` lands inside the left border, which hides a 24-pixel sprite
completely. **It does not hide an X-expanded one:** that is 48 pixels wide and
the left border ends at 24, so parked at 0 it pokes 24 pixels into the picture.
Any band that parks sprites this way must also clear `$D01D` — see
[clouds.md](clouds.md) §1.4 and [planes.md](planes.md) §5.)

The sun's current `y >= kRasterScreenYStart + kViewportEndYPixels` cull is
therefore stricter than it needs to be — it hides an object that could legally
be shown partially.

### 1.2. Sprite-to-sprite priority is free depth sorting

VIC priority is by index: **lower index draws in front**. If the candidate list
is sorted by distance and assigned nearest → index 0, correct mutual occlusion
between objects comes out with no extra work.

Sprites always draw over the character-mode terrain, which is correct here:
every object of interest is at or above ground level and there is no terrain
relief to occlude it.

---

## 2. The structural change: index sharing

Every viewport sprite index collides with an instrument index, because the
panel uses all of 0–7. Today this is handled by one-off special cases
(`kIdxSun == kIdxAlt1`, plus `if (kIdxThrottle == kIdxSun)` in
`sprites_show_panel_bottom_sprites`). That does not generalise to eight shared
slots.

**Proposed split.** In the panel-top band sprites 0–6 are parked at `x = 0` and
their colour is irrelevant; only index 7 must be correct. So:

- `_switch_to_panel_top` — cycle-counted and NOP-padded, leave it alone. Set
  index 7's colour only.
- `_switch_to_panel_bottom` — not cycle-critical, sits below the split.
  Restore all eight instrument colours here.

Then the terrain handler is free to write all eight `spr_color` entries without
any handshake with the panel code.

**Fold the sun into the object list.** Rather than keeping `kIdxSun` special,
make the sun an ordinary entry with a fixed "infinite" distance so it always
sorts last (highest index, drawn behind everything). One code path, one cull
test, one set of position registers.

---

## 3. Sizing and levels of detail

The viewport is 40 × 14 characters = 320 × 112 screen pixels. The terrain is
drawn at half that horizontally — **160 × 112 world pixels** — so one world
pixel is 2 screen pixels wide, which is exactly one X-expanded sprite pixel.

| Sprite configuration | Screen footprint | World footprint | Granularity | Colours |
| :--- | :--- | :--- | :--- | :--- |
| Hires, 1:1 | 24 × 21 | 12 × 21 | ½ world px | 1 |
| Hires, X-expand | 48 × 21 | 24 × 21 | 1 world px | 1 |
| Two hires stacked, 1:1 | 24 × 42 | 12 × 42 | ½ world px | 1 each |
| Two hires stacked, X-expand | 48 × 42 | 24 × 42 | 1 world px | 1 each |

The Y-expanded and multicolour rows that used to appear here have been removed
under §0.

A single X-expanded sprite is **24 world pixels wide — 15% of the viewport
width**. That is already large. The detail ladder therefore lives mostly in the
bitmap, not in the sprite count:

1. Small silhouette inside one sprite (distant).
2. X-expand — still one sprite, 48 × 21 screen pixels (mid).
3. 1 × 2 stack — two sprites, 48 × 42 (near).

Y-expansion used to be listed here as a free third rung, on the grounds that it
costs nothing from the budget of eight. It costs nothing in *sprites* and a
great deal in *shape*: it halves vertical placement precision, which for an
aircraft is exactly the axis a near-horizontal wing line is read by, and for a
cloud makes the checkerboard dither read as stripes. X-expansion by contrast
lands on 2 screen pixels — the same granularity as the world around it — so it
costs nothing either way. See [planes.md](planes.md) §4.

For aircraft the two axes are chosen **independently** rather than as a ladder:
width picks expansion, height picks the sprite count. A steeply banked aircraft
is tall and narrow and must not be expanded along with its extra sprite.

**Vertical granularity mismatch — 2:1, and only vertically.** An earlier draft
of this paragraph said the terrain dot characters put each plotted dot in a
"4 × 4 screen-pixel quadrant, i.e. 4 raster lines tall". That is wrong by a
factor of two on both axes. `gfx_project_and_draw()` selects the character with
`((lpx & 0x06) >> 1) + ((lpy & 0x06) << 1)` — four sub-positions per axis
across an 8 × 8 cell, so each is **2 × 2 screen pixels**; the 16 variants are a
4 × 4 *arrangement*, not a 4 × 4 pixel. `tools/generate_gfx_chars.py` draws the
glyph to match: one multicolour pixel wide (`alt_lines[x]`, 2 screen pixels)
and two raster lines tall (`char_bytes[y * 2]` and `char_bytes[y * 2 + 1]`).

So the comparison is: **horizontally 1:1** — an X-expanded sprite pixel is 2
screen pixels and so is a terrain dot, which is the same point made above — and
**vertically 2:1**, an unexpanded sprite line against the dot's two. One axis,
one factor of two, against a world the eye has no finer reference for. This is
not a problem and does not need Y-expansion to fix, which is just as well,
since §0 rules it out.

For clouds the question does not arise at all: the dither-phase snap in
[clouds.md](clouds.md) §4 quantises a cloud's vertical position to two raster
lines, putting it on exactly the terrain's own 2 × 2 lattice.

---

## 4. Colour

**Hires, one colour per sprite, for everything** (§0). Sprite mode is selectable
per sprite via `$D01C`, and this project does not use it.

An earlier draft argued for multicolour clouds — soft blobs that want shading,
two shared colours plus one per sprite. Shading is instead done with a
**dither**: a cloud is a white-and-transparent checkerboard, which at 1 bit per
pixel reads as a light half-tone against the sky and needs no second colour.
Hires keeps the full horizontal resolution the dither depends on; in
multicolour the checkerboard would collapse into flat 2-pixel blocks.

- **Clouds** — white, checkerboard-dithered (§6.1).
- **Aircraft** — one fixed grey, no background test
  ([planes.md](planes.md) §8).

---

## 5. Per-frame cost, which is the actual constraint

Each visible object needs `vec_transform_inv` followed by `vec_project`, and
`vec_project` carries a 16-bit divide. Estimating around **1,000 cycles per
object**, eight objects is ~8,000 cycles.

> **Correction: the denominator is the sim frame, not the PAL frame.** An
> earlier draft of this section compared 8,000 cycles against a 19,705-cycle
> PAL frame and concluded it does not fit. But the viewport is rebuilt once per
> `flight_advance`, at a wobbling ~10 Hz — five PAL frames, ~98,500 cycles. On
> that budget eight objects is **8%**, alongside `flight_advance` (up to ~6,000
> on a re-orthonormalising frame, [flight.md](flight.md) §1) and the grid
> render. Eight is affordable. See [planes.md](planes.md) §11 for a measured-
> operation breakdown of the two-aircraft case, which comes to ~7,600 cycles.

That does not make the mitigations below pointless — it makes them optional
rather than load-bearing, and worth adding when the object count grows past a
handful of clouds.

Design the mitigations in from the start rather than bolting them on:

1. **Cheap world-space reject before projecting.** A `front · delta` sign test
   plus a Manhattan distance bound rejects most candidates for roughly three
   multiplications, well under a tenth of a full projection.
2. **Round-robin the candidate scan.** *Which* objects are nearby changes
   slowly; *where they are on screen* changes every frame. Rescan the candidate
   set at 5–10 Hz and project only the selected few each frame.
3. **Budget four or five concurrent objects, not eight.** Eight is the hardware
   ceiling, not the performance target.
4. **Measure before committing.** `benchmark.h` provides the harness
   (`__DEBUG_CYCLES__`) and there is currently exactly one `bm_end` call site,
   in `world_update_sun_pos`. Wrap the projection loop and get a real number
   before fixing the object count.

Do **not** try to save cycles by projecting distant objects on alternate
frames. Camera rotation moves them across the screen even when they are
stationary in the world, so a half-rate update reads as jitter, not economy.

---

## 6. Object classes

### 6.1. Clouds — procedural placement, pre-rendered art

**Positions are procedural; bitmaps are not.** Clouds are the opposite of
aircraft in this respect, and for a good reason: a cloud has no orientation, so
its appearance depends on size alone and a short ladder of pre-rendered bitmaps
covers every case. Aircraft need a shape per attitude, which is why they are
drawn at runtime ([planes.md](planes.md) §1).

**Art.** A circle filled with a **white-and-transparent checkerboard**. At 1
bit per pixel the 50% dither reads as a light half-tone against the blue sky —
softer than solid white, and it lets the sky through so the cloud does not read
as a cut-out. Hires only, so the checkerboard stays a checkerboard (§4).

**Ladder.** One sprite (1 × 1) for distant to mid-range clouds, two stacked (1 × 2) when near, all using X-expansion. Extracted from `gfx/ppilot_clouds_concept.png` (upper set) into 10 sizes across 15 sprite blocks:
- **1 sprite** (4 sizes, pointers 81–84): 5×9, 7×13, 9×17, 11×21 (world width × raster lines). A 3×5 at pointer 80 was dropped: [clouds.md](clouds.md) §3.5 shows the size ladder can never select it, and §1.9's orientation indicator has the block now.
- **2 sprites stacked** (5 sizes, 10 bitmaps, pointers 85–94 as `[top, bot]` pairs): 13×25, 15×29, 17×33, 19×37, 21×41.

**They occlude the horizon line.** A cloud is drawn in front of the terrain
like every other sprite, not clipped to the sky. A cloud that cut off at the
horizon would read as a hole in the world; one that overlaps it reads as
distance.

**Placement.** Derive positions by hashing the world tile coordinate. The map
is 32 × 16 and wraps ([world.h](../c64o/world.h)), so a hash over
`(tile_x, tile_y)` yields position, size and a fixed altitude band with **zero
RAM** for the layout and an unbounded number of clouds.

This also gives the nearby-object query for free: a 3 × 3 tile scan around the
aircraft *is* the candidate set, and it doubles as the cull radius.

Cap draw distance at two to three tiles. Beyond that the 8.8 projection
shimmers by whole world pixels as the camera rotates. Fade in by stepping up
the ladder rather than popping the sprite on at full size.

**Where the bitmaps live.** `$D400–$D7BF` — 15 free sprite blocks, pointers
80–94. That region is RAM under the SID, so writing it needs `$01` banked and
interrupts off, which rules it out for anything written per frame
([planes.md](planes.md) §5). Cloud bitmaps are written **once at startup**,
exactly like the instrument needles already there, so the restriction costs
nothing. The division of labour is clean: static art under I/O at `$D400`,
dynamic aircraft buffers in plain RAM at `$CE00`.

### 6.2. Other aircraft

**Designed in full in [planes.md](planes.md); this section is superseded.**

The reasoning that led here was: aircraft need orientation-dependent
silhouettes, a full azimuth × elevation set is unaffordable, so approximate it
with a generic airframe at three sizes plus head-on and tail-on views — six to
eight bitmaps at 64 bytes each.

The premise is what turned out to be wrong. An aircraft silhouette at sprite
scale is **three straight lines**, and their endpoints come out of the existing
projection directly, so the bitmap can be drawn at runtime. There is no bitmap
count to bound and no azimuth × elevation set to approximate — just two
double-buffered scratch blocks per aircraft in plain RAM, plus **one** shared
static block for the far tier, where an aircraft is under 4 px and has no
silhouette left to draw. That one block lives in `$D400` next to the cloud art
(§6.1). Nothing needs checking against [memory_map.md](memory_map.md).

### 6.3. Projectiles — do not spend sprites on these

Projectiles have no shape and want to be numerous, which is the opposite of
what a sprite is good at. `gfx_project_and_draw()` already plots a coloured
point into the character grid: cheaper per projectile, unbounded count, and it
leaves all eight sprites for objects that need a silhouette.

---

## 7. Known rough edge: the message strip

`_hidden_by_msg` is currently one box-overlap test against the message span,
run for the sun. With eight objects it becomes eight tests per frame, in a
budget that is already tight (§5).

The cheap resolution is to drop the overlap test and simply cull any object
sprite whose top lands in the message band while a message is up. It hides
slightly more than necessary, but messages are transient and the test is a
single comparison. Moving messages to the bottom of the viewport would remove
the conflict entirely and is worth considering separately.

---

## 8. Open questions

- Does the projection loop actually cost ~1,000 cycles per object? §5 is an
  estimate from operation counts, exactly like the flight-model budget in
  [flight.md](flight.md) §1, and should be measured before the object count is
  treated as settled.
- ~~Is the 4:1 vertical granularity mismatch (§3) visible enough to matter?~~
  **Answered — the premise was wrong.** It is 2:1, not 4:1, and only on the
  vertical axis; §3 has been corrected. Not visible enough to matter, and moot
  for clouds, which land on the terrain's lattice exactly
  ([clouds.md](clouds.md) §4.5).
- ~~Should clouds occlude the horizon line, or is drawing them only above it
  sufficient and cheaper?~~ **Answered** — they occlude it (§6.1). A cloud
  clipped at the horizon reads as a hole in the world.
- ~~How many cloud radii, and at what sizes?~~ **Answered** — 10 sizes across
  15 sprite blocks: 5 single-sprite (3×5 to 11×21) and 5 two-sprite stacks
  (13×25 to 21×41) (§6.1).
- ~~Do aircraft need per-object colour at all, or is a single traffic colour
  enough to free the colour writes in the terrain handler?~~ **Answered** —
  [planes.md](planes.md) §8: colour switches on whether the aircraft is above
  or below the eye's altitude, which is a one-comparison test and the
  difference between a visible silhouette and an invisible one against the
  green ground. Two planes can differ, so it must be per object.
- One more thing this document got right and should not be relitigated: §5's
  warning against projecting distant objects on alternate frames. Camera
  rotation moves stationary objects across the screen, so half-rate updates
  read as jitter. [planes.md](planes.md) adopts every-sim-frame updates on that
  basis.
