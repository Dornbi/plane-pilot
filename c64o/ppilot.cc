#include "benchmark.h"
#include "cia.h"
#include "gfx.h"
#include "mem.h"
#include "menu.h"
#include "sim.h"

int main(void) {
  cia_init();
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
