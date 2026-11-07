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
// The orientation indicator (sprites.h). Light red for the same reason the
// panel's indicator lamps are: it is the one hue in the palette that reads as a
// marker against both halves of the viewport, and it cannot be mistaken for a
// cloud or for the sun.
static const uint8_t kColorOrientation = kColorWhite;
// The tail fin in the back view (sprites.h). Medium grey, because it has to
// read as aircraft structure against both halves of the viewport - the blue to
// cyan sky above the horizon and the green ground below it - and it must not
// be mistaken for a cloud, which is the one other large pale shape out there.
static const uint8_t kColorAircraft = kColorMedGray;
// Other aircraft (docs/planes.md §8), by size. Black while far - the dot, or a
// silhouette under about 8 pixels - so that a few pixels still stand out;
// dark grey nearer, where the shape carries it (opponent.cc kOpponentNearD).
// Neither is the tail fin's medium grey or a cloud's white.
static const uint8_t kColorOpponentFar = kColorBlack;
static const uint8_t kColorOpponentNear = kColorDarkGray;
static const uint8_t kColorWater = kColorBlue;

// The viewport's colour scheme (docs/color_schemes.md). ppilot builds with
// __ENABLE_COLOR_SCHEMES__ and every mission picks one of the schemes in
// pmission.cc; _start_mission() (sim.cc) copies it into color_scheme. fpilot,
// the demos and the host tests build without it, and every COLOR_ macro below
// is then today's constant, so their code does not change by a byte.
//
// Fields marked "colour RAM" end up in colour RAM, where multicolour mode
// keeps only colours 0 to 7; pmission.cc asserts that.
#ifdef __ENABLE_COLOR_SCHEMES__
static const uint8_t kColorSchemeDots = 7;  // world.h KWorldDotColors
static const uint8_t kColorSchemeObjs = 9;  // world.h kWorldObjDim
struct ColorScheme {
  uint8_t sky8;    // colour RAM, | 0x08: the sky, bit pair 11
  uint8_t grad1_8; // colour RAM, | 0x08: the horizon band of the gradient
  uint8_t grad2;   // $D021: the upper band of the gradient
  uint8_t ground;  // $D022
  uint8_t grndobj; // $D023
  uint8_t cloud;
  uint8_t sun;
  uint8_t fin;  // the tail fin in the back view
  uint8_t text; // colour RAM: the message line, hires
  uint8_t dot[kColorSchemeDots]; // colour RAM, by WorldMapType
  uint8_t obj[kColorSchemeObjs]; // colour RAM, by object
};
extern ColorScheme color_scheme;
#define COLOR_SKY8 (color_scheme.sky8)
#define COLOR_GRAD1_8 (color_scheme.grad1_8)
#define COLOR_GRAD2 (color_scheme.grad2)
#define COLOR_GROUND (color_scheme.ground)
#define COLOR_GRNDOBJ (color_scheme.grndobj)
#define COLOR_CLOUD (color_scheme.cloud)
#define COLOR_SUN (color_scheme.sun)
#define COLOR_FIN (color_scheme.fin)
#define COLOR_TEXT (color_scheme.text)
#else
#define COLOR_SKY8 (kColorSky | 0x08)
#define COLOR_GRAD1_8 (kColorGrad1 | 0x08)
#define COLOR_GRAD2 kColorGrad2
#define COLOR_GROUND kColorGround
#define COLOR_GRNDOBJ kColorGrndObj
#define COLOR_CLOUD kColorCloud
#define COLOR_SUN kColorSun
#define COLOR_FIN kColorAircraft
#define COLOR_TEXT kColorBlack
#endif

#endif /* COLOR_H */