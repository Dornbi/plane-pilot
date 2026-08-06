#include "mission.h"

const uint8_t kWaypointDefault = 2;
static const uint8_t kWaypointMin1000ft = 0;
static const uint8_t kWaypointMin2000ft = 1;
static const uint8_t kWaypointLanded1 = 2;
static const uint8_t kWaypointLake1 = 3;
static const uint8_t kWaypointLanded2a = 6;
static const uint8_t kWaypointUpsideDown2 = 7;
static const uint8_t kWaypointLanded2b = 8;
static const uint8_t kWaypointCity1 = 9;
static const uint8_t kWaypointLanded2c = 12;
static const uint8_t kWaypointField1 = 13;
static const uint8_t kWaypointLanded2d = 16;

const uint8_t kMissionWpX[kMissionWpCount] = {
    0x00, // 00 (Min 1000ft)
    0x00, // 01 (Min 2000ft)
    0x20, // 02 (Runway 1)
    0x10, // 05 (Lake 1)
    0x40, // 04 (Lake 2)
    0x58, // 03 (Lake 3)
    0x60, // 06 (Runway 2)
    0x60, // 07 (Runway 2 upside down)
    0x60, // 08 (Runway 2)
    0x10, // 09 (City 1)
    0x68, // 10 (City 2)
    0x70, // 11 (City 3)
    0x60, // 12 (Runway 2)
    0x30, // 13 (Field 1)
    0x08, // 14 (Field 2)
    0x38, // 15 (Field 3)
    0x60, // 12 (Runway 2)
};

const uint8_t kMissionWpY[kMissionWpCount] = {
    0x00, // 00 (Min 1000ft)
    0x00, // 01 (Min 2000ft)
    0x3F, // 02 (Runway 1)
    0xD0, // 05 (Lake 1)
    0xE0, // 04 (Lake 2)
    0x10, // 03 (Lake 3)
    0xBF, // 06 (Runway 2)
    0xBF, // 07 (Runway 2 upside down)
    0xBF, // 08 (Runway 2)
    0x68, // 09 (City 1)
    0x98, // 10 (City 2)
    0xE8, // 11 (City 3)
    0xBF, // 12 (Runway 2)
    0x88, // 13 (Field 1)
    0xA0, // 14 (Field 2)
    0xB0, // 15 (Field 3)
    0xBF, // 16 (Runway 2)
};

const MissionWaypointConstraint kMissionWpConstraint[kMissionWpCount] = {
    WP_MIN_1000FT,  // 00 (Min 1000ft)
    WP_MIN_2000FT,  // 01 (Min 2000ft)
    WP_LANDED,      // 02 (Runway 1)
    WP_NOTHING,     // 03 (Lake 1)
    WP_NOTHING,     // 04 (Lake 2)
    WP_NOTHING,     // 05 (Lake 3)
    WP_LANDED,      // 06 (Runway 2)
    WP_UPSIDE_DOWN, // 07 (Runway 2)
    WP_LANDED,      // 08 (Runway 2)
    WP_MIN_3000FT,  // 09 (City 1)
    WP_MIN_3000FT,  // 10 (City 2)
    WP_MIN_3000FT,  // 11 (City 3)
    WP_LANDED,      // 12 (Runway 2)
    WP_MAX_125FT,   // 13 (Field 1)
    WP_MAX_125FT,   // 14 (Field 2)
    WP_MAX_125FT,   // 15 (Field 3)
    WP_LANDED,      // 16 (Runway 2)
};

const uint8_t kMissionWpBegin[kMissionCount] = {
    kWaypointMin1000ft,   // 01
    kWaypointLanded1,     // 02
    kWaypointMin2000ft,   // 03
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
    "CLIMB TO 2000FT AND LAND AGAIN.",     //
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
    "MAKE SURE TO STAY BELOW 125FT.",      //
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
    0x04, // 10
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

bool mission_completed[kMissionCount] = {
    false, // 01
    false, // 02
    false, // 03
    false, // 04
    false, // 05
    false, // 06
    false, // 07
    false, // 08
    false, // 09
    false, // 10
};
