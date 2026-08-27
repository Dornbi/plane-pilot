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

// --- Blanking a transition --------------------------------------------------
//
// Turns the display off by clearing $d011's DEN bit, so that the several
// frames a transition spends rewriting character RAM, screen RAM or the panel
// bitmap are not seen at all: with DEN clear the whole screen is border
// colour, and the border is black on every screen this program has. It is also
// faster than leaving the display on - a blanked screen has no badlines to
// steal cycles - so the transitions it covers are shorter as well as cleaner.
// map_enter() has done this since it was written; these two put the same trick
// on the other transitions.
//
// Two details that are easy to get wrong:
//
//   - The write is a constant, never a read-modify-write of $d011. Bit 7 of
//     that register reads back as the current raster line's bit 8, so
//     `&= ~0x10` would write that bit back as the raster compare's high bit
//     and move the split.
//   - Clearing DEN mid-frame does not re-close the vertical border flip flop,
//     which was already opened at raster $30; the VIC drops to idle state and
//     shows whatever byte is at $ffff for the rest of the frame. Blanking
//     therefore waits for the lower border first, which costs at most one
//     frame and is the difference between a clean cut and a flash.
//
// Only safe with the raster interrupts stopped - the split rewrites $d011
// three times a frame - which every caller has already done.
//
// Only the static text pages pair blank with unblank. The simulation does not
// need to: gfx_init_raster_irqs() restores mem_den and the split's next $d011
// write turns the display back on, which is exactly the moment a finished
// screen exists to show.
void screen_blank(void);
void screen_unblank(void);

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
