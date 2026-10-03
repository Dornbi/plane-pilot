#include "box.h"

#include <stddef.h>
#include <string.h>

#include "benchmark.h"
#include "boxdefs.h"
#include "chardefs.h"
#include "color.h"
#include "mem.h"
#include "render.h"
#include "vec.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

#pragma bss(bss2)

// The two screen buffers use distinct charset slots (mem_box_char_start
// 0x01 / 0x61), so everything box_prepare produces is cached per slot and
// only rebuilt when that slot's box definition changes. 384 bytes, and with
// the two tables below 454, in the boot region rather than in bss2 (mem.h
// kBootScratch): every byte of them is written before it is read - the caches
// once _slot_def says the slot is empty, which is how it starts, and the
// tables on every rebuild - so they need neither bss's zeroing nor anything
// to survive from before _boot() returned.
//
// Laid out by hand so that none of them crosses a page, which in bss2 none
// did either: box_draw() reads the caches through a pointer every frame, and a
// (zp),y read that crosses a page costs a cycle. The 512 bytes from $0860
// hold four 96-byte caches 160 apart, with the tables in two of the gaps:
//
//   $0860  chars, slot 0       $08C0  _char_lut
//   $0900  chars, slot 1       $0960  colours, slot 0
//   $09C0  _color_lut          $0A00  colours, slot 1
static const uint8_t kBoxSlotStride = 0xA0;
static uint8_t *const kBoxChars = kBootScratch;
static uint8_t *const kBoxColors = kBootScratch + 0x100;
// Which definition each charset slot currently holds.
static const boxdef_t *_slot_def[2];
// The current slot's buffers, for box_draw.
static const uint8_t *_cur_box_chars;
static const uint8_t *_cur_box_colors;

// Indices 0..2 are the three solid characters (ground, sky, 11); the tile's
// own characters follow at 3..char_count+2. kMaxBoxCharCount counts only the
// latter, so the tables need three extra slots.
static uint8_t *const _char_lut = kBootScratch + 0x60;
static uint8_t *const _color_lut = kBootScratch + 0x160;

// The layout above, checked: everything inside the region, nothing on top of
// anything else, nothing across a page.
#define BOX_IN_PAGE(off, len) \
  ((((MEM_BOOT_START + (off)) & 0xFF) + (len)) <= 0x100)
static_assert(0x100 + kBoxSlotStride + kMaxBoxTotalSize <= kBootScratchSize,
              "box.cc's caches outgrow the boot region (mem.h kBootScratch)");
static_assert(kMaxBoxTotalSize <= 0x60 &&
                  0x60 + kMaxBoxCharCount + 3 <= kBoxSlotStride &&
                  0x160 + kMaxBoxCharCount + 3 <= 0x100 + kBoxSlotStride,
              "box.cc's caches and tables overlap");
static_assert(BOX_IN_PAGE(0x000, kMaxBoxTotalSize) &&
                  BOX_IN_PAGE(kBoxSlotStride, kMaxBoxTotalSize) &&
                  BOX_IN_PAGE(0x100, kMaxBoxTotalSize) &&
                  BOX_IN_PAGE(0x100 + kBoxSlotStride, kMaxBoxTotalSize) &&
                  BOX_IN_PAGE(0x060, kMaxBoxCharCount + 3) &&
                  BOX_IN_PAGE(0x160, kMaxBoxCharCount + 3),
              "a box.cc cache or table crosses a page");
#undef BOX_IN_PAGE

void box_invalidate(void) {
  _slot_def[0] = NULL;
  _slot_def[1] = NULL;
}

void box_prepare(void) {
  // The whole function, both exits. Started here rather than after the
  // definition lookup because boxdef_set_main()/_alt() copy a boxdef_t on
  // every frame including the early return below, and cycles nothing counts
  // are cycles TOT is short by.
  bm_view_start();
  const boxdef_t *src_def;
  if (render_alt_box) {
    src_def = boxdef_set_alt();
  } else {
    src_def = boxdef_set_main();
  }

  const uint8_t slot = mem_box_char_start != 0x01;
  const uint8_t slot_offset = slot ? kBoxSlotStride : 0;
  _cur_box_chars = kBoxChars + slot_offset;
  _cur_box_colors = kBoxColors + slot_offset;

  if (src_def != NULL && src_def == _slot_def[slot]) {
    // This slot already holds this definition (typical when flying
    // straight); the char RAM and the buffers below are still valid.

    // Still printed, so this buffer does not keep a stale CHR: from the last
    // frame that did the work and flash against the other buffer's value.
    bm_view_end(750, "CHR:");
    return;
  }
  _slot_def[slot] = src_def;

  // Copy unique characters to kCharRam.
  // The definition stores one byte per character, its index into chardefs.
  // chardefs holds each character once per vertical flip, so some local
  // characters are their entry upside down, and are copied bottom row first
  // (lib/find_boxes.py build_c_charset()). They are one run around
  // grad1_color_start, which flip_around holds as two nibbles: how far it
  // reaches below, and how far from there up.
  uint8_t *dst_ram = kCharRam + ((uint16_t)mem_box_char_start << 3);
  const uint8_t *src_idx = boxdef.char_idx;
  const uint8_t flip_start =
      boxdef.grad1_color_start - (boxdef.flip_around & 0x0F);
  const uint8_t flip_end = boxdef.grad1_color_start + (boxdef.flip_around >> 4);

  for (uint8_t i = 0;;) {
    const uint8_t *src = chardefs[src_idx[i]];
    if (i >= flip_start && i < flip_end) {
#pragma unroll(full)
      for (uint8_t row = 0; row < 8; ++row) {
        dst_ram[row] = src[7 - row];
      }
    } else {
      memcpy(dst_ram, src, 8);
    }
    dst_ram += 8;
    if (++i >= boxdef.char_count) {
      break;
    }
  }

  // Populate box_chars and box_colors mapping. No bm_view_start() here, and
  // that is the point: there used to be one, left behind when CHR and PRP were
  // collapsed into a single counter - deleting CHR's end and PRP's start took
  // only the first of the two. It restarted the timer at this line, so the
  // char RAM copy above was in no counter at all, TOT included, and CHR
  // reported the tail of the function as though it were the whole of it.
  _char_lut[0] = kCharSolidGround;
  _color_lut[0] = kColorGrad1 | 0x08;
  _char_lut[1] = kCharSolidSky;
  _color_lut[1] = kColorSky | 0x08;
  _char_lut[2] = kCharSolid11;
  _color_lut[2] = kColorGrad1 | 0x08;

  // A byte counter and a base three entries in, not `_char_lut[i + 3]` with an
  // int8_t: the tables are behind a pointer now (kBootScratch), and with a
  // signed index oscar64 builds a 16-bit address for every store, ~20 cycles a
  // character, where this is a plain absolute,x. Every box has characters, so
  // nothing is lost by the loop no longer running once for an empty one.
  uint8_t *const char_lut_tile = _char_lut + 3;
  uint8_t *const color_lut_tile = _color_lut + 3;
  for (uint8_t i = boxdef.char_count; i-- != 0;) {
    char_lut_tile[i] = mem_box_char_start + i;
    color_lut_tile[i] = (i >= boxdef.grad1_color_start) ? (kColorGrad1 | 0x08)
                                                        : (kColorSky | 0x08);
  }

  // Fill box_chars and box_colors with the box definition.
  if (boxdef.total_size > 0) {
    const uint8_t *src = boxdef.box_chars;
    uint8_t *dst_chars = kBoxChars + slot_offset;
    uint8_t *dst_colors = kBoxColors + slot_offset;
    for (int8_t i = boxdef.total_size - 1;;) {
      const uint8_t idx = src[i];
      dst_chars[i] = _char_lut[idx];
      dst_colors[i] = _color_lut[idx];
      if (--i < 0) {
        break;
      }
    }
  }
  bm_view_end(750, "CHR:");
}

static void _draw_one_box(int8_t cx, int8_t cy) {
  const uint8_t *src_chr = _cur_box_chars;
  const uint8_t *src_col = _cur_box_colors;

  int8_t h = boxdef.h;
  if (cy < 0) {
    h += cy;
    // cy * boxdef.w via the quarter-square routine rather than oscar64's
    // mul16by8. vec_fastmul8p8 returns trunc(a * b / 256), so shifting cy up
    // by 8 gives back the exact product. cy is a negative int8_t, so cy << 8
    // stays in range, and w * h <= kMaxBoxCharCount bounds the result.
    const int16_t skip = vec_fastmul8p8((int16_t)cy << 8, boxdef.w);
    src_chr -= skip;
    src_col -= skip;
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

// __noinline: two call sites, run once per box repetition; inlined it cost
// about 60 bytes.
static __noinline bool _out_of_bounds(int8_t cx, int8_t cy, bool reverse) {
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

__forceinline void box_draw(void) {
  bm_view_start();
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

  bm_view_end(790, "DRW:");
}
#pragma optimize(pop)
