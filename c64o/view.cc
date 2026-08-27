#include "view.h"

#include <string.h>

#include "color.h"

#ifdef __OSCAR64__
#include <oscar.h>
#else
static inline void oscar_expand_lzo(char *dst, const char *src) {}
#endif

#include "color.h"
#include "flight.h"
#include "gfx.h"
#include "mem.h"
#include "vec.h"
#include "world.h"

view_state_t view_state = VIEW_UNKNOWN;
static view_state_t view_bitmap_state = VIEW_UNKNOWN;

#pragma data(data_compr)
static const char kViewPanelBitmapCompressed[] = {
#embed 3904 4098 lzo "panel.koa"
};

static const char kViewPanelScreenCompressed[] = {
#embed 440 8562 lzo "panel.koa"
};

static const char kViewPanelColorCompressed[] = {
#embed 440 9562 lzo "panel.koa"
};
#pragma data(data)

void view_update_cam() {
  if (view_state == VIEW_CENTER) {
    world_cam.front = flight_cam.front;
    world_cam.left = flight_cam.left;
  } else if (view_state == VIEW_LEFT) {
    world_cam.front = flight_cam.left;
    world_cam.left = flight_cam.front;
    vec_negate(&world_cam.left);
  } else {
    world_cam.front = flight_cam.left;
    vec_negate(&world_cam.front);
    world_cam.left = flight_cam.front;
  }
  world_cam.up = flight_cam.up;
}

// Panel rebuilding runs only when the view changes (a keypress), never from
// the per-frame render path, so the outliner's trade is free here.
#pragma optimize(push, outline)

static char *const kViewBitmapDst = (char *)0xF000;
static char *const kViewScreenDst = (char *)0xEE30;
static char *const kViewColorDst = (char *)0xDA30;

// clang-format off
static const uint8_t kViewFillPattern[] = {
    0xFF, 0xFF, 0xBB, 0xEE, 0xBB, 0xEE, 0xBB, 0xEE,
    0xBB, 0xEE, 0xBB, 0xEE, 0xBB, 0xEE, 0xBB, 0xEE,
    0xBB, 0xEE, 0xAA, 0xEE, 0xAA, 0xEE, 0xAA, 0xAA,
};
// clang-format on

static void _view_fill_with_pattern(char *dst, const char *src) {
  for (uint8_t n = 0; n < kFillWidthChars; ++n) {
    for (uint8_t c = 0; c < 8; ++c) {
      *dst++ = src[c];
    }
  }
}

// The panel: 40 columns of the eleven rows below the viewport.
static const uint16_t kViewPanelCells =
    kScreenWidth * (kScreenHeight - kViewportHeight);

// Blacks the panel out, whatever the bitmap under it happens to hold.
//
// In multicolor bitmap mode the panel's four bit pairs come from $d021 (00,
// which the split's _gfx_panel_top_writes() sets to black on every frame), the
// screen RAM nybbles at $ee30 (01 and 10) and the color RAM at $da30 (11), so
// zeroing those 880 bytes is the whole of it. That is about 6000 cycles,
// comfortably inside one frame, against the two to four frames the bitmap work
// below takes -- and it is what lets that work happen in plain sight, with the
// raster split still running and the viewport still flying. A 1/2/3 view
// switch cannot use screen_blank() for the same job: the split owns $d011 and
// rewrites it three times a frame, and blanking the whole screen mid-flight
// would be a worse artifact than the one it hides.
//
// It also destroys the colors it covers, which is why _view_build_panel()
// re-expands them from the compressed copy rather than shifting what is on
// screen, the way this file used to.
static void _view_blackout_panel(void) {
  memset(kViewScreenDst, 0, kViewPanelCells);
  memset(kViewColorDst, 0, kViewPanelCells);
}

// A side view keeps kCopyWidthChars of the panel's 40 columns and fills the
// rest with the gradient pattern. The bitmap half of that, and the color half
// below it, used to be one loop; they are two so that the slow half can run
// while the panel is blacked out and the fast half can run at the end, where
// it is the frame the finished panel appears in.
static void _view_shift_bitmap(bool is_left_view) {
  char *bmp_src = (char *)0xE000 + 320 * kViewportHeight;
  char *bmp_dst = bmp_src;
  char *bmp_fill = bmp_src;
  if (is_left_view) {
    bmp_dst += kFillWidthChars * 8;
  } else {
    bmp_src += kFillWidthChars * 8;
    bmp_fill += kCopyWidthChars * 8;
  }
  for (uint8_t row = 0; row < kScreenHeight - kViewportHeight; ++row) {
    memcpy(bmp_dst, bmp_src, kCopyWidthChars * 8);
    if (row >= 3) {
      memset(bmp_fill, 0xAA, kFillWidthChars * 8);
    } else {
      _view_fill_with_pattern(bmp_fill,
                              (const char *)kViewFillPattern + row * 8);
    }
    bmp_dst += kScreenWidth * 8;
    bmp_src += kScreenWidth * 8;
    bmp_fill += kScreenWidth * 8;
  }
}

static void _view_shift_colors(bool is_left_view) {
  char *screen_src = (char *)kViewScreenDst;
  char *screen_dst = screen_src;
  char *screen_fill = screen_src;
  char *color_src = (char *)kViewColorDst;
  char *color_dst = color_src;
  char *color_fill = color_src;
  if (is_left_view) {
    screen_dst += kFillWidthChars;
    color_dst += kFillWidthChars;
  } else {
    screen_src += kFillWidthChars;
    screen_fill += kCopyWidthChars;
    color_src += kFillWidthChars;
    color_fill += kCopyWidthChars;
  }
  for (uint8_t row = 0; row < kScreenHeight - kViewportHeight; ++row) {
    // The fill leaves color 01 black rather than picking something for it:
    // kViewFillPattern uses only the 10 and 11 pairs, so 01 never appears.
    memcpy(screen_dst, screen_src, kCopyWidthChars);
    memset(screen_fill, kColorMedGray, kFillWidthChars);
    memcpy(color_dst, color_src, kCopyWidthChars);
    memset(color_fill, kColorLightGray, kFillWidthChars);
    screen_dst += kScreenWidth;
    screen_src += kScreenWidth;
    screen_fill += kScreenWidth;
    color_dst += kScreenWidth;
    color_src += kScreenWidth;
    color_fill += kScreenWidth;
  }
}

// Rebuilds the panel for the current view_state, in an order chosen so that
// nothing half-built is ever on screen: black it out, do all the slow work
// under that, and put the colors back last. Every caller is either a screen
// transition, where screen_blank() has the display off anyway, or a 1/2/3 view
// switch, where the blackout above is the only cover there is.
//
// The caller has already ruled out the debug view.
static void _view_build_panel(void) {
  _view_blackout_panel();

  // The one genuinely slow step, two to four frames of it, and the reason
  // view_bitmap_state exists: coming from the center view the bitmap still
  // holds the pristine art, so only the shift below is needed.
  if (view_bitmap_state != VIEW_CENTER) {
    oscar_expand_lzo(kViewBitmapDst, kViewPanelBitmapCompressed);
    // The expansion overwrote the heading strip with the default bitmap.
    gfx_invalidate_heading_bitmap();
  }
  view_bitmap_state = view_state;
  if (view_state != VIEW_CENTER) {
    _view_shift_bitmap(view_state == VIEW_LEFT);
  }

  // Colors last, and always from the compressed copy: the blackout destroyed
  // the center art that the shift used to read out of screen and color RAM.
  // Re-expanding it costs two 440-byte LZO passes, which together with the
  // shift is well under a frame, so the finished panel appears in one go
  // rather than being wiped in a column at a time.
  oscar_expand_lzo(kViewScreenDst, kViewPanelScreenCompressed);
  oscar_expand_lzo(kViewColorDst, kViewPanelColorCompressed);
  if (view_state != VIEW_CENTER) {
    _view_shift_colors(view_state == VIEW_LEFT);
  }
}

void view_refresh_panel() {
  if (mem_debug_enabled) {
    return;
  }
  _view_build_panel();
}

void view_invalidate_bitmap() { view_bitmap_state = VIEW_UNKNOWN; }

void view_update_view(view_state_t new_state) {
  if (new_state == view_state) {
    return;
  }
  view_state = new_state;
  if (mem_debug_enabled) {
    return;
  }
  // Unlike every other caller of this, the simulation is still running and the
  // raster split still owns the screen. _view_build_panel() is written for
  // that case; the old shortcut for "the previous state was already center"
  // now lives inside it, as the view_bitmap_state test.
  _view_build_panel();
}

#pragma optimize(pop)
