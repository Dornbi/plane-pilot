#ifndef SCREEN_H
#define SCREEN_H

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

#pragma compile("screen.cc")

#endif
