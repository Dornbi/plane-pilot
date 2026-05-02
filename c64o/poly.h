#ifndef POLY_H
#define POLY_H

#include <stdint.h>

// Using signed 8-bit integers for coordinates since the screen is 40x25
struct vertex_t {
  int8_t x, y;
};

// Fill the polygon using the traced edges.
void fill_poly(vertex_t *vertices, uint8_t num_vertices,
               unsigned char fill_char);

#pragma compile("poly.cc")

#endif