#ifndef PLANES_H
#define PLANES_H

#include <stdint.h>

#include "bool.h"
#include "vec.h"

// The opponent-sprite renderer of docs/planes.md, ported from its reference,
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

// A sprite block holds 21 rows of 3 bytes, and blocks sit 64 bytes apart.
static const uint8_t kPlaneBlockBytes = 63;
static const uint8_t kPlaneBlockStride = 64;

// Where the aircraft is drawn, where sprite DMA stops, and how big it may get.
//
// ppilot's viewport is centred on (160, 56), and the last line a sprite may
// start on is 89, or 68 if it is Y-expanded (mem.h). A program with no panel
// under its sprites passes cuts below the bottom of the screen and nothing
// ever slides.
//
// kmax is the size cap, the largest perspective scale drawn: kPlaneKMax
// (planedef.h) lets the aircraft grow to 2 x 2 sprites, and
// kPlaneKMaxTwoSprites holds it to two at every attitude (docs/planes.md §13).
//
// kn is the perspective scale's numerator, k = kn * 512 / C.x: kPlaneKnModel
// draws the model at its own size, and more draws it bigger in proportion,
// reaching the cap - and every pixel size - that much farther out.
struct planes_view_t {
  int16_t cx0, cy0;
  int16_t cut1, cut2;
  uint16_t kmax;
  uint8_t kn;
};

// The model at its own size: k = 32768 / C.x.
static const uint8_t kPlaneKnModel = 64;

// The largest kmax that never needs more than two sprites. Measured by a sweep
// of every attitude in 2.5 degree steps, hysteresis carried from one attitude
// to the next: two hold up to 90, about 91 m, and 92 needs four. Two below
// that for margin, which freezes the size at about 93 m.
static const uint16_t kPlaneKMaxTwoSprites = 88;

// The most vertices a frame has: the flat surfaces, the fuselage outline and
// the end-on disc.
static const uint8_t kPlaneVertMax = 32;

// A bitmap's layout: the pixel size, Y-expansion and sprites (the hysteresis
// latches), and where the buffer sits - its top left less the centre before
// the slide, and the slide. Kept on its own to show a bitmap at a later
// frame's centre than the one it was projected in (planes_place()).
struct planes_layout_t {
  uint8_t level, ys, cols, rows;
  int16_t ox, oy, slid;
};

// Everything that persists between frames: the hysteresis latches and the
// caches. One per aircraft.
struct planes_state_t {
  // The last projection's layout. ox, oy and slid mean something only while
  // key_valid.
  planes_layout_t lay;
  // The rest describes the last frame that projected the model, if
  // key_valid; the dot tier and planes_state_init() clear it.
  bool key_valid;
  // The vertex cache: that frame's vertices in buffer coordinates, under the
  // layout in the latches above. They are the last bitmap drawn. A byte
  // each: x is inside the buffer, and y is too until the slide pushes it
  // down, so y is kept modulo 256 and the slide's 128-line band beside it --
  // within one band no two rows share a byte.
  uint8_t key_count, key_band;
  // key_y must follow key_x: planes_asm.cc reaches both from key_x.
  uint8_t key_x[kPlaneVertMax], key_y[kPlaneVertMax];
  // How many polygons those vertices make: the three flat surfaces, then
  // the fuselage and the end-on disc where the attitude shows them. With
  // them, the key is everything planes_fill() needs.
  uint8_t key_poly_count;
  // What they were projected from -- the scale, and the screen components
  // (y, z) of the front, left and up axes. The same scale and axes project
  // the same silhouette again, moved with the centre.
  uint16_t k;
  int16_t axes[6];
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
  int16_t cx, cy;       // the centre, viewport pixels
};


void planes_state_init(planes_state_t *state);

// One aircraft, one frame.
//
// `c` is its position in camera space, in quarter metres, and `axes` its
// front, left and up in camera space: unit vectors in 8.8, so no component
// reaches 512. An aircraft more than 45 degrees off the view axis, up or
// across, is out of range -- far outside any viewport, and past where the
// projection's division is exact.
//
// On a miss the silhouette is drawn into `back`, four sprite blocks 64 bytes
// apart: frame->cols * frame->rows of them, row-major, which the caller then
// shows. On a hit (frame->cached) and in the dot tier nothing is drawn and
// `back` is not touched; for the dot the caller shows the block it filled
// with planes_dot_bitmap() once.
void planes_render(planes_state_t *state, const planes_view_t *view,
                   const vec3_t *c, const mat3_t *axes, uint8_t *back,
                   planes_frame_t *frame);

// planes_render() in two halves, for a caller that spreads a redraw over two
// frames (docs/planes.md §13, "The silhouette at half rate"):
//
// - planes_project() is everything but the fill. On a miss - shown, not the
//   dot, not cached - the state holds the new silhouette and its layout,
//   and the frame describes them; the bitmap is not drawn yet.
// - planes_fill() draws the state's silhouette into `back`, on this frame or
//   a later one, whatever other aircraft were projected in between, as long
//   as this state was not projected again.
//
// Until the new bitmap is filled the old one is on screen, and the caller
// shows it at each frame's centre with planes_place(), having kept a copy of
// state->lay when it was filled. planes_centre() is the first part of
// planes_project(), for the frames that only place.
void planes_project(planes_state_t *state, const planes_view_t *view,
                    const vec3_t *c, const mat3_t *axes, planes_frame_t *frame);
void planes_fill(const planes_state_t *state, uint8_t *back);
bool planes_centre(const planes_view_t *view, const vec3_t *c, planes_frame_t *frame);
// A bitmap of layout `lay` at frame->cx, cy: level, layout, top left and
// slide, and hidden if the dot is below the cut. A bitmap whose last row
// would now start below the DMA cut is moved up to it. Returns the slide
// this centre needs, which planes_project() compares with lay->slid.
int16_t planes_place(const planes_layout_t *lay, const planes_view_t *view,
                     planes_frame_t *frame);

// The far tier's static bitmap: a 2 x 2 blob on the block's last two rows.
void planes_dot_bitmap(uint8_t *block);

// Where the dot's blob sits in its block, for placing the sprite.
static const uint8_t kPlaneDotX = 12;
static const uint8_t kPlaneDotY = 19;

#pragma compile("planes.cc")
#pragma compile("planes_asm.cc")

#endif
