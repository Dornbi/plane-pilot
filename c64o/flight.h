#ifndef FLIGHT_H
#define FLIGHT_H

#include <stdint.h>

#include "bool.h"
#include "mission.h"
#include "vec.h"

enum FlightCrashReason {
  FLIGHT_CRASH_NONE = 0,
  FLIGHT_CRASH_ROLL,
  FLIGHT_CRASH_INVERTED,
  FLIGHT_CRASH_PITCH_LOW,
  FLIGHT_CRASH_PITCH_HIGH,
  FLIGHT_CRASH_VSPEED,
  FLIGHT_CRASH_SPEED,
  FLIGHT_CRASH_GEAR,
};

extern bool flight_paused;
extern enum FlightCrashReason flight_crashed;

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

void flight_init();
void flight_init_alt();
void flight_init_from_mission(const mission_t *mission);

void flight_advance();
void flight_input(enum flight_input_t input);

#pragma compile("flight.cc")

#endif // FLIGHT_H
