#include "render.h"

#include <stdint.h>
#include <string.h>

#include "benchmark.h"
#include "chardefs.h"
#include "color.h"
#include "fmath.h"
#include "mem.h"
#include "print.h"
#include "roll.h"

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
static const int8_t kSkipLines = 0;

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
    render_py_pixels = render_cy_pixels + (dx * (int16_t)roll_dy << 3);
  } else {
    int16_t dx = _render_rshift(64 - render_cy_pixels + dy);
    render_py_pixels = render_cy_pixels + _render_lshift(dx);
    if (roll_dy < 0) {
      dx = -dx;
    }
    render_px_pixels = render_cx_pixels + (dx * (int16_t)roll_dx << 3);
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

  bm_horiz_start();
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
    bm_horiz_end(710, SCREEN_STR("SNP:"));
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
  bm_horiz_end(710, SCREEN_STR("SNP:"));
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
    int16_t dx = (int16_t)roll_dx_div_dy * (kViewportStartY - render_cy_chars) +
                 ((render_cx_chars - kViewportStartX) << 4) + 8;
    if (render_alt_box) {
      if (render_alt_shift_x) {
        dx += 8;
      }
      if (render_alt_shift_y) {
        dx -= (roll_dx_div_dy >> 1);
      }
    }

    if (roll_dy < 0) {
      int16_t dx_left = dx - _abs(roll_dx_div_dy) * kSkipLines;
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
      int16_t dx_right = dx + _abs(roll_dx_div_dy) * kSkipLines;
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
    int16_t dx = (int16_t)roll_dx_div_dy * (kViewportStartY - render_cy_chars) +
                 ((render_cx_chars - kViewportStartX) << 4) + 8;
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
  bm_horiz_start();
  if (kSkipLines > 0) {
    _fill_sky_ground_with_skip();
  } else {
    _fill_sky_ground_no_skip();
  }

  bm_horiz_end(750, SCREEN_STR("BGR:"));
}