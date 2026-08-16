# Plane Pilot RAM Footprint & Memory Map

This document provides a detailed breakdown of RAM usage by feature area and segment for **Plane Pilot (`ppilot.prg`)**.

Segments are categorized into:
* **Code**: Executable CPU instructions (`code`, `startup`).
* **Data**: Read-only lookup tables, compressed assets, data constants (`data`, `data_box`, `data_compr`).
* **BSS**: Dynamic uninitialized variables and scratch buffers (`bss`, `bss2`, hardware stack `$0200-$027F`).
* **ZP**: Zero Page memory registers (`$0060-$00FC`).
* **VRAM**: Fixed VIC-II Video RAM allocations (Char RAM `$E000-$E6FF`, Screen RAMs `$E800`/`$EC00`, Color RAM `$D800`, Sprite RAM `$E700`).

All numbers are derived directly from the compiled link map (`c64o/ppilot.map`) using `tools/analyze_ram.py`.

---

## Executive Memory Map Summary

| Feature Area | Code | Data | BSS | ZP | VRAM | **Total Footprint** |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **1. Horizon Graphics** | 3,352 B | 8,780 B | 398 B | 37 B | 3,792 B | **16,359 B (16.0 KB)** |
| **2. Polygon Graphics & 3D Math** | 5,213 B | 1,260 B | 201 B | 20 B | 0 B | **6,694 B (6.5 KB)** |
| **3. World Model & Flight Sim** | 5,570 B | 158 B | 542 B | 64 B | 0 B | **6,334 B (6.2 KB)** |
| **4. Instrument Panel (incl Sprites)** | 1,626 B | 1,076 B | 182 B | 8 B | 256 B | **3,148 B (3.1 KB)** |
| **5. Menu, Missions & Help** | 2,443 B | 3,041 B | 0 B | 0 B | 0 B | **5,484 B (5.4 KB)** |
| **6. Message System** | 345 B | 13 B | 40 B | 0 B | 0 B | **398 B (0.4 KB)** |
| **7. Sound Effects** | 802 B | 89 B | 25 B | 9 B | 0 B | **925 B (0.9 KB)** |
| **8. Music** | 497 B | 913 B | 0 B | 0 B | 0 B | **1,410 B (1.4 KB)** |
| **9. Debug Messages & Overlay** | 0 B | 0 B | 0 B | 0 B | 0 B | **0 B (0.0 KB)** |
| **10. Benchmarks & Timing** | 0 B | 0 B | 0 B | 0 B | 0 B | **0 B (0.0 KB)** |
| **11. Core System, Drivers & Runtime** | 4,258 B | 1,058 B | 307 B | 19 B | 1,000 B | **6,642 B (6.5 KB)** |
| **TOTAL** | **24,106 B** | **16,388 B** | **1,695 B** | **157 B** | **5,048 B** | **47,394 B (46.3 KB)** |

---

## Tool Usage

To regenerate or verify this analysis against a fresh build:

```bash
make ram
```

Or invoke the Python script directly:

```bash
python3 tools/analyze_ram.py [c64o/ppilot.map]
```

To output directly in Markdown format:

```bash
python3 tools/analyze_ram.py --markdown
```

Use `-v` or `--verbose` to list every individual symbol and its byte size by category and segment:

```bash
python3 tools/analyze_ram.py --verbose
```

---

## Detailed Breakdown by Feature

### 1. Horizon Graphics (16,359 B)
* **Code (3,352 B)**: Viewport horizon rendering, cell filling, box generation, slot drawing (`render.cc`, `box.cc`, `roll.cc`, `roll_asm.cc`).
* **Data (8,780 B)**: Box definitions (`g_boxdefs`), character definitions (`g_chardefs`), roll multiplication lookup tables (`g_roll_mul_table`, `g_roll_slopes`), compressed graphics data (`kGfxCharsCompressed`).
* **BSS (398 B)**: Frame working arrays (`_box_chars`, `_box_colors`).
* **ZP (37 B)**: Zero Page roll registers (`roll_dx`, `roll_dy`, `roll_period`, `render_cx_pixels`, etc.).
* **VRAM (3,792 B)**:
  * Character RAM at `$E000-$E6FF`: 1,792 B (box & solid character patterns)
  * Main Screen RAM at `$E800-$EBFF`: 1,000 B
  * Alt Screen RAM at `$EC00-$EFF7`: 1,000 B (double-buffered VIC-II screens)

---

### 2. Polygon Graphics & 3D Math (6,694 B)
* **Code (5,213 B)**: Polygon rendering pipeline, edge scan conversion, fixed-point vector math (`poly.cc`, `vec.cc`, `vec_asm.cc`, `fmath.cc`).
* **Data (1,260 B)**: Sine & Cosine tables (`g_sin_table`, `g_cos_table`), Inverse Z LUT (`g_inv_z_table`), multiplication LUTs.
* **BSS (201 B)**: Scratch vertex buffers (`poly_verts`, `clip3_buf`, `proj_buf`).
* **ZP (20 B)**: Zero Page vector registers (`vec_v`, `vec_sx`, `vec_sy`).
* **VRAM (0 B)**.

---

### 3. World Model & Flight Simulation (6,334 B)
* **Code (5,570 B)**: Flight dynamics, physics integration, waypoint checking, world terrain rendering (`flight.cc`, `world.cc`, `sim.cc`, `world_map.cc`).
* **Data (158 B)**: Orientation matrices (`mat3_rot`, `kHeadingLut`), constants.
* **BSS (542 B)**: Flight path history buffers (`flight_path_px`, `flight_path_py`), delta transform vectors (`_world_dx4`, `_world_dy4`), camera positions.
* **ZP (64 B)**: Zero Page flight state (`flight_eye_x/y/z`, `flight_speed`, `flight_throttle`, `flight_fuel`, `flight_vspeed`).
* **VRAM (0 B)**.

---

### 4. Instrument Panel & Sprites (3,148 B)
* **Code (1,626 B)**: Viewport split raster handlers, gauge updates, sprite hardware controller (`view.cc`, `panel.cc`, `sprites.cc`, `spritedef.cc`).
* **Data (1,076 B)**: Sprite Graphics data (`g_spritedef_bin`), character/color LUTs (`_char_lut`, `_color_lut`).
* **BSS (182 B)**: Raster IRQ split structures (`_rirq_panel_top`, `_rirq_panel_bottom`), sprite positions.
* **ZP (8 B)**: Zero Page sprite index & pointers.
* **VRAM (256 B)**: Hardware Sprite pointers & shape RAM at `$E700-$E7FF`.

---

### 5. Menu, Missions & Help System (5,484 B)
* **Code (2,443 B)**: Menu loop, mission cursor handler, help screen renderer (`menu.cc`, `mission.cc`, `help.cc`, `map.cc`).
* **Data (3,041 B)**: Instrument Panel Koala image (`g_panel_koa_lzo`), menu item text, mission definitions, help text.
* **BSS (0 B)**: Uses shared hardware stack memory (`$0200-$027F`).
* **ZP (0 B)**.
* **VRAM (0 B)**.

---

### 6. Message System (398 B)
* **Code (345 B)**: Status message display timer, line formatter, clear/restore routines (`msg.cc`).
* **Data (13 B)**: Format strings and delay counters.
* **BSS (40 B)**: Active message buffer (`_status_text`).
* **ZP (0 B)**.
* **VRAM (0 B)**.

---

### 7. Sound Effects (925 B)
* **Code (802 B)**: SID audio driver, engine sound generator, stall alarm, wind noise, volume control (`sound.cc`).
* **Data (89 B)**: Engine pitch tables (`kEngineFreq`), wind frequency table (`kWindFreq`), volume names (`kSoundVolumeNames`).
* **BSS (25 B)**: SID register shadow buffer (`sound_shadow`).
* **ZP (9 B)**: Zero Page sound registers (`_pwm_phase`, `sound_gen`, `sound_gen_seen`, `_rng`, `_v3_effect`, `_v3_frames`, `_stall_phase`).
* **VRAM (0 B)**.

---

### 8. Music (1,410 B)
* **Code (497 B)**: Music playback driver, tick handler, frequency converter (`music.cc`).
* **Data (913 B)**: Note lookup tables (`kMusicNoteTable`), note track streams (`kMusicLeadStart`, `kMusicBassStart`), volume mapping (`kMusicVolMap`), volume composition matrix (`kVolumeMix`), bit-packed gate/drum masks (`kMusicLeadOnBits`, `kMusicDrumBits`).
* **BSS (0 B)**.
* **ZP (0 B)**.
* **VRAM (0 B)**.

---

### 9. Debug Messages & Overlay (0 B)
* **Code (0 B)**: Disabled in standard release `ppilot.prg` (743 B in debug `ppilotd.prg`).
* **Data (0 B)**.
* **BSS (0 B)**.
* **ZP (0 B)**.
* **VRAM (0 B)**.

---

### 10. Benchmarks & Timing (0 B)
* **Code (0 B)**: Disabled in standard release `ppilot.prg` (168 B in debug `ppilotd.prg`).
* **Data (0 B)**.
* **BSS (0 B)**.
* **ZP (0 B)**.
* **VRAM (0 B)**.

---

### 11. Core System, Drivers & Runtime (6,642 B)
* **Code (4,258 B)**: Main entry point, VIC-II setup, raster IRQ core driver, LZO decompressor, oscar64 runtime utilities (`ppilot.cc`, `mem.cc`, `gfx.cc`, `screen.cc`, `keys.cc`, `bcd.cc`, `print.cc`).
* **Data (1,058 B)**: C64 startup header (`$0801-$0853`), screen row pointer tables, fill patterns.
* **BSS (307 B)**: Hardware C Stack (`$0200-$027F`), screen row pointer pointers, raster IRQ lists.
* **ZP (19 B)**: Zero Page compiler temporaries & kernel flags.
* **VRAM (1,000 B)**: Color RAM at `$D800-$DBE7`.

---

## Overall C64 Memory Utilization Summary

* **Total Available RAM**: 65,536 Bytes (64 KB)
* **Used Memory Footprint**: **47,394 Bytes** (~72.3% of total C64 RAM)
* **Free / Unallocated Memory**: **18,142 Bytes** (~27.7% of total RAM)

