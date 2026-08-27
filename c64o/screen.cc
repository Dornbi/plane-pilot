#include "screen.h"

#include <string.h>

#include "color.h"
#include "gfx.h"
#include "mem.h"
#include "print.h"
#include "sprites.h"
#include "vic.h"
#include "view.h"

// The routines below only run on screen transitions (menu, help, map) or at
// startup, never inside the per-frame simulation loop, so the outliner's
// size-for-a-JSR trade costs nothing that matters here. It stays off
// globally so the renderer and the raster IRQ handlers keep their
// straight-line code.
#pragma optimize(push, outline)


// __noinline, and the $d011 store written in assembler rather than through
// vic.ctrl1: both are load-bearing. oscar64 orders volatile accesses against
// each other, but a memset is not a volatile access, so with these inlined it
// schedules the very writes that are supposed to bracket the rebuilding
// straight through it. It was seen hoisting screen_unblank()'s $d011 store
// above the two 1000-byte memsets in screen_begin_text_page(), which puts back
// exactly the artifact this is here to remove - and the same trap is set in
// _enter_simulation() and map_exit(), where a blank has to stay above a
// memset or an LZO expansion. A call boundary and an asm block are what hold
// the writes where they were written.
__noinline void screen_blank(void) {
  if (mem_den == 0) {
    // Already blanked, and blanks nest: map_exit() blanks before it rebuilds
    // the charset and then calls screen_restore_simulation(), which blanks
    // again. Returning here is not just tidiness - it is what keeps the second
    // call from spending another frame in gfx_wait_vsync().
    return;
  }
  // In the lower border, so the frame that goes dark goes dark whole. See
  // screen.h for what a mid-frame DEN clear does instead.
  gfx_wait_vsync();
  mem_den = 0x00;
  __asm {
    lda #$0b;
    sta $d011;
  }
}

__noinline void screen_unblank(void) {
  mem_den = 0x10;
  __asm {
    lda #$1b;
    sta $d011;
  }
}

void screen_enter_static_mccm(void) {
  // Stop raster IRQs first so they don't run in the background or fire in the
  // middle of reconfiguring the screen buffer and VIC-II registers.
  gfx_stop_raster_irqs();
  // Then blank, before any of that reconfiguring can be seen: what is on
  // screen is still the simulation's, and screen_begin_text_page()'s two
  // memsets below take a couple of frames to replace it. mem_set_mccm_mode()
  // at the end of this function composes mem_den in, so it sets the mode
  // without switching the display back on.
  screen_blank();
  mem_use_main_buffer();
  vic.spr_enable = 0x00;
  mem_set_mccm_mode();
}

// --- Transient notices ------------------------------------------------------

static uint8_t _notice_frames;
static uint8_t _notice_len;

void screen_notice(const char *text, uint8_t len) {
  if (_notice_len > len) {
    memset(mem_screen_row_ptrs[kNoticeRow] + kNoticeCol, ' ', _notice_len);
  }
  print_str(kNoticeRow, kNoticeCol, text, len);
  _notice_len = len;
  _notice_frames = kNoticeFrames;
}

void screen_notice_tick(void) {
  if (_notice_frames != 0 && --_notice_frames == 0) {
    memset(mem_screen_row_ptrs[kNoticeRow] + kNoticeCol, ' ', _notice_len);
  }
}

void screen_begin_text_page(void) {
  screen_enter_static_mccm();

  vic.color_border = kColorBlack;
  vic.color_back = kColorWhite;

  memset(kScreenRamMain, 32, 1000);
  memset(kColorRam, kColorBlack, 1000);

  // The page just went blank, so any notice on it went with it. Dropping the
  // countdown here keeps the state and the screen agreeing; without it a
  // notice shown just before a screen change would later blank a cell that
  // something else had since drawn into.
  _notice_frames = 0;

  // The page is now a cleared, correctly coloured text screen, so it can be
  // shown. Callers print onto it afterwards, one cell at a time, which needs
  // no hiding. This is the only unblank in the program that is not the raster
  // split's - these pages have no split to restart.
  screen_unblank();
}

void screen_restore_simulation(void) {
  // view_refresh_panel() below re-expands the panel bitmap, which is a few
  // frames of half-built graphics in plain sight otherwise. No unblank here:
  // gfx_init_raster_irqs() is the unblank, and it is already in the right
  // place - last, once there is a finished screen to show.
  screen_blank();
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
