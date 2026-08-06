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
- **`$D000`** — RAM under I/O, outside `main`, currently unused, and useful for
  almost nothing *except* things the VIC reads. There is already precedent:
  sprite bitmaps live at `$D7C0`.

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
| `01` | screen RAM high nibble | **red (`kColorRed`) in every cell** — flight path and nothing else |
| `10` | screen RAM low nibble | per cell |
| `11` | colour RAM | per cell |

Pinning `01` to red in all 1000 screen cells is what makes the path drawable
anywhere without a per-cell colour negotiation, and it is why screen RAM is
write-once: the path only ever sets bit pairs to `01` in the *bitmap*.

Two free colours remain per cell, chosen independently. The tile generator
enforces this: a tile using more than two colours besides green and red is a
build error.

The aircraft is a sprite (phase 6), so it can be white without competing for a
bit pair.

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

Set the pair to red with `*addr = (*addr & ~(3 << shift)) | (1 << shift)`.

---

## 5. Tile data

### Format

```c
typedef struct {
  uint8_t bits[8];  // MCBM rows, four bit pairs each
  uint8_t lo;       // colour for bit pair 10 -> screen RAM low nibble
  uint8_t col;      // colour for bit pair 11 -> colour RAM
} map_tile_t;       // 10 bytes
```

Tiles needed:

| Group | Count | Source |
| --- | --- | --- |
| `MAP_DOT_GROUND` (`D__`) — dotted gridline | 1 | art |
| `MAP_DOT_BLACK/WHITE/CYAN/BLUE/YELLOW` | 5 | one shared pattern, `col` from `KWorldDotColors` |
| `MAP_OBJ_*` (runway … city) | 9 | art, `col` seeded from `kWorldObjColors` |
| digits `1`–`6` for navpoints | 6 | art, `lo` = white |

21 tiles × 10 bytes = 210 bytes, plus a small type→index lookup. Types are
`1..6` and `16..24`, so
`idx = type < kWorldMapObjStart ? type : type - kWorldMapObjStart + 7`.

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
sei
gfx_stop_raster_irqs(); vic.spr_enable = 0
vic_memptr = 0x48                    // screen $D000, bitmap $E000
vic.ctrl1 = 0x3b; vic.ctrl2 = 0xd8   // MCBM
vic.color_back = kColorGreen; vic.color_border = kColorBlack

// Pass A — I/O in
memset(0xE000, 0xFF, 8000)           // surround = solid 11
memset(kColorRam, kColorBlack, 1000)
for each of the 512 cells:
    copy tile.bits[0..7] to the cell's 8 bitmap bytes
    kColorRam[screen_offset] = tile.col
draw navpoint digit tiles over their cells
plot the flight path ring buffer          // bitmap only, bit pair 01

// Pass B — $01 = $34, I/O out
memset(0xD000, (kColorRed << 4) | kColorBlack, 1000)
for each of the 512 cells:
    screen[offset] = (kColorRed << 4) | tile.lo
$01 = $35
cli
```

The 8000-byte `memset` costs about one frame and is simpler than computing the
non-contiguous surround. Interrupts stay disabled across pass B so no handler
touches I/O while it is banked out.

`map_exit()`:

```
gfx_init_chars()             // NEW — rebuild the font at $E000
box_invalidate()             // NEW — clear box.cc _slot_def[0..1]
view_invalidate_bitmap()     // NEW — force panel bitmap re-expansion
screen_restore_simulation()
```

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
in `main`. `bss2` (`$0280–$0800`) is used to `$077C` and has no room.

Appended from the simulation loop:

```
compute px, py
if (px, py) != most recent entry: append, advance head
```

Comparison against the single most recent entry is sufficient. Maximum speed is
`kMaxSpeed = 0x0F00` ≈ 15 m per step; the smallest map pixel is 256 m, so the
position advances by at most one pixel per step. **Consecutive samples are
therefore always 4-neighbours** — the stored points already form a connected
path, so there is no line drawing, no interpolation, and consequently no
wrap-seam special case. Cost is about 30 cycles per frame.

At cruise a vertical pixel takes ~1.7 s, so 128 samples covers roughly 3.5
minutes of flight — about one full traverse of the map. Cleared by
`flight_init_from_mission()`.

---

## 8. Navpoints

`flight_init_from_mission()` unpacks the active mission's waypoint range into
`flight_nav_point_x/y[6]`, so there are at most six, numbered `1`–`6`. Each is
drawn by stamping the corresponding digit tile over the cell containing it,
overwriting whatever object art is there — as specified.

Digits use `lo` = white (bit pair `10`), which is free in that cell because the
object art it replaces is gone. Red stays available, so the path can still
cross a navpoint cell.

---

## 9. Phases

| # | Work | Notes |
| --- | --- | --- |
| 1 | Restore-path fixes (`gfx_init_chars`, `box_invalidate`, `view_invalidate_bitmap`) | ~40 bytes; testable against the existing char-mode map |
| 2 | Tile art `gfx/ppilot_map_tiles.png` + `tools/generate_map_tiles.py` + `make map-tiles` | |
| 3 | MCBM `map_enter()` / `map_exit()`, static map only | the bulk of the work |
| 4 | Navpoint digits | |
| 5 | Flight path ring buffer and plotting | |
| 6 | Aircraft sprite | white, re-enables one sprite in map mode |

---

## 10. Open questions

- **Concept art coverage.** `gfx/ppilot_map2.png` shows the gridlines and the
  object cells, but no navpoint numbers, no path and no aircraft. The final
  frame should be mocked up before phase 2 so the colour budget is checked
  against a real image rather than a table.
- **Grid colour.** The gridline tile consumes one per-cell pair in every empty
  cell. Dark grey is the assumption; it should be checked against green at
  actual size.
- **Does the path survive a mission restart?** Assumed cleared by
  `flight_init_from_mission()`. If it should persist across `R`, say so.
- **Runway cells vs navpoints.** Two `MAP_OBJ_RUNWAY` cells exist in
  `kWorldMap`; a mission's navpoints may or may not coincide with them. When
  they do, the digit wins.
