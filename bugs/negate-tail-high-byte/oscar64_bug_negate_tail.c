// oscar64 miscompile (regression in 1616138): a function returning either a
// value or its negation, where the value comes back from another call, never
// stores the high byte of its result.
//
//   oscar64 -O2 -e oscar64_bug_negate_tail.c
//
// Expected: "PASS"   Actual (-O2, 1616138 and later): "FAIL ..."
#include <stdint.h>
#include <stdio.h>

#ifdef __GNUC__
#define __noinline __attribute__((noinline))
#endif

volatile uint8_t sink;

// Any out-of-line call returning a uint16_t; this one is a / b to 8 bits for
// 0 <= a < b, by restoring division.
static __noinline uint16_t frac8(int16_t a, int16_t b) {
  uint16_t r = (uint16_t)a, q = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    r <<= 1;
    q <<= 1;
    if (r >= (uint16_t)b) {
      r -= (uint16_t)b;
      q |= 1;
    }
  }
  sink = (uint8_t)r;
  return q;
}

// a / b as 8.8 fixed point, for |a| <= b: the magnitude through frac8, the
// whole case taken aside, and the sign put back at the end.
static __noinline int16_t div8p8(int16_t a, int16_t b) {
  int16_t m = a < 0 ? (int16_t)-a : a;
  int16_t q = m == b ? 256 : (int16_t)frac8(m, b);
  return a < 0 ? (int16_t)-q : q;
}

volatile int16_t in_a[6] = {100, -100, 200, -200, 1, -1};
static const int16_t expect[6] = {128, -128, 256, -256, 1, -1};

int main(void) {
  int16_t out[6];
  uint8_t bad = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    out[i] = div8p8(in_a[i], 200);
    if (out[i] != expect[i]) {
      bad = 1;
    }
  }
  printf(bad ? "FAIL" : "PASS");
  if (bad) {
    for (uint8_t i = 0; i < 6; ++i) {
      printf(" %d", out[i]);
    }
  }
  printf("\n");
  return bad;
}
