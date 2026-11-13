#ifndef RENDER_ASM_H
#define RENDER_ASM_H

#include <stdint.h>

#include "bool.h"
#include "color.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "chardefs.h"
#include "mem.h"
#include "render.h"
#include "roll.h"
#else
static const uint8_t kCharSolid11 = 0;
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
extern bool mem_using_alt_buffer;
extern int8_t render_cx_chars;
extern int8_t render_cy_chars;
extern bool render_alt_box;
extern int8_t render_alt_shift_x;
extern int8_t render_alt_shift_y;
extern uint8_t render_sky_rows;
extern int8_t roll_dx;
extern int8_t roll_dy;
extern uint8_t roll_period;
extern bool roll_x_is_major;
extern int8_t roll_ex_dy[8];
extern int8_t roll_ey_dx[8];
#endif

// The assembly half of render.cc, in render_asm.cc: Pass 139's horizon lattice
// snap (_render_snap_center_asm) and self-modifying 14-row sky/ground span
// sled (_render_flush_span). render.cc holds the C equivalents (#ifndef
// __OSCAR64__) for host builds and tests.

// Sky rows filled contiguously from row 0 so far this frame.
extern uint8_t _render_sky_rows;

// Picks the nearest horizon lattice point after _pull_to_center(), updating
// render_alt_box, render_alt_shift_x/y, and render_cx/cy_chars. On __OSCAR64__,
// inputs arrive in vec_proj_mul_a/b; on host, in render_px/py_pixels.
void _render_snap_center_asm(void);

// Fills whole viewport rows [start_row, end_row) with `ch` (`ch & 1` is a
// no-op), updating _render_sky_rows and colour RAM when `ch == kCharSolid11`.
void _render_flush_span(uint8_t ch, uint8_t start_row, uint8_t end_row);

#ifdef __TARGET_TEST__
#pragma compile("render_asm.cc")
#endif

#endif
