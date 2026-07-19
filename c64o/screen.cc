#include "screen.h"

#include "gfx.h"
#include "mem.h"
#include "sprites.h"
#include "vic.h"
#include "view.h"

void screen_enter_static_mccm(void) {
  gfx_stop_raster_irqs();
  vic.spr_enable = 0x00;
  mem_use_main_buffer();
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
