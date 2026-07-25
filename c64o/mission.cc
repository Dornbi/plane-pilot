#include "mission.h"

const char *const kMissionTitles[] = {
    "01 TAKEOFF",
    "02 LANDING",
    "03 SOLO FLIGHT",
    "04 FIND THE RUNWAY",
};

// clang-format off
const char *const kMissionDesc[] = {
    "HANDS ON THE THROTTLE! LIFT THE\n"
    "PLANE AND CLIMB TO 3000FT.",
    "YOU ARE ON FINAL ON RUNWAY 1.\n"
    "LAND SAFELY!",
    "PUT IT ALL TOGETHER! GET AIRBORNE,\n"
    "CLIMB TO 3000 FT AND LAND AGAIN.",
    "WHERE AM I? FIND THE RUNWAY\n"
    "AND GET ON THE GROUND.",
};
// clang-format on

const mission_waypoint_t kMissionWaypoints[] = {
    {0x18, 0x3F, WP_LANDED, 0x00},
    {0x00, 0x00, WP_MIN_3000FT, 0x00},
};

const uint8_t kWaypointDefault = 0;
static const uint8_t kWaypointLanded = 0;
static const uint8_t kWaypointMin3000ft = 1;

const mission_t kMissions[] = {
    // 01 Takeoff
    {0x1C, // x
     0x3F, // y
     0x00, // z
     0x00, // speed
     0x00, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     1,    // num_waypoints
     {kWaypointMin3000ft}},
    // 02 Landing
    {0x10, // x
     0x3F, // y
     0x02, // z
     0x60, // speed
     0x02, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     0,    // num_waypoints
     {kWaypointLanded}},
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
     {kWaypointMin3000ft, kWaypointLanded}},
    // 03 Where am I
    {0x10, // x
     0x5F, // y
     0x04, // z
     0x60, // speed
     0x14, // throttle
     0x22, // fuel
     0x00, // wind_x
     0x00, // wind_y
     1,    // num_waypoints
     {kWaypointLanded}},
};
