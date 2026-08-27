# Map View — Implementation Plan

The `M` key currently opens a placeholder: single-colour character mode, every
non-ground cell drawn as `.`. This document plans the real map view — a
multicolor bitmap rendering of the 32 × 16 world, with navpoint numbers, a
recent flight path, and (later) the aircraft as a sprite.

See [project.md](project.md) for the surrounding architecture.

---

## 1. Scope

| In | Out (for now) |
| --- | --- |
| MCBM rendering of `kWorldMap` | Live update while flying — the map is a pause screen |
| Pre-rendered 4 × 8 art per cell type | Scrolling or zooming |
| Navpoint numbers `1`–`6` | Map access from the main menu |
| Recent flight path, red, per pixel | Heading / track vector on the map |
| | Aircraft marker (sprite — phase 6) |

`sim.cc:175` already skips `flight_advance()` while `map_mode` is set, so the
simulation is frozen for the duration. Nothing on the map moves, and a
multi-frame rebuild on exit is invisible.

---

## 2. Decisions and rationale

### Multicolor bitmap, not character mode

The map needs a per-pixel flight path overlay. In MCCM that means allocating a
unique character per touched cell out of a 256-entry space, with a
pool-exhaustion failure mode. MCBM removes the allocator and the failure mode.

The usual objection — that a bitmap costs 8000 bytes — does not apply here.
`main` is `$0860–$D000`; the bitmap lands at `$E000–$FF3F`, which is outside it
and already occupied by character RAM, both screen buffers and the panel
bitmap. The map **time-shares** that region rather than allocating it. Net cost
to the scarce region is the tile table, the compositor, and the path ring
buffer — roughly 700 bytes.

### Runtime compositing, not a pre-rendered image

Three options were considered:

| | Data (in `$0860–$D000`) | Code | Sync with `world_map.cc` |
| --- | --- | --- | --- |
| Pre-rendered `.koa` + LZO | ~400–1000 | ~0 | Python must parse C, or `world_map.cc` becomes generated |
| Runtime composite, art in C | ~150 | ~250 | free |
| **Runtime composite, art from PNG** | **~150** | **~250** | **free** |

The third wins. Art is drawn in GIMP as a tile strip and converted by a Python
tool into 8-byte MCBM patterns; the C64 reads `kWorldMap[][]` at runtime and
stamps tiles. Python never reads C, so there is no parser to keep honest and
no generated file to go stale.

A pre-rendered image also cannot bake in the navpoint numbers — those depend
on the active mission (`kMissionWpBegin/End`, unpacked in
`flight_init_from_mission()`) — so the runtime drawing code has to exist either
way. Compositing the static cells on top of it costs about 120 extra bytes.

### Screen RAM at `$D000`

Bitmap base is already `$E000` (`$D018` bit 3; the panel IRQ writes `#$b8`).
Only the video matrix base changes. Candidates within VIC bank 3:

- **`$CC00`** — normal RAM, but it is the top 1 KB of `main`, the region that is
  actually scarce.
- **`$D000`** — RAM under I/O, outside `main`, and useful for almost nothing
  *except* things the VIC reads. There is already precedent: `mem_init()` banks
  `$01 = MMAP_RAM` and expands the sprite bitmaps to `$D7C0`.

Note that only `$D000–$D7BF` is free. `kSpriteDefBitmapCount` is 33 bitmaps of
64 bytes starting at index 95, so the sprite data occupies `$D7C0–$DFFF`
exactly, ending where character RAM begins. Screen RAM at `$D000–$D3E7` fits
with 984 bytes to spare.

`$D000` it is. `vic_memptr = 0x48`. The cost is that CPU writes there need
`$01 = $34`, which is acceptable because — see §4 — screen RAM is written once
per `map_enter()` and never touched again.

---

## 3. Memory and colours

### Memory map during map mode

| Address | Contents |
| --- | --- |
| `$D000–$D3E7` | screen RAM (bit pairs `01` and `10`), written once, banked |
| `$D800–$DBE7` | colour RAM (bit pair `11`) |
| `$E000–$FF3F` | bitmap — destroys char RAM, both screen buffers, panel bitmap |

### Colour assignment

| Bit pair | Source | Colour |
| --- | --- | --- |
| `00` | `$D021` (global) | green (`kColorGreen`) — ground base |
| `01` | screen RAM high nibble | **white (`kColorWhite`) in every cell** — overlay layer |
| `10` | screen RAM low nibble | per cell |
| `11` | colour RAM | per cell |

Pinning `01` to white in all 1000 screen cells creates a single **overlay
layer** that can be drawn anywhere without per-cell colour negotiation. Both
the flight path and the navpoint digits live in it, and both are drawn the same
way: set bit pairs to `01` in the *bitmap*, on top of whatever object art is
already there. Nothing is erased.

That is also why screen RAM is write-once — neither overlay ever touches it.

Two free colours remain per cell for the object art, chosen independently. The
tile generator enforces this: a tile using more than two colours besides green
and white is a build error.

The aircraft is a sprite (§9), so it carries its own colour and competes for
no bit pair at all. It is light red, which also keeps it distinct from the
white trail it sits on the end of.

Border is black. The 8-cell-wide surround outside the map is bitmap `0xFF`
(all `11`) with colour RAM black, matching the existing `kCharSolid11` idiom.

---

## 4. Coordinates

### World to map cell

`flight.cc:_on_runway()` already defines the mapping, and it is worth stating
explicitly because **x selects the row and y selects the column**:

```
row = ((uint8_t)((flight_eye_x >> 16) + 0x04) >> 3) & kWorldMapHeightMask  // 0..15
col = ((uint8_t)((flight_eye_y >> 16) + 0x04) >> 3) & kWorldMapWidthMask   // 0..31
```

`flight_eye_*` is 24.8 fixed-point metres, so `>> 16` gives 256-metre world
units. A cell is 8 units = 2048 m. The world is therefore 65.5 km × 32.8 km,
wrapping at 128 units in x and 256 units in y. The `+ 0x04` centres cells on
multiples of 8.

Verified against the mission data: waypoint 02 (`0x20, 0x3F`) → `[4][8]` and
waypoint 06 (`0x60, 0xBF`) → `[12][24]`, both `MAP_OBJ_RUNWAY` in `kWorldMap`.

### Map orientation

`world_map.cc` is stored with N down, W right, S up, E left. The displayed map
is rotated 180° to put N up and W left. This affects every consumer, so it
lives in one place:

```
screen_row = 15 - map_row
screen_col = 31 - map_col
```

The map occupies character rows 4–19, columns 4–35, of the 40 × 25 screen.

### World to map pixel

MCBM gives 4 multicolor pixels across and 8 rows down per cell, so the map area
is exactly 128 × 128 map pixels:

```
py = 127 - ( ((uint8_t)((flight_eye_x >> 16) + 0x04)     ) & 0x7F )   // from x
px = 127 - ( ((uint8_t)((flight_eye_y >> 16) + 0x04) >> 1) & 0x7F )   // from y
```

That is: one shift, one mask, one complement per axis. `py >> 3` and `px >> 2`
agree with `screen_row` and `screen_col` by construction.

A vertical map pixel is 1 world unit (256 m) over 1 screen pixel; a horizontal
map pixel is 2 world units (512 m) over 2 screen pixels. **256 m per screen
pixel in both axes — the map is geometrically square.** The 4 × 8 sub-cell
granularity is asymmetric only in the addressing, not on screen.

### Map pixel to bitmap address

```
addr  = 0xE000 + ((4 + (py >> 3)) * 40 + (4 + (px >> 2))) * 8 + (py & 7)
shift = (3 - (px & 3)) * 2
```

Set the pair into the overlay layer with
`*addr = (*addr & ~(3 << shift)) | (1 << shift)`.

---

## 5. Tile data

### Format

Unpacked parallel arrays, not a struct. A 10-byte struct would need a
multiply by 10 on every access, which the 6510 has no addressing mode for.
This also matches how `mission.cc`, `boxdefs.cc` and `world_map.cc` already
store their tables.

```c
// Transposed: row index outer, tile index inner.
extern const uint8_t kMapTileRows[8][kMapTileCount];
extern const uint8_t kMapTileLo[kMapTileCount];   // bit pair 10 -> screen low nibble
extern const uint8_t kMapTileCol[kMapTileCount];  // bit pair 11 -> colour RAM
```

With the transpose, the compositor's inner loop is eight indexed loads sharing
one index register and a compile-time-constant base per row:

```
lda kMapTileRows[0], x   ; x = tile index
sta dst + 0
lda kMapTileRows[1], x
sta dst + 1
...
```

No multiply and no pointer arithmetic anywhere — the `<< 3` that a
`[kMapTileCount][8]` layout would need disappears too.

Tiles needed:

| Group | Count | Source |
| --- | --- | --- |
| `MAP_DOT_GROUND` (`D__`) — dotted gridline | 1 | art |
| `MAP_DOT_BLACK/WHITE/CYAN/BLUE/YELLOW` | 5 | one shared pattern, `col` from `KWorldDotColors` |
| `MAP_OBJ_*` (runway … city) | 9 | art, `col` seeded from `kWorldObjColors` |

15 tiles: 8 row arrays plus two colour arrays, so 10 bytes per tile, 150 bytes.
Plus a type→index lookup — types are `1..6` and `16..24`, so
`idx = type < kWorldMapObjStart ? type : type - kWorldMapObjStart + 7`.

Navpoint digits are **not** tiles. They are overlay stencils — see §8.

**Gridlines only in empty cells.** `MAP_DOT_GROUND` (`D__`) is the empty-cell
type and carries the dotted-gridline art; every other type draws its own tile
instead. The grid therefore has a gap wherever an object sits, which is what
the concept image shows.

### Generator

New tool `tools/generate_map_tiles.py`, `make map-tiles`:

1. Read `gfx/ppilot_map_tiles.png` — a strip of 4 × 8 multicolor cells, one per
   tile, drawn in GIMP alongside the existing `.xcf` assets.
2. Map palette colours to bit pairs using `lib/c64_colors.py`: green → `00`,
   red → `01` (**error** if art uses red — red is reserved for the path), the
   remaining two distinct colours → `10` and `11`.
3. Emit `c64o/mapdefs.cc` / `mapdefs.h`.

This follows the existing Python → C data flow (`chardefs`, `boxdefs`,
`spritedef`, `gfx_chars.bin`) rather than inverting it.

---

## 6. Build sequence

`map_enter()`:

```
gfx_stop_raster_irqs()               // = sei, and that is all it is
vic.spr_enable = 0
vic_memptr = 0x48                    // screen $D000, bitmap $E000
vic.ctrl1 = 0x2b; vic.ctrl2 = 0xd8   // MCBM, screen off for the build
vic.color_back = kColorGreen; vic.color_border = kColorBlack

// Pass A — I/O in
memset(0xE000, 0xFF, 8000)           // surround = solid 11
memset(kColorRam, kColorBlack, 1000)
for each of the 512 cells:                        // object layer
    copy kMapTileRows[0..7][idx] to the cell's 8 bitmap bytes
    kColorRam[screen_offset] = kMapTileCol[idx]
plot the flight path ring buffer                  // overlay, bit pair 01
draw navpoint digit stencils, ascending           // overlay, bit pair 01

// Pass B — $01 = $34, I/O out
memset(0xD000, (kColorWhite << 4) | kColorBlack, 1000)
for each of the 512 cells:
    screen[offset] = (kColorWhite << 4) | kMapTileLo[idx]
$01 = $35
vic.ctrl1 = 0x3b                     // screen on
```

The 8000-byte `memset` costs about one frame and is simpler than computing the
non-contiguous surround.

### No `cli` anywhere in map mode

`gfx_stop_raster_irqs()` is `rirq_stop()`, and oscar64's `rirq_stop()` is
**nothing but an `sei`** — it leaves `vic.intr_enable` set and relies on the
CPU mask alone. `rirq_start()` is the matching `cli`. So the raster split is
only masked, not disarmed, and a `cli` anywhere between `map_enter()` and
`map_exit()` restarts it on the next raster compare.

That failure is worth recognising on sight: `_switch_to_terrain()` puts rows
0–13 back into MCCM over a character set the map has overwritten, and
`_switch_to_panel_top()` leaves rows 14–24 in MCBM with `$d018 = $b8`, so the
screen splits into a band of light-blue character garbage above a band of map
tiles wearing colors read from the wrong video matrix.

Interrupts therefore stay masked for the whole life of the map view — as they
did for the character-mode placeholder — and `map_exit()`'s
`gfx_init_raster_irqs()` is what re-enables them. The banked-out window in
pass B needs no `sei` of its own as a result.

The display is blanked (`DEN` clear) for the whole build. Ten thousand bytes
of writes take several frames, and the alternative is watching the old
character RAM reinterpreted as a bitmap for the duration. A blanked screen also
has no badlines, so the passes run faster for free.

The tile index is recomputed in pass B rather than carried over from pass A.
Caching it would need a 512-byte scratch buffer in the region the whole design
exists to protect, to save work that happens once per `map_enter()`.

`map_exit()`:

```
screen_blank()               // display off before the font goes back
gfx_init_chars()             // rebuild the font at $E000
box_invalidate()             // clear box.cc _slot_def[0..1]
view_invalidate_bitmap()     // force panel bitmap re-expansion
screen_restore_simulation()
```

The blank is the same trick `map_enter()` opens with, and it is needed for the
same reason at this end: the map's bitmap is still on display and reads its
pixels from `$E000`, which is exactly where `gfx_init_chars()` expands the
character set. Without it the several frames of that expansion, and of the
panel bitmap that `screen_restore_simulation()` expands after it, are visible
as the charset being drawn as map tiles. `screen_blank()` is defined in
`screen.h`; nothing turns the display back on until the raster split restarts
inside `gfx_init_raster_irqs()`, whose own `$d011` writes always have `DEN`
set.

### Restore-path gaps

`screen_restore_simulation()` today calls `mem_init_mccm()`,
`view_refresh_panel()`, `gfx_init_raster_irqs()` and `sprites_init()`. That
covers the VIC registers, the panel screen and colour rows, and the sprite
pointers at `+1016`. Three things it does **not** cover, all of which fail as
silent visual corruption rather than a crash:

1. **The font.** `gfx_init_chars()` is called only from `ppilot.cc:18` at
   startup. Character RAM `$E000–$E7FF` is never rebuilt.
2. **The gradient tile characters.** `box.cc` caches in `_slot_def[2]` with no
   invalidate entry point. After map exit, `box_prepare()` sees
   `src_def == _slot_def[slot]`, concludes the slot is still valid, and skips
   the copy into character RAM that no longer holds it. Flying straight is the
   cache-hit case, so this shows up immediately.
3. **The panel bitmap.** `view_refresh_panel()` re-expands the bitmap only when
   `view_bitmap_state != VIEW_CENTER`. Map mode wipes `$F000–$FF3F` without
   touching that variable.

Roughly 40 bytes to fix, and worth landing first — see §9.

---

## 7. Flight path

A ring buffer of 128 map-pixel positions, 2 bytes each (`px`, `py`), 256 bytes
in `bss2` (`$0280–$0800`), at `$0300–$0400`. It did not fit there originally;
`poly.cc`'s `clip2_buf1`, `clip2_buf2` and `final_verts` moved to `main`'s bss
to make room, which leaves `bss2` with no free bytes at all. Cleared by
`flight_init_from_mission()`, so the trail is per-attempt and `R` wipes it.

It lives in `flight.cc`, not `map.cc`. That is the one place that can clear it
without a second call site, and `flight_advance()` is already where the
position is known — but it does mean `flight.cc`, otherwise a pure model file
with no rendering dependencies, knows the map's 128 × 128 pixel space. The
trade bought host test coverage: `flight_test` exercises the sampler directly,
which it could not do if the buffer sat behind `map.cc`.

Plotted into the overlay layer (§3) with the same "set pair to `01`" primitive
the navpoint digits use.

Appended from the simulation loop:

```
compute px, py
if (px, py) != most recent entry: append, advance head
```

Maximum speed is `kMaxSpeed = 0x0F00` ≈ 15 m per step; the smallest map pixel
is 256 m, so the position advances by at most one pixel **per axis** per step.
**Consecutive samples are therefore always 8-neighbours** — the stored points
already form a connected path, so there is no line drawing, no interpolation,
and consequently no wrap-seam special case. Cost is about 30 cycles per frame.

Not 4-neighbours, as an earlier draft of this section claimed: a step passing
near a cell corner crosses a row and a column boundary at once and lands
diagonally. Rare — one append in 385 in
`test_flight_path_samples_are_connected` — and it makes no difference to the
drawing, since two pixels touching at a corner still read as a line. The test
asserts the true bound, `dx ≤ 1 && dy ≤ 1 && dx + dy ≥ 1`.

### Comparing against only the most recent entry

The dedup looks at one entry back, so a position that dithers across a pixel
boundary appends `A, B, A, B, …` and spends the ring on two pixels. Measured
rather than assumed: over 266 full 128-entry rings flown with deliberately
oscillating yaw and pitch inputs, the *worst* ring still held 114 distinct
pixels, and `A, B, A` revisits accounted for about 0.5% of appends.

The trail cannot collapse because the aircraft cannot hover — it always moves
forward at up to 15 m per step, so it can only dither across a boundary during
the brief window where it turns through the tangent. The one input that does
reverse position exactly, `Z`/`X` while paused, moves by a fixed amount each
way and returns to the same pixel, which the existing one-entry comparison
already drops.

At cruise a vertical pixel takes ~1.7 s, so 128 samples covers roughly 3.5
minutes of flight — about one full traverse of the map. Cleared by
`flight_init_from_mission()`.

---

## 8. Navpoints

`flight_init_from_mission()` unpacks the active mission's waypoint range into
`flight_nav_point_x/y`, skipping position-free `(0, 0)` waypoints. Each
navpoint draws its digit into the **overlay layer** over the cell containing
it, on top of the object art rather than replacing it.

### Four navpoints, enforced in code

Walking `kMissionWpBegin/End` against the `(0, 0)` skip rule:

| Missions | Navpoints |
| --- | --- |
| 01, 02, 03, 04, 05, 10 | 1 |
| 07 | 2 |
| 06, 08, 09 | 4 |

Four is the cap, and it becomes the declared limit rather than an observation.
`flight.cc` currently sizes `flight_nav_point_x[6]`, `flight_nav_point_y[6]`
and `flight_waypoint_nav[6]`; all three drop to `kMaxNavPoints = 4`, saving 10
bytes.

Both index spaces stay in bounds: `flight_waypoint_nav` is indexed by
waypoint-within-mission and `flight_nav_point_*` by navpoint, and the largest
mission slice (`kMissionWpEnd - kMissionWpBegin`) is also 4. The unpack loop in
`flight_init_from_mission()` needs a clamp so a future five-waypoint mission
loses its extra waypoints instead of writing past the array:

```c
if (num_wp > kMaxNavPoints) num_wp = kMaxNavPoints;
```

Clamping the loop bound rather than breaking on `flight_num_nav_points`
covers *both* arrays with one comparison. Breaking on the navpoint counter
leaves `flight_waypoint_nav[i]` unbounded: a six-waypoint mission whose first
two are position-free would still reach `i == 5` with only four navpoints
counted.

### Digit source: a new multicolor asset

The map digits are **not** derived from `_FONT_CHARS`. That font is hires — one
bit per pixel, `0` = ink, 8 pixels wide — which is why the game's text is
legible in MCCM: text cells clear bit 3 of their colour RAM nibble and render
at full resolution, while terrain cells set it (`kColorSky | 0x08` in
`mem_init()`) and render multicolor.

The overlay is multicolor, so it needs 4-pixel-wide glyphs. Those are a
separate hand-made asset covering `1`–`4` only. Authored as bit pairs:

| Pair | Meaning | In the overlay |
| --- | --- | --- |
| `00` | light blue — the stroke | ink |
| `10` | brown — edge shading, **ignored** | background |
| `11` | background | background |

Rendered with ink = `00` and nothing else, `1`–`4` read cleanly at 4 × 8:

```
 .#..    ##..    ##..    ..#.
 ##..    ..#.    ..#.    .##.
 .#..    ..#.    ..#.    #.#.
 .#..    .#..    .#..    ###.
 .#..    #...    ..#.    ..#.
 .#..    ###.    ##..    ..#.
```

The art belongs in `gfx/ppilot_map_tiles.png` alongside the object tiles, so
all map art comes through one asset and one generator pass.

### Stencil format

A glyph is 4 multicolor pixels wide and 8 rows tall — **exactly one cell, byte
aligned**. So a digit needs no shifting and no per-pixel addressing: store one
mask byte per row with `11` in each ink pair, and the draw is eight iterations
of

```c
dst[r] = (dst[r] & ~mask[r]) | (mask[r] & 0x55);
```

`0x55` is `01 01 01 01`, so `mask & 0x55` deposits `01` in exactly the ink
positions and leaves every other pair of the underlying object art untouched.

For `'1'` (`FF 8F 0F CF CF CF 8B FF`) the masks are
`00 30 F0 30 30 30 30 00`.

Four digits × 8 bytes = **32 bytes**, and the generator derives the masks from
the same PNG:

```python
mask = sum(3 << (2 * p) for p in range(4) if (b >> (2 * p)) & 3 == 0)
```

### Collision: mission 07

Waypoints 7 and 8 are both `(0x60, 0xBF)` — fly inverted over runway 2, then
land on runway 2. Both resolve to cell `[12][24]`, so navpoints 1 and 2 land on
the same 8 × 8 area.

Rule: **the last one wins.** Draw ascending and let the last write stand. Note
the stencil only sets pairs, so a naive overlay would leave both glyphs
superimposed — the digit draw must clear the cell's overlay pairs to `00`
first, then apply the mask.

---

## 9. Aircraft sprite

Sprites survive map mode. Their bitmaps live at `$D7C0–$DFFF`, outside the
`$E000–$FF3F` the map bitmap claims, so the pre-rotated instrument needles are
still loaded and free to reuse. `spr_multi` is never set, so sprites are hires
with an independent colour each from `spr_color[n]` — both plane sprites can be
white with no global colour negotiation.

### Two crossed long arms

Both sprites use `kSpriteDefMetaLongArm` — a 14 px segment — placed **centred**
rather than pivoted, so each renders as a full 14 px line rather than a
half-length needle:

```
body: long arm, direction d,     centred on the aircraft position
wing: long arm, direction d + 8, centred slightly forward of it
```

Reading `spritedef.py`: directions `d` and `d + 16` share a `bitmap_idx` and
differ only in pivot (direction 0 → bitmap 96, pivot `(12, 17)`; direction 16 →
bitmap 96, pivot `(12, 3)`). A bitmap is therefore a **line segment spanning
between the two pivots**, and the pivot table only picks which end anchors to
the hub. Centring uses the same 16 bitmaps with no new sprite data.

**The centre is a constant.** The midpoint of `pivot[d]` and `pivot[d ^ 16]` is
`(12, 10)` for all 32 directions — checked for every entry in both arm tables.
So centring needs no table and no arithmetic:

```c
spr_x = plane_screen_x + 24 - 12;   // sprite regs position the top-left
spr_y = plane_screen_y + 50 - 10;
```

**The forward offset is also free.** `pivot[d ^ 16] - (12, 10)` is the fore
vector at half the arm length, 7 px. Halving it gives the "little bit forward"
offset without a new table:

```c
fx = (kSpriteDefMetaLongArm[d ^ 16].pivot_x - 12) >> 1;   // ~3.5 px forward
fy = (kSpriteDefMetaLongArm[d ^ 16].pivot_y - 10) >> 1;
```

Wing position is the body position plus `(fx, fy)`. Two sprites, two table
lookups, no new data at all.

### Details

- **Colour.** Light red (`kColorLightRed`), not white. White is the overlay
  layer, so a white marker would be indistinguishable from the flight path it
  sits on the end of. Sprites carry their own colour, so this costs nothing.
- **Heading conversion.** `_get_heading()` has 48 steps; sprites have 32, and
  both run clockwise from up once the map's 180° rotation is applied, so the
  conversion is a pure change of scale: `round(heading * 32 / 48)`, which is
  `(2 * heading + 1) / 3`. An earlier draft of this section proposed a 48-byte
  lookup table to avoid the division — but this runs once per `map_enter()`,
  not per frame, so spending 48 bytes of the scarce region to save one
  division is the wrong way round. The divide stays.
- **Sprite pointers.** Video matrix + 1016 is `$D3F8..$D3FF` in map mode —
  RAM under I/O, so the two pointers are written in pass B with the rest of
  the screen RAM. They sit past the 1000 bytes the `memset` covers, so only
  the two in use are touched; the other six stay disabled.
- **No cleanup on exit.** `sprites_show_*()` park every unused sprite at x=0
  and clear `$D010` every frame, and `sprites_init()` resets all eight
  colours, so the restore path already covers the marker.
- **X coordinate.** The map spans screen x 32–288, so sprite x runs 56–312 with
  the 24 px border offset. Both sprites cross 255 — `$D010` MSB handling is
  required.
- **Y coordinate.** Rows 4–19 → sprite y 82–210. Fits in 8 bits.
- **Set once.** The simulation is frozen in map mode, so both sprites are
  positioned in `map_enter()` and never updated.
- **Enable two.** `map_enter()` currently clears `spr_enable`; set it to the two
  sprites in use. `sprites_init()` from `screen_restore_simulation()` restores
  the instrument setup on exit.
- **Scale.** Body and wingspan are both 14 px ≈ 2 cells. Not to scale with
  anything, and the marker is roughly square — check at actual size.

---

## 10. Phases

| # | Work | Notes |
| --- | --- | --- |
| 1 | ~~Restore-path fixes (`gfx_init_chars`, `box_invalidate`, `view_invalidate_bitmap`)~~ | **done** — 13 bytes code, 8 bytes heap |
| 2 | ~~Tile + digit art `gfx/ppilot_map_tiles.png`; `tools/generate_map_tiles.py`; `make map-tiles`~~ | **done** — 18 tiles + 4 digits = 212 bytes |
| 3 | ~~MCBM `map_enter()` / `map_exit()`, object layer only~~ | **done** — verified pixel-identical to `render_map_preview.py` |
| 4 | ~~Overlay layer: `set_overlay_pixel()` + digit stencils; `kMaxNavPoints = 4` in `flight.cc` + clamp~~ | **done** — verified against `render_map_preview.py`, collision case included |
| 5 | ~~Flight path ring buffer, cleared by `flight_init_from_mission()`~~ | **done** — 3 new `flight_test` cases |
| 6 | ~~Aircraft sprites — two centred long arms, 48→32 heading conversion, `$D010` handling~~ | **done** — light red, not white |

---

## 11. Open questions

- **White-on-white.** The overlay is a single colour, so where the path crosses
  a navpoint cell the digit and the trail are indistinguishable. The digit wins
  its own cell — the stencil clears the cell's overlay pairs before drawing —
  so this only shows as a trail that stops a pixel short. Worth a look on
  hardware. `01` was going to be red, so swapping the two layers apart later
  costs nothing but a constant.
- **Aircraft marker size.** Two crossed 14 px arms make a roughly square marker
  about 2 × 2 cells on a 128 × 128 map (§9). If that is too heavy, the short
  arm is the drop-in alternative for one or both.

### Settled

- Gridlines appear only in cells with nothing else in them (§5), and the grid
  colour reads against green at actual size. The concept art in
  `gfx/ppilot_map2.png` has been superseded by the real tile sheet, which
  `make map-preview` composites over `kWorldMap` for judging as a whole map.
- The path is cleared by `flight_init_from_mission()` (§7).
- **Digit `1` reads fine** without its base serif, on hardware, at actual size.
  Digit contrast over the object art is fine too — the digits overlay rather
  than replace, and white holds up over every tile.
- Towns and cities are interchangeable; mission 08's "cities" including two
  `MAP_OBJ_TOWN` cells is not a defect.
