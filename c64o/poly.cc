#include "poly.h"

#include "benchmark.h"
#include "mem.h"
#include <stdint.h>

// Buffers to store the left-most and right-most X bounds for each scanline
int8_t min_x[kScreenHeight];
int8_t max_x[kScreenHeight];

// Initialize the scanline buffers
void clear_buffers() {
  for (uint8_t i = 0; i < kScreenHeight; i++) {
    min_x[i] = kScreenWidth; // Set to out-of-bounds max
    max_x[i] = -1;           // Set to out-of-bounds min
  }
}

// Trace a single edge using Bresenham's algorithm (Zero Division)
void trace_edge(int8_t x1, int8_t y1, int8_t x2, int8_t y2) {
  // Calculate absolute differences and step directions
  int8_t dx = (x2 > x1) ? (x2 - x1) : (x1 - x2); // abs(x2 - x1)
  int8_t sx = (x1 < x2) ? 1 : -1;

  int8_t dy = (y2 > y1)
                  ? (y1 - y2)
                  : (y2 - y1); // -abs(y2 - y1) to match standard Bresenham
  int8_t sy = (y1 < y2) ? 1 : -1;

  // The error accumulator. Since max screen width is 40 and height is 25,
  // the error * 2 will never overflow a signed 8-bit integer (-128 to 127).
  int8_t err = dx + dy;
  int8_t e2;

  int8_t x = x1;
  int8_t y = y1;

  while (1) {
    // Record the X coordinate for this Y scanline
    if (y >= 0 && y < kScreenHeight) {
      if (x < min_x[y])
        min_x[y] = x;
      if (x > max_x[y])
        max_x[y] = x;
    }

    // Break when we reach the destination vertex
    if (x == x2 && y == y2)
      break;

    // e2 = err * 2 (using a bit-shift for speed)
    e2 = err << 1;

    // Step X if needed
    if (e2 >= dy) {
      err += dy;
      x += sx;
    }

    // Step Y if needed
    if (e2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

// Fill the polygon using the traced edges
void fill_poly(vertex_t *vertices, uint8_t num_vertices,
               unsigned char fill_char) {
  if (num_vertices < 3) {
    return; // A polygon needs at least 3 vertices
  }
  bm_start();
  clear_buffers();
  bm_end(880, SCREEN_STR("clear:"));

  // 1. Trace all edges
  bm_start();
  for (uint8_t i = 0; i < num_vertices; i++) {
    uint8_t next = (i + 1);
    if (next == num_vertices)
      next = 0; // Wrap around to the first vertex

    trace_edge(vertices[i].x, vertices[i].y, vertices[next].x,
               vertices[next].y);
  }
  bm_end(920, SCREEN_STR("trace:"));

  // 2. Draw the scanlines directly to Screen RAM
  bm_start();
  unsigned char *screen = mem_screen_ram;
  for (int8_t y = 0; y < kScreenHeight; y++) {
    int8_t left = min_x[y];
    int8_t right = max_x[y];

    // Clip X coordinates to the physical screen width
    if (left < 0)
      left = 0;
    if (right >= kScreenWidth)
      right = kScreenWidth - 1;

    // If valid bounds exist for this scanline, fill it
    if (left <= right) {
      // Calculate row start pointer to avoid multiplying y * 40 in the inner
      // loop
      unsigned char *row_ptr = screen + (y * 40);
      for (int8_t x = left; x <= right; x++) {
        row_ptr[x] = fill_char;
      }
    }
  }
  bm_end(960, SCREEN_STR("scan: "));
}
