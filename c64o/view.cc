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

static void _view_copy_and_fill(bool is_left_view) {
  char *bmp_src = (char *)0xE000 + 320 * kViewportHeight;
  char *bmp_dst = (char *)0xE000 + 320 * kViewportHeight;
  char *bmp_fill = (char *)0xE000 + 320 * kViewportHeight;
  char *screen_src = (char *)kViewScreenDst;
  char *screen_dst = (char *)kViewScreenDst;
  char *screen_fill = (char *)kViewScreenDst;
  char *color_src = (char *)kViewColorDst;
  char *color_dst = (char *)kViewColorDst;
  char *color_fill = (char *)kViewColorDst;
  if (is_left_view) {
    bmp_dst += kFillWidthChars * 8;
    screen_dst += kFillWidthChars;
    color_dst += kFillWidthChars;
  } else {
    bmp_src += kFillWidthChars * 8;
    bmp_fill += kCopyWidthChars * 8;
    screen_src += kFillWidthChars;
    screen_fill += kCopyWidthChars;
    color_src += kFillWidthChars;
    color_fill += kCopyWidthChars;
  }
  for (uint8_t row = 0; row < kScreenHeight - kViewportHeight; ++row) {
    memcpy(bmp_dst, bmp_src, kCopyWidthChars * 8);
    if (row >= 3) {
      memset(bmp_fill, 0xAA, kFillWidthChars * 8);
    } else {
      _view_fill_with_pattern(bmp_fill,
                              (const char *)kViewFillPattern + row * 8);
    }
    // The fill leaves color 01 black rather than picking something for it:
    // kFillPattern uses only the 10 and 11 pairs, so 01 never appears.
    memcpy(screen_dst, screen_src, kCopyWidthChars);
    memset(screen_fill, kColorMedGray, kFillWidthChars);
    memcpy(color_dst, color_src, kCopyWidthChars);
    memset(color_fill, kColorLightGray, kFillWidthChars);
    bmp_dst += kScreenWidth * 8;
    bmp_src += kScreenWidth * 8;
    bmp_fill += kScreenWidth * 8;
    screen_dst += kScreenWidth;
    screen_src += kScreenWidth;
    screen_fill += kScreenWidth;
    color_dst += kScreenWidth;
    color_src += kScreenWidth;
    color_fill += kScreenWidth;
  }
}

void view_refresh_panel() {
  if (mem_debug_enabled) {
    return;
  }
  oscar_expand_lzo(kViewScreenDst, kViewPanelScreenCompressed);
  oscar_expand_lzo(kViewColorDst, kViewPanelColorCompressed);
  if (view_bitmap_state != VIEW_CENTER) {
    oscar_expand_lzo(kViewBitmapDst, kViewPanelBitmapCompressed);
    // The expansion overwrote the heading strip with the default bitmap.
    gfx_invalidate_heading_bitmap();
  }
  view_bitmap_state = view_state;
  if (view_state == VIEW_CENTER) {
    return;
  }
  if (view_state == VIEW_LEFT) {
    _view_copy_and_fill(/*view_left=*/true);
  } else {
    _view_copy_and_fill(/*view_left=*/false);
  }
}

void view_invalidate_bitmap() { view_bitmap_state = VIEW_UNKNOWN; }

void view_update_view(view_state_t new_state) {
  if (new_state == view_state) {
    return;
  }
  if (mem_debug_enabled) {
    view_state = new_state;
    return;
  }
  if (view_state == VIEW_CENTER) {
    // Optimization: if the previous state was already center,
    // don't rebuild it first.
    if (new_state == VIEW_LEFT) {
      _view_copy_and_fill(/*view_left=*/true);
    } else {
      _view_copy_and_fill(/*view_left=*/false);
    }
    view_state = new_state;
    view_bitmap_state = new_state;
  } else {
    view_state = new_state;
    view_refresh_panel();
  }
}

#pragma optimize(pop)
