#ifndef POLY_H
#define POLY_H

#include "vec.h"
#include <stdint.h>

static const uint8_t kPolyMaxVertices = 6;
static const uint8_t kPolyMax2dVertices =
    kPolyMaxVertices + 6; // +5 derived, +6 for safety

// Using signed 8-bit integers for coordinates since the screen is 40x25
struct vertex_t {
  int8_t x, y;
};

// Fill the polygon using the traced edges.
void poly_fill(const vertex_t *vertices, uint8_t num_vertices,
               uint8_t fill_char_start_idx, uint8_t color);

// Draw a 3D polygon (clips against near plane, projects, clips against screen
// edges, and fills). Note: This modifies the vertices array in place for
// clipping!
void poly_draw_3d(const vec3_t *vertices, uint8_t num_vertices,
                  uint8_t fill_char_start_idx, uint8_t color);

#pragma compile("poly.cc")

#endif