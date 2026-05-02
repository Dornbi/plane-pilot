#include "gfx.h"

#include "chardefs.h"
#include "color.h"
#include "mem.h"
#include "sprites.h"
#include "vec.h"
#include "vic.h"

#ifdef __OSCAR64__
#include <c64/rasterirq.h>
#else
#define RIRQ_SIZE 2
typedef struct RIRQCode {
  uint8_t size;
  uint8_t code[RIRQ_SIZE];
} RIRQCode;

static void rirq_init(bool kernalIRQ) {}
static void rirq_build(RIRQCode *ic, uint8_t size) {}
static void rirq_call(RIRQCode *ic, uint8_t n, void *addr) {}
static void rirq_set(uint8_t n, uint8_t raster, RIRQCode *ic) {}
static void rirq_sort(void) {}
static void rirq_start(void) {}
#endif

#pragma optimize(push, noasm)
static void _switch_to_panel_top() {
  if (mem_debug_enabled) {
    sprites_show_no_sprites();
    return;
  }
#assign num_nop 16
#repeat
  __asm {
      nop;
  }
#assign num_nop num_nop - 1
#until num_nop == 0
#undef num_nop

  // clang-format off
  __asm {
    lda #$b8;
    sta $d018;
    lda #$3b;
    sta $d011;
    lda #$00;
    sta $d021;
  }
  // clang-format on
  sprites_show_panel_top_sprites();
}

static void _switch_to_panel_bottom() {
  if (mem_debug_enabled) {
    return;
  }
  sprites_show_panel_bottom_sprites();
}

static void _switch_to_terrain() {
  sprites_show_terrain_sprites();
  vic.color_back = kColorGrad2;
  vic.ctrl1 = 0x1b; // Multicolor character mode
  vic_memptr = mem_using_alt_buffer ? 0xA8 : 0xB8;
}

#pragma optimize(pop)

RIRQCode _rirq_panel_top, _rirq_panel_bottom, _rirq_terrain;

void gfx_init(void) {
  rirq_init(/*kernalIRQ=*/false);
  rirq_build(&_rirq_panel_top, 1);
  rirq_call(&_rirq_panel_top, 0, (void *)_switch_to_panel_top);
  rirq_set(0, kRasterScreenYStart + kViewportEndYPixels - 1, &_rirq_panel_top);
  rirq_build(&_rirq_panel_bottom, 1);
  rirq_call(&_rirq_panel_bottom, 0, (void *)_switch_to_panel_bottom);
  rirq_set(1, kRasterScreenYStart + kViewportEndYPixels + 24,
           &_rirq_panel_bottom);
  rirq_build(&_rirq_terrain, 1);
  rirq_call(&_rirq_terrain, 0, (void *)_switch_to_terrain);
  rirq_set(2, kRasterScreenYStart + kScreenHeightPixels, &_rirq_terrain);
  rirq_sort();
  rirq_start();
}

inline void gfx_draw_single_point(int16_t px, int16_t py) {
  uint8_t cx = (uint16_t)px >> 3;
  uint8_t cy = (uint8_t)py >> 3;
  uint8_t *p = mem_screen_row_ptrs[cy] + kViewportStartX + cx;
  if (*p == kCharSolidGround) {
    uint8_t lpx = (uint8_t)px;
    uint8_t lpy = (uint8_t)py;
    uint8_t ch =
        kSinglePointCharStart + ((lpx & 0x06) >> 1) + ((lpy & 0x06) << 1);
    *p = ch;
  }
}

void gfx_project_and_draw() {
  if (vec_project()) {
    int16_t px = kViewportWidthPixels / 2 - vec_sx;
    int16_t py = kViewportHeightPixels / 2 - vec_sy;
    if ((uint16_t)px < (uint16_t)kViewportWidthPixels &&
        (uint16_t)py < (uint16_t)kViewportHeightPixels) {
      gfx_draw_single_point(px, py);
    }
  }
}
