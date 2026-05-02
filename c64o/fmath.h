#ifndef FMATH_H
#define FMATH_H

#include <stdint.h>

inline int8_t _abs8(int8_t a) { return a > 0 ? a : -a; }
inline int16_t _abs16(int16_t a) { return a > 0 ? a : -a; }

inline uint8_t _max8(uint8_t a, uint8_t b) { return a > b ? a : b; }
inline uint16_t _max16(uint16_t a, uint16_t b) { return a > b ? a : b; }

inline uint8_t _min8(uint8_t a, uint8_t b) { return a > b ? b : a; }
inline uint16_t _min16(uint16_t a, uint16_t b) { return a > b ? b : a; }

uint8_t _get_msb(uint16_t n);

// Calculates the ratio of |y| to |x| + |y|,
// scaled by a factor of 64 to return a value between 0 and 64.
uint8_t _get_ratio(int16_t x, int16_t y);

#pragma compile("fmath.cc")

#endif