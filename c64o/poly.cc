#include "poly.h"

#include "benchmark.h"
#include "fmath.h"
#include "mem.h"
#include <stdint.h>

// Buffers to store the left-most and right-most X bounds for each scanline
static uint8_t _min_x[kViewportHeight];
static uint8_t _max_x[kViewportHeight];

// Initialize the scanline buffers
static inline void _clear_buffers() {
#pragma unroll(full)
  for (uint8_t i = 0; i < kViewportHeight; i++) {
    _min_x[i] = kScreenWidth; // Set to out-of-bounds max
    _max_x[i] = 0;            // Set to out-of-bounds min
  }
}

extern const uint8_t vec_recip_lut[128];

static inline uint16_t _div(uint8_t a, uint8_t b) {
  if (a == 0) {
    return 0;
  }

  uint8_t b_norm = b;
  uint8_t shift = 0;
  while (b_norm < 128) {
    b_norm <<= 1;
    shift++;
  }
  uint16_t b_recip = 0x100 + vec_recip_lut[b_norm - 128];
  uint16_t p = (uint16_t)a * b_recip;
  return p >> (8 - shift);
}

static void _trace_edge_dda(int8_t x1, int8_t y1, int8_t x2, int8_t y2) {
  if (y1 == y2) {
    return; // Skip horizontal edges
  }

  // Enforce top-to-bottom order to guarantee we always step Y positively
  if (y1 > y2) {
    int8_t tmp_x = x1;
    x1 = x2;
    x2 = tmp_x;

    int8_t tmp_y = y1;
    y1 = y2;
    y2 = tmp_y;
  }

  uint8_t dy = y2 - y1;
  int8_t dx = x2 - x1;
  uint8_t abs_dx = _abs8(dx);
  int16_t dx_fp = _div(abs_dx, dy);

  if (dx < 0) {
    dx_fp = -dx_fp;
  }

  int16_t x_fp = ((int16_t)x1 << 8) + 0x80; // +0x80 for half-pixel rounding

  for (int8_t y = y1; y <= y2; y++) {
    int8_t start_x = x_fp >> 8;
    int8_t end_x = start_x;
    if (y < y2) {
      end_x = (x_fp + dx_fp) >> 8;
    }

    // Update scanline bounds
    if (start_x < _min_x[y]) {
      _min_x[y] = start_x;
    }
    if (start_x > _max_x[y]) {
      _max_x[y] = start_x;
    }
    if (end_x < _min_x[y]) {
      _min_x[y] = end_x;
    }
    if (end_x > _max_x[y]) {
      _max_x[y] = end_x;
    }
    x_fp += dx_fp;
  }
}

static void _trace_edge_bresenham(int8_t x1, int8_t y1, int8_t x2, int8_t y2) {
  if (y1 == y2) {
    return; // Skip horizontal edges
  }

  // Enforce top-to-bottom order to guarantee we always step Y positively
  if (y1 > y2) {
    int8_t tmp_x = x1;
    x1 = x2;
    x2 = tmp_x;

    int8_t tmp_y = y1;
    y1 = y2;
    y2 = tmp_y;
  }

  int8_t dx = x2 - x1;
  int8_t sx = 1;
  if (dx < 0) {
    sx = -1;
    dx = -dx;
  }

  int8_t dy = y2 - y1;
  int8_t err = dy >> 1; // Half-pixel offset for rounding
  int8_t x = x1;

  for (int8_t y = y1; y <= y2; y++) {
    int8_t start_x = x;

    // Step X if we haven't reached the last scanline
    if (y < y2) {
      err += dx;
      while (err >= dy) {
        x += sx;
        err -= dy;
      }
    }

    int8_t end_x = x;

    // Update scanline bounds (eliminating inner-loop viewport bounds checks)
    if (sx > 0) {
      if (start_x < _min_x[y])
        _min_x[y] = start_x;
      if (end_x > _max_x[y])
        _max_x[y] = end_x;
    } else {
      if (end_x < _min_x[y])
        _min_x[y] = end_x;
      if (start_x > _max_x[y])
        _max_x[y] = start_x;
    }
  }
}

static inline void _fill_line(uint8_t *dst, uint8_t val, int8_t cnt) {
  for (int8_t i = cnt - 1; i >= 0; --i) {
    dst[i] = val;
  }
}

// Fill the polygon using the traced edges
void fill_poly(const vertex_t *vertices, uint8_t num_vertices,
               uint8_t fill_char_start_idx) {
  if (num_vertices < 3) {
    return; // A polygon needs at least 3 vertices
  }

  bm_start();
  _clear_buffers();
  bm_end(880, SCREEN_STR("CLEAR:"));

  // 1. Trace all edges
  bm_start();
  for (uint8_t i = 0; i < num_vertices; i++) {
    uint8_t next = (i + 1);
    if (next == num_vertices) {
      next = 0; // Wrap around to the first vertex
    }
    _trace_edge_bresenham(vertices[i].x, vertices[i].y, vertices[next].x,
                          vertices[next].y);
  }
  bm_end(920, SCREEN_STR("TRACE:"));

  // 2. Draw the scanlines directly to Screen RAM
  bm_start();
  for (int8_t y = 0; y < kViewportHeight; y++) {
    int8_t left = _max8(_min_x[y], 0);
    int8_t right = _min8(_max_x[y], kScreenWidth - 1);
    _fill_line(mem_screen_row_ptrs[y] + left, fill_char_start_idx + 15,
               right - left + 1);
  }
  bm_end(960, SCREEN_STR("SCAN: "));
}
