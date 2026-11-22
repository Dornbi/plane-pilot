# Frame Time & Benchmark Figures

Exact per-frame cycle counts measured in cycle-exact `x64sc` (PAL, 19,705 cycles per 50 Hz raster frame). See [emulator.md](emulator.md) for the harness (`tools/bench.sh`) and [development.md](development.md) for the live `D` debug counters in `ppilot.prg`.

## Current `fpilot` timings (`tools/bench.sh`)

`tools/bench.sh` measures one frame's work (`bm_model_start()` through `bm_total()` in `sim_run()`: model step + full render, excluding the vsync flip wait) across ten deterministic `fpilot` scenes — five frozen (averaged over 16 frames) and five live combat scenes (averaged over the frames when opponents are near and redrawn).

Snapshot as of pass 152 ([ram-loop.md](ram-loop.md)):

| Scene | Type | Cycles / frame | ~PAL frames | ~FPS (work-bound) |
| --- | --- | ---: | ---: | ---: |
| `start` (01 Recon, airborne) | frozen | 86,410 | 4.4 | ~10.0 |
| `approach` (final to runway) | frozen | 105,701 | 5.4 | ~8.3 |
| `cruise` (level flight, clouds) | frozen | 113,148 | 5.7 | ~8.3 |
| `town` (over town polygons) | frozen | 100,576 | 5.1 | ~8.3 |
| `lake` (over lake polygons) | frozen | 103,074 | 5.2 | ~8.3 |
| `scramble` (04 pair pass + guns) | live | 153,235 | 7.8 | ~6.2 |
| `shootdown` (04 scripted 5 hits + fall) | live | 148,865 | 7.6 | ~6.2 |
| `headon` (05 hunters closing ahead) | live | 93,221 | 4.7 | ~10.0 |
| `chase` (05 hunters behind, back view) | live | 117,568 | 5.9 | ~8.3 |
| `strafe` (04 pair strafing run) | live | 150,562 | 7.6 | ~6.2 |
| **Total (10 scenes)** | | **1,172,360** | | |

(For comparison, the 2026-11-01 pre-optimisation baseline total was 1,548,482 cycles; 152 passes cut frame cost by 18–30% across scenes.)

### Where a frame goes (historical ablation)

An earlier August 2026 ablation of `ppilot` (`CLOUDS=`, `SOUND=`, polygon disable) showed the structural balance that still holds:

- **Polygons (`PLY` inside `GRD`)** are the single largest variable cost when a large runway or lake fills the near foreground (up to ~35–50% of `GRD`).
- **Terrain dots & horizon draw (`GRD`, `DRW`, `BGR`, `COL`)** form the steady baseline.
- **Clouds (`CLD` inside `UPD`)** scale with visible cloud count (~11k–28k cycles; see [clouds.md](clouds.md) §5).
- **Opponents & combat (`fpilot`)** add ~35k–50k cycles on live close passes (`scramble`, `strafe`) when two silhouettes are projected, filled, and smoking.
- **Sound (`sound_update` + `sound_blit`)** costs only ~1,500–2,000 cycles (< 2% of a frame).

## Reading the numbers

1. **Frame rate is quantised to whole PAL frames** (50/4 = 12.5, 50/5 = 10.0, 50/6 = 8.33, 50/7 = 7.14, 50/8 = 6.25 fps). Because `mem_switch_buffer()` waits for the next raster split, `FPS = 50 / ceil(work / 19705)`. A 5,000-cycle saving changes FPS only when it crosses a 19,705-cycle boundary; compare **cycle counts**, not FPS, when profiling.
2. **Stage counters include VIC DMA steals.** Enabling sprites adds DMA cycles on the scanlines they occupy, so turning clouds or opponents off lowers `BGR`/`DRW`/`GRD` slightly alongside `UPD`.
3. **Rolling raises `CHR`.** On a frozen pose `box_prepare()` takes its early return (~400 cycles); while rolling, copying new horizon characters costs ~10,000–15,000 cycles.
