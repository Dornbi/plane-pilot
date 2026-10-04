#include "render.h"

#include <stdint.h>
#include <string.h>

#include "benchmark.h"
#include "chardefs.h"
#include "color.h"
#include "fmath.h"
#include "mem.h"
#include "roll.h"
#include "vec.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

int16_t render_cx_pixels;
int16_t render_cy_pixels;
int16_t render_px_pixels;
int16_t render_py_pixels;
int8_t render_cx_chars;
int8_t render_cy_chars;
bool render_alt_box;
int8_t render_alt_shift_x;
int8_t render_alt_shift_y;

// Skip so many lines, which will be filled by tiles.
// PERF: 4 -> cycles: -1000. Bytes: +260 with the old per-case fill loops; the
// span fill handles any value with the same code.
static const int8_t kSkipLines = 4;

static inline int16_t _render_lshift(int16_t x) {
  if (roll_shift_2chars) {
    return x << 7;
  } else {
    return x << 6;
  }
}

static inline int16_t _render_rshift(int16_t x) {
  if (roll_shift_2chars) {
    return x >> 7;
  } else {
    return x >> 6;
  }
}

// Plain 16x16 -> low 16 product, routed through the quarter-square routine
// instead of oscar64's mul16. vec_fastmul8p8 returns trunc(a * b / 256), so
// pre-shifting b by 8 recovers a * b exactly - the remainder is zero, so the
// truncation never rounds.
//
// b is int8_t, so b << 8 always fits and the whole int8_t range is usable -
// including -128. Callers must therefore pass the raw value and negate the
// product afterwards rather than negating the operand first: 0 - (-128) is
// 128, which does not survive the parameter, and the sign flip that follows
// is silent. That mistake is what put diagonal bands through the sky fill.
//
// The routine also builds the product from the magnitudes and applies the sign
// last, so it wraps sign-magnitude where mul16 wrapped two's complement. Both
// of these are checked rather than argued: each call site below was compared
// exhaustively against the expression it replaced, over the operand's full
// range, overflow included.
static inline int16_t _render_mul(int16_t a, int8_t b) {
  return vec_fastmul8p8(a, (int16_t)b << 8);
}

// _fill_sky_ground_* negates the _mul result in place of the subtraction the
// horizon term used to spell out. (oscar64 only takes static_assert at file
// scope, so it lives here rather than next to the code it guards.)
static_assert(kViewportStartY == 0, "the horizon term assumes it");

// Finds a point (px, py) on the horizon line that is shifted by an
// integer number of major axis steps to be close to the viewport center.
//
// The two axes are the same computation with x and y swapped, so they are
// swapped into major/minor locals once rather than spelled out twice.
static void _pull_to_center() {
  int16_t c_maj, c_min, t;
  int8_t d_maj, d_min;
  if (roll_x_is_major) {
    c_maj = render_cx_pixels;
    c_min = render_cy_pixels;
    t = 160;
    d_maj = roll_dx;
    d_min = roll_dy;
  } else {
    c_maj = render_cy_pixels;
    c_min = render_cx_pixels;
    t = 64;
    d_maj = roll_dy;
    d_min = roll_dx;
  }
  t += (roll_shift_2chars ? 64 : 32) - c_maj;
  int16_t d = _render_rshift(t);
  // _render_lshift(_render_rshift(t)), without the second shift chain.
  int16_t p_maj = c_maj + (t & (roll_shift_2chars ? -128 : -64));
  if (d_maj < 0) {
    d = -d;
  }
  int16_t p_min = c_min + (_render_mul(d, d_min) << 3);
  if (roll_x_is_major) {
    render_px_pixels = p_maj;
    render_py_pixels = p_min;
  } else {
    render_px_pixels = p_min;
    render_py_pixels = p_maj;
  }
}

static inline void _render_set_alt_shift() {
  if (roll_x_is_major) {
    render_alt_shift_x = 0;
    render_alt_shift_y = 4;
  } else {
    render_alt_shift_x = 4;
    render_alt_shift_y = 0;
  }
}

// Distance to line from nearest character.
// Optimized to avoid large multiplications by using (px, py) as anchor.
// dist = |roll_dy * (px - mx*8) - roll_dx * (py - my*8)|
// PERF: roll_get_dist() -> cycles: -120 bytes: -100
static uint16_t _unused_get_dist(int8_t px, int8_t py) {
  int8_t ex = px - ((px + 4) & 0xF8);
  int8_t ey = py - ((py + 4) & 0xF8);
  int16_t dist = (int16_t)roll_dy * ex - (int16_t)roll_dx * ey;
  if (dist < 0) {
    dist = -dist;
  }
  return dist;
}

void render_snap_center_chars() {
  bm_view_start();
  uint16_t min_dist = 0x7fff;
  _pull_to_center();

  if (roll_period == 1) {
    _render_set_alt_shift();

    // 1. Main Lattice
    uint16_t dist = roll_get_dist(render_px_pixels, render_py_pixels);
    min_dist = dist;
    render_cx_chars = (int8_t)((render_px_pixels + 4) >> 3);
    render_cy_chars = (int8_t)((render_py_pixels + 4) >> 3);
    render_alt_box = false;

    // 2. Alt Lattice
    dist = roll_get_dist(render_px_pixels - render_alt_shift_x,
                         render_py_pixels - render_alt_shift_y);
    if (dist < min_dist) {
      render_cx_chars =
          (int8_t)((render_px_pixels - render_alt_shift_x + 4) >> 3);
      render_cy_chars =
          (int8_t)((render_py_pixels - render_alt_shift_y + 4) >> 3);
      render_alt_box = true;
    }
    bm_view_end(670, "SNP:");
    return;
  }

  int16_t px = render_px_pixels;
  int16_t py = render_py_pixels;
  render_alt_box = false;
  for (uint8_t i = 0; i < roll_period; ++i) {
    uint16_t dist = roll_get_dist(px, py);
    if (dist < min_dist) {
      min_dist = dist;
      render_cx_chars = (int8_t)((px + 4) >> 3);
      render_cy_chars = (int8_t)((py + 4) >> 3);
    }
    px += roll_dx;
    py += roll_dy;
  }
  bm_view_end(670, "SNP:");
}

static void _fill_line(uint8_t *dst, uint8_t val) {
#pragma unroll(full)
  for (uint8_t i = 0; i < kViewportWidth; ++i) {
    dst[i] = val;
  }
}

// The row _fill_span writes to, in the screen and in the colour buffer.
static uint8_t *_row_dst, *_row_color;

// Fills cells [from, to) of the current row, from < to: sky sets the colour
// cell too, ground leaves it alone. A whole row goes through the unrolled
// _fill_line.
static inline void _fill_span(uint8_t from, uint8_t to, bool sky) {
  uint8_t n = to - from;
  if (n == kViewportWidth) {
    if (sky) {
      _fill_line(_row_dst, kCharSolid11);
      _fill_line(_row_color, kColorSky | 0x08);
    } else {
      _fill_line(_row_dst, kCharSolidGround);
    }
    return;
  }
  uint8_t *dst = _row_dst + from;
  if (sky) {
    uint8_t *dst_color = _row_color + from;
    do {
      --n;
      dst[n] = kCharSolid11;
      dst_color[n] = kColorSky | 0x08;
    } while (n);
  } else {
    do {
      dst[--n] = kCharSolidGround;
    } while (n);
  }
}

// Clamps a row number into [0, kViewportHeight].
static inline uint8_t _clamp_rows(int16_t row) {
  if (row < 0) {
    return 0;
  }
  if (row > kViewportHeight) {
    return kViewportHeight;
  }
  return row;
}

// One 12.4 position past the right edge of the viewport.
static const int16_t kFull = kViewportWidth << 4;

// Every row is a left span [0, l) and a right span [r, kViewportWidth), sky on
// one side and ground on the other, and the cells between them are left for
// the tiles. For a tilted horizon the spans come from two 12.4 positions per
// row, lo and hi = lo + w, where w is the kSkipLines band and lo walks by
// roll_dx_div_dy a row:
//
//   hi < 0x0f    the whole row is the right side's
//   lo >= kFull  the whole row is the left side's
//   otherwise    l = lo >> 4 (0 below 0x10), r = hi >> 4 (the edge at kFull)
//
// The left span is always filled before the right one, the order the old
// per-case loops used, so even a wrapped lo/hi pair, where the spans overlap,
// writes the same cells.
void render_fill_sky_ground() {
  bm_view_start();
  _row_dst = (uint8_t *)(mem_screen_ram + kViewportStartX +
                         kViewportStartY * kScreenWidth);
  _row_color = (uint8_t *)(mem_color_buffer + kViewportStartY * kViewportWidth);

  if (roll_dy == 0) {
    // Level: whole rows only. Rows above e are the left side's, rows from
    // e + kSkipLines on the right side's, and the ones between are skipped.
    // The sky side's rows stop kSkipLines short of the horizon row, the
    // ground side's run up to it.
    bool sky_left = roll_dx > 0;
    int16_t e = render_cy_chars;
    if (sky_left) {
      e -= kSkipLines;
    }
    // Its own loop rather than the span logic below, which costs about 90
    // cycles a row more, and level flight is the common case.
    uint8_t row_l = _clamp_rows(e);
    uint8_t row_r = _clamp_rows(e + kSkipLines);
    for (uint8_t y = 0; y < kViewportHeight; ++y) {
      if (y < row_l || y >= row_r) {
        bool sky = (y < row_l) == sky_left;
        _fill_line(_row_dst, sky ? kCharSolid11 : kCharSolidGround);
        if (sky) {
          _fill_line(_row_color, kColorSky | 0x08);
        }
      }
      _row_dst += kScreenWidth;
      _row_color += kViewportWidth;
    }
  } else {
    bool sky_left = roll_dy < 0;
    // 12.4 fixpoint representation of the divider x between sky and ground.
    //
    // render_cy_chars is a truncating int8_t cast of a horizon that can sit far
    // off screen, so it really does reach -128 and the old product really did
    // overflow there. Negating the result rather than the operand keeps both
    // cases identical to the mul16 version; see the note on _mul.
    int16_t lo = -_render_mul(roll_dx_div_dy, render_cy_chars) +
                 ((render_cx_chars - kViewportStartX) << 4);
    if (roll_dx_div_dy > 0) {
      // Hack to make sure the boxes always cover the horizon.
      lo += 8;
    }
    if (render_alt_box) {
      if (render_alt_shift_x) {
        lo += 8;
      }
      if (render_alt_shift_y) {
        lo -= (roll_dx_div_dy >> 1);
      }
    }
    int16_t w = _abs16(roll_dx_div_dy) * kSkipLines;
    // The divider is the ground side's edge of the band: hi when the sky is
    // on the left, lo when it is on the right.
    if (sky_left) {
      lo -= w;
    }

    for (uint8_t y = 0; y < kViewportHeight; ++y) {
      uint8_t l = 0, r = kViewportWidth;
      int16_t hi = lo + w;
      if (hi < 0x0f) {
        r = 0;
      } else if (lo >= kFull) {
        l = kViewportWidth;
      } else {
        if (lo >= 0x10) {
          l = lo >> 4;
        }
        if (hi < kFull) {
          r = hi >> 4;
        }
      }
      if (l) {
        _fill_span(0, l, sky_left);
      }
      if (r < kViewportWidth) {
        _fill_span(r, kViewportWidth, !sky_left);
      }
      _row_dst += kScreenWidth;
      _row_color += kViewportWidth;
      lo += roll_dx_div_dy;
    }
  }

  bm_view_end(710, "BGR:");
}
#pragma optimize(pop)
