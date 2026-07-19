#include "screen.h"

#include "gfx.h"
#include "mem.h"
#include "sprites.h"
#include "vic.h"
#include "view.h"

void screen_enter_static_mccm(void) {
  // mem_use_main_buffer() ends with its own sei/cli pair (it's normally used
  // mid-simulation, where raster IRQs are expected to keep running). Calling
  // it after gfx_stop_raster_irqs() would let that trailing cli re-enable
  // interrupts, and since rirq_stop() never tears down the raster-IRQ
  // schedule (it only sets the interrupt-disable flag), that would leave the
  // simulation's panel/terrain raster split running in the background,
  // fighting this screen's own register writes every frame. So
  // gfx_stop_raster_irqs() must run last, once nothing after it can flip
  // interrupts back on.
  mem_use_main_buffer();
  gfx_stop_raster_irqs();
  vic.spr_enable = 0x00;
  mem_set_mccm_mode();
}

void screen_restore_simulation(void) {
  if (mem_debug_enabled) {
    mem_switch_debug(true);
  } else {
    mem_init_mccm();
    view_refresh_panel();
  }
  gfx_init_raster_irqs();
  sprites_init();
}
