#include "cpu.h"

#include "cia.h"

volatile uint16_t cpu_probe_us;
volatile uint8_t cpu_step_shift;

// How many times round the probe loop. 1000 is about 11.8 ms on a stock C64
// and 2.0 ms on a 20 MHz SuperCPU - far enough apart to be unambiguous, and
// far enough inside timer A's 65,536 us wrap that a machine slower than a C64
// (there is no such thing, but a future emulator quirk is free to be one)
// would still be read correctly rather than aliased.
static const uint16_t kProbeIterations = 1000;

// The loop's accumulator. Volatile is load bearing twice over: it stops the
// optimiser folding the loop into a closed form and leaving nothing to time,
// and it makes every iteration a real read-modify-write in RAM. That second
// part is what makes the measurement mean something on an accelerator, where
// the CPU runs from its own fast SRAM but writes are mirrored back to the
// C64's DRAM at bus speed. A register-only loop would report the CPU clock;
// this reports the speed the program will actually see.
static volatile uint8_t _probe_work;

uint8_t cpu_shift_for_us(uint16_t us) {
  // Guard the divide, and treat anything absurd as a plain C64 rather than
  // guessing: a zero would mean the timer never moved and a huge value would
  // mean it wrapped, and in both cases the safe answer is the one the program
  // was written for.
  if (us == 0 || us > kCpuReferenceUs + (kCpuReferenceUs >> 1)) {
    return 0;
  }

  // Every halving of the time is one more step the model can afford, which is
  // exactly what the shift means (docs/flight.md §8). Done as repeated
  // comparison rather than a divide: there is no 16-bit divide in this program
  // and this needs at most kCpuMaxStepShift iterations.
  uint8_t shift = 0;
  uint16_t threshold = kCpuReferenceUs >> 1;
  while (shift < kCpuMaxStepShift && us <= threshold) {
    ++shift;
    threshold >>= 1;
  }
  return shift;
}

void cpu_probe(void) {
  // Timer A free running off the 1 MHz bus clock, which is the one thing on
  // this machine that an accelerator does not speed up - that is the whole
  // basis of the measurement.
  cia2.cra = 0;
  cia2.ta = 0xffff;
  cia2.cra = 0x11;

#ifdef __OSCAR64__
  // An interrupt landing inside the timed section would be counted as work.
  // Nothing is armed this early, but the probe is cheap and being wrong here
  // is silent, so it does not rely on that.
  __asm { sei }
#endif

  const uint16_t t0 = cia2.ta;
  for (uint16_t i = 0; i < kProbeIterations; ++i) {
    _probe_work = _probe_work + 1;
  }
  const uint16_t t1 = cia2.ta;

#ifdef __OSCAR64__
  __asm { cli }
#endif

  // Timer A counts down, so the elapsed time is the drop.
  cpu_probe_us = t0 - t1;
  cpu_step_shift = cpu_shift_for_us(cpu_probe_us);
}
