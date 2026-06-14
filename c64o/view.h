#ifndef VIEW_H
#define VIEW_H

enum view_state_t {
  VIEW_LEFT = 0,
  VIEW_CENTER = 1,
  VIEW_RIGHT = 2,
};

extern view_state_t view_state;

void view_update_cam();

#pragma compile("view.cc")

#endif