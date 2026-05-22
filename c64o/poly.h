#ifndef POLY_H
#define POLY_H

#include "vec.h"
#include <stdint.h>

// Using signed 8-bit integers for coordinates since the screen is 40x25
struct vertex_t {
  int8_t x, y;
};

// Fill the polygon using the traced edges.
void poly_fill(const vertex_t *vertices, uint8_t num_vertices,
               uint8_t fill_char_start_idx);

// Draw a 3D polygon (clips against near plane, projects, clips against screen
// edges, and fills). Note: This modifies the vertices array in place for
// clipping!
void poly_draw_3d(vec3_t *vertices, uint8_t num_vertices,
                  uint8_t fill_char_start_idx);

#pragma compile("poly.cc")

#endif