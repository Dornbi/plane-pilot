// Arithmetic that only tells the truth on a 6510.
//
// Every other suite in this directory is compiled by g++, where `int` is 32
// bits. oscar64's is 16, and C promotes every narrower operand to `int` before
// it does anything, so an intermediate that wraps on the target does not wrap
// on the host. A host test then compares against arithmetic the C64 never
// performed, and passes.
//
// c64o/test/int16.h carries the measurements and the i16() helpers that let a
// host test model the target's width by hand. This file is the other half: the
// cases where modelling is not good enough and the answer has to come off the
// real chip. It is built by oscar64 and run in VICE - `make -C c64o test` does
// both when x64sc is on the PATH, and skips with a line when it is not.
//
// **There is no reference arithmetic in here.** That is the point. The expected
// values below are literals, each one a number a 6510 produces and an x86 does
// not, so nothing in this file can be quietly rewritten into 32-bit arithmetic
// by the compiler that reads it. Where a case does need a computed reference,
// it computes it in plain `int` - which *is* 16 bits here, so it wraps for free
// and needs no helper.
//
// Adding a case: give it the next id, add it to the table in the comment above
// main(), and keep the whole file inside a few million cycles so the VICE run
// stays under a second of wall clock in warp.
//
//   id  what
//   ---------------------------------------------------------------------
//    1  poly.cc's old rounding term, (sx + 2) >> 2 at the saturation point
//    2  poly.cc's current rounding term, same input
//    3  the int8_t negate-then-shift of mul_test.cc
//    4  vec_fastmul8p8 against a 16-bit product, at the edges
//    5  render.cc's _mul over the roll table's full int8_t range

#include <stdint.h>

#include "../vec.h"

// If this ever fails the file is being built for something that is not the
// target, and every literal below is wrong.
static_assert(sizeof(int) == 2, "target_test must be built by oscar64");

// volatile, or oscar64 drops a global that is only ever written - symbol and
// all, so vice_dump.sh cannot even find it. docs/emulator.md.
volatile uint16_t g_failures;
volatile uint16_t g_first_fail;
volatile int16_t g_first_got;
volatile int16_t g_first_want;
volatile uint16_t g_cases;

static void expect(uint16_t id, int16_t got, int16_t want) {
  ++g_cases;
  if (got == want) {
    return;
  }
  if (g_failures == 0) {
    g_first_fail = id;
    g_first_got = got;
    g_first_want = want;
  }
  ++g_failures;
}

// Opaque to the optimiser, so a case cannot be folded into its own answer.
volatile int16_t v_sx = 32767;
volatile int8_t v_cy = -128;

// 1 and 2. vec_div8p8 saturates at 32767 and a vertex just past the near plane
// saturates it routinely, so this input is reachable rather than synthetic.
//
// The old form adds 2 first: on a 16-bit int that is 32769, which wraps to
// -32767, and the vertex lands on the far side of the screen. One polygon then
// covered the whole viewport. The host cannot see it - there the sum is 32769
// and the result 8192, which is also what the *fixed* form gives. So the two
// forms are indistinguishable under g++ and differ by 16,384 here.
static void test_poly_rounding(void) {
  const int16_t sx = v_sx;
  expect(1, (int16_t)((sx + 2) >> 2), -8192);
  expect(2, (int16_t)((sx >> 2) + ((sx >> 1) & 1)), 8192);
}

// 3. The conversion of render.cc's horizon term shipped broken because the
// int8_t operand was negated before the shift: -(-128) is 128, which does not
// fit an int8_t and wraps straight back to -128. Diagonal bands through the sky.
static void test_int8_negate_then_shift(void) {
  const int8_t cy = v_cy;
  expect(3, (int16_t)((int16_t)(-cy) << 8) >> 8, -128);
}

// 4. vec_fastmul8p8 builds its product from the magnitudes and applies the sign
// last, so it wraps sign-magnitude where a plain product wraps two's
// complement. The two agree everywhere the product fits; these are the corners
// where it does not, and the literals are what the routine actually returns.
static void test_fastmul_edges(void) {
  expect(4, vec_fastmul8p8(32000, 256), 32000);
  expect(4, vec_fastmul8p8(-32000, 256), -32000);
  expect(4, vec_fastmul8p8(256, 256), 256);
  expect(4, vec_fastmul8p8(-256, -256), 256);
  expect(4, vec_fastmul8p8(4096, -2048), -32768);
}

// 5. render.cc's _mul(a, b) is vec_fastmul8p8(a, b << 8), standing in for the
// 16-bit product a * b. mul_test.cc sweeps this on the host against an i16()
// reference; here the reference is just `int`, which is 16 bits, so it wraps by
// itself and there is nothing to get wrong.
//
// A subset of the roll table rather than all 60 rows, and a stride over dx
// rather than all 65,536 values: this has to run in an emulator, and the point
// is that the arithmetic agrees at the edges, not an exhaustive sweep. The
// exhaustive one lives on the host, where it is affordable.
static const int8_t kRollB[] = {16, 8, 6, 4, 2, 1, -1, -2, -4, -6, -8, -16};

static void test_mul_against_16bit_product(void) {
  for (uint8_t i = 0; i < sizeof(kRollB); ++i) {
    const int8_t b = kRollB[i];
    for (int32_t dx = -32768; dx <= 32767; dx += 257) {
      const int16_t a = (int16_t)dx;
      const int16_t got = vec_fastmul8p8(a, (int16_t)b << 8);
      // `int` is 16 bits here, so this product wraps exactly as the C64's did.
      const int16_t want = (int16_t)(a * b);
      expect(5, got, want);
    }
  }
}

int main(void) {
  g_failures = 0;
  g_first_fail = 0;
  g_cases = 0;

  test_poly_rounding();
  test_int8_negate_then_shift();
  test_fastmul_edges();
  test_mul_against_16bit_product();

  // Something for vice_dump.sh's @spin to break on. Everything above has
  // landed in the globals by the time the loop is reached.
  for (;;) {
  }
}
