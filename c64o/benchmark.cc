#include "benchmark.h"

#if defined(__DEBUG_CYCLES__) || defined(__ENABLE_DEBUG__) ||                    \
    defined(__DEBUG_POLY__)

#include <stdint.h>

#include "cia.h"
#include "print.h"

extern bool mem_debug_enabled;

static uint32_t _benchmark_total_cycles;
static uint32_t _benchmark_start;

void bm_init(void) {
  // 1. Stop timer A and B (CIA2)
  cia2.cra = 0;
  cia2.crb = 0;

  // 2. Set both timers to max ($FFFF)
  cia2.ta = 0xffff;
  cia2.tb = 0xffff;

  // 3. Chain Timers: Set Timer B to count Timer A underflows
  // %01000001: Bit 6-5 (Count Timer A), Bit 0 (Start)
  cia2.crb = 0x41;

  // 4. Start Timer A (System Cycles)
  // %00010001: Bit 4 (Force Load), Bit 0 (Start)
  cia2.cra = 0x11;
}

void bm_start(void) {
  if (!mem_debug_enabled)
    return;
  const uint16_t timer_a = cia2.ta;
  const uint16_t timer_b = cia2.tb;
  _benchmark_start = (((uint32_t)timer_b << 16) | timer_a);
}

void bm_end(uint16_t pos, const char *label) {
  if (!mem_debug_enabled)
    return;
  const uint16_t timer_a = cia2.ta;
  const uint16_t timer_b = cia2.tb;
  const uint32_t benchmark_end = (((uint32_t)timer_b << 16) | timer_a);
  const uint32_t benchmark_cycles = _benchmark_start - benchmark_end;
  _benchmark_total_cycles += benchmark_cycles;
  print_labeled_bcd(pos, label, benchmark_cycles, 6);
}

void bm_total(uint16_t pos, const char *label) {
  if (!mem_debug_enabled)
    return;
  print_labeled_bcd(pos, label, _benchmark_total_cycles, 6);
  _benchmark_total_cycles = 0;
}

void bm_view_start(void) { bm_start(); }
void bm_view_end(uint16_t pos, const char *label) { bm_end(pos, label); }

void bm_model_start(void) { bm_start(); }
void bm_model_end(uint16_t pos, const char *label) { bm_end(pos, label); }

void bm_poly_start(void) { bm_start(); }
void bm_poly_end(uint16_t pos, const char *label) { bm_end(pos, label); }

#endif
