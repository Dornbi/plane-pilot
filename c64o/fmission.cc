#include "mission.h"

// fpilot's missions. The tables are pmission.cc's, with the same names and the
// same encoding - see mission.h - so everything that reads them is shared.
//
// One mission for now. It flies with the ppilot aircraft and none of the
// combat yet, so it is built only out of constraints flight.cc already
// checks: get to the city above 3000ft, then come home.

const uint8_t kWaypointDefault = 1;
static const uint8_t kWaypointCity1 = 0;
static const uint8_t kWaypointLanded1 = 1;

const uint8_t kMissionWpX[kMissionWpCount] = {
    0x10, // 00 (City 1)
    0x20, // 01 (Runway 1)
};

const uint8_t kMissionWpY[kMissionWpCount] = {
    0x68, // 00 (City 1)
    0x3F, // 01 (Runway 1)
};

const MissionWaypointConstraint kMissionWpConstraint[kMissionWpCount] = {
    WP_MIN_3000FT, // 00 (City 1)
    WP_LANDED,     // 01 (Runway 1)
};

const uint8_t kMissionWpBegin[kMissionCount] = {
    kWaypointCity1, // 01
};
const uint8_t kMissionWpEnd[kMissionCount] = {
    kWaypointLanded1 + 1, // 01
};

const char *const kMissionTitles[kMissionCount] = {
    "01 SCRAMBLE", // 01
};

const char *const kMissionDesc[kMissionCount] = {
    "BANDITS OVER THE CITY! TAKE OFF,\n" // 01
    "PATROL IT ABOVE 3000FT AND LAND.",  //
};

// On runway 1, engine at idle, as ppilot's mission 01 starts.
const uint8_t kMissionStartX[kMissionCount] = {
    0x1C, // 01
};

const uint8_t kMissionStartY[kMissionCount] = {
    0x3F, // 01
};

const uint8_t kMissionStartZ[kMissionCount] = {
    0x00, // 01
};

const uint8_t kMissionStartSpeed[kMissionCount] = {
    0x00, // 01
};

const uint8_t kMissionStartThrottle[kMissionCount] = {
    0x00, // 01
};

const uint8_t kMissionStartFuel[kMissionCount] = {
    0x22, // 01
};

const uint8_t kMissionWindX[kMissionCount] = {
    0x00, // 01
};

const uint8_t kMissionWindY[kMissionCount] = {
    0x00, // 01
};

bool mission_completed[kMissionCount] = {
    false, // 01
};
