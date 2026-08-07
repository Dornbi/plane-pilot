// Guards the conversion of oscar64's mul16 / mul16by8 call sites to
// vec_fastmul8p8, which is what removed those two runtime routines from the
// link. Each case below replays the exact expression the C64 code now uses
// against the plain 16-bit product it replaced, over the operand's *full*
// range rather than the range the call site is believed to stay inside.
//
// That distinction is the whole point of this file. The conversion shipped
// once with a bug - the horizon term negated its int8_t operand before passing
// it, so render_cy_chars == -128 became +128, wrapped back to -128, and flipped
// the sign of the product - and it survived a check that assumed cy could not
// reach -128 and skipped the case. It showed up as diagonal bands through the
// sky fill. Nothing here assumes an input range.
//
// Two things make vec_fastmul8p8 a non-obvious drop-in, and both are covered:
//   1. It returns trunc(a * b / 256), not the low 16 bits, so one operand has
//      to be pre-shifted by 8. Only an int8_t survives that shift intact.
//   2. It builds the product from the magnitudes and applies the sign last, so
//      it wraps sign-magnitude where mul16 wrapped two's complement. The two
//      agree everywhere the product fits and can disagree where it does not.

#include <stdint.h>
#include <stdio.h>

#include "vec.h"

// Mirrors _mul() in render.cc.
static inline int16_t _mul(int16_t a, int8_t b) {
  return vec_fastmul8p8(a, (int16_t)b << 8);
}

// oscar64's int is 16 bits, so the products the C64 computed were 16-bit and
// wrapped. Reproduce that on the host, where int is wider, with an explicit
// truncation - otherwise this compares against arithmetic the target never did.
static inline int16_t mul16_ref(int a, int b) { return (int16_t)(a * b); }

static int failures = 0;
static int reported = 0;

static void expect(int16_t got, int16_t want, const char *site, int a, int b) {
  if (got == want) {
    return;
  }
  ++failures;
  if (reported < 10) {
    printf("  FAIL %s: a=%d b=%d -> %d, expected %d\n", site, a, b, got, want);
    ++reported;
  }
}

// The roll tables from roll.cc: (dx, dy) per angle. roll_dx_div_dy is dx*16/dy.
static const int kRollTab[][2] = {
    {8, 0},     {16, -1},  {8, -1},    {8, -2},   {8, -3},   {8, -4},
    {8, -5},    {8, -6},   {8, -8},    {6, -8},   {10, -16}, {4, -8},
    {6, -16},   {2, -8},   {2, -16},   {0, -8},   {-2, -16}, {-2, -8},
    {-6, -16},  {-4, -8},  {-10, -16}, {-6, -8},  {-8, -8},  {-8, -6},
    {-8, -5},   {-8, -4},  {-8, -3},   {-8, -2},  {-8, -1},  {-16, -1},
    {-8, 0},    {-16, 1},  {-8, 1},    {-8, 2},   {-8, 3},   {-8, 4},
    {-8, 5},    {-8, 6},   {-8, 8},    {-6, 8},   {-10, 16}, {-4, 8},
    {-6, 16},   {-2, 8},   {-2, 16},   {0, 8},    {2, 16},   {2, 8},
    {6, 16},    {4, 8},    {10, 16},   {6, 8},    {8, 8},    {8, 6},
    {8, 5},     {8, 4},    {8, 3},     {8, 2},    {8, 1},    {16, 1},
};
static const int kRollMax = (int)(sizeof(kRollTab) / sizeof(kRollTab[0]));

// render.cc _pull_to_center: dx * roll_dy and dx * roll_dx, dx unconstrained.
static long test_pull_to_center() {
  long n = 0;
  for (int t = 0; t < kRollMax; ++t) {
    for (int k = 0; k < 2; ++k) {
      const int8_t b = (int8_t)kRollTab[t][k];
      if (b == 0) {
        continue;
      }
      for (long dx = -32768; dx <= 32767; ++dx) {
        expect(_mul((int16_t)dx, b), mul16_ref((int16_t)dx, b), "pull_to_center",
               (int)dx, b);
        ++n;
      }
    }
  }
  return n;
}

// render.cc _fill_sky_ground_*: the horizon term, roll_dx_div_dy * -cy.
// render_cy_chars is a truncating int8_t cast of an off-screen horizon, so the
// full int8_t range is live and the product genuinely overflows at the edges.
static long test_fill_sky_ground() {
  long n = 0;
  for (int t = 0; t < kRollMax; ++t) {
    const int x = kRollTab[t][0], y = kRollTab[t][1];
    const int16_t a = (y == 0) ? 0 : (int16_t)(x * 16 / y);
    for (int cy = -128; cy <= 127; ++cy) {
      expect((int16_t)(-_mul(a, (int8_t)cy)), mul16_ref(a, 0 - cy),
             "fill_sky_ground", a, cy);
      ++n;
    }
  }
  return n;
}

// box.cc _draw_one_box: cy * boxdef.w, with cy < 0 on entry and w a uint8_t.
static long test_draw_one_box() {
  long n = 0;
  for (int cy = -128; cy <= -1; ++cy) {
    for (int w = 0; w <= 255; ++w) {
      expect(vec_fastmul8p8((int16_t)((int16_t)cy << 8), (int16_t)w),
             mul16_ref(cy, w), "draw_one_box", cy, w);
      ++n;
    }
  }
  return n;
}

// sound.cc: the engine pitch jitter, (amp * n) >> 4. The >> 4 is an arithmetic
// shift and vec_fastmul8p8 truncates toward zero, so this would differ on every
// negative product with a remainder if the product were not exact. amp is
// base >> kEngineJitterShift; sweep every value that shift can produce.
static long test_sound_jitter() {
  long n = 0;
  for (int amp = 0; amp <= 2047; ++amp) {
    for (int nn = -16; nn <= 15; ++nn) {
      expect((int16_t)(vec_fastmul8p8((int16_t)amp, (int16_t)(nn << 8)) >> 4),
             (int16_t)(mul16_ref(amp, nn) >> 4), "sound_jitter", amp, nn);
      ++n;
    }
  }
  return n;
}

int main() {
  printf("pull_to_center:  %ld combinations\n", test_pull_to_center());
  printf("fill_sky_ground: %ld combinations\n", test_fill_sky_ground());
  printf("draw_one_box:    %ld combinations\n", test_draw_one_box());
  printf("sound_jitter:    %ld combinations\n", test_sound_jitter());

  if (failures) {
    printf("mul_test: %d failures\n", failures);
    return 1;
  }
  printf("mul_test: all conversions match mul16 exactly\n");
  return 0;
}
