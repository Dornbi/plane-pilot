#ifndef CPU_H
#define CPU_H

#include <stdint.h>

// How fast this machine actually is, measured once at boot.
//
// The program is written for a 1 MHz 6510 and runs unchanged on a 20 MHz
// accelerator, where every cycle-counted or per-frame assumption in it is
// wrong by the same factor. Nothing in the C64 reports its own speed, and the
// one register that comes close - $D0BC bit 7 on a CMD SuperCPU - is
// documented with opposite polarities by the two main references, so this
// measures instead of asking. A measurement also generalises: a Turbo Master,
// a Chameleon and an emulator running warped all answer honestly, and none of
// them would have set a SuperCPU's register.
//
// See docs/supercpu.md for what the numbers were on real emulated hardware and
// what the result is for.

// Bus microseconds the probe loop took. Published for the debug panel and for
// anything that wants the raw figure rather than the verdict.
//
// volatile because nothing in a release build reads either of these yet, and
// oscar64 deletes a global that is only ever written - symbol and all, so it
// cannot even be read from a monitor. See docs/emulator.md. Once the flight
// model consumes cpu_step_shift the qualifier can come off that one.
extern volatile uint16_t cpu_probe_us;

// log2 of how much faster than a stock C64 this machine is, capped at
// kCpuMaxStepShift. 0 is a plain C64. Handed to flight_set_step_shift() at
// boot, which scales the model to match (docs/flight.md §8).
extern volatile uint8_t cpu_step_shift;

// Runs the probe and sets both of the above. Call once, early, before the
// raster interrupts are armed: it is timed, so a handler firing during it
// would be measured too. It leaves CIA2 timer A running and free of any
// configuration bm_init() cares about, which is why main() calls it first.
void cpu_probe(void);

// The verdict, split out from the measurement so it can be tested on the host.
// Pure: no hardware, no globals.
uint8_t cpu_shift_for_us(uint16_t us);

// A stock C64 takes about this long over the probe loop. Everything is a ratio
// against it, so it is the one number here that has to be re-measured if the
// loop's code generation ever changes - print cpu_probe_us on a 1 MHz machine
// and put it here. The bands either side are wide (a halving apart), so it
// does not have to be exact.
static const uint16_t kCpuReferenceUs = 10700;

// The fastest step the flight model has constants for (vec.h). A 20 MHz
// SuperCPU measures about six times a stock C64, so it would ask for two even
// without the cap; the cap is what stops an emulator in warp mode from asking
// for a step size that does not exist.
static const uint8_t kCpuMaxStepShift = 2;

// CMD SuperCPU speed control, for the one routine on the machine that is timed
// rather than merely fast (gfx.cc's panel switch). Write-sensitive: the value
// written is ignored, only the access matters.
//
// Unconditional, with no check that a SuperCPU is there at all. On a stock C64
// $D07A and $D07B decode to VIC registers that do not exist, so the writes land
// nowhere - verified rather than assumed, by building with and without them and
// diffing the frames: pixel-identical (docs/supercpu.md). That is worth more
// than a conditional would be, because a conditional inside a cycle-counted
// handler is a branch whose cost has to be counted too.
#ifdef __OSCAR64__
inline void cpu_speed_slow(void) { *((volatile uint8_t *)0xD07A) = 0; }
inline void cpu_speed_fast(void) { *((volatile uint8_t *)0xD07B) = 0; }
#else
// The host build maps the I/O area onto arrays, so a raw pointer would scribble
// on something real. Nothing on the host is timed.
inline void cpu_speed_slow(void) {}
inline void cpu_speed_fast(void) {}
#endif

#ifdef __OSCAR64__
#pragma compile("cpu.cc")
#endif

#endif
