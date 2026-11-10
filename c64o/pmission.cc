#include "mission.h"

const uint8_t kWaypointDefault = 1;
static const uint8_t kWaypointMin1000ft = 0;
static const uint8_t kWaypointLanded1 = 1;
static const uint8_t kWaypointLake1 = 2;
static const uint8_t kWaypointLanded2a = 5;
static const uint8_t kWaypointUpsideDown2 = 6;
static const uint8_t kWaypointLanded2b = 7;
static const uint8_t kWaypointCity1 = 8;
static const uint8_t kWaypointLanded2c = 11;
static const uint8_t kWaypointField1 = 12;
static const uint8_t kWaypointLanded2d = 15;

const uint8_t kMissionWpX[kMissionWpCount] = {
    0x00, // 00 (Min 1000ft)
    0x20, // 01 (Runway 1)
    0x10, // 02 (Lake 1)
    0x40, // 03 (Lake 2)
    0x58, // 04 (Lake 3)
    0x60, // 05 (Runway 2)
    0x60, // 06 (Runway 2 upside down)
    0x60, // 07 (Runway 2)
    0x10, // 08 (City 1)
    0x68, // 09 (City 2)
    0x70, // 10 (City 3)
    0x60, // 11 (Runway 2)
    0x30, // 12 (Field 1)
    0x08, // 13 (Field 2)
    0x38, // 14 (Field 3)
    0x60, // 15 (Runway 2)
};

const uint8_t kMissionWpY[kMissionWpCount] = {
    0x00, // 00 (Min 1000ft)
    0x3F, // 01 (Runway 1)
    0xD0, // 02 (Lake 1)
    0xE0, // 03 (Lake 2)
    0x10, // 04 (Lake 3)
    0xBF, // 05 (Runway 2)
    0xBF, // 06 (Runway 2 upside down)
    0xBF, // 07 (Runway 2)
    0x68, // 08 (City 1)
    0x98, // 09 (City 2)
    0xE8, // 10 (City 3)
    0xBF, // 11 (Runway 2)
    0x88, // 12 (Field 1)
    0xA0, // 13 (Field 2)
    0xB0, // 14 (Field 3)
    0xBF, // 15 (Runway 2)
};

const MissionWaypointConstraint kMissionWpConstraint[kMissionWpCount] = {
    WP_MIN_1000FT,  // 00 (Min 1000ft)
    WP_LANDED,      // 01 (Runway 1)
    WP_NOTHING,     // 02 (Lake 1)
    WP_NOTHING,     // 03 (Lake 2)
    WP_NOTHING,     // 04 (Lake 3)
    WP_LANDED,      // 05 (Runway 2)
    WP_UPSIDE_DOWN, // 06 (Runway 2)
    WP_LANDED,      // 07 (Runway 2)
    WP_MIN_3000FT,  // 08 (City 1)
    WP_MIN_3000FT,  // 09 (City 2)
    WP_MIN_3000FT,  // 10 (City 3)
    WP_LANDED,      // 11 (Runway 2)
    WP_MAX_250FT,   // 12 (Field 1)
    WP_MAX_250FT,   // 13 (Field 2)
    WP_MAX_250FT,   // 14 (Field 3)
    WP_LANDED,      // 15 (Runway 2)
};

const uint8_t kMissionWpBegin[kMissionCount] = {
    kWaypointMin1000ft,   // 01
    kWaypointLanded1,     // 02
    kWaypointMin1000ft,   // 03
    kWaypointLanded1,     // 04
    kWaypointLanded2a,    // 05
    kWaypointLake1,       // 06
    kWaypointUpsideDown2, // 07
    kWaypointCity1,       // 08
    kWaypointField1,      // 09
    kWaypointLanded2a,    // 10
};
const uint8_t kMissionWpEnd[kMissionCount] = {
    kWaypointMin1000ft + 1, // 01
    kWaypointLanded1 + 1,   // 02
    kWaypointLanded1 + 1,   // 03
    kWaypointLanded1 + 1,   // 04
    kWaypointLanded2a + 1,  // 05
    kWaypointLanded2a + 1,  // 06
    kWaypointLanded2b + 1,  // 07
    kWaypointLanded2c + 1,  // 08
    kWaypointLanded2d + 1,  // 09
    kWaypointLanded2a + 1,  // 10
};

const char *const kMissionTitles[kMissionCount] = {
    "01 AIRBORNE",        // 01
    "02 GETTING DOWN",    // 02
    "03 SOLO FLIGHT",     // 03
    "04 FIND THE RUNWAY", // 04
    "05 FERRY FLIGHT",    // 05
    "06 LAKE PATROL",     // 06
    "07 AIRSHOW",         // 07
    "08 AERIAL RECON",    // 08
    "09 CROP DUSTER",     // 09
    "10 FUEL CHALLENGE",  // 10
};

const char *const kMissionDesc[kMissionCount] = {
    "HANDS ON THE THROTTLE! LIFT THE\n"    // 01
    "PLANE AND CLIMB TO 1000FT.",          //
    "YOU ARE ON FINAL ON RUNWAY 1.\n"      // 02
    "LAND SAFELY!",                        //
    "PUT IT ALL TOGETHER! GET AIRBORNE,\n" // 03
    "CLIMB TO 1000FT AND LAND AGAIN.",     //
    "WHERE AM I? FIND THE RUNWAY\n"        // 04
    "AND GET ON THE GROUND.",              //
    "NAVIGATE TO THE OTHER FIELD\n"        // 05
    "AND LAND SAFELY.",                    //
    "VISIT THREE LAKES ON YOUR WAY\n"      // 06
    "BEFORE TOUCHING DOWN ON RUNWAY 2",    //
    "PLEASE THE CROWD! MAKE A LOW PASS\n"  // 07
    "UPSIDEDOWN ABOVE THE RUNWAY.\n",      //
    "FLY AT LEAST 3000FT OVER ALL\n"       // 08
    "CITIES ON THE MAP",                   //
    "TOUCH ALL FIELDS ON THE MAP.\n"       // 09
    "MAKE SURE TO STAY BELOW 250FT.",      //
    "YOU ARE ALMOST OUT OF FUEL.\n"        // 10
    "GET TO THE NEAREST FIELD AND LAND.",  //
};

const uint8_t kMissionStartX[kMissionCount] = {
    0x1C, // 01
    0x10, // 02
    0x1C, // 03
    0x10, // 04
    0x1C, // 05
    0x08, // 06
    0x40, // 07
    0x10, // 08
    0x10, // 09
    0x50, // 10
};

const uint8_t kMissionStartY[kMissionCount] = {
    0x3F, // 01
    0x3F, // 02
    0x3F, // 03
    0x5F, // 04
    0x3F, // 05
    0xB8, // 06
    0xBF, // 07
    0x50, // 08
    0x88, // 09
    0x90, // 10
};

const uint8_t kMissionStartZ[kMissionCount] = {
    0x00, // 01
    0x02, // 02
    0x00, // 03
    0x04, // 04
    0x00, // 05
    0x04, // 06
    0x04, // 07
    0x04, // 08
    0x01, // 09
    0x04, // 10
};

const uint8_t kMissionStartSpeed[kMissionCount] = {
    0x00, // 01
    0x60, // 02
    0x00, // 03
    0x60, // 04
    0x00, // 05
    0x60, // 06
    0x60, // 07
    0x60, // 08
    0x50, // 09
    0x50, // 10
};

const uint8_t kMissionStartThrottle[kMissionCount] = {
    0x00, // 01
    0x02, // 02
    0x00, // 03
    0x14, // 04
    0x00, // 05
    0x14, // 06
    0x14, // 07
    0x14, // 08
    0x14, // 09
    0x10, // 10
};

// flight_fuel = start_fuel << 12 - 1, so one step of this dial is 4096 - a
// quarter of what mission 10 flies on. 0x22 is a full tank; every mission but
// the last one has fuel to spare and the number is not what makes them hard.
const uint8_t kMissionStartFuel[kMissionCount] = {
    0x22, // 01
    0x22, // 02
    0x22, // 03
    0x22, // 04
    0x22, // 05
    0x22, // 06
    0x22, // 07
    0x22, // 08
    0x22, // 09
    // 10. The 49.6 world units from the start to runway 2 cost about 12,700
    // at the cheapest cruise the model has (throttle 16 holds trim speed), so
    // 0x04 - 16,383 - was 78% of the tank spent flying straight there. That
    // left nothing for the circuit: an autopilot flown on a tight pattern
    // landed with 3.7% in hand, a wider one arrived dead-stick, and a wider
    // one still came down short of the field. 0x05 keeps it a fuel challenge
    // and pays for one normal approach.
    0x05,
};

const uint8_t kMissionWindX[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
    0x00, // 04
    0x00, // 05
    0x00, // 06
    0x00, // 07
    0x00, // 08
    0x00, // 09
    0x00, // 10
};

const uint8_t kMissionWindY[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
    0x00, // 04
    0x00, // 05
    0x00, // 06
    0x00, // 07
    0x00, // 08
    0x00, // 09
    0x00, // 10
};

uint8_t mission_completed[kMissionCount] = {
    0, // 01
    0, // 02
    0, // 03
    0, // 04
    0, // 05
    0, // 06
    0, // 07
    0, // 08
    0, // 09
    0, // 10
};

#ifdef __ENABLE_COLOR_SCHEMES__
#include "gfx.h"
#include "mapdefs.h"

ColorScheme color_scheme;

// clang-format off
// Earth is what ppilot always looked like, and what fpilot still does: the
// constants in color.h, KWorldDotColors and kWorldObjColors. The others come
// from the scheme sheet in docs/color_schemes.md, plus two colours it does
// not give: Night's white (lit) runway, and Dusk's dark grey tail fin, which
// in medium grey would vanish into its grey ground. Night's fin stays medium
// grey, the same grey as its clouds. The message box stays black, as it is
// for fpilot (msg.cc): its letters are $D021, grad2, so at night they are
// brown on the black sky.
//
// Every colour RAM field - sky8, grad1_8, dot and obj - is 0 to 7, the
// only colours colour RAM has in multicolour mode. dot[MAP_DOT_GROUND] is
// kGfxDotGround in all of them: those dots are ground, and drawn in $D022.
//
// The map's 18 tiles, each a pair of colours (mapdefs.h), are drawn on the
// ground colour. Each scheme's follow its view: a tile in a colour of the
// view has that colour, and one that would vanish into the ground is moved
// off it.
const ColorScheme kColorSchemes[kColorSchemeCount] = {
    // Earth
    {kColorBlue | 0x08, kColorCyan | 0x08, kColorLightBlue, kColorGreen, kColorOrange,
     kColorWhite, kColorYellow, kColorMedGray,
     {kColorBlack, kGfxDotGround, kColorBlack, kColorWhite, kColorCyan, kColorBlue, kColorYellow},
     {kColorBlack, kColorBlack, kColorBlack, kColorYellow, kColorYellow,
      kColorBlue, kColorBlue, kColorBlack, kColorBlack},
     // Map: mapdefs.cc's kMapTileColors.
     {0x00, 0xC0, 0xC0, 0xC0,                         // grid
      0x00, 0xF0, 0x30, 0xE6, 0xD7,                   // dots
      0x0C, 0x8C, 0xC8, 0x7F, 0xF7, 0xE6, 0xE6, 0x80, 0x80}},
    // Desert: Earth on orange ground, its fields brown - in orange, as
    // Earth's are, they would vanish into it.
    {kColorBlue | 0x08, kColorCyan | 0x08, kColorLightBlue, kColorOrange, kColorBrown,
     kColorWhite, kColorYellow, kColorMedGray,
     {kColorBlack, kGfxDotGround, kColorBlack, kColorWhite, kColorCyan, kColorBlue, kColorYellow},
     {kColorBlack, kColorBlack, kColorBlack, kColorYellow, kColorYellow,
      kColorBlue, kColorBlue, kColorBlack, kColorBlack},
     // Map: fields and towns brown, as in the view; orange would be ground.
     {0x00, 0xC0, 0xC0, 0xC0,
      0x00, 0xF0, 0x30, 0xE6, 0xD7,
      0x0C, 0x9C, 0xC9, 0x7F, 0xF7, 0xE6, 0xE6, 0x90, 0x90}},
    // Dusk: a red sky over grey ground, a white sun, towns and cities lit.
    {kColorPurple | 0x08, kColorYellow | 0x08, kColorLightRed, kColorDarkGray, kColorOrange,
     kColorWhite, kColorWhite, kColorMedGray,
     {kColorBlack, kGfxDotGround, kColorBlack, kColorWhite, kColorCyan, kColorBlue, kColorYellow},
     {kColorBlack, kColorBlack, kColorBlack, kColorYellow, kColorYellow,
      kColorBlue, kColorBlue, kColorYellow, kColorYellow},
     // Map: on grey, the grid dark grey, white dots white, the runway's and
     // the fields' grey light grey; towns and cities lit yellow.
     {0x00, 0xB0, 0xB0, 0xB0,
      0x00, 0x10, 0x30, 0xE6, 0xD7,
      0x0F, 0x8F, 0xF8, 0x7F, 0xF7, 0xE6, 0xE6, 0x87, 0x87}},
    // Night: a black sky, a white moon, grey clouds over dark grey ground;
    // the runway, towns and cities lit, yellow fields gone dark red.
    {kColorBlack | 0x08, kColorRed | 0x08, kColorDarkGray, kColorBrown, kColorLightGray,
     kColorMedGray, kColorWhite, kColorMedGray,
     {kColorBlack, kGfxDotGround, kColorBlack, kColorWhite, kColorGreen, kColorBlue, kColorRed},
     {kColorWhite, kColorBlack, kColorBlack, kColorRed, kColorRed,
      kColorBlue, kColorBlue, kColorYellow, kColorYellow},
     // Map: as the view - cyan dots green, yellow ones and yellow fields red,
     // fields light grey, towns and cities lit - but the runway black with
     // light grey markings: white, the navpoint digit on it would vanish.
     {0x00, 0xC0, 0xC0, 0xC0,
      0x00, 0xF0, 0x50, 0xE6, 0x52,
      0x0F, 0xFC, 0xCF, 0x2C, 0xC2, 0xE6, 0xE6, 0xF7, 0xF7}},
};
// clang-format on

#define COLOR_SCHEME(n) ((uint8_t)((n) * sizeof(ColorScheme)))
static const uint8_t kSchemeEarth = COLOR_SCHEME(0);
static const uint8_t kSchemeDesert = COLOR_SCHEME(1);
static const uint8_t kSchemeDusk = COLOR_SCHEME(2);
static const uint8_t kSchemeNight = COLOR_SCHEME(3);
#undef COLOR_SCHEME

const uint8_t kMissionScheme[kMissionCount] = {
    kSchemeEarth,  // 01
    kSchemeEarth,  // 02
    kSchemeEarth,  // 03
    kSchemeDesert, // 04
    kSchemeDesert, // 05
    kSchemeEarth,  // 06
    kSchemeDusk,   // 07
    kSchemeNight,  // 08
    kSchemeEarth,  // 09
    kSchemeDusk,   // 10
};
#endif
