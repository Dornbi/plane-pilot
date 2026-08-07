#include "gfx.h"

#include <string.h>

#include "chardefs.h"
#include "color.h"
#include "fmath.h"
#include "mem.h"
#include "sound.h"
#include "sprites.h"
#include "vec.h"
#include "vic.h"
#include "view.h"

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

#pragma data(data_compr)
const char kGfxCharsCompressed[] = {
#embed 1792 lzo "gfx_chars.bin"
};
#pragma data(data)

// Raster IRQ handlers: cycle-counted (note the NOP padding below), so keep
// the outliner out of them as well.
#pragma optimize(push, noasm, nooutline)
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

  // Last, and only after the $d018 latch above, which is the one write here
  // with a deadline. The blit is ~200 cycles at raster 250; on PAL that ends
  // around line 254 with 58 lines of border left, on NTSC around 254 with 9.
  // Nothing waits on a raster line past this one - gfx_wait_flip_window()
  // closes at 242 - so overrunning only delays the blit itself.
  sound_blit();
}

#pragma optimize(pop)

#pragma bss(bss2)
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

// The sound driver's release point, and the reason it needs no other. Because
// sound_blit() lives in _switch_to_terrain, "raster interrupts masked" and
// "the driver has stopped running" are the same statement, and this is the
// only place that makes it true - reached from screen_enter_static_mccm()
// (menu, help) and map_enter(), and nowhere else. Silencing here therefore
// covers all three without any of them knowing about sound.
//
// Before the rirq_stop(), not after: map_enter() goes on to bank I/O out, and
// sound_silence() writes $D400 directly.
inline void gfx_stop_raster_irqs(void) {
  sound_silence();
  rirq_stop();
}

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

// The raster window in which mem_switch_buffer() may do its work. Two
// deadlines bracket it, and neither of them is the vertical blank:
//
//   Opens at 162. _copy_color_ram() rewrites $D800-$DA2F, the color RAM the
//   viewport fetches while it is on screen on lines 50..161. The first line
//   past the viewport is the earliest the copy can start without recoloring
//   the picture out from under the beam.
//
//   Closes at 242. _switch_to_terrain() runs at raster 250 and latches
//   mem_using_alt_buffer into $d018 for the next frame's viewport, so the
//   toggle has to land before 250 or the new colors are shown against the old
//   characters for a frame. Eight lines of margin, which is ~500 cycles for a
//   store that needs about ten.
//
// The copy itself is ~5760 cycles, near enough 92 lines, and it is allowed to
// run right through 250 and into the border - it only has to be finished by
// the next frame's line 50. Starting as late as 242 that lands around line 24
// of the following frame, 26 lines clear on PAL. NTSC has 263 lines rather
// than 312 and only 69 of them left at that point, so this constant would
// have to come down to about 215 there. It does not today: the old line-255
// wait left the copy just 57 lines on NTSC, so that path was already over
// budget before this change.
static const uint8_t kFlipWindowFirst =
    kRasterScreenYStart + kViewportEndYPixels;
static const uint8_t kFlipWindowLast =
    kRasterScreenYStart + kScreenHeightPixels - 8;
static const uint8_t kFlipWindowSpan = kFlipWindowLast - kFlipWindowFirst;

// A range test rather than an edge, which is the point of it. Waiting for one
// specific raster line means a handler that happens to straddle that line
// costs a whole frame; an 81-line window cannot be stepped over. $d012 is only
// the low byte of the raster counter, but every line in the window is below
// 256 and PAL's lines 256..311 read back as 0..55, so the unsigned compare
// rejects them along with the top border.
inline void gfx_wait_flip_window(void) {
  while ((uint8_t)(vic.raster - kFlipWindowFirst) > kFlipWindowSpan)
    ;
}

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

// The indicator lamps are lit by recoloring one bit pair of their char cell,
// and that pair is 11 — color RAM, a whole byte per cell — so switching a lamp
// is a plain store with nothing to preserve.
//
// The two screen RAM colors would do the job too, but they share a byte, and
// which of them a cell's colors land in is png2koa.py's choice, not ours: it
// re-labels the slots for compression and has already swapped a lamp's nibbles
// once, silently. `make panel` therefore pins light red to color RAM in these
// four cells, by the same row and column as the pointers below, and
// tests/test_png2koa.py checks all three lists still agree. Only these cells
// are pinned; light red elsewhere in the panel is the encoder's business.
static void _set_lamp(uint8_t *color_ptr, bool on) {
  *color_ptr = on ? kColorLightRed : kColorBlack;
}

// Only the center view shows the lamps. A side view keeps 8 of the panel's 40
// columns and fills the rest with a pattern, and every lamp is in the filled
// part — where color RAM is what the fill draws its own pixels with, so a lamp
// written there would be a red speck on the gradient. The heading strip bails
// out on the same test, for the same kind of reason.
static inline bool _lamps_live(void) {
  return !mem_debug_enabled && view_state == VIEW_CENTER;
}

void gfx_update_nav_heading(uint8_t heading) {
  static uint8_t *const kHdgPtr = kColorRam + 15 * kScreenWidth + 21;
  if (_lamps_live()) {
    _set_lamp(kHdgPtr, heading == 0 || heading > kHeadingMax / 2);
    _set_lamp(kHdgPtr + 1, heading < kHeadingMax / 2);
  }
}

void gfx_update_flap(bool flap) {
  static uint8_t *const kFlapPtr = kColorRam + 16 * kScreenWidth + 13;
  if (_lamps_live()) {
    _set_lamp(kFlapPtr, flap);
  }
}

void gfx_update_gear(bool gear) {
  static uint8_t *const kGearPtr = kColorRam + 17 * kScreenWidth + 13;
  if (_lamps_live()) {
    _set_lamp(kGearPtr, gear);
  }
}
