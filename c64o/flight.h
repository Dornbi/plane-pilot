#ifndef FLIGHT_H
#define FLIGHT_H

#include <stdint.h>

#include "bool.h"
#include "mission.h"
#include "vec.h"

enum FlightStatus {
  FLIGHT_ONGOING = 0,
  FLIGHT_MISSION_COMPLETED,
  FLIGHT_CRASH_ROLL,
  FLIGHT_CRASH_INVERTED,
  FLIGHT_CRASH_PITCH_LOW,
  FLIGHT_CRASH_PITCH_HIGH,
  FLIGHT_CRASH_VSPEED,
  FLIGHT_CRASH_SPEED,
  FLIGHT_CRASH_GEAR,
  FLIGHT_CRASH_NOT_ON_RUNWAY,
};

// Message for a status: "WARNING: ..." on the approach, "CRASHED: ..." after
// touchdown, from one shared set of strings. The returned pointer is a shared
// buffer, valid until the next call.
const char *flight_status_text(enum FlightStatus status, bool crashed);

extern bool flight_paused;
extern enum FlightStatus flight_status;

extern mat3_t flight_cam;

extern int16_t flight_speed;
extern int16_t flight_vspeed;
extern uint8_t flight_throttle;
extern uint32_t flight_fuel;
extern uint8_t flight_flap;
extern uint8_t flight_gear;

// Navigation state
extern uint8_t flight_nav;
extern int16_t flight_nav_x;
extern int16_t flight_nav_y;
extern uint8_t flight_true_heading;
extern uint8_t flight_nav_heading;

// The most navpoints any mission produces. Walking kMissionWpBegin/End
// against the "skip a (0, 0) waypoint" rule, missions 06, 08 and 09 reach
// four and nothing reaches five, so this is a declared limit rather than an
// observation: flight_init_from_mission() clamps to it, and the map view
// only has digit stencils for '1'..'4'.
static const uint8_t kMaxNavPoints = 4;

// Navpoint positions in world coordinates, high byte = world unit. Read by
// the map view to place the navpoint digits; flight_num_nav_points is how
// many of the arrays are live.
extern uint16_t flight_nav_point_x[kMaxNavPoints];
extern uint16_t flight_nav_point_y[kMaxNavPoints];
extern uint8_t flight_num_nav_points;

// Recent flight path, in the map view's pixel space: 0..127 on both axes over
// the 128 x 128 map area, px across and py down, already rotated to N up and
// W left. Sampled once per flight_advance() and reset by
// flight_init_from_mission(), so the trail is per attempt and R wipes it.
//
// A sample is only appended when it differs from the previous one. At
// kMaxSpeed the aircraft covers about 15 m per step and the smallest map
// pixel is 256 m, so the position advances by at most one pixel per step and
// consecutive entries are always 4-neighbours. The stored points therefore
// already form a connected path: no line drawing, no interpolation, and no
// wrap-seam special case.
//
// At cruise a vertical pixel takes about 1.7 s, so 128 samples is roughly
// 3.5 minutes of flight, about one full traverse of the map.
static const uint8_t kFlightPathLen = 128;
extern uint8_t flight_path_px[kFlightPathLen];
extern uint8_t flight_path_py[kFlightPathLen];
// Entries 0 .. flight_path_count - 1 are live in both cases: before the ring
// wraps, count is the write position; after it, every slot is live.
extern uint8_t flight_path_count;

// Aircraft position in world coordinates (24.8 fixed point)
extern int32_t flight_eye_x;
extern int32_t flight_eye_y;
extern int32_t flight_eye_z;

enum flight_input_t {
  FLIGHT_INPUT_NONE,
  FLIGHT_INPUT_ROLL_LEFT,
  FLIGHT_INPUT_ROLL_RIGHT,
  FLIGHT_INPUT_PITCH_UP,
  FLIGHT_INPUT_PITCH_DOWN,
  FLIGHT_INPUT_YAW_LEFT,
  FLIGHT_INPUT_YAW_RIGHT,
  FLIGHT_INPUT_THROTTLE_UP,
  FLIGHT_INPUT_THROTTLE_DOWN,
  FLIGHT_INPUT_TOGGLE_FLAP,
  FLIGHT_INPUT_TOGGLE_GEAR,
  FLIGHT_INPUT_TOGGLE_NAV,
  FLIGHT_INPUT_MOVE_FORWARD,
  FLIGHT_INPUT_MOVE_BACKWARD,
  FLIGHT_INPUT_BRAKE,
};

// Mission waypoint tracking state
extern uint8_t flight_current_wp;
extern uint8_t flight_active_mission_idx;

// Free flight start state. Nothing in the game uses it any more - every
// flight now starts from a mission - but it is the fixture the host tests
// build their scenarios on, so it stays out of the C64 build only.
#ifndef __OSCAR64__
void flight_init();
#endif
void flight_init_from_mission(uint8_t mission_idx = 0);

void flight_advance();
void flight_input(enum flight_input_t input);

#pragma compile("flight.cc")

#endif // FLIGHT_H
