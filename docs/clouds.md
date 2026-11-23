# Clouds & Viewport Sprite Stack (`clouds.md`)

Shipped in both `ppilot.prg` and `fpilot.prg` behind `-D__ENABLE_CLOUDS__` (`make -C c64o CLOUDS=` omits clouds). Everything below describes the current implementation (`clouds.*`, `clouddef.*`, `sprites.*`, `sprites_asm.*`, `spritedef.*`, `gfx.cc`); only left-edge X wrap (§1.6) remains unbuilt.

See [sprite_objects.md](sprite_objects.md) for the viewport sprite rules and client overview.

---

## 1. The sprite stack

### 1.1 Priority & overflow

Hardware sprite 0 draws in front of 1, ..., in front of 7. Candidates are inserted sorted by increasing depth so nearer objects take lower indices; when more than `kSpriteTerrainSlots` candidates arrive, the farthest drops out cleanly.

### 1.2 API & clients (`c64o/sprites.h`)

- `sprites_stack_reset()` clears the candidate list each frame (`world.cc:world_update_objects()`).
- `sprites_stack_add(depth, x, y, bitmap, bitmap2, color, flags)` clips, snaps, and inserts a candidate (1 or 2 hardware sprites).
  - Flags:
    - `kSpriteFlagExpandX = 0x01` — 2× horizontal expansion (`$D01D`).
    - `kSpriteFlagAlignDither = 0x02` — snap onto the global checkerboard dither lattice (§4).
    - `kSpriteFlagWide = 0x04` — 2-column horizontal pair (`bitmap2` placed to the right, used by near opponents).
    - `kSpriteFlagExpandY = 0x08` — 2× vertical expansion (`$D017`, 42 lines tall, used by near banked opponents).
  - **Culls inside `sprites_stack_add()`:** horizontal `(uint16_t)(x + 24) >= 344` (`vx < 0` is currently rejected — §1.6), vertical DMA clearance (§1.8: `(uint16_t)(y + h_m1) >= 110`, unifying unexpanded `h=21` and Y-expanded `h=42`), and row-13 message strip (`_sprites_hidden_by_msg()` when `msg_active()`).
- `sprites_stack_commit()` builds the inactive `sprite_frame_t` from the sorted list.
- **Stack clients (`world_update_objects()`):** clouds (`clouds_add_candidates()`), opponents (`opponent_add_sprites()`, `fpilot`), tracers and smoke (`gun_add_sprites()`, `fpilot`), and the sun (`depth = 0x7FFF`, `COLOR_SUN`).
- **Fixed non-stack sprites:** orientation mark (`sprites_set_orientation()`, slot 0 in front view) and tail fin (`sprites_set_fin()`, slots 0–1 in back view).

### 1.3 Storage & 6502 assembly (`sprites_asm.h`, `sprites_asm.cc`, `sprites.cc`)

- `sprite_cand_t` is **9 B** (`int16_t depth, x; uint8_t y, bitmap, bitmap2, color, flags;`); `_sprites_cand[8]` = 72 B, indexed by byte offset `_sprites_cand_end` (`0..72`, step 9).
- `sprite_frame_t` is **37 B** (`pos[16]`, `ptr[8]`, `color[8]`, `msbx`, `expand`, `expand_y`, `enable`, `last`).
- `volatile sprite_frame_t _sprites_frames[2]` (74 B in `bss2`) holds `_sprites_frame_a` and `_sprites_frame_b`, **synchronised directly with `mem_using_alt_buffer`** (no separate flip flag): `sprites_stack_commit()` writes frame B when `mem_using_alt_buffer` is true (else frame A), and the terrain raster handler reads the opposite committed frame. Declaring `_sprites_frames` `volatile` prevents oscar64 from hoisting or reordering writes across the IRQ boundary.
- `sprites_stack_add()`, `sprites_stack_commit()`, `_sprites_set_instrument_sprite()`, `sprites_set_alt()`, and `sprites_set_vspeed()` are hand-assembled 6502 `__asm` in `c64o/sprites_asm.cc` (with `#ifndef __OSCAR64__` C mirrors in `c64o/sprites.cc` for host `sprites_test.cc`, verified bit-for-bit by `target_test.cc` test 20).

### 1.4 Raster handlers & IRQ zero-page rule (`gfx.cc`, `sprites.cc`)

There are **three raster IRQs** (`-DNUM_IRQS=3`):

| Slot | Raster | Mechanism | Handler work |
| ---: | ---: | --- | --- |
| 0 | **161** | 14 B `RIRQCode` stub (`_rirq_buf` in `$FFC0`) | 12 NOPs (C64) / 19 NOPs + 1 MHz (SuperCPU) → MCBM switch + `sprites_show_panel_top_sprites()` (`$D015=$FF`, clears `$D01D`/`$D017`, restores sprite 0 colour, parks 1–7, places sprite 0 on `kScreenRamAlt`) |
| 1 | **187** | Direct ISR call | `sprites_show_panel_bottom_sprites()` (panel-bottom gauge sprites on `kScreenRamAlt`) |
| 2 | **252** | Direct ISR call | `sprites_show_terrain_sprites()` → `$D021 = COLOR_GRAD2`, `$D011 = $1B`, `$D018`, `sound_blit()` |

- **Enable-first + `last` trim:** `_sprites_program_frame()` writes `vic.spr_enable = frame.enable` **first** so unused sprites parked at `y = 0` are disabled before raster 256 (preventing ~100 cy/slot of lower-border DMA fetches), copies all 16 `pos[]` bytes downward, copies `ptr[]`/`color[]` only for `0..last`, then writes `msbx`, `expand_x`, `expand_y`. Each band writes pointers only to its own screen RAM (`kScreenRamMain` or `kScreenRamAlt`).
- **IRQ zero-page rule:** Raster handlers run over main-line code and must never touch oscar64 runtime ZP `< $60` (enforced on every link by `tools/check_irq_zp.py`). Keeping `_sprites_frame_a`/`_sprites_frame_b` at constant link-time addresses and copying `pos[16]` downward in a single indexed loop keeps the ISR completely ZP-free.

### 1.5 Register initialisation

`sprites_init()` zeroes `$D01D` (expand X), `$D017` (expand Y), `$D01C` (multicolour), and `$D01B` (priority), sets `$D015 = $FF`, and builds the 2-sprite back-view tail fin at `$FF40`.

### 1.6 Left-edge X wrap (open, not yet built)

Currently `sprites_stack_add()` rejects any sprite with `vx < 0` (`x < -24` in screen coordinates), so clouds/objects pop off the left viewport edge instead of sliding smoothly off-screen. On PAL (504 X ticks per line), wrapping negative `vx` via `vx + 504` (`kSpriteXWrap = 504`, MSB set) slides sprites cleanly off the left edge; `504 % 4 == 0` commutes with §4's dither snap. Requires an ink-box visibility check and is PAL-only (NTSC is 512 ticks).

### 1.7 Sun

`world_update_objects()` submits the sun as an ordinary stack entry with `depth = 0x7FFF` and `COLOR_SUN`, so clouds and opponents pass cleanly in front of it.

### 1.8 DMA clearance before the panel split

A sprite that is still being fetched by VIC DMA on raster 161 delays the panel-top IRQ and shifts `sta $d018` past horizontal blank, tearing the top row of the instrument panel.

- **Unexpanded sprites (21 lines):** `kSpritesOffLead = 22` → `kSpriteVisibleEndYPixels = 90` → maximum start raster `50 + 89 = 139`, finishing on raster 159 (2 lines clear of 161).
- **Y-expanded sprites (42 lines):** `kSpritesOffLeadExpandY = 43` → maximum start raster `50 + 68 = 118`, finishing on raster 159.
- Both bounds are enforced solely by `sprites_stack_add()`'s vertical cull (and fixed Y positions for the orientation mark and fin). A former 4th raster IRQ clearing `$D015 = 0` at raster 140 was dropped in commit `e45c32d` once the cull alone guaranteed zero sprites start in rasters 140–161 (saving 53 B and ~170 cy/frame).

Measured `sta $d018` landing window in `x64sc` (target: raster 162, cycles 53–59):

| Sprite start raster (unexpanded) | DMA ends | `sta $d018` lands | Result |
| ---: | ---: | --- | --- |
| `<= 139` (enforced by cull) | `<= 159` | line 162, cy 53–59 | Clean (0 torn px) |
| `140` | `160` | line 162, cy 56–62 | Borderline |
| `141–144` | `161–164` | shifts right | Torn panel top |

### 1.9 Reserved slot 0 & back-view tail fin

- **Slot 0 (`kSpriteIdxOrient = kSpriteIdxVSpeed = 0`):** On the panel band, sprite 0 is the vertical-speed needle; in the viewport band it is reserved outside the stack (`kSpriteStackFirst = 1`, `kSpriteTerrainSlots = 7`, stack uses slots 1–7).
  - In `VIEW_CENTER` (front view), slot 0 draws the white orientation mark (bitmap pointer 80, always in front of clouds/opponents).
  - In `VIEW_BACK` (back view), slots **0 and 1** draw the Y-expanded vertical tail fin (pointers 253–254 at `$FF40`, Y = 78 and Y = 120 — `kSpriteFinDropLines = 2` past 118, measured clean at 162), leaving **6 stack slots (2–7)**.

---

## 2. Where clouds are

### 2.1 Units

Clouds work in **1 m integer units** (`flight_eye_* >> 9`, 128 units per world unit, 1,024 per map cell).

### 2.2 Cell grid & scan/cull coupling

The 32 × 16-cell world is partitioned into **8 × 16 = 128 cloud cells** of 2 × 2 map cells (2,048 m × 2,048 m). `clouds_add_candidates()` scans a **5 × 5 cell neighbourhood** around the aircraft and culls clouds past **`kCloudCullU = 4096` m** (4 map cells). The scan radius and cull distance are strictly coupled and asserted by `tools/generate_clouds.py` and `tests/test_clouddef.py`.

### 2.3 Hash tables & gate

Each cell `(cx, cy)` hashes via `kCloudHashX`/`kCloudHashY` into `kCloudHashA` (`ha`) and `kCloudHashB` (`hb`):
- **Occupancy gate:** `(ha & kCloudGateBits) < kCloudGateLimit` with `kCloudGateBits = 0x1F`, `kCloudGateLimit = 10`.
- **Group pattern:** `ha >> 6` (4 patterns).
- **Intra-cell jitter:** low/high nibbles of `hb` × 128 m.

### 2.4 Density & evenness

`tools/generate_clouds.py` searches 40,000 seeds for blue-noise evenness (`TARGET_CELLS = 38`). The current checked-in seed (`34977`) populates **37 of 128 cells (29%)** with evenness **0.458** (`min_separation = 1,741 m`, `covering_radius = 3,799 m`), yielding ~16% two-group frames and 3.6% worst-case slot overrun.

### 2.5 Three-blob groups & runtime culling

Each active cell holds a 3-blob cloud group (4 world-axis offset patterns, `kCloudOffsetU = 32` m horizontal, 16 m lift on blob C, max blob separation 64 m = 0.67 diameter). `clouds_asm.cc` (with host C counterparts in `clouds.cc`) runs the 5×5 scan, hash gate, world-box cull, staged camera-row dot products via `vec_mul8` with lazy `_clouds_z[3]` / `_clouds_z_valid[3]` caching, wedge rejects (`vert = (x >> 2) + 128`, `lat = (x >> 1) + (x >> 3) + 128`), and rung walk in 6502 assembly, skipping `_clouds_build_basis()` (8-bit `trunc(world_cam / 16)` in camera-row order) and `_clouds_apply_offset()` entirely for collapsed far groups (§3.5). Per-call temporaries live in `tscratch` (`$EA30`) on `fpilot` and `bss2` on `ppilot`. Colour is `COLOR_CLOUD` (white, or med grey at Night in `ppilot`).

### 2.6 Deck altitude

Clouds sit at a single flat deck `kCloudDeckU = 700` m (~2,700 ft):
- **On runway (`z = 16 m`):** clouds sit ~10°–20° above the horizon across 2–4 km.
- **At deck (`z ≈ 700 m`):** clouds lie on the horizon; you fly through them.
- **High altitude (`z > 1,000 m`):** clouds drop below the horizon against the ground.

---

## 3. How big a cloud is

### 3.1 Physical scale

`kCloudBlobU = 96` m diameter, fixed so rung 0 (smallest bitmap) projects to 3 screen pixels at `kCloudCullU = 4096` m.

### 3.2 Ten-rung size ladder (`kCloudRungDepth`)

Depth (in 1 m units) selects rung `0..9` via a linear compare chain against `kCloudRungDepth`:

| Rung | Max depth (m) | Sprites | Flags | Pointer(s) |
| ---: | ---: | ---: | --- | --- |
| 0 (collapsed) | 4,096 | 1 (uses rung 1) | `AlignDither` | 81 |
| 1 | 3,151 | 1 | `AlignDither` | 81 |
| 2 | 2,424 | 1 | `AlignDither` | 82 |
| 3 | 1,864 | 1 | `AlignDither` | 83 |
| 4 | 1,434 | 1 | `ExpandX \| AlignDither` | 84 |
| 5 | 1,103 | 2 stacked | `ExpandX \| AlignDither` | 85, 86 |
| 6 | 936 | 2 stacked | `ExpandX \| AlignDither` | 87, 88 |
| 7 | 795 | 2 stacked | `ExpandX \| AlignDither` | 89, 90 |
| 8 | 675 | 2 stacked | `ExpandX \| AlignDither` | 91, 92 |
| 9 | 585 | 2 stacked | `ExpandX \| AlignDither` | 93, 94 |

### 3.3 Pivots

Single-sprite blobs pivot at `(12, 10)`; vertically stacked 2-sprite blobs pivot at `(12, 20)` with the lower sprite at `y + 21`. `ExpandX` doubles the horizontal pivot (`24`).

### 3.4 Flat `kSpriteDefCloudRung[10]` table

`clouds.cc` indexes a flat 10-row `kSpriteDefCloudRung[]` table directly by rung rather than branching between single and stacked rungs. Originally introduced to avoid an oscar64 branch miscompile, it is kept because it is branchless, faster, and smaller (verified by `tests/test_spritedef.py`).

### 3.5 Far-group collapse & block 80 reallocation

For rungs `< kCloudRungCollapsed` (`3`, i.e. rungs 0–2, >1,864 m), a 3-blob group collapses into **one** blob rendered with `kSpriteDefCloudRung[rung + 1]` (`if (!collapsed)` skips `_clouds_build_basis()` and `_clouds_apply_offset()`):
- Cuts cloud scan time roughly in half (~1 PAL frame saved in cruise).
- Rung 0's own bitmap is never indexed (dead row 0 in `kSpriteDefCloudRung` points to 81), freeing sprite block **80** (`$D400`) for the front-view orientation mark.

---

## 4. The dither lattice

### 4.1 Phase interlock

Cloud bitmaps use a 50% checkerboard dither so the sky shows through. When two overlapping clouds land in **aligned** checkerboard phase, their union covers **48%** of pixels (translucent); when **misaligned**, they interlock to **82%** (opaque solid white blocks).

### 4.2 Two-part invariant

1. **In the bitmaps:** every lit pixel obeys `(c + r + 21 * is_lower) % 2 == 0` (the `+21` matches the odd 21-line height of the upper sprite in stacked pairs).
2. **At runtime:** every dithered sprite lands on even `X` and even `(X >> 1) + Y`.

### 4.3 Generator enforcement

`tools/generate_sprites.py` enforces the checkerboard lattice when generating cloud bitmaps from `gfx/ppilot_sprites_concept.png` and asserts compliance; `tests/test_spritedef.py` verifies every rung bitmap and both halves of stacked rungs.

### 4.4 Runtime snap

Inside `sprites_stack_add()`, `kSpriteFlagAlignDither` snaps screen `(x, y)` in four instructions (`x &= ~1; if (((x >> 1) ^ y) & 1) y ^= 1;`), guaranteeing identical lattice phase across all clouds and black smoke puffs.

### 4.5 Granularity & block budget

The snap quantises clouds to 2×2 screen pixels — the exact same 2×2 granularity as terrain dots ([sprite_objects.md](sprite_objects.md) §3). Storing two-phase bitmaps to avoid the snap would double cloud blocks (28 > 15 available in `$D400`).

---

## 5. Per-frame cost

Measured on `x64sc` (see [framerate.md](framerate.md) and the `CLD`/`SPR` debug counters in [development.md](development.md)):
- `clouds_add_candidates()` (`CLD`): **11,761–28,283 cycles** depending on visible cloud count and deck altitude (runway ~11.8k, approach ~13.4k, at-deck cruise up to ~28k).
- `sprites_stack_commit()` (`SPR`): **~250–990 cycles** (faster since pass 148 moved it into `sprites_asm.cc`).

---

## 6. Files

### 6.1 Sprite blocks (`$D400–$DFFF`)

The 48 static sprite blocks (`kSpriteData`, pointers 80–127) hold:
- Pointer **80** (`$D400`): front-view orientation mark.
- Pointers **81–94** (`$D440–$D7BF`): 14 cloud bitmaps (4 single-sprite rungs + 5 stacked 2-sprite rungs; rungs 1–4 are also reused in black by `gun.cc` for smoke).
- Pointer **95** (`$D7C0`): sun.
- Pointers **96–127** (`$D800–$DFFF`): 32 instrument gauge arms (also reused in yellow by `gun.cc` for tracers).

### 6.2 Source & tool files

- C64 runtime: `c64o/clouds.{h,cc}`, `c64o/clouddef.{h,cc}`, `c64o/sprites.{h,cc}`, `c64o/sprites_asm.{h,cc}`, `c64o/spritedef.{h,cc,bin}`, `c64o/gfx.cc`.
- Generators & verification: `tools/generate_clouds.py`, `tools/generate_sprites.py`, `tools/render_cloud_preview.py`, `tools/check_irq_zp.py`.
- Tests: `c64o/test/sprites_test.cc`, `c64o/test/target_test.cc`, `tests/test_clouddef.py`, `tests/test_spritedef.py`.

---

## 7. Generated constants & verification

`make clouds` runs `tools/generate_clouds.py`, which derives all tables from four parameters at the top of the script, searches 40,000 seeds under balance and coupling invariants, and writes `c64o/clouddef.{h,cc}` and `lib/clouddef.py` in lockstep.

Verify changes with:
- `make cloud-preview` (`tools/render_cloud_preview.py` → `out/cloud_preview.png`, density & slot report; supports `--compare-gates` and `--groups` dither checks).
- `make test` (`tests/test_clouddef.py` asserts lockstep between C and Python constants and scan/cull coupling).
