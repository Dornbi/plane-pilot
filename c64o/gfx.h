#ifndef GFX_H
#define GFX_H

#include "bool.h"
#include <stdint.h>

// Start of range with 16 characters used to draw single pixels.
static const uint8_t kGroundPointCharStart = 128;
static const uint8_t kColorPointCharStart = 144;
static const uint8_t kQuadCharStart = 160;

// Init the fixed characters.
void gfx_init_chars(void);

// Init raster interrupts.
void gfx_init_raster_irqs(void);

// Project and draw a single point using vec_project().
// @param vec_v
void gfx_project_and_draw(bool is_ground);

#pragma compile("gfx.cc")

#endif