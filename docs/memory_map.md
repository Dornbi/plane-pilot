# RAM Footprint & Memory Map

A breakdown of RAM usage by feature area and segment for **Plane Pilot (`ppilot.prg`)** and **Fighter Pilot (`fpilot.prg`)**, plus the 64 KB address-space walk.

Every number below is generated from the compiled link maps (`c64o/ppilot.map` and `c64o/fpilot.map`) by `tools/analyze_ram.py` (refreshed **20 November 2026**). Regenerate with:

```bash
make pram                                           # ppilot.map (text)
make fram                                           # fpilot.map (text)
python3 tools/analyze_ram.py c64o/ppilot.map --markdown
python3 tools/analyze_ram.py c64o/fpilot.map --markdown
python3 tools/analyze_ram.py c64o/fpilot.map --verbose
```

## Reading the tables

- **Feature table** sums linker symbols plus hand-placed `FIXED` VRAM ranges by feature area (`Code`, `Data`, `BSS`, `ZP`, `VRAM`).
- **Address space walk** covers all **65,536 bytes** and reconciles to exactly that. Free RAM is read from the walk, never from `65536 - feature_total`.
  - **free** — inside a linker region; usable by the compiler. Since `bss2` runs through `$085F` (`mem.h` `MEM_BSS2_END`), it reuses the BASIC link byte (`$0800`) and oscar64 startup stub (`$0801–$0852`), both dead once `main()` starts.
  - **stack headroom** — unused portion of `#pragma stacksize` (`$0200–$0240` in `ppilot`, `$0200–$0220` in `fpilot`). Static call frames grow down from the top; exceeding the region fails at link time (oscar64 error 3034).
  - **boot headroom** — spare bytes inside the `$0860–$0A5F` boot region (reused as `box.cc` `kBootScratch` after `_boot()` returns).
  - **free (orphan)** — outside every linker region (`$EBE8`, `$EFE8`, `$FFF8`); reachable only by hand-placing.
- **Color RAM (`$D800–$DBE7`, 1,000 × 4 bits)** is separate SRAM inside the I/O block, not part of the 64 KB DRAM (the DRAM behind `$D800` is the sprite bitmap block), so it is reported off budget.
- **Map view bitmap (`$E000–$FF3F`)** reuses character RAM, both screens, and the panel bitmap; only `$D000–$D3FF` is charged to it.

---

## Plane Pilot (`ppilot.prg`)

| Feature Area | Code | Data | BSS | ZP | VRAM | **Total Footprint** |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| **1. Horizon Graphics** | 2,605 B | 6,837 B | 25 B | 29 B | 4,176 B | **13,672 B (13.4 KB)** |
| **2. Polygon Graphics** | 4,229 B | 1,158 B | 312 B | 20 B | 0 B | **5,719 B (5.6 KB)** |
| **3. World Model** | 6,248 B | 375 B | 350 B | 67 B | 0 B | **7,040 B (6.9 KB)** |
| **4. Instrument Panel** | 2,461 B | 1,224 B | 170 B | 1 B | 6,992 B | **10,848 B (10.6 KB)** |
| **5. Menu & Missions** | 2,568 B | 2,828 B | 0 B | 7 B | 1,024 B | **6,427 B (6.3 KB)** |
| **6. Message System** | 370 B | 11 B | 0 B | 2 B | 0 B | **383 B (0.4 KB)** |
| **7. Sound Effects** | 685 B | 139 B | 28 B | 9 B | 0 B | **861 B (0.8 KB)** |
| **8. Music** | 749 B | 380 B | 1 B | 10 B | 0 B | **1,140 B (1.1 KB)** |
| **9. Debug Messages & Overlay** | 988 B | 0 B | 0 B | 5 B | 0 B | **993 B (1.0 KB)** |
| **10. Benchmarks & Timing** | 349 B | 0 B | 8 B | 0 B | 0 B | **357 B (0.3 KB)** |
| **11. Core System & Drivers** | 3,670 B | 427 B | 85 B | 10 B | 0 B | **4,192 B (4.1 KB)** |
| **TOTAL** | **24,922 B** | **13,379 B** | **979 B** | **160 B** | **12,192 B** | **51,632 B (50.4 KB)** |

### Address space walk (`ppilot.map`)

| Range | Bytes | State | Contents |
| :--- | ---: | :--- | :--- |
| `$0000-$0001` | 2 | used | 6510 processor port |
| `$0002-$005F` | 94 | used | oscar64 runtime zero page (checked by `check_zeropage.py`) |
| `$0060-$00FF` | 160 | used | linker-allocated zero page |
| `$0100-$01FF` | 256 | used | 6502 hardware stack |
| `$0200-$0221` | 34 | stack headroom | software stack headroom (`#pragma stacksize(0x40)`) |
| `$0222-$057B` | 833 used / 25 free | mixed | static stack frames + `bss2` (page-aligned holes: 1 B, 20 B, 4 B) |
| `$057C-$085F` | 740 | free | `bss2` free tail (reuses `$0800-$0852` startup stub after boot) |
| `$0860-$0A31` | 466 | used | boot-only code (`boot` region), reused as `kBootScratch` |
| `$0A32-$0A5F` | 46 | boot headroom | boot region headroom (`MEM_BOOT_END`) |
| `$0A60-$9E77` | 37,885 used / 27 free | mixed | main region code, data, BSS (27 B alignment hole at `$3A49`) |
| `$9E78-$CFFF` | 12,680 | free | main region free tail |
| `$D000-$D3FF` | 1,024 | used | RAM under I/O: map screen RAM / title aircraft 4 sprite blocks |
| `$D400-$DFFF` | 3,072 | used | sprite bitmaps, 48 blocks (`mem.cc kSpriteData`) |
| `$E000-$E7FF` | 2,048 | used | character RAM, 256 chars (`kCharRam`) |
| `$E800-$EBE7` | 1,000 | used | main screen RAM (`kScreenRamMain`) |
| `$EBE8-$EBF7` | 16 | free (orphan) | gap before sprite pointers |
| `$EBF8-$EBFF` | 8 | used | main screen sprite pointers |
| `$EC00-$EFE7` | 1,000 | used | alt screen RAM (`kScreenRamAlt`) |
| `$EFE8-$EFF7` | 16 | free (orphan) | gap before sprite pointers |
| `$EFF8-$EFFF` | 8 | used | alt screen sprite pointers |
| `$F000-$FF3F` | 3,904 | used | panel bitmap, incl. heading strips at `$F000` |
| `$FF40-$FFBF` | 128 | used | back view's tail fin, 2 sprite blocks (`kFinSpriteData`) |
| `$FFC0-$FFC3` | 4 | used | direct raster IRQ trampoline & SuperCPU flag (`irqcode`, `gfx.cc _gfx_irq_vec`, `_gfx_panel_fast`) |
| `$FFC4-$FFF7` | 52 | free | free inside `irqcode` region |
| `$FFF8-$FFF9` | 2 | free (orphan) | gap before hardware vectors |
| `$FFFA-$FFFF` | 6 | used | NMI / RESET / IRQ vectors |

```
Used                       51,908 B   79.2%
Free, allocatable          13,514 B   largest run 12,680 B at $9E78
Free, stack headroom           34 B
Free, orphan fragments         34 B
Free, boot headroom            46 B
Free, total                13,628 B   20.8%

Feature table total        51,632 B
+ machine-owned ranges        358 B   processor port, runtime ZP, hardware stack
- startup stub in bss2         82 B   mem.h bss2 over $0801: dead once main() runs
= address space, used      51,908 B
```

---

## Fighter Pilot (`fpilot.prg`)

`fpilot.prg` drops the `D` debug view and colour schemes and adds opponents, 3D plane rasteriser, guns, damage, and debrief. Key layout differences from `ppilot.prg`:

- `#pragma stacksize(0x20)` (`$0200–$021F`, 2 B headroom); `bss2` starts at `$0220`.
- Main region ends at `$CFC0` so `$CFC0–$CFFF` (64 B) holds `kOpponentDotBlock` outside I/O.
- `$D000–$D3FF` (1,024 B under I/O) is also time-shared with fpilot's 16 dynamic opponent sprite blocks during flight.
- `scratch` / `tscratch` overlay **420 B** of main screen RAM rows 14–24 (`$EA30–$EBE7`), which are covered by the MCBM instrument panel during flight.

| Feature Area | Code | Data | BSS | ZP | VRAM | **Total Footprint** |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| **1. Horizon Graphics** | 2,594 B | 6,837 B | 39 B | 15 B | 4,176 B | **13,661 B (13.3 KB)** |
| **2. Polygon Graphics** | 4,288 B | 1,470 B | 0 B | 20 B | 0 B | **5,778 B (5.6 KB)** |
| **3. World Model** | 16,644 B | 1,367 B | 549 B | 80 B | 64 B | **18,704 B (18.3 KB)** |
| **4. Instrument Panel** | 2,790 B | 1,218 B | 171 B | 0 B | 6,992 B | **11,171 B (10.9 KB)** |
| **5. Menu & Missions** | 2,495 B | 3,088 B | 7 B | 1 B | 1,024 B | **6,615 B (6.5 KB)** |
| **6. Message System** | 394 B | 11 B | 2 B | 0 B | 0 B | **407 B (0.4 KB)** |
| **7. Sound Effects** | 698 B | 139 B | 34 B | 3 B | 0 B | **874 B (0.9 KB)** |
| **8. Music** | 774 B | 380 B | 6 B | 5 B | 0 B | **1,165 B (1.1 KB)** |
| **11. Core System & Drivers** | 5,062 B | 614 B | 139 B | 36 B | 0 B | **5,851 B (5.7 KB)** |
| **TOTAL** | **35,739 B** | **15,124 B** | **947 B** | **160 B** | **12,256 B** | **64,226 B (62.7 KB)** |

```
Used                       63,682 B   97.2%
Free, allocatable           1,769 B   largest run 896 B at $CC40 (plus 800 B in bss2 at $0540)
Free, stack headroom            2 B   ($0200-$0201)
Free, orphan fragments         34 B
Free, boot headroom            49 B   ($0A2F-$0A5F)
Free, total                 1,854 B   2.8%

Feature table total        64,226 B
+ machine-owned ranges        358 B   processor port, runtime ZP, hardware stack
- addresses counted twice     400 B   oscar64 overlays non-overlapping call frames
- screen rows as scratch      420 B   mem.h scratch: main screen rows 14-24, dead in flight
- startup stub in bss2         82 B   mem.h bss2 over $0801: dead once main() runs
= address space, used      63,682 B
```

---

## Feature area contents

1. **Horizon Graphics:** `render.cc`, `render_asm.cc`, `box.cc`, `box_asm.cc`, `roll.cc`, `roll_asm.cc`; `boxdefs`, `chardefs`, roll tables, compressed charset; char RAM `$E000–$E7FF`, main/alt screen RAM `$E800`/`$EC00`, back-view fin `$FF40–$FFBF`. Per-slot caches live in `kBootScratch` (`$0860–$0A5F`).
2. **Polygon Graphics:** `poly.cc`, `poly_asm.h`, `poly_asm.cc`, `vec.cc`, `vec_asm.cc`, `fmath.cc`; trig, inverse-Z, quarter-square LUTs; vertex scratch buffers.
3. **World Model:** `flight.cc`, `flight_asm.cc`, `world.cc`, `world_asm.cc`, `sim.cc`, `world_map.cc`, `clouds.cc`, `clouds_asm.cc`, and in `fpilot`: `opponent.cc`, `opponent_asm.cc`, `planes.cc`, `planes_asm.cc`, `gun.cc`, `gun_asm.cc`, `damage.cc`, plus `kOpponentDotBlock` at `$CFC0`.
4. **Instrument Panel:** `view.cc`, `panel.cc`, `sprites.cc`, `sprites_asm.cc`, `spritedef.cc`; compressed panel/sprite art; sprite bitmaps `$D400–$DFFF`, panel bitmap `$F000–$FF3F`, sprite pointers.
5. **Menu & Missions:** `menu.cc`, `pmission.cc`/`fmission.cc`, `debrief.cc`, `help.cc`, `map.cc`, `title.cc`; text, mission tables, map tiles, title sprites; `$D000–$D3FF` time-shared under I/O.
6. **Message System:** `msg.cc` and `screen.cc` notices.
7. **Sound Effects:** `sound.cc`, `sound_asm.cc`, pitch/wind/V3 tables, 25 B SID shadow.
8. **Music:** `music.cc`, packed `musicdef.cc` pools, note/drum tables.
9. **Debug / 10. Benchmarks (`ppilot` only):** `panel.cc` `D` overlay, `benchmark.cc` CIA2 counters.
11. **Core System & Drivers:** `ppilot.cc`, `mem.cc`, `gfx.cc`, `screen.cc`, `keys.cc`, `cpu.cc`, `bcd.cc`, `print.cc`, oscar64 runtime; boot region at `$0860`; off-budget color RAM `$D800–$DBE7`.

---

## The ROM window before `mem_init()`

At power-on, BASIC ROM is banked in at `$A000–$BFFF` and KERNAL at `$E000–$FFFF`. Until `mem_init()` calls `mmap_set(MMAP_NO_ROM)`, stores to those ranges write RAM underneath while loads read ROM bytes. A global placed in `$A000–$BFFF` by the linker therefore writes cleanly and reads back garbage ROM bytes — a layout-dependent trap that once froze `ppilot` when `cpu_step_shift` landed at `$AC9D`.

**Rule:** nothing that runs before `mem_init()` may read a global in `$A000–$BFFF` or `$E000–$FFFF`. Pass values in registers or move the work after `mem_init()`.

`tools/check_rom_window.py` enforces this automatically on every link of `ppilot`, `fpilot`, and `polydemo`, walking `main()` and its callees up to `mem_init()` and failing the build on any absolute load from `$A000–$BFFF` or `$E000–$FFFF`.
