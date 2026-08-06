#ifndef MAP_H
#define MAP_H

#include <stdint.h>

#include "bool.h"

extern bool map_mode;

void map_enter();
void map_exit();

// Sets one pixel of the map's overlay layer (bit pair 01, white in every
// cell), px 0..127 across and py 0..127 down over the 128 x 128 map area.
// Only meaningful while the map bitmap is up, so map_enter() is its only
// caller today; the flight path draw joins it in phase 5.
void map_set_overlay_pixel(uint8_t px, uint8_t py);

#pragma compile("map.cc")

#endif // MAP_H
