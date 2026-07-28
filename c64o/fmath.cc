#include "fmath.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

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

// Calculates the ratio of |y| to |x| + |y|,
// scaled by a factor of 64 to return a value between 0 and 64.
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

static const uint8_t kHeadingLut[65] = {
    0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  2, 2, 3, 3,
    3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  6, 6, 6, 6,
    6,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  9,  9,  9, 9, 9, 10,
    10, 10, 10, 10, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12};

uint8_t _get_heading(int16_t x, int16_t y) {
  uint8_t ratio = _get_ratio(x, y);
  uint8_t angle = kHeadingLut[ratio];
  if (x >= 0) {
    if (y < 0) {
      // Q0: 0-12
      return angle;
    } else {
      // Q3: 36-48 (wraps to 0)
      return angle > 0 ? kHeadingMax - angle : 0;
    }
  } else {
    if (y < 0) {
      // Q1: 12-24
      return kHeadingMax / 2 - angle;
    } else {
      // Q2: 24-36
      return kHeadingMax / 2 + angle;
    }
  }
}

static const uint8_t kRollAngleLut[65] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,
    4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,
    8,  8,  9,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12,
    12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 15};

static const uint8_t kRollMax = 60;

uint8_t _get_roll_angle(int16_t up_z, int16_t left_z) {
  uint8_t ratio = _get_ratio(up_z, left_z);
  uint8_t angle = kRollAngleLut[ratio];
  if (up_z >= 0) {
    if (left_z >= 0) {
      // Q0: 0-15
      return angle;
    } else {
      // Q3: 45-60 (wraps to 0)
      return angle > 0 ? kRollMax - angle : 0;
    }
  } else {
    if (left_z >= 0) {
      // Q1: 15-30
      return kRollMax / 2 - angle;
    } else {
      // Q2: 30-45
      return kRollMax / 2 + angle;
    }
  }
}
#pragma optimize(pop)
