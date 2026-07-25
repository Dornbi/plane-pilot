#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>

#include "flight.h"
#include "mission.h"
#include "vec.h"

enum model_input_t {
  MODEL_INPUT_TOGGLE_NAV,
};

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