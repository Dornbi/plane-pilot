// Guards the one place where the host build can silently disagree with the
// C64: vec_fastmul8p8.
//
// On the C64 it is hand written assembly (vec_asm.cc). On the host it is the
// inline fallback in vec.h. Both must compute trunc(a * b / 256) - the
// product formed from the magnitudes with the sign applied at the end, so it
// rounds toward zero. An ordinary arithmetic shift would floor toward
// -infinity and differ by one on roughly half of all products, which would
// make every host test result meaningless for the real target.
//
// The same goes for the two clip primitives vec_frac16 and vec_mulfrac. Their
// stand-ins are checked here against the contract; that the assembly keeps it
// was checked on the real 6502, by running the same sweep under x64sc and
// comparing checksums (docs/project.md, "poly.cc - filled polygons").
//
// This file used to carry a host implementation of vec_fastmul8p8 plus the
// world_eye_* globals. Both are gone: vec.h now provides the multiply, and
// the position lives in flight.cc as flight_eye_*.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../vec.h"

// Reference spelling of the contract documented at the top of vec_asm.cc.
static int32_t reference_mul8p8(int32_t a, int32_t b) {
  int32_t magnitude = (labs(a) * labs(b)) >> 8;
  return ((a < 0) != (b < 0)) ? -magnitude : magnitude;
}

// |a| / |b| as a 0.16 fraction, saturating - the contract of vec_frac16.
static uint32_t reference_frac16(int32_t a, int32_t b) {
  uint32_t ua = (uint32_t)labs(a);
  uint32_t ub = (uint32_t)labs(b);
  if (ub == 0) {
    return 0xFFFF;
  }
  uint32_t q = (ua << 16) / ub;
  return q > 0xFFFF ? 0xFFFF : q;
}

// vec_mulfrac is the 8.8 multiply applied to the two halves of the fraction,
// with the low half rounded and the high half left truncating. Spelled out
// here so a change to either half has to be a deliberate one.
static int32_t reference_mulfrac(uint32_t t, int32_t d) {
  int32_t hi = reference_mul8p8((int32_t)(t >> 8), d);
  int32_t lo = reference_mul8p8((int32_t)(t & 0xFF), d);
  return hi + ((lo + 128) >> 8);
}

// Returns the number of mismatches found; 0 means the host agrees with the
// documented 6502 behaviour over the range the flight model actually uses.
int host_vec_selfcheck() {
  int mismatches = 0;
  for (int32_t a = -8192; a <= 8192; a += 3) {
    for (int32_t b = -256; b <= 256; b += 1) {
      int32_t want = reference_mul8p8(a, b);
      // Only meaningful where the result fits the 16 bit return value; past
      // that the assembly wraps and the exact value is not relied upon.
      if (want < -32768 || want > 32767) {
        continue;
      }
      int16_t got = vec_fastmul8p8((int16_t)a, (int16_t)b);
      if (got != (int16_t)want) {
        if (mismatches < 5) {
          printf("  vec_fastmul8p8(%d, %d) = %d, expected %d\n", (int)a, (int)b,
                 got, (int)want);
        }
        ++mismatches;
      }
    }
  }
  // The clip primitives, over the shape the clippers pass them: same sign,
  // |a| <= |b|. Their inputs run the whole int16 range - a clipped edge can
  // be a thousand units long or two - so the sweep does too.
  for (int32_t b = 1; b <= 32000; b += 61) {
    for (int32_t k = 0; k <= 8; ++k) {
      int32_t a = b * k / 8;
      for (int32_t sign = 1; sign >= -1; sign -= 2) {
        uint32_t want = reference_frac16(a * sign, b * sign);
        uint16_t got = vec_frac16((int16_t)(a * sign), (int16_t)(b * sign));
        if (got != (uint16_t)want) {
          if (mismatches < 5) {
            printf("  vec_frac16(%d, %d) = %u, expected %u\n", (int)(a * sign),
                   (int)(b * sign), got, want);
          }
          ++mismatches;
        }
        int32_t d = (int32_t)(int16_t)(a ^ 0x5A5A);
        int32_t want_m = reference_mulfrac(got, d);
        int16_t got_m = vec_mulfrac(got, (int16_t)d);
        if (want_m >= -32768 && want_m <= 32767 && got_m != (int16_t)want_m) {
          if (mismatches < 5) {
            printf("  vec_mulfrac(%u, %d) = %d, expected %d\n", got, (int)d,
                   got_m, (int)want_m);
          }
          ++mismatches;
        }
      }
    }
  }

  return mismatches;
}
