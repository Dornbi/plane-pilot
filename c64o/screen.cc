#include "screen.h"

#include <string.h>

#include "color.h"
#include "gfx.h"
#include "mem.h"
#include "sprites.h"
#include "vic.h"
#include "view.h"

// The routines below only run on screen transitions (menu, help, map) or at
// startup, never inside the per-frame simulation loop, so the outliner's
// size-for-a-JSR trade costs nothing that matters here. It stays off
// globally so the renderer and the raster IRQ handlers keep their
// straight-line code.
#pragma optimize(push, outline)


void screen_enter_static_mccm(void) {
  // Stop raster IRQs first so they don't run in the background or fire in the
  // middle of reconfiguring the screen buffer and VIC-II registers.
  gfx_stop_raster_irqs();
  mem_use_main_buffer();
  vic.spr_enable = 0x00;
  mem_set_mccm_mode();
}

void screen_begin_text_page(void) {
  screen_enter_static_mccm();

  vic.color_border = kColorBlack;
  vic.color_back = kColorWhite;

  memset(kScreenRamMain, 32, 1000);
  memset(kColorRam, kColorBlack, 1000);
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

#pragma optimize(pop)
