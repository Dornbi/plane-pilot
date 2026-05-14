#ifndef MODEL_H
#define MODEL_H

#include "vec.h"
#include <stdint.h>

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
  MODEL_INPUT_MOVE_FORWARD,
  MODEL_INPUT_MOVE_BACKWARD,
};

// View matrix.
extern mat3_t model_cam;
extern bool model_paused;

// Roughly 24.8 fixed point in meters
extern int32_t model_eye_x;
extern int32_t model_eye_y;
extern int32_t model_eye_z;

void model_init();
void model_init_alt();
void model_reset_fuel();
void model_input(enum model_input_t input);
void model_update();

#pragma compile("model.cc")

#endif