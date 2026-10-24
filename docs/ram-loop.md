# RAM and cycle optimisation loop

`tools/ram_loop.sh` runs an unattended loop that frees RAM in fpilot and saves
cycles. RAM and cycles matter equally, and fpilot is the only target: ppilot
and ppilota only have to keep building and passing their tests. Each pass starts a fresh Claude agent,
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
- **Cycles:** `tools/bench.sh` gives a frame's work (`bm_model_start()`
  through `bm_total()` in `sim_run()`: the model step and the render, all but
  the wait for the flip) in seven fpilot scenes:
  - Five frozen, timed over 16 frames: start (01 Recon's start, airborne),
    approach, cruise, town and lake.
  - **scramble**, live: mission 04 with both opponents overtaking the parked
    aeroplane and the guns firing, timed over frames 48-175. It is the only
    scene where the opponents' and the rounds' model steps and the
    opponents' silhouette fills cost anything: frozen, they stand still and
    a silhouette is filled once.
  - **shootdown**, live: scramble with the first opponent hit three times on
    its pass and downed (the hits scripted, at frames ~77, 101 and 125), so
    the smoke and the hit and downing logic are timed too. Opponents do not
    fire yet; when they do, a scene for their rounds belongs here.

  The emulator is cycle-exact, frozen scenes hold still, and the live ones'
  models step once a frame however long the frame takes, so the numbers are
  deterministic. Only code changes move them. A change to the flight or
  opponent model can change what the live scene shows, and so its cost.

A change is kept only when all of these hold:

- fpilot free RAM does not drop.
- **Every scene stays within +2%** of the fixed baseline below. The baseline
  does not move as passes are kept, so small slowdowns cannot add up past 2%.
- It gains something: more free RAM, or at least 0.5% fewer total cycles than
  the last kept state.

ppilot and ppilota must still build and pass their tests. Their free RAM is
logged in the table but is not a goal or a gate.

The script reads this line, so keep its format exactly:

cycles-baseline: start=119463 approach=144228 cruise=152154 town=144007 lake=133350 scramble=184492 shootdown=179998

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
   - fpilot's opponents keep their silhouettes, sizes and colours, and the
     tracers and smoke still show. The bench frames (`bench_*.png`) are the
     fpilot pictures to check; `make_shots.sh` shows ppilot.
2. **The flight model must behave similarly.** Loops, climbs, turns, takeoff
   and landing must all still work and feel the same. Small numeric
   differences are fine if every one of these stays supported.
   - **Combat behaves the same:** the opponents fly their paths, the guns
     fire, rounds hit and stop at the ground, hits smoke and three bring an
     aircraft down, and the missions complete and fail as before.
   - Changing an expectation in `flight_test`, `opponent_test`, `gun_test`,
     `mission_test` or `fpilot_test` is a red flag. Do it only for a
     tolerance-sized shift, and give the reason in the summary. Never delete
     a test or weaken a test so that it no longer checks the behaviour.
3. **Cycles count as much as RAM.** Spend the 2% cycle budget only for a
   RAM gain that is worth it, and prefer savings that are speed-neutral or
   faster. Pure speed-ups are welcome too. Do not un-inline anything in the
   per-point or per-edge hot paths (grid loop, `vec_project`, `vec_add`, the
   poly fill loops). The bench times the model step only in the live scramble
   scene, and the sound not as such. Do not slow down the flight model, the
   opponents, the guns or the sound either, where no scene shows it.
4. **Keep every feature.** The debug view is already gone from fpilot. Nothing
   else may be dropped: sound, music, the map, missions, clouds, the
   opponents, the guns, tracers and smoke, menu and help.
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
- `if ((uint8_t)(u + 16) >= 32)` compiles to `u >= 16`: the cast's 8-bit
  wraparound is dropped. Write the range test without relying on it.
- A loop that passes its counter to a function taking it in A can leave the
  loop's byte offset in A from the second iteration on (gun.cc's
  `_gun_track()` works around it). Spell the calls out, or check the asm.
- fpilot is nearly full (about 1.4 KB free in October 2026), and
  `tools/bench.sh` links more code into it than the game does. A change that
  frees RAM gives the bench room too; one that grows code can stop the bench
  scenes linking even when fpilot.prg itself still does.

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
- Orphan fragments: `$EBE8`, `$EFE8` (16 B each) and `$FFC0-$FFF9` (58 B).
  They can only be reached by hand-placing data.
- Used up: main-screen rows 14-24 (pass 16, now the `scratch` region) and the
  stack headroom (pass 5).
- New since October 2026: the opponents (`opponent.cc`, `planes*.cc`) and the
  guns (`gun.cc`) have not been through the loop yet.

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

The cycles baseline was reset again on 2026-10-24 at 1787679, after fpilot
gained its opponents and guns, when the bench grew the live scramble and
shootdown scenes and began timing the model step too: fpilot 1,437 B free
(largest run 970 B). Cycles per frame: start 119,463, approach 144,228,
cruise 152,154, town 144,007, lake 133,350, scramble 184,492, shootdown
179,998; total 1,057,692. The runway
scene became "start": in fpilot it is 01 Recon's airborne start.

The script appends one row per pass. The branch column names the loop the
change came from: claude (claude-loop) or argon (argon-loop).
The fpilot and ppilot columns are the change in free allocatable bytes. The
cycles column is the change in bench total against the last kept state.

Rows 21.a-21.j break pass 21 down into argon-loop's ten commits. Each was
rebuilt and measured with this loop's tools/bench.sh and analyze_ram.py.
Their columns are the change against the argon commit before it (21.a
against 63e2950), not against the last kept pass. On its own, argon-loop
took fpilot 9,546 -> 11,587 B free (+2,041) and the bench 695,076 -> 648,182
(-6.7%). That is more than row 21's +1,783 because three of its ideas (21.c's
fmath pragma, 21.e's volume mix, 21.f's map row) had already landed as passes
2, 1 and 13.

| Pass | Date | Branch | fpilot Δ | ppilot Δ | Cycles Δ | Result | Idea |
|-----:|------|--------|---------:|---------:|---------:|--------|------|
| 1 | 2026-10-17 | claude | 45 | 45 | +0.3% | kept | Replaced the 48 B kMusicVolumeMix[3][16] table in music.cc with its three closed forms (0, v>>1, v) in music_master_volume() (codesize.md §3.4): fpilot 9,546 -> 9,591 B free (+45), ppilot 6,466 -> 6,511 (+45); bench total 695,076 -> 697,254 (+0.31%, layout shift only — music is not in the render path; worst scene approach +0.51%, all well inside +2%). |
| 2 | 2026-10-17 | claude | 45 | 45 | -0.2% | kept | Dropped the file-wide nooutline pragma from fmath.cc (codesize.md §3.1; _get_ratio runs at most 3x a frame): fpilot 9,591 -> 9,636 B free (+45), ppilot 6,511 -> 6,556 (+45); bench total 697,254 -> 696,176 (-0.15%; runway 130920 approach 123093 cruise 150529 town 152914 lake 138720, all inside +2%). |
| 3 | 2026-10-17 | claude | 125 | 125 | +0.4% | kept | kSpriteDefLongArm/ShortArm now hold 16 pivots (2 B each) instead of 32 {pivot, bitmap} rows: direction d+16 reuses bitmap d with the pivot mirrored through (12,10), and the bitmap is base + (d & 15); _sprites_roll_to_dir became a 30-entry _sprites_roll_to_bitmap (it repeats every half-turn), and _sprites_set_instrument_sprite went __noinline (inlined, the mirror cost 70 B at six sites). fpilot 9,636 -> 9,761 B free (+125), ppilot 6,556 -> 6,681 (+125). Bench total 696,176 -> 698,756 (+0.37%; runway 131234 approach 123741 cruise 150794 town 153646 lake 139341, worst approach +0.80% vs baseline). The inline variant measured +55 B at +0.03%; runway got faster under the out-of-line build, so part of the +0.37% is layout shift. |
| 4 | 2026-10-17 | claude | 38 | 41 | -0.1% | kept | Wrapped _limit_vec/vec_normalize/vec_cross/vec_orthonormalize in vec.cc in #pragma optimize(push, outline) (codesize.md §3.2; flight model only, once per model step, never in the render): fpilot 9,761 -> 9,799 B free (+38), ppilot 6,681 -> 6,722 (+41); bench total 698,756 -> 698,083 (-0.10%; runway 131118 approach 123614 cruise 150700 town 153458 lake 139193, all inside +2%). |
| 5 | 2026-10-17 | claude | 64 | 64 | +0.0% | kept | SP is set at startup and read nowhere (no dynamic stack use), so mem.h's stack region shrinks $0200-$0280 -> $0200-$0240 (stacksize 0x80 -> 0x40) and bss2 starts at $0240: fpilot 9,799 -> 9,863 B free (+64), ppilot 6,722 -> 6,786 (+64); bench total 698,083 -> 698,137 (+0.01%, layout only; runway 131351 approach 123549 cruise 150698 town 153411 lake 139128, all inside +2%). |
| 6 | 2026-10-17 | claude | 43 | 40 | -0.1% | kept | Dropped the unsigned twin of the flight step shift (codesize.md §3.3); the drag square, the only argument that can reach 0x8000 (0xE100 at kMaxSpeed), is halved before the call and its three shifts are one less, so drag stays bit-identical: fpilot 9,863 -> 9,906 B free (+43), ppilot 6,786 -> 6,826 (+40); bench total 698,137 -> 697,669 (-0.07%, layout only; runway 131356 approach 123331 cruise 150796 town 153163 lake 139023, all inside +2%). |
| 7 | 2026-10-17 | claude | 96 | 96 | -0.0% | rejected by review: XOR flip assumes the table was set up first; polydemo never calls mem_use_main_buffer(), so its row pointers end up at $0000 and it draws into zero page | mem.cc drops kScreenRowPtrsMain/Alt (2 x 50 B): mem_use_main_buffer() fills mem_screen_row_ptrs with a +40 loop and mem_switch_buffer() XORs each high byte with $04 (the buffers are 1 KB apart, static_asserted); fpilot 9,906 -> 10,002 B free (+96), ppilot 6,826 -> 6,922 (+96); bench total 697,669 -> 697,527 (-0.02%; runway 131531 approach 123200 cruise 150721 town 153133 lake 138942, all inside +2%). The per-frame flip is 25 x 18 cycles instead of a 50 x 14 copy, outside the bench window. |
| 8 | 2026-10-17 | claude | 132 | 132 | +0.1% | kept | roll.cc keeps 30 of the 60 rows of kRollDx/kRollDy/kRollDxDivDy/kRollPeriod; angle a+30 is angle a with (dx, dy) negated (same slope and period, checked for all 30 pairs), so roll_update_state() folds it with one compare: fpilot 9,906 -> 10,038 B free (+132), ppilot 6,826 -> 6,958 (+132); bench total 697,669 -> 698,294 (+0.09%, layout shift; runway 131343 approach 123671 cruise 150824 town 153410 lake 139046, worst approach +0.75% vs baseline). |
| 9 | 2026-10-17 | claude | 55 | 58 | -0.1% | kept | fmath.cc drops kRollAngleLut[65]; it is exactly min(ratio >> 2, 15) for all 65 ratios (checked), so _get_roll_angle() shifts and clamps 16 to 15. Bit-identical. fpilot 10,038 -> 10,093 B free (+55), ppilot 6,958 -> 7,016 (+58); bench total 698,294 -> 697,884 (-0.06%; runway 131312 approach 123441 cruise 150783 town 153300 lake 139048, all inside +2%). |
| 10 | 2026-10-17 | claude | 25 | 35 | -0.0% | kept | fmath.cc drops kHeadingLut[65]; it is exactly ratio / 5 for all 65 ratios, which _get_heading() now computes as (ratio * 51 + 12) >> 8 with 51 = 3 * 17 in shifts and adds (no runtime multiply; checked bit-identical for every ratio). fpilot 10,093 -> 10,118 B free (+25), ppilot 7,016 -> 7,051 (+35); bench total 697,884 -> 697,618 (-0.04%; runway 131122 approach 123285 cruise 150866 town 153212 lake 139133, all inside +2%). The flight step pays about 30 cycles per call, twice per step. |
| 11 | 2026-10-17 | claude | 201 | 195 | -0.3% | kept | The flight model's six 18 B kVec* control matrices and vec_transform3() (27 multiplies plus an 18 B transposed copy per step) are replaced by vec_rotate_step(): each step only turns two rows of the camera matrix towards each other (roll left/up, pitch front/up, yaw front/left), so it does p += s*q, q -= s*p with 6 multiplies and a 6-byte int8 angle table that vec_set_rotation_shift() rescales. Bit-identical (same three fastmul identities as vec_turn3_xy, plus commutativity of the magnitude product). fpilot 10,118 -> 10,319 B free (+201), ppilot 7,051 -> 7,246 (+195); bench total 697,618 -> 695,811 (-0.26%, render untouched so layout only; runway 130755 approach 122929 cruise 150687 town 152712 lake 138728, all inside +2%). The flight step also gets cheaper: flight.md §8 measured 6,191 cycles a frame for the 27-multiply path while a key is held. |
| 12 | 2026-10-17 | claude | 49 | 49 | +0.1% | kept | Every shift by a variable amount (sim.cc's three key-table loops and keys_wait_release via key_pressed, the fin-row 0x80 >> n in sprites.cc, map_set_overlay_pixel's 1/3 << shift, and the boot-time 8 >> / 1 << / 32 >> shift in flight_set_step_shift and vec_set_rotation_shift) linked oscar64's 56 B bitshift table (and __shltab3L, 7 B); they now read fmath.cc's kBit[8] (new keys_pressed() in keys.h), use bit / bit << 1, or loop over the shift (boot code, shift 0..2). All bit-identical. fpilot 10,319 -> 10,368 B free (+49), ppilot 7,246 -> 7,295 (+49); bench total 695,811 -> 696,305 (+0.07%, layout only, no render code touched; runway 130906 approach 123053 cruise 150736 town 152763 lake 138847, all inside +2%). |
| 13 | 2026-10-17 | claude | 30 | 30 | -0.6% | kept | world_render_grid() looked up kWorldMap[cx & 15][cy & 31] for every cell, which rebuilt the row address through oscar64's 32 B __multab32L/H tables on each inner step; the row pointer is now set once per x row as kWorldMap[0] + ((uint16_t)(cx & 15) << 5), so the tables are no longer linked. Same addresses, bit-identical output. fpilot 10,368 -> 10,398 B free (+30), ppilot 7,295 -> 7,325 (+30); bench total 696,305 -> 691,912 (-0.63%; runway 130342 approach 122230 cruise 149393 town 151881 lake 138066, all faster). |
| 14 | 2026-10-17 | claude | 165 | 165 | -0.5% | kept | The lead starts notes only on even rows, so generate_music.py now emits kMusicLeadStartPat[20][8] (even rows only, the premise is asserted) and MUSIC_LEAD_START_IN reads odd rows as 0 via (r & 1) ? 0 : pat[r >> 1]; the tune is bit-identical. fpilot 10,398 -> 10,563 B free (+165), ppilot 7,325 -> 7,490 (+165); bench total 691,912 -> 688,792 (-0.45%, layout shift only since the player is not in the render path; runway 130481 approach 121672 cruise 148458 town 150405 lake 137776, all inside +2%). |
| 15 | 2026-10-17 | claude | 117 | 120 | +0.4% | kept | Every bass note is its bar's chord root + 0/7/10/12, so generate_music.py now emits kMusicBassStepPat[3][16] (interval + 1, 0 = no new note; the 11 note patterns collapse to 3 rhythm shapes) in place of kMusicBassStartPat[11][16], and music_tick() tests the step for the hard restart and adds MUSIC_CHORD(bar).root only when a note starts (new MUSIC_BASS_STEP_IN / MUSIC_BASS_NOTE; MUSIC_BASS_START_IN keeps its meaning for music_test). The tune is bit-identical. fpilot 10,563 -> 10,680 B free (+117), ppilot 7,490 -> 7,610 (+120); bench total 688,792 -> 691,692 (+0.42%, layout shift only since the player is not in the render path; runway 130345 approach 122276 cruise 149171 town 151871 lake 138029, all inside +2%). |
| 16 | 2026-10-17 | claude | 420 | 0 | -0.0% | kept | New fpilot-only linker region "scratch" at $EA30-$EBE7 (main screen rows 14-24, never fetched in flight: the panel split writes $d018 on line 162, before row 14's badline at 163, and the panel colors come from the alt screen) now holds poly.cc's per-call buffers (_min_x/_max_x, clip3_buf, proj_buf, clip2_buf1/2 still adjacent and in order, final_verts) and world.cc's per-frame _world_dx4/_world_dy4/poly_verts, 420 of 440 B; ppilot (debug view uses those rows) keeps them in bss2. fpilot 10,680 -> 11,100 B free (+420), ppilot 7,610 unchanged; bench total 691,692 -> 691,680 (runway 130337 approach 122272 cruise 149171 town 151871 lake 138029, cycle-neutral). |
| 17 | 2026-10-17 | claude | 95 | 95 | +0.0% | kept | boxdefs.cc's alt_boxes[60] (120 B of pointers, 52 of them NULL) becomes alt_box_angles[8] plus alt_boxes[8], and boxdef_set_alt() searches the 8 angles, falling back to boxdef_set_main() (which keeps the kRollMax check). The same tiles are picked, so the output is bit-identical. fpilot 11,100 -> 11,195 B free (+95), ppilot 7,610 -> 7,705 (+95); bench total 691,680 -> 691,787 (+0.02%; runway 130374 approach 122328 cruise 149100 town 151996 lake 137989, all inside +2%). |
| 18 | 2026-10-17 | claude | 817 | 817 | +0.0% | kept | chardefs[224][8] (1,792 B) becomes chardef_bytes[728], a greedy shortest common superstring in which every stored char is 8 consecutive bytes (one char's bottom rows are often another's top rows), plus chardef_offset_lo[224]; the generator numbers the chars in the order they start in the stream, so the offset grows with the index and box_prepare() adds 256 at kCharDefPage1 and again at kCharDefPage2 (two compares). Box output is bit-identical. fpilot 11,195 -> 12,012 B free (+817), ppilot 7,705 -> 8,522 (+817); bench total 691,787 -> 692,080 (+0.04%; runway 130298 approach 122319 cruise 149322 town 152108 lake 138033, all inside +2%). |
| 19 | 2026-10-17 | claude | 221 | 221 | +0.5% | kept | The 9 MAP_OBJ_* types move from 16..24 to 7..15, so every map type fits a nibble and kWorldMap[16][32] (512 B) becomes kWorldMap[16][16] (256 B, even column in the low nibble), read through new world.h helpers world_map_row()/world_map_cell() in world_render_grid, map.cc, flight.cc, polydemo and map_test; world_map.cc keeps one entry per cell via a WORLD_MAP_ROW/WORLD_MAP_PAIR macro. fpilot 12,012 -> 12,233 B free (+221), ppilot 8,522 -> 8,743 (+221); bench total 692,080 -> 695,804 (+0.54%, ~13 cycles per grid cell for the nibble select; runway 130910 approach 122998 cruise 150487 town 152631 lake 138778, worst cruise +0.31% vs baseline). |
| 20 | 2026-10-17 | claude | 181 | 201 | +0.0% | kept | boxdef_t loses total_size (= w * h) and the box_chars pointer: find_boxes.py now emits one box_*_map[] per box (char_count chardefs indices, then the w * h cells), so box_prepare() reads the cells at char_idx + char_count and sums w * h with an add loop (h <= 11, slow path only, no multiply linked); the per-frame boxdef copy shrinks 14 -> 11 B. Box output is bit-identical (generator verifier passes). fpilot 12,233 -> 12,414 B free (+181), ppilot 8,743 -> 8,944 (+201); bench total 695,804 -> 696,133 (+0.05%; runway 130841 approach 123030 cruise 150595 town 152846 lake 138821, worst cruise +0.38% vs baseline). |
| 21 | 2026-10-17 | argon | 1783 | 1742 | -6.6% | merged | argon-loop, a separate cycle-only loop forked from 63e2950, merged by hand: world_render_grid and gfx_project_and_draw, 8-bit loops in mem/box, vec_transform_inv, _poly_scan_lines2 and _clip_near, panel lamps, clouds_add_candidates, the render.cc fills, roll period computed instead of kRollPeriod, view camera, mission logic, vec_project/vec_frac_setup sharing in vec_asm.cc, sound and msg cleanups. It includes pass 7's row-pointer XOR, made safe by starting mem_screen_row_ptrs out filled with the main buffer's rows. Its single-pass map draw lost the map's bottom row and was reverted to two passes. fpilot 12,414 -> 14,197 B free (+1,783), ppilot 8,944 -> 10,686 (+1,742); bench total 696,133 -> 650,277 (-6.6%; runway 122384 approach 116327 cruise 138732 town 141890 lake 130944). |
| 21.a | 2026-10-17 | argon | 73 | 73 | -2.3% | merged | 3d908c7: gfx_project_and_draw() folds the ground and colour dot helpers into one bounds-checked path that computes the cell offset once; _world_draw_box_points() adds the mitch offsets straight into vec_v instead of copying and adding; world_render_grid() runs its radius and abs tests in 8 bits (_abs8/_max8). fpilot 9,546 -> 9,619 B free, ppilot 6,466 -> 6,539; bench total 695,076 -> 679,397 (runway 128145 approach 120704 cruise 144047 town 149958 lake 136543). |
| 21.b | 2026-10-17 | argon | 179 | 158 | -0.8% | merged | 1dad22d: mem.cc drops kScreenRowPtrsMain/Alt and flips the high bytes of mem_screen_row_ptrs with XOR $04 on every buffer switch (pass 7's idea; the merge made it safe by starting the table out on the main buffer's rows); gfx_frame_count goes; _copy_color_ram and box.cc's _out_of_bounds use 8-bit arithmetic. fpilot 9,619 -> 9,798 B free, ppilot 6,539 -> 6,697; bench total 679,397 -> 674,151 (runway 127030 approach 119827 cruise 143109 town 148421 lake 135764). |
| 21.c | 2026-10-17 | argon | 240 | 243 | -0.7% | merged | 9e76559: vec_transform_inv() loads u once and sums its three products in one expression, and vec_transform3_inv() transforms in place without the 18 B copy; an outline region covers vec.cc's cold attitude helpers (like pass 4); fmath.cc loses its nooutline pragma (as in pass 2); _clouds_build_basis() becomes a 3-axis loop and is skipped for collapsed groups. fpilot 9,798 -> 10,038 B free, ppilot 6,697 -> 6,940; bench total 674,151 -> 669,688 (runway 125603 approach 119004 cruise 142694 town 147086 lake 135301). |
| 21.d | 2026-10-17 | argon | 267 | 267 | -1.2% | merged | a774ecf: _poly_scan_lines2() drops the t_valid/b_valid flags (an untouched sub-row's min 80 / max 0 is neutral for _min8/_max8) and computes the solid core with unsigned 8-bit bounds; _clip_near() tests the range on the high byte (_mag_hi) and moves the edge intersection into a __noinline _clip_near_edge(). fpilot 10,038 -> 10,305 B free, ppilot 6,940 -> 7,207; bench total 669,688 -> 661,720 (runway 123947 approach 118297 cruise 142012 town 144189 lake 133275). |
| 21.e | 2026-10-17 | argon | 207 | 206 | +1.1% | merged | b7f5583: panel_update_instruments() always reuses roll_angle (only the centred view draws the roll dial) and runs under nooutline; the lamp, nav heading, stall, flap and gear writers are inlined; sprites_show_panel_bottom_sprites() uses counted-down loops instead of full unrolls; kMusicVolumeMix becomes its closed forms (as in pass 1). argon's 4-pose harness timed it 1,571 cycles faster; bench.sh's render-only window sees +1.1%, mostly layout. fpilot 10,305 -> 10,512 B free, ppilot 7,207 -> 7,413; bench total 661,720 -> 668,698 (runway 125244 approach 119218 cruise 143548 town 146289 lake 134399). |
| 21.f | 2026-10-17 | argon | 175 | 175 | -0.9% | merged | 6096306: _world_split_vec() walks the components with one pointer instead of indexed stores; radius 2 shares kNumPoints3; _world_render_object() sums vec_v, dx and dy per component instead of two vec_add() calls; world_render_grid() hoists the map row out of the inner loop (as in pass 13). fpilot 10,512 -> 10,687 B free, ppilot 7,413 -> 7,588; bench total 668,698 -> 662,395 (runway 124258 approach 118462 cruise 141537 town 144677 lake 133461). |
| 21.g | 2026-10-17 | argon | 106 | 111 | -0.4% | merged | 9ca09dc: clouds_add_candidates() scans with unsigned cell counters whose offsets come out biased by kCloudCullU, so the world-axis cull is one unsigned compare per axis, tests the vertical bound before the lateral one, and _clouds_add_step()'s three calls (and kCloudNoOffset) become one _clouds_apply_offset() loop over the pattern; sprites_stack_add() and _sprites_hidden_by_msg() need fewer compares. fpilot 10,687 -> 10,793 B free, ppilot 7,588 -> 7,699; bench total 662,395 -> 659,557 (runway 123694 approach 117889 cruise 140888 town 143845 lake 133241). |
| 21.h | 2026-10-17 | argon | 224 | 241 | -0.3% | merged | 06fd50f: render_snap_center_chars() keeps the best lattice position and converts it to chars once, after either path; render_fill_sky_ground() drops _fill_span() for _fill_full_row() plus direct loops for the partial rows, and clamps the horizon rows in 8 bits. fpilot 10,793 -> 11,017 B free, ppilot 7,699 -> 7,940; bench total 659,557 -> 657,446 (runway 123515 approach 117594 cruise 139848 town 143789 lake 132700). |
| 21.i | 2026-10-17 | argon | 258 | 288 | +0.1% | merged | 7130934: roll.cc computes the roll period instead of reading kRollPeriod; view.cc's camera setup is simplified; flight.cc's mission waypoint checks share a fall-through and flight_input() merges the yaw cases; map.cc's stencil and compass are tightened and its two draw passes become one. The single pass lost the map's bottom row, so the merge (475c69b) put pass B back and kept the rest. fpilot 11,017 -> 11,275 B free, ppilot 7,940 -> 8,228; bench total 657,446 -> 658,167 (runway 123686 approach 117835 cruise 139901 town 143861 lake 132884). |
| 21.j | 2026-10-17 | argon | 312 | 241 | -1.5% | merged | 3ec8bb2: vec_project() exits early on |y| > x before it computes |z|, and its multiply is shared as vec_project_mul; vec_fracn and vec_div8p8 share vec_frac_setup; _abs8/_abs16 test the sign; sound.cc computes the triangle PWM and keeps voice 3's tables in 8 bits; msg.cc stores an 8-bit duration; the menu keys become a loop and screen_notice() computes strlen itself. fpilot 11,275 -> 11,587 B free, ppilot 8,228 -> 8,469; bench total 658,167 -> 648,182 (runway 121758 approach 116298 cruise 137788 town 141941 lake 130397). |
| 22 | 2026-10-17 | claude | 56 | 56 | +0.0% | kept | menu_run()'s key loop called oscar64's key_pressed() on a variable scan code (kMenuKeys[i]), which linked the 56 B bitshift table again after the argon-loop merge (pass 12 had removed every other use); it now calls keys.h's keys_pressed(), which reads kBit[] instead. Same keys, same result. fpilot 14,197 -> 14,253 B free (+56), ppilot 10,686 -> 10,742 (+56); bench unchanged in every scene (runway 122384 approach 116327 cruise 138732 town 141890 lake 130944, total 650277). |
| 23 | 2026-10-17 | claude | 16 | 16 | +0.1% | kept | mem.h MEM_BOOT_END $0A70 -> $0A60: box.cc's kBootScratch layout needs exactly 512 B (its static_assert floor, kMaxBoxTotalSize 96 is real) and the largest boot code is ppilota's at $0A3E, so the 16 B of unusable boot headroom go back to the main region; fpilot 14,253 -> 14,269 B free (+16), ppilot 10,742 -> 10,758 (+16); bench total 650,277 -> 650,649 (+0.06%, layout shift only; runway 122314 approach 116361 cruise 138952 town 141991 lake 131031, all inside +2%). |
| 24 | 2026-10-17 | claude | 130 | 130 | +0.1% | kept | oscar64's keyb_poll() (174 B) also decodes the last new key into keyb_key, which nothing here reads; keys.cc's new 51 B keys_poll() fills keyb_matrix the same way (port-1 joystick guard kept) and replaces it in sim.cc, menu.cc, help.cc and keys_wait_release(). fpilot 14,269 -> 14,399 B free (+130), ppilot 10,758 -> 10,888 (+130); bench total 650,649 -> 651,443 (+0.12%, layout shift only, polling is outside the render window; runway 122262 approach 116727 cruise 139043 town 142279 lake 131132, worst approach +0.34% vs baseline). |
| 25 | 2026-10-17 | claude | 54 | 51 | -0.1% | kept | mem_init() no longer calls oscar64's mmap_trampoline(), which linked IRQTrampoline/NMITrampoline/DoneTrampoline (53 B in main); their KERNAL handlers jump via $0314/$0318, which bss2 overlays, so they could only crash. $fffa and $fffe now point at a 1-byte rti (_mem_ignore_irq, in main), and _gfx_isr still replaces $fffe in gfx_init_raster_irqs(); RESTORE is now ignored instead of jumping into bss2. fpilot 14,399 -> 14,453 B free (+54), ppilot 10,888 -> 10,939 (+51), boot headroom 42 -> 46 B; bench total 651,443 -> 650,738 (-0.11%, layout shift; runway 122306 approach 116436 cruise 138889 town 142044 lake 131063, all inside +2%). |
| 26 | 2026-10-17 | claude | 68 | 68 | +0.1% | kept | world_map.cc's kWorldObjX/Y[9][6] (108 B, 4 of the 9 types repeat another's outline) become flat kWorldObjX/Y[23] holding the 5 distinct outlines plus kWorldObjFirstVert[9]; each vertex now stores the byte offset (n * sizeof(vec3_t)) of its _world_dx4/_world_dy4 entry, so _world_render_object() reads them abs,y and indexes the tables directly instead of multiplying by 6 through a pointer. Same vertices, bit-identical polygons. fpilot 14,453 -> 14,521 B free (+68), ppilot 10,939 -> 11,007 (+68); bench total 650,738 -> 651,157 (+0.06%, layout shift; runway 122650 approach 116438 cruise 138914 town 142066 lake 131089, worst runway +0.22% vs baseline). |
| 27 | 2026-10-17 | claude | 7 | 7 | -0.3% | kept | poly.cc's edge tracer stepped x-major edges one pixel at a time (while err >= dy); it now splits dx = q * dy + r once per edge and moves q a row plus at most one carry step, and the rising/falling y-major loops and end-point marks are shared with a signed step. Bit-identical spans. fpilot 14,521 -> 14,528 B free (+7), ppilot 11,007 -> 11,014 (+7); bench total 651,157 -> 649,429 (-0.27%; runway 122141 approach 116483 cruise 138950 town 141288 lake 130567, all inside +2%). |
| 28 | 2026-10-24 | claude | 55 | 0 | -0.0% | kept | opponent.cc's (uint8_t)(1 << i) slot masks (9 sites, incl. _opponent_live() inlined into gun.cc) and _opponent_cap()'s 2 >> _opponent_nearer had linked oscar64's 56 B bitshift table back into fpilot (passes 12/22 had removed it); they now read fmath.h's kBit[i] and kBit[_opponent_nearer ^ 1] (_opponent_nearer is only ever 0 or 1). Bit-identical. fpilot 1,437 -> 1,492 B free (+55); bench total 1,057,692 -> 1,057,599 (-0.01%; start 119445 approach 144216 cruise 152141 town 144005 lake 133336 scramble 184468 shootdown 179988, every scene a little faster). |
| 29 | 2026-10-24 | claude | 68 | 0 | +0.2% | kept | opponent.cc's kOpponentPaths held x[4]/y[4] in every path (stride 32 B, 256 B in all, 14 of 32 waypoint slots unused, the parked and empty slots' all wasted); opponent_path_t now has a wp index into new striped kOpponentWpX/Y[18] pools (72 B) and is 16 B (128 B), and opponent_step() reads kOpponentWpX[p->wp + w]. Same waypoints, bit-identical paths. fpilot 1,492 -> 1,560 B free (+68); bench total 1,057,599 -> 1,060,228 (+0.25%, layout shift: the frozen scenes do not step the opponents; start 119621 approach 144673 cruise 152364 town 144346 lake 134140 scramble 184851 shootdown 180233, worst lake +0.59% vs baseline). |
| 30 | 2026-10-24 | claude | 70 | 0 | +0.3% | kept | opponent_reset() searched the mission's waypoints for WP_TARGETS_DOWN to find the targets' navpoint, but every fmission.cc mission has it first (navpoint 1, index 0, as flight.cc's TARGET/HOME BASE naming already assumes and test/fpilot_test.cc checks), so _opponent_nav, kOpponentNoNav and the search go and _opponent_track() writes flight_nav_point_x/y[kOpponentNav] at a constant address with no NoNav test. Same navpoint, bit-identical behaviour. fpilot 1,560 -> 1,630 B free (+70); bench total 1,060,228 -> 1,063,418 (+0.30%, layout shift: the frozen scenes do not run this code; start 119780 approach 145063 cruise 154001 town 144841 lake 134587 scramble 184667 shootdown 180479, worst cruise +1.21% vs baseline). |
| 31 | 2026-10-24 | claude | 42 | 0 | -0.3% | kept | planes.cc's kCols/kRows << (cols/rows/xs/ys - 1) (planes_project's wpx/hpx, planes_fill's height, _slide) and planes_asm.cc's 24 << (cols - 1) linked oscar64's __shltab24L, __shltab21L and __shltab21H (48 B); each count or expansion is only ever 1 or 2, so new planes.cc _twice_if(v, n) doubles v when n == 2, and _slide() tests ys once for both the cut and the height. Same values, bit-identical. fpilot 1,630 -> 1,672 B free (+42); bench total 1,063,418 -> 1,060,317 (-0.29%; start 119600 approach 144758 cruise 152419 town 144500 lake 134205 scramble 184459 shootdown 180376, every scene faster, all inside +2%). |
| 32 | 2026-10-24 | claude | 108 | 0 | +0.1% | kept | planes.cc's _poly_start[] (and kMaxPolys) was written each projection but never read (planes_fill uses kPlanePolyStart and key_poly_count), so it and its copy loop go; planes_project()'s dot tier repeated planes_state_init() and planes_place()'s dot branch line for line and now calls them (the only extra write, key_count = 0, is read only after a non-dot projection sets it). Same frames, bit-identical. fpilot 1,672 -> 1,780 B free (+108; dead array +32, dot tier +76); bench total 1,060,317 -> 1,061,168 (+0.08%, layout shift; start 119694 approach 144420 cruise 153584 town 144679 lake 134322 scramble 184391 shootdown 180078, worst cruise +0.94% vs baseline). |
| 33 | 2026-10-24 | claude | 63 | 0 | -0.2% | kept | opponent.cc's static __noinline _opponent_dot(a, b, c, d) (fastmul(a, b) + fastmul(c, d)) moves to vec.cc as vec_dot2, and gun.cc's _gun_fire (round start and velocity, 2 pairs) and gun_add_sprites (tracer dx/dy, written as dot2(a, b, -c, d), exact because vec_fastmul8p8 applies the sign last) call it instead of two inline vec_fastmul8p8 calls and an add. Bit-identical. fpilot 1,780 -> 1,843 B free (+63; _gun_fire 319 -> 279 B, gun_add_sprites 304 -> 294 B); bench total 1,061,168 -> 1,058,853 (-0.22%; start 119620 approach 143948 cruise 152099 town 144409 lake 133951 scramble 184500 shootdown 180326, worst lake +0.45% vs baseline). |
| 34 | 2026-10-24 | claude | 119 | 0 | +0.0% | kept | opponent.cc's kView, kViewTwo and kViewFar (3 x 11 B planes_view_t, differing only in kmax and kn) and the _opponent_view pointer chosen between them become one data-section _opponent_view whose kmax and kn new inline _opponent_set_view(far, two) writes per slot in _opponent_draw(); the planes_* calls now get a constant address, which also shrinks planes_project (only caller) and the planes_place proxy. Same views, bit-identical. fpilot 1,843 -> 1,962 B free (+119: _opponent_draw -57, planes_project -18, data -22, planes_place@proxy -8, stack frame -4, planes_centre -2); bench total 1,058,853 -> 1,059,165 (+0.03%; start 119559 approach 144170 cruise 153008 town 144501 lake 134329 scramble 183855 shootdown 179743, worst cruise +0.56% vs baseline). |
| 35 | 2026-10-24 | claude | 160 | 0 | +0.1% | kept | planes.cc's _same_axes() (six unrolled int16 compares, 122 B) and planes_project()'s six unrolled stores of the axes' screen components into state->axes (~92 B) become one 55 B _new_axes() that walks the 12 bytes, stores each one that differs and says whether any did; planes_project() calls it before the cache test (a hit leaves them as they were, and every miss path stored the same values later anyway). Bit-identical. fpilot 1,962 -> 2,122 B free (+160; planes_project 928 -> 835 B); bench total 1,059,165 -> 1,060,496 (+0.13%; start 119639 approach 145131 cruise 152466 town 144429 lake 133887 scramble 184607 shootdown 180337, worst approach +0.63% vs baseline, all inside +2%). |
| 36 | 2026-10-24 | claude | 114 | 0 | +0.2% | kept | planes.cc's _planes_lx/_planes_ly (64 B in main bss) kept the last projection's buffer-space vertices alive until the next frame's planes_fill(), which skipped copying them from the vertex-cache key when no other aircraft had been projected since (_planes_lx_state). They now live in tscratch with the other per-call arrays, and planes_fill() always copies them from state->key_x/key_y in one byte loop just before the fill, so planes_asm.cc's _key() drops its two stores into them (and the host _planes_key() likewise), and _planes_lx_state and its writes are gone. The fill reads the same bytes as before, so the output is bit-identical. fpilot 2,122 -> 2,236 B free (+114). Bench total 1,060,496 -> 1,062,775 (+0.21%; start 119596 approach 145317 cruise 153577 town 144675 lake 134401 scramble 184733 shootdown 180476; worst cruise +0.94% vs baseline, all inside +2%). The frozen scenes never run the fill, so their change is layout shift. In scramble and shootdown each fill now pays for the copy, and each projection saves 8 cycles a vertex. |
| 37 | 2026-10-24 | claude | 94 | 0 | +0.0% | kept | planes.cc's planes_project() wrote the frame's xs/ys/cols/rows, x, y and slide and called _slide() itself, repeating planes_place()'s non-dot path line for line; it now stores the layout with slid 0, calls planes_place() (whose max(need, 0) is need) and keeps its result as lay.slid, so _slide() has one caller and inlines there. Same values, bit-identical. fpilot 2,236 -> 2,330 B free (+94; planes_project 859 -> 750 B, planes_place + _slide 323 -> 332 B); bench total 1,062,775 -> 1,062,776 (start 119878 approach 144830 cruise 153745 town 144547 lake 134083 scramble 185142 shootdown 180551; worst cruise +1.05% vs baseline, all inside +2%). |
| 38 | 2026-10-25 | claude | 10 | 0 | -0.1% | kept | opponent.cc's opponent_step() tested its waypoint reach (/dx/, /dy/ < 512) and the steering shrink loop (/dx/, /dy/ > 63) with eight signed int16 compares; new inline _opponent_within(d, r) does each as (uint16_t)(d + r) <= 2r, so the shrink loop is now two add-and-compare tests per pass. Same results for every int16, bit-identical behaviour. fpilot 2,330 -> 2,340 B free (+10); bench total 1,062,776 -> 1,061,341 (-0.13%; start 119840 approach 144751 cruise 152377 town 144336 lake 134154 scramble 185291 shootdown 180592, all inside +2%). |
| 39 | 2026-10-25 | claude | 22 | 0 | +0.2% | kept | opponent.cc's _opponent_draw() picked the front set's first block twice (again after the fill's flip) and spelt out planes_centre() twice and planes_place() three times; the flip now comes first, so the block is picked once, the fill and the other slot's turn share one planes_centre() branch, and one planes_place() at the end serves all three paths (a `place` flag clears it for the projected dot, hidden and cached cases). Same calls in the same order, bit-identical. fpilot 2,340 -> 2,362 B free (+22); bench total 1,061,341 -> 1,062,952 (+0.15%; start 119879 approach 144722 cruise 153558 town 144386 lake 134121 scramble 185382 shootdown 180904; worst cruise +0.92% vs baseline, all inside +2%; the frozen scenes do not run this code, so their change is layout shift). |
| 40 | 2026-10-25 | claude | 39 | 48 | -0.1% | kept | sim_run()'s four spelt-out `if (key_pressed(KSCAN_1..4)) view_update_view(...)` blocks (~12 B each) become four kHeldKeys rows whose input carries a kHeldKeyView flag (0x80, static_asserted clear of every FLIGHT_INPUT_* and view_state_t), and the held-key loop calls view_update_view(input & ~flag) for them: fpilot 2,362 -> 2,401 B free (+39); bench total 1,062,952 -> 1,061,883 (-0.10%; start 119730 approach 144771 cruise 152796 town 144480 lake 134181 scramble 185187 shootdown 180738, all inside +2%; key polling is outside the timed window). |
| 41 | 2026-10-25 | claude | 31 | 6 | -0.1% | kept | sim.cc's R key repeated _enter_simulation()'s msg_clear, flight_init_from_mission, opponent_reset, gun_reset and msg_show(kMissionTitles[m]) line for line; both now call one __noinline _start_mission(m) in the outline region (same calls, same order): fpilot 2,401 -> 2,432 B free (+31); bench total 1,061,883 -> 1,060,420 (-0.14%, layout only; start 119733 approach 144623 cruise 152082 town 144155 lake 133800 scramble 185294 shootdown 180733, all inside +2%). |
