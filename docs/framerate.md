# Frame Time & Benchmark Figures

Exact per-frame cycle counts measured in cycle-exact `x64sc` (PAL, 985,248 Hz, 19,656 cycles per 50.125 Hz raster frame). See [emulator.md](emulator.md) for the harness (`tools/bench.sh`) and [development.md](development.md) for the live `D` debug counters in `ppilot.prg`.

## Current `fpilot` timings (`tools/bench.sh`)

`tools/bench.sh` measures full wall-clock cycles per frame using a continuous CIA2 32-bit timer across complete iterations of `sim_run()` — input poll, model step, render, buffer flip and `_copy_color_ram()`, any remaining flip-window wait, raster IRQs, and VIC DMA — across ten deterministic `fpilot` scenes: five frozen (averaged over 16 frames) and five live combat scenes (averaged over the frames when opponents are near and redrawn).

Snapshot after the deferred/checkpointed buffer flip (post-pass 164):

| Scene | Type | Wall cycles / frame | PAL frames | Effective FPS |
| --- | --- | ---: | ---: | ---: |
| `start` (01 Recon, airborne) | frozen | 81,766 | 4.16 | 12.0 |
| `approach` (final to runway) | frozen | 106,456 | 5.42 | 9.3 |
| `cruise` (level flight, clouds) | frozen | 109,402 | 5.57 | 9.0 |
| `town` (over town polygons) | frozen | 98,863 | 5.03 | 10.0 |
| `lake` (over lake polygons) | frozen | 108,107 | 5.50 | 9.1 |
| `scramble` (04 pair pass + guns) | live | 136,620 | 6.95 | 7.2 |
| `shootdown` (04 scripted 5 hits + fall) | live | 133,501 | 6.79 | 7.4 |
| `headon` (05 hunters closing ahead) | live | 87,625 | 4.46 | 11.2 |
| `chase` (05 hunters behind, back view) | live | 104,359 | 5.31 | 9.4 |
| `strafe` (04 pair strafing run) | live | 134,500 | 6.84 | 7.3 |
| **Total (10 scenes)** | | **1,101,199** | | |

*(Before the deferred/checkpointed flip, the same continuous wall-clock measurement totaled **1,160,901** cycles, including **70,128** cycles spent spinning in `gfx_wait_flip_window()`. Deferring the flip into the next frame's buffer-independent stages cut spin-wait to **10,085** cycles overall — and from **35,028** to **1,182** cycles (−96.6%) across the five live combat scenes — saving **59,702** wall-clock cycles (−5.14%). Earlier snapshots in [ram-loop.md](ram-loop.md) timed only `bm_model_start()`..`bm_total()`, excluding input polling, `_copy_color_ram()`, and flip-window waits.)*

### Where a frame goes (historical ablation)

An earlier August 2026 ablation of `ppilot` (`CLOUDS=`, `SOUND=`, polygon disable) showed the structural balance that still holds:

- **Polygons (`PLY` inside `GRD`)** are the single largest variable cost when a large runway or lake fills the near foreground (up to ~35–50% of `GRD`).
- **Terrain dots & horizon draw (`GRD`, `DRW`, `BGR`, `COL`)** form the steady baseline.
- **Clouds (`CLD` inside `UPD`)** scale with visible cloud count (~11k–28k cycles; see [clouds.md](clouds.md) §5).
- **Opponents & combat (`fpilot`)** add ~35k–50k cycles on live close passes (`scramble`, `strafe`) when two silhouettes are projected, filled, and smoking.
- **Sound (`sound_update` + `sound_blit`)** costs only ~1,500–2,000 cycles (< 2% of a frame).

## Reading the numbers

1. **Unquantised average frame rate via deferred flip.** `mem_request_flip()` flips immediately if the raster is inside the safe flip window (rasters 162–242) and otherwise sets `mem_flip_pending` and immediately begins the next frame's buffer-independent stages (input, model, camera/roll, panel instruments, sound, cloud/opponent/gun sprite stack, horizon snap, and grid setup), calling `mem_try_flip()` checkpoints to catch the window as soon as the beam enters it and `mem_finish_flip()` before writing the back buffer. Individual frames still present on a 50 Hz refresh boundary without tearing, but average frame time tracks true CPU work rather than rounding each frame up to `ceil(work / 19656)`.
2. **Stage counters include VIC DMA steals.** Enabling sprites adds DMA cycles on the scanlines they occupy, so turning clouds or opponents off lowers `BGR`/`DRW`/`GRD` slightly alongside `UPD`.
3. **Rolling raises `CHR`.** On a frozen pose `box_prepare()` takes its early return (~400 cycles); while rolling, copying new horizon characters costs ~10,000–15,000 cycles.
