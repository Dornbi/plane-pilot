#ifndef POLY_ASM_H
#define POLY_ASM_H

#include <stdint.h>

#include "bool.h"
#include "fmath.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "chardefs.h"
#include "mem.h"
#else
static const uint8_t kScreenWidth = 40;
static const uint8_t kViewportWidth = 40;
static const uint8_t kCharSolidGround = 128;
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
#endif

// The assembly half of poly.cc, in poly_asm.cc: Pass 134's 3D frustum early
// rejection (_poly_frustum_all_out) and Pass 137's quad-cell scanline pass
// (_poly_scan_lines2). poly.cc holds the C equivalents (#ifndef __OSCAR64__)
// for the host and host tests.

// Working buffers defined in poly.cc (in scratch/bss2).
extern vec3_t _poly_clip3_buf[];
extern uint8_t _poly_min_x[2 * kViewportHeight];
extern uint8_t _poly_max_x[2 * kViewportHeight];

// Returns true when every vertex in _poly_clip3_buf[0 .. end3) (byte offset)
// lies past the same frustum plane and the polygon can be rejected before
// projection.
bool _poly_frustum_all_out(uint8_t end3);

// Fast core fill helpers shared by _poly_scan_lines2() in poly_asm.cc (6502)
// and poly.cc (host).
static inline void _poly_set_line(uint8_t *dst, uint8_t val, int8_t cnt) {
  for (int8_t i = cnt - 1; i >= 0; --i) {
    if (dst[i] < kCharSolidGround) {
      continue;
    }
    dst[i] = val;
  }
}

static inline void _poly_set_color_line(uint8_t *dst, uint8_t val,
                                        uint8_t *color_dst, uint8_t color,
                                        int8_t cnt) {
  for (int8_t i = cnt - 1; i >= 0; --i) {
    if (dst[i] < kCharSolidGround) {
      continue;
    }
    dst[i] = val;
    color_dst[i] = color;
  }
}

// Rasterises polygon scanlines in character rows [py_start, py_end] (inclusive)
// from _poly_min_x/_poly_max_x.
void _poly_scan_lines2(uint8_t fill_char_start_idx, uint8_t color,
                       uint8_t py_start, uint8_t py_end);

#ifdef __TARGET_TEST__
#pragma compile("poly_asm.cc")
#endif

#endif
