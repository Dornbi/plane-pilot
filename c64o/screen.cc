#include "screen.h"

#include "gfx.h"
#include "mem.h"
#include "sprites.h"
#include "vic.h"
#include "view.h"

void screen_enter_static_mccm(void) {
  // Stop raster IRQs first so they don't run in the background or fire in the
  // middle of reconfiguring the screen buffer and VIC-II registers.
  gfx_stop_raster_irqs();
  mem_use_main_buffer();
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
