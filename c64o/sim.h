#ifndef SIM_H
#define SIM_H

#include <stdint.h>

// Runs the flight simulation screen for the given mission: the default 3D
// view, the debug view (D), and the map view (M), plus the help overlay
// (H). Returns when the player quits back to the main menu (Q).
void sim_run(uint8_t selected_mission);

#pragma compile("sim.cc")

#endif
