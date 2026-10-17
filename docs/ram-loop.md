# RAM and cycle optimisation loop

`tools/ram_loop.sh` runs an unattended loop that frees RAM in fpilot and saves
cycles. RAM and cycles matter equally. Each pass starts a fresh Claude agent,
which looks for **one** new saving and implements it. The script then rebuilds
everything, runs every test, measures free RAM, times the render with
`tools/bench.sh`, takes the `make_shots` screenshots, and has a second agent review the diff
against the rules below. A change is committed only if all of that passes.
Otherwise it is reverted, and the attempt is logged here as a dead end so that
later passes skip it.

This file is the loop's memory. Every pass reads it first.

## Goal and metrics

- **RAM:** fpilot's "Free, allocatable" from `make fram`, after a full
  `make -C c64o all`.
- **Cycles:** `tools/bench.sh` gives the render work per frame (`bm_start()`
  through `bm_total()` in `sim_run()`) in five frozen fpilot scenes: runway,
  approach, cruise, town and lake. The emulator is cycle-exact and the scenes
  hold still, so the numbers are deterministic. Only code changes move them.

A change is kept only when all of these hold:

- fpilot free RAM does not drop.
- **Every scene stays within +2%** of the fixed baseline below. The baseline
  does not move as passes are kept, so small slowdowns cannot add up past 2%.
- It gains something: more free RAM, or at least 0.5% fewer total cycles than
  the last kept state.

ppilot and ppilota must still build and pass their tests. Their free RAM is
logged but is not a gate.

The script reads this line, so keep its format exactly:

cycles-baseline: runway=122384 approach=116327 cruise=138732 town=141890 lake=130944

## Rules (binding for the worker and the reviewer)

1. **Rendering stays as it is.** Small visual changes are allowed:
   - The dot layout (ground dots, clouds and the like) may change, as long as
     the visual density stays similar.
   - Polygon rendering may change slightly, as long as each polygon stays
     close to the original: an edge pixel here and there, not a different
     shape.
   - Nothing may disappear, flicker, tear or change colour. Panel, map, menu,
     help and title screens stay pixel-identical, apart from timing-driven
     differences such as the menu flyby being a frame ahead.
2. **The flight model must behave similarly.** Loops, climbs, turns, takeoff
   and landing must all still work and feel the same. Small numeric
   differences are fine if every one of these stays supported.
   - Changing a `c64o/test/flight_test.cc` expectation is a red flag. Do it
     only for a tolerance-sized shift, and give the reason in the summary.
     Never delete a test or weaken a test so that it no longer checks the
     behaviour.
3. **Cycles count as much as RAM.** Spend the 2% cycle budget only for a
   RAM gain that is worth it, and prefer savings that are speed-neutral or
   faster. Pure speed-ups are welcome too. Do not un-inline anything in the
   per-point or per-edge hot paths (grid loop, `vec_project`, `vec_add`, the
   poly fill loops). The bench covers the render only. Do not slow down the
   flight model or the sound either, even though they are not timed.
4. **Keep every feature.** The debug view is already gone from fpilot. Nothing
   else may be dropped: sound, music, the map, missions, clouds, traffic
   sprites, menu and help.
5. **All link checks and tests must pass:** zp, irq, math, rom and boot
   checks, the host suites, target_test and `make test`.
6. **One idea per pass,** small enough to review. No sweeping rewrites.
7. **Code style:** match the surrounding code's comments, naming and idiom.
   Update comments and docs that the change makes stale (for example
   `docs/memory_map.md` and `docs/codesize.md`).

## Known traps

These come from earlier work (see the auto-memory notes `fpilot-ram-survey`,
`inline-audit`, `oscar64-bugs` and `vec-div-exact`).

- `__striped` on a table of `&object` pointers miscompiles: all high bytes
  come out 0. Striped arrays of integers, and of `array + offset` entries,
  work.
- At -O1 and above, a `t = a; a = b; b = t;` swap inside a loop loses
  `b = t`. Pick the endpoints instead of swapping.
- Inlining `_project_vertices` pushes ppilot's zero-page spill to $5F, with no
  headroom left. Inlining `box_prepare` together with other inlines fails
  check_zeropage.
- Inlining `_fill_line`, `vec_transform*`, `vec_div8p8` or `_clip_2d` costs
  0.8-1.3 KB each.
- oscar64 inline asm takes only literal offsets. Pin them with a
  static_assert.
- With tables behind a pointer constant, a signed int8 index makes oscar64
  build 16-bit addresses. Use a uint8 counter and a base pointer.
- crt.c zeroes only BSSStart..BSSEnd. bss2 is memset by `mem_init()`.
- Main bss objects sit in holes of the page-aligned data. Moving an object
  that fills a hole gains nothing.
- check_irq_zp forbids zero page below $60 in IRQ handlers. Pad any ISR
  prologue change to a multiple of 7 cycles.
- oscar64 aborts silently on long paths. `make` in c64o/ works from the repo.
  Scratch builds and `make_shots` need a short build dir under /tmp.

## Measured dead ends (do not retry)

- LZO-compressing the menu/help/music/title overlay: 4,074 → 3,568 B, not
  worth it.
- Mirror-dedupe of boxes: no box is an exact L/R or U/D mirror of another.
- Edge-RLE on the box maps: only 364 B.
- H-mirror char dedupe: needs a 256 B lookup table.
- 7-bit relative char index with the flip in bit 7: 7 boxes span more than
  128 slots.
- Un-inlining `vec_add`: 41 B, but about 1k cycles a frame.
- `vec_fastsqr8p8` → `fastmul(a,a)`: twice as slow; the user rejected it.
- An asm square: −40 B, mixed speed.
- `__striped` on `_sprites_cand` (−11 B), kFlightRecipV and
  kMissionWpConstraint (0 B).

## Open leads from earlier surveys (check status first)

- The startup stub at `$0801` (96 B) is not reused yet. poly.cc's clip buffers
  are the candidate, but they have an adjacency rule.
- Main-screen rows 14-24 (`$EA30-$EBF7`, 456 B) may be dead in flight now
  that the debug view is gone; they could hold per-frame scratch.
- Orphan fragments: `$EBE8`, `$EFE8` (16 B each) and `$FFC0-$FFF9` (58 B).
  They can only be reached by hand-placing data.
- Stack headroom: 82 B, reachable by lowering `#pragma stacksize`.

## Log

Baseline on 2026-10-16 at 63e2950: fpilot 9,546 B free allocatable (largest
run 8,970 B), ppilot 6,466 B. Render cycles per frame: runway 131,082,
approach 122,756, cruise 150,020, town 152,709, lake 138,509; total 695,076.

The cycles baseline was reset on 2026-10-17 after pass 21 (the argon-loop
merge, 84c4ccb), so the 2% budget starts from its faster render: fpilot
14,197 B free (largest run 13,066 B), ppilot 10,686 B. Render cycles per
frame: runway 122,384, approach 116,327, cruise 138,732, town 141,890, lake
130,944; total 650,277. The "vs baseline" figures in rows 1-20 are against
the old one.

The script appends one row per pass. The fpilot and ppilot columns are the
change in free allocatable bytes. The cycles column is the change in bench
total against the last kept state.

| Pass | Date | fpilot Δ | ppilot Δ | Cycles Δ | Result | Idea |
|-----:|------|---------:|---------:|---------:|--------|------|
| 1 | 2026-10-17 | 45 | 45 | +0.3% | kept | Replaced the 48 B kMusicVolumeMix[3][16] table in music.cc with its three closed forms (0, v>>1, v) in music_master_volume() (codesize.md §3.4): fpilot 9,546 -> 9,591 B free (+45), ppilot 6,466 -> 6,511 (+45); bench total 695,076 -> 697,254 (+0.31%, layout shift only — music is not in the render path; worst scene approach +0.51%, all well inside +2%). |
| 2 | 2026-10-17 | 45 | 45 | -0.2% | kept | Dropped the file-wide nooutline pragma from fmath.cc (codesize.md §3.1; _get_ratio runs at most 3x a frame): fpilot 9,591 -> 9,636 B free (+45), ppilot 6,511 -> 6,556 (+45); bench total 697,254 -> 696,176 (-0.15%; runway 130920 approach 123093 cruise 150529 town 152914 lake 138720, all inside +2%). |
| 3 | 2026-10-17 | 125 | 125 | +0.4% | kept | kSpriteDefLongArm/ShortArm now hold 16 pivots (2 B each) instead of 32 {pivot, bitmap} rows: direction d+16 reuses bitmap d with the pivot mirrored through (12,10), and the bitmap is base + (d & 15); _sprites_roll_to_dir became a 30-entry _sprites_roll_to_bitmap (it repeats every half-turn), and _sprites_set_instrument_sprite went __noinline (inlined, the mirror cost 70 B at six sites). fpilot 9,636 -> 9,761 B free (+125), ppilot 6,556 -> 6,681 (+125). Bench total 696,176 -> 698,756 (+0.37%; runway 131234 approach 123741 cruise 150794 town 153646 lake 139341, worst approach +0.80% vs baseline). The inline variant measured +55 B at +0.03%; runway got faster under the out-of-line build, so part of the +0.37% is layout shift. |
| 4 | 2026-10-17 | 38 | 41 | -0.1% | kept | Wrapped _limit_vec/vec_normalize/vec_cross/vec_orthonormalize in vec.cc in #pragma optimize(push, outline) (codesize.md §3.2; flight model only, once per model step, never in the render): fpilot 9,761 -> 9,799 B free (+38), ppilot 6,681 -> 6,722 (+41); bench total 698,756 -> 698,083 (-0.10%; runway 131118 approach 123614 cruise 150700 town 153458 lake 139193, all inside +2%). |
| 5 | 2026-10-17 | 64 | 64 | +0.0% | kept | SP is set at startup and read nowhere (no dynamic stack use), so mem.h's stack region shrinks $0200-$0280 -> $0200-$0240 (stacksize 0x80 -> 0x40) and bss2 starts at $0240: fpilot 9,799 -> 9,863 B free (+64), ppilot 6,722 -> 6,786 (+64); bench total 698,083 -> 698,137 (+0.01%, layout only; runway 131351 approach 123549 cruise 150698 town 153411 lake 139128, all inside +2%). |
| 6 | 2026-10-17 | 43 | 40 | -0.1% | kept | Dropped the unsigned twin of the flight step shift (codesize.md §3.3); the drag square, the only argument that can reach 0x8000 (0xE100 at kMaxSpeed), is halved before the call and its three shifts are one less, so drag stays bit-identical: fpilot 9,863 -> 9,906 B free (+43), ppilot 6,786 -> 6,826 (+40); bench total 698,137 -> 697,669 (-0.07%, layout only; runway 131356 approach 123331 cruise 150796 town 153163 lake 139023, all inside +2%). |
| 7 | 2026-10-17 | 96 | 96 | -0.0% | rejected by review: XOR flip assumes the table was set up first; polydemo never calls mem_use_main_buffer(), so its row pointers end up at $0000 and it draws into zero page | mem.cc drops kScreenRowPtrsMain/Alt (2 x 50 B): mem_use_main_buffer() fills mem_screen_row_ptrs with a +40 loop and mem_switch_buffer() XORs each high byte with $04 (the buffers are 1 KB apart, static_asserted); fpilot 9,906 -> 10,002 B free (+96), ppilot 6,826 -> 6,922 (+96); bench total 697,669 -> 697,527 (-0.02%; runway 131531 approach 123200 cruise 150721 town 153133 lake 138942, all inside +2%). The per-frame flip is 25 x 18 cycles instead of a 50 x 14 copy, outside the bench window. |
| 8 | 2026-10-17 | 132 | 132 | +0.1% | kept | roll.cc keeps 30 of the 60 rows of kRollDx/kRollDy/kRollDxDivDy/kRollPeriod; angle a+30 is angle a with (dx, dy) negated (same slope and period, checked for all 30 pairs), so roll_update_state() folds it with one compare: fpilot 9,906 -> 10,038 B free (+132), ppilot 6,826 -> 6,958 (+132); bench total 697,669 -> 698,294 (+0.09%, layout shift; runway 131343 approach 123671 cruise 150824 town 153410 lake 139046, worst approach +0.75% vs baseline). |
| 9 | 2026-10-17 | 55 | 58 | -0.1% | kept | fmath.cc drops kRollAngleLut[65]; it is exactly min(ratio >> 2, 15) for all 65 ratios (checked), so _get_roll_angle() shifts and clamps 16 to 15. Bit-identical. fpilot 10,038 -> 10,093 B free (+55), ppilot 6,958 -> 7,016 (+58); bench total 698,294 -> 697,884 (-0.06%; runway 131312 approach 123441 cruise 150783 town 153300 lake 139048, all inside +2%). |
| 10 | 2026-10-17 | 25 | 35 | -0.0% | kept | fmath.cc drops kHeadingLut[65]; it is exactly ratio / 5 for all 65 ratios, which _get_heading() now computes as (ratio * 51 + 12) >> 8 with 51 = 3 * 17 in shifts and adds (no runtime multiply; checked bit-identical for every ratio). fpilot 10,093 -> 10,118 B free (+25), ppilot 7,016 -> 7,051 (+35); bench total 697,884 -> 697,618 (-0.04%; runway 131122 approach 123285 cruise 150866 town 153212 lake 139133, all inside +2%). The flight step pays about 30 cycles per call, twice per step. |
| 11 | 2026-10-17 | 201 | 195 | -0.3% | kept | The flight model's six 18 B kVec* control matrices and vec_transform3() (27 multiplies plus an 18 B transposed copy per step) are replaced by vec_rotate_step(): each step only turns two rows of the camera matrix towards each other (roll left/up, pitch front/up, yaw front/left), so it does p += s*q, q -= s*p with 6 multiplies and a 6-byte int8 angle table that vec_set_rotation_shift() rescales. Bit-identical (same three fastmul identities as vec_turn3_xy, plus commutativity of the magnitude product). fpilot 10,118 -> 10,319 B free (+201), ppilot 7,051 -> 7,246 (+195); bench total 697,618 -> 695,811 (-0.26%, render untouched so layout only; runway 130755 approach 122929 cruise 150687 town 152712 lake 138728, all inside +2%). The flight step also gets cheaper: flight.md §8 measured 6,191 cycles a frame for the 27-multiply path while a key is held. |
| 12 | 2026-10-17 | 49 | 49 | +0.1% | kept | Every shift by a variable amount (sim.cc's three key-table loops and keys_wait_release via key_pressed, the fin-row 0x80 >> n in sprites.cc, map_set_overlay_pixel's 1/3 << shift, and the boot-time 8 >> / 1 << / 32 >> shift in flight_set_step_shift and vec_set_rotation_shift) linked oscar64's 56 B bitshift table (and __shltab3L, 7 B); they now read fmath.cc's kBit[8] (new keys_pressed() in keys.h), use bit / bit << 1, or loop over the shift (boot code, shift 0..2). All bit-identical. fpilot 10,319 -> 10,368 B free (+49), ppilot 7,246 -> 7,295 (+49); bench total 695,811 -> 696,305 (+0.07%, layout only, no render code touched; runway 130906 approach 123053 cruise 150736 town 152763 lake 138847, all inside +2%). |
| 13 | 2026-10-17 | 30 | 30 | -0.6% | kept | world_render_grid() looked up kWorldMap[cx & 15][cy & 31] for every cell, which rebuilt the row address through oscar64's 32 B __multab32L/H tables on each inner step; the row pointer is now set once per x row as kWorldMap[0] + ((uint16_t)(cx & 15) << 5), so the tables are no longer linked. Same addresses, bit-identical output. fpilot 10,368 -> 10,398 B free (+30), ppilot 7,295 -> 7,325 (+30); bench total 696,305 -> 691,912 (-0.63%; runway 130342 approach 122230 cruise 149393 town 151881 lake 138066, all faster). |
| 14 | 2026-10-17 | 165 | 165 | -0.5% | kept | The lead starts notes only on even rows, so generate_music.py now emits kMusicLeadStartPat[20][8] (even rows only, the premise is asserted) and MUSIC_LEAD_START_IN reads odd rows as 0 via (r & 1) ? 0 : pat[r >> 1]; the tune is bit-identical. fpilot 10,398 -> 10,563 B free (+165), ppilot 7,325 -> 7,490 (+165); bench total 691,912 -> 688,792 (-0.45%, layout shift only since the player is not in the render path; runway 130481 approach 121672 cruise 148458 town 150405 lake 137776, all inside +2%). |
| 15 | 2026-10-17 | 117 | 120 | +0.4% | kept | Every bass note is its bar's chord root + 0/7/10/12, so generate_music.py now emits kMusicBassStepPat[3][16] (interval + 1, 0 = no new note; the 11 note patterns collapse to 3 rhythm shapes) in place of kMusicBassStartPat[11][16], and music_tick() tests the step for the hard restart and adds MUSIC_CHORD(bar).root only when a note starts (new MUSIC_BASS_STEP_IN / MUSIC_BASS_NOTE; MUSIC_BASS_START_IN keeps its meaning for music_test). The tune is bit-identical. fpilot 10,563 -> 10,680 B free (+117), ppilot 7,490 -> 7,610 (+120); bench total 688,792 -> 691,692 (+0.42%, layout shift only since the player is not in the render path; runway 130345 approach 122276 cruise 149171 town 151871 lake 138029, all inside +2%). |
| 16 | 2026-10-17 | 420 | 0 | -0.0% | kept | New fpilot-only linker region "scratch" at $EA30-$EBE7 (main screen rows 14-24, never fetched in flight: the panel split writes $d018 on line 162, before row 14's badline at 163, and the panel colors come from the alt screen) now holds poly.cc's per-call buffers (_min_x/_max_x, clip3_buf, proj_buf, clip2_buf1/2 still adjacent and in order, final_verts) and world.cc's per-frame _world_dx4/_world_dy4/poly_verts, 420 of 440 B; ppilot (debug view uses those rows) keeps them in bss2. fpilot 10,680 -> 11,100 B free (+420), ppilot 7,610 unchanged; bench total 691,692 -> 691,680 (runway 130337 approach 122272 cruise 149171 town 151871 lake 138029, cycle-neutral). |
| 17 | 2026-10-17 | 95 | 95 | +0.0% | kept | boxdefs.cc's alt_boxes[60] (120 B of pointers, 52 of them NULL) becomes alt_box_angles[8] plus alt_boxes[8], and boxdef_set_alt() searches the 8 angles, falling back to boxdef_set_main() (which keeps the kRollMax check). The same tiles are picked, so the output is bit-identical. fpilot 11,100 -> 11,195 B free (+95), ppilot 7,610 -> 7,705 (+95); bench total 691,680 -> 691,787 (+0.02%; runway 130374 approach 122328 cruise 149100 town 151996 lake 137989, all inside +2%). |
| 18 | 2026-10-17 | 817 | 817 | +0.0% | kept | chardefs[224][8] (1,792 B) becomes chardef_bytes[728], a greedy shortest common superstring in which every stored char is 8 consecutive bytes (one char's bottom rows are often another's top rows), plus chardef_offset_lo[224]; the generator numbers the chars in the order they start in the stream, so the offset grows with the index and box_prepare() adds 256 at kCharDefPage1 and again at kCharDefPage2 (two compares). Box output is bit-identical. fpilot 11,195 -> 12,012 B free (+817), ppilot 7,705 -> 8,522 (+817); bench total 691,787 -> 692,080 (+0.04%; runway 130298 approach 122319 cruise 149322 town 152108 lake 138033, all inside +2%). |
| 19 | 2026-10-17 | 221 | 221 | +0.5% | kept | The 9 MAP_OBJ_* types move from 16..24 to 7..15, so every map type fits a nibble and kWorldMap[16][32] (512 B) becomes kWorldMap[16][16] (256 B, even column in the low nibble), read through new world.h helpers world_map_row()/world_map_cell() in world_render_grid, map.cc, flight.cc, polydemo and map_test; world_map.cc keeps one entry per cell via a WORLD_MAP_ROW/WORLD_MAP_PAIR macro. fpilot 12,012 -> 12,233 B free (+221), ppilot 8,522 -> 8,743 (+221); bench total 692,080 -> 695,804 (+0.54%, ~13 cycles per grid cell for the nibble select; runway 130910 approach 122998 cruise 150487 town 152631 lake 138778, worst cruise +0.31% vs baseline). |
| 20 | 2026-10-17 | 181 | 201 | +0.0% | kept | boxdef_t loses total_size (= w * h) and the box_chars pointer: find_boxes.py now emits one box_*_map[] per box (char_count chardefs indices, then the w * h cells), so box_prepare() reads the cells at char_idx + char_count and sums w * h with an add loop (h <= 11, slow path only, no multiply linked); the per-frame boxdef copy shrinks 14 -> 11 B. Box output is bit-identical (generator verifier passes). fpilot 12,233 -> 12,414 B free (+181), ppilot 8,743 -> 8,944 (+201); bench total 695,804 -> 696,133 (+0.05%; runway 130841 approach 123030 cruise 150595 town 152846 lake 138821, worst cruise +0.38% vs baseline). |
| 21 | 2026-10-17 | 1783 | 1742 | -6.6% | merged | argon-loop, a separate cycle-only loop forked from 63e2950, merged by hand: world_render_grid and gfx_project_and_draw, 8-bit loops in mem/box, vec_transform_inv, _poly_scan_lines2 and _clip_near, panel lamps, clouds_add_candidates, the render.cc fills, roll period computed instead of kRollPeriod, view camera, mission logic, vec_project/vec_frac_setup sharing in vec_asm.cc, sound and msg cleanups. It includes pass 7's row-pointer XOR, made safe by starting mem_screen_row_ptrs out filled with the main buffer's rows. Its single-pass map draw lost the map's bottom row and was reverted to two passes. fpilot 12,414 -> 14,197 B free (+1,783), ppilot 8,944 -> 10,686 (+1,742); bench total 696,133 -> 650,277 (-6.6%; runway 122384 approach 116327 cruise 138732 town 141890 lake 130944). |
| 22 | 2026-10-17 | 56 | 56 | +0.0% | kept | menu_run()'s key loop called oscar64's key_pressed() on a variable scan code (kMenuKeys[i]), which linked the 56 B bitshift table again after the argon-loop merge (pass 12 had removed every other use); it now calls keys.h's keys_pressed(), which reads kBit[] instead. Same keys, same result. fpilot 14,197 -> 14,253 B free (+56), ppilot 10,686 -> 10,742 (+56); bench unchanged in every scene (runway 122384 approach 116327 cruise 138732 town 141890 lake 130944, total 650277). |
| 23 | 2026-10-17 | 16 | 16 | +0.1% | kept | mem.h MEM_BOOT_END $0A70 -> $0A60: box.cc's kBootScratch layout needs exactly 512 B (its static_assert floor, kMaxBoxTotalSize 96 is real) and the largest boot code is ppilota's at $0A3E, so the 16 B of unusable boot headroom go back to the main region; fpilot 14,253 -> 14,269 B free (+16), ppilot 10,742 -> 10,758 (+16); bench total 650,277 -> 650,649 (+0.06%, layout shift only; runway 122314 approach 116361 cruise 138952 town 141991 lake 131031, all inside +2%). |
| 24 | 2026-10-17 | 130 | 130 | +0.1% | kept | oscar64's keyb_poll() (174 B) also decodes the last new key into keyb_key, which nothing here reads; keys.cc's new 51 B keys_poll() fills keyb_matrix the same way (port-1 joystick guard kept) and replaces it in sim.cc, menu.cc, help.cc and keys_wait_release(). fpilot 14,269 -> 14,399 B free (+130), ppilot 10,758 -> 10,888 (+130); bench total 650,649 -> 651,443 (+0.12%, layout shift only, polling is outside the render window; runway 122262 approach 116727 cruise 139043 town 142279 lake 131132, worst approach +0.34% vs baseline). |
| 25 | 2026-10-17 | 54 | 51 | -0.1% | kept | mem_init() no longer calls oscar64's mmap_trampoline(), which linked IRQTrampoline/NMITrampoline/DoneTrampoline (53 B in main); their KERNAL handlers jump via $0314/$0318, which bss2 overlays, so they could only crash. $fffa and $fffe now point at a 1-byte rti (_mem_ignore_irq, in main), and _gfx_isr still replaces $fffe in gfx_init_raster_irqs(); RESTORE is now ignored instead of jumping into bss2. fpilot 14,399 -> 14,453 B free (+54), ppilot 10,888 -> 10,939 (+51), boot headroom 42 -> 46 B; bench total 651,443 -> 650,738 (-0.11%, layout shift; runway 122306 approach 116436 cruise 138889 town 142044 lake 131063, all inside +2%). |
| 26 | 2026-10-17 | 68 | 68 | +0.1% | kept | world_map.cc's kWorldObjX/Y[9][6] (108 B, 4 of the 9 types repeat another's outline) become flat kWorldObjX/Y[23] holding the 5 distinct outlines plus kWorldObjFirstVert[9]; each vertex now stores the byte offset (n * sizeof(vec3_t)) of its _world_dx4/_world_dy4 entry, so _world_render_object() reads them abs,y and indexes the tables directly instead of multiplying by 6 through a pointer. Same vertices, bit-identical polygons. fpilot 14,453 -> 14,521 B free (+68), ppilot 10,939 -> 11,007 (+68); bench total 650,738 -> 651,157 (+0.06%, layout shift; runway 122650 approach 116438 cruise 138914 town 142066 lake 131089, worst runway +0.22% vs baseline). |
