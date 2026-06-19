#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>

#include "mem.h"

enum view_state_t {
  VIEW_UNKNOWN = 0,
  VIEW_LEFT = 1,
  VIEW_CENTER = 2,
  VIEW_RIGHT = 3,
};

extern view_state_t view_state;

static const uint8_t kCopyWidthChars = 8;
static const uint8_t kFillWidthChars = kScreenWidth - kCopyWidthChars;

// Updates world_cam based on model_cam and the view_state.
void view_update_cam();

// Unconditionally updates the bitmap based on the current view.
void view_refresh_panel();

// Updates view_state and the bitmap based on the previous state.
void view_update_view(view_state_t new_state);

#pragma compile("view.cc")

#endif