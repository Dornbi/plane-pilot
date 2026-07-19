#include "gfx.h"

#include "chardefs.h"
#include "color.h"
#include "fmath.h"
#include "mem.h"
#include "sprites.h"
#include "vec.h"
#include "vic.h"
#include "view.h"
#include <string.h>

#ifdef __OSCAR64__
#include <c64/rasterirq.h>
#include <oscar.h>
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
static void rirq_stop(void) {}

static const char *oscar_expand_lzo(char *dp, const char *sp) { return sp; }
#endif

const char kGfxCharsCompressed[] = {
#embed 1792 lzo "gfx_chars.bin"
};

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

// Raster-IRQ setup and charset loading; one-off, not per frame. The IRQ
// handlers themselves are above and are deliberately left alone.
#pragma optimize(push, outline)

void gfx_init_raster_irqs(void) {
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

inline void gfx_stop_raster_irqs(void) { rirq_stop(); }

inline void gfx_wait_vsync(void) {
  while (vic.raster != 255)
    ;
  while (vic.raster == 255)
    ;
}

static inline void _init_solid_chars() {
  memset(kCharRam + kCharSolid11 * 8, 0xFF, 8);
}

void gfx_init_chars(void) {
  _init_solid_chars();
  oscar_expand_lzo((char *)kCharRam + kGfxCharStart * 8, kGfxCharsCompressed);
}

#pragma optimize(pop)

static inline void _draw_ground_point(int16_t px, int16_t py) {
  uint8_t cx = (uint16_t)px >> 3;
  uint8_t cy = (uint8_t)py >> 3;
  uint8_t *p = mem_screen_row_ptrs[cy] + kViewportStartX + cx;
  if (*p == kCharSolidGround) {
    uint8_t lpx = (uint8_t)px;
    uint8_t lpy = (uint8_t)py;
    uint8_t ch = kGfxGroundPoints + ((lpx & 0x06) >> 1) + ((lpy & 0x06) << 1);
    *p = ch;
  }
}

static inline void _draw_color_point(int16_t px, int16_t py, uint8_t color) {
  uint8_t cx = (uint16_t)px >> 3;
  uint8_t cy = (uint8_t)py >> 3;
  uint8_t *p = mem_screen_row_ptrs[cy] + kViewportStartX + cx;
  if (*p == kCharSolidGround) {
    uint8_t lpx = (uint8_t)px;
    uint8_t lpy = (uint8_t)py;
    uint8_t ch = kGfxColorPoints + ((lpx & 0x06) >> 1) + ((lpy & 0x06) << 1);
    *p = ch;
    mem_color_row_ptrs[cy][cx] = color;
  }
}

void gfx_project_and_draw(uint8_t color) {
  if (vec_project()) {
    int16_t px = kViewportWidthPixels / 2 - vec_sx;
    int16_t py = kViewportHeightPixels / 2 - vec_sy;
    if ((uint16_t)px < (uint16_t)kViewportWidthPixels &&
        (uint16_t)py < (uint16_t)kViewportHeightPixels) {
      if (color == kColorGround) {
        _draw_ground_point(px, py);
      } else {
        _draw_color_point(px, py, 0x08 | color);
      }
    }
  }
}

static const char *const kHeadingBitmaps[] = {
    (const char *const)0xF120,
    (const char *const)0xF0C0,
    (const char *const)0xF060,
    (const char *const)0xF000,
};
static const char *kHeadingDest = (const char *)0xF5C8;

// Last heading drawn into the panel bitmap; 0xFF (not a valid heading)
// forces a redraw after the bitmap has been re-expanded.
static uint8_t _heading_bitmap_cache = 0xFF;

void gfx_invalidate_heading_bitmap(void) { _heading_bitmap_cache = 0xFF; }

inline void gfx_update_heading_bitmap(uint8_t heading) {
  if (mem_debug_enabled || view_state != VIEW_CENTER) {
    return;
  }
  // The heading strip lives in the (single) panel bitmap, so once drawn it
  // stays valid until the heading changes or the bitmap is re-expanded.
  if (heading == _heading_bitmap_cache) {
    return;
  }
  _heading_bitmap_cache = heading;

  static const uint8_t kHeadingCharMax = kHeadingMax / 4;
  uint8_t heading_ch = (heading >> 2) + 3;
  if (heading_ch >= kHeadingCharMax) {
    heading_ch -= kHeadingCharMax;
  }
  const char *src_start = kHeadingBitmaps[heading & 0x03];
  char *dst = (char *)kHeadingDest;
  const char *src = src_start + (heading_ch * 8);
  for (uint8_t i = 6;;) {
    memcpy(dst, src, 8);
    ++heading_ch;
    if (heading_ch >= kHeadingCharMax) {
      heading_ch = 0;
      src = src_start;
    } else {
      src += 8;
    }
    dst += 8;
    if (--i == 0) {
      break;
    }
  }
}

static void _set_ptr_color(uint8_t *screen_ptr, bool on) {
  // Looks like Albert always puts light red as color 10.
  *screen_ptr =
      *screen_ptr & 0x0F | (on ? kColorLightRed << 4 : kColorBlack << 4);
}

void gfx_update_nav_toggle(uint8_t nav) {
  static uint8_t *const kNavPtrs[kGfxNumNavpoints] = {
      kScreenRamAlt + 17 * kScreenWidth + 13,
      kScreenRamAlt + 18 * kScreenWidth + 13,
  };
  if (!mem_debug_enabled) {
    _set_ptr_color(kNavPtrs[0], nav == 0);
    _set_ptr_color(kNavPtrs[1], nav == 1);
  }
}

void gfx_update_nav_heading(uint8_t heading) {
  static uint8_t *const kHdgPtr = kScreenRamAlt + 15 * kScreenWidth + 21;
  if (!mem_debug_enabled) {
    _set_ptr_color(kHdgPtr, heading == 0 || heading > kHeadingMax / 2);
    _set_ptr_color(kHdgPtr + 1, heading < kHeadingMax / 2);
  }
}

void gfx_update_flap(bool flap) {
  static uint8_t *const kFlapPtr = kScreenRamAlt + 15 * kScreenWidth + 13;
  if (!mem_debug_enabled) {
    _set_ptr_color(kFlapPtr, flap);
  }
}

void gfx_update_gear(bool gear) {
  static uint8_t *const kGearPtr = kScreenRamAlt + 16 * kScreenWidth + 13;
  if (!mem_debug_enabled) {
    _set_ptr_color(kGearPtr, gear);
  }
}
