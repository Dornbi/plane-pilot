# Full-Screen Map View (`map.md`)

Pressed with `M` during flight (`c64o/map.{h,cc}`, `c64o/mapdefs.{h,cc}`), the map pauses the simulation and displays the entire 32 × 16-cell world in Multicolor Bitmap Mode (MCBM). Any key among `M`, `Space`, `Return`, or `Q` exits back to flight.

---

## 1. Scope

Shows the full 32 × 16-cell map (`256 × 128` bitmap pixels inside a `320 × 200` screen with a black surround):
- 18 terrain/object/grid tiles rendered from `kWorldMap`.
- Compass letters **`N`**, **`E`**, **`S`**, **`W`** stencilled in the black surround.
- Mission navpoints **`1`–`4`** (`ppilot`) or **`1`–`2`** (`fpilot`) in white.
- Recent **flight path** (up to 128 samples) in **white** (`01` bit pair).
- Aircraft position and heading marker as two overlapping **light-green** (`kColorLightGreen`) sprites.

---

## 2. Memory reuse & constraints

The map view allocates **zero extra DRAM** — it borrows memory already owned by flight and menu modes:

| Range | Size | In map view | Otherwise |
| --- | ---: | --- | --- |
| `$D000–$D3E7` | 1,000 B | Map screen RAM (`kMapScreenRam`) + sprite ptrs `$D3F8–$D3F9` | Title aircraft (menu) / 16 opponent sprite blocks (`fpilot` flight) |
| `$D800–$DBE7` | 1,000 B | Map colour RAM | Flight viewport/panel colour RAM |
| `$E000–$FF3F` | 8,000 B | Map MCBM bitmap (40 × 25 cells) | Char RAM, main/alt screens, panel bitmap |

**Margin at `$D000`:** The 48 static sprite bitmaps (`kSpriteDefBitmapCount = 48`, pointers 80–127) occupy `$D400–$DFFF`, leaving exactly `$D000–$D3FF` (1,024 B) for map screen RAM + sprite pointers — **24 bytes** to spare (`static_assert` in `map.cc`).

---

## 3. Colours

MCBM bit-pair assignment per 4×8-pixel cell:

| Bit pair | Source | Usage |
| --- | --- | --- |
| `00` | `$D021` (`COLOR_GROUND`) | Ground background (green in `fpilot`/Earth; scheme-driven in `ppilot`, [color_schemes.md](color_schemes.md)) |
| `01` | Screen RAM high nibble (`1` = white) | Reserved overlay ink: flight path, navpoint digits, compass letters |
| `10` | Screen RAM low nibble | Per-tile colour 1 (`MAP_TILE_COLORS`) |
| `11` | Colour RAM (`$D800`) | Per-tile colour 2 (`MAP_TILE_COLORS`) |

- Surround cells outside the 32 × 16 map area use black background (`0x00` screen/colour, white `0x10` on compass cells).
- Aircraft marker sprites (`$D027`, `$D028`) are **`kColorLightGreen`**.

---

## 4. Coordinates

### World to map cell

`kWorldMap` is indexed `[row][col]` where `row` (`0..15`) runs along world **X** (North/South) and `col` (`0..31`) runs along world **Y** (West/East), packed two nibbles per byte (`world_map_cell()`).

### The scale

- **1 world unit** (`flight_eye_* >> 16`) = **128 m**.
- **1 map cell** = 8 × 8 world units = **1,024 m** × **1,024 m**, drawn as **1 × 1 MCBM cell** (8 × 8 hires pixels = 4 × 8 multicolour pixels).
- **Full world:** 32 × 16 cells = **32,768 m × 16,384 m** (32.8 km × 16.4 km), wrapping seamlessly on both axes (X mod 128 u, Y mod 256 u).

### Map orientation

- Screen columns **4..35** (32 cols) correspond to world **Y** (`col = 0..31`, West left → East right).
- Screen rows **4..19** (16 rows) correspond to world **X** (`row = 15` at top = North → `row = 0` at bottom = South).

### World to map pixel

- `px = (world_y_px) & 127` (4 multicolour pixels per cell × 32 cells = 128 MCBM pixels = 256 hires pixels, offset +32 hires px for the 4-col border).
- `py = 127 - ((world_x_px) & 127)` (8 scanlines per cell × 16 cells = 128 lines, offset +32 lines for the 4-row border).

### Map pixel to bitmap address

Standard C64 bitmap cell addressing from base `$E000`: `cell_addr = $E000 + ((row * 40 + col) << 3) + (py & 7)`.

---

## 5. Tile data & generator (`tools/generate_map_tiles.py`)

The source of truth is `gfx/ppilot_map_tiles.png` (edited in GIMP; `make map-tiles` regenerates `c64o/mapdefs.{h,cc}`; `make map-preview` renders `out/map_preview.png`).

- **18 tiles total:** 4 gridline parity variants (`GRID_EVEN_EVEN .. GRID_ODD_ODD`) + 5 ground-dot types + 9 map objects (`MAP_OBJ_*`).
- **Colour budget:** `green` → `00` (ground), **`white` (`1`) → `01` (reserved — generator errors if tile art uses white)**, plus at most 2 per-tile colours mapped to `10` and `11`.
- **Packed footprint:**
  - `kMapTilePool[104]` — overlapping 8-byte tile bitmaps (104 B).
  - `kMapTileStart[18]` + `kMapTileColors[18]` — pool offsets and colour bytes (36 B).
  - `kMapDigitMask[4][8]` (`ppilot`, 32 B) / `[2][8]` (`fpilot`, 16 B) — white-on-green digit stencils read directly from `gfx/ppilot_map_tiles.png`.
  - `kMapCompassMask[4][8]` (32 B) — `N`, `E`, `S`, `W` stencils.
  - Total: **204 B (`ppilot`) / 188 B (`fpilot`)**.

---

## 6. Build & restore sequence

### `map_enter()`

1. Stop raster IRQs (`gfx_stop_raster_irqs()`, which silences flight SID), blank screen, clear `vic.spr_expand_x = 0` and `vic.spr_expand_y = 0`.
2. Switch VIC to MCBM (`$D011 = $3B`, `$D016 = $D8`, `$D018` pointing screen RAM to `$D000` and bitmap to `$E000`, `$D021 = COLOR_GROUND`).
3. Bank out I/O (`mmap_set(MMAP_RAM)`) while writing `$D000–$D3FF` screen RAM and sprite pointers, then immediately restore `MMAP_NO_ROM` for `$D800` colour RAM and VIC register writes (**no `cli` while in map mode** — raster IRQs stay off).
4. Draw the 32 × 16 map tiles (`_map_draw_layer()`), black surround, `_map_draw_compass()`, flight path, navpoint digits, and aircraft marker sprites (`kColorLightGreen`).
5. Unblank screen and wait for exit key (`M`/`Space`/`Return`/`Q`).

### `map_exit()`

1. `screen_blank()`, disable sprites, rebuild character RAM (`gfx_init_chars()`), invalidate cached horizon boxes (`box_invalidate()`), invalidate the panel bitmap (`view_invalidate_bitmap()`), and invalidate `fpilot` opponent sprites (`opponent_invalidate()`).
2. `screen_restore_simulation()` decompresses the panel back to `$F000`, restores VIC registers, and restarts the 3 raster IRQs.

---

## 7. Flight path

`flight.cc` records up to `kFlightPathLen = 128` `(px, py)` positions (1-back deduplicated, sampled on `whole_step`) in `kSpriteScratchPath` (256 B inside the already-decompressed `kSpriteDataCompressed` area). `map_enter()` connects consecutive points using 8-neighbour stepping when within a short threshold (preventing wrap-around streaks across world edges) and sets bit pair `01` (white).

---

## 8. Navpoints & compass

- **Navpoints:** `kMaxNavPoints` (4 in `ppilot`, 2 in `fpilot`, clamped in `flight_init_from_mission()`). Stencilled from `kMapDigitMask` in ascending order (`1..N`), clearing prior `01` pairs in each cell so overlapping navpoints (e.g. `ppilot` mission 07) remain legible.
- **Compass:** `_map_draw_compass()` stamps `N`, `E`, `S`, `W` into the black surround via `_map_draw_stencil()` using `kMapCompassMask`.

---

## 9. Aircraft marker sprite

Rendered with two overlapping long-arm sprites (pointers stored at `$D3F8–$D3F9`, colour **`kColorLightGreen`**):
- Heading `0..23` (`flight_get_heading()`) converts to 16-direction arm index `(2 * heading + 1) / 3` via a **subtractive loop** (`while (rem >= 3) { rem -= 3; dir++; }`) to avoid linking 16-bit division.
- Uses the mirrored 16-entry arm table pivoted at `(12, 10)`, with the second sprite offset by `(dx >> 1, dy >> 1)` to form a crisp pointer, handling `$D010` X MSB cleanly across columns 28–35.
