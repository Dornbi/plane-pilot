# Plane Pilot RAM Footprint & Memory Map

This document provides a detailed breakdown of RAM usage by feature area for **Plane Pilot (`ppilot.prg`)**, categorized into **Code** (CPU instructions), **Static Data / Lookup Tables**, **Dynamic State (BSS, Zero Page, C Stack)**, and **Video RAM / Hardware Buffers**.

All numbers are derived directly from the compiled link map (`c64o/ppilot.map`) using `tools/analyze_ram.py`.

---

## Executive Memory Map Summary

| Feature Area | Code (CPU Inst.) | Static Data / LUTs | Dynamic BSS / ZeroPage | Video RAM / Hardware Buffers | **Total Footprint** |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **1. Horizon Graphics** | 3,514 B | 8,780 B | 435 B | 3,792 B | **16,521 B (16.1 KB)** |
| **2. Polygon Graphics & 3D Math** | 5,213 B | 1,260 B | 221 B | 0 B | **6,694 B (6.5 KB)** |
| **3. World Model & Flight Sim** | 5,730 B | 158 B | 606 B | 0 B | **6,494 B (6.3 KB)** |
| **4. Instrument Panel (incl Sprites)** | 2,118 B | 1,076 B | 190 B | 256 B | **3,640 B (3.6 KB)** |
| **5. Menu, Missions & Help** | 2,431 B | 3,104 B | 0 B | 0 B | **5,535 B (5.4 KB)** |
| **6. Message System** | 341 B | 13 B | 40 B | 0 B | **394 B (0.4 KB)** |
| **7. Sound Engine** | 802 B | 7 B | 32 B | 0 B | **841 B (0.8 KB)** |
| **8. Core System, Drivers & Runtime** | 5,027 B | 1,140 B | 335 B | 1,000 B | **7,502 B (7.3 KB)** |
| **TOTAL** | **25,176 B** | **15,538 B** | **1,859 B** | **5,048 B** | **47,621 B (46.5 KB)** |

---

## Tool Usage

To regenerate or verify this analysis against a fresh build:

```bash
python3 tools/analyze_ram.py [c64o/ppilot.map]
```

Use `-v` or `--verbose` to list every individual symbol and its byte size by category:

```bash
python3 tools/analyze_ram.py --verbose
```

---

## Detailed Breakdown by Feature

### 1. Horizon Graphics (16,521 B)
* **Code (3,514 B)**: Viewport horizon rendering, cell filling, box generation, slot drawing (`render.cc`, `box.cc`, `roll.cc`, `roll_asm.cc`).
* **Static Data / LUTs (8,780 B)**: Box definitions (`g_boxdefs`), character definitions (`g_chardefs`), roll multiplication lookup tables (`g_roll_mul_table`, `g_roll_slopes`), compressed graphics data (`kGfxCharsCompressed`).
* **Dynamic BSS / Zero Page (435 B)**: Frame working arrays (`_box_chars`, `_box_colors`), Zero Page roll registers (`roll_dx`, `roll_dy`, `roll_period`, `render_cx_pixels`, etc.).
* **Video RAM & Hardware Buffers (3,792 B)**:
  * Character RAM at `$E000-$E6FF`: 1,792 B (box & solid character patterns)
  * Main Screen RAM at `$E800-$EBFF`: 1,000 B
  * Alt Screen RAM at `$EC00-$EFF7`: 1,000 B (double-buffered VIC-II screens)

---

### 2. Polygon Graphics & 3D Math (6,694 B)
* **Code (5,213 B)**: Polygon rendering pipeline, edge scan conversion, fixed-point vector math (`poly.cc`, `vec.cc`, `vec_asm.cc`, `fmath.cc`).
* **Static Data / LUTs (1,260 B)**: Sine & Cosine tables (`g_sin_table`, `g_cos_table`), Inverse Z LUT (`g_inv_z_table`), multiplication LUTs.
* **Dynamic BSS / Zero Page (221 B)**: Scratch vertex buffers (`poly_verts`, `clip3_buf`, `proj_buf`), Zero Page vector registers.
* **Video RAM & Hardware Buffers (0 B)**.

---

### 3. World Model & Flight Simulation (6,494 B)
* **Code (5,730 B)**: Flight dynamics, physics integration, waypoint checking, world terrain rendering (`flight.cc`, `world.cc`, `sim.cc`, `world_map.cc`).
* **Static Data / LUTs (158 B)**: Orientation matrices (`mat3_rot`, `kHeadingLut`), constants.
* **Dynamic BSS / Zero Page (606 B)**: Flight path history buffers (`flight_path_px`, `flight_path_py`), delta transform vectors (`_world_dx4`, `_world_dy4`), camera positions, Zero Page state.
* **Video RAM & Hardware Buffers (0 B)**.

---

### 4. Instrument Panel & Sprites (3,640 B)
* **Code (2,118 B)**: Viewport split raster handlers, gauge updates, sprite hardware controller (`view.cc`, `panel.cc`, `sprites.cc`, `spritedef.cc`).
* **Static Data / LUTs (1,076 B)**: Sprite Graphics data (`g_spritedef_bin`), character/color LUTs (`_char_lut`, `_color_lut`).
* **Dynamic BSS / Zero Page (190 B)**: Raster IRQ split structures (`_rirq_panel_top`, `_rirq_panel_bottom`), sprite positions.
* **Video RAM & Hardware Buffers (256 B)**: Hardware Sprite pointers & shape RAM at `$E700-$E7FF`.

---

### 5. Menu, Missions & Help System (5,535 B)
* **Code (2,431 B)**: Menu loop, mission cursor handler, help screen renderer (`menu.cc`, `mission.cc`, `help.cc`, `map.cc`).
* **Static Data / LUTs (3,104 B)**: Instrument Panel Koala image (`g_panel_koa_lzo`), menu item text, mission definitions, help text.
* **Dynamic BSS / Zero Page (0 B)**: Uses shared hardware stack memory (`$0200-$027F`).
* **Video RAM & Hardware Buffers (0 B)**.

---

### 6. Message System (394 B)
* **Code (341 B)**: Status message display timer, line formatter, clear/restore routines (`msg.cc`).
* **Static Data / LUTs (13 B)**: Format strings and delay counters.
* **Dynamic BSS / Zero Page (40 B)**: Active message buffer (`_status_text`).
* **Video RAM & Hardware Buffers (0 B)**.

---

### 7. Sound Engine (841 B)
* **Code (802 B)**: SID chip audio driver, pitch modulation, noise & stall warning generators (`sound.cc`).
* **Static Data / LUTs (7 B)**: Master volume & SID init values.
* **Dynamic BSS / Zero Page (32 B)**: Noise frequency arrays (`kEngineFreq`, `kWindFreq`), SID state registers.
* **Video RAM & Hardware Buffers (0 B)**.

---

### 8. Core System, Drivers & Runtime (7,502 B)
* **Code (5,027 B)**: Main entry point, VIC-II setup, raster IRQ core driver, LZO decompressor, oscar64 runtime utilities (`ppilot.cc`, `mem.cc`, `gfx.cc`, `screen.cc`, `keys.cc`, `bcd.cc`, `print.cc`).
* **Static Data / LUTs (1,140 B)**: C64 startup header (`$0801-$0853`), screen row pointer tables, fill patterns.
* **Dynamic BSS / Zero Page (335 B)**: Hardware C Stack (`$0200-$027F`), screen row pointer pointers, raster IRQ lists, Zero Page temporaries.
* **Video RAM & Hardware Buffers (1,000 B)**: Color RAM at `$D800-$DBE7`.

---

## Overall C64 Memory Utilization Summary

* **Total Available RAM**: 65,536 Bytes (64 KB)
* **Used Memory Footprint**: **47,621 Bytes** (~72.7% of total C64 RAM)
* **Free / Unallocated Memory**: **17,915 Bytes** (~27.3% of total RAM)
