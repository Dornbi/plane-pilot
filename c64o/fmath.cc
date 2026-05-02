#include "fmath.h"

uint8_t _get_msb(uint16_t n) {
  if (n == 0) {
    return 0;
  }

  uint8_t r = 0;
  if (n & 0xFF00) {
    n >>= 8;
    r += 8;
  }
  if (n & 0x00F0) {
    n >>= 4;
    r += 4;
  }
  if (n & 0x000C) {
    n >>= 2;
    r += 2;
  }
  if (n & 0x0002) {
    r += 1;
  }

  return r;
}

uint8_t _get_ratio(int16_t x, int16_t y) {
  uint16_t ay = _abs16(y);
  uint16_t ax = _abs16(x);
  uint16_t sum = ay + ax;

  if (sum == 0) {
    return 0;
  }

  // --- OPTIMIZED DIVISION-FREE RATIO CALCULATION ---
  uint8_t ratio = 0;

  // If ay == sum, the result is exactly 64.
  // (We handle this first because 64 requires a 7th bit).
  if (ay == sum) {
    ratio = 64;
  } else {
    uint16_t n = ay;
    uint16_t d = sum;

    // Safety check: Prevent 16-bit overflow when we shift 'n' left.
    // If sum is incredibly large, we scale both down by a factor of 2.
    // Because n < d at this point, if d <= 32767, then n * 2 is guaranteed <=
    // 65534.
    if (d > 32767) {
      n >>= 1;
      d >>= 1;
    }

    // Unrolled 6-bit shift-subtract division to calculate (n * 64) / d
    n <<= 1;
    if (n >= d) {
      n -= d;
      ratio |= 32;
    }
    n <<= 1;
    if (n >= d) {
      n -= d;
      ratio |= 16;
    }
    n <<= 1;
    if (n >= d) {
      n -= d;
      ratio |= 8;
    }
    n <<= 1;
    if (n >= d) {
      n -= d;
      ratio |= 4;
    }
    n <<= 1;
    if (n >= d) {
      n -= d;
      ratio |= 2;
    }
    n <<= 1;
    if (n >= d) {
      n -= d;
      ratio |= 1;
    }
  }
  return ratio;
}