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

**19**, the middle of the joint window, with a NOP of margin either side. One
constant, one code path, no run-time branch in a cycle-counted handler. The
three values that work differ by about 0.4% of the render at the runway pose and
nothing measurable at cruise, so the choice is margin rather than cycles.

The window is three NOPs wide, so real hardware may want a different number —
`GFX_PANEL_NOPS` is a `#define` for that reason, and a photograph of the split
settles it rather than an argument. If the two machines ever need different
values, the way out is a data-driven delay count set from the probe at init: a
constant-shape loop in the handler rather than a branch.

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
