#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

static const uint8_t kColorBlack = 0;
static const uint8_t kColorWhite = 1;
static const uint8_t kColorRed = 2;
static const uint8_t kColorCyan = 3;
static const uint8_t kColorGreen = 5;
static const uint8_t kColorBlue = 6;
static const uint8_t kColorYellow = 7;
static const uint8_t kColorOrange = 8;
static const uint8_t kColorBrown = 9;
static const uint8_t kColorLightRed = 10;
static const uint8_t kColorDarkGray = 11;
static const uint8_t kColorMedGray = 12;
static const uint8_t kColorLightGreen = 13;
static const uint8_t kColorLightBlue = 14;
static const uint8_t kColorLightGray = 15;

static const uint8_t kColorGround = kColorGreen;
static const uint8_t kColorGrndObj = kColorOrange;
static const uint8_t kColorSky = kColorBlue;
static const uint8_t kColorGrad1 = kColorCyan;
static const uint8_t kColorGrad2 = kColorLightBlue;
static const uint8_t kColorBg = kColorBlack;
static const uint8_t kColorPanelBg = kColorBlack;
static const uint8_t kColorInstrument = kColorWhite;
static const uint8_t kColorSun = kColorYellow;
// Clouds are a white-and-transparent checkerboard, one colour per sprite
// (sprite_objects.md §4). The dither is what makes them read as a half-tone.
static const uint8_t kColorCloud = kColorWhite;
static const uint8_t kColorWater = kColorBlue;

#endif /* COLOR_H */