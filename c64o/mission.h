#ifndef MISSION_H
#define MISSION_H

#include <stdint.h>

#include "bool.h"

enum MissionWaypointConstraint {
  WP_NOTHING = 0,
  WP_LANDED = 1,
  WP_MIN_1000FT = 2,
  WP_MIN_3000FT = 3,
  // 0x008000, which is 250ft on the altitude scale where 1000ft is 0x020000.
  WP_MAX_250FT = 4,
  // Inverted, and low enough for it to be a pass rather than a distant roll:
  // both halves are in flight.cc, next to the altitude they share.
  WP_UPSIDE_DOWN = 5,
};

// Waypoints across all missions.
static const uint8_t kMissionWpCount = 16;
extern const uint8_t kMissionWpX[kMissionWpCount]; // flight_eye_x = x << 16;
extern const uint8_t
    kMissionWpY[kMissionWpCount]; // flight_eye_y = (y << 16) + 0x8000;
extern const MissionWaypointConstraint kMissionWpConstraint[kMissionWpCount];
extern const uint8_t kWaypointDefault;

static const uint8_t kMissionCount = 10;
extern const char *const kMissionTitles[kMissionCount];
extern const char *const kMissionDesc[kMissionCount];

// Missions.
// flight_eye_x = start_x << 16
extern const uint8_t kMissionStartX[kMissionCount];
// flight_eye_y = (start_y << 16) + 0x8000;
extern const uint8_t kMissionStartY[kMissionCount];
// flight_eye_z = start_z << 16
extern const uint8_t kMissionStartZ[kMissionCount];
// flight_speed = start_speed << 4;
extern const uint8_t kMissionStartSpeed[kMissionCount];
// flight_throttle = start_throttle;
extern const uint8_t kMissionStartThrottle[kMissionCount];
// flight_fuel = (start_fuel << 12) - 1;
extern const uint8_t kMissionStartFuel[kMissionCount];
extern const uint8_t kMissionWindX[kMissionCount];
extern const uint8_t kMissionWindY[kMissionCount];

extern const uint8_t kMissionWpBegin[kMissionCount];
extern const uint8_t kMissionWpEnd[kMissionCount];
extern bool mission_completed[kMissionCount];

#pragma compile("mission.cc")

#endif