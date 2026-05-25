#ifndef CHARDEFS_H
#define CHARDEFS_H

#include <stdint.h>

static const uint16_t kTotalChars = 333;

static const uint8_t kCharSolidGround = 127;
static const uint8_t kCharSolidSky = 0;
static const uint8_t kCharSolidGrad1 = 0;
static const uint8_t kCharSolid11 = 0;

extern const uint8_t chardefs[kTotalChars][8];

#pragma compile("chardefs.cc")

#endif
