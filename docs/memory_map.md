# Plane Pilot RAM Footprint & Memory Map

A breakdown of RAM usage by feature area and segment for **Plane Pilot
(`ppilot.prg`)**, and a walk of the whole 64 KB address space.

Every number below is generated from the compiled link map
(`c64o/ppilot.map`) by `tools/analyze_ram.py`. Regenerate the whole document
with:

```bash
python3 tools/analyze_ram.py --markdown
```

## Read the two tables differently

They count different things, and the difference is the point.

The **feature table** answers "what does this part of the program cost". It
adds up the symbols in the link map, plus the fixed allocations in the tool's
`FIXED` table, and attributes each to a feature. It is only ever as complete
as those two inputs.

The **address space walk** answers "how much room is left". It covers all
65,536 bytes and reconciles to exactly that, so a symbol nobody thought to
categorise shows up as memory missing from the walk rather than as free space.
Free RAM is read off the walk and nowhere else.

That distinction is not academic. This document used to report 18,142 bytes
free by subtracting the feature total from 64 KB, at a time when the real
figure was 6,154. Four allocations were simply not in the model: the
3,904-byte panel bitmap at `$F000`, the 3,072 bytes of sprite bitmaps at
`$D400`, 256 bytes of character RAM, and the 1,024-byte map screen buffer
under I/O. A fifth, "Sprite Data RAM at `$E700-$E7FF`", was in the model but
does not exist - those addresses hold character definitions 224-255
(`kGfxGroundPoints` and `kGfxColorPoints`).

Segments:

- **Code** - executable instructions (`code`, `startup`).
- **Data** - read-only tables, compressed assets, constants (`data`,
  `data_box`, `data_compr`).
- **BSS** - uninitialised variables and scratch buffers (`bss`, `bss2`, and
  the software stack frames at `$0200`).
- **ZP** - zero page, `$0060-$00FF`.
- **VRAM** - allocations the linker never sees, written at absolute addresses
  by `mem.h`, `view.cc`, `gfx.cc` and `map.cc`. These are the ones that have
  to be kept in sync by hand: the `FIXED` table at the top of
  `tools/analyze_ram.py` is the list, and each entry names the source that
  places it.

---

## Footprint by feature area

| Feature Area | Code | Data | BSS | ZP | VRAM | **Total Footprint** |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **1. Horizon Graphics** | 3,544 B | 9,338 B | 398 B | 37 B | 4,048 B | **17,365 B (17.0 KB)** |
| **2. Polygon Graphics** | 5,468 B | 1,260 B | 215 B | 20 B | 0 B | **6,963 B (6.8 KB)** |
| **3. World Model** | 6,458 B | 364 B | 589 B | 59 B | 0 B | **7,470 B (7.3 KB)** |
| **4. Instrument Panel** | 2,865 B | 1,370 B | 284 B | 10 B | 6,992 B | **11,521 B (11.3 KB)** |
| **5. Menu & Missions** | 2,598 B | 3,104 B | 0 B | 0 B | 1,024 B | **6,726 B (6.6 KB)** |
| **6. Message System** | 469 B | 13 B | 0 B | 2 B | 0 B | **484 B (0.5 KB)** |
| **7. Sound Effects** | 948 B | 92 B | 28 B | 8 B | 0 B | **1,076 B (1.1 KB)** |
| **8. Music** | 884 B | 791 B | 2 B | 11 B | 0 B | **1,688 B (1.6 KB)** |
| **9. Debug Messages & Overlay** | 0 B | 0 B | 0 B | 0 B | 0 B | **0 B (0.0 KB)** |
| **10. Benchmarks & Timing** | 0 B | 0 B | 0 B | 0 B | 0 B | **0 B (0.0 KB)** |
| **11. Core System & Drivers** | 4,839 B | 350 B | 213 B | 13 B | 0 B | **5,415 B (5.3 KB)** |
| **TOTAL** | **28,073 B** | **16,682 B** | **1,729 B** | **160 B** | **12,064 B** | **58,708 B (57.3 KB)** |

Color RAM is deliberately absent from the VRAM column. It is a separate
1000 x 4 bit array inside the I/O block, not part of the 64 KB of DRAM - the
DRAM behind `$D800` is the sprite bitmap block. Counting it here would book
the same addresses twice.

---

## Address space walk

Every byte of the 64 KB, in order. Three kinds of free space, because they
are not equally reachable:

- **free** - inside a linker region, so the compiler can still use it. This is
  the number that matters when deciding whether a feature fits.
- **stack headroom** - the unused part of the software stack region. oscar64's
  software stack grows _down_ from the end of its region, so the `sections`
  line reports what was never reached: `$0200 - $0251` on a `$0200..$0280`
  region means 47 bytes in use and 81 spare, not the other way round.
  Convertible to bss2 by lowering `#pragma stacksize` in `mem.h` and raising
  the bss2 region floor to match.
- **free (orphan)** - outside every linker region. Real RAM, but only
  reachable by placing something there by hand, the way `mem.h` and `view.cc`
  already do.

| Range | Bytes | State | Contents |
| :--- | ---: | :--- | :--- |
| `$0000-$0001` | 2 | used | 6510 processor port |
| `$0002-$005F` | 94 | used | oscar64 runtime zero page (reserved; headroom checked by check_zeropage.py) |
| `$0060-$00FF` | 160 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$0100-$01FF` | 256 | used | 6502 hardware stack |
| `$0200-$0250` | 81 | stack headroom | software stack headroom (see #pragma stacksize in mem.h) |
| `$0251-$02F9` | 169 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$02FA-$02FF` | 6 | free | free, inside a linker region |
| `$0300-$04EF` | 496 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$04F0-$04FF` | 16 | free | free, inside a linker region |
| `$0500-$05F5` | 246 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$05F6-$05FF` | 10 | free | free, inside a linker region |
| `$0600-$06F5` | 246 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$06F6-$06FF` | 10 | free | free, inside a linker region |
| `$0700-$07FF` | 256 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$0800-$0800` | 1 | used | BASIC link byte |
| `$0801-$0852` | 82 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$0853-$085F` | 13 | free | free, inside a linker region |
| `$0860-$753E` | 27,871 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$753F-$7551` | 19 | free | free, inside a linker region |
| `$7552-$ACFA` | 14,249 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$ACFB-$ACFF` | 5 | free | free, inside a linker region |
| `$AD00-$B7EC` | 2,797 | used | linker-allocated: code, data, bss, zero page, stack frames |
| `$B7ED-$CFFF` | 6,163 | free | free, inside a linker region |
| `$D000-$D3FF` | 1,024 | used | map view screen RAM, under I/O (map.cc kMapScreenRam) |
| `$D400-$DFFF` | 3,072 | used | sprite bitmaps, 48 blocks (mem.cc kSpriteData) |
| `$E000-$E7FF` | 2,048 | used | character RAM, 256 chars (mem.h kCharRam) |
| `$E800-$EBE7` | 1,000 | used | main screen RAM (mem.h kScreenRamMain) |
| `$EBE8-$EBF7` | 16 | free (orphan) | free, but outside every linker region |
| `$EBF8-$EBFF` | 8 | used | main screen sprite pointers |
| `$EC00-$EFE7` | 1,000 | used | alt screen RAM (mem.h kScreenRamAlt) |
| `$EFE8-$EFF7` | 16 | free (orphan) | free, but outside every linker region |
| `$EFF8-$EFFF` | 8 | used | alt screen sprite pointers |
| `$F000-$FF3F` | 3,904 | used | panel bitmap, incl. the four heading strips at $F000 (view.cc, gfx.cc) |
| `$FF40-$FFF9` | 186 | free (orphan) | free, but outside every linker region |
| `$FFFA-$FFFF` | 6 | used | NMI / RESET / IRQ vectors |

```
Used                       58,995 B   90.0%
Free, allocatable           6,242 B   largest run 6,163 B at $B7ED
Free, stack headroom           81 B   reachable by lowering #pragma stacksize
Free, orphan fragments        218 B   only reachable by hand-placing
Free, total                 6,541 B   10.0%
```

```
Feature table total        58,708 B
+ machine-owned ranges        359 B   processor port, runtime ZP, hardware stack, BASIC link
- addresses counted twice      72 B   oscar64 overlays call frames that cannot be live together
= address space, used      58,995 B
```

---

## Detail by feature area

### 1. Horizon Graphics (17,365 B)

* **Code (3,544 B)**: viewport horizon rendering, cell filling, box generation, slot drawing (`render.cc`, `box.cc`, `roll.cc`, `roll_asm.cc`).
* **Data (9,338 B)**: box definitions (`boxdefs`), character definitions (`chardefs`), roll multiply and slope tables, compressed charset (`kGfxCharsCompressed`).
* **BSS (398 B)**: per-slot frame arrays (`_box_chars`, `_box_colors`).
* **ZP (37 B)**: roll and render registers (`roll_dx`, `roll_dy`, `roll_period`, `render_cx_pixels`, ...).
* **VRAM (4,048 B)**: character RAM `$E000-$E7FF`, main screen `$E800`, alt screen `$EC00` - the two double-buffered VIC screens.

### 2. Polygon Graphics (6,963 B)

* **Code (5,468 B)**: polygon pipeline, edge scan conversion, near and screen clipping, fixed-point vector math (`poly.cc`, `vec.cc`, `vec_asm.cc`, `fmath.cc`).
* **Data (1,260 B)**: sine and cosine tables, inverse-Z LUT, the quarter-square multiply tables (`vec_sqr_lo`, `vec_sqr_hi`).
* **BSS (215 B)**: scratch vertex buffers (`poly_verts`, `clip3_buf`, `proj_buf`, `clip2_buf1/2`, `final_verts`).
* **ZP (20 B)**: vector registers (`vec_v`, `vec_sx`, `vec_sy`).

### 3. World Model (7,470 B)

* **Code (6,458 B)**: flight dynamics, physics integration, waypoint checking, terrain grid rendering (`flight.cc`, `world.cc`, `sim.cc`, `world_map.cc`, `clouds.cc`).
* **Data (364 B)**: orientation matrices (`mat3_rot`, `kHeadingLut`), the world map, cloud hash and ladder tables.
* **BSS (589 B)**: flight path history (`flight_path_px/py`), delta transform vectors (`_world_dx4`, `_world_dy4`), camera state.
* **ZP (59 B)**: flight state (`flight_eye_x/y/z`, `flight_speed`, `flight_throttle`, `flight_fuel`, `flight_vspeed`).

### 4. Instrument Panel (11,521 B)

* **Code (2,865 B)**: viewport split raster handlers, gauge updates, the sprite stack and hardware controller (`view.cc`, `panel.cc`, `sprites.cc`, `spritedef.cc`).
* **Data (1,370 B)**: compressed panel image and sprite bitmaps, character and color LUTs.
* **BSS (284 B)**: raster IRQ split structures, the sprite candidate stack and the two committed sprite frames.
* **ZP (10 B)**: sprite index and pointers.
* **VRAM (6,992 B)**: sprite bitmaps `$D400-$DFFF`, panel bitmap `$F000-$FF3F` (the four heading strips live in its off-screen head at `$F000-$F17F`), and both screens' sprite pointers.

### 5. Menu & Missions (6,726 B)

* **Code (2,598 B)**: menu loop, mission cursor, help screen, map view (`menu.cc`, `mission.cc`, `help.cc`, `map.cc`).
* **Data (3,104 B)**: menu and mission text, mission definitions, help text, map tiles.
* **VRAM (1,024 B)**: map view screen RAM at `$D000-$D3FF`, RAM under I/O, live only while the map is open.

### 6. Message System (484 B)

* **Code (469 B)**: status message timer, line formatter, clear and restore (`msg.cc`, `screen.cc` notices).
* **Data (13 B)**: format strings and delays.
* **BSS (0 B)**: the active message buffer.
* **ZP (2 B)**: notice countdown and length.

### 7. Sound Effects (1,076 B)

* **Code (948 B)**: SID driver, engine generator, stall alarm, wind noise, volume control (`sound.cc`).
* **Data (92 B)**: engine pitch table, wind frequency table, volume names.
* **BSS (28 B)**: the SID register shadow.
* **ZP (8 B)**: PWM phase, generation counters, RNG, voice-3 arbitration, stall phase.

### 8. Music (1,688 B)

* **Code (884 B)**: playback driver, tick handler, note-to-frequency conversion (`music.cc`).
* **Data (791 B)**: note table, per-row lead and bass streams, chord table, volume map and mix matrix, bit-packed gate and drum masks.
* **BSS (2 B)**: voice-3 sweep step.
* **ZP (11 B)**: row, bar and frame counters, arpeggio index, voice-3 ownership.

### 9. Debug Messages & Overlay (0 B)

* **Code (0 B)**: compiled out of `ppilot.prg`; present only in `ppilotd.prg`.

### 10. Benchmarks & Timing (0 B)

* **Code (0 B)**: compiled out of `ppilot.prg`; present only in `ppilotd.prg`.

### 11. Core System & Drivers (5,415 B)

* **Code (4,839 B)**: entry point, VIC setup, raster IRQ core, LZO decompressor, keyboard, CPU speed probe, oscar64 runtime (`ppilot.cc`, `mem.cc`, `gfx.cc`, `screen.cc`, `keys.cc`, `cpu.cc`, `bcd.cc`, `print.cc`).
* **Data (350 B)**: startup header, screen row pointer tables, fill patterns.
* **BSS (213 B)**: raster IRQ lists, keyboard matrix, CPU probe results.
* **ZP (13 B)**: compiler temporaries and kernel flags.
* **Off budget (1,000 B)**: color RAM $D800-$DBE7: 1000 x 4 bits inside the I/O block, not DRAM.


- **Off budget, 1,000 B** — color RAM $D800-$DBE7: 1000 x 4 bits inside the I/O block, not DRAM
- **Note** — The map view also borrows $E000-$FF3F for its bitmap, on top of char RAM, both screens and the panel. That is reuse, not extra memory, and only $D000-$D3FF is charged to it here.

---

## Tool usage

Both binaries, which is what the build runs:

```bash
make ram
```

One map:

```bash
python3 tools/analyze_ram.py [c64o/ppilot.map]
```

Every individual symbol with its size, by category and segment:

```bash
python3 tools/analyze_ram.py --verbose
```

The tool asserts that the walk reconciles to 65,536 and that the feature table
bridges to it, so a layout change the `FIXED` table has not been told about
fails loudly instead of quietly inflating the free figure. It warns when
pointed at a map that is not a `-D__MAX_RAM__` build (`vecdemo`, `vectest`),
where those fixed addresses do not apply.
