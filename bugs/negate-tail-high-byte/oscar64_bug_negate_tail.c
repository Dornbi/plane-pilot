#include <stdint.h>

// planes.cc's _div8p8: a magnitude through a called helper, the whole case
// taken aside, and the sign put back at the end.
volatile uint8_t sink;

static __noinline uint16_t frac8(int16_t a, int16_t b) {
  // Any out-of-line call returning a uint16_t; this one is a / b to 8 bits
  // for 0 <= a < b, by restoring division.
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

static __noinline int16_t div8p8(int16_t a, int16_t b) {
  int16_t m = a < 0 ? (int16_t)-a : a;
  int16_t q = m == b ? 256 : (int16_t)frac8(m, b);
  return a < 0 ? (int16_t)-q : q;
}

volatile int16_t in_a[6] = {100, -100, 200, -200, 1, -1};
volatile int16_t out[6];

int main(void) {
  for (uint8_t i = 0; i < 6; ++i) {
    out[i] = div8p8(in_a[i], 200);
  }
  for (;;) {
  }
  return 0;
}
