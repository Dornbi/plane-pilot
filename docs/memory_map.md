# Plane Pilot RAM Footprint & Memory Map

This document provides a detailed breakdown of RAM usage by feature area for **Plane Pilot (`ppilot.prg`)**, categorized into **Code** (CPU instructions), **Static Data / Lookup Tables**, **Dynamic State (BSS, Zero Page, C Stack)**, and **Video RAM / Hardware Buffers**.

All numbers are derived directly from the compiled link map (`c64o/ppilot.map`) and memory configuration (`c64o/mem.h`).

---

## Executive Memory Map Summary

| Feature Area | Code (CPU Inst.) | Static Data / LUTs | Dynamic BSS / ZeroPage | Video RAM / Hardware Buffers | **Total Footprint** |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **1. Horizon Graphics** | 5,512 B | 8,816 B | 455 B | 3,792 B | **18,575 B (18.1 KB)** |
| **2. Polygon Graphics & 3D Math** | 5,213 B | 1,224 B | 239 B | 0 B | **6,676 B (6.5 KB)** |
| **3. World Model & Flight Sim** | 4,733 B | 673 B | 568 B | 0 B | **5,974 B (5.8 KB)** |
| **4. Instrument Panel (incl Sprites)** | 2,118 B | 2,813 B | 190 B | 256 B | **5,377 B (5.3 KB)** |
| **5. Menu, Missions & Help** | 1,507 B | 852 B | 0 B | 0 B | **2,359 B (2.3 KB)** |
| **6. Message System** | 264 B | 33 B | 40 B | 0 B | **337 B (0.3 KB)** |
| **7. Sound Engine** | 802 B | 7 B | 32 B | 0 B | **841 B (0.8 KB)** |
| **8. Core System, Drivers & Runtime** | 5,027 B | 1,120 B | 335 B | 1,000 B | **7,482 B (7.3 KB)** |
| **TOTAL** | **25,176 B** | **15,538 B** | **1,859 B** | **5,048 B** | **47,621 B (46.5 KB)** |

---

## Detailed Breakdown by Feature

### 1. Horizon Graphics (18,575 B)
* **Code (5,512 B)**:
  * Viewport horizon rendering & cell filling (`render.cc`, `render_snap_center_chars`, `render_fill_sky_ground`): ~1.8 KB
  * Box generation & slot drawing (`box.cc`, `_draw_one_box`, `box_prepare`, `box_draw`): ~1.6 KB
  * Roll slope multiplication routines (`roll.cc`, `roll_asm.cc`, `_roll_mul_*`, `_call_roll_mul_*`): ~1.2 KB
  * Character definitions & setup (`chardefs.cc`, `boxdefs.cc`): ~0.9 KB
* **Static Data / LUTs (8,816 B)**:
  * `g_boxdefs`: 2,482 B (primary box definitions table)
  * `g_chardefs`: 2,130 B (primary character definitions table)
  * `g_roll_mul_table`: 2,560 B (precalculated roll multiplication LUT)
  * `g_roll_slopes`: 1,280 B (roll angle slope lookup table)
  * `kGfxCharsCompressed`: 618 B (LZO compressed character graphics data)
  * `g_alt_boxdefs` & `g_alt_chardefs`: 200 B (alternate box/character definitions)
* **Dynamic BSS / Zero Page (455 B)**:
  * `_box_chars` & `_box_colors`: 384 B (active frame box character and color working arrays)
  * Zero Page registers (`roll_dx`, `roll_dy`, `roll_period`, `render_cx_pixels`, `render_cy_pixels`, `render_alt_box`): 71 B
* **Video RAM & Hardware Buffers (3,792 B)**:
  * Character RAM at `$E000-$E6FF`: 1,792 B (generated box & solid character patterns)
  * Main Viewport Screen RAM at `$E800-$EBFF`: 1,000 B
  * Alt Viewport Screen RAM at `$EC00-$EFF7`: 1,000 B (double-buffered VIC-II screens)

---

### 2. Polygon Graphics & 3D Math (6,676 B)
* **Code (5,213 B)**:
  * Polygon 3D rendering pipeline & edge scan conversion (`poly.cc`, `poly_draw_3d`, `_scan_lines2`, `_trace_edge_bresenham`): ~2.4 KB
  * Fixed-point 3D math & matrix transformations (`vec.cc`, `vec_asm.cc`, `vec_transform3`, `vec_fastmul8p8`): ~2.1 KB
  * Clipping and fast math helpers (`fmath.cc`, `_clip_2d`, `_project_vertices`): ~0.7 KB
* **Static Data / LUTs (1,224 B)**:
  * Sine & Cosine tables (`g_sin_table`, `g_cos_table`): 512 B (256 bytes each)
  * Inverse Z division LUT (`g_inv_z_table`): 512 B
  * Bit shift and fast math multiplication LUTs (`__multab*`, `__shrtab*`): ~200 B
* **Dynamic BSS / Zero Page (239 B)**:
  * Scratch clipping & projection vertex buffers (`poly_verts`, `clip3_buf`, `proj_buf`, `clip2_buf1/2`, `_min_x`, `_max_x`): 208 B
  * Zero Page vector registers (`vec_v`, `vec_sx`, `vec_sy`, `project_mul_a/b`, `mul_res`): 31 B

---

### 3. World Model & Flight Simulation (5,974 B)
* **Code (4,733 B)**:
  * Flight physics & control surfaces (`flight.cc`, `flight_advance`, `_flight_update_nav`, `_flight_move_forward`): ~2.6 KB
  * World terrain grid rendering (`world.cc`, `world_render_grid`, `_world_render_object`): ~1.2 KB
  * Physics simulation step (`sim.cc`): ~0.5 KB
  * World map loader (`world_map.cc`): ~0.4 KB
* **Static Data / LUTs (673 B)**:
  * World Map matrix & tiles (`g_world_map`): 512 B
  * Direction & orientation matrices (`mat3_rot`, `kHeadingLut`): 83 B
  * Mitchell points & constants: 78 B
* **Dynamic BSS / Zero Page (568 B)**:
  * Flight path history buffers (`flight_path_px`, `flight_path_py`): 256 B
  * Delta transform vectors (`_world_dx4`, `_world_dy4`, `_world_dx_vec`, `_world_dy_vec`): 132 B
  * Camera positions (`world_cam`, `flight_cam`): 36 B
  * Zero Page flight state (`flight_eye_x/y/z`, `flight_speed`, `flight_throttle`, `flight_fuel`, `flight_vspeed`): 44 B

---

### 4. Instrument Panel & Sprites (5,377 B)
* **Code (2,118 B)**:
  * Viewport split & raster IRQ switches (`view.cc`, `_switch_to_panel_top/bottom`, `_switch_to_terrain`): ~1.0 KB
  * Instrument panel gauges update (`panel.cc`, `panel_update_instruments`): ~0.6 KB
  * Sprite hardware controller (`sprites.cc`, `spritedef.cc`, `_set_instrument_sprite`): ~0.5 KB
* **Static Data / LUTs (2,813 B)**:
  * Instrument Panel Koala image (`g_panel_koa_lzo` compressed): 2,219 B
  * Sprite Graphics binary data (`g_spritedef_bin`): 440 B
  * Color and character lookup tables (`_char_lut`, `_color_lut`): 154 B
* **Dynamic BSS / Zero Page (190 B)**:
  * Raster IRQ split structures (`_rirq_panel_top`, `_rirq_panel_bottom`, `_rirq_terrain`): 96 B
  * Instrument sprite positions (`_sprite_instrument_xy`): 16 B
  * Zero Page sprite indexes & pointers (`_sprite_instrument_idx`): 14 B
* **Video RAM & Hardware Buffers (256 B)**:
  * Hardware Sprite pointers & shape RAM at `$E700-$E7FF`: 256 B

---

### 5. Menu, Missions & Help System (2,359 B)
* **Code (1,507 B)**:
  * Menu event loop & item rendering (`menu.cc`, `_render_menu_items`, `_enter_menu`): ~0.7 KB
  * Mission selection handler (`mission.cc`, `_draw_mission_cursor`): ~0.5 KB
  * Help screen pagination (`help.cc`): ~0.3 KB
* **Static Data / LUTs (852 B)**:
  * Menu text strings & navigation structures (`g_menu_items`): ~0.4 KB
  * Mission text descriptions & waypoints (`g_missions`): ~0.3 KB
  * Help page string data (`g_help_pages`): ~0.15 KB
* **Dynamic BSS / Zero Page (0 B)**:
  * Executed context uses shared C stack memory (`$0200-$027F`).

---

### 6. Message System (337 B)
* **Code (264 B)**:
  * On-screen status message renderer (`msg.cc`, `msg_show`, `msg_update`, `msg_render`): 264 B
* **Static Data / LUTs (33 B)**:
  * Fault text & waypoint reached format strings (`kFaultText`, `kWaypointFault`): 33 B
* **Dynamic BSS / Zero Page (40 B)**:
  * Active message buffer (`_status_text`): 40 B

---

### 7. Sound Engine (841 B)
* **Code (802 B)**:
  * SID chip audio driver (`sound.cc`, `sound_update`, `sound_wind_freq`, `_set_voice`): 802 B
* **Static Data / LUTs (7 B)**:
  * Master volume & SID init tables (`kMasterVolume`): 7 B
* **Dynamic BSS / Zero Page (32 B)**:
  * Noise frequency lookup tables (`kEngineFreq`, `kWindFreq`): 82 B (in BSS)
  * Zero Page SID sound state registers (`sound_gen`, `_pwm_phase`, `_rng`, `_v3_effect`): 12 B

---

### 8. Core System, Drivers & Runtime (7,482 B)
* **Code (5,027 B)**:
  * Main entry point & state loop (`ppilot.cc`, `main`): ~0.2 KB
  * Memory allocator & VIC setup (`mem.cc`, `mem_init`, `mem_switch_buffer`): ~0.7 KB
  * Raster IRQ driver (`gfx.cc`, `rirq_*`): ~1.1 KB
  * LZO decompressor (`oscar_expand_lzo`): ~0.4 KB
  * Screen, Keyboard & BCD print utils (`screen.cc`, `keys.cc`, `bcd.cc`, `print.cc`): ~1.2 KB
  * oscar64 CRT runtime (`$outline#*`, `divmod`, `memcpy`, `memset`): ~1.4 KB
* **Static Data / LUTs (1,120 B)**:
  * C64 startup header (`$0801-$0853`): 83 B
  * Row pointer lookups (`kScreenRowPtrsMain`, `kScreenRowPtrsAlt`, `mem_color_row_ptrs`): 128 B
  * Fast fill patterns & bitshift tables (`kFillPattern`, `bitshift`): 80 B
* **Dynamic BSS / Zero Page (335 B)**:
  * Hardware C Stack (`$0200-$027F`): 128 B
  * Screen row pointer pointers (`mem_screen_row_ptrs`): 50 B
  * Raster IRQ row lists (`rasterIRQRows`, `rasterIRQIndex`): 83 B
  * Zero Page compiler temporaries & kernel flags: ~74 B
* **Video RAM & Hardware Buffers (1,000 B)**:
  * Color RAM at `$D800-$DBE7`: 1,000 B

---

## Overall C64 Memory Utilization Summary

* **Total Available RAM**: 65,536 Bytes (64 KB)
* **Used Memory Footprint**: **47,621 Bytes** (~72.7% of total C64 RAM)
* **Free / Unallocated Memory**: **17,915 Bytes** (~27.3% of total RAM)
