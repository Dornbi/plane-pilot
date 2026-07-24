#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>

#include "mission.h"
#include "vec.h"

enum model_input_t {
  MODEL_INPUT_NONE,
  MODEL_INPUT_ROLL_LEFT,
  MODEL_INPUT_ROLL_RIGHT,
  MODEL_INPUT_PITCH_UP,
  MODEL_INPUT_PITCH_DOWN,
  MODEL_INPUT_YAW_LEFT,
  MODEL_INPUT_YAW_RIGHT,
  MODEL_INPUT_THROTTLE_UP,
  MODEL_INPUT_THROTTLE_DOWN,
  MODEL_INPUT_TOGGLE_NAV,
  MODEL_INPUT_TOGGLE_FLAP,
  MODEL_INPUT_TOGGLE_GEAR,
  MODEL_INPUT_MOVE_FORWARD,
  MODEL_INPUT_MOVE_BACKWARD,
};

extern mat3_t model_cam;
extern bool model_paused;

void model_init_from_mission(const mission_t *mission);

// Provides input to the model.
void model_input(enum model_input_t input);

// Advances the model simulation.
void model_advance();

// Updates instruments state.
void model_update_instruments();

// Prints debug info.
void model_maybe_print_debug();

#pragma compile("model.cc")

#endif