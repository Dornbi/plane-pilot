#ifndef TITLE_H
#define TITLE_H

// --- The title screen flyby -------------------------------------------------
//
// An aircraft crosses the menu page every ten seconds or so, entering from the
// top right and diving out through the bottom of the screen. It is four
// multicolour sprites in a 2 x 2 block - 48 x 42 screen pixels, one drawing
// across four hardware sprites - and it is the only thing in this program that
// uses multicolour sprites at all. docs/sprite_objects.md §0 forbids them for
// world objects, and every reason it gives is about the flight viewport:
// nothing here dithers, nothing here is projected, and there is exactly one
// object rather than eight competing for the same eight slots.
//
// It also owns hardware sprites 0-3 outright while it runs, which it may
// because the menu page has no raster split and no instrument panel - the
// simulation's own claim on those indices is over by the time menu_run() is
// reached, and sprites_init() takes them back on the way out.
//
// The bitmaps live at $CF00 (mem.h kTitleSpriteData), which is not part of the
// $D400 sprite blob and is expanded separately by title_arm().

// Expands the bitmaps, programs everything about the four sprites that does
// not change per frame, and arms the timer with the flyby switched off.
//
// Call once per entry to the menu page, after the page has been painted:
// screen_begin_text_page() clears $D015, so anything set before it is lost.
void title_arm(void);

// One frame of the flyby. Call once per menu loop iteration, as early in the
// iteration as possible - the sprite registers are written straight from the
// main line with no raster interrupt to hide behind, so the safe moment is
// immediately after gfx_wait_vsync() returns, while the raster is still above
// the first line a sprite can start on.
void title_tick(void);

#pragma compile("title.cc")

#endif
