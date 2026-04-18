#ifndef RENDER_H
#define RENDER_H

#include "bool.h"
#include <stdint.h>

extern int16_t render_cx_pixels;
extern int16_t render_cy_pixels;
extern int16_t render_px_pixels;
extern int16_t render_py_pixels;
extern int8_t render_cx_chars;
extern int8_t render_cy_chars;
extern bool render_alt_box;
extern int8_t render_alt_shift_x;
extern int8_t render_alt_shift_y;

// Snaps (cx, cy) to the nearest supported center (Main or Alt lattice).
// Computes character coordinates and updates render_alt_box.
//
// @param render_cx_pixels
// @param render_cy_pixels
// @result render_cx_chars
// @result render_cy_chars
// @result render_alt_box
// @result render_alt_shift_x
// @result render_alt_shift_y
void render_snap_center_chars();

// Fills the viewport with Ground/Sky solid colors.
// @param roll_state.angle
// @param render_cx_chars
// @param render_cy_chars
// @param render_alt_box
// @param render_alt_shift_x
// @param render_alt_shift_y
void render_fill_sky_ground();

#pragma compile("render.cc")

#endif // RENDER_H
