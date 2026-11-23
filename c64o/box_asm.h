#ifndef BOX_ASM_H
#define BOX_ASM_H

#include <stdint.h>

#include "bool.h"
#include "color.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "boxdefs.h"
#include "chardefs.h"
#include "mem.h"
#include "render.h"
#define BOX_SCRATCH_BASE 0x0860
#define BOX_CHAR_LUT_ADDR 0x08C0
#define BOX_COLOR_LUT_ADDR 0x09C0
#define BOX_CHAR_RAM_HI 0xE0
#else
#define BOX_SCRATCH_BASE 0xFC00
#define BOX_CHAR_LUT_ADDR 0xFC60
#define BOX_COLOR_LUT_ADDR 0xFD60
#define BOX_CHAR_RAM_HI 0xE8
static const uint8_t kCharDefPage1 = 91;
static const uint8_t kCharDefPage2 = 164;
#ifndef POLY_ASM_H
static const uint8_t kCharSolidGround = 128;
#endif
static const uint8_t kCharSolidSky = 0;
#ifndef RENDER_ASM_H
static const uint8_t kCharSolid11 = 0;
#endif
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
struct boxdef_t {
  uint8_t w;
  uint8_t h;
  int8_t step_x;
  int8_t step_y;
  int8_t rel_x;
  int8_t rel_y;
  uint8_t grad1_color_start;
  uint8_t char_count;
  uint8_t flip_around;
};
extern boxdef_t boxdef;
extern const uint8_t *boxdef_char_idx;
extern uint8_t chardef_bytes[];
extern uint8_t chardef_offset_lo[];
extern uint8_t mem_box_char_start;
extern int8_t render_cx_chars;
extern int8_t render_cy_chars;
extern uint8_t render_sky_rows;
#endif

// Literal constants for oscar64 inline __asm (oscar64 treats C `static const`
// identifiers as memory addresses rather than immediates).
#define BOX_CHARDEF_PAGE1 91
#define BOX_CHARDEF_PAGE2 164
#define BOX_CHAR_SOLID_GROUND 128
#define BOX_CHAR_SOLID_SKY 0
#define BOX_CHAR_SOLID_11 0

// The assembly half of box.cc (box_asm.cc):
//   - _box_prepare_fill: copies unique box characters (with vertical flips) to
//     character RAM, builds _char_lut/_color_lut, and expands the w * h tile
//     cells into the active slot caches (_cur_box_chars and +0x100 for colors).
//   - _box_draw_asm: draws horizon tiles into screen and color RAM, with a
//     4x-unrolled full-row fast path for level horizons (boxdef.step_y == 0)
//     and a self-modifying 2D copy loop for tilted horizons.
// box.cc holds the pure C equivalents (#ifndef __OSCAR64__) for host builds
// and host tests.

// Current slot's character tile cache, set by box_prepare(). Its matching color
// cache always lives at _cur_box_chars + 0x100, and neither crosses a page.
extern const uint8_t *_cur_box_chars;

void _box_prepare_fill(void);
void _box_draw_asm(void);

#ifdef __OSCAR64__
#pragma compile("box_asm.cc")
#endif

#endif // BOX_ASM_H
