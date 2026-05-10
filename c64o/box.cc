#include "box.h"

#include <string.h>

#include "benchmark.h"
#include "boxdefs.h"
#include "color.h"
#include "mem.h"
#include "print.h"
#include "render.h"

static uint8_t _box_chars[kMaxBoxTotalSize];
static uint8_t _box_colors[kMaxBoxTotalSize];
static uint8_t _char_lut[kMaxBoxCharCount];
static uint8_t _color_lut[kMaxBoxCharCount];

void box_prepare(void) {
  if (render_alt_box) {
    boxdef_set_alt();
  } else {
    boxdef_set_main();
  }

  // Copy unique characters to kCharRam
  bm_horiz_start();
  uint8_t *dst_ram = kCharRam + ((uint16_t)mem_box_char_start << 3);
  const uint8_t **src_ptrs = boxdef.char_addr;

  for (int8_t i = boxdef.char_count - 1;;) {
    memcpy(dst_ram, *src_ptrs, 8);
    dst_ram += 8;
    src_ptrs++;
    if (--i < 0) {
      break;
    }
  }
  bm_horiz_end(790, SCREEN_STR("CHR:"));

  // Populate box_chars and box_colors mapping
  bm_horiz_start();
  _char_lut[0] = 0;
  _color_lut[0] = kColorGrad1 | 0x08;
  _char_lut[1] = 1;
  _color_lut[1] = kColorSky | 0x08;
  _char_lut[2] = 1;
  _color_lut[2] = kColorGrad1 | 0x08;

  for (int8_t i = boxdef.char_count - 1;;) {
    _char_lut[i + 3] = mem_box_char_start + i;
    _color_lut[i + 3] = (i >= boxdef.grad1_color_start) ? (kColorGrad1 | 0x08)
                                                        : (kColorSky | 0x08);
    if (--i < 0) {
      break;
    }
  }

  // Fill box_chars and box_colors with the box definition.
  if (boxdef.total_size > 0) {
    const uint8_t *src = boxdef.box_chars;
    for (int8_t i = boxdef.total_size - 1;;) {
      const uint8_t idx = src[i];
      _box_chars[i] = _char_lut[idx];
      _box_colors[i] = _color_lut[idx];
      if (--i < 0) {
        break;
      }
    }
  }
  bm_horiz_end(830, SCREEN_STR("PRP:"));
}

static void _draw_one_box(int8_t cx, int8_t cy) {
  const uint8_t *src_chr = _box_chars;
  const uint8_t *src_col = _box_colors;

  int8_t h = boxdef.h;
  if (cy < 0) {
    h += cy;
    src_chr -= (int16_t)cy * boxdef.w;
    src_col -= (int16_t)cy * boxdef.w;
    cy = 0;
  }
  int8_t x = cy + h - kViewportHeight;
  if (x > 0) {
    h -= x;
  }
  if (h <= 0) {
    return;
  }

  int8_t w = boxdef.w;
  if (cx < 0) {
    w += cx;
    src_chr -= cx;
    src_col -= cx;
    cx = 0;
  }
  x = cx + w - kViewportWidth;
  if (x > 0) {
    w -= x;
  }
  if (w <= 0) {
    return;
  }

  uint8_t *dst_chr = mem_screen_row_ptrs[cy] + cx + kViewportStartX;
  uint8_t *dst_col = mem_color_row_ptrs[cy] + cx;

  --w;
  for (int8_t y = --h;;) {
    for (int8_t x = w;;) {
      dst_chr[x] = src_chr[x];
      dst_col[x] = src_col[x];
      if (--x < 0) {
        break;
      }
    }
    if (--y < 0) {
      break;
    }
    src_chr += boxdef.w;
    src_col += boxdef.w;
    dst_chr += kScreenWidth;
    dst_col += kViewportWidth;
  }
}

static inline bool _out_of_bounds(int8_t cx, int8_t cy, bool reverse) {
  bool step_x_increasing = reverse ? boxdef.step_x < 0 : boxdef.step_x > 0;
  bool step_y_increasing = reverse ? boxdef.step_y < 0 : boxdef.step_y > 0;
  if (step_x_increasing) {
    if (cx >= kViewportWidth) {
      return true;
    }
  } else {
    if (cx + (int8_t)boxdef.w <= 0) {
      return true;
    }
  }
  if (step_y_increasing) {
    if (cy >= kViewportHeight) {
      return true;
    }
  } else {
    if (cy + (int8_t)boxdef.h <= 0) {
      return true;
    }
  }
  return false;
}

void box_draw(void) {
  bm_horiz_start();
  // cx and cy are now relative to the viewport not the screen.
  const int8_t base_cx = render_cx_chars - kViewportStartX + boxdef.rel_x;
  const int8_t base_cy = render_cy_chars - kViewportStartY + boxdef.rel_y;

  // Forward repetition
  int8_t cx = base_cx;
  int8_t cy = base_cy;
  while (true) {
    _draw_one_box(cx, cy);
    cx += boxdef.step_x;
    cy += boxdef.step_y;
    if (_out_of_bounds(cx, cy, /*reverse=*/false)) {
      break;
    }
  }

  // Backward repetition
  cx = base_cx - boxdef.step_x;
  cy = base_cy - boxdef.step_y;
  while (true) {
    _draw_one_box(cx, cy);
    cx -= boxdef.step_x;
    cy -= boxdef.step_y;
    if (_out_of_bounds(cx, cy, /*reverse=*/true)) {
      break;
    }
  }

  bm_horiz_end(870, SCREEN_STR("DRW:"));
}
