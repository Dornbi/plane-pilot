# Running the build in an emulator

`x64sc` runs the real `.prg` cycle-exactly and headlessly, which turns a class
of arguments into measurements. Three things it has settled that reasoning had
got wrong: whether clearing `$D015` can stop sprite DMA already in flight
(§1.8 of [clouds.md](clouds.md) — it can), whether an oscar64 fix reached the
real code ([oscar64-bug/](oscar64-bug/) — a pixel-identical screenshot said
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

## The debug panel

When you can press keys, `D` switches the instrument panel to the built-in
counters (`SNP`, `BGR`, `DRW`, `MDL`, `GRD` — see
[development.md](development.md)), which is faster than any of the above. It
needs `ppilotd.prg` and a human. Headless, force `mem_debug_enabled = true` in
`mem_init()`, or use the timer recipe, which does not depend on the debug build
at all.
