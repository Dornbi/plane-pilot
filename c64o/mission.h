#ifndef MISSION_H
#define MISSION_H

#include <stdint.h>

enum MissionWaypointConstraint {
  WP_NOTHING = 0,
  WP_LANDED = 1,
  WP_MIN_3000FT = 2,
};

// Each mission leads through a number of "waypoints".
struct mission_waypoint_t {
  // Next navigation point. 0 = no restrictions.
  uint8_t x; // flight_eye_x = x << 16;
  uint8_t y; // flight_eye_y = y << 16 + 0x8000;
  MissionWaypointConstraint constraint;
  uint8_t max_time_10sec;
};

struct mission_t {
  // Next navigation point. 0 = no restrictions.
  uint8_t start_x;        // flight_eye_x = start_x << 16;
  uint8_t start_y;        // flight_eye_y = start_y << 16 + 0x8000;
  uint8_t start_z;        // flight_eye_z = start_z << 16
  uint8_t start_speed;    // flight_speed = start_speed << 4;
  uint8_t start_throttle; // flight_throttle = start_throttle;
  uint8_t start_fuel;     // flight_fuel = start_fuel << 12 - 1;

  uint8_t wind_x;
  uint8_t wind_y;
  uint8_t num_waypoints;
  uint8_t waypoints[6];
};

extern const char *const kMissionTitles[];
extern const char *const kMissionDesc[];
extern const char *const kMissionMsg[];
extern const mission_waypoint_t kMissionWaypoints[];
extern const mission_t kMissions[];
extern const uint8_t kWaypointDefault;
static const uint8_t kMissionCount = 4;

#pragma compile("mission.cc")

#endif