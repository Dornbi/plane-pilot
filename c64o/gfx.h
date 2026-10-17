#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#include "bool.h"

// Start of character range where uncompression starts.
static const uint8_t kGfxCharStart = 32;

// Start of character range with 16 characters used to draw single pixels.
static const uint8_t kGfxQuadGroundSparse = 128;
static const uint8_t kGfxQuadGround = 144;
static const uint8_t kGfxQuad11Sparse = 160;
static const uint8_t kGfxQuad11 = 176;
static const uint8_t kGfxQuadMixedSparse = 192;
static const uint8_t kGfxQuadMixed = 208;
static const uint8_t kGfxGroundPoints = 224;
static const uint8_t kGfxColorPoints = 240;

// Init the fixed characters.
void gfx_init_chars(void);

// Init raster interrupts.
void gfx_init_raster_irqs(void);

// Stop raster interrupts.
void gfx_stop_raster_irqs(void);

// Blocks until one vertical blank has passed: waits for the raster to reach
// line 255, then waits for it to leave line 255 again.
//
// Only for the screens that do not render every frame - menu, help, and the
// map - where it is a 50 Hz pacing tick and nothing more. All three run with
// the raster interrupts stopped, which this relies on: a handler that
// straddled line 255 would make the first loop miss the line and cost a whole
// frame. The simulation keeps its interrupts, so it uses the window below.
void gfx_wait_vsync(void);

// Blocks until the raster is somewhere it is safe to swap the screen buffers
// and rewrite the viewport's color RAM. Unlike gfx_wait_vsync() this is a
// window, not a line, so it usually returns without waiting at all and can
// never be stepped over by a long interrupt handler. See gfx.cc for the two
// deadlines that define it. Interrupt-safe; used by mem_switch_buffer().
void gfx_wait_flip_window(void);

// Project and draw a single point using vec_project().
// If color < 8, it uses kGfxColorPoints and sets the color ram.
// Otherwise it uses kGfxGroundPoints.
// @param vec_v
void gfx_project_and_draw(uint8_t color);

// Update the heading bitmap on the instrument panel.
// Skips the copy when the heading is unchanged since the last draw.
void gfx_update_heading_bitmap(uint8_t heading);

// Forces the next gfx_update_heading_bitmap to redraw. Must be called
// whenever the panel bitmap is re-expanded over the heading strip.
void gfx_invalidate_heading_bitmap(void);

// Toggle various indicators;
static const uint8_t kGfxNumNavpoints = 2;
void gfx_update_nav_heading(uint8_t heading);
void gfx_update_stall(bool stall);
void gfx_update_flap(bool flap);
void gfx_update_gear(bool gear);

#pragma compile("gfx.cc")

#endif