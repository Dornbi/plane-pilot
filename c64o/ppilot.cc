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
  bm_init();

  mem_init();

  // What the probe found, handed to the model: it scales its step down and its
  // rate up by the same power of two, so this buys smoothness and never speed.
  // docs/flight.md §8, docs/supercpu.md.
  //
  // *After* mem_init(), and that is not cosmetic. Until mem_init() runs
  // mmap_set(MMAP_NO_ROM) the machine is in its power-on memory map, so
  // $A000-$BFFF reads BASIC ROM and $E000-$FFFF reads KERNAL. Writes still go
  // to RAM, so cpu_probe() above stores the shift correctly - but a *read* of
  // any global the linker happened to place in those windows comes back as a
  // ROM byte instead.
  //
  // That is exactly what used to happen here. cpu_step_shift landed at $AC9D
  // in one build, this line read $A5 out of BASIC ROM, and the model got a
  // shift of 165: kFlightSubstepMask became 31 and kFlightFramesPerStep 1,
  // which left sim_run()'s catch-up loop spinning on flight_advance() forever
  // while the raster interrupt carried on. The screen froze with the engine
  // still audible, and it only reproduced when an unrelated change moved the
  // variable into the ROM window. tools/check_rom_window.py now fails the
  // build if anything before mem_init() reads from there.
  flight_set_step_shift(cpu_step_shift);
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
