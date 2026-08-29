# Plane Pilot — Architecture

Plane Pilot is a 3D flight simulator for the Commodore 64. This document
describes how the code works. For build and usage instructions see
[../README.md](../README.md) and [development.md](development.md).

The repository holds two related codebases:

- **`c64o/`** — the actual C64 program, written in C (`.cc`, compiled with
  [oscar64](https://github.com/drmortalwombat/oscar64)) with a few hand-written
  assembly routines.
- **`lib/` + `tools/`** — a Python prototype that models the C64 graphics
  hardware, explores the horizon-rendering scheme, and _generates_ the character
  set, tile definitions and sprite data that the C64 code compiles in.

Data flows one way: Python generates `chardefs`, `boxdefs`, `spritedef` and
`gfx_chars.bin`; the C64 code consumes them.

---

## 1. The core idea

Drawing a rotating horizon pixel by pixel is far too slow on a 1 MHz 6510. So
the horizon is never drawn — it is _looked up_.

1. The roll angle is quantized to one of **60 fixed angles** (`kRollMax`), each
   defined by an integer direction vector such as `r8u1` (8 right, 1 up).
2. For each angle, an offline pass finds a small repeating **tile** (a "box")
   of characters that reproduces the dithered sky→ground gradient along that
   line, plus the step vector to place the next copy of the tile.
3. At runtime the frame is built by filling solid sky and solid ground, then
   stamping the tile repeatedly along the horizon line.

The cost of a frame is therefore roughly independent of the roll angle. The
price is a large static data set: **333 unique gradient characters**
(`kTotalChars`) across **68 tile definitions** — more characters than the VIC-II
can address at once, so the ~30 characters a tile needs are copied into
character RAM each time the tile changes.

---

## 2. Memory map

`mem.h` moves everything into VIC bank 3 and hands the low memory back to the
program (`__MAX_RAM__`).

| Address       | Contents                                                   |
| ------------- | ---------------------------------------------------------- |
| `$0002–$005A` | oscar64's own zero page: registers, parameters, temporaries |
| `$0060–$00FF` | zero page (oscar64 `zeropage` region)                      |
| `$0200–$0280` | CPU stack (`#pragma stacksize(0x80)`)                      |
| `$0280–$0800` | `bss2`, a second BSS region — full to the byte             |
| `$0860–$CEFF` | code, data, bss, heap                                      |
| `$CF00–$CFFF` | title screen aircraft, expanded from `titledef.bin`        |
| `$D000–$DFFF` | I/O (`MMAP_NO_ROM`)                                        |
| `$D000–$D3FF` | map view screen RAM, live only while the map is open       |
| `$D400`       | sprite bitmaps, expanded from `spritedef.bin` at startup   |
| `$D800`       | color RAM                                                  |
| `$DA30`       | panel color rows (= `$D800 + 14*40`)                       |
| `$E000–$E7FF` | character RAM                                              |
| `$E800`       | screen RAM, main buffer                                    |
| `$EC00`       | screen RAM, alt buffer                                     |
| `$EE30`       | panel screen rows (= `$EC00 + 14*40`)                      |
| `$F000–$FF3F` | MCBM bitmap for the instrument panel                       |

The zeropage region starts below oscar64's `$80` default because the KERNAL and
BASIC are banked out and their zero page is ours to take. The limit is not the
ROMs but the compiler: its spilled temporaries start at `$53` and grow upward
with the call graph, unbounded and unchecked, so `$60` is the measured high
water mark (`$5A`) plus margin rather than a fixed safe address. An overrun
corrupts globals silently, so `tools/check_zeropage.py` runs on every link and
fails the build if the two ever meet. If it fires, raise the region start in
`mem.h` — see the comment there for the full runtime layout.

VIC bank selection is `cia2.pra &= 0xFC` (bank 3, `$C000–$FFFF`). The two
`vic_memptr` values are `$A8` (screen `$E800`) and `$B8` (screen `$EC00`); both
select character RAM at `$E000`.

Note the deliberate overlap: character RAM and both screen buffers sit _inside_
the address range the MCBM bitmap would occupy. That is safe because a raster
interrupt switches modes at the viewport boundary — the top of the screen never
reads bitmap data, and the bottom never reads characters.

### Screen layout

- 40 × 25 characters total.
- **Viewport**: rows 0–13, full 40 columns (`kViewportWidth` 40,
  `kViewportHeight` 14), multicolor character mode.
- **Panel**: rows 14–24, multicolor bitmap mode.
- `kRasterScreenYStart` is 50; raster IRQs fire at lines 161 (switch to panel
  top), 186 (panel bottom sprites) and 250 (back to terrain).

### Character RAM layout

| Range     | Contents                                                                                                                                                    |
| --------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `0`       | solid `0xFF` — every pixel pair is `11`, so the color comes from color RAM (`kCharSolid11`, `kCharSolidSky`, `kCharSolidGrad1`)                             |
| `1–29`    | gradient tile characters for the **main** buffer (`mem_box_char_start` = `$01`)                                                                             |
| `32–127`  | fixed characters from `gfx_chars.bin`, indexed by ASCII (font)                                                                                              |
| `97–125`  | gradient tile characters for the **alt** buffer (`$61`) — overlaid on the unused lowercase span of the font                                                 |
| `128`     | `kCharSolidGround`, the zero-density member of the quad group                                                                                               |
| `128–255` | eight 16-character groups from `gfx.h`: quad fills (ground sparse/dense, `11` sparse/dense, mixed sparse/dense) and single-point characters (ground, color) |

The two tile slots exist so double buffering works: while one screen buffer is
displayed, the other's tile characters can be rewritten. `kMaxBoxCharCount` is
29 — the largest actual tile — which is what keeps slot `$61` clear of
`kCharSolidGround` at 128. `box.cc` sizes its lookup tables at
`kMaxBoxCharCount + 3`, since indices 0–2 are reserved for the solid ground,
sky and `11` characters.

### Colors

Multicolor character mode fixes three colors screen-wide and takes the fourth
per character position from color RAM:

| Bit pair | Register          | Color                                       |
| -------- | ----------------- | ------------------------------------------- |
| `00`     | `vic.color_back`  | light blue (`kColorGrad2`)                  |
| `01`     | `vic.color_back1` | green (`kColorGround`)                      |
| `10`     | `vic.color_back2` | orange (`kColorGrndObj`)                    |
| `11`     | color RAM         | blue sky, cyan gradient, or an object color |

Because `11` is per-position, solid sky and the near-horizon gradient band both
use character 0 and differ only in their color RAM byte. That is also why color
RAM has to be rewritten every frame — it cannot be double buffered, since
`$D800` is fixed. Colors live in `color.h`.

---

## 3. Frame loop

`ppilot.cc` is a thin shell: initialize, then alternate `menu_run()` and
`sim_run()` forever. The real loop is in `sim.cc`:

```
keyb_poll() → edge-detect toggles → dispatch controls
model_advance()                        MDL
view_update_cam()
world_update_roll_state()
world_update_sun_pos()                 UPD
render_snap_center_chars()             SNP
render_fill_sky_ground()               BGR
box_prepare()                          CHR, PRP
box_draw()                             DRW
world_render_grid()                    GRD
model_update_instruments()
                                       TOT
mem_switch_buffer()                    COL
```

The right-hand labels are the cycle counters shown in the debug view (`D`);
`benchmark.cc` reads the chained CIA2 timers around each phase.

The instrument update sits *after* the drawing calls on purpose. Everything
above it writes the viewport, so it has to be finished before
`mem_switch_buffer()` shows that buffer; the panel does not, because its bitmap
and the sprite registers are single-buffered and owned by the raster handlers.
Placing it last spends it in the gap between the final drawing call and the
raster reaching the flip window — time `mem_switch_buffer()` would otherwise
spin away. It still reads the same iteration's flight state, so nothing is
pipelined and no control input is a frame staler.

Map mode short-circuits the whole render path and just waits for vsync.

---

## 4. Rendering the horizon

### `roll.cc` / `roll_asm.cc` — angle tables

One macro list defines all 60 angles and expands into three parallel tables:
`kRollDx`, `kRollDy` and `kRollDxDivDy` (a 12.4 fixed-point slope).
`kRollPeriod` gives how many pixel steps along the line before it returns to a
character-aligned position; the renderer caps it at 8. `roll_update_state()`
also selects hand-written multiply-by-constant routines (`roll_mul_funcs`) for
the current `dx`/`dy`.

### `render.cc` — snapping and background fill

`render_snap_center_chars()` takes the horizon center in pixels
(`render_cx_pixels`, `render_cy_pixels`, computed by `world_update_roll_state()`
by projecting a far-away point), slides it along the horizon to near the
viewport center (`_pull_to_center`), then searches the candidate lattice
positions for the character-aligned point closest to the true line. For angles
with period 1 there are exactly two candidates — the main lattice and an
"alt" lattice shifted half a character — and the winner sets `render_alt_box`.

`render_fill_sky_ground()` then fills the viewport with solid sky and solid
ground, walking `roll_dx_div_dy` down the rows. It deliberately leaves
`kSkipLines` (4) rows either side of the horizon unfilled, because the tiles
are about to overwrite them anyway — worth about 1000 cycles for 260 bytes.

### `boxdefs.cc` / `box.cc` — tiles

`boxdef_t` is the tile: width, height, the step vector to the next copy, its
offset relative to the snapped center, the character indices, and the index
array. `boxdefs.cc` holds 68 definitions — 60 main (one per angle) and 8 alt —
looked up through `main_boxes[]` / `alt_boxes[]`. Both files are generated;
edit the generators under `lib/` and re-run `make chardefs`.

A tile's characters are stored as one byte each, relative to the tile's own
`char_offset`, rather than as pointers into `chardefs`: the characters a single
tile uses are clustered, so `char_offset` is placed at the start of the largest
gap in the (circular) character id space and every relative index then fits in
a byte. That halves the tables, at the cost of one add and one conditional
subtract per character in `box_prepare()`.

`box_prepare()` copies the tile's unique characters into the current buffer's
character slot and builds parallel character/color arrays. It caches per slot:
if the slot already holds this definition — the common case when flying
straight — everything is still valid and the copy is skipped.

`box_draw()` stamps the tile from the snapped center outward in both
directions along the step vector, clipping each copy against the viewport.

---

## 5. World and 3D

### `vec.h` / `vec.cc` / `vec_asm.cc` / `vec_lut.cc`

All 3D math is 8.8 fixed point with unit length 256. Orientation is a `mat3_t`
of three orthonormal vectors (`front`, `left`, `up`).

The multiply is the interesting part. `vec_fastmul8p8` is hand-written assembly
built on quarter-square tables (`x*y = T(x+y) − T(|x−y|)` with `T(i) = i²/4`),
assembling the 16-bit result from four 8×8 partial products. Unlike the earlier
C version it is exact for all `int16` inputs, which removed several precision
workarounds elsewhere. `vec_lut.cc` also carries a reciprocal table used by
`vec_div8p8`.

`vec_project()` and `vec_project_nocull()` turn a camera-space vector into
screen coordinates (`vec_sx`, `vec_sy`); the culling variant rejects anything
off screen, the other only rejects points behind the near plane (`x <= 8`).

`vec_frac16` and `vec_mulfrac` are the clipping pair, and they exist because
8.8 is not enough for a clip parameter — see the clipping notes under
[`poly.cc`](#polycc--filled-polygons) below. `vec_frac16` is a restoring
32/16 division in assembly (16 iterations, ~927 cycles) returning `|a| / |b|`
as an unsigned 0.16 fraction; `vec_mulfrac` multiplies that fraction by a
delta using `vec_fastmul8p8` on each half, rounding the low one (~720
cycles).

`vectest.prg` and `vecdemo.prg` exist to verify and benchmark these routines
in isolation.

### `world.cc` / `world_map.cc` — ground detail

The world is a **32 × 16 map** (`kWorldMap`) that wraps in both directions.
Each cell is either nothing, a colored dot cluster, or a polygon object
(runway, field, pond, lake, town, city).

`world_render_grid()` walks a square of cells centered on the plane. The radius
is chosen from altitude (2 at ground level, 3 by default, 4 when high), and the
number of dots per cell falls off with distance from the center
(`kNumPoints2/3/4`). Dot positions inside a cell come from a 16-entry
Mitchell's-best-candidate distribution, precombined once per frame into
`_mitch_x/y/z` so each dot costs three additions and a projection. Iteration
breaks out early once cells fall behind the camera.

Dots are drawn by `gfx_project_and_draw()`, which only writes if the target
character is still solid ground — so dots never land on the sky or on a
polygon.

### `poly.cc` — filled polygons

`poly_draw_3d()` clips against the near plane, projects, clips against the four
viewport edges (one data-driven routine per edge), and fills.

**Clipping precision.** Both clippers interpolate `prev + t * delta`, and `t`
used to come from `vec_div8p8`, which has eight fractional bits. A part in 256
sounds harmless and is not: a clipped edge is often a thousand units long, and
a vertex just past the near plane projects thousands of sub-pixels off screen,
so a part in 256 of that span is whole characters. The near plane makes it
worse — at `x = 8` the projection multiplies `y` by 32, so **one world unit is
eight sub-pixels**, and an intersection rounded to whole units can only land on
every fourth character column. A polygon covering most of the ground could come
out as a thin wedge running into a corner of the screen, and it did.

Three things fix it, and `test/poly_test.cc` measures all three against the
same geometry projected in double precision:

1. `t` is a 0.16 fraction from `vec_frac16`, and the lerp is `vec_mulfrac`.
   `_clip_2d` keeps the old 8.8 path when the interpolated delta is 255
   sub-pixels or less, which is the common case and costs nothing.
2. The near-plane intersection is computed eight times finer and emitted at
   `x = 64` rather than `x = 8` whenever the components have room for the
   shift. Projection is a ratio, so scaling one vertex changes nothing but its
   own resolution.
3. The projection rounds instead of truncating toward zero. It is the only
   one of the three that helps an ordinary distant polygon, where nothing is
   clipped at all.

   Written as `(vec_sx >> 2) + ((vec_sx >> 1) & 1)` rather than the
   `(vec_sx + 2) >> 2` it started as. `vec_div8p8` saturates at 32767 and a
   vertex just past the near plane saturates it routinely, so on a 16-bit
   `int` the addition wrapped to −32767 and put that vertex on the opposite
   side of the screen — one polygon then covered the whole viewport. The host
   suite cannot see that class of bug at all, because there `int` is 32 bits
   and the sum simply fits; it was found by flying mission 4 and pinned down
   by reading the projected vertices out of a running C64
   ([emulator.md](emulator.md)).

Measured over 65,520 poses of a polygon at takeoff and landing height, in
sub-pixels filled wrongly out of the viewport's 2,240: mean 28.8 and 5.7% of
poses more than 128 wrong before, mean 7.8 and none over 128 after. The cost
is +722 bytes, and about 830 cycles for each near-plane crossing plus 490 for
each viewport crossing wider than the guard — zero for a polygon that does not
straddle the camera plane, which is most of them.

Filling works on a scanline buffer at **half-character vertical resolution**
(`_min_x`/`_max_x` are `2 * kViewportHeight` entries) and half-character
horizontal resolution, matching the 4×4 sub-cell granularity of the quad
character groups. Two fillers exist: `_scan_lines` is smaller and better for
small polygons, `_scan_lines2` costs ~250 bytes more and wins on large ones.

### `model.cc` — flight model

State: orientation matrix `model_cam`, position (`world_eye_x/y/z` as 24.8
meters), speed, vertical speed, throttle, fuel, and the flap / gear / nav
toggles.

Per step: air resistance proportional to speed², extra drag from gear and
flaps, a gravity term from pitch, throttle thrust, then lift computed from
speed² and `up.z`. A lift deficit costs both vertical speed and forward speed.
Below stall speed the nose is forced down; flaps lower the stall threshold.

Two distinct regimes:

- **Airborne** — full 3-axis control; a roll away from level induces a yaw
  proportional to `left.z` (the turn).
- **On ground** — no stall, wings forced level, no nose-down pitch, and roll
  input is remapped to yaw. Pitching up above stall speed is what takes off.

Touching down checks roll, pitch, vertical speed, forward speed and gear; fail
any of them and `_model_crashed` latches, freezing the model and all input.

`vec_orthonormalize()` runs after any rotation that needs it, to stop the
matrix from drifting.

---

## 6. Display and instruments

### `mem.cc` — buffers and modes

`mem_switch_buffer()` is the frame boundary: wait for the flip window, toggle
`mem_using_alt_buffer`, point `mem_screen_ram`, the row-pointer table and
`mem_box_char_start` at the other slot, then copy the color buffer to `$D800`.

The wait is `gfx_wait_flip_window()`, not `gfx_wait_vsync()`, and it is a raster
*range* rather than a line — see the constants in `gfx.cc`. It opens at 162, the
first line past the viewport, because the copy rewrites the color RAM lines
50–161 are still fetching; it closes at 242, because `_switch_to_terrain()`
latches `mem_using_alt_buffer` into `$d018` at raster 250 and the toggle has to
beat it. Most frames are already inside the window and never wait at all, and
because it is a range no interrupt handler can straddle it and cost a frame the
way a single-line wait can.

That latch is also why nothing here writes `$d018` directly any more. A write of
its own would have to land inside the window, which is the middle of the panel,
where it would repoint the panel's video matrix mid-screen. And since
`mem_using_alt_buffer` is one byte and the only state the handler reads, the
store is atomic on its own — the `sei`/`cli` pair this used to need is gone.

The color buffer (40 × 14 = 560 bytes) is aliased onto
`kSpriteDataCompressed` — the compressed sprite blob is dead once it has been
expanded to `$D7C0`, so its storage is reused.

### `gfx.cc` — raster IRQs, characters, points

Three raster interrupts drive the mode split. `_switch_to_panel_top` pads with
16 `nop`s to land in the border, then writes `$D018`, `$D011` and `$D021`
directly in assembly to enter bitmap mode. `_switch_to_terrain` restores
multicolor character mode at the bottom of the frame. All three are compiled
with `#pragma optimize(noasm)` so the compiler leaves the timing alone.

`gfx.cc` also owns the fixed character set (`gfx_chars.bin`, embedded LZO,
expanded at startup), the ground/color point plotting, the scrolling heading
strip (cached — it only redraws when the heading changes), and the panel's
nav / flap / gear indicator lights, which are toggled by storing light red or
black into the cell's color RAM byte.

Color RAM, and not one of the two screen RAM colors, because those two share a
byte and which of them holds what is `png2koa.py`'s choice: its lossless slot
optimizer relabels them for compression, and it once moved a lamp from one
nibble to the other, which killed the right-hand nav light silently. `make
panel` therefore passes `--pin-color-ram 10@ROW,COL` for the four lamp cells,
and nothing else — light red anywhere else in the panel is still placed for
compression. `tests/test_png2koa.py` reads the lamp coordinates out of `gfx.cc`
and checks both the shipped `panel.koa` and the Makefile's pin list against
them, since the same four cells are now written down in three places.
The lamps also only update in the center view, like the heading strip: a side
view replaces those cells with the fill pattern, which draws with color RAM
itself.

### `view.cc` — the three views

`view_update_cam()` derives the camera from `model_cam`: forward, or ±90°
using the `left` vector. Switching to a side view rebuilds the panel bitmap,
sliding the retained 8 columns (`kCopyWidthChars`) to one side and filling the
rest with a gradient pattern. Returning to center re-expands the compressed
panel. Bitmap state is tracked separately (`view_bitmap_state`) so a
center→side switch skips the re-expansion.

### `sprites.cc` / `spritedef.cc`

Eight sprites, reused twice per frame by the raster split: the sun over the
terrain, and the instrument needles over the panel. Needle bitmaps are
pre-rotated into 32 directions with per-direction pivot offsets
(`kSpriteDefMetaLongArm`, `kSpriteDefMetaShortArm`), so setting an instrument
is a table lookup plus two subtractions. Sprite pointers are written into both
screen buffers at offset 1016.

---

## 7. Screens

| Module      | Screen                                                                             |
| ----------- | ---------------------------------------------------------------------------------- |
| `menu.cc`   | main menu — mission list, `I`/`K` to move, `SPACE`/`RETURN` to start, `H` for help |
| `title.cc`  | the aircraft that crosses the menu page every ten seconds or so                     |
| `help.cc`   | static key reference, one text blob printed by `print_lines()`                     |
| `map.cc`    | single-color character mode rendering of the 32 × 16 world map                     |
| `screen.cc` | shared mode transitions: enter static MCCM, begin text page, restore simulation    |
| `sim.cc`    | the flight screen and its input dispatch                                           |

`keys.cc` provides the two primitives every screen needs: `keys_edges()` for
rising-edge detection on toggle keys, and `keys_wait_release()` for momentary
ones.

### `title.cc` — the menu flyby

Four **multicolour** sprites in a 2 × 2 block — 48 × 42 screen pixels of one
aeroplane — cross the page every six seconds or so, taking one and a half to
two and a half seconds about it.

Every flyby is the same aeroplane at the same angle at the same speed: two
pixels left and two lines down per frame, always. The art is a fixed
silhouette, so the direction of travel has to match the direction it is
pointing or it reads as a skid, and a second speed would want a second
silhouette to be honest about it. **What varies between flybys is only where
the aeroplane comes in.**

Because the angle never changes, every path is one of a family of parallel
lines, and a 45° line down and to the left is the set of points where X + Y is
constant. So one number picks a flyby out of that family — the code calls it
the *lane*, to keep it apart from the 45° itself, which is not a choice anyone
makes. Everything else follows from it: where the aircraft crosses the top
edge, where it crosses the right edge, where it leaves, and how long it is on
screen.

Flybys **alternate** between two bands of lanes. The first comes in over the
right half of the top edge, the second over the top half of the right edge, and
so on; the count restarts when the page is painted. Alternating rather than
drawing the side at random is what makes the variety visible — those bands are
160 lanes and 99, so a single sweep across both would come in over the top
nearly twice as often as over the side, and a run of four the same way would
not be unusual. An LFSR stepped every menu frame then picks the lane within the
band, scaled to fit it by two shifts and an add (`(r >> a) + (r >> b)` is
`r × (2⁻ᵃ + 2⁻ᵇ)`); a remainder would use each band exactly and cost the
139-byte divmod routine that `tools/check_mul_div.py` fails the build over.

The lower lanes leave through the **left** edge rather than the bottom, and
that is the one case the VIC will not clip for you: hiding a 24-pixel sprite
column behind a left border that ends at X = 24 wants X = −24, and one step
below zero wraps the sprite round to the right of the same raster line. Parking
a column at X = 0 is the exact answer rather than an approximation — a sprite
there spans 0–23 and the picture starts at 24, so it is completely hidden, and
every negative position is equally hidden. `_title_program()` clamps the two
columns independently, so the column still partly on screen keeps its true
position and clips a pixel at a time while the other sits behind the border.
The aeroplane goes off the left edge column by column with nothing snapping.

### `mission.cc`

Ten missions (`kMissionCount`), from "01 AIRBORNE" through "10 FUEL
CHALLENGE". There is no per-mission struct: everything is held in parallel
arrays indexed by mission, which keeps each table a flat run of bytes the
6510 can index directly.

- **Start state** — `kMissionStartX/Y/Z`, `kMissionStartSpeed`,
  `kMissionStartThrottle`, `kMissionStartFuel`, plus `kMissionWindX/Y`
  (declared and zero everywhere; nothing reads them yet). The scale factors
  are documented per array in `mission.h` — e.g. `flight_eye_x = start_x << 16`.
- **Text** — `kMissionTitles` and `kMissionDesc`, shown by `menu.cc`.
- **Progress** — `mission_completed[]`, rendered as `@` or `.` in the menu's
  mission list.

**Waypoints** live in a second set of arrays, `kMissionWpX/Y` and
`kMissionWpConstraint`, pooled across all missions
(`kMissionWpCount` = 17). Each mission owns a half-open slice of that pool
given by `kMissionWpBegin[]` and `kMissionWpEnd[]`, so missions can share
waypoints — the runway-2 landing appears in four of them. `kWaypointDefault`
is the fallback nav target for a mission whose slice yields no usable point.

A constraint (`MissionWaypointConstraint`) says what has to be true at the
waypoint: nothing, landed, a minimum altitude, below 125 ft, or inverted.

`flight_init_from_mission()` applies the start state and unpacks the slice
into `flight_nav_point_x/y[6]` — hence at most six navpoints, which is what
the `N` key cycles through. Waypoints at `(0, 0)` are position-free
(altitude-only goals such as "climb to 1000 ft") and are skipped when building
the nav list; `flight_waypoint_nav[]` records which nav point, if any, each
waypoint maps to. `flight_current_wp` tracks progress, advanced in
`flight.cc` when the aircraft is inside the waypoint's tolerance box and
satisfies its constraint. Unmet constraints surface as warning text through
`flight_status_text()` and `msg_show()`.

Clearing the last waypoint sets `flight_status` to `FLIGHT_MISSION_COMPLETED`
and announces it for a few seconds, but it does **not** end the flight: the
pilot keeps flying until they crash, restart with `R` or quit with `Q`. Only a
crash freezes the model, which is what `flight_crashed()` in `flight.h` asks —
`flight_status` being non-zero is not the same question, and the crash statuses
are kept last in the enum so that predicate stays a single comparison.

---

## 8. Support modules

- **`fmath.cc`** — `_get_msb`, a division-free `_get_ratio` (6-bit
  shift-subtract), and LUT-based `_get_heading` (48 steps) and
  `_get_roll_angle` (60 steps).
- **`sound.cc`** — flight audio. Split by rate: `sound_update()` computes a
  25-byte shadow on the main line, `sound_blit()` copies it to `$D400` from
  `_switch_to_terrain`. See [sound.md](sound.md). A menu tune is planned as a
  separate module with its own owner — see [music.md](music.md).
- **`print.cc`** — screen text, plus a double-dabble BCD converter in assembly
  for the debug readouts.
- **`benchmark.cc`** — CIA2 timer A/B chained to a 32-bit cycle counter; all
  entry points compile to nothing unless a `__DEBUG_*__` flag is set.
- **`color.h`, `vic.h`, `cia.h`, `bool.h`** — constants and non-oscar64
  fallbacks so the code can be reasoned about outside the compiler.

---

## 9. Build targets

`c64o/Makefile`, with `-O2 -Op -Oa -Oi -Oz -Oo`:

| Target         | Purpose                                                       |
| -------------- | ------------------------------------------------------------- |
| `ppilot.prg`   | standard game release (`-D__ENABLE_SOUND__ -D__MAX_RAM__`)   |
| `ppilotd.prg`  | debug build (`-D__ENABLE_DEBUG__ -D__MAX_RAM__`)             |
| `polydemo.prg` | polygon rasterizer test bed (`-D__DEBUG_POLY__`)              |
| `vecdemo.prg`  | character-mode ground-dot prototype                           |
| `vectest.prg`  | correctness and cycle counts for the vector routines          |

`ppilot.prg` is around 36 KB as currently built. `OSCAR64_INCLUDE` at the top
of the Makefile points at the oscar64 include directory and may need adjusting.

---

## 10. The Python prototype

`lib/` models the C64 display well enough to design the horizon scheme offline,
and generates the tables the C64 code compiles in.

**Hardware model**

- `c64_colors.py` — the 16-color palette.
- `c64_graphics.py` — `C64Screen`, rendering both multicolor bitmap mode
  (background + screen RAM + color RAM + 8000 bytes of bitmap) and multicolor
  character mode (3 global colors + screen RAM + color RAM + a 2048-byte
  character set) to PNG.
- `c64_converter.py` — converts an MCBM image to MCCM, deduplicating character
  patterns within a pixel tolerance.

**Horizon generation**

- `roll_angle.py` — the `RollAngle` enum and the 60 valid angles. An angle is
  valid when its horizontal component is even (an MCCM pixel is two pixels
  wide) and one component is divisible by 8. Note the period table here is the
  true period, while `roll.cc` caps it at 8 for rendering.
- `frame_generator.py` — draws one 320×200 reference frame: solid ground, solid
  sky, and a Bayer-dithered gradient band of 0, 2 or 4 characters between them.
- `batch_generator.py` — renders all angles (and the alternate centers),
  collects the deduplicated global character set, and emits `chardefs`.
- `find_boxes.py` — finds the minimal repeating tile for each frame and emits
  `boxdefs`.
- `verify_defs.py` — checks the generated C against the Python originals.
- `renderer_engine.py` — reimplements the C64 renderer in Python from the
  generated `chardefs`/`boxdefs`, used to validate the scheme end to end.
- `spritedef.py` — the pre-rotated instrument needle bitmaps.

**Command line tools** (`tools/`)

| Script                  | Make target       | Output                                                                   |
| ----------------------- | ----------------- | ------------------------------------------------------------------------ |
| `generate_frame.py`     | —                 | one reference frame as PNG                                               |
| `generate_all.py`       | `make chardefs`   | all frames, plus `chardefs` and `boxdefs` for both Python and C          |
| `render_frame.py`       | —                 | one frame through `renderer_engine`                                      |
| `render_all.py`         | `make render`     | all frames through `renderer_engine`                                     |
| `flight_demo.py`        | `make demo`       | interactive roll/pitch demo                                              |
| `generate_sprites.py`   | `make sprites`    | `spritedef.bin` and `spritedef.py`                                       |
| `generate_gfx_chars.py` | `make gfx-chars`  | `c64o/gfx_chars.bin` — the font plus the quad and point character groups |
| `png2koa.py`            | `make panel`      | a `.koa` image from a 320×200 PNG — `c64o/panel.koa` from the panel art  |

`make data` runs the three generator targets together. The canonical flags for
each tool live in the root `Makefile`; the scripts resolve their own paths from
the repo root, so they can be run from any directory. Rendered frames and other
disposable output go to `out/`, which is gitignored — `chardefs`, `boxdefs`,
`spritedef` and `reference_frames/` are checked in and written in place.

Tests live in `tests/` and run with `make test`.

---

## 11. Known gaps and inconsistencies

- **Wind is unimplemented.** `kMissionWindX` / `kMissionWindY` are defined and
  zero for every mission; nothing reads them.
- **Map view is a placeholder.** Every non-ground cell renders as `.`; polygon
  objects, the plane's position, the nav points and the flight path are not
  shown. See [map.md](map.md) for the plan to replace it, which also lists
  three gaps in `screen_restore_simulation()` that the current char-mode map
  happens not to expose.
- **Prototype and C64 viewports differ.** `renderer_engine.py` uses a 32 × 15
  character viewport; the C64 uses 40 × 14. The Python renderer is a design
  tool, not a mirror of the shipped renderer.
- **The menu is silent.** Flight audio ships (`sound.cc`, [sound.md](sound.md));
  the title tune is planned but not built — see [music.md](music.md).
- No joystick, no takeoff/landing interaction with objects.
