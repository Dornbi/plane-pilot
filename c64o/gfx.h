#ifndef GFX_H
#define GFX_H

#include <stdint.h>

// Start of range with 16 characters used to draw single pixels.
static const uint8_t kSinglePointCharStart = 128;

// Init raster interrupts.
void gfx_init(void);

// Draw a single point from the pre-initialized character set.
void gfx_draw_single_point(int16_t px, int16_t py);

// Project and draw a single point using vec_project().
// @param vec_v
void gfx_project_and_draw(void);

#pragma compile("gfx.cc")

#endif