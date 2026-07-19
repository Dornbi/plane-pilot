#include "mission.h"

#include "print.h"

const char *const kMissionTitles[] = {
    "TAKEOFF",
    "LANDING",
};

const char *const kMissionDesc[] = {
    "HANDS ON THE THROTTLE, LIFT THE\n"
    "PLANE, AND CLIMB TO 1000FT.",
    "YOU ARE ON FINAL ON RUNWAY 1.\n"
    "LAND SAFELY!",
};

const mission_waypoint_t kMissionWaypoints[] = {
    {0x00, 0x00, WP_MIN_1000FT, 0x00},
    {0x18, 0x3F, WP_LANDED, 0x00},
};

const mission_t kMissions[] = {
    // Takeoff mission
    {0x18, // x
     0x3F, // y
     0x00, // z
     0x00, // speed
     0x00, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     1,    // num_waypoints
     {0}},
    // Landing mission
    {0x14, // x
     0x3F, // y
     0x01, // z
     0x86, // speed
     0x14, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     1,    // num_waypoints
     {0}},
};
