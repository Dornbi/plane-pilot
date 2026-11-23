# Plane Pilot & Fighter Pilot — Architecture Reference

Two C64 3D flight simulators built from one engine (`c64o/ppilot.cc`):

- **Plane Pilot (`ppilot.prg`)**: 10 peaceful missions (`pmission.cc`), per-mission colour schemes, sound, title music, and the `D` debug view.
- **Fighter Pilot (`fpilot.prg`, `-D__FPILOT__`)**: 10 combat missions (`fmission.cc`), 3D enemy fighters and bombers (`planes*.cc`, `opponent*.cc`), guns/tracers/smoke (`gun*.cc`), progressive damage (`damage.cc`), and mission debrief (`debrief.cc`); drops the debug view and colour schemes to fit 64 KB.

## Repository layout

- `c64o/` — C64 source (`.cc`/`.h`, targeted 6502 `__asm` in `*_asm.*`), build (`c64o/Makefile`), and 16 host C++ test suites + on-target VICE suite (`c64o/test/`).
- `lib/` — Python reference models, generators, and graphics libraries (`planes.py`, `music.py`, `chardefs.py`, `boxdefs.py`, `spritedef.py`, `clouddef.py`, `renderer_engine.py`, etc.).
- `tools/` — Data generators, link-time checkers, RAM analyser, VICE screenshot/benchmark scripts, and `ram_loop.sh`.
- `gfx/`, `screens/`, `bin/` — Source panel/tile art, generated CRT screenshots, and released `.prg` binaries.
- `tests/` — Python unit tests (`make test`).

See [development.md](development.md) for build flags, `Makefile` targets, debug view, and tests.

---

## 1. Core idea

A C64 cannot rasterise a tilted sky/ground gradient at 8–10 fps in bitmap mode, so the viewport uses **Multicolor Character Mode (MCCM)** with pre-rendered tiles:

- **60 roll angles** (`0..59`), each with a main `boxdef` (plus 8 alternates for half-character alignment = **68 `boxdef`s**, 4×5 to 6×16 chars).
- **333 unique characters** across all tiles; 107 are vertical flips of another, so only **224** are stored in one 728-byte overlapping superstring (`chardefs.cc`), copied backwards when flipped.
- Each active `boxdef` uses at most **29** characters (`kMaxBoxCharCount = 32`), copied on demand into one of two 32-character double-buffered slots in character RAM.
- Solid sky/ground fills the rest of the 40×14-char (320×112 px) viewport; ground dots and 3D polygons draw directly into screen/char RAM; clouds, sun, orientation mark, tail fin, opponents, tracers, and smoke are hardware sprites; the bottom 11 rows switch via raster IRQ to a **Multicolor Bitmap Mode (MCBM)** instrument panel.

---

## 2. Memory map

Exact per-feature byte totals and the 64 KB address-space walk live in [memory_map.md](memory_map.md) (`make pram` / `make fram`). High-level layout (`c64o/mem.h`):

| Range | Purpose |
| --- | --- |
| `$0000–$005F` | 6510 I/O port (`$00–$01`) + oscar64 runtime zero page (headroom checked by `check_zeropage.py`) |
| `$0060–$00FF` | Program zero page (160 B) |
| `$0100–$01FF` | 6502 hardware stack |
| `$0200–$023F` (`$021F` fpilot) | oscar64 static software stack frames (`#pragma stacksize(0x40)` / `0x20`, `#pragma heapsize(0)`) |
| `$0240–$085F` (`$0220` fpilot) | `bss2` (zeroed by `mem_init()`; reuses dead BASIC link `$0800` & startup stub `$0801–$0852`) |
| `$0860–$0A5F` | `boot` region (`_boot()` only); reused as `kBootScratch` (`box.cc` slot caches) after boot |
| `$0A60–$CFFF` (`$CFBF` fpilot) | Main code, data, BSS |
| `$CFC0–$CFFF` (fpilot only) | `kOpponentDotBlock` — 1 static opponent-dot sprite block outside I/O |
| `$D000–$D3FF` | RAM under I/O (1,024 B), time-shared: map screen RAM / title aircraft (4 sprite blocks) / fpilot's 16 dynamic opponent sprite blocks |
| `$D400–$DFFF` | 48 static sprite blocks (`kSpriteData`, pointers 80–127: orient mark, 14 clouds, sun, 32 gauge arms) |
| `$D800–$DBE7` | Color RAM (separate 1,000 × 4-bit I/O SRAM, off DRAM budget) |
| `$E000–$E7FF` | Character RAM (256 chars, VIC bank 3) |
| `$E800–$EBE7` / `$EBF8–$EBFF` | Main screen RAM + sprite pointers; in fpilot, rows 14–24 (`$EA30–$EBE7`, 420 B) double as `scratch`/`tscratch` during flight |
| `$EC00–$EFE7` / `$EFF8–$EFFF` | Alt screen RAM + sprite pointers |
| `$F000–$FF3F` | MCBM panel bitmap (rows 12–13 hold off-screen heading strips & ammo digits; rows 14–24 visible) |
| `$FF40–$FFBF` | Back-view vertical tail fin (2 sprite blocks, pointers 253–254) |
| `$FFC0–$FFF7` | `irqcode` region (14 B `_rirq_buf` raster stub) |
| `$FFFA–$FFFF` | Hardware NMI / RESET / IRQ vectors |

### Screen layout & raster split

- Rows 0–13 (rasters 50–161): 3D viewport in MCCM, double-buffered between `$E800` and `$EC00`.
- Rows 14–24 (rasters 162–249): MCBM instrument panel (or text debug view in `ppilot`).
- **Three raster IRQs** (`-DNUM_IRQS=3`, `gfx.cc` `kGfxRasterRows[3]`):
  1. **Raster 161** — `RIRQCode` stub (`_rirq_buf` at `$FFC0`) switches to MCBM panel top (`12` NOPs stock C64 / `19` NOPs SuperCPU in 1 MHz mode) and shows panel-top sprites.
  2. **Raster 187** — direct ISR switches to panel-bottom sprites.
  3. **Raster 252** — direct ISR switches back to MCCM terrain, commits terrain sprites, and runs `sound_blit()` when `sound_dirty` is set.

### Character RAM layout (`$E000–$E7FF`)

| Chars | Contents |
| --- | --- |
| `0..63` | 64-char ASCII font (decompressed from `gfx_chars.bin`) |
| `64..127` | Inverted font (status messages on row 13) |
| `128..159` | Active horizon `boxdef` slot 0 (32 chars) |
| `160..191` | Active horizon `boxdef` slot 1 (32 chars) |
| `192..223` | Solid fills (`192` sky, `193` ground), ground-dot bitmaps, polygon fill chars |
| `224..255` | 2×2 sub-cell pixel patterns (`kGfxGroundPoints`, `kGfxColorPoints`) |

### Colors

MCCM provides three shared screen colours (`$D021` background, `$D022` multi 1, `$D023` multi 2) plus per-cell colour RAM (`$D800`, values `0..7` with bit 3 set for multicolour).

- Default Earth palette (`color.h`): green ground, cyan/light-blue/blue sky gradient, orange ground dots, colour-RAM objects (black runway, yellow/grey/red buildings & fields, blue lakes).
- `ppilot.prg` adds four per-mission **colour schemes** (`Earth`, `Desert`, `Dusk`, `Night`) via `-D__ENABLE_COLOR_SCHEMES__` ([color_schemes.md](color_schemes.md)). `fpilot.prg` compiles the `COLOR_*` macros to Earth constants.

---

## 3. Frame loop (`sim.cc`)

`sim_run()` loops once per rendered frame until `Q` (or menu exit):

1. **Input:** `keys_update()`, toggle keys (`P`, `R`, `N`, `M`, `T`, `V`, `H`, `Q`, `D` in `ppilot`, views `1`–`4`), plus held flight controls (`I/J/K/L`, `A/S`, `+/-`, `F`, `G`, `B`, `Space` in `fpilot`).
2. **Model step (`MDL`):** Loops `sim_time_steps` times (1×, 2×, or 4× under `T` time compression, re-polling held keys on extra steps): `flight_advance()`, and in `fpilot` `opponent_step()`, `gun_step(trigger)`, ORing `flight_events` across substeps.
3. **Horizon & terrain (`SNP`, `BGR`, `CHR`, `DRW`, `GRD`/`PLY`):** `render_snap_center_chars()`, `render_fill_sky_ground()`, `box_prepare()`, `render_draw_tiles()`, `world_draw_terrain()` (ground grid dots + 3D polygons), `msg_restore_color()`.
4. **Post-render (`UPD`/`CLD`/`SPR`):** `view_update_cam()`, `sprites_stack_reset()`, `world_update_objects()` (clouds, sun, plus `opponent_add_sprites()` and `gun_add_sprites()` in `fpilot`), `sprites_stack_commit()`, `panel_update()`, `damage_frame()` (red hit flash in `fpilot`), `sound_update()`.
5. **Buffer flip (`COL`):** `mem_switch_buffer()` waits for the raster flip window, swaps `$E800`/`$EC00`, and copies the staged colour RAM to `$D800`.

For the `D` debug counters and caveats see [development.md](development.md).

---

## 4. Rendering the horizon

### `roll.cc` / `roll_asm.cc` — angle tables

Maps roll angle (`0..59`) to slope (`roll_dx`, `roll_dy`), repeat period (`roll_period`), and signed screen-pixel distance from the horizon (`roll_get_dist`). Caches the last `roll_angle` so steady-roll frames return immediately, and uses precomputed multiply tables and 6502 `__asm` for fast inner-loop queries.

### `render.cc` / `render_asm.cc` — snapping and background fill

- `render_snap_center_chars()` (hand-assembled in `render_asm.cc` via `_render_pull_and_snap_asm()` / `_render_snap_center_asm()`, with a level `d_min == 0` fast path and inlined `roll_get_dist()`) pulls the horizon centre near the viewport middle, snaps to character coordinates, and picks main vs alternate `boxdef`.
- `render_fill_sky_ground()` (hand-assembled in `render_asm.cc` via `_render_fill_sky_ground_asm()`) fills level and tilted sky/ground spans using the 14-row self-modifying sled (`_render_flush_span_core`, with a 6-step paired high-byte buffer sync and a zero-save/restore tail-jump fast path for `end_row == 14`) and a 2×-unrolled partial-row span helper, skipping colour-RAM writes on sky rows that remained sky from the previous frame.
- `render_draw_tiles()` stamps the active `boxdef` across the horizon band.

### `boxdefs.cc` / `box.cc` / `box_asm.cc` — tiles

- `boxdefs.cc` stores the 68 tile definitions and the 728-byte overlapping character superstring (`chardefs.cc`).
- `box.cc` (`box_prepare()`, `box_draw()`) and its 6502 assembly half `box_asm.cc` (`_box_prepare_fill()`, `_box_draw_asm()`) copy the active tile's unique characters (reversing byte order for vertically flipped chars) into the inactive 32-char RAM slot (`$01..$20` or `$61..$80`), returning early when the same `boxdef` is already resident, and stamp the horizon tiles across the 40×14 viewport (using a 4×-unrolled full-row fast path for level `step_y == 0` boxes and a self-modifying 2D copy loop for tilted horizons). Per-slot cache tables live in `kBootScratch` (`$0860–$0A5F`).

---

## 5. World and 3D

### `vec.h` / `vec.cc` / `vec_asm.cc` / `vec_lut.cc`

Fixed-point 8.8 vector and 3×3 matrix math (zero hardware multiply/divide, enforced by `check_mul_div.py`):
- **Quarter-square multiply (`vec_fastmul8p8`, `vec_fastsqr8p8`)** in `vec_asm.cc` using 512-byte square LUTs (`vec_lut.cc`), with zero-high-byte fast paths.
- **Exact restoring division (`vec_frac8`)** in `vec_asm.cc` for perspective projection and opponent rendering.
- **Perspective & rotation:** `vec_project()`, `vec_transform_inv()`, `vec_rotate_step()` (6-multiply incremental rotation from a 6-byte table), `vec_turn3_xy()` (horizontal yaw), and `vec_orthonormalize()`.

### `world.cc` / `world_asm.cc` / `world_map.cc` — ground detail

- `world_map.cc` defines the 32×16-cell repeating world (each cell = 8×8 world units = 1,024 m; total 32.8×16.4 km).
- `world.cc` / `world_asm.cc` build the per-frame grid basis (`_world_init_start_dx_dy()`), walk visible cells around the aircraft with 8-bit high-byte 3D frustum culling (`_world_render_grid_walk()`), project ground grid dots via `_world_draw_box_points()`, and build/dispatch map objects (`_world_build_obj_verts()`, runways, towns, cities, lakes, fields) to `poly_draw_3d()`. Host builds retain exact C counterparts in `world.cc`.
- `world_update_objects()` feeds sprites into the viewport sprite stack: clouds (`clouds.cc`), sun, and in `fpilot`, opponents (`opponent.cc`) and tracers/smoke (`gun.cc`).

### `poly.cc` / `poly_asm.{h,cc}` — filled polygons

3D convex polygon pipeline in character/2×2 sub-cell space (keeps 16-bit `int` intermediates safe against overflow — see `c64o/test/int16.h`):
1. 6502 `__asm` 3D-to-2D projection & clipping (`poly_asm.cc`: `_poly_project_vertices`, `_poly_clip_near_mixed`, `_poly_frustum_core`, `_poly_clip_2d_edge`), with exact host C counterparts in `poly.cc`: fast near-plane pre-scan (skips copying all-in polygons), near-plane clipping with zero-delta fast paths, tightened `0.75·x` / `0.25·x` 3D frustum early-reject, perspective projection directly into unified striped 2D buffers, and 2D Sutherland–Hodgman viewport clipping.
2. 6502 `__asm` rasteriser (`poly_asm.h`): `_poly_bounds_and_clear`, `_poly_trace_edges`, and `_poly_scan_lines2` (solid interior fills + 2×2 fringe characters at edges).

### `flight.cc` / `flight_asm.cc` — flight model

Arcade flight model (shipping in both `ppilot` and `fpilot`; optional angle-of-attack model under `FLIGHT_AOA=1`). On the C64 (`__OSCAR64__`), `flight_asm.cc` hand-assembles the arcade model's per-step speed/drag/lift/stall/ground-attitude/eye integration (`_flight_physics_move_asm`), 32-bit fuel burn (`_flight_fuel_step_asm`), and airborne turn rate (`_flight_turn_rot_asm`) over shared `vec_mul_ab` and ZP scratch, with exact host C counterparts in `flight.cc`. Handles lift/drag, stall, banked turns, ground roll/takeoff/landing/crash envelope, fuel/refuel, waypoints, SuperCPU substepping (`cpu_step_shift`), and `fpilot` damage/falling hooks. Full specification in [flight.md](flight.md); missions in [missions.md](missions.md).

---

## 6. Display and instruments

### `mem.cc` — buffers and modes

Initialises VIC bank 3 (`$C000–$FFFF`), banks out BASIC/KERNAL (`MMAP_NO_ROM`), zeroes `bss2`, decompresses the 48 static sprite bitmaps to `$D400` and the character set to `$E000`, and handles double-buffer swaps + colour-RAM copy.

### `gfx.cc` — raster IRQs, characters, points

- Installs the 3 raster IRQs (161, 187, 252) with stock (12 NOP) / SuperCPU (19 NOP + 1 MHz) panel-top handlers ([supercpu.md](supercpu.md)).
- Draws MCCM ground dots (`gfx_draw_point`) and updates the instrument panel:
  - **5 indicator lamps** (`--pin-color-ram` in `Makefile`: nav left/right, stall, flaps, gear), toggled by storing light red or black into colour RAM.
  - **Heading strip** and (`fpilot`) **ammo digits** (`--pin-slots`), copied from the off-screen head of the panel bitmap (`$F000–$F17F`).

### `view.cc` — the four views

Manages the four cockpit views (`1` left, `2` forward, `3` right, `4` back over tail), derives `world_cam` from `flight_cam`, decompresses the LZO-packed MCBM panel (`ppanel_*.lzo` or `fpanel_*.lzo`), and controls the orientation mark (front view) and 2-sprite vertical tail fin (back view).

### `sprites.cc` / `sprites_asm.h` / `sprites_asm.cc` / `spritedef.cc`

Shared depth-sorted viewport sprite stack (slots 1–7 in front/side views; 2–7 behind the tail fin in back view) plus instrument gauge needles on the panel. Core stack add/commit and instrument gauge updates are in 6502 `__asm` (`sprites_asm.cc`). Double-buffered via `mem_using_alt_buffer`, serving clouds, sun, opponents, tracers, and smoke. See [sprite_objects.md](sprite_objects.md) and [clouds.md](clouds.md) §1.

### `clouds.cc` / `clouddef.cc`

Procedural 3-blob cloud deck at 700 m across 128 hash-gated cells (37 active), 10-rung size ladder, far-group collapse, and global checkerboard dither lattice. See [clouds.md](clouds.md).

---

## 7. Screens & Combat

### `menu.cc`, `help.cc`, `debrief.cc`

- `menu.cc`: interactive 10-mission selector with completion checkmarks, title tune (`music.cc`), and `title.cc` flyby.
- `help.cc`: key reference screen (accessible from menu and in-flight via `H`).
- `debrief.cc` (`fpilot` only): post-flight debrief shown on `Q` (goals, 6-tier aircraft damage status, rounds fired/on target, enemy gun kills).

### `title.cc` — the menu flyby

Animates a multicolour title aeroplane across the menu screen using 4 sprite blocks time-shared in `$D000–$D3FF` (`kTitleSpriteData`).

### `map.cc` — the map view

Full-screen MCBM world map (`M`) reusing `$E000–$FF3F` for its bitmap and `$D000–$D3FF` for screen RAM, rendering 18 tiles (`mapdefs.cc`), compass letters (`N/E/S/W`), white flight path, navpoints, and a light-green aircraft sprite. See [map.md](map.md).

### `pmission.cc` / `fmission.cc`

Mission definitions, start poses, waypoints, navpoints, and (`pmission.cc` only) per-mission colour schemes. See [missions.md](missions.md).

### Combat modules (`fpilot` only)

- `planes.cc` / `planes_asm.cc` / `planedef.h` — filled-polygon 3D aircraft rasteriser into 16 dynamic sprite blocks (`$D000–$D3FF`) + 1 static dot block (`$CFC0`), split half-rate across frames ([planes.md](planes.md)).
- `opponent.cc` / `opponent_asm.cc` — 2-slot wave spawner, fighter/bomber flight model, hunting/strafing/break-off AI, bomber rear gunners, and falling downed aircraft ([opponent_ai.md](opponent_ai.md)).
- `gun.cc` / `gun_asm.cc` — 8-round eye-relative projectile pool, swept AABB hits/provokes/collisions, yellow tracers, black smoke/ground puffs, finite ammo (`600`), and tallies ([projectiles.md](projectiles.md)).
- `damage.cc` — armour (3–4 hits), 5 progressive system failures, red border flash, and shot-down dive ([opponent_ai.md](opponent_ai.md) §8).

---

## 8. Support modules

- `fmath.cc` — closed-form integer heading and roll-angle approximations (no LUTs).
- `sound.cc` — 3-voice SID flight SFX (`sound_update` → 25 B shadow → 22-reg `sound_blit` gated by `sound_dirty`). See [sound.md](sound.md).
- `music.cc` / `musicdef.cc` — 50 Hz PAL title tune player and packed 24-bar D-minor data. See [music.md](music.md).
- `cpu.cc` — boot-time CIA2 speed probe for SuperCPU detection ([supercpu.md](supercpu.md)).
- `keys.cc` — direct CIA1 keyboard matrix and Port 2 joystick scanner (`joy2_state`).
- `bcd.cc` — 6502 double-dabble binary-to-BCD converter.
- `print.cc` — character/number printing and debug readouts.
- `msg.cc` / `screen.cc` — timed row-13 HUD status messages and shared screen transitions/notices.
- `benchmark.cc` — CIA2 cycle counters for `ppilot`'s `D` view and `tools/bench.sh`.

---

## 9. Build & verification overview

Build commands, feature dials, binary list, and the 16 host + on-target test suites are documented in [development.md](development.md). Every link of `ppilot`, `fpilot`, and `polydemo` enforces five automated invariants:

1. `check_zeropage.py` — runtime ZP spill stays below `$0060`.
2. `check_irq_zp.py` — raster ISRs never touch oscar64 runtime ZP.
3. `check_mul_div.py` — no accidental compiler 16-bit multiply/divide library calls.
4. `check_rom_window.py` — no global reads from `$A000–$BFFF` or `$E000–$FFFF` before `mem_init()`.
5. `check_boot_region.py` — nothing calls into the `$0860` boot region after `_boot()` returns.

RAM footprints and headroom are tracked in [memory_map.md](memory_map.md); frame cycle benchmarks in [framerate.md](framerate.md) and [ram-loop.md](ram-loop.md).

---

## 10. Python reference & asset pipeline

`lib/` mirrors core C64 subsystems so assets and invariants can be generated and unit-tested in Python (`make test`):
- Horizon tiles & charset: `lib/chardefs.py`, `lib/boxdefs.py`, `lib/renderer_engine.py`, `lib/roll_angle.py` → `make chardefs`, `make gfx-chars`.
- Sprites, clouds, map tiles, panels: `lib/spritedef.py`, `lib/titledef.py`, `lib/clouddef.py`, `tools/png2koa.py`, `tools/generate_map_tiles.py` → `make sprites`, `make clouds`, `make map-tiles`, `make panel`.
- Music: `lib/music.py` → `make music` (`c64o/musicdef.{h,cc}` + `docs/sid-intro-theme.html`).
- 3D opponent renderer twin: `lib/planes.py` (bit-exact twin of `docs/planes-prototype.html` and `c64o/planes*.cc`, checked across 12,304 + 5,848 cases) → `make planes` (`c64o/planedef.h`).

---

## 11. Known gaps

- **Wind fields unused:** `kMissionWindX`/`kMissionWindY` exist in `mission.h` as zero placeholders not read by `flight.cc`.
- **Sprite left-edge wrap not built:** sprites pop off the left viewport edge instead of wrapping via PAL X-wrap ([clouds.md](clouds.md) §1.6).
- **Python vs C64 viewport:** `lib/renderer_engine.py` models a 320×200 full-screen preview; the C64 viewport is 320×112 above the panel.
- **SuperCPU wall-clock speed:** without a raster catch-up loop, 50 fps at `1/4` substep runs ~1.3–1.8× faster than a stock C64 ([supercpu.md](supercpu.md)).
