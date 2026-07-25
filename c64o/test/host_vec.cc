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
  return mismatches;
}
