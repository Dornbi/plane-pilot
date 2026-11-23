# RAM and cycle optimisation loop

`tools/ram_loop.sh` runs an unattended loop that frees RAM in `fpilot` and cuts frame cycles. RAM and cycles matter equally, and `fpilot` is the target (`ppilot` only needs to keep building and passing tests). Each pass starts a fresh worker agent to implement **one** change, rebuilds, runs all tests, benchmarks ten `fpilot` scenes via `tools/bench.sh`, compares screenshots, and passes an independent reviewer before committing; rejected attempts are logged here as dead ends so later passes skip them.

This file is the loop's memory. Every pass reads it first.

## Goal and metrics

- **RAM:** `fpilot`'s "Free, allocatable" from `make fram` after `make -C c64o all` (pass 152: **1,769 B** free, largest run 896 B at `$CC40`, plus 800 B in `bss2` at `$0540`).
- **Cycles:** `tools/bench.sh` times `bm_model_start()` through `bm_total()` across ten deterministic `fpilot` scenes:
  - Five frozen (16 frames each): `start` (01 Recon, airborne), `approach`, `cruise`, `town`, `lake`.
  - Five live combat: `scramble` (04 pair pass + guns), `shootdown` (04 scripted 5 hits + fall), `headon` / `chase` (05 hunters ahead then behind in back view), `strafe` (04 pair strafing run).
- **Acceptance gates:** `fpilot` free RAM must not drop; every scene must stay within **+2%** of `cycles-baseline`; and the change must either free RAM or cut total cycles by at least **0.5%** vs the last kept state.

The script parses this line verbatim — keep its exact format:

cycles-baseline: start=119431 approach=144722 cruise=152338 town=144513 lake=134136 scramble=187667 shootdown=183141 headon=128118 chase=169815 strafe=187334

## Rules (binding for the worker and the reviewer)

1. **Rendering stays the same.** Minor dot-layout or edge-pixel polygon tweaks are allowed; nothing may disappear, flicker, tear, or change colour. Panel, map, menu, help, title, opponent silhouettes, tracers, and smoke stay visually intact.
2. **Flight and combat behave the same.** Loops, climbs, turns, takeoff, landing, AI hunting/strafing, player & enemy guns, hits/smoke/falling, damage, and mission completion stay intact. Tightening or deleting expectations in `flight_test`, `opponent_test`, `gun_test`, `mission_test`, or `fpilot_test` is a red flag (tolerance-sized shifts only, explained).
3. **Cycles count as much as RAM.** Prefer cycle-neutral or faster changes; never un-inline hot per-point/per-edge loops (`world_render_grid`, `vec_project`, `vec_add`, `poly_fill`). Do not slow unbenched paths (flight model, sound, opponents, guns).
4. **Keep every feature.** Nothing may be dropped (`sound`, `music`, `map`, `missions`, `clouds`, `opponents`, `guns`, `damage`, `debrief`, `menu`, `help`).
5. **All checks and tests must pass:** `zp`, `irq`, `math`, `rom`, `boot` link checks, all 16 host C++ suites, `narrowing`, `target_test`, and `make test`.
6. **One small, reviewable idea per pass.** Keep docs/comments accurate (`docs/memory_map.md` etc.).

## Known traps & oscar64 rules

- `__striped` on a table of `&object` pointers miscompiles (all high bytes become 0); striped integers and `array + offset` work.
- At `-O1`+, a `t = a; a = b; b = t;` swap inside a loop drops `b = t`; pick endpoints instead.
- `if ((uint8_t)(u + 16) >= 32)` drops the 8-bit cast and compiles to `u >= 16`; write range tests without relying on 8-bit wraparound.
- Passing a loop counter in `A` to a helper can leave the loop's byte offset in `A` from iteration 2 on (see `gun.cc` `_gun_track`); check `.asm`.
- oscar64 inline `__asm` takes only literal struct offsets — pin them with `static_assert`.
- With pointer-constant tables, signed `int8_t` indices generate 16-bit addressing; use `uint8_t` + base pointer.
- oscar64 `@proxy` trampolines are constant-propagation savings, not slack; replacing constant-arg calls with descriptor tables defeats constant propagation and costs RAM.
- `#pragma optimize(size)` often increases size or blows zero page; dropping `nooutline` on hot files trades cycles for bytes.
- Inlining `_project_vertices` or `box_prepare` risks ZP spill (`$5F`); inlining `_fill_line`, `vec_transform*`, `vec_div8p8`, or `_clip_2d` costs 0.8–1.3 KB each.
- `crt.c` zeroes only main BSS; `bss2` (`$0220–$085F` in `fpilot`) is zeroed by `mem_init()` — never place anything read before `mem_init()` there.
- `check_irq_zp.py` forbids runtime ZP `< $60` in raster ISRs; pad any ISR prologue change to a multiple of 7 cycles.
- oscar64 aborts on long paths; scratch builds and `make_shots.sh` use a short `/tmp` directory.
- **Current tight limits (`fpilot`, pass 152):**
  - `tools/bench.sh` links ~250 B of harness (stubbing `help.cc` ~550 B); growing main RAM can break `bench.sh` before `fpilot.prg` itself overflows.
  - Software stack (`$0200–$021F`, `#pragma stacksize(0x20)`) has **2 B** spare; runtime ZP has **4 B** spare (`$5B` vs `$60`). Exceeding stack fails with error 3034.
  - `tscratch` (`$EA30–$EBE7`, shared screen rows 14–24) has **~45 B** spare after passes 142/150/151. Moving main BSS to `bss2` shifts free RAM without creating new free bytes, but opens main-region headroom.

## Measured dead ends (do not retry)

- LZO-compressing menu/help/music/title overlay (4,074 → 3,568 B, not worth it).
- Mirror-deduping `boxdefs` (none are exact L/R or U/D mirrors), edge-RLE on box maps (364 B), H-mirror char dedupe (needs 256 B LUT), 7-bit relative char index (7 boxes span >128 slots).
- Un-inlining `vec_add` (+41 B, ~1k cycles/frame), `vec_fastsqr8p8` → `fastmul(a,a)` (2× slower), asm square (−40 B, mixed speed).
- `__striped` on `_sprites_cand` (−11 B), `kFlightRecipV`, or `kMissionWpConstraint` (0 B).
- Remaining orphan fragments are only `$EBE8` (16 B), `$EFE8` (16 B), and `$FFF8` (2 B) — `$0801` startup stub (`bss2` to `$085F`), `$FFC0–$FFF7` (`irqcode`), and the raster-140 IRQ removal (pass 129) are already landed.

## Log

### Baseline resets

| Date | Commit | Event | `fpilot` free | Bench total (scenes) |
| --- | --- | --- | ---: | ---: |
| 2026-10-16 | `63e2950` | Initial loop start (5 frozen scenes, render only) | 9,546 B | 695,076 |
| 2026-10-17 | `84c4ccb` | Post-pass 21 `argon-loop` merge | 14,197 B | 650,277 |
| 2026-10-24 | `1787679` | Opponents & guns landed; 7 scenes incl. model step | 1,437 B | 1,057,692 |
| 2026-11-01 | `740aa57` | Hunting AI, rear gunners, 10 scenes (`cycles-baseline` above) | 707 B | 1,548,482 |
| 2026-11-18 | `5cde8c6` | Pass 152 | 1,769 B | 1,172,360 |
| 2026-11-23 | `b73518a` | Post-158 + Port 2 joystick, fine control, bomber scaling, landing/debrief grades | 1,084 B | 1,033,435 |

*Note: Non-integer sub-rows from parallel branch merges (`21.a–21.j`, `44.a–44.q`) are summarised in passes 21 and 44; full per-pass diffs live in git history. Keep exactly one integer pass number per row so `tools/ram_loop.sh` continues numbering from 153.*

| Pass | Date | Branch | fpilot Δ | ppilot Δ | Cycles Δ | Result | Idea |
|-----:|------|--------|---------:|---------:|---------:|--------|------|
| 1 | 2026-10-17 | claude | 45 | 45 | +0.3% | kept | Replaced the 48 B kMusicVolumeMix[3][16] table in music.cc with its three closed forms (0, v>>1, v) in music_master_volume(). |
| 2 | 2026-10-17 | claude | 45 | 45 | -0.2% | kept | Dropped the file-wide nooutline pragma from fmath.cc. |
| 3 | 2026-10-17 | claude | 125 | 125 | +0.4% | kept | kSpriteDefLongArm/ShortArm now hold 16 pivots (2 B each) instead of 32 {pivot, bitmap} rows: direction d+16 reuses bitmap d with the pivot mirrored through (12,10), and the bitmap is base + (d & 15). |
| 4 | 2026-10-17 | claude | 38 | 41 | -0.1% | kept | Wrapped _limit_vec/vec_normalize/vec_cross/vec_orthonormalize in vec.cc in #pragma optimize(push, outline). |
| 5 | 2026-10-17 | claude | 64 | 64 | +0.0% | kept | SP is set at startup and read nowhere (no dynamic stack use), so mem.h's stack region shrinks $0200-$0280 -> $0200-$0240 (stacksize 0x80 -> 0x40) and bss2 starts at $0240. |
| 6 | 2026-10-17 | claude | 43 | 40 | -0.1% | kept | Dropped the unsigned twin of the flight step shift; the drag square, the only argument that can reach 0x8000 (0xE100 at kMaxSpeed), is halved before the call and its three shifts are one less, so drag stays bit-identical. |
| 7 | 2026-10-17 | claude | 96 | 96 | -0.0% | rejected by review: XOR flip assumes the table was set up first; polydemo never calls mem_use_main_buffer(), so its row pointers end up at $0000 and it draws into zero page | mem.cc drops kScreenRowPtrsMain/Alt (2 x 50 B): mem_use_main_buffer() fills mem_screen_row_ptrs with a +40 loop and mem_switch_buffer() XORs each high byte with $04 (the buffers are 1 KB apart, static_asserted). |
| 8 | 2026-10-17 | claude | 132 | 132 | +0.1% | kept | roll.cc keeps 30 of the 60 rows of kRollDx/kRollDy/kRollDxDivDy/kRollPeriod; angle a+30 is angle a with (dx, dy) negated (same slope and period, checked for all 30 pairs), so roll_update_state() folds it with one compare. |
| 9 | 2026-10-17 | claude | 55 | 58 | -0.1% | kept | fmath.cc drops kRollAngleLut[65]; it is exactly min(ratio >> 2, 15) for all 65 ratios (checked), so _get_roll_angle() shifts and clamps 16 to 15. Bit-identical. |
| 10 | 2026-10-17 | claude | 25 | 35 | -0.0% | kept | fmath.cc drops kHeadingLut[65]; it is exactly ratio / 5 for all 65 ratios, which _get_heading() now computes as (ratio * 51 + 12) >> 8 with 51 = 3 * 17 in shifts and adds. |
| 11 | 2026-10-17 | claude | 201 | 195 | -0.3% | kept | The flight model's six 18 B kVec* control matrices and vec_transform3() (27 multiplies plus an 18 B transposed copy per step) are replaced by vec_rotate_step(): each step only turns two rows of the camera matrix towar... |
| 12 | 2026-10-17 | claude | 49 | 49 | +0.1% | kept | Every shift by a variable amount (sim.cc's three key-table loops and keys_wait_release via key_pressed, the fin-row 0x80 >> n in sprites.cc, map_set_overlay_pixel's 1/3 << shift, and the boot-time 8 >> / 1 << / 32 >>... |
| 13 | 2026-10-17 | claude | 30 | 30 | -0.6% | kept | world_render_grid() looked up kWorldMap[cx & 15][cy & 31] for every cell, which rebuilt the row address through oscar64's 32 B __multab32L/H tables on each inner step. |
| 14 | 2026-10-17 | claude | 165 | 165 | -0.5% | kept | The lead starts notes only on even rows, so generate_music.py now emits kMusicLeadStartPat[20][8] (even rows only, the premise is asserted) and MUSIC_LEAD_START_IN reads odd rows as 0 via (r & 1) ? 0 : pat[r >> 1]. |
| 15 | 2026-10-17 | claude | 117 | 120 | +0.4% | kept | Every bass note is its bar's chord root + 0/7/10/12, so generate_music.py now emits kMusicBassStepPat[3][16] (interval + 1, 0 = no new note; the 11 note patterns collapse to 3 rhythm shapes) in place of kMusicBassStar... |
| 16 | 2026-10-17 | claude | 420 | 0 | -0.0% | kept | New fpilot-only linker region "scratch" at $EA30-$EBE7 (main screen rows 14-24, never fetched in flight: the panel split writes $d018 on line 162, before row 14's badline at 163, and the panel colors come from the alt... |
| 17 | 2026-10-17 | claude | 95 | 95 | +0.0% | kept | boxdefs.cc's alt_boxes[60] (120 B of pointers, 52 of them NULL) becomes alt_box_angles[8] plus alt_boxes[8], and boxdef_set_alt() searches the 8 angles, falling back to boxdef_set_main() (which keeps the kRollMax check). |
| 18 | 2026-10-17 | claude | 817 | 817 | +0.0% | kept | chardefs[224][8] (1,792 B) becomes chardef_bytes[728], a greedy shortest common superstring in which every stored char is 8 consecutive bytes (one char's bottom rows are often another's top rows). |
| 19 | 2026-10-17 | claude | 221 | 221 | +0.5% | kept | The 9 MAP_OBJ_* types move from 16..24 to 7..15, so every map type fits a nibble and kWorldMap[16][32] (512 B) becomes kWorldMap[16][16] (256 B, even column in the low nibble). |
| 20 | 2026-10-17 | claude | 181 | 201 | +0.0% | kept | boxdef_t loses total_size (= w * h) and the box_chars pointer: find_boxes.py now emits one box_*_map[] per box (char_count chardefs indices, then the w * h cells). |
| 21 | 2026-10-17 | argon | 1783 | 1742 | -6.6% | merged | argon-loop, a separate cycle-only loop forked from 63e2950, merged by hand: world_render_grid and gfx_project_and_draw, 8-bit loops in mem/box, vec_transform_inv, _poly_scan_lines2 and _clip_near, panel lamps. |
| 22 | 2026-10-17 | claude | 56 | 56 | +0.0% | kept | menu_run()'s key loop called oscar64's key_pressed() on a variable scan code (kMenuKeys[i]), which linked the 56 B bitshift table again after the argon-loop merge (pass 12 had removed every other use). |
| 23 | 2026-10-17 | claude | 16 | 16 | +0.1% | kept | mem.h MEM_BOOT_END $0A70 -> $0A60: box.cc's kBootScratch layout needs exactly 512 B (its static_assert floor, kMaxBoxTotalSize 96 is real) and the largest boot code is ppilota's at $0A3E. |
| 24 | 2026-10-17 | claude | 130 | 130 | +0.1% | kept | oscar64's keyb_poll() (174 B) also decodes the last new key into keyb_key, which nothing here reads. |
| 25 | 2026-10-17 | claude | 54 | 51 | -0.1% | kept | mem_init() no longer calls oscar64's mmap_trampoline(), which linked IRQTrampoline/NMITrampoline/DoneTrampoline (53 B in main); their KERNAL handlers jump via $0314/$0318, which bss2 overlays, so they could only crash. |
| 26 | 2026-10-17 | claude | 68 | 68 | +0.1% | kept | world_map.cc's kWorldObjX/Y[9][6] (108 B, 4 of the 9 types repeat another's outline) become flat kWorldObjX/Y[23] holding the 5 distinct outlines plus kWorldObjFirstVert[9]. |
| 27 | 2026-10-17 | claude | 7 | 7 | -0.3% | kept | poly.cc's edge tracer stepped x-major edges one pixel at a time (while err >= dy). |
| 28 | 2026-10-24 | claude | 55 | 0 | -0.0% | kept | opponent.cc's (uint8_t)(1 << i) slot masks (9 sites, incl. _opponent_live() inlined into gun.cc) and _opponent_cap()'s 2 >> _opponent_nearer had linked oscar64's 56 B bitshift table back into fpilot. |
| 29 | 2026-10-24 | claude | 68 | 0 | +0.2% | kept | opponent.cc's kOpponentPaths held x[4]/y[4] in every path (stride 32 B, 256 B in all, 14 of 32 waypoint slots unused, the parked and empty slots' all wasted). |
| 30 | 2026-10-24 | claude | 70 | 0 | +0.3% | kept | opponent_reset() searched the mission's waypoints for WP_TARGETS_DOWN to find the targets' navpoint. |
| 31 | 2026-10-24 | claude | 42 | 0 | -0.3% | kept | planes.cc's kCols/kRows << (cols/rows/xs/ys - 1) (planes_project's wpx/hpx, planes_fill's height, _slide) and planes_asm.cc's 24 << (cols - 1) linked oscar64's __shltab24L, __shltab21L and __shltab21H (48 B). |
| 32 | 2026-10-24 | claude | 108 | 0 | +0.1% | kept | planes.cc's _poly_start[] (and kMaxPolys) was written each projection but never read (planes_fill uses kPlanePolyStart and key_poly_count), so it and its copy loop go. |
| 33 | 2026-10-24 | claude | 63 | 0 | -0.2% | kept | opponent.cc's static __noinline _opponent_dot(a, b, c, d) (fastmul(a, b) + fastmul(c, d)) moves to vec.cc as vec_dot2. |
| 34 | 2026-10-24 | claude | 119 | 0 | +0.0% | kept | opponent.cc's kView, kViewTwo and kViewFar (3 x 11 B planes_view_t, differing only in kmax and kn) and the _opponent_view pointer chosen between them become one data-section _opponent_view whose kmax and kn new inline... |
| 35 | 2026-10-24 | claude | 160 | 0 | +0.1% | kept | planes.cc's _same_axes() (six unrolled int16 compares, 122 B) and planes_project()'s six unrolled stores of the axes' screen components into state->axes (~92 B) become one 55 B _new_axes() that walks the 12 bytes. |
| 36 | 2026-10-24 | claude | 114 | 0 | +0.2% | kept | planes.cc's _planes_lx/_planes_ly (64 B in main bss) kept the last projection's buffer-space vertices alive until the next frame's planes_fill(). |
| 37 | 2026-10-24 | claude | 94 | 0 | +0.0% | kept | planes.cc's planes_project() wrote the frame's xs/ys/cols/rows, x, y and slide and called _slide() itself, repeating planes_place()'s non-dot path line for line. |
| 38 | 2026-10-25 | claude | 10 | 0 | -0.1% | kept | opponent.cc's opponent_step() tested its waypoint reach (/dx/, /dy/ < 512) and the steering shrink loop (/dx/, /dy/ > 63) with eight signed int16 compares. |
| 39 | 2026-10-25 | claude | 22 | 0 | +0.2% | kept | opponent.cc's _opponent_draw() picked the front set's first block twice (again after the fill's flip) and spelt out planes_centre() twice and planes_place() three times. |
| 40 | 2026-10-25 | claude | 39 | 48 | -0.1% | kept | sim_run()'s four spelt-out `if (key_pressed(KSCAN_1..4)) view_update_view(...)` blocks (~12 B each) become four kHeldKeys rows whose input carries a kHeldKeyView flag. |
| 41 | 2026-10-25 | claude | 31 | 6 | -0.1% | kept | sim.cc's R key repeated _enter_simulation()'s msg_clear, flight_init_from_mission, opponent_reset, gun_reset and msg_show(kMissionTitles[m]) line for line. |
| 42 | 2026-10-25 | claude | 72 | 0 | -0.1% | kept | fpilot's static frames now take 63 B ($0249-$0287: sim_run 17, _opponent_draw's axes and f 38, _project 8), not the 132 B that raised its stack region to $0288 on 2026-10-18. |
| 43 | 2026-10-25 | claude | 55 | 0 | +0.2% | kept | opponent.cc's _opponent_toward() computed (int16)(((w << 8) - pos) >> 7) >> 1, seven 3-byte shift-right passes (81 B). |
| 44 | 2026-10-25 | merge | 1543 | - | -0.0% | merged | argon-loop passes 28-44 merged into main's tooling (275f325): its code changes to opponent.cc, planes.cc, planes_asm.cc, gun.cc, clouds.cc, title.cc and mem.h, analyze_ram.py's tscratch accounting and generate_planes.py. |
| 45 | 2026-10-25 | claude | 119 | 51 | -0.1% | kept | claude rows 40, 41 and 43 cherry-picked onto row 44 (29d4a26, aada719, 44b917e; their own messages keep claude-loop's numbers): the view keys in kHeldKeys, one _start_mission() for entering and R. |
| 46 | 2026-10-25 | claude | 75 | 0 | +0.3% | kept | claude row 29 ported (6c5c186): the waypoint pool, kOpponentWpX/Y[18] at opponent_path_t::wp, keeping argon-loop's _opponent_abs() reach test. Bit-identical. |
| 47 | 2026-10-25 | claude | 69 | 0 | -0.1% | kept | claude row 30 ported (1b093dd): the targets' navpoint fixed at 1, the search, _opponent_nav and kOpponentNoNav gone, opponent_test checking the mission order. Bit-identical. |
| 48 | 2026-10-25 | claude | 94 | 0 | -0.1% | kept | claude row 34 ported (df8b372): kView, kViewTwo, kViewFar and argon-loop's tscratch view pointer become one data-section _opponent_view that _opponent_set_view(far, two) writes per slot. Bit-identical. |
| 49 | 2026-10-25 | claude | 126 | 0 | +0.2% | kept | claude row 36 reworked (7a370ec): tscratch had no room left for _planes_lx/_ly (rows 44.e and 44.g filled it; the link failed with error 3034). |
| 50 | 2026-10-25 | claude | 93 | 0 | -0.0% | kept | claude row 35 ported (5510808): argon-loop's row-pointer _same_axes() and planes_project()'s axes store loop become claude-loop's _new_axes(). |
| 51 | 2026-11-01 | argon | 378 | 0 | -0.0% | kept | Rebased onto main (b4c1d6b + 03dd13c. |
| 52 | 2026-11-01 | argon | 69 | 73 | +0.2% | kept | music.cc shrinks _music_pwm_phase from uint16_t (step 8 mod 0x800) to a wrapping uint8_t counter, counts music_note_freq()'s shift down in a loop instead of f >> (6 - octave). |
| 53 | 2026-11-01 | argon | 136 | -4 | -0.1% | kept | Drop dead menu scrolling in fpilot (menu.cc, static_assert(kMissionCount <= kVisibleMissions)): inline the 4-mission render loop into _menu_enter() without the redundant 19-row memset or scroll arrows. |
| 54 | 2026-11-01 | argon | 46 | 21 | -0.0% | kept | Transform _world_p_start and vec_v in-place in _world_init_start_dx_dy() and world_update_roll_state() (dropping two 6 B stack vec3_t locals). |
| 55 | 2026-11-01 | argon | 91 | 89 | +0.1% | kept | Narrow _sprites_set_instrument_sprite() in sprites.cc from 6 parameters to (idx, dir) using 8-byte kInstrumentPivotX/Y tables and direct arm-table lookup. |
| 56 | 2026-11-01 | argon | 71 | 71 | -0.1% | kept | sound.cc drops per-frame shadow pokes to the 7 SID registers that never leave zero (V2 PwLo/PwHi, V3 PwLo/AttDec, CutoffLo/Hi, ResFilt, asserted 0 and zeroed by sound_silence()). |
| 57 | 2026-11-01 | argon | 215 | 194 | +0.1% | kept | view.cc copies flight_cam into world_cam and swaps front/left in view_update_cam() with rolled countdown byte loops (73 vs 237 B). |
| 58 | 2026-11-01 | argon | 132 | 101 | -0.0% | kept | Rebased onto main (e1298c6. |
| 59 | 2026-11-01 | argon | 173 | 0 | +0.7% | kept | opponent.cc drops kOpponentSlope[7] (deriving slope as -kOpponentClimbSpeed[climb + 3] * 4), narrows kOpponentPitchCos/kOpponentBankCos and _opponent_rot()'s cosine to uint8_t. |
| 60 | 2026-11-01 | argon | 127 | 0 | +0.1% | kept | gun.cc narrows _gun_track()'s eye argument from int32_t to uint16_t (dropping 24 B of dead high-word parameter stores). |
| 61 | 2026-11-01 | claude | 131 | 0 | +0.3% | kept | opponent.cc's kOpponentPaths had a 32 B row of kOpponentNone for each placeholder mission 06-10; it now has 6 rows (01-05 and one empty row) and opponent_reset() clamps any later mission to the last row. |
| 62 | 2026-11-02 | claude | 55 | 0 | -0.2% | kept | opponent.cc's _opponent_camera() built each camera-space component as range == kOpponentNear ? (u << 10) + (f >> 6) : (u << 8) + (f >> 8), and oscar64 compiled that as two separate 3-axis loops, one per range. |
| 63 | 2026-11-02 | claude | 22 | 0 | +0.2% | kept | opponent.cc's kOpponentPaths loses its last row, two kOpponentNone paths (32 B) that the placeholder missions 06-10 shared. |
| 64 | 2026-11-02 | claude | 45 | 36 | -0.2% | kept | keys.h's keys_pressed() (variable scan code, kBit mask) and keys.cc's keys_wait_release() were inlined at every key table's loop and wait. |
| 65 | 2026-11-02 | claude | 23 | 0 | +0.1% | kept | opponent_step() and _opponent_fall() each ended with two _opponent_move() calls along _opponent_head[i].x and .y; both now call one __noinline _opponent_ahead(i), which makes the same two calls in the same order. |
| 66 | 2026-11-02 | claude | 11 | 0 | -0.1% | kept | opponent_step()'s waypoint reach ((/dx/ / /dy/) < 512) and its cut loop (/dx/ >= 64 // /dy/ >= 64) made up to four _opponent_abs() calls per test. |
| 67 | 2026-11-02 | claude | 23 | 0 | +0.0% | kept | opponent.cc's outliner had split _opponent_abs()'s negation into a private $outline#27 (9 + 14 B). |
| 68 | 2026-11-02 | claude | 40 | 0 | +0.0% | kept | oscar64 unrolled two cold three-axis for loops in gun.cc: _gun_fire_from()'s copy of _gun_t/_gun_v into the new round (74 B) and gun_step()'s _gun_puff -= _gun_moved (57 B). |
| 69 | 2026-11-02 | claude | 135 | 133 | +0.0% | kept | kHelpText's key column was padded with runs of spaces (92 B in fpilot); each key is now followed by one '\t', which print_lines() pads with spaces to kPrintTabCol (9) columns from col. |
| 70 | 2026-11-02 | claude | 6 | 0 | +0.0% | kept | generate_planes.py emitted kPlaneRecip[42] with entries 0-2 zero and never read (dy 1 and 2 are shifts in both fills). |
| 71 | 2026-11-02 | claude | 9 | 0 | -0.1% | kept | fmission.cc's six kMissionStart* tables (X, Y, Z, Turns, Speed, Throttle) held a row for each placeholder mission 06-10, every one a copy of 04's (parked on runway 1). |
| 72 | 2026-11-02 | claude | 35 | 35 | +0.0% | kept | flight_advance()'s altitude penalty, min((Z - 0x080000) >> 12, 128) for Z > 0x080000, did a 32-bit compare, subtract and 12-bit shift (97 B). |
| 73 | 2026-11-02 | claude | 309 | 280 | -0.1% | kept | roll_asm.cc's 16 hand-written multiply-by-constant routines (137 B), their 66 B roll_mul_funcs pointer table. |
| 74 | 2026-11-02 | claude | 81 | 87 | -0.1% | kept | fmath.cc's _get_ratio() wrote its 6-bit shift-subtract division (n * 64) / d out as six copies of shift, compare, subtract and OR in a bit (~150 B). |
| 75 | 2026-11-02 | claude | 30 | 30 | +0.1% | kept | generate_music.py now overlaps kMusicLeadStartPat's 20 patterns (20 x 8 = 160 B) greedily into a 133 B kMusicLeadStartPool, and kMusicLeadStartBar holds each bar's start offset in it. |
| 76 | 2026-11-03 | claude | 5 | 0 | +0.0% | kept | damage.cc's kDamageBit[5] was {0x01, 0x02, 0x04, 0x08, 0x10}, exactly fmath.h's kBit[0..4] (failure k is bit k, now static_asserted), so damage_hit() reads kBit[k] instead; same LDA abs,x, damage_hit unchanged at 0x76 B. |
| 77 | 2026-11-03 | claude | 0 | 0 | +0.0% | dead end | Dead end. clouds_add_candidates() was changed to transform the deck's (0, 0, delta.z) once a frame (lazily, by the first cell past the world cull, into a tscratch _clouds_deck) and each cell's (dx, dy, 0) after it. |
| 78 | 2026-11-03 | claude | 41 | 44 | +0.0% | kept | vec_normalize() wrote its three vec_fastsqr8p8() calls and three vec_div8p8() calls out per component. |
| 79 | 2026-11-03 | claude | 148 | 148 | +0.3% | rejected by review: new comment says clip2_buf1 is used first, but the first dst is clip2_buf2 (src starts at proj_buf); fix the comment or the dst choice and resubmit | poly.cc's _project_vertices() spelt out four _clip_2d() stages (min_x, max_x, min_y, max_y) with the same n < 3 / src = dst / dst = other-buffer tail. |
| 80 | 2026-11-03 | claude | 112 | 112 | +0.2% | kept | poly.cc's _project_vertices() spelt out four _clip_2d() stages with the same n < 3 / src = dst / other-buffer tail. |
| 81 | 2026-11-03 | claude | 42 | 32 | +0.1% | kept | flight.cc's _flight_move_forward() (inlined into flight_advance(), both the AOA and default variants) wrote out flight_eye_x/y/z += _flight_step_s(v) as three 32-bit adds with sign extension (~33 B each). |
| 82 | 2026-11-03 | claude | 49 | 0 | -0.1% | kept | flight_init_from_mission()'s fpilot loop turned flight_cam a quarter turn at a time, kMissionStartTurns[m] times (89 B of unrolled vec3 copies and negations). |
| 83 | 2026-11-03 | claude | 41 | 0 | -0.0% | kept | opponent_step()'s small-angle heading turn (h->x = hx - s*hy, h->y = hy + s*hx, two inline vec_fastmul8p8 calls and the stores, ~90 B) now calls vec.cc's existing per-vector helper of vec_turn3_xy(). |
| 84 | 2026-11-03 | claude | 37 | 0 | -0.2% | kept | opponent_path_t's int16 head_x/head_y (4 B a path; every start heading is along an axis: (-256,0), (0,-256) or (256,0)) become quarter turns left of north in kind's free top two bits. |
| 85 | 2026-11-03 | claude | 12 | 12 | -0.3% | kept | world.cc's kMitchellPointsX/Y held _world_dx4/_world_dy4 indices (0, 2, 4, 6), so _world_init_start_dx_dy()'s per-frame 16-entry precompute read each through oscar64's __multab6L. |
| 86 | 2026-11-03 | claude | 14 | 14 | +0.0% | kept | generate_map_tiles.py now emits one kMapTileColors[18] (bit pair 10's colour in the high nibble, 11's in the low) in place of kMapTileLo[18] and kMapTileCol[18]. |
| 87 | 2026-11-04 | claude | 60 | 60 | +0.0% | kept | _world_init_start_dx_dy() took _get_msb(eye_z >> 9) >> 1, clamped it to 2..4 and then picked kNumPoints3/4 from the result. |
| 88 | 2026-11-04 | claude | 44 | 0 | +0.2% | kept | opponent.cc's _opponent_camera() spelt out its three 32-bit opponent-minus-eye differences (104 B, all four bytes each, though only bytes 0-2 are read). |
| 89 | 2026-11-04 | claude | 23 | 23 | +0.0% | kept | generate_map_tiles.py now emits the 18 map tiles' 8-byte row strings as a greedy shortest common superstring, kMapTilePool[104], plus kMapTileStart[18] (each tile's offset, asserted in the generator). |
| 90 | 2026-11-04 | claude | 15 | 15 | -0.1% | kept | roll.cc's kRollDx/kRollDy/kRollDxDivDy held angles 0-29; angle 30 - a is angle a with dx negated (and so dx*16/dy negated, exactly, as C division truncates towards zero; checked for all 29 pairs). |
| 91 | 2026-11-04 | claude | 33 | 33 | -0.0% | kept | flight_advance()'s arcade model (fpilot and ppilot) clamped flight_speed to 0..kMaxSpeed twice: airborne around the stall test (the low end before it, the top only when not stalled) and again in the ground branch afte... |
| 92 | 2026-11-04 | claude | 40 | 36 | -0.0% | kept | New __noinline keys_scan(keys, count) in keys.cc (entry i of a scan-code table to bit i) replaces sim_run()'s toggle-key loop. |
| 93 | 2026-11-04 | claude | 51 | 76 | +0.0% | kept | flight.cc's angle-of-attack flight_advance() wrote out five +-lim clamps (lift_t 4096, cl_d kFlightClPeak, flight_gamma 4096, the stall break's front.z 256, the turn's rot 24), each two signed 16-bit compares in line. |
| 94 | 2026-11-04 | claude | 39 | 39 | -0.2% | kept | sprites.cc's 9-byte sprite_cand_t entries were indexed by count through oscar64's __multab9L at every access. |
| 95 | 2026-11-04 | claude | 47 | 0 | -0.0% | kept | opponent.cc's _opponent_toward(w, pos) took the opponent's int32_t position by value. |
| 96 | 2026-11-04 | claude | 118 | 128 | +0.0% | kept | lib/find_boxes.py now emits each of the 68 horizon boxes as one object, struct { boxdef_t def; uint8_t map[N]; }, so boxdef_t drops its 2 B char_idx pointer (11 -> 9 B, 136 B of data). |
| 97 | 2026-11-05 | claude | 44 | 44 | -0.0% | kept | poly.cc's _clip_near_edge() wrote the intersection out twice, lifted by 8 when all six magnitudes fit and plain otherwise, each with its own pair of vec_mulfrac() calls. |
| 98 | 2026-11-05 | claude | 52 | 51 | -0.0% | kept | oscar64's rasterirq.c sizes its five dispatch tables (rasterIRQRows/Index/Next 17 B each, Low/High 16 B each, 83 B of main bss) by NUM_IRQS, default 16, but gfx_init_raster_irqs() only ever sets slots 0-3. |
| 99 | 2026-11-05 | claude | 72 | 72 | -0.0% | kept | gfx.cc's four handlers were oscar64's RIRQCode (32 B, sized for five writes), but each is rirq_build(ic, 1), which lays out only code[0..12] (RTS at 12; write 0's data at 1, its address/JSR at 9-11). |
| 100 | 2026-11-05 | claude | 31 | 0 | +0.1% | kept | opponent_fires() summed scatter and direction into x, y, z temporaries and wrote three unrolled (a << 1) + a products into v. |
| 101 | 2026-11-05 | claude | 24 | 24 | -0.1% | kept | poly.cc's _clip_near() indexed out[] (clip3_buf) by a vertex count, so every store and its &in[num_in - 1] went through oscar64's __multab6L (10 B, no other user). |
| 102 | 2026-11-05 | claude | 56 | 56 | +0.0% | kept | gfx.cc's _rirq_buf (4 x 14 B handler code, 56 B of bss2) moves to a new mem.h linker region "irqcode" at $FFC0-$FFF7, sized exactly to it. |
| 103 | 2026-11-05 | claude | 21 | 0 | +0.1% | kept | fpilot's kMissionStartSpeed/kMissionStartThrottle (2 x 10 B) were 0x88/0x12 for every mission starting in the air and 0/0 for the two starting at height 0 (02, 04 parked). |
| 104 | 2026-11-05 | claude | 83 | 83 | +0.0% | kept | mem.h's MEM_BSS2_END goes from $0800 to $0860, so bss2 also covers the BASIC link byte and oscar64's 82 B startup stub at $0801-$0852. |
| 105 | 2026-11-05 | claude | 25 | 0 | -0.2% | kept | gun_reset() (inlined into sim.cc's _start_mission()) copied flight_eye_x/y/z into _gun_eye[3] as twelve unrolled LDA/STA byte moves (60 B). |
| 106 | 2026-11-05 | claude | 10 | 16 | +0.0% | kept | sim_run()'s three spelt-out `if (toggle_edges & kToggleKeyF/G/N) flight_input(TOGGLE_FLAP/GEAR/NAV)` blocks (11 B each) become one do-while over the inputs 9..11. |
| 107 | 2026-11-05 | claude | 36 | 0 | +0.0% | kept | opponent.cc's kOpponentPaths[13][2] (12 B a slot, 312 B) repeated count, fail and speed in both slots of every pair. |
| 108 | 2026-11-05 | claude | 6 | 3 | -0.1% | kept | generate_music.py now emits the bass lane the lead's way, kMusicBassStepPool[48] with kMusicBassStepBar holding each bar's start offset (0/16/32), in place of kMusicBassStepPat[3][16] indexed by pattern. |
| 109 | 2026-11-07 | claude | 73 | -94 | +0.0% | kept | print_lines() now expands a byte from 0x80 up into one of ten words the help and mission texts repeat. |
| 110 | 2026-11-07 | claude | 10 | 9 | +0.1% | kept | sprites.cc's _sprites_orient_on/_sprites_fin_on (`view_state == VIEW_CENTER/BACK`, a compare and two loads, 16 B each, inlined into sim_run) become _sprites_orient_off/_sprites_fin_off = view_state ^ VIEW_CENTER/BACK... |
| 111 | 2026-11-07 | claude | 39 | 42 | +0.1% | kept | generate_music.py now lays the bass, lead gate and drum patterns end to end in kMusicBassStepPool[48], kMusicLeadOnBitsPool[6] and kMusicDrumBitsPool[20]. |
| 112 | 2026-11-07 | claude | 17 | 20 | -0.0% | kept | flight_advance()'s arcade model made two 32-bit compares of flight_eye_z beyond the clamp in _flight_move_forward(): the touchdown test's flight_eye_z <= kFlightMinEyeZ. |
| 113 | 2026-11-07 | claude | 30 | 0 | -0.0% | kept | gun.cc's _gun_view() spelt out the cone test (/y/ < x, /z/ < x) and the projection (centre - y/x, centre - z/x) once per axis, 104 B. |
| 114 | 2026-11-07 | claude | 12 | 0 | -0.1% | kept | gun.cc's _gun_smoke() had two per-axis loops: the smoking aircraft's (_gun_t[c] - _gun_h[c] * kSmokeBack) >> 1, and the ground puff's own _gun_puff[c] >> 1. |
| 115 | 2026-11-07 | claude | 55 | 0 | +0.1% | kept | opponent.cc's _opponent_head, _next, _turn, _bank, _climb, _pitch, _hits and _gun (26 B of bss2, every byte 0 when a pair starts) move into one struct _opponent_fresh, kept under their old names by #defines. |
| 116 | 2026-11-07 | claude | 64 | 67 | -0.2% | kept | sound_silence()'s _sound_write_through() was a fully unrolled copy of the just-zeroed shadow to $D400 (25 x STA abs, 75 B; unrolled because oscar64 dropped the index from the rolled ascending copy). |
| 117 | 2026-11-07 | claude | 177 | 174 | +0.3% | kept | gfx_init_raster_irqs() set its four slots with rirq_set() and ran oscar64's rirq_sort() (118 B), and _gfx_rirq_init() ran rirq_init_tables(). |
| 118 | 2026-11-07 | claude | 30 | 39 | -0.4% | rejected by review: docs/sound.md still says the blit makes 25 register stores that "cannot change"; update it for the 6 skipped registers | sound_blit() (fully unrolled in _gfx_switch_to_terrain, every raster IRQ) copied all 25 shadow bytes to $D400, but sound_update() never moves V2 PwLo/PwHi, V3 PwLo or the three filter registers off zero. |
| 119 | 2026-11-07 | claude | 30 | 39 | -0.4% | kept | sound_blit() (fully unrolled into _gfx_switch_to_terrain, every raster IRQ) copied all 25 shadow bytes to $D400, but sound_update() never moves V2 PwLo/PwHi, V3 PwLo or the three filter registers off zero. |
| 120 | 2026-11-07 | claude | 24 | 0 | +0.0% | kept | Every fpilot mission has the same two navpoints (targets, home), so under __FPILOT__ flight.h's kMaxNavPoints is 2 (flight_nav_point_x/y 8 -> 4 B each) and generate_map_tiles.py emits kMapDigitCount 2 with digits '3'/... |
| 121 | 2026-11-07 | argon | 10 | 20 | -0.7% | kept | Move FLIGHT_INPUT_BRAKE right after FLIGHT_INPUT_THROTTLE_DOWN (1..9 contiguous) so sim.cc's kHeldKeys[13] drops its input byte (26 -> 13 B, entries 0..8 -> flight_input(i+1), 9..12 -> view_update_view(i-8)). |
| 122 | 2026-11-07 | argon | 181 | 209 | -0.4% | kept | poly.cc's _project_vertices() folds its overflow-safe round-half-up subpixel mapping into `(80 - (vec_sx >> 1)) >> 1` and `(28 - (vec_sy >> 1)) >> 1` (moving fallback `px = 40; py = 14;` to `else`). |
| 123 | 2026-11-08 | argon | 8 | 8 | -1.8% | kept | Add inline vec_project_viewport() in vec_asm.cc/vec.h for gfx_project_and_draw() (the only inlined grid-dot caller). |
| 124 | 2026-11-08 | argon | 84 | 84 | -3.4% | kept | Extract `__noinline int16_t vec_dot(const vec3_t *a, const vec3_t *b)` in vec.cc/vec.h (`vec_transform_inv. |
| 125 | 2026-11-09 | fable | 67 | 61 | -1.0% | kept | sound_blit() (in _gfx_switch_to_terrain, every raster-250 IRQ) stored all 19 live SID registers on every tick, though the shadow only changes once a frame (6-10 ticks). |
| 126 | 2026-11-09 | fable | 50 | 50 | -1.0% | kept | Every raster IRQ went through an oscar64 RIRQCode stub, which spins on $d012 for about a raster line to take the entry's jitter out. |
| 127 | 2026-11-10 | fable | 14 | 5 | -1.1% | kept | A VICE profile put the raster interrupts at ~9% of every frame (about 1,800 cycles per PAL frame, most of it the terrain and panel-bottom handlers copying sprite registers). |
| 128 | 2026-11-10 | fable | 4 | 4 | -0.8% | kept | world_render_grid()'s per-dot loop (the biggest slice of a frame, ~22% of cruise in a VICE profile) summed all three components of vec_v before the cone tests. |
| 129 | 2026-11-12 | fable | 53 | 65 | -0.8% | kept | The split's first raster interrupt only wrote $D015 = 0 on raster 140, 22 lines above the panel. |
| 130 | 2026-11-12 | fable | 10 | 1 | -0.4% | kept | render_fill_sky_ground() rewrote the 40 colour bytes of every whole row of sky each frame (320 cycles a row), though the colour buffer nearly always held the sky colour there already. |
| 131 | 2026-11-12 | fable | 6 | 6 | -0.7% | kept | A VICE profile of scramble showed the parked runway polygon paying four exact divisions a frame (about 770 cycles each) for its two near-clipped vertices. |
| 132 | 2026-11-13 | hydrogen | -137 | -151 | -1.3% | kept by hand, at the user's call: it loses RAM, which the loop's gate does not allow | Ported from hydrogen-loop's only pass (8f98281, its "pass 121"), whose per-axis cloud transform argon's pass 124 had already landed on main; only its new part is taken. |
| 133 | 2026-11-13 | fable | 22 | 28 | -1.7% | kept | An ablation of the frame (each stage stubbed out and timed with tools/bench.sh) put the grid's dots at 22-26k cycles in every scene, most of it the ~400-cycle projection of dots the plot then throws away. |
| 134 | 2026-11-13 | hydrogen | 1 | -52 | -0.7% | merged | (On `hydrogen-loop`, pre-133 branch baseline) poly.cc's _project_vertices() adds a hand-assembled 3D frustum early-reject loop in `__asm` (with matching `#else` host C fallback) right after `_clip_near()`: every `_cli... |
| 135 | 2026-11-13 | hydrogen | 9 | 17 | -0.6% | merged | (On `hydrogen-loop`, pre-133 branch baseline) Split `planes_project()` in `planes.cc`/`planes.h` into `planes_centre()`. |
| 136 | 2026-11-13 | fable | 2 | 5 | -0.8% | kept | vec_fastmul8p8 took its four-product path whenever both low bytes were nonzero, so an /a/ below 256 (a camera row's component in vec_dot() and the clouds, a fraction's byte in vec_mulfrac()) paid for ah*bh and ah*bl. |
| 137 | 2026-11-13 | hydrogen | 0 | 0 | -0.9% | merged | `poly.cc`'s `_poly_scan_lines2()` replaces its C fringe loop (`#ifdef __OSCAR64__`, preserving the `#else` C fallback) with a hand-assembled `__asm` fringe loop (`L_fr_loop..L_fr_end`) that skips directly over `[overl... |
| 138 | 2026-11-13 | hydrogen | 13 | 13 | -1.3% | merged | `world.cc`'s `_world_draw_box_points()` replaces its C terrain-dot loop (`#ifdef __OSCAR64__`, preserving the `#else` C fallback) with a hand-assembled `__asm` loop (`L_wd_loop..L_wd_done`) using shared `vec_proj_*` Z... |
| 139 | 2026-11-13 | hydrogen | 0 | 0 | -0.6% | merged | `render.cc` speeds up horizon background filling and center snapping (`#ifdef __OSCAR64__`, preserving `#else` C fallbacks): (1) replaces per-row `_fill_full_row()` calls in `render_fill_sky_ground()` with consecutive... |
| 140 | 2026-11-14 | argon | 176 | 176 | -0.6% | kept | Rewrite `poly.cc`'s `poly_fill()` rasterisation pipeline (`_poly_bounds_and_clear()`, `_poly_trace_edges()`, and the full outer row/solid-core/fringe loop of `_poly_scan_lines2()`) as `static inline` 6502 `__asm` in `... |
| 141 | 2026-11-14 | argon | 213 | 0 | +0.1% | kept | Port the `fpilot` opponent silhouette renderer's remaining hot stages (`_planes_fuselage()`, `_planes_layout()`, `planes_place()`, 2-arg `_planes_key()`, and the outer `planes_fill. |
| 142 | 2026-11-15 | argon | 65 | 0 | -0.5% | kept | Extract `fpilot`'s 3-axis 24.8 world-space delta, 7-bit `x` / 8-bit `y` wraparound, range classify. |
| 143 | 2026-11-15 | argon | 114 | 0 | -0.2% | kept | Port `gun.cc`'s per-step 6502 hot paths to `__asm` in new `gun_asm.h` (`#ifdef __OSCAR64__`, keeping `#ifndef __OSCAR64__` C counterparts in `gun.cc` and adding test 18 in `target_test.cc`): `_gun_track_all()` steps `... |
| 144 | 2026-11-14 | fable | 25 | 25 | -2.6% | rejected by review: polydemo.prg never calls sprites_init(), so `used` stays 0 and the terrain IRQ's pointer/colour loop runs 256 times, overwriting every VIC register each PAL frame | (Run as pass 140 on `fable-loop`, alongside argon's 140-143.) The terrain raster handler (sprites.cc _sprites_program_frame, every PAL frame) wrote $D015 last, after ~450 cycles of copying, so every unused sprite. |
| 145 | 2026-11-15 | fable | 28 | 28 | -2.7% | kept by hand | (Run as pass 141 on `fable-loop`.) Pass 144's idea with the fault its review found put right. |
| 146 | 2026-11-16 | argon | -25 | 0 | -0.7% | kept | Stage `opponent.cc`'s `_opponent_cull()` camera-space transform and frustum reject one row at a time via `vec_dot(&world_cam.row, &vec_v)` directly into `_opponent_cs[i]`: compute `cx = vec_dot. |
| 147 | 2026-11-17 | argon | -91 | -84 | -1.4% | kept | Replace `vec_asm.cc`'s bit-by-bit `vec_frac_loop` (~49 cycles/bit) with exact `vec_frac8`. |
| 148 | 2026-11-17 | argon | 27 | 6 | -0.3% | kept | Port `sprites.cc`'s per-frame viewport sprite stack hot path (`sprites_stack_add()` and `sprites_stack_commit()`) to 6502 `__asm` in new `sprites_asm.h`. |
| 149 | 2026-11-17 | argon | 12 | 10 | -0.1% | kept | Split Pass 148's header-only assembly out of `sprites_asm.h` into a separate `sprites_asm.cc` translation unit (`#pragma compile. |
| 150 | 2026-11-17 | argon | -85 | 0 | -0.5% | kept | Speed up `planes_asm.cc` / `planes.cc` opponent silhouette projection, resliding, and polygon fill: port `planes_reslide()` to 6502 `__asm` sharing `_place()` with `planes_place()` and skipping zero-delta `key_y` loops. |
| 151 | 2026-11-18 | argon | -7 | 0 | -0.1% | kept | Extract `gun_asm.h`'s 6502 assembly and shared projectile/smoke state into standalone `gun_asm.cc` (`#pragma compile("gun_asm.cc")` in `gun_asm.h`). |
| 152 | 2026-11-18 | argon | -23 | 0 | -0.7% | kept | Extract `opponent_asm.h` into standalone `opponent_asm.cc` (`#pragma compile("opponent_asm.cc")` in `opponent_asm.h`). |
| 153 | 2026-11-22 | argon | 7 | -2 | -1.0% | kept | Port `clouds.cc` per-frame scan, staged camera-row dot products, half-basis generation, and blob offsets to standalone 6502 `__asm` module `clouds_asm.{h,cc}` (keeping `#ifndef __OSCAR64__` C counterparts and splitting `target_planes_test.prg` in `c64o/test/` for Test 21). |
| 154 | 2026-11-22 | argon | 6 | 27 | -1.1% | kept | Port `world.cc` per-frame grid pipeline (`_world_init_start_dx_dy`, `_world_build_obj_verts`, `_world_draw_box_points`, `_world_render_grid_walk` with 8-bit high-byte 3D cell frustum cull) into standalone 6502 `__asm` module `world_asm.{h,cc}` (+15 B stack headroom, +2 B ZP headroom, Test 22 in `target_test.cc`). |
| 155 | 2026-11-23 | argon | 3 | 6 | -2.3% | kept | Port `poly.cc` 3D-to-2D near-plane clip, tightened `0.75·x`/`0.25·x` frustum reject, projection, and unified striped 2D viewport clip into standalone 6502 `__asm` module `poly_asm.cc` (zero-delta skips on level ground polygons, Test 23 in `target_test.cc`). |
| 156 | 2026-11-23 | argon | 14 | 3 | -5.3% | kept | Port `box.cc` horizon tile preparation (`_box_prepare_fill`) and drawing (`_box_draw_asm`, with 4×-unrolled horizontal fast path for `step_y == 0` and self-modifying tilted tile copy) into standalone 6502 `__asm` module `box_asm.{h,cc}` (+9 B stack, +3 B ZP headroom, Test 24 in `target_test.cc`). |
| 157 | 2026-11-23 | argon | 34 | 316 | -1.3% | kept | Extract shared 8.8 multiply core `vec_mul_ab` in `vec_asm.cc`, port `vec_fastsqr8p8`, `vec_dot`, `vec_transform_inv`, `vec_cross`, `vec_rotate_step` to 6502 `__asm`, and port `_opponent_move`, `_opponent_ahead`, `_opponent_axes`, `_opponent_cull_slot` in `opponent_asm.{h,cc}` (Tests 25–26 in `target_test.cc`). |
| 158 | 2026-11-23 | argon | 7 | -7 | -1.4% | kept | Port `render.cc` `_pull_to_center` + `render_fill_sky_ground` (2×-unrolled self-modifying partial spans, paired 6-step sled sync, `end_row==14` tail jump) to `render_asm.{h,cc}`, cache `roll_update_state()` angle in `roll.cc`, and early-return `x==0`/`y==0` in `fmath.cc` `_get_ratio()` (Test 27 in `target_test.cc`). |
| 159 | 2026-11-23 | argon | 40 | 38 | -0.4% | kept | Port arcade `flight_advance()` physics, fuel step, and turn rate into standalone 6502 `__asm` module `flight_asm.{h,cc}` (`combat5 -0.84%`, Test 28 in `target_planes_test.prg`). |
