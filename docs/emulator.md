# Running the build in an emulator

`x64sc` runs the real `.prg` cycle-exactly and headlessly, which turns a class
of arguments into measurements. Three things it has settled that reasoning had
got wrong: whether clearing `$D015` can stop sprite DMA already in flight
(§1.8 of [clouds.md](clouds.md) — it cannot, and the first answer it gave was
"it can", from a pose that did not contain the case), whether an oscar64 fix reached the
real code ([bugs/](../bugs/) — a pixel-identical screenshot said
yes), and what the far-group collapse actually buys (§3.5 — one PAL frame).

Reach for it when the question is *how many cycles*, *is this frame identical*,
or *what does the VIC really do*. Do not reach for it to see whether the clouds
look nice; that is what `tools/render_cloud_preview.py` is for, and it answers
in a second.

## Setup

```bash
brew install vice                       # macOS
sudo apt-get install vice xvfb          # Linux; xvfb only if headless
```

Both scripts run `x64sc` directly when there is a display and wrap it in
`xvfb-run` when there is not, so they work the same either way.

The official macOS and Windows builds ship the C64 ROMs. Debian and Ubuntu do
not — their `vice` package leaves them out, and `vice-roms` is in non-free. Put
`kernal-901227-03.bin`, `basic-901226-01.bin` and `chargen-901225-01.bin` in
`/usr/share/vice/C64/` yourself. Without them every screenshot comes out black
and nothing says why.

## A screenshot

```bash
tools/vice_shot.sh c64o/ppilot.prg out/shot.png [cycles]
```

`cycles` is emulated cycles before the frame is grabbed, default 40,000,000 —
about 40 s of C64 time, which warp mode gets through in a couple of seconds of
real time. One PAL frame is 19,656 cycles and one second is 985,248.

The useful part is not looking at the picture, it is `cmp`:

```bash
tools/vice_shot.sh build_a/ppilot.prg out/a.png
tools/vice_shot.sh build_b/ppilot.prg out/b.png
cmp out/a.png out/b.png && echo pixel-identical
```

Two builds that render identically produce byte-identical PNGs. That is how a
refactor gets shown to be a refactor.

Two flags in the script are load bearing. `-autostartprgmode 1` injects the PRG
straight into RAM; the default wraps it in a disk image first, which fails with
no 1541 ROM present and leaves you with a black screenshot and no error.
`-jamaction 1` makes a jammed CPU continue rather than sit in a dialog forever.

## A number

Screenshots cannot tell you what a routine costs. For that, park the program on
a known instruction and read a global out of memory:

```bash
tools/vice_dump.sh c64o/ppilot.prg @spin:world_update_objects g_frame_cycles 4
# break $3d07  $02fa: 51 66 02 00
#   le32 = 157265
```

The break point and the address may each be a hex address or a symbol from the
`.lbl` oscar64 writes beside the `.prg`. `@spin` finds the first instruction
that branches to itself; `@spin:func` finds it inside one function's listing.
Both listings come from `-g`, which `c64o/Makefile` always passes.

The other half is on the C64 side, and it is three small things:

```c
volatile uint32_t g_frame_cycles;   // volatile, or oscar64 deletes it
volatile uint16_t g_frame_count;
static uint32_t _frame_last;

void world_update_objects() {
  {
    if (g_frame_count == 0) {
      // bm_init() only exists in debug builds, so start the timers by hand.
      cia2.cra = 0; cia2.crb = 0;
      cia2.ta = 0xffff; cia2.tb = 0xffff;
      cia2.crb = 0x41;              // timer B counts timer A underflows
      cia2.cra = 0x11;
    }
    const uint16_t fa = cia2.ta;
    const uint16_t fb = cia2.tb;
    const uint32_t now = (((uint32_t)fb << 16) | fa);
    g_frame_cycles = _frame_last - now;
    _frame_last = now;
    if (++g_frame_count == 60) {
      for (;;) {                    // something for @spin to break on
      }
    }
  }
  ...
```

Wrap the same pair of reads around a single call to cost that call instead of
the whole frame. The timers count down, so the delta is `before - after`.

Three ways this goes wrong, all of which cost an hour once:

- **A non-`volatile` global is deleted.** It is only ever written, so oscar64
  drops it and the symbol never reaches the `.lbl`. If `vice_dump.sh` says a
  symbol is missing, this is why.
- **`bm_init()` is a no-op outside debug builds**, so in `ppilot.prg` the CIA2
  timers were never started and every reading comes back 0.
- **Frame times quantise.** The main loop waits for the raster, so a frame is a
  whole number of PAL frames — 137,593 reads as 7.00, 157,246 as 8.00. A build
  that lands on 8.81 is alternating between 8 and 9.

## A repeatable scene

Both scripts need the same frame every run, and the menu and the flight model
are both in the way. Two patches, in a scratch copy of `c64o/` rather than in
the tree:

```c
// ppilot.cc - skip the menu. The menu scans the key matrix directly, so
// VICE's -keybuf (which fills the BASIC buffer) cannot answer it.
uint8_t selected_mission = 0;

// flight.cc, inside flight_init_from_mission() - freeze a known pose.
#ifdef __DIAG_POSE__
  flight_eye_x = 0x8D5000;
  flight_eye_y = 0x2C0000;
  flight_eye_z = 0x057800;   // 24.8 fixed metres; >> 9 gives 2 m units
  flight_speed = 0;
  flight_throttle = 0;
  flight_paused = true;
  return;
#endif
```

then build with `-D__DIAG_POSE__` added to `CFLAGS`. Letting the aircraft fly
instead is rarely worth it: with no input it stalls into the terrain within a
couple of seconds and every later frame is the crash message.

To exercise a code path the pose does not reach, edit the path rather than hunt
for a pose. Forcing the rung ladder to pick a nearer rung
(`(centre.x >> 2) <= kCloudRungDepth[rung + 1]`) puts rung-8 clouds in front of
the camera at any distance — that is what showed the oscar64 fix reached the
real code. Beware of forcing the *result* instead: setting `rung = 7` outright
made the compiler fold the branch under test, and the broken build and the
fixed one drew the same picture.

## The screenshots in screens/

`tools/make_shots.sh` regenerates every picture in `screens/` from the current
sources in about half a minute:

```bash
tools/make_shots.sh              # all of them
tools/make_shots.sh help map     # two scenes
```

It is the repeatable scene above, generalised. Instead of patching a pose into
`flight.cc` it copies `c64o/` to a scratch directory, drops `tools/shot.cc` in
beside it, and points the keyboard poll of the three screen loops - the menu,
the help screen and the flight loop - at a scripted one that clears the matrix
bits of whatever key the scene says is held. That is the only way in: the game
scans the matrix directly, so `-keybuf` cannot reach it. The scenes are in
`tools/shot.cc` and the table at the top of `make_shots.sh` says when each
frame is grabbed and which file it lands in.

Three things that are load bearing, each of which cost an hour once:

- **`keys_wait_release()` keeps the plain `keyb_poll()`.** A scripted press is
  one poll long and releases itself. Scripted through the wait loops as well,
  `M` opens the map and the key still being down closes it on the next pass.
- **The freeze is not the P key.** A pseudo-key sets `flight_paused` directly,
  so the frame holds still for the capture with no PAUSED banner across the
  viewport, and the capture point stops being delicate.
- **oscar64 dies with a bare SIGABRT when the path to the sources is long.** It
  builds an absolute path in a fixed stack buffer while parsing
  `#pragma compile()` and smashes it; the crash report says `__stack_chk_fail`
  inside `CompilationUnits::AddUnit()`. The scratch directory is
  `/tmp/ppilot-shots` for that reason rather than for taste.

The CRT look is `tools/shot_crt.py`, not VICE. `-VICIIfilter` changes what is
on the emulator window and nothing at all in the file - screenshots are written
from the native frame buffer, before the video chain runs. So the tube gets
built afterwards, on a 5x upscale of the cropped frame: horizontal bleed in
source pixels for the PAL colour bandwidth, scanlines, and a bloom pass without
which the scanlines just read as a dark grid.

Five of the seven scenes come back byte-identical run after run; the menu and
the debug view do not. Emulation is deterministic, but the moment VICE's
autostart injects the program is not, and a few frames of offset move the title
flyby along its path and change the numbers in the cycle counters. That is
exactly what the freeze buys the other five: everything after it is the same
frame however the run started. Neither of the two can be frozen - a still
flyby is not a flyby, and a frozen model reports a model cost of nothing - so
they are checked by looking, and `cmp` is not the tool for them.

One more thing worth knowing: VICE's autostart occasionally does not take, and
what lands in `screens/` then is a perfectly valid PNG of the BASIC prompt.
`make_shots.sh` checks the corner pixel for it - every screen in the game draws
a black border and BASIC draws a light blue one - and retries.

## The debug panel

When you can press keys, `D` switches the instrument panel to the built-in
counters (`MDL`, `SNP`, `BGR`, `CHR`, `DRW`, `GRD`, `COL`, `UPD` and `TOT`,
plus `PLY`, `CLD` and `SPR` breaking out the polygons, the cloud scan and the
sprite stack — see
[development.md](development.md)), which is faster than any of the above. It
is in `ppilot.prg` itself but needs a human to press the key. Headless, force
`mem_debug_enabled = true` in `mem_init()`, or use the timer recipe, which does
not depend on the counters at all.
