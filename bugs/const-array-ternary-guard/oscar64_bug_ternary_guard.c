// oscar64 miscompile: the guard of a `?:` whose false arm indexes a `const`
// array at a *negative-biased* index is discarded, and the biased load runs for
// every index.
//
//   oscar64 -O2 -Op -Oa -Oi -Oz -Oo oscar64_bug_ternary_guard.c
//
// Expected: "PASS"   Actual (-O1/-O2/-O3/-Os): "FAIL <i> ..." for i = 0..12
//
// `row` is a loop variable the compiler knows is 0..20, and the false arm is
// only reachable for row >= 13, where `row - 13` is 0..7 and in bounds. The
// generated code is a single unguarded `LDA tb-13,x`, so rows 0..12 read the
// thirteen bytes in front of the table and `out[]` fills with whatever the
// linker put there.
//
// This is the same corner as the two fixed reports beside it - a const array
// read at a biased index - but the failure is the other way round: those folded
// the *taken* arm away to a constant, this one keeps the untaken arm and throws
// the condition out.
//
// Found in plane-pilot's c64o/sprites.cc, drawing the tail fin's sprite: the
// rows above the taper came out as full-width bars instead of blank ones.
// Written there as two passes over the rows - clear all of them, then paint
// the taper over the ones that have it - so that both arrays are indexed from
// zero and there is no bias to fold.
#include <stdint.h>
#include <stdio.h>

// Builds and runs on the host too, where it prints PASS - which is what says
// the C is right and the target code is wrong.
#ifdef __GNUC__
#define __noinline __attribute__((noinline))
#endif

#define FIRST_INKED_ROW 13
#define ROWS 21

static const uint8_t tb[8] = {2, 4, 6, 6, 8, 10, 10, 12};

// Something for the bad index to read, so the failure is data rather than a
// crash - which is exactly how it presents in real code.
// What fill() has to produce, spelled out rather than recomputed from the same
// ternary: written the second way, the *checker* miscompiles too and the two
// wrong answers can agree.
static const uint8_t expect[ROWS] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 2, 4, 6, 6, 8, 10, 10, 12};

static const uint8_t before[16] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                                   0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                                   0x18, 0x18, 0x18, 0x18};

// The bitmap the real code builds: three bytes per row, a centred run of
// `width` pixels. Kept whole rather than reduced to `*dst = width`, because a
// store of the width alone lets the compiler precompute the whole loop into a
// 21-byte table and copy that - which is correct, and hides the bug.
uint8_t out[ROWS * 3];

static void put(uint8_t *dst, uint8_t width) {
  // Only so the reduction stays inside its own array when the bug fires: the
  // widths it then gets are whatever bytes precede tb, which here are machine
  // code and reach 159. The clamp is in the callee, so the call site - which is
  // what miscompiles - is untouched.
  if (width > 24) {
    width = 24;
  }
  dst[0] = 0;
  dst[1] = 0;
  dst[2] = 0;
  uint8_t x = (24 - width) >> 1;
  for (uint8_t n = width; n != 0; --n) {
    dst[x >> 3] |= 0x80 >> (x & 7);
    ++x;
  }
}

__noinline void fill(void) {
  uint8_t *dst = out;
  for (uint8_t row = 0; row < ROWS; ++row) {
    put(dst, row < FIRST_INKED_ROW ? 0 : tb[row - FIRST_INKED_ROW]);
    dst += 3;
  }
}

int main(void) {
  // Referenced so the linker cannot drop it; the table it should not be read
  // through is the one in front of tb.
  out[0] = before[0];
  fill();

  uint8_t bad = 0;
  for (uint8_t row = 0; row < ROWS; ++row) {
    const uint8_t want = expect[row];
    // The row is blank or a centred run; either way its population count is
    // the width, which is all this needs to check.
    uint8_t got = 0;
    for (uint8_t b = 0; b < 3; ++b) {
      uint8_t v = out[row * 3 + b];
      while (v) {
        got += v & 1;
        v >>= 1;
      }
    }
    if (got != want) {
      if (bad == 0) {
        printf("FAIL");
      }
      if (bad < 5) {
        printf(" row %d: %d not %d", (int)row, (int)got, (int)want);
      }
      ++bad;
    }
  }
  printf(bad ? "\n" : "PASS\n");
#ifndef __GNUC__
  // Something for VICE to stop on, and to keep the screen readable.
  for (;;) {
  }
#endif
  return bad != 0;
}
