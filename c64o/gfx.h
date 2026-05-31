#ifndef GFX_H
#define GFX_H

#include "bool.h"
#include <stdint.h>

// Start of range with 16 characters used to draw single pixels.
static const uint8_t kGfxCharStart = 128;

static const uint8_t kGfxQuadGroundSparse = 128;
static const uint8_t kGfxQuadGround = 144;
static const uint8_t kGfxQuad11 = 160;
static const uint8_t kGfxGroundPoints = 176;
static const uint8_t kGfxColorPoints = 192;

// Init the fixed characters.
void gfx_init_chars(void);

// Init raster interrupts.
void gfx_init_raster_irqs(void);

// Project and draw a single point using vec_project().
// If color < 8, it uses kGfxColorPoints and sets the color ram.
// Otherwise it uses kGfxGroundPoints.
// @param vec_v
void gfx_project_and_draw(uint8_t color);

#pragma compile("gfx.cc")

#endif