#include "poly.h"

#include "benchmark.h"
#include "mem.h"
#include <stdint.h>

// Buffers to store the left-most and right-most X bounds for each scanline
static __zeropage uint8_t _min_x[kViewportHeight];
static __zeropage uint8_t _max_x[kViewportHeight];

// Initialize the scanline buffers
static inline void _clear_buffers() {
  for (uint8_t i = 0; i < kViewportHeight; i++) {
    _min_x[i] = kScreenWidth; // Set to out-of-bounds max
    _max_x[i] = 0;            // Set to out-of-bounds min
  }
}

// Trace a single edge using Bresenham's algorithm (No Division)
static void _trace_edge(int8_t x1, int8_t y1, int8_t x2, int8_t y2) {
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
    if (y >= 0 && y < kViewportHeight) {
      if (x < _min_x[y])
        _min_x[y] = x;
      if (x > _max_x[y])
        _max_x[y] = x;
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

static uint8_t *const kPolyScreenRamMain = (uint8_t *)0x0400;

static uint8_t *const kPolyScreenRowPtrsMain[kViewportHeight] = {
    kPolyScreenRamMain + 0,   kPolyScreenRamMain + 40,
    kPolyScreenRamMain + 80,  kPolyScreenRamMain + 120,
    kPolyScreenRamMain + 160, kPolyScreenRamMain + 200,
    kPolyScreenRamMain + 240, kPolyScreenRamMain + 280,
    kPolyScreenRamMain + 320, kPolyScreenRamMain + 360,
    kPolyScreenRamMain + 400, kPolyScreenRamMain + 440,
    kPolyScreenRamMain + 480, kPolyScreenRamMain + 520};

// Fill the polygon using the traced edges
void fill_poly(vertex_t *vertices, uint8_t num_vertices,
               unsigned char fill_char) {
  if (num_vertices < 3) {
    return; // A polygon needs at least 3 vertices
  }

  bm_start();
  _clear_buffers();
  bm_end(880, SCREEN_STR("clear:"));

  // 1. Trace all edges
  bm_start();
  for (uint8_t i = 0; i < num_vertices; i++) {
    uint8_t next = (i + 1);
    if (next == num_vertices)
      next = 0; // Wrap around to the first vertex

    _trace_edge(vertices[i].x, vertices[i].y, vertices[next].x,
                vertices[next].y);
  }
  bm_end(920, SCREEN_STR("trace:"));

  // 2. Draw the scanlines directly to Screen RAM
  bm_start();
  for (int8_t y = 0; y < kViewportHeight; y++) {
    int8_t left = _min_x[y];
    int8_t right = _max_x[y];

    // Clip X coordinates to the physical screen width
    if (left < 0) {
      left = 0;
    }
    if (right >= kScreenWidth) {
      right = kScreenWidth - 1;
    }

    // If valid bounds exist for this scanline, fill it
    if (left <= right) {
      // Calculate row start pointer to avoid multiplying y * 40 in the inner
      // loop
      unsigned char *row_ptr = kPolyScreenRowPtrsMain[y];
      for (int8_t x = left; x <= right; x++) {
        row_ptr[x] = fill_char;
      }
    }
  }
  bm_end(960, SCREEN_STR("scan: "));
}
