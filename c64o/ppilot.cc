#include "benchmark.h"
#include "cia.h"
#include "cpu.h"
#include "flight.h"
#include "gfx.h"
#include "mem.h"
#include "menu.h"
#include "sim.h"

int main(void) {
  cia_init();
  // Before bm_init(), which takes CIA2's timers for itself, and before any
  // raster interrupt is armed: the probe is timed and would count a handler.
  cpu_probe();
  // What the probe found, handed to the model: it scales its step down and its
  // rate up by the same power of two, so this buys smoothness and never speed.
  // docs/flight.md §8, docs/supercpu.md.
  flight_set_step_shift(cpu_step_shift);
  bm_init();

  mem_init();
  mem_switch_buffer();
  mem_clear_screen();
  mem_switch_buffer();
  mem_clear_screen();

  gfx_init_chars();

  while (1) {
    uint8_t selected_mission = menu_run();
    sim_run(selected_mission);
  }
}
