#include "poly.h"

#include "benchmark.h"
#include "fmath.h"
#include "mem.h"
#include "print.h"
#include <stdint.h>

// Buffers to store the left-most and right-most X bounds for each scanline
static uint8_t _min_x[2 * kViewportHeight];
static uint8_t _max_x[2 * kViewportHeight];

// Initialize the scanline buffers
static inline void _clear_buffers() {
#pragma unroll(full)
  for (uint8_t i = 0; i < 2 * kViewportHeight; i++) {
    _min_x[i] = kScreenWidth * 2; // Set to out-of-bounds max
    _max_x[i] = 0;                // Set to out-of-bounds min
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

static void _trace_edge_dda(int8_t x1, uint8_t y1, int8_t x2, uint8_t y2) {
  if (y1 == y2) {
    return; // Skip horizontal edges
  }

  // Enforce top-to-bottom order to guarantee we always step Y positively
  if (y1 > y2) {
    int8_t tmp_x = x1;
    x1 = x2;
    x2 = tmp_x;

    uint8_t tmp_y = y1;
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

  for (uint8_t y = y1; y <= y2; y++) {
    int8_t start_x = x_fp >> 8;
    int8_t end_x = start_x;
    if (y < y2) {
      if (abs_dx > dy) {
        end_x = (x_fp + dx_fp) >> 8;
      }
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

static void _trace_edge_bresenham(int8_t x1, uint8_t y1, int8_t x2,
                                  uint8_t y2) {
  if (y1 == y2) {
    return; // Skip horizontal edges
  }

  // Enforce top-to-bottom order to guarantee we always step Y positively
  if (y1 > y2) {
    int8_t tmp_x = x1;
    x1 = x2;
    x2 = tmp_x;

    uint8_t tmp_y = y1;
    y1 = y2;
    y2 = tmp_y;
  }

  uint8_t dy = y2 - y1;
  uint8_t err = 0; // 0 removes half-pixel offset to prevent wide top lines,
                   // uint8 prevents overflow

  if (x2 > x1) {
    uint8_t dx = x2 - x1;
    uint8_t x = x1;

    if (dx > dy) {
      for (uint8_t y = y1; y < y2; y++) {
        uint8_t start_x = x;
        err += dx;
        while (err >= dy) {
          x++;
          err -= dy;
        }
        uint8_t end_x = x;

        if (start_x < _min_x[y]) {
          _min_x[y] = start_x;
        }
        if (end_x > _max_x[y]) {
          _max_x[y] = end_x;
        }
      }
    } else {
      for (uint8_t y = y1; y < y2; y++) {
        if (x < _min_x[y]) {
          _min_x[y] = x;
        }
        if (x > _max_x[y]) {
          _max_x[y] = x;
        }

        err += dx;
        if (err >= dy) {
          x++;
          err -= dy;
        }
      }
    }
    if (x < _min_x[y2]) {
      _min_x[y2] = x;
    }
    if (x > _max_x[y2]) {
      _max_x[y2] = x;
    }
  } else {
    uint8_t dx = x1 - x2;
    uint8_t x = x1;

    if (dx > dy) {
      for (uint8_t y = y1; y < y2; y++) {
        uint8_t start_x = x;
        err += dx;
        while (err >= dy) {
          x--;
          err -= dy;
        }
        uint8_t end_x = x;

        if (end_x < _min_x[y]) {
          _min_x[y] = end_x;
        }
        if (start_x > _max_x[y]) {
          _max_x[y] = start_x;
        }
      }
    } else {
      for (uint8_t y = y1; y < y2; y++) {
        if (x < _min_x[y]) {
          _min_x[y] = x;
        }
        if (x > _max_x[y]) {
          _max_x[y] = x;
        }

        err += dx;
        if (err >= dy) {
          x--;
          err -= dy;
        }
      }
    }
    if (x < _min_x[y2]) {
      _min_x[y2] = x;
    }
    if (x > _max_x[y2]) {
      _max_x[y2] = x;
    }
  }
}

static inline void _set_pixel(uint8_t *dst, uint8_t fill_char_start_idx,
                              uint8_t val) {
  dst[0] = fill_char_start_idx | (dst[0] & 0xF) | val;
}

static inline void _scan_lines(uint8_t fill_char_start_idx) {
  for (uint8_t sy = 0; sy < 2 * kViewportHeight; ++sy) {
    uint8_t min_x = _min_x[sy];
    uint8_t max_x = _max_x[sy];

    if (max_x < min_x) {
      continue;
    }

    uint8_t py = sy >> 1;
    uint8_t *row = mem_screen_row_ptrs[py];
    uint8_t shift = (sy & 1) << 1;   // 0 for top sub-row, 2 for bottom
    uint8_t left_mask = 1 << shift;  // 1 or 4
    uint8_t right_mask = 2 << shift; // 2 or 8
    uint8_t full_mask = 3 << shift;  // 3 or 12

    // 1. Handle left edge: if min_x is odd, it only occupies the right half of
    // the pixel
    if (min_x & 1) {
      _set_pixel(row + (min_x >> 1), fill_char_start_idx, right_mask);
      ++min_x; // Snap to the next even boundary
    }

    // 2. Handle right edge: if max_x is even, it only occupies the left half of
    // the pixel
    if ((max_x & 1) == 0 && max_x >= min_x) {
      _set_pixel(row + (max_x >> 1), fill_char_start_idx, left_mask);
      --max_x; // Snap back to previous odd boundary
    }

    // 3. Fast bulk fill for everything in between (now guaranteed to be full
    // 2-wide pixels)
    for (uint8_t px = min_x >> 1; px <= max_x >> 1; ++px) {
      _set_pixel(row + px, fill_char_start_idx, full_mask);
    }
#ifdef __DEBUG_POLY__
    if (py < 20) {
      uint16_t shift = py * 40;
      if (sy & 1) {
        shift += 5;
      }
      print_labeled_bcd(610 + shift, SCREEN_STR("L:"), min_x, 2);
      print_labeled_bcd(630 + shift, SCREEN_STR("R:"), max_x, 2);
    }
#endif
  }
}

static inline void _set_line(uint8_t *dst, uint8_t val, int8_t cnt) {
  for (int8_t i = cnt - 1; i >= 0; --i) {
    dst[i] = val;
  }
}

void _scan_lines2(uint8_t fill_char_start_idx) {
  for (uint8_t py = 0; py < kViewportHeight; ++py) {
    uint8_t t_min, b_min, t_max, b_max;
    t_min = _min_x[py << 1];
    t_max = _max_x[py << 1];
    b_min = _min_x[(py << 1) + 1];
    b_max = _max_x[(py << 1) + 1];

    bool t_valid, b_valid;
    t_valid = (t_max >= t_min);
    b_valid = (b_max >= b_min);

    if (!t_valid && !b_valid) {
      continue;
    }

    uint8_t *row = mem_screen_row_ptrs[py];

    // 1. Find the absolute left and right bounds (in pixels) for this row
    uint8_t min_px = kViewportWidth;
    uint8_t max_px = 0;
    if (t_valid) {
      min_px = _min8(min_px, t_min >> 1);
      max_px = _max8(max_px, t_max >> 1);
    }
    if (b_valid) {
      min_px = _min8(min_px, b_min >> 1);
      max_px = _max8(max_px, b_max >> 1);
    }

    // 2. Calculate the "Solid Core" where both top and bottom are fully
    // covered. A pixel is fully covered by a sub-row if min <= px*2 and max >=
    // px*2+1. This translates to a solid pixel span starting at (min + 1)/2 and
    // ending at (max - 1)/2.
    uint8_t overlap_start = kViewportWidth;
    uint8_t overlap_end = 0;

    if (t_valid && b_valid) {
      uint8_t t_solid_start = (t_min + 1) >> 1;
      uint8_t t_solid_end = (t_max - 1) >> 1;
      uint8_t b_solid_start = (b_min + 1) >> 1;
      uint8_t b_solid_end = (b_max - 1) >> 1;

      overlap_start = _max8(t_solid_start, b_solid_start);
      overlap_end = _min8(t_solid_end, b_solid_end);
    }

    // If the spans don't overlap or the polygon is too narrow, default to
    // normal rendering
    if (overlap_end < overlap_start) {
      overlap_start =
          max_px + 1; // Forces the whole line to be treated as a "fringe"
      overlap_end = max_px;
    }

    // 3. Process the Left Fringe (pixels before the solid core)
    for (uint8_t px = min_px; px < overlap_start; ++px) {
      uint8_t mask = 0;
      uint8_t sx_left = px << 1;
      uint8_t sx_right = sx_left + 1;
      if (t_valid) {
        if (sx_left >= t_min && sx_left <= t_max) {
          mask |= 1;
        }
        if (sx_right >= t_min && sx_right <= t_max) {
          mask |= 2;
        }
      }
      if (b_valid) {
        if (sx_left >= b_min && sx_left <= b_max) {
          mask |= 4;
        }
        if (sx_right >= b_min && sx_right <= b_max) {
          mask |= 8;
        }
      }
      if (mask) {
        _set_pixel(row + px, fill_char_start_idx, mask);
      }
    }

    // 4. Fast path: Memset the Solid Core (No read-modify-write!)
    if (overlap_end >= overlap_start) {
      _set_line(row + overlap_start, fill_char_start_idx | 0xF,
                overlap_end - overlap_start + 1);
    }

    // 5. Process the Right Fringe (pixels after the solid core)
    for (uint8_t px = _max8(overlap_end + 1, min_px); px <= max_px; ++px) {
      uint8_t mask = 0;
      uint8_t sx_left = px << 1;
      uint8_t sx_right = (px << 1) + 1;
      if (t_valid) {
        if (sx_left >= t_min && sx_left <= t_max) {
          mask |= 1;
        }
        if (sx_right >= t_min && sx_right <= t_max) {
          mask |= 2;
        }
      }
      if (b_valid) {
        if (sx_left >= b_min && sx_left <= b_max) {
          mask |= 4;
        }
        if (sx_right >= b_min && sx_right <= b_max) {
          mask |= 8;
        }
      }
      if (mask) {
        _set_pixel(row + px, fill_char_start_idx, mask);
      }
    }
#ifdef __DEBUG_POLY__
    if (py < 10) {
      print_labeled_bcd(610 + py * 40, SCREEN_STR("1:"), t_min, 2);
      print_labeled_bcd(615 + py * 40, SCREEN_STR("2:"), b_min, 2);
      print_labeled_bcd(625 + py * 40, SCREEN_STR("1:"), t_max, 2);
      print_labeled_bcd(630 + py * 40, SCREEN_STR("2:"), b_max, 2);
    }
#endif
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
  bm_end(880, SCREEN_STR("CLR:"));

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
  bm_end(920, SCREEN_STR("TRC:"));

  // 2. Draw the scanlines directly to Screen RAM
  bm_start();
  _scan_lines(fill_char_start_idx);
  bm_end(960, SCREEN_STR("SCN:"));
}
