#ifndef VIC_H
#define VIC_H

#ifdef __OSCAR64__
#include <c64/vic.h>
#else
#include <stdint.h>
typedef uint8_t byte;
struct VIC {
  struct XY {
    volatile byte x, y;
  } spr_pos[8];
  byte spr_msbx;

  volatile byte ctrl1;
  volatile byte raster;
  volatile byte lpx, lpy;
  volatile byte spr_enable;
  volatile byte ctrl2;
  volatile byte spr_expand_y;
  volatile byte memptr;
  volatile byte intr_ctrl;
  volatile byte intr_enable;
  volatile byte spr_priority;
  volatile byte spr_multi;
  volatile byte spr_expand_x;
  volatile byte spr_sprcol;
  volatile byte spr_backcol;
  volatile byte color_border;
  volatile byte color_back;
  volatile byte color_back1;
  volatile byte color_back2;
  volatile byte color_back3;
  volatile byte spr_mcolor0;
  volatile byte spr_mcolor1;
  volatile byte spr_color[8];
};

// Where the host build's "chip" lives, by the same device sid.h uses for the
// SID: the test points this at a register-sized buffer, so a write is an array
// store rather than a segfault at 0xD000. That is what makes the raster
// handlers in sprites.cc testable off the C64 - see test/sprites_test.cc, and
// docs/clouds.md §1.4 for why they are worth testing.
extern struct VIC *vic_host;
#define vic (*vic_host)

#endif

#endif