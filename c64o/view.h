#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>

#include "mem.h"

enum view_state_t {
  VIEW_UNKNOWN = 0,
  VIEW_LEFT = 1,
  VIEW_CENTER = 2,
  VIEW_RIGHT = 3,
  // Over the shoulder, 180 degrees from the nose. The odd one out: the other
  // three all look out through some part of the cockpit, so they keep a strip
  // of the dashboard under the viewport, and this one looks at the tail, where
  // there is no dashboard to keep. Nothing of the panel is drawn in it - no
  // art, no instrument sprites, no lamps, no heading strip.
  VIEW_BACK = 4,
};

extern view_state_t view_state;

// How many of the panel's 40 columns a side view keeps from the dashboard art,
// and how many the gradient pattern fills. The back view keeps none and fills
// all forty, so it uses neither.
static const uint8_t kCopyWidthChars = 8;
static const uint8_t kFillWidthChars = kScreenWidth - kCopyWidthChars;

// Updates world_cam based on flight_cam and the view_state.
void view_update_cam();

// Unconditionally updates the bitmap based on the current view.
void view_refresh_panel();

// Forgets which view the panel bitmap currently holds, so the next
// view_refresh_panel() re-expands it instead of concluding it is already
// correct. Must be called by anything that overwrites $F000..$FF3F behind
// view_refresh_panel's back.
void view_invalidate_bitmap();

// Updates view_state and the bitmap based on the previous state.
void view_update_view(view_state_t new_state);

#pragma compile("view.cc")

#endif