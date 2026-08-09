#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

// Configures the VIC-II and screen buffer for a static screen: stops raster
// IRQs, disables sprites, switches to the single (main) screen buffer, and
// enables multicolor character mode. Used by screens with no per-frame
// animation or double buffering (main menu, help). Callers still set their
// own colors and screen/color RAM content.
void screen_enter_static_mccm(void);

// Full setup for a black-on-white static text page: screen_enter_static_mccm()
// plus the border/background colors and a cleared screen and color RAM.
// Shared by the main menu and the help screen, which differ only in what they
// print afterwards.
void screen_begin_text_page(void);

// Restores the VIC-II, sprite, and raster-IRQ state used by the simulation
// screen (default view or debug view, whichever mem_debug_enabled selects).
// Used when returning to the simulation from something that reconfigured
// the VIC-II, such as the map view or the help screen.
void screen_restore_simulation(void);

// --- Transient notices on a text page ---------------------------------------
//
// A one-line message that clears itself after a few seconds, for the menu and
// the help screen. Both of them need it and neither can use msg.cc: that
// module writes row 0 of the *flight viewport*, restores the colour buffer
// behind itself and publishes a span so the sprite layer can dodge it, none of
// which exists on a text page.
//
// It lives here rather than in menu.cc or help.cc because it is the second
// thing those two screens have needed to share, and screen.cc is already what
// they share.
static const uint8_t kNoticeRow = 24;
static const uint8_t kNoticeCol = 15;

// 150 frames. The menu and help loops are driven by gfx_wait_vsync(), which is
// a true 50 Hz tick, so this is 3 seconds on PAL and 2.5 on NTSC. The flight
// loop's equivalent is msg.cc's MSG_DEFAULT_DURATION, which is 30 frames at
// ~10 Hz - the same 3 seconds by a different route.
static const uint8_t kNoticeFrames = 150;

// Print the notice and start its countdown. Re-showing while one is up
// restarts the clock, which is what repeated V presses should do.
void screen_notice(const char *text, uint8_t len);

// Call once per frame from a polling loop. Clears the notice when the
// countdown expires; does nothing the rest of the time.
void screen_notice_tick(void);

#pragma compile("screen.cc")

#endif
