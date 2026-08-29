# Where the frame goes

A one-off measurement, taken on **18 August 2026** against the source at that
date (clouds with the far-group collapse of [clouds.md](clouds.md) §3.5 in, built
with oscar64 `v1.32.272-117-ga7305f9`). It answers one question — what do the
polygons and the clouds cost — and it is written down so the answer does not
have to be guessed at again.

> **2026-08-29.** The two numbers this document went looking for are now
> counters in the debug view: `PLY` is the polygon total and `CLD` the cloud
> scan, alongside `SPR` for the sprite stack the "stage counters are calls, not
> consequences" note below calls out. So the poses no longer have to be rebuilt
> to ask this question - press `D` and read it. The figures agree with the ones
> below: 43,128 against 44,132 for the runway polygon, 11,760 against 13,824
> for clouds at the same pose, the difference being that the debug view turns
> the sprites off and the frozen pose is not bit-identical.

**This is a snapshot, not a regression suite.** Nothing re-runs it, nothing
fails if the numbers move, and there is no reason to repeat it after an ordinary
change. Re-measure when the shape of the render changes — a new per-frame stage,
a rework of `poly.cc` or the grid walk, a different sprite budget — or when a
specific decision needs a number, which is the only reason it was taken in the
first place. [emulator.md](emulator.md) has the method; it is about twenty
minutes of work.

## The poses

Frame cost depends far more on what is on screen than on anything else, so three
frozen poses rather than one, each with the aircraft paused:

| name | what | how |
| --- | --- | --- |
| **runway** | parked at the start of mission 01, runway filling the lower viewport | `selected_mission = 0` |
| **approach** | mission 02's start, 512 m up on final | `selected_mission = 1` |
| **cruise** | 1,400 m, at the cloud deck, terrain and clouds and two distant objects | eye `0x8D5000, 0x2C0000, 0x057800` |

## What it costs

Per stage, from CIA2 timers around the calls — unquantised, so these are the
honest cycle counts. "Render work" is `view_update_cam()` through
`sound_update()`, i.e. everything `bm_total()` covers.

| pose | render work | clouds | polygons | clouds % | polys % | together |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| runway | 129,841 | 13,824 | 44,132 | 10.6% | 34.0% | **44.6%** |
| approach | 100,365 | 15,367 | 17,426 | 15.3% | 17.4% | **32.7%** |
| cruise | 145,663 | 28,283 | 30,042 | 19.4% | 20.6% | **40.0%** |

`clouds` is `clouds_add_candidates()`. `polygons` is every `poly_draw_3d()` call
in the grid walk, summed over the frame.

## What it is worth

The same three poses built four ways — both features, each one removed
(`CLOUDS=` and a `#ifndef __NO_POLY__` around `_world_render_object()`), and
neither:

| pose | with both | with neither | |
| --- | ---: | ---: | ---: |
| runway | 7.12 frames, 7.04 fps | 4.17 frames, 12.02 fps | +5.0 fps |
| approach | 6.00, 8.35 fps | 4.08, 12.29 fps | +3.9 fps |
| cruise | 8.00, 6.27 fps | 5.00, 10.02 fps | +3.8 fps |

**Between them, about three PAL frames** — the difference between roughly 7 fps
and roughly 12 fps.

## The one thing worth acting on

**A single polygon can cost more than everything else in the frame.** On the
runway there is exactly one on screen and it takes **44,132 cycles, 2.25 PAL
frames**. The same routine at cruise draws two for 15,021 cycles each.
`poly_draw_3d()` fills, so its cost tracks the area covered, and the worst case
is the moment the frame rate matters most: on final with the runway filling the
viewport.

Clouds are both cheaper and much steadier — 14k to 28k across every pose, and
the cruise figure was 36,756 before §3.5 collapsed far groups to one blob. If
there is a second round of that kind of work to do, it is in `poly.cc`, not in
`clouds.cc`.

## Reading the numbers

- **Frame period quantises, stage counters do not.** The main loop waits for the
  raster, so a frame is a whole number of PAL frames and the ablation column
  rounds: removing clouds at cruise reads as 19,645 cycles against the counter's
  28,283. Use the stage counters for what something *costs* and the ablation for
  what you would *feel*.
- **The stage counters are calls, not consequences.** `clouds` excludes the
  cycles the VIC steals for sprite DMA and the extra work
  `sprites_stack_commit()` does with a fuller stack. Both are small; neither is
  zero.
- **One frame per pose**, with the aircraft frozen, 60 frames in. There is no
  averaging and no variance figure here, which is fine for 30%-sized effects and
  would not be for 3%-sized ones.
