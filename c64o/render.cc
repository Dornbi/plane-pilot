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
// PERF: 4 -> cycles: -1000 bytes: +260
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
static inline int16_t _mul(int16_t a, int8_t b) {
  return vec_fastmul8p8(a, (int16_t)b << 8);
}

// _fill_sky_ground_* negates the _mul result in place of the subtraction the
// horizon term used to spell out. (oscar64 only takes static_assert at file
// scope, so it lives here rather than next to the code it guards.)
static_assert(kViewportStartY == 0, "the horizon term assumes it");

// Finds a point (px, py) on the horizon line that is shifted by an
// integer number of major axis steps to be close to the viewport center.
static void _pull_to_center() {
  int16_t dy = roll_shift_2chars ? 64 : 32;
  if (roll_x_is_major) {
    int16_t dx = _render_rshift(160 - render_cx_pixels + dy);
    render_px_pixels = render_cx_pixels + _render_lshift(dx);
    if (roll_dx < 0) {
      dx = -dx;
    }
    render_py_pixels = render_cy_pixels + (_mul(dx, roll_dy) << 3);
  } else {
    int16_t dx = _render_rshift(64 - render_cy_pixels + dy);
    render_py_pixels = render_cy_pixels + _render_lshift(dx);
    if (roll_dy < 0) {
      dx = -dx;
    }
    render_px_pixels = render_cx_pixels + (_mul(dx, roll_dx) << 3);
  }
}

static inline void _set_alt_shift() {
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
  uint16_t min_dist = 0x7fff;

  bm_view_start();
  _pull_to_center();

  if (roll_period == 1) {
    _set_alt_shift();

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
    bm_view_end(710, "SNP:");
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
  bm_view_end(710, "SNP:");
}

static void _fill_line(uint8_t *dst, uint8_t val) {
#pragma unroll(full)
  for (uint8_t i = 0; i < kViewportWidth; ++i) {
    dst[i] = val;
  }
}

static void _fill_lines(uint8_t *dst, uint8_t *dst_color, uint8_t val,
                        uint8_t val_color, int8_t cnt) {
  for (int8_t i = cnt - 1; i >= 0; --i) {
    dst[i] = val;
    dst_color[i] = val_color;
  }
}

static inline void _fill_sky_ground_with_skip() {
  uint8_t *dst = (uint8_t *)(mem_screen_ram + kViewportStartX +
                             kViewportStartY * kScreenWidth);
  uint8_t *dst_color =
      (uint8_t *)(mem_color_buffer + kViewportStartY * kViewportWidth);

  if (roll_dy == 0) {
    int8_t cy = render_cy_chars;
    if (cy < 0) {
      cy = 0;
    } else if (cy > kViewportHeight) {
      cy = kViewportHeight;
    }

    if (roll_dx > 0) {
      for (int8_t y = 0; y < cy; ++y) {
        if (y < render_cy_chars - kSkipLines) {
          _fill_line(dst, kCharSolid11);
          _fill_line(dst_color, kColorSky | 0x08);
        }
        dst += kScreenWidth;
        dst_color += kViewportWidth;
      }
      for (int8_t y = cy; y < kViewportHeight; ++y) {
        _fill_line(dst, kCharSolidGround);
        dst += kScreenWidth;
      }
    } else {
      for (int8_t y = 0; y < cy; ++y) {
        _fill_line(dst, kCharSolidGround);
        dst += kScreenWidth;
        dst_color += kViewportWidth;
      }
      for (int8_t y = cy; y < kViewportHeight; ++y) {
        if (y >= render_cy_chars + kSkipLines) {
          _fill_line(dst, kCharSolid11);
          _fill_line(dst_color, kColorSky | 0x08);
        }
        dst += kScreenWidth;
        dst_color += kViewportWidth;
      };
    }
  } else {
    // 12.4 fixpoint representation of the divider x between sky and ground.
    //
    // render_cy_chars is a truncating int8_t cast of a horizon that can sit far
    // off screen, so it really does reach -128 and the old product really did
    // overflow there. Negating the result rather than the operand keeps both
    // cases identical to the mul16 version; see the note on _mul.
    int16_t dx = -_mul(roll_dx_div_dy, render_cy_chars) +
                 ((render_cx_chars - kViewportStartX) << 4);
    if (roll_dx_div_dy > 0) {
      // Hack to make sure the boxes always cover the horizon.
      dx += 8;
    }
    if (render_alt_box) {
      if (render_alt_shift_x) {
        dx += 8;
      }
      if (render_alt_shift_y) {
        dx -= (roll_dx_div_dy >> 1);
      }
    }

    if (roll_dy < 0) {
      int16_t dx_left = dx - _abs16(roll_dx_div_dy) * kSkipLines;
      for (uint8_t y = 0; y < kViewportHeight; ++y) {
        if (dx < 0x0f) {
          _fill_line(dst, kCharSolidGround);
        } else if (dx_left >= (kViewportWidth << 4)) {
          _fill_line(dst, kCharSolid11);
          _fill_line(dst_color, kColorSky | 0x08);
        } else {
          if (dx_left >= 0x10) {
            _fill_lines(dst, dst_color, kCharSolid11, kColorSky | 0x08,
                        dx_left >> 4);
          }
          if (dx < (kViewportWidth << 4)) {
            uint8_t x = dx >> 4;
            memset(dst + x, kCharSolidGround, kViewportWidth - x);
          }
        }
        dst += kScreenWidth;
        dst_color += kViewportWidth;
        dx += roll_dx_div_dy;
        dx_left += roll_dx_div_dy;
      }
    } else {
      int16_t dx_right = dx + _abs16(roll_dx_div_dy) * kSkipLines;
      for (uint8_t y = 0; y < kViewportHeight; ++y) {
        if (dx_right < 0x0f) {
          _fill_line(dst, kCharSolid11);
          _fill_line(dst_color, kColorSky | 0x08);
        } else if (dx >= (kViewportWidth << 4)) {
          _fill_line(dst, kCharSolidGround);
        } else {
          if (dx >= 0x10) {
            memset(dst, kCharSolidGround, dx >> 4);
          }
          if (dx_right < (kViewportWidth << 4)) {
            uint8_t x = dx_right >> 4;
            _fill_lines(dst + x, dst_color + x, kCharSolid11, kColorSky | 0x08,
                        kViewportWidth - x);
          }
        }
        dst += kScreenWidth;
        dst_color += kViewportWidth;
        dx += roll_dx_div_dy;
        dx_right += roll_dx_div_dy;
      }
    }
  }
}

static inline void _fill_sky_ground_no_skip() {
  uint8_t *dst = (uint8_t *)(mem_screen_ram + kViewportStartX +
                             kViewportStartY * kScreenWidth);
  uint8_t *dst_color =
      (uint8_t *)(mem_color_buffer + kViewportStartY * kViewportWidth);

  if (roll_dy == 0) {
    int8_t cy = render_cy_chars;
    if (cy < 0) {
      cy = 0;
    } else if (cy > kViewportHeight) {
      cy = kViewportHeight;
    }

    if (roll_dx > 0) {
      for (int8_t y = 0; y < cy; ++y) {
        _fill_line(dst, kCharSolid11);
        _fill_line(dst_color, kColorSky | 0x08);
        dst += kScreenWidth;
        dst_color += kViewportWidth;
      }
      for (int8_t y = cy; y < kViewportHeight; ++y) {
        _fill_line(dst, kCharSolidGround);
        dst += kScreenWidth;
      }
    } else {
      for (int8_t y = 0; y < cy; ++y) {
        _fill_line(dst, kCharSolidGround);
        dst += kScreenWidth;
        dst_color += kViewportWidth;
      }
      for (int8_t y = cy; y < kViewportHeight; ++y) {
        _fill_line(dst, kCharSolid11);
        _fill_line(dst_color, kColorSky | 0x08);
        dst += kScreenWidth;
        dst_color += kViewportWidth;
      };
    }
  } else {
    // 12.4 fixpoint representation of the divider x between sky and ground.
    //
    // render_cy_chars is a truncating int8_t cast of a horizon that can sit far
    // off screen, so it really does reach -128 and the old product really did
    // overflow there. Negating the result rather than the operand keeps both
    // cases identical to the mul16 version; see the note on _mul.
    int16_t dx = -_mul(roll_dx_div_dy, render_cy_chars) +
                 ((render_cx_chars - kViewportStartX) << 4);
    if (roll_dx_div_dy > 0) {
      // Hack to make sure the boxes always cover the horizon.
      dx += 8;
    }
    if (render_alt_box) {
      if (render_alt_shift_x) {
        dx += 8;
      }
      if (render_alt_shift_y) {
        dx -= (roll_dx_div_dy >> 1);
      }
    }

    if (roll_dy < 0) {
      for (uint8_t y = 0; y < kViewportHeight; ++y) {
        if (dx < 0x0f) {
          _fill_line(dst, kCharSolidGround);
        } else if (dx >= (kViewportWidth << 4)) {
          _fill_line(dst, kCharSolid11);
          _fill_line(dst_color, kColorSky | 0x08);
        } else {
          uint8_t x = dx >> 4;
          _fill_lines(dst, dst_color, kCharSolid11, kColorSky | 0x08, x);
          memset(dst + x, kCharSolidGround, kViewportWidth - x);
        }
        dst += kScreenWidth;
        dst_color += kViewportWidth;
        dx += roll_dx_div_dy;
      }
    } else {
      for (uint8_t y = 0; y < kViewportHeight; ++y) {
        if (dx < 0x0f) {
          _fill_line(dst, kCharSolid11);
          _fill_line(dst_color, kColorSky | 0x08);
        } else if (dx >= (kViewportWidth << 4)) {
          _fill_line(dst, kCharSolidGround);
        } else {
          uint8_t x = dx >> 4;
          memset(dst, kCharSolidGround, x);
          _fill_lines(dst + x, dst_color + x, kCharSolid11, kColorSky | 0x08,
                      kViewportWidth - x);
        }
        dst += kScreenWidth;
        dst_color += kViewportWidth;
        dx += roll_dx_div_dy;
      }
    }
  }
}

void render_fill_sky_ground() {
  bm_view_start();
  if (kSkipLines > 0) {
    _fill_sky_ground_with_skip();
  } else {
    _fill_sky_ground_no_skip();
  }

  bm_view_end(750, "BGR:");

#ifdef __ENABLE_DEBUG__
  if (mem_debug_enabled) {
    print_labeled_bcd(980, "ROL:", roll_angle, 3);
  }
#endif
}
#pragma optimize(pop)
