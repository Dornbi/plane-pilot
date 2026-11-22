# SuperCPU Support

Both `ppilot.prg` and `fpilot.prg` run on a stock 1 MHz C64 and on a 20 MHz SuperCPU (`xscpu64`), targeting full 50 fps PAL on the accelerator without tearing the raster split.

## Boot-time CPU probe (`c64o/cpu.cc`)

`cpu_probe()` runs once in `_boot()` before `mem_init()` (using CIA2 timer A/B and a fixed dummy workload, keeping the result in registers until after `mem_init()` to respect the ROM-window rule in [memory_map.md](memory_map.md)):

- Reference stock C64 time: `kCpuReferenceUs = 10700` µs (PAL).
- Sets `cpu_fast` (`true` when faster than stock) and `cpu_step_shift` in `0..kCpuMaxStepShift` (`0` on stock C64, `2` on 20 MHz SuperCPU).
- Displayed on `ppilot`'s `D` debug view as `CPU` (probe µs) and `CSHIFT`.

## Raster split timing (`c64o/gfx.cc`)

The viewport/panel split fires on raster line 161 (panel top), switching from MCCM to MCBM mid-line during horizontal blank. On a 20 MHz SuperCPU, two hardware facts break a stock 1 MHz handler:

1. **Speed:** At 20 MHz, a 12-NOP delay finishes before the scanline reaches horizontal blank, tearing the top of the panel.
2. **I/O mirror writes:** VIC writes run at 1 MHz regardless of CPU speed, while CPU instructions between them run at 20 MHz, shifting where the `$D011`/`$D018`/`$D021` writes land on the scanline.

### Solution: 1 MHz mode + split-specific NOP pads

1. On a fast CPU (`cpu_fast`), the panel-top handler drops to 1 MHz (`$D07A = 0`) on entry and restores 20 MHz (`$D07B = 0`) on exit.
2. Because a single NOP count cannot sit cleanly inside the horizontal blank window on both machines (IRQ latency + `$D07A` write timing differ by ~14 cycles), `gfx.cc` provides two tiny entry stubs sharing one `_gfx_panel_top_writes()` body:
   - **Stock C64 (`_gfx_switch_to_panel_top`):** **12 NOPs** before writing VIC registers.
   - **SuperCPU (`_gfx_switch_to_panel_top_fast`):** switches to 1 MHz, **19 NOPs**, writes VIC registers, restores 20 MHz.
3. `_gfx_panel_top_writes()` loads all three new register values before storing them (`load-load-load / store-store-store`), tightening the actual VIC write burst to 6 cycles.
4. Verified over 1,800 consecutive frames on both `x64sc` and `xscpu64` with zero torn scanlines.

## Flight model speed scaling (`c64o/flight.cc`)

At 20 MHz, a full frame finishes well inside one 19,705-cycle PAL frame (50 fps — even the August 2026 unoptimised runway pose took ~7,500–13,000 SuperCPU-scaled cycles; after passes 1–152 it has even more margin). Running one full stock-C64 step per frame at 50 fps would run the simulation ~4.7× too fast.

To compensate without reintroducing a variable catch-up loop (see [flight.md](flight.md) §8):

- After `mem_init()`, `ppilot.cc` calls `flight_set_step_shift(cpu_step_shift)`.
- On SuperCPU (`shift = 2`), per-step integrations scale down by `1 >> 2` (`1/4` step via `flight_step_s()` and `vec_set_rotation_shift(2)`), and non-divisible terms (fuel burn, ground friction, stall break floor, map path sampling) gate on `whole_step = (model_substep == 0)` every 4 frames.
- Because the simulation runs one substep per rendered frame (50 substeps/s = 12.5 whole steps/s) instead of the stock C64's ~7–10 whole steps/s, SuperCPU plays smoothly at 50 fps and roughly **1.3–1.8×** stock C64 wall-clock speed (down from 4.67×).

## Known limits

- `cpu_step_shift` is capped at `2` (`1/4` step) because further right-shifts lose precision in 8.8 fixed-point thrust/drag terms.
- Title music (`music_tick()` via vsync) already runs at 50 Hz PAL on both machines and needs no SuperCPU adjustment.
- Sound effects (`sound_update()`) run once per rendered frame; at 50 fps, frame-counted voice-3 one-shots (e.g. gear/flap clicks) are shorter than at 8–10 fps.
