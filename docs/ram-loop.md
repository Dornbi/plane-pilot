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

cycles-baseline: runway=131082 approach=122756 cruise=150020 town=152709 lake=138509

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
