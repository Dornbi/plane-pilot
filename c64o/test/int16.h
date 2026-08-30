#ifndef TEST_INT16_H
#define TEST_INT16_H

// Target integer widths, for host tests.
//
// **oscar64's `int` is 16 bits. The host's is 32.** C promotes every operand
// narrower than `int` to `int` before it does any arithmetic, so an
// intermediate that wraps on the 6510 does not wrap under g++ - and a host test
// that computes its expected value in plain C is then comparing against
// arithmetic the target never performed.
//
// Measured, by compiling one file both ways and reading the C64's globals back
// with tools/vice_dump.sh (docs/emulator.md):
//
//   expression                              host      C64
//   (sx + 2) >> 2, sx = 32767               8192     -8192
//   a * b, a = b = 300                     24464     24464
//   (int16_t)(-cy) << 8 >> 8, cy = -128      128      -128
//
// The middle row is the trap: it agrees, because the value is narrowed at the
// assignment. Only *intermediates* diverge, which is why this is invisible to
// inspection and why the two rows either side of it were both real bugs -
// docs/project.md section 5 for the first, the header of mul_test.cc for the
// third.
//
// So: wherever a test computes a reference value whose intermediate can leave
// 16 bits, put it through i16() / u16() at each step the target would have
// truncated at. Not at the end - truncating only the final result reproduces
// the middle row and none of the others.
//
//     int16_t want = i16(i16(a + b) >> 2);   // yes: wraps where the 6510 wraps
//     int16_t want = (int16_t)((a + b) >> 2); // no: 32-bit add, then truncate
//
// What this cannot do is find the sites for you, and neither can the compiler.
// `make -C c64o/test narrowing` ratchets on implicit narrowings and does catch
// the third row above, but it is blind to the first: that expression's *result*
// fits in an int16_t on both machines, so nothing is narrowed and nothing is
// reported - only the intermediate differs. narrowing.baseline has the details.
//
// The only exact check is running the arithmetic on a real 6510, which is what
// target_test.cc does and why it exists.

#include <stdint.h>

// Truncate to the target's `int`. Both are the conversion C would apply on the
// 6510, spelled out so the host performs it too.
//
// The narrowing conversion of an out-of-range value to a signed type is
// implementation-defined before C++20 and wraps two's-complement on every
// compiler this project builds with, which is also what the 6510 does.
static inline int16_t i16(int32_t v) { return (int16_t)v; }
static inline uint16_t u16(uint32_t v) { return (uint16_t)v; }

// The 8-bit pair, for the same reason one step down. These matter less - an
// int8_t is 8 bits on both machines - but an intermediate that overflowed 16
// bits on the target before being cut to 8 still leaves a different byte.
static inline int8_t i8(int32_t v) { return (int8_t)v; }
static inline uint8_t u8(uint32_t v) { return (uint8_t)v; }

#endif // TEST_INT16_H
