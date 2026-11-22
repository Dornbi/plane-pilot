# Running Headless in VICE (`x64sc`)

Automated tests, screenshots, and cycle benchmarks run the real `.prg` binaries in cycle-exact `x64sc` (VICE 3.x) without human input.

## Setup

Install VICE so `x64sc` is on `PATH` (on macOS, add `/Applications/vice-x86_64-gtk3-*/bin` or `/Applications/vice-arm64-gtk3-*/bin`; on headless Linux, `xvfb-run` wraps `x64sc` automatically when `DISPLAY` is unset).

All scripts pass `-default` so `~/.config/vice/vicerc` never leaks user settings into test runs, plus `-sounddev dummy -warp -keybuf ""`.

### Avoiding focus steal on macOS

`make prg` launches `x64sc` twice on a passing `target_test` (five times on failure), and `make_shots.sh` / `bench.sh` launch it once per scene. On macOS, `x64sc` activates to the foreground on launch; `tools/vice_nofocus.sh` (which compiles `tools/vice_nofocus.m` once into `out/vice_nofocus.dylib` and injects it via `DYLD_INSERT_LIBRARIES`) suppresses activation so the terminal keeps focus.

## Reading C64 memory: `tools/vice_dump.sh`

`vice_dump.sh` runs a `.prg` in `x64sc`, breaks on an instruction via `-moncommands`, prints a global's bytes with `m`, and quits:

```bash
tools/vice_dump.sh <prg> <break-addr> <dump-start> <dump-end> [hits]
```

Typical workflow:
1. Build with `-g` (`c64o/Makefile` already passes `-g`) so oscar64 emits `.lbl` / `.asm` / `.map`.
2. Look up the break address (e.g. a function entry or a `nop` marker) and the variable's address in `.lbl` or `.map`.
3. Pass `hits` (default 1) to skip earlier iterations (`ignore 1 <hits-1>`).

### Timing caveats

- **`x64sc` is cycle-exact** (unlike `x64`, which is line-accurate); always use `x64sc` for timing and raster-split checks.
- **CIA2 timer init:** `bm_init()` starts CIA2 timers A+B as a chained 32-bit countdown. It is active in `ppilot.prg` (built with `-D__ENABLE_DEBUG__`), and a no-op in `fpilot.prg` or `DEBUG=` builds — custom probes (`tools/bench.cc`, `c64o/proto/cycles_probe.cc`) call `bm_init()` directly or compile with `-D__ENABLE_DEBUG__`.
- **Two frames minimum:** Measure after at least one warm-up frame so `box_prepare()` and `COL` (which lags `TOT` by one frame) are in steady state.

## On-target tests: `tools/run_target_test.sh`

`make -C c64o test` compiles [c64o/test/target_test.cc](../c64o/test/target_test.cc) with oscar64 and runs it in `x64sc`:
- On pass, `vice_dump.sh` runs **twice** (reading `g_cases` and `g_failures` at `target_done`).
- On failure, it runs three more times to read the failing test ID, actual value, and expected value.
- If `oscar64` or `x64sc` is missing, the script skips cleanly so the 16 host C++ suites still run.

## Deterministic screenshots: `tools/vice_shot.sh` and `tools/make_shots.sh`

`vice_shot.sh` drives `x64sc` with `-limitcycles` and `-exitscreenshot` (always exit cleanly — never `kill` VICE mid-frame).

`tools/make_shots.sh` (`make shots`) builds a special instrumented binary (`tools/shot.cc` + `tools/shot_bake.cc`) that jumps straight to a named scene, lets it settle for a fixed cycle count, and captures a raw PNG that `tools/shot_crt.py` processes into `screens/*.png`.

- Captures **15 scenes**: 11 `ppilot` scenes (title/menu, help, map, debug, and 7 flight views/schemes) and 4 `fpilot` combat scenes (`ffront`, `fside`, `frear`, `fdebrief`).
- **13 of 15 scenes are byte-identical** across repeated runs; `menu` (title flyby timing) and `debug` (live cycle counters) vary from run to run.
- Because oscar64 aborts on very long paths, `make_shots.sh` compiles in a short temp directory (`SHOT_BUILD_DIR`, default `/tmp/ppilot-shots`).
- Use `SHOT_SCHEME=<0..3>` in `tools/shot.cc` to preview a colour scheme on any flight pose.

## Cycle benchmarks: `tools/bench.sh`

`tools/bench.sh` builds `fpilot` with `tools/bench.cc` (which pairs `shot.cc`'s deterministic poses and scripted combat inputs with CIA2 `bm_*` accumulation) and dumps total cycles for **10 scenes**:
- Five frozen (`start`, `approach`, `cruise`, `town`, `lake`), 16 frames each.
- Five live combat (`scramble`, `shootdown`, `headon`, `chase`, `strafe`), timed across the active engagement frames.

Options:
- `tools/bench.sh <scene>...` — time a subset of scenes.
- `BENCH_SHOTS=<dir> tools/bench.sh` — also save each scene's rendered PNG (used by `tools/ram_loop.sh` for visual regression checks).

## Live debug panel

For interactive cycle profiling on a running C64 or emulator, press `D` in `ppilot.prg`. See [development.md](development.md#debug-view-d-plane-pilot-only) for the counter table and caveats.
