# Running on a CMD SuperCPU

Everything here was measured in `xscpu64`, VICE's SuperCPU emulator, against
`x64sc` for the stock machine. [emulator.md](emulator.md) has the harness;
`xscpu64` needs the SuperCPU's own 64 KB `scpu64` ROM in
`/usr/share/vice/SCPU64/` on top of the usual three.

## It already runs, and at 50 fps

| | stock C64 | SuperCPU | |
| --- | ---: | ---: | ---: |
| frame period | 140,355 µs (7.1 PAL frames) | 19,657 µs (1 frame) | |
| frame rate | 7.0 fps | **50.1 fps** | |
| render work | 129,841 | 6,626 | 19.6× |
| polygons | 44,132 | 2,226 | 19.8× |
| clouds | 13,824 | 788 | 17.5× |

Runway pose, the heaviest of the three in [framerate.md](framerate.md). The
render uses **34% of a PAL frame**, so the frame rate is not something to go and
win — it is there the moment it boots, and two thirds of the frame is spare.

Three compatibility classes were checked against the binary rather than assumed:
no illegal opcodes (the 65816 has none, and this is the usual killer), every I/O
store inside the documented VIC registers `$D000`–`$D02E` with nothing in the
mirror range the SuperCPU's own registers live in, and no KERNAL calls or ROM
timing to break, since both ROMs are already banked out.

## What was wrong: one raster line

The cycle-counted panel switch in `gfx.cc` — nineteen NOPs and three writes that
have to land in horizontal blanking — takes a twentieth of the time at 20 MHz,
so the writes land one line early. Diffing the two emulators' screenshots at the
same frozen pose, the entire difference is **320 pixels on row 146**, and two
SuperCPU frames taken at different times are byte-identical, so it is a static
offset rather than a flicker. It reads as the grey line under the terrain
turning black.

**Fixed.** The handler drops to 1 MHz for its own duration — `$D07A` on entry,
`$D07B` on exit — which is not "run slower": it is ~19 µs either way, so against
leaving it fast it costs about 18 µs of 19,656, **0.09% of the frame**, and the
padding stays in the environment it was flown in.

VICE emulates the speed registers, which is worth knowing because the fix would
otherwise be untestable here. Timing the probe loop across a switch: 1,003 µs at
default, 5,868 µs after `$D07A`, 1,123 µs after `$D07B` — a stock C64 reads
~5,800 for the same loop, so the slow mode really is 1 MHz.

That alone was not enough. The switch happens *inside* the handler, but the
interrupt entry and oscar64's `rirq` dispatcher ahead of it still run at 20 MHz,
so the handler starts earlier in the line by however long that preamble takes.
The padding has to make it up, and how much was found by sweeping it and
diffing frames against a stock C64:

| `GFX_PANEL_NOPS` | C64 | SuperCPU |
| ---: | --- | --- |
| 16 (before) | identical | row 146 wrong |
| 17 | identical | row 146 wrong |
| 18 | identical | identical |
| **19** | **identical** | **identical** |
| 20 | identical | identical |
| 21 | row 147 wrong | identical |

**19** was taken as the middle of the joint window, with a NOP of margin either
side. That is where this section stood, and it was wrong about the margin.

## What was wrong: a NOP of margin is not a margin

A stock C64 tore the top of the panel about **one frame in 250** — a single
raster line of character-mode colour across raster 163, gone the next frame,
which reads as an occasional flicker at the split and is what
[clouds.md](clouds.md) §1.8's sprite fix left behind.

The joint window above is not three NOPs wide because the switch is tolerant.
It is three NOPs wide because two ten-NOP windows barely overlap, and 19 was
against the C64's own late edge. Counting torn frames rather than looking at one
screenshot puts numbers on it: at 19 the C64 tears one frame in 250, at 20 two
frames in twelve, at 21 ten in twelve. There was never a NOP of margin on that
side; there was a NOP of *overlap*, and the interrupt's entry jitter — six
cycles, with no stabiliser under `rirq` — spends it.

Two changes, and neither is a re-tune:

**The write block was made shorter.** All three writes share one deadline, and
it is not "the horizontal blanking" in general: raster 163 is the panel's first
line and a badline, so the VIC starts fetching the row at cycle 15. `$D018`'s
video matrix, `$D011`'s BMM bit and `$D021` all have to be in before that.
Three `lda`/`sta` pairs spread that deadline over eighteen cycles; three loads
followed by three stores spread it over twelve. Six cycles of window, for no
bytes and no branch.

**The count became two counts.** Swept on the split itself, with the shorter
block:

| | window | middle |
| --- | :---: | ---: |
| stock C64 (`x64sc`) | 8 – 17 | **12** |
| SuperCPU (`xscpu64`) | 15 – 24 | **19** |

Ten NOPs each, overlapping by three. `gfx_init_raster_irqs()` installs
`_gfx_switch_to_panel_top` or `_gfx_switch_to_panel_top_fast` — same handler,
different `#assign` count, one shared `__noinline` tail with the writes in it —
according to `cpu_step_shift`, which the boot probe already sets. One
`rirq_call`, no run-time branch inside a cycle-counted handler, and each machine
gets four NOPs of margin below and five above instead of one.

This is exactly the escape hatch the previous section ended with: *"If the two
machines ever need different values, the way out is a data-driven delay count
set from the probe at init."* It turned out to be a function pointer rather than
a delay count, which is cheaper still.

**Measured after.** 1,000 consecutive frames in the side view and 800 in the
centre view, each grabbed from the monitor at the terrain interrupt and checked
at rasters 162 and 163: **no torn frame, and in the centre view the whole split
band renders byte-identically every frame.** The frozen-pose frame is
**pixel-identical between `x64sc` and `xscpu64`** over the entire screen.

## What was wrong: the flight model flew 4.67× too fast

The model advances in fixed steps and had no elapsed time in it, so it used to
step once per *render* — and the aircraft covered the same ground per frame
however long the frame took. On a machine twenty times faster that is a
disaster, and on a stock C64 it was already a 13% airspeed swing between the
runway and cruise.

Fixed in [flight.md](flight.md) §8: the timebase is now the raster, and one knob
divides the step size and multiplies the step rate together. Distance flown over
the same four seconds of wall clock, from mission 02's start:

| build | C64 | SuperCPU | ratio |
| --- | ---: | ---: | ---: |
| once per render (before) | 96,122 | 448,602 | **4.67** |
| raster timebase, shift 0 | 69,480 | 72,200 | 1.04 |
| shift 2, 25 Hz | 71,838 | 73,229 | 1.02 |

`ppilot.prg` measures its own machine at boot and picks the shift, so there is
one binary and no SuperCPU build to remember to use.

> **2026-08-29.** Half of this was reverted. The step *size* still scales with
> `cpu_step_shift` exactly as described, but the raster timebase is gone and
> `sim.cc` is back to one `flight_advance()` per rendered frame - because the
> catch-up loop made the worst frame worse, owing the model a second step on
> precisely the frames that had already overrun. [flight.md](flight.md) §8 has
> the measurements on both sides. The consequence here is that the two halves
> of the shift no longer cancel: at 50.1 fps against a stock 7.0, a quarter-size
> step leaves the aircraft about 1.8x fast on a SuperCPU rather than 1.04x.
> Better than the 4.67x above and not as good as the row below it. Re-measuring
> that on real `xscpu64` is open.

## Detecting the machine

`cpu.cc` times a fixed RAM-only loop against CIA2 timer A, which stays on the
1 MHz bus whatever the CPU does. Measured, and repeatable to the microsecond
over three runs on each:

| | `cpu_probe_us` | `cpu_step_shift` |
| --- | ---: | ---: |
| x64sc, stock C64 | 10,676 | 0 |
| xscpu64, SuperCPU | 496 | 2 |

A 21× separation, with the C64 sitting twice above the first band edge and the
SuperCPU five times inside the last. The probe costs about 11 ms, once, at boot.

Measuring beats asking. The register that would answer — `$D0BC` bit 7 — is
documented with **opposite polarities** by the two main SuperCPU references, so
it cannot be used without hardware to check it against. A measurement also
generalises free to a Turbo Master, a Chameleon, or an emulator in warp mode,
none of which would have set a SuperCPU's register.

The loop's accumulator is `volatile`, which is doing two jobs: it stops the
optimiser folding the loop into a closed form and leaving nothing to time, and
it makes every iteration a real read-modify-write in RAM. That second part is
what makes the number mean something on an accelerator, where the CPU runs from
its own fast SRAM but writes are mirrored back to the C64's DRAM at bus speed —
a register-only loop would report the CPU clock rather than the speed this
program will actually see.

`kCpuReferenceUs` in `cpu.h` is the one number that has to be re-measured if the
loop's code generation ever changes. The bands are a halving apart, so it does
not have to be exact.

## Notes for later

- **Leave the mirroring optimisation registers alone** (`$D074`–`$D076`). The
  char RAM, both screen buffers and every sprite block live at `$C000`–`$FFFF`,
  so the VIC needs the default full mirroring (`$D077`) to see any of it.
- **One binary serves both machines.** `ppilot.prg` measures at boot and scales
  the model to match. It cost 295 bytes and 558 cycles a model step on the stock
  C64 — 0.35% of the machine — and the same binary run on both emulators lands
  on positions identical to the pinned builds it replaced. flight.md §8 has the numbers.
- `$D0B8` bit 6 reads back the current speed setting, if that is ever wanted.

Sources: [Programming in a SuperCPU compatible way](http://supercpu.cbm8bit.com/comp.htm),
[SuperCPU Tutorial — Part 3](http://supercpu.cbm8bit.com/scpu/scpu_3.html)
