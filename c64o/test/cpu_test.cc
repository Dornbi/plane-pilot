// The boot-time speed probe's verdict (cpu.h), on the host.
//
// Only the pure half is testable here: cpu_probe() reads a CIA and times a
// loop, and neither means anything on an x86. What it turns that measurement
// into is ordinary arithmetic, and it is the half that decides how the whole
// flight model is scaled, so it gets checked rather than eyeballed. The
// measurement itself is checked against real emulated hardware - the numbers
// are in docs/supercpu.md.

#include <assert.h>
#include <stdio.h>

#include "../cpu.h"

// cpu.cc pulls in cia.h, which on the host is a plain struct, so the whole
// file compiles. cpu_probe() is never called from here.
#include "../cpu.cc"

static void test_a_stock_c64_asks_for_no_scaling() {
  assert(cpu_shift_for_us(kCpuReferenceUs) == 0);
  // Either side of the reference, well inside the first halving.
  assert(cpu_shift_for_us(kCpuReferenceUs - 100) == 0);
  assert(cpu_shift_for_us((kCpuReferenceUs >> 1) + 1) == 0);
}

static void test_each_halving_is_one_more_step() {
  // Exactly twice as fast, and exactly four times.
  assert(cpu_shift_for_us(kCpuReferenceUs >> 1) == 1);
  assert(cpu_shift_for_us(kCpuReferenceUs >> 2) == 2);
}

static void test_the_measured_machines() {
  // What the two emulators actually reported, so the constants and the real
  // readings cannot drift apart silently. docs/supercpu.md.
  assert(cpu_shift_for_us(10676) == 0); // x64sc, a stock C64
  assert(cpu_shift_for_us(496) == 2);   // xscpu64, a 20 MHz SuperCPU
  // Both are a comfortable distance from the band edge they sit next to: the
  // C64 is twice the first threshold, the SuperCPU five times inside the last.
  assert(cpu_shift_for_us(kCpuReferenceUs >> 1) == 1);
}

static void test_it_is_capped() {
  // A machine a hundred times faster still only asks for what the model has
  // constants for. Warp mode in an emulator is the realistic way to get here.
  assert(cpu_shift_for_us(1) == kCpuMaxStepShift);
  assert(cpu_shift_for_us(118) == kCpuMaxStepShift);
}

static void test_nonsense_reads_as_a_plain_c64() {
  // A stopped timer, and a wrapped one. Both mean the probe failed, and the
  // safe answer to that is the machine the program was written for - scaling
  // a model that is not being stepped any faster would only fly it slowly.
  assert(cpu_shift_for_us(0) == 0);
  assert(cpu_shift_for_us(65535) == 0);
  assert(cpu_shift_for_us(kCpuReferenceUs * 2) == 0);
}

static void test_it_is_monotone() {
  // Faster is never fewer steps, at every reading the timer can produce.
  uint8_t previous = 0;
  for (uint32_t us = kCpuReferenceUs; us >= 1; --us) {
    const uint8_t shift = cpu_shift_for_us((uint16_t)us);
    assert(shift >= previous);
    previous = shift;
  }
  assert(previous == kCpuMaxStepShift);
}

int main(void) {
  test_a_stock_c64_asks_for_no_scaling();
  test_each_halving_is_one_more_step();
  test_the_measured_machines();
  test_it_is_capped();
  test_nonsense_reads_as_a_plain_c64();
  test_it_is_monotone();
  printf("cpu_test: all passed\n");
  return 0;
}
