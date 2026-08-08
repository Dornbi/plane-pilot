# Sprite Objects — Design Notes (`sprite_objects.md`)

**Status: proposal, not implemented.** This describes a scheme for drawing
world objects — clouds, other aircraft, possibly projectiles — with the eight
hardware sprites in the viewport band, generalising what `sprites.cc` currently
does for the sun alone.

For how sprites are used today see [project.md](project.md); for the raster
band split see `gfx.cc`. For the aircraft case in full — which supersedes §6.2
and diverges from §3 — see [planes.md](planes.md).

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
(`x = 0` lands inside the left border; watch that an X-expanded sprite is 48
screen pixels wide and the border ends at 24, so a sprite parked at 0 with the
`msbx` bit clear is still fully hidden.)

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

The viewport is 40 × 14 characters = 320 × 112 screen pixels. In multicolour
that is **160 × 112 world pixels**.

| Sprite configuration | Screen footprint | World footprint | Granularity | Colours |
| :--- | :--- | :--- | :--- | :--- |
| Hires, X-expand | 48 × 21 | 24 × 21 | 1 world px | 1 |
| Hires, X + Y expand | 48 × 42 | 24 × 42 | 1 × 2 world px | 1 |
| Multicolour, X-expand | 48 × 21 | 24 × 21 | 2 world px | 3 |
| Two hires stacked, X-expand | 48 × 42 | 24 × 42 | 1 world px | 1 each |

A single X-expanded sprite is **24 world pixels wide — 15% of the viewport
width**. That is already large. The detail ladder therefore lives mostly in the
bitmap, not in the sprite count:

1. Small silhouette inside one sprite (distant).
2. Large silhouette filling one sprite (mid).
3. Y-expand — free, still one sprite, 48 × 42 screen pixels (near).
4. 1 × 2 stack — two sprites, full vertical detail (very near, rare).

Step 3 is worth having before step 4 because it costs nothing from the budget
of eight. The 1 × 2 arrangement is the last rung, not the second.

> **Aircraft skip step 3.** [planes.md](planes.md) §4 uses X-expansion only and
> goes straight from one sprite to a 1 × 2 stack. Y-expansion is free in
> sprites but not in silhouette: an aircraft is two near-horizontal strokes,
> and their readability is set by vertical placement precision, which is
> exactly what Y-expansion halves. X-expansion, by contrast, lands on 2 screen
> pixels — the same granularity as the world around it, so it costs nothing.
> A cloud has no silhouette to lose and keeps the free rung.

**Vertical granularity mismatch.** The terrain dot characters
(`kGfxQuadGround` and friends, 16 variants per group) put each plotted dot in a
4 × 4 screen-pixel quadrant, i.e. **4 raster lines tall**. Unexpanded sprite
pixels are 1 line tall, so sprites will read as four times finer vertically
than the world they sit in. Y-expand halves that discrepancy. Whether the
mismatch is a problem or an asset (crisp aircraft against a coarse world) is a
judgement call to make with a screenshot, not on paper.

---

## 4. Colour: hires vs multicolour, per sprite

Sprite mode is selected per sprite by a bit in `$D01C`, so the two can be
mixed in the same frame. Both options above occupy the same 24-world-pixel
footprint; the trade is horizontal detail against colour count:

- **Clouds → multicolour.** They are soft blobs that want shading, and two
  globally shared colours plus one per sprite is exactly the right shape for
  white/grey/highlight. Losing horizontal detail costs a cloud nothing.
- **Aircraft → hires.** They are small on screen for most of their life and
  want a readable silhouette; halving horizontal resolution would destroy it.

This decision has to be made **before the bitmaps are drawn**, since the two
modes need different source art. It is the one item here with a redraw cost
attached to changing your mind.

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

### 6.1. Clouds — procedural, not stored

Derive cloud positions by hashing the world tile coordinate. The map is 32 × 16
and wraps ([world.h](../c64o/world.h)), so a hash over `(tile_x, tile_y)`
yields position, size and a fixed altitude band with **zero RAM** and an
unbounded number of clouds.

This also gives the nearby-object query for free: a 3 × 3 tile scan around the
aircraft *is* the candidate set, and it doubles as the cull radius.

Cap draw distance at two to three tiles. Beyond that the 8.8 projection
shimmers by whole world pixels as the camera rotates. Fade in by growing the
silhouette across LOD steps rather than popping the sprite on at full size.

### 6.2. Other aircraft

**Designed in full in [planes.md](planes.md); this section is superseded.**

The reasoning that led here was: aircraft need orientation-dependent
silhouettes, a full azimuth × elevation set is unaffordable, so approximate it
with a generic airframe at three sizes plus head-on and tail-on views — six to
eight bitmaps at 64 bytes each.

The premise is what turned out to be wrong. An aircraft silhouette at sprite
scale is **two straight lines**, and the four endpoints come out of the
existing projection directly, so the bitmap can be drawn at runtime for the
cost of ~1,400 cycles. There is no bitmap count to bound, no azimuth ×
elevation set to approximate and **no static sprite data at all** — only two
double-buffered scratch blocks per aircraft. Nothing needs checking against
[memory_map.md](memory_map.md).

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
- Is the 4:1 vertical granularity mismatch (§3) visible enough to matter?
- Should clouds occlude the horizon line, or is drawing them only above it
  sufficient and cheaper?
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
