#include "poly.h"

#include <stdint.h>

#include "benchmark.h"
#include "chardefs.h"
#include "fmath.h"
#include "mem.h"
#include "vec.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

struct vertex16_t {
  int16_t x;
  int16_t y;
};

#pragma bss(bss2)

// Buffers to store the left-most and right-most X bounds for each scanline
static uint8_t _min_x[2 * kViewportHeight];
static uint8_t _max_x[2 * kViewportHeight];

// Initialize the scanline buffers for the sub-rows in [start, end].
static inline void _poly_clear_buffers(uint8_t start, uint8_t end) {
  for (uint8_t i = start; i <= end; i++) {
    _min_x[i] = kScreenWidth * 2; // Set to out-of-bounds max
    _max_x[i] = 0;                // Set to out-of-bounds min
  }
}

static void _poly_trace_edge_dda(int8_t x1, uint8_t y1, int8_t x2, uint8_t y2) {
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
  int16_t dx_fp = vec_div8p8(abs_dx, dy);

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

static void _poly_trace_edge_bresenham(int8_t x1, uint8_t y1, int8_t x2,
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
  uint8_t err =
      dy >> 1; // dy >> 1 (half-pixel offset) for even line segment distribution

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

static inline void _poly_set_char(uint8_t *dst, uint8_t fill_char_start_idx,
                                  uint8_t val) {
  dst[0] = fill_char_start_idx | (dst[0] & 0xF) | val;
}

// This implementation is simpler and faster for small filled polygons.
static inline void _poly_scan_lines(uint8_t fill_char_start_idx) {
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
      _poly_set_char(row + (min_x >> 1), fill_char_start_idx, right_mask);
      ++min_x; // Snap to the next even boundary
    }

    // 2. Handle right edge: if max_x is even, it only occupies the left half of
    // the pixel
    if ((max_x & 1) == 0 && max_x >= min_x) {
      _poly_set_char(row + (max_x >> 1), fill_char_start_idx, left_mask);
      --max_x; // Snap back to previous odd boundary
    }

    // 3. Fast bulk fill for everything in between (now guaranteed to be full
    // 2-wide pixels)
    for (uint8_t px = min_x >> 1; px <= max_x >> 1; ++px) {
      _poly_set_char(row + px, fill_char_start_idx, full_mask);
    }
#ifdef __DEBUG_POLY__
    if (py < 20) {
      uint16_t shift = py * 40;
      if (sy & 1) {
        shift += 5;
      }
      print_labeled_bcd(610 + shift, "L:", min_x, 2);
      print_labeled_bcd(630 + shift, "R:", max_x, 2);
    }
#endif
  }
}

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

// This version does not take the previous value into account.
static inline void _poly_set_char2(uint8_t *dst, uint8_t fill_char_start_idx,
                                   uint8_t val) {
  dst[0] = fill_char_start_idx + val;
}

// About 250 bytes more code, faster for large polygons.
// Only processes char rows in [py_start, py_end] (inclusive).
void _poly_scan_lines2(uint8_t fill_char_start_idx, uint8_t color,
                       uint8_t py_start, uint8_t py_end) {
  for (uint8_t py = py_start; py <= py_end; ++py) {
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
    uint8_t *color_row = mem_color_row_ptrs[py];

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
    int8_t overlap_start = kViewportWidth;
    int8_t overlap_end = -1;

    if (t_valid && b_valid) {
      int8_t t_solid_start = ((int16_t)t_min + 1) >> 1;
      int8_t t_solid_end = ((int16_t)t_max - 1) >> 1;
      int8_t b_solid_start = ((int16_t)b_min + 1) >> 1;
      int8_t b_solid_end = ((int16_t)b_max - 1) >> 1;

      overlap_start =
          t_solid_start > b_solid_start ? t_solid_start : b_solid_start;
      overlap_end = t_solid_end < b_solid_end ? t_solid_end : b_solid_end;
    }

    // If the spans don't overlap or the polygon is too narrow, default to
    // normal rendering
    if (overlap_end < overlap_start) {
      overlap_start =
          max_px + 1; // Forces the whole line to be treated as a "fringe"
      overlap_end = max_px;
    }

    // 3. Fast path: Memset the Solid Core (No read-modify-write!)
    if (overlap_end >= overlap_start) {
      if (color) {
        _poly_set_color_line(row + overlap_start, fill_char_start_idx | 0xF,
                             color_row + overlap_start, color,
                             overlap_end - overlap_start + 1);
      } else {
        _poly_set_line(row + overlap_start, fill_char_start_idx | 0xF,
                       overlap_end - overlap_start + 1);
      }
    }

    // 4. Process the fringe (every pixel outside the solid core). The core,
    // when present, lies within [min_px, max_px] and was filled above, so we
    // jump over it here. overlap_start is guaranteed >= min_px; when there is
    // no core overlap_start is set to max_px + 1, so the skip never triggers
    // and the whole span is treated as fringe.
    for (uint8_t px = min_px; px <= max_px; ++px) {
      if (px == overlap_start) {
        px = overlap_end; // skip the solid core; ++px lands just past it
        continue;
      }
      uint8_t *dst = row + px;
      if (*dst < kCharSolidGround) {
        continue;
      }

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
        _poly_set_char2(dst, fill_char_start_idx, mask);
        if (color) {
          color_row[px] = color;
        }
      }
    }
#ifdef __DEBUG_POLY__
    if (false && py >= 6 && py < 14) {
      uint8_t y = py - 6;
      print_labeled_bcd(610 + y * 40, "1:", t_min, 2);
      print_labeled_bcd(615 + y * 40, "2:", b_min, 2);
      print_labeled_bcd(620 + y * 40, "1:", t_max, 2);
      print_labeled_bcd(625 + y * 40, "2:", b_max, 2);
    }
#endif
  }
}

// Fill the polygon using the traced edges
void poly_fill(const vertex_t *vertices, uint8_t num_vertices,
               uint8_t fill_char_start_idx, uint8_t color) {
  if (num_vertices < 3) {
    return; // A polygon needs at least 3 vertices
  }

#ifdef __DEBUG_POLY__
  if (true) {
    for (uint8_t y = 0; y < num_vertices; y++) {
      print_labeled_bcd(610 + y * 40, "X:", vertices[y].x, 2);
      print_labeled_bcd(620 + y * 40, "Y:", vertices[y].y, 2);
    }
  }
#endif

  // Find the polygon's vertical extent (in sub-rows) so we only clear and
  // scan the rows the polygon actually touches. Vertex y values are sub-row
  // indices in [0, 2 * kViewportHeight - 1].
  uint8_t min_y = vertices[0].y;
  uint8_t max_y = vertices[0].y;
  for (uint8_t i = 1; i < num_vertices; i++) {
    uint8_t vy = vertices[i].y;
    if (vy < min_y) {
      min_y = vy;
    }
    if (vy > max_y) {
      max_y = vy;
    }
  }
  // Snap to whole char rows so the clear and scan ranges stay aligned: each
  // char row py covers sub-rows 2*py and 2*py+1, both read by _scan_lines2.
  uint8_t py_start = min_y >> 1;
  uint8_t py_end = max_y >> 1;

  bm_poly_start();
  _poly_clear_buffers(py_start << 1, (py_end << 1) + 1);
  bm_poly_end(670, "CLR:");

  // 1. Trace all edges
  bm_poly_start();
  for (uint8_t i = 0; i < num_vertices; i++) {
    uint8_t next = (i + 1);
    if (next == num_vertices) {
      next = 0; // Wrap around to the first vertex
    }
    _poly_trace_edge_bresenham(vertices[i].x, vertices[i].y, vertices[next].x,
                               vertices[next].y);
  }
  bm_poly_end(710, "TRC:");

  // 2. Draw the scanlines directly to Screen RAM
  uint8_t color_val = 0;
  if (color < 8) {
    color_val = color | 8;
  }
  bm_poly_start();
  _poly_scan_lines2(fill_char_start_idx, color_val, py_start, py_end);
  bm_poly_end(750, "SCN:");
}

// 3D near clip
static inline uint8_t _clip_near(const vec3_t *in, uint8_t num_in,
                                 vec3_t *out) {
  uint8_t num_out = 0;
  const vec3_t *prev = &in[num_in - 1];
  bool prev_inside = prev->x >= 8;

  for (uint8_t i = 0; i < num_in; ++i) {
    const vec3_t *curr = &in[i];
    bool curr_inside = curr->x >= 8;

    if (curr_inside != prev_inside) {
      // Where the edge crosses, as a 0.16 fraction of it. vec_div8p8's eight
      // bits are a part in 256 of an edge that is often a thousand units
      // long, and at the near plane that lands whole characters away.
      int16_t dx = curr->x - prev->x;
      int16_t a = 8 - prev->x;
      int16_t dy = curr->y - prev->y;
      int16_t dz = curr->z - prev->z;
      uint16_t t = vec_frac16(a, dx);
      // Then the resolution of the answer. At x = 8 the projection
      // multiplies y by 32, so one unit in is eight sub-pixels out: rounded
      // to whole units the intersection can only land on every fourth
      // character. Projection is a ratio, so scaling one vertex changes
      // nothing but its own resolution - lift this one by eight, near plane
      // included, whenever sixteen bits have the room.
      //
      // All six magnitudes have to be tested, not just the near end and the
      // deltas: the intersection lies between prev and curr, so bounding
      // prev and the delta bounds neither it nor curr. Missing curr here
      // wrapped the scaled intersection to the far side of the screen and
      // filled the whole viewport with one polygon, which is what this
      // scaling was added to prevent. test_near_clip_scaling_never_overflows
      // covers it. The OR is a cheap "all six fit": any bit at 4096 or above
      // fails the test.
      bool fits =
          (uint16_t)(_abs16(prev->y) | _abs16(prev->z) | _abs16(curr->y) |
                     _abs16(curr->z) | _abs16(dy) | _abs16(dz)) < 4096;
      vec3_t *dest = &out[num_out++];
      if (fits) {
        dest->x = 8 << 3;
        dest->y = (prev->y << 3) + vec_mulfrac(t, (int16_t)(dy << 3));
        dest->z = (prev->z << 3) + vec_mulfrac(t, (int16_t)(dz << 3));
      } else {
        dest->x = 8;
        dest->y = prev->y + vec_mulfrac(t, dy);
        dest->z = prev->z + vec_mulfrac(t, dz);
      }
    }
    if (curr_inside) {
      out[num_out++] = *curr;
    }
    prev = curr;
    prev_inside = curr_inside;
  }
  return num_out;
}

// 2D screen clip

// Interpolate base + t * delta, where t is an 8.8 clip parameter in [0, 256]
// and delta is a difference of two projected screen coordinates, which can
// reach ~16000 sub-pixels for a corner just past the near plane.
//
// The assembly vec_fastmul8p8 is exact for all int16 inputs (the old C
// quarter-square version overflowed once |t| + |delta| exceeded ~4095, which
// corrupted clipped vertices here). It truncates toward zero where a 32-bit
// multiply with >> 8 would floor, so a negative t * delta can come out one
// sub-pixel higher; that is harmless and avoids the mul32 runtime.
static inline int16_t _clip_lerp(int16_t base, int16_t t, int16_t delta) {
  return base + vec_fastmul8p8(t, delta);
}

// Clips the polygon against one viewport edge. The edge is data: axis is the
// int16 index of the clipped coordinate within vertex16_t (0 = x, 1 = y),
// limit is the edge position and keep_greater selects which side survives.
// One shared body instead of per-edge code keeps this small.
static uint8_t _clip_2d(const vertex16_t *in, uint8_t num_in, vertex16_t *out,
                        uint8_t axis, int16_t limit, bool keep_greater) {
  const uint8_t other = axis ^ 1;
  uint8_t num_out = 0;
  const int16_t *prev = (const int16_t *)&in[num_in - 1];
  bool prev_inside = keep_greater ? prev[axis] >= limit : prev[axis] <= limit;

  for (uint8_t i = 0; i < num_in; ++i) {
    const int16_t *curr = (const int16_t *)&in[i];
    bool curr_inside = keep_greater ? curr[axis] >= limit : curr[axis] <= limit;

    if (curr_inside != prev_inside) {
      int16_t *dest = (int16_t *)&out[num_out++];
      int16_t d = curr[other] - prev[other];
      int16_t a = limit - prev[axis];
      int16_t den = curr[axis] - prev[axis];
      dest[axis] = limit;
      if (d >= -255 && d <= 255) {
        // Over a span this short the 8.8 parameter is worth a sub-pixel, and
        // this is the common case by far: a polygon that reaches a little
        // past one edge. Keeping it here is what makes the fix free for
        // everything except the case that needed it.
        dest[other] = _clip_lerp(prev[other], vec_div8p8(a, den), d);
      } else {
        // A vertex just past the near plane projects thousands of sub-pixels
        // away, and a part in 256 of that span is characters, not pixels.
        dest[other] = prev[other] + vec_mulfrac(vec_frac16(a, den), d);
      }
    }
    if (curr_inside) {
      out[num_out++] = in[i];
    }
    prev = curr;
    prev_inside = curr_inside;
  }
  return num_out;
}

// Projects 3d vertices to 2d.
static uint8_t _project_vertices(const vec3_t *vertices_3d,
                                 uint8_t num_vertices, vertex_t *vertices_2d) {
  static vec3_t clip3_buf[kPolyMaxVertices +
                          4]; // max 7 vertices after 1 plane clip, 10 is safe
  uint8_t num_clip3 = _clip_near(vertices_3d, num_vertices, clip3_buf);
  if (num_clip3 < 3) {
    return 0;
  }
  static vertex16_t proj_buf[kPolyMaxVertices + 4];
  int16_t min_x = 32767;
  int16_t max_x = -32768;
  int16_t min_y = 32767;
  int16_t max_y = -32768;

  for (uint8_t i = 0; i < num_clip3; ++i) {
    vec_v = clip3_buf[i];
    if (vec_project_nocull()) {
      // Rounded, not truncated toward zero: the bias is half a sub-pixel and
      // it is signed, so the two ends of a polygon are pulled in opposite
      // directions depending on which side of the screen centre they land.
      //
      // Spelled without the (vec_sx + 2) the rounding wants, because vec_sx
      // reaches 32767: vec_div8p8 saturates there, and a vertex just past the
      // near plane saturates it routinely. On the C64 int is 16 bits, so the
      // + 2 wrapped to -32767 and put the vertex on the *opposite* side of
      // the screen, which turned a polygon into the whole viewport. The host
      // cannot see it - there int is 32 bits and the sum simply fits - so
      // this one was caught in the emulator, not in test/poly_test.cc.
      //
      // (x >> 2) + bit 1 of x is the same round-half-up with no addition to
      // overflow: the bit is set exactly when x mod 4 >= 2.
      proj_buf[i].x = 40 - ((vec_sx >> 2) + ((vec_sx >> 1) & 1));
      proj_buf[i].y = 14 - ((vec_sy >> 2) + ((vec_sy >> 1) & 1));
    } else {
      proj_buf[i].x = 40;
      proj_buf[i].y = 14;
    }
    if (proj_buf[i].x < min_x)
      min_x = proj_buf[i].x;
    if (proj_buf[i].x > max_x)
      max_x = proj_buf[i].x;
    if (proj_buf[i].y < min_y)
      min_y = proj_buf[i].y;
    if (proj_buf[i].y > max_y)
      max_y = proj_buf[i].y;
  }

  if (max_x < 0 || min_x > 79 || max_y < 0 || min_y > 27) {
    return 0;
  }

  // In main's bss rather than bss2: 96 bytes here plus final_verts below are
  // what pays for flight_path_px/py, which needs 256 contiguous bytes and had
  // nowhere else to go. Nothing else changes — both regions are plain RAM at
  // absolute addresses, so an access costs the same either way. Also keep
  // clip2_buf1 and clip2_buf2 adjacent and in this order: _clip_2d ping-pongs
  // between them and the pointer swap reads better when they sit together.
#pragma bss(bss)
  static vertex16_t clip2_buf1[kPolyMax2dVertices];
  static vertex16_t clip2_buf2[kPolyMax2dVertices];
#pragma bss(bss2)
  uint8_t n = num_clip3;

  vertex16_t *src = proj_buf;
  vertex16_t *dst = clip2_buf1;

  if (min_x < 0) {
    n = _clip_2d(src, n, dst, 0, 0, /*keep_greater=*/true);
    if (n < 3)
      return 0;
    src = dst;
    dst = (src == clip2_buf1) ? clip2_buf2 : clip2_buf1;
  }
  if (max_x > 79) {
    n = _clip_2d(src, n, dst, 0, kViewportWidth * 2 - 1,
                 /*keep_greater=*/false);
    if (n < 3)
      return 0;
    src = dst;
    dst = (src == clip2_buf1) ? clip2_buf2 : clip2_buf1;
  }
  if (min_y < 0) {
    n = _clip_2d(src, n, dst, 1, 0, /*keep_greater=*/true);
    if (n < 3)
      return 0;
    src = dst;
    dst = (src == clip2_buf1) ? clip2_buf2 : clip2_buf1;
  }
  if (max_y > 27) {
    n = _clip_2d(src, n, dst, 1, kViewportHeight * 2 - 1,
                 /*keep_greater=*/false);
    if (n < 3)
      return 0;
    src = dst;
    dst = (src == clip2_buf1) ? clip2_buf2 : clip2_buf1;
  }

  for (uint8_t i = 0; i < n; ++i) {
    vertices_2d[i].x = (int8_t)src[i].x;
    vertices_2d[i].y = (int8_t)src[i].y;
  }
  return n;
}

__noinline void poly_draw_3d(const vec3_t *vertices, uint8_t num_vertices,
                             uint8_t fill_char_start_idx, uint8_t color) {
  // Main's bss, with the clip2 buffers above and for the same reason.
#pragma bss(bss)
  static vertex_t final_verts[kPolyMax2dVertices];
#pragma bss(bss2)
  bm_poly_start();
  uint8_t n = _project_vertices(vertices, num_vertices, final_verts);
  bm_poly_end(630, "PRJ:");

  if (n >= 3) {
    poly_fill(final_verts, n, fill_char_start_idx, color);
  }
}
#pragma optimize(pop)
