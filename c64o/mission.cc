#include "mission.h"

const uint8_t kWaypointDefault = 1;
static const uint8_t kWaypointMin1000ft = 0;
static const uint8_t kWaypointMin2000ft = 1;
static const uint8_t kWaypointLanded1 = 2;
static const uint8_t kWaypointLanded2 = 3;

const uint8_t kMissionWpX[kMissionWpCount] = {
    0x00, // 00 (Min 1000ft)
    0x00, // 01 (Min 2000ft)
    0x20, // 02 (Runway 1)
    0x60, // 03 (Runway 2)
};

const uint8_t kMissionWpY[kMissionWpCount] = {
    0x00, // 00 (Min 1000ft)
    0x00, // 01 (Min 2000ft)
    0x3F, // 02 (Runway 1)
    0xBF, // 03 (Runway 2)
};

const MissionWaypointConstraint kMissionWpConstraint[kMissionWpCount] = {
    WP_MIN_1000FT, // 00 (Min 1000ft)
    WP_MIN_2000FT, // 01 (Min 2000ft)
    WP_LANDED,     // 02 (Runway 1)
    WP_LANDED,     // 03 (Runway 2)
};

// clang-format off
const uint8_t kMissionWpBegin[kMissionCount] = {
    kWaypointMin1000ft,
    kWaypointLanded1,
    kWaypointMin2000ft,
    kWaypointLanded1,
    kWaypointLanded1};
const uint8_t kMissionWpEnd[kMissionCount] = {
    kWaypointMin1000ft + 1,
    kWaypointLanded1 + 1,
    kWaypointLanded1 + 1,
    kWaypointLanded1 + 1,
    kWaypointLanded2 + 1};

const char *const kMissionTitles[kMissionCount] = {
    "01 AIRBORNE",
    "02 GETTING DOWN",
    "03 SOLO FLIGHT",
    "04 FIND THE RUNWAY",
    "05 FERRY FLIGHT",
    /*
    "06 AREA PATROL",
    "07 AIRSHOW",
    "08 AERIAL RECON",
    "09 CROP DUSTER",
    "10 FUEL CHALLENGE",
    */
};

const char *const kMissionDesc[kMissionCount] = {
    // 01
    "HANDS ON THE THROTTLE! LIFT THE\n"
    "PLANE AND CLIMB TO 1000FT.",
    // 02
    "YOU ARE ON FINAL ON RUNWAY 1.\n"
    "LAND SAFELY!",
    // 03
    "PUT IT ALL TOGETHER! GET AIRBORNE,\n"
    "CLIMB TO 2000FT AND LAND AGAIN.",
    // 04
    "WHERE AM I? FIND THE RUNWAY\n"
    "AND GET ON THE GROUND.",
    // 05
    "NAVIGATE TO THE OTHER FIELD\n"
    "AND LAND SAFELY.",
    /**
    // 06
    "FLY TO ALL 3 NAV POINTS\n"
    "BEFORE TOUCHDOWN",
    // 07
    "PLEASE THE CROWD! FLY UPSIDE\n"
    "DOWN ABOVE THE RUNWAY.\n",
    // 08
    "FLY AT LEAST 5000FT OVER ALL\n"
    "CITIES ON THE MAP",
    // 09 
    "TOUCH ALL FIELDS ON THE MAP.\n"
    "MAKE SURE TO STAY BELOW 100FT.",
    // 10
    "YOU ARE ALMOST OUT OF FUEL.\n"
    "GET TO THE NEAREST FIELD AND LAND."
    */
};

const uint8_t kMissionStartX[kMissionCount] = {
    0x1C, // 01
    0x10, // 02
    0x1C, // 03
    0x10, // 04
    0x1C, // 05
};

const uint8_t kMissionStartY[kMissionCount] = {
    0x3F, // 01
    0x3F, // 02
    0x3F, // 03
    0x5F, // 04
    0x3F, // 05
};

const uint8_t kMissionStartZ[kMissionCount] = {
    0x00, // 01
    0x02, // 02
    0x00, // 03
    0x04, // 04
    0x00, // 05
};

const uint8_t kMissionStartSpeed[kMissionCount] = {
    0x00, // 01
    0x60, // 02
    0x00, // 03
    0x60, // 04
    0x00, // 05
};

const uint8_t kMissionStartThrottle[kMissionCount] = {
    0x00, // 01
    0x02, // 02
    0x00, // 03
    0x14, // 04
    0x00, // 05
};

const uint8_t kMissionStartFuel[kMissionCount] = {
    0x22, // 01
    0x22, // 02
    0x22, // 03
    0x22, // 04
    0x22, // 05
};

const uint8_t kMissionWindX[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
    0x00, // 04
    0x00, // 05
};

const uint8_t kMissionWindY[kMissionCount] = {
    0x00, // 01
    0x00, // 02
    0x00, // 03
    0x00, // 04
    0x00, // 05
};


bool mission_completed[kMissionCount] = {
    false,
    false,
    false,
    false,
    false,
};


/*
const mission_t kMissions[] = {
    // 01 Airborne
    {0x1C, // x
     0x3F, // y
     0x00, // z
     0x00, // speed
     0x00, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     1,    // num_waypoints
     {kWaypointMin1000ft}},
    // 02 Getting down
    {0x10, // x
     0x3F, // y
     0x02, // z
     0x60, // speed
     0x02, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     1,    // num_waypoints
     {kWaypointLanded1}},
    // 03 Solo Flight
    {0x1C, // x
     0x3F, // y
     0x00, // z
     0x00, // speed
     0x00, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     2,    // num_waypoints
     {kWaypointMin2000ft, kWaypointLanded1}},
    // 04 Where am I
    {0x10, // x
     0x5F, // y
     0x04, // z
     0x60, // speed
     0x14, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     1,    // num_waypoints
     {kWaypointLanded1}},
    // 05 Ferry Flight
    {0x1C, // x
     0x3F, // y
     0x00, // z
     0x00, // speed
     0x00, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     2,    // num_waypoints
     {kWaypointLanded1, kWaypointLanded2}},
    // 05 Area Patrol
    {0x1C, // x
     0x3F, // y
     0x00, // z
     0x00, // speed
     0x00, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     2,    // num_waypoints
     {kWaypointLanded1, kWaypointLanded2}},
};
*/
// clang-format on
