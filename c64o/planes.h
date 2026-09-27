#ifndef PLANES_H
#define PLANES_H

#include <stdint.h>

#include "bool.h"
#include "vec.h"

// The traffic-sprite renderer of docs/planes.md, ported from its reference,
// lib/planes.py, and held to it byte for byte by test/planes_test.cc.
//
// One call per aircraft per frame: project the polygon model, choose the
// pixel size and the sprite layout, and fill the silhouette into up to four
// sprite blocks. The caller owns the blocks and the hardware sprites; this
// only says what to put where.

// The horizontal pixel size, from distance (docs/planes.md section 4).
static const uint8_t kPlaneLevelDot = 0;
static const uint8_t kPlaneLevel1x = 1;
static const uint8_t kPlaneLevelX = 2;

// A sprite block holds 21 rows of 3 bytes.
static const uint8_t kPlaneBlockBytes = 63;

// Where the aircraft is drawn and where sprite DMA stops.
//
// ppilot's viewport is centred on (160, 56), and the last line a sprite may
// start on is 89, or 68 if it is Y-expanded (mem.h). A program with no panel
// under its sprites passes cuts below the bottom of the screen and nothing
// ever slides.
struct planes_view_t {
  int16_t cx0, cy0;
  int16_t cut1, cut2;
};

// The vertex cache's key: the level and layout, then every vertex in buffer
// coordinates, two bytes an axis.
static const uint8_t kPlaneKeyMax = 4 + 32 * 4;

// Everything that persists between frames: the hysteresis latches and the
// vertex cache. One per aircraft.
struct planes_state_t {
  uint8_t level, ys, cols, rows;
  bool key_valid;
  uint8_t key_len;
  uint8_t key[kPlaneKeyMax];
};

enum planes_hidden_t {
  kPlaneShown = 0,
  kPlaneBehindCamera,
  kPlaneOutOfRange,
  kPlaneBelowCut,
};

struct planes_frame_t {
  uint8_t hidden;       // a planes_hidden_t; kPlaneShown when drawn
  uint8_t level;        // a kPlaneLevel*
  uint8_t xs, ys;       // 1 or 2: X- and Y-expansion
  uint8_t cols, rows;   // the sprite layout
  int16_t x, y;         // top left of the layout, viewport pixels
  uint8_t slid;         // lines moved up to clear the DMA cut
  bool clamped;         // apparent size held by the size cap
  bool cached;          // nothing drawn: the last bitmap still stands
  uint16_t k;           // perspective scale, after the cap
  uint8_t d;            // unforeshortened diameter, pixels
};

void planes_state_init(planes_state_t *state);

// One aircraft, one frame.
//
// `c` is its position in camera space, in quarter metres, and `axes` its
// front, left and up in camera space, 8.8. On a miss the silhouette is drawn
// into `back`: frame->cols * frame->rows blocks, row-major, which the caller
// then shows. On a hit (frame->cached) and in the dot tier nothing is drawn
// and `back` is not touched; for the dot the caller shows the block it filled
// with planes_dot_bitmap() once.
void planes_render(planes_state_t *state, const planes_view_t *view,
                   const vec3_t *c, const mat3_t *axes, uint8_t *const *back,
                   planes_frame_t *frame);

// The far tier's static bitmap: a 2 x 2 blob on the block's last two rows.
void planes_dot_bitmap(uint8_t *block);

// Where the dot's blob sits in its block, for placing the sprite.
static const uint8_t kPlaneDotX = 12;
static const uint8_t kPlaneDotY = 19;

#pragma compile("planes.cc")

#endif
