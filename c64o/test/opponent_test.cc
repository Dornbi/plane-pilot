// Host test for opponent.cc: what each frame offers the sprite stack, and
// where the aircraft fly.
//
// planes_edge_test.cc says planes.cc draws the right thing for each frame.
// This says opponent.cc shows it: a redraw takes two frames there (docs/
// planes.md §13, "The silhouette at half rate"), and until the new bitmap is
// filled the old one stands in, placed at the new centre. That is right only
// while the old one is recent. A bitmap left from the last time the aircraft
// was in view can be any size, and once was: a chase that looked away and
// back saw a frame of the aircraft as close as it had been.
//
// So the rule here, at every frame and for each slot: the sprite offered is
// the dot, or a silhouette the size of one drawn for this frame or one of the
// two before it, with the aircraft on screen all the way since. "Drawn for" is
// planes.cc's own answer - a copy of the slot's state with its cache thrown
// away, rendered before opponent.cc renders the frame - so the hysteresis is
// the same and the sizes compare exactly. With two aircraft on screen the two
// take turns at redrawing, one redraw in flight at a time, and a slot can
// wait two frames more: the window is then this frame and the four before.
// Every frame also checks the turns - no frame both fills one slot and starts
// a redraw of another - that every aircraft in view is offered, and that the
// two fit the stack's seven sprites, less the back view's fin: the farther,
// or in the back view both, held to two.
//
// The paths are checked by flying them: the patrol goes round its waypoints in
// order, near its square, without weaving on the legs; the pass flies dead
// straight; and the bank leans the right way.
//
// opponent.cc is #included rather than linked, for its statics; the flight
// and world globals it reads, and the sprite stack it writes, are defined
// here, and the missions are fmission.cc's own. Its sprite blocks sit in kOpponentBank, which on the host is
// the array below.

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../color.h"
#include "../flight.h"
#include "../mission.h"
#include "../planes.h"
#include "../sprites.h"
#include "../vec.h"
#include "../view.h"
#include "../world.h"

// --- What opponent.cc reads ---------------------------------------------------

bool flight_paused = false;
bool model_on_ground = false;
view_state_t view_state = VIEW_CENTER;
enum FlightStatus flight_status;
uint8_t flight_step_shift = 0;
uint8_t kFlightSubstepMask = 0;
int32_t flight_eye_x, flight_eye_y, flight_eye_z;
int32_t *const kFlightEye[3] = {&flight_eye_x, &flight_eye_y, &flight_eye_z};
mat3_t world_cam;
// The player's heading, for a hunter breaking off: head-on or not.
mat3_t flight_cam;
uint16_t flight_nav_point_x[kMaxNavPoints];
uint16_t flight_nav_point_y[kMaxNavPoints];

static uint8_t _bank[0x4000];
uint8_t *kOpponentBank = _bank;

// gun.cc's say in the sprite budget: whether a tracer is up.
static bool _tracer_up = false;
bool gun_tracer_up(void) { return _tracer_up; }
bool gun_puff_up(void) { return false; }


// --- What it writes -----------------------------------------------------------

struct offer_t {
  bool on;
  int16_t x, y;
  uint8_t bitmap, bitmap2, color, flags;
};
static offer_t _offer[2];

static bool _test_offer(uint8_t slot, int16_t depth, int16_t x, int16_t y, int8_t pivot_x,
                        int8_t pivot_y, uint8_t bitmap, uint8_t bitmap2, uint8_t color,
                        uint8_t flags) {
  (void)depth;
  (void)pivot_x;
  (void)pivot_y;
  _offer[slot] = {true, x, y, bitmap, bitmap2, color, flags};
  return true;
}

// opponent_add_sprites() offers each aircraft from inside its loop over the
// slots, as `i`; the macro passes it on, so that each offer says whose it is.
#define sprites_stack_add(...) _test_offer(i, __VA_ARGS__)
// kSquare's and kStraight's waypoints (section 6), after the missions' in
// the pool.
#define OPPONENT_TEST_WP_N 8
#define OPPONENT_TEST_WP_X \
  , 0x1600, 0x1600, 0x0A00, 0x0A00, 0x3A70, 0x5A70, 0x7A70, 0x9A70
#define OPPONENT_TEST_WP_Y \
  , 0x6200, 0x6E00, 0x6E00, 0x6200, 0x3FE0, 0x3FE0, 0x3FE0, 0x3FE0
#include "../opponent.cc"
#undef sprites_stack_add

// --- Checks -------------------------------------------------------------------

static int _fails = 0;

#define CHECK(cond, ...)               \
  do {                                 \
    if (!(cond)) {                     \
      if (++_fails <= 20) {            \
        printf("opponent_test: ");     \
        printf(__VA_ARGS__);           \
        printf("\n");                  \
      }                                \
    }                                  \
  } while (0)

// The extent of the ink in some sprite blocks, in screen pixels.
struct size_t2 {
  int w, h;
  bool operator==(const size_t2 &o) const { return w == o.w && h == o.h; }
};

static size_t2 extent(const uint8_t *blocks[4], int cols, int rows, int xs, int ys) {
  int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
  for (int b = 0; b < cols * rows; ++b) {
    int bc = b % cols, br = b / cols;
    for (int row = 0; row < 21; ++row) {
      for (int col = 0; col < 24; ++col) {
        if (blocks[b][row * 3 + (col >> 3)] & (0x80 >> (col & 7))) {
          int x = (bc * 24 + col) * xs, y = (br * 21 + row) * ys;
          x0 = std::min(x0, x);
          x1 = std::max(x1, x + xs - 1);
          y0 = std::min(y0, y);
          y1 = std::max(y1, y + ys - 1);
        }
      }
    }
  }
  if (x1 < 0) return {0, 0};
  return {x1 - x0 + 1, y1 - y0 + 1};
}

// The offered sprite's extent, read from the blocks it names.
static size_t2 offered_extent(const offer_t &o) {
  int cols = (o.flags & kSpriteFlagWide) ? 2 : 1;
  int rows = o.bitmap2 != kSpriteNoBitmap ? 2 : 1;
  const uint8_t *blocks[4];
  blocks[0] = _opponent_block(o.bitmap);
  blocks[1] = _opponent_block((uint8_t)(o.bitmap + 1));
  int lower = cols;
  if (rows == 2) {
    blocks[lower] = _opponent_block(o.bitmap2);
    blocks[lower + 1] = _opponent_block((uint8_t)(o.bitmap2 + 1));
  }
  return extent(blocks, cols, rows, (o.flags & kSpriteFlagExpandX) ? 2 : 1,
                (o.flags & kSpriteFlagExpandY) ? 2 : 1);
}

// --- The scene ----------------------------------------------------------------

// A camera, as world_cam: its front, left and up in world axes, 8.8.
static mat3_t camera(int heading, int pitch, int roll) {
  const double r = M_PI / 180.0;
  double h = heading * r, p = pitch * r, b = roll * r;
  double f[3] = {cos(p) * cos(h), cos(p) * sin(h), sin(p)};
  double l0[3] = {-sin(h), cos(h), 0};
  double u0[3] = {-sin(p) * cos(h), -sin(p) * sin(h), cos(p)};
  int16_t l[3], u[3], ff[3];
  for (int i = 0; i < 3; ++i) {
    ff[i] = (int16_t)lround(256 * f[i]);
    l[i] = (int16_t)lround(256 * (cos(b) * l0[i] + sin(b) * u0[i]));
    u[i] = (int16_t)lround(256 * (-sin(b) * l0[i] + cos(b) * u0[i]));
  }
  mat3_t m;
  m.front = make_vector(ff[0], ff[1], ff[2]);
  m.left = make_vector(l[0], l[1], l[2]);
  m.up = make_vector(u[0], u[1], u[2]);
  return m;
}

// Where the aircraft is: `range` eighth metres ahead of the camera, `px` and
// `py` pixels right of and above the viewport's centre; or nowhere in view.
struct where_t {
  int range, px, py;
  bool away;  // off to the side, out of the viewport
};

// The frame's record, for the ones after it.
struct seen_t {
  bool offered;   // anything at all was offered
  bool visible;   // planes.cc shows something here, the dot or a silhouette
  bool sized;     // planes.cc draws a silhouette here, not the dot
  size_t2 size;
};

// Up to the last five frames a slot can be showing a bitmap from.
static const int kHist = 5;

struct scene_t {
  int slots;                // 1, or 2 with both aircraft on
  int window;               // frames back a silhouette may come from
  seen_t hist[2][kHist];    // per slot: this frame, the one before, ...
  int frames, silhouettes, dots_standing_in;
  int shared;        // both offered, more than four sprites between them
  int four_and_two;  // one at 2 x 2 with the other held to two
};

// Slot `slot` at `w`, from an eye in the middle of the world.
static void place(int slot, const where_t &w) {
  // Camera space, then world: the camera's axes are world_cam's rows.
  double cx = w.range;
  double cy = w.away ? 3.0 * w.range : -(double)w.px * w.range / 256.0;
  double cz = (double)w.py * w.range / 256.0;
  const mat3_t &m = world_cam;
  double rel[3];
  rel[0] = (cx * m.front.x + cy * m.left.x + cz * m.up.x) / 256.0;
  rel[1] = (cx * m.front.y + cy * m.left.y + cz * m.up.y) / 256.0;
  rel[2] = (cx * m.front.z + cy * m.left.z + cz * m.up.z) / 256.0;
  // Eighth metres to 24.8 half metres.
  flight_eye_x = 40L << 16;
  flight_eye_y = 100L << 16;
  flight_eye_z = 8L << 16;
  _opponent_x[slot] = flight_eye_x + (lround(rel[0]) << 6);
  _opponent_y[slot] = flight_eye_y + (lround(rel[1]) << 6);
  _opponent_z[slot] = flight_eye_z + (lround(rel[2]) << 6);
}

// planes.cc's answer for slot `slot` this frame, from its latches as they
// were before opponent.cc drew the frame, and under the view it chose: held
// to two sprites if `two`.
static seen_t reference(int slot, const planes_state_t &latches, bool two) {
  seen_t now = {false, false, false, {0, 0}};
  uint8_t range = _opponent_camera((uint8_t)slot);
  vec3_t c = _opponent_c;
  if (range != kOpponentOut) {
    _opponent_set_view(range == kOpponentFar, two);
    const planes_view_t &v = _opponent_view;
    mat3_t axes;
    _opponent_axes((uint8_t)slot, &axes);
    static planes_state_t fresh;
    static uint8_t blocks[4][kPlaneBlockStride];
    fresh = latches;
    fresh.key_valid = false;
    planes_frame_t f;
    planes_render(&fresh, &v, &c, &axes, blocks[0], &f);
    now.visible = f.hidden == kPlaneShown;
    if (f.hidden == kPlaneShown && f.level != kPlaneLevelDot) {
      const uint8_t *b[4] = {blocks[0], blocks[1], blocks[2], blocks[3]};
      now.sized = true;
      now.size = extent(b, f.cols, f.rows, f.xs, f.ys);
    }
  }
  return now;
}

static int popcount(uint8_t v) {
  int n = 0;
  for (; v; v &= (uint8_t)(v - 1)) ++n;
  return n;
}

// Hardware sprites an offer takes.
static int sprites_of(const offer_t &o) {
  if (!o.on) return 0;
  return ((o.flags & kSpriteFlagWide) ? 2 : 1) * (o.bitmap2 != kSpriteNoBitmap ? 2 : 1);
}

static void frame(scene_t &sc, const where_t *w, const char *what) {
  static planes_state_t latches[2];
  for (int k = 0; k < sc.slots; ++k) place(k, w[k]);
  // The cull and the cap first, as opponent_add_sprites() does them again:
  // the cap can start a slot's latches afresh (_opponent_cap()), and the
  // reference has to draw from the latches the frame does. A second pass
  // changes nothing.
  if (_opponent_cull() == 3) {
    _opponent_cap();
  } else {
    _opponent_capped = 0;
  }
  for (int k = 0; k < sc.slots; ++k) latches[k] = _opponent_planes[k];

  uint8_t before = _opponent_pending;
  uint8_t sets = _opponent_front;
  _offer[0].on = _offer[1].on = false;
  opponent_add_sprites();
  uint8_t after = _opponent_pending;
  // A fill flips the slot's front set.
  bool filled = _opponent_front != sets;
  ++sc.frames;
  seen_t now[2];
  for (int k = 0; k < sc.slots; ++k) {
    now[k] = reference(k, latches[k], (_opponent_capped & (1 << k)) != 0);
  }

  // The stack's seven, less the one the fin takes beyond the mark's index in
  // the back view: both always fit.
  int sprites = sprites_of(_offer[0]) + sprites_of(_offer[1]);
  int budget = kSpriteTerrainSlots - (view_state == VIEW_BACK ? 1 : 0);
  CHECK(sprites <= budget, "%s: frame %d offers %d sprites, %d fit", what, sc.frames, sprites,
        budget);
  if (_offer[0].on && _offer[1].on && sprites > 4) ++sc.shared;
  // In front the one held to two is the farther, give or take the eighth
  // that keeps two at about the same range from trading.
  if (_opponent_capped == 1 || _opponent_capped == 2) {
    int far = _opponent_capped == 1 ? 0 : 1;
    int16_t dn = _opponent_cs[far ^ 1].x, df = _opponent_cs[far].x;
    // Both in one range, by depth; else the one in the far range, whose
    // depth is in half metres.
    if (_opponent_far == 0 || _opponent_far == 3) {
      CHECK(df >= dn - (dn >> 3), "%s: frame %d holds slot %d to two at %d, the other at %d",
            what, sc.frames, far, df, dn);
    } else {
      CHECK(_opponent_far == 1 << far, "%s: frame %d holds slot %d to two, slot %d is far",
            what, sc.frames, far, _opponent_far == 1 ? 0 : 1);
    }
  }
  for (int k = 0; k < sc.slots; ++k) {
    if (sprites_of(_offer[k]) == 4 && _offer[k ^ 1].on && sprites_of(_offer[k ^ 1]) == 2) {
      ++sc.four_and_two;
    }
  }
  CHECK(popcount(after) <= 1, "%s: frame %d, %d redraws in flight", what, sc.frames,
        popcount(after));
  CHECK(!filled || !(after & ~before),
        "%s: frame %d starts a redraw and fills another", what, sc.frames);

  for (int k = 0; k < sc.slots; ++k) {
    const offer_t &o = _offer[k];
    now[k].offered = o.on;
    // Clear of the bottom of the viewport, where a stale bitmap's slide can
    // hide what a fresh one would show, every aircraft in view is offered.
    CHECK(o.on || !now[k].visible || w[k].py < 0,
          "%s: frame %d, slot %d %d m out is in view and not offered", what, sc.frames, k,
          w[k].range / 4);
    seen_t *hist = sc.hist[k];
    for (int n = kHist - 1; n > 0; --n) hist[n] = hist[n - 1];
    hist[0] = now[k];
    if (!o.on || o.bitmap == kOpponentBlockDot) {
      if (o.on && now[k].sized) ++sc.dots_standing_in;
      continue;
    }
    ++sc.silhouettes;
    CHECK(o.bitmap >= kOpponentBlockFirst[k] &&
              o.bitmap < kOpponentBlockFirst[k] + 2 * kOpponentSetBlocks,
          "%s: frame %d, slot %d shows block %d, not one of its own", what, sc.frames, k,
          o.bitmap);
    size_t2 shown = offered_extent(o);
    bool recent = false;
    for (int n = 0; n <= sc.window && hist[n].offered; ++n) {
      if (hist[n].sized && hist[n].size == shown) {
        recent = true;
        break;
      }
    }
    CHECK(recent,
          "%s: frame %d, slot %d %d m out, shows a %dx%d silhouette; the last frames drew "
          "%dx%d, %dx%d, %dx%d",
          what, sc.frames, k, w[k].range / 4, shown.w, shown.h, hist[0].size.w,
          hist[0].size.h, hist[1].offered ? hist[1].size.w : -1,
          hist[1].offered ? hist[1].size.h : -1, hist[2].offered ? hist[2].size.w : -1,
          hist[2].offered ? hist[2].size.h : -1);
  }
}

static void frame(scene_t &sc, const where_t &w, const char *what) { frame(sc, &w, what); }

static void start(scene_t &sc, const mat3_t &cam, int slots = 1) {
  world_cam = cam;
  for (int k = 0; k < 2; ++k) {
    _opponent_on[k] = k < slots;
    _opponent_head[k] = make_vector(256, 0, 0);
    _opponent_turn[k] = 0;
    _opponent_bank[k] = 0;
  }
  _opponent_first = 0;
  opponent_invalidate();
  memset(&sc, 0, sizeof(sc));
  sc.slots = slots;
  sc.window = slots == 1 ? 2 : 4;
}

// --- 1. Looking away from a close chase, and back --------------------------------
//
// Close behind it, 2 x 2 sprites; then off to the side while it pulls ahead
// to 300 m; then in view again. The first frame back may show the dot, never
// the 2 x 2.

static void test_look_back(void) {
  static const int kHeadings[] = {0, 90, 180, 300};
  for (int h : kHeadings) {
    for (int one_frame = 0; one_frame < 2; ++one_frame) {
      scene_t sc;
      start(sc, camera(h, 5, 20));
      char what[64];
      snprintf(what, sizeof(what), "look back, camera %d%s", h, one_frame ? ", pending" : "");
      for (int n = 0; n < 8; ++n) frame(sc, {240, 10, 10, false}, what);
      if (one_frame) {
        // Moved, so projected and still to be filled when it goes.
        frame(sc, {230, 10, 10, false}, what);
      }
      for (int n = 0; n < 30; ++n) frame(sc, {240 + n * 70, 10, 10, true}, what);
      for (int n = 0; n < 6; ++n) frame(sc, {2400 + n * 4, 10, 10, false}, what);
      CHECK(sc.silhouettes > 0, "%s: never drawn", what);
    }
  }
}

// --- 2. Down to the dot and back up ---------------------------------------------
//
// Close, then away to the dot in view, then off screen, then back in at the
// near edge of 1:1.

static void test_through_the_dot(void) {
  scene_t sc;
  start(sc, camera(90, 0, 0));
  const char *what = "through the dot";
  for (int n = 0; n < 6; ++n) frame(sc, {200, 0, 15, false}, what);
  for (int r = 200; r < 9000; r = r * 21 / 20) frame(sc, {r, 0, 15, false}, what);
  for (int n = 0; n < 10; ++n) frame(sc, {200, 0, 15, true}, what);
  for (int n = 0; n < 6; ++n) frame(sc, {200, 0, 15, false}, what);
  for (int r = 9000; r > 200; r = r * 20 / 21) frame(sc, {r, 0, 15, false}, what);
}

// --- 2b. In on a diagonal ---------------------------------------------------------
//
// Dead ahead from 4 km to 150 m, on bearings between the world's axes and
// climbing: on screen in every frame, the dot or a silhouette. The near range
// is a box, 16 units on each world axis, so its corners are ~2.9 km out on a
// diagonal; planes.cc's old 2 km range cull sat inside it, and an aircraft
// coming in on a diagonal vanished from ~2.9 km to 2 km.

static void test_diagonal(void) {
  static const int kCams[][2] = {{0, 0}, {30, 0}, {45, 0}, {45, 30}, {135, -20}, {225, 10}};
  for (const auto &cam : kCams) {
    scene_t sc;
    start(sc, camera(cam[0], cam[1], 0));
    char what[64];
    snprintf(what, sizeof(what), "diagonal, camera %d up %d", cam[0], cam[1]);
    int missing = 0, first = -1;
    for (int r = 32000; r > 1200; r -= 100) {
      frame(sc, {r, 0, 0, false}, what);
      if (!_offer[0].on) {
        ++missing;
        if (first < 0) first = r / 8;
      }
    }
    CHECK(missing == 0, "%s: not drawn in %d frames, the first %d m out", what, missing, first);
  }
}

// --- 3. Everything at once ------------------------------------------------------
//
// A random walk: mostly smooth, a few percent of the range and a pixel or two
// a frame, with jumps in range and position and spells off screen, some of
// them a single frame straight after a move. Clear of the bottom of the
// viewport, where the slide clips what a stale and a fresh bitmap show.

static uint32_t _seed = 1;
static int rnd(int n) {
  _seed = _seed * 1103515245u + 12345u;
  return (int)((_seed >> 16) % (uint32_t)n);
}

static void test_random(void) {
  static const int kCameras[][3] = {{0, 0, 0}, {90, 5, 0}, {180, -5, 30}, {45, 20, -45},
                                    {270, 0, 90}, {135, -30, 10}};
  int frames = 0, silhouettes = 0, dots = 0;
  for (const auto &cam : kCameras) {
    scene_t sc;
    start(sc, camera(cam[0], cam[1], cam[2]));
    where_t w = {1600, 0, 10, false};
    int away = 0;
    char what[64];
    snprintf(what, sizeof(what), "random walk, camera %d %d %d", cam[0], cam[1], cam[2]);
    for (int n = 0; n < 20000; ++n) {
      int r = rnd(100);
      if (away > 0) {
        --away;
        w.away = away > 0;
      } else if (r < 4) {
        away = 1 + (rnd(4) == 0 ? 1 : rnd(20));
        w.away = true;
      } else if (r < 8) {
        w.range = (int)(200 * pow(30.0, rnd(1000) / 1000.0));
        w.px = rnd(200) - 100;
        w.py = rnd(50);
      } else {
        w.range = std::max(160, std::min(7000, w.range * (1000 + rnd(61) - 30) / 1000));
        w.px = std::max(-120, std::min(120, w.px + rnd(5) - 2));
        w.py = std::max(0, std::min(50, w.py + rnd(5) - 2));
      }
      frame(sc, w, what);
    }
    frames += sc.frames;
    silhouettes += sc.silhouettes;
    dots += sc.dots_standing_in;
  }
  printf("  random walk: %d frames, %d silhouettes, %d frames of the dot standing in\n",
         frames, silhouettes, dots);
}

// --- 4. Two at once -------------------------------------------------------------
//
// The same walk for both slots, each its own, and each turning now and then
// and banking, so that their axes change as well as their range.

static void walk(where_t &w, int &away) {
  int r = rnd(100);
  if (away > 0) {
    --away;
    w.away = away > 0;
  } else if (r < 3) {
    away = 1 + (rnd(4) == 0 ? 1 : rnd(20));
    w.away = true;
  } else if (r < 6) {
    w.range = (int)(200 * pow(30.0, rnd(1000) / 1000.0));
    w.px = rnd(200) - 100;
    w.py = rnd(50);
  } else {
    w.range = std::max(160, std::min(7000, w.range * (1000 + rnd(61) - 30) / 1000));
    w.px = std::max(-120, std::min(120, w.px + rnd(5) - 2));
    w.py = std::max(0, std::min(50, w.py + rnd(5) - 2));
  }
}

static void test_two(void) {
  static const int kCameras[][3] = {{0, 0, 0}, {90, 5, 0}, {200, -10, 25}, {315, 15, -60}};
  int frames = 0, silhouettes = 0, dots = 0;
  for (const auto &cam : kCameras) {
    scene_t sc;
    start(sc, camera(cam[0], cam[1], cam[2]), 2);
    where_t w[2] = {{1600, -30, 10, false}, {900, 40, 20, false}};
    int away[2] = {0, 0};
    char what[64];
    snprintf(what, sizeof(what), "two, camera %d %d %d", cam[0], cam[1], cam[2]);
    for (int n = 0; n < 20000; ++n) {
      for (int k = 0; k < 2; ++k) {
        walk(w[k], away[k]);
        if (rnd(10) == 0) {
          _opponent_turn[k] = _opponent_bank[k] = (int8_t)(rnd(3) - 1);
        }
        if (_opponent_turn[k] != 0) {
          vec3_t &h = _opponent_head[k];
          int16_t s = _opponent_turn[k] > 0 ? kOpponentTurn : -kOpponentTurn;
          int16_t hx = h.x, hy = h.y;
          h.x = hx - vec_fastmul8p8(s, hy);
          h.y = hy + vec_fastmul8p8(s, hx);
          vec_normalize(&h);
        }
      }
      frame(sc, w, what);
    }
    frames += sc.frames;
    dots += sc.dots_standing_in;
    silhouettes += sc.silhouettes;
  }
  CHECK(silhouettes > frames / 2, "two: only %d silhouettes in %d frames", silhouettes, frames);
  printf("  two at once: %d frames, %d silhouettes, %d frames of the dot standing in\n",
         frames, silhouettes, dots);
}

// --- 5. Both close -------------------------------------------------------------
//
// Both inside ~60 m, where either alone would be 2 x 2, walking in and out
// past each other so that the nearer changes, in the front view and the back.
// The budget check in frame() is the point: the farther is held to two
// sprites, in front and behind alike.

static void test_both_close(void) {
  static const view_state_t kViews[] = {VIEW_CENTER, VIEW_BACK};
  for (view_state_t v : kViews) {
    view_state = v;
    scene_t sc;
    start(sc, camera(30, 5, 10), 2);
    where_t w[2] = {{300, -40, 10, false}, {420, 40, 20, false}};
    char what[64];
    snprintf(what, sizeof(what), "both close, %s view", v == VIEW_BACK ? "back" : "front");
    for (int n = 0; n < 6000; ++n) {
      for (int k = 0; k < 2; ++k) {
        w[k].range = std::max(170, std::min(500, w[k].range + rnd(31) - 15));
        w[k].px = std::max(-90, std::min(90, w[k].px + rnd(5) - 2));
        w[k].py = std::max(0, std::min(40, w[k].py + rnd(3) - 1));
        if (rnd(20) == 0) {
          _opponent_turn[k] = _opponent_bank[k] = (int8_t)(rnd(3) - 1);
        }
      }
      frame(sc, w, what);
    }
    CHECK(sc.four_and_two > 300, "%s: only %d frames of one at 2 x 2 and one at two", what,
          sc.four_and_two);
    printf("  %s: %d frames, %d with more than four sprites between them, %d of 2 x 2 and "
           "two\n",
           what, sc.frames, sc.shared, sc.four_and_two);
  }
  view_state = VIEW_CENTER;
}

// --- 6. The paths ---------------------------------------------------------------

// Metres from a waypoint coordinate, 8.8 units, to a position, 24.8 half
// metres.
static double metres(uint16_t w, int32_t pos, bool wrap) {
  double d = ((double)((int32_t)w << 8) - (double)pos) / 512.0;
  if (wrap) {
    d = fmod(d + 8192.0 + 32768.0, 16384.0) - 8192.0;
  }
  return d;
}

// Two paths of the test's own, flown as opponent_reset() starts a mission's:
// dead straight north for ever, the world repeating every 128 units of x,
// and a 1.5 km square anticlockwise. Both paths' waypoints are appended to
// the pool after the missions' (OPPONENT_TEST_WP_X/Y above): the square's,
// then the straight one's, 32 units apart, the fourth 128 units on from its
// start. A test path is a pair's row and a slot's path in one: the two
// slots share the pair (_opponent_pair), so two flown at once share it too.
struct test_path_t {
  opponent_pair_t pair;
  opponent_path_t path;
};
static const test_path_t kStraight = {{4, kOpponentNoFail, 1500},
    {kOpponentLoop, kOpponentWpCount + 4, 0x1A70, 0x3FE0, 0x0060}};
static const test_path_t kSquare = {{4, kOpponentNoFail, 1500},
    {kOpponentLoop, kOpponentWpCount, 0x0A00, 0x6200, 0x0700}};
static uint16_t wp_x(const opponent_path_t &p, int w) { return kOpponentWpX[p.wp + w]; }
static uint16_t wp_y(const opponent_path_t &p, int w) { return kOpponentWpY[p.wp + w]; }

static void fly(int slot, const test_path_t *t) {
  const opponent_path_t *p = &t->path;
  _opponent_on[slot] = true;
  _opponent_pair = &t->pair;
  _opponent_path[slot] = p;
  _opponent_next[slot] = 0;
  _opponent_x[slot] = (int32_t)p->start_x << 8;
  _opponent_y[slot] = (int32_t)p->start_y << 8;
  _opponent_z[slot] = (int32_t)p->z << 8;
  _opponent_start_head(p, &_opponent_head[slot]);
  _opponent_turn[slot] = _opponent_bank[slot] = 0;
  _opponent_speed[slot] = t->pair.speed;
  _opponent_climb[slot] = 0;
}

static void test_paths(void) {
  int turning0 = 0;
  for (uint8_t shift = 0; shift < 3; ++shift) {
    flight_step_shift = shift;
    kFlightSubstepMask = (uint8_t)((1 << shift) - 1);
    flight_paused = false;
    flight_status = FLIGHT_ONGOING;
    opponent_reset(0);
    fly(0, &kStraight);
    fly(1, &kSquare);
    const test_path_t &patrol = kSquare;

    // A lap of the patrol at shift 0 is ~1,100 steps; four of them.
    const int steps = 4500 << shift;
    int reached = 0, flips = 0, weaves = 0, straight_to_turn = 0, turning = 0;
    int8_t last = 0, last_way = 0;
    double worst_out = 0, worst_len = 0;
    uint8_t next = _opponent_next[1];
    int32_t y0 = _opponent_y[0];
    for (int n = 0; n < steps; ++n) {
      int32_t x0 = _opponent_x[0];
      opponent_step();

      // The pass: dead north, a full step a step, never turning.
      CHECK(_opponent_head[0].x == 256 && _opponent_head[0].y == 0 && _opponent_turn[0] == 0,
            "paths: the pass turned at step %d", n);
      CHECK(_opponent_y[0] == y0 && _opponent_x[0] - x0 == ((kStraight.pair.speed << 1) >> shift),
            "paths: the pass moved %d, %d at step %d", (int)(_opponent_x[0] - x0),
            (int)(_opponent_y[0] - y0), n);

      // The patrol: waypoints in order, and the wings never rocked from one
      // side to the other.
      if (_opponent_next[1] != next) {
        CHECK(_opponent_next[1] == (next + 1) % patrol.pair.count,
              "paths: the patrol skipped from waypoint %d to %d", next, _opponent_next[1]);
        next = _opponent_next[1];
        ++reached;
      }
      if (_opponent_turn[1] != 0) ++turning;
      int8_t t = _opponent_bank[1];
      if (t != 0 && last != 0 && t != last) ++flips;
      if (t != 0 && last == 0) ++straight_to_turn;
      if (t != 0) {
        if (last_way != 0 && t != last_way) ++weaves;
        last_way = t;
      }
      last = t;
      const vec3_t &h = _opponent_head[1];
      double len = sqrt((double)h.x * h.x + (double)h.y * h.y);
      worst_len = std::max(worst_len, fabs(len - 256));
      CHECK(h.z == 0, "paths: the patrol's heading has a z");
      CHECK(_opponent_z[1] == (int32_t)patrol.path.z << 8, "paths: the patrol changed height");
      // How far outside its square, in metres.
      double nx = metres(wp_x(patrol.path, 0), _opponent_x[1], true);
      double sx = metres(wp_x(patrol.path, 2), _opponent_x[1], true);
      double wy = metres(wp_y(patrol.path, 1), _opponent_y[1], false);
      double ey = metres(wp_y(patrol.path, 0), _opponent_y[1], false);
      double out = std::max(std::max(-nx, sx), std::max(-wy, ey));
      worst_out = std::max(worst_out, out);
    }
    CHECK(reached >= 4 * (int)patrol.pair.count, "paths: shift %d, %d waypoints in %d steps", shift,
          reached, steps);
    CHECK(flips == 0, "paths: shift %d, the patrol rolled straight from one bank into the other "
          "%d times", shift, flips);
    // A banked turn at each corner, and now and then one on a leg: the
    // corrections, mostly a step, are not drawn banked.
    CHECK(straight_to_turn <= 2 * reached, "paths: shift %d, %d banks for %d waypoints", shift,
          straight_to_turn, reached);
    // The same turns on a faster machine: as many degrees a second, so as
    // many steps again for each step the shift splits in two.
    if (shift == 0) turning0 = turning;
    CHECK(fabs((double)turning / (turning0 << shift) - 1) < 0.1,
          "paths: shift %d, %d steps turning against %d at shift 0", shift, turning, turning0);
    // vec_normalize() takes a seven-bit square root: good to about 1%.
    CHECK(worst_len <= 4, "paths: shift %d, the heading's length is %.1f off", shift, worst_len);
    // Corners are cut by the reach and rounded by the turn, ~190 m: not more
    // than half a kilometre outside.
    CHECK(worst_out < 500, "paths: shift %d, %.0f m outside the square", shift, worst_out);
    printf("  paths, shift %d: %d waypoints in %d steps, %d banked turns, %d of them the other "
           "way, %.0f m outside at most\n",
           shift, reached, steps, straight_to_turn, weaves, worst_out);
  }
  flight_step_shift = 0;
  kFlightSubstepMask = 0;

  // The bank: from behind, level, a left turn drops the left wing (left's
  // screen z goes down) and leans the top to the left (up's screen y up).
  world_cam.front = make_vector(256, 0, 0);
  world_cam.left = make_vector(0, 256, 0);
  world_cam.up = make_vector(0, 0, 256);
  _opponent_head[0] = make_vector(256, 0, 0);
  // A path's turn, +-1, banks 30 degrees, a hunter's, +-2, 60.
  for (int t = -2; t <= 2; ++t) {
    _opponent_bank[0] = (int8_t)t;
    mat3_t a;
    _opponent_axes(0, &a);
    int sg = (t > 0) - (t < 0);
    int sn = t == 0 ? 0 : abs(t) == 1 ? 128 : 222;
    int cs = t == 0 ? 256 : abs(t) == 1 ? 222 : 128;
    CHECK(a.front.y == 0 && a.front.z == 0, "bank %d: the front moved, %d %d", t, a.front.y,
          a.front.z);
    CHECK(a.left.z == -sg * sn && a.up.y == sg * sn, "bank %d: left.z %d, up.y %d", t, a.left.z,
          a.up.y);
    CHECK(a.left.y == cs && a.up.z == cs, "bank %d: left.y %d, up.z %d", t, a.left.y, a.up.z);
  }
  _opponent_bank[0] = 0;
}

// --- 6b. The flight model ---------------------------------------------------------
//
// docs/opponent_ai.md §4, on a path of the test's own: kStraight's line at
// 2,000 ft, flown from 300 m below its height and from 300 m above. Level, the
// speed is the path's to the unit; a climb to the height settles a step's
// cost below it and a descent a step's cost above, and once there both come
// back to it. Too slow, the aircraft does not climb until the speed is back;
// too fast, it does not descend until the speed has come down.

static const test_path_t kHigh = {{4, kOpponentNoFail, 1500},
    {kOpponentLoop, kOpponentWpCount + 4, 0x1A70, 0x3FE0, 0x0400}};

// Half metres from slot 0 to kHigh's height.
static int below(void) { return (int)kHigh.path.z - (int)(_opponent_z[0] >> 8); }

static void test_flight_model(void) {
  flight_step_shift = 0;
  kFlightSubstepMask = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  opponent_reset(0);

  // Level: the path's speed and height, to the unit.
  fly(0, &kHigh);
  for (int n = 0; n < 200; ++n) {
    opponent_step();
    CHECK(_opponent_speed[0] == kHigh.pair.speed && _opponent_climb[0] == 0 && below() == 0,
          "model: level at step %d, speed %d, climb %d, %d half metres off", n,
          _opponent_speed[0], _opponent_climb[0], below());
  }

  for (int dir = 1; dir >= -1; dir -= 2) {
    const char *what = dir > 0 ? "climb" : "descent";
    fly(0, &kHigh);
    _opponent_z[0] -= (int32_t)(dir * 600) << 8;
    int steps = 0, last = below();
    while (_opponent_climb[0] == dir || steps == 0) {
      opponent_step();
      ++steps;
      int now = below();
      int16_t sp = _opponent_speed[0];
      CHECK(dir > 0 ? now <= last : now >= last, "model: %s went the wrong way at step %d",
            what, steps);
      CHECK(dir > 0 ? sp >= kHigh.pair.speed - kOpponentClimbCost && sp <= kHigh.pair.speed
                    : sp <= kHigh.pair.speed + kOpponentClimbCost && sp >= kHigh.pair.speed,
            "model: %s at speed %d at step %d", what, sp, steps);
      last = now;
      if (steps > 2000) break;
    }
    // ~0.73 m a step at level 1 and 1,500: 300 m in ~410 steps, ~66 s.
    CHECK(abs(below()) <= kOpponentPathBand && steps > 350 && steps < 470,
          "model: %s ended %d half metres off after %d steps", what, below(), steps);
    for (int n = 0; n < 100; ++n) opponent_step();
    CHECK(_opponent_speed[0] == kHigh.pair.speed && _opponent_climb[0] == 0 &&
              abs(below()) <= kOpponentPathBand,
          "model: after the %s, speed %d, climb %d", what, _opponent_speed[0],
          _opponent_climb[0]);
    printf("  model: a 300 m %s in %d steps\n", what, steps);
  }

  // The pitch drawn: the climb's, once it has lasted two whole steps, and
  // held, not levelled, in between.
  fly(0, &kHigh);
  _opponent_z[0] -= 600 << 8;
  _opponent_pitch[0] = 0;
  opponent_step();
  CHECK(_opponent_climb[0] == 1 && _opponent_pitch[0] == 0, "pitch: drawn on the climb's first step");
  opponent_step();
  CHECK(_opponent_pitch[0] == 1, "pitch: not drawn on the climb's second step");
  _opponent_z[0] = (int32_t)kHigh.path.z << 8;
  opponent_step();
  CHECK(_opponent_climb[0] == 0 && _opponent_pitch[0] == 1, "pitch: levelled on the first step");
  opponent_step();
  CHECK(_opponent_pitch[0] == 0, "pitch: still up on the second level step");

  // The axes drawn, at every pitch, bank and a sweep of headings: read back
  // whole by putting the camera's rows on the world's axes, x and y, then z,
  // they are orthonormal to within rounding, the front's z is the pitch's
  // sine, and the left stays level unless banked.
  {
    const mat3_t saved = world_cam;
    double worst_len = 0, worst_dot = 0;
    // kOpponentDive, a falling aircraft's, among them, and the hunter's and
    // a spin's 60 degree bank.
    for (int pitch = -3; pitch <= kOpponentDive; ++pitch) {
      for (int bank = -2; bank <= 2; ++bank) {
        for (int deg = 0; deg < 360; deg += 15) {
          _opponent_head[0] = make_vector((int16_t)lround(256 * cos(deg * M_PI / 180)),
                                          (int16_t)lround(256 * sin(deg * M_PI / 180)), 0);
          _opponent_pitch[0] = (int8_t)pitch;
          _opponent_bank[0] = (int8_t)bank;
          mat3_t xy, zz;
          world_cam.left = make_vector(256, 0, 0);
          world_cam.up = make_vector(0, 256, 0);
          _opponent_axes(0, &xy);
          world_cam.left = make_vector(0, 0, 256);
          _opponent_axes(0, &zz);
          double ax[3][3] = {{(double)xy.front.y, (double)xy.front.z, (double)zz.front.y},
                             {(double)xy.left.y, (double)xy.left.z, (double)zz.left.y},
                             {(double)xy.up.y, (double)xy.up.z, (double)zz.up.y}};
          for (int a = 0; a < 3; ++a) {
            double len = sqrt(ax[a][0] * ax[a][0] + ax[a][1] * ax[a][1] + ax[a][2] * ax[a][2]);
            worst_len = std::max(worst_len, fabs(len - 256));
            for (int b2 = a + 1; b2 < 3; ++b2) {
              double d = (ax[a][0] * ax[b2][0] + ax[a][1] * ax[b2][1] + ax[a][2] * ax[b2][2]) / 256;
              worst_dot = std::max(worst_dot, fabs(d));
            }
          }
          CHECK(zz.front.y == kOpponentPitchSin[pitch + 3],
                "axes: pitch %d heading %d, the front's z is %d", pitch, deg, zz.front.y);
          CHECK(bank != 0 || zz.left.y == 0, "axes: pitch %d heading %d, unbanked left's z is %d",
                pitch, deg, zz.left.y);
        }
      }
    }
    CHECK(worst_len <= 5 && worst_dot <= 5, "axes: lengths %.1f off, products %.1f off",
          worst_len, worst_dot);
    printf("  axes: lengths within %.1f, products within %.1f, of 256\n", worst_len, worst_dot);
    world_cam = saved;
    _opponent_pitch[0] = _opponent_bank[0] = 0;
  }

  // Too slow to climb, too fast to descend: level until the speed is back
  // inside the range.
  for (int dir = 1; dir >= -1; dir -= 2) {
    fly(0, &kHigh);
    _opponent_z[0] -= (int32_t)(dir * 600) << 8;
    _opponent_speed[0] = dir > 0 ? kOpponentMinSpeed[0] : kOpponentMaxSpeed[0];
    int32_t z = _opponent_z[0];
    opponent_step();
    CHECK(_opponent_climb[0] == 0 && _opponent_z[0] == z,
          "model: %s at the end of the speed range", dir > 0 ? "climbed" : "descended");
    opponent_step();
    CHECK(_opponent_climb[0] == dir, "model: no %s once the speed was back in range",
          dir > 0 ? "climb" : "descent");
  }
}

// --- 7. The missions -------------------------------------------------------------
//
// Each flown as it starts, at shift 0, 6.25 steps a second: when and where
// things happen against the timings fmission.cc and opponent.cc give, the
// recon's escape and the bombers' arrival failing the mission once, the
// bombers keeping formation, the parked pair staying put, and the scramble's
// two flying on for ever. And every path
// within the reach of _opponent_toward(): no leg 64 units long on either axis.

static int _msgs = 0;
static const char *_msg = "";
void msg_show(const char *text, uint8_t duration, bool permanent) {
  (void)duration;
  (void)permanent;
  _msg = text;
  ++_msgs;
}

static double units(int32_t d) { return d / 65536.0; }
static double to_eye(int i, double *ahead);

// Row r's pair, sent as a wave that has it would send it.
static void send_row(uint8_t r) {
  uint8_t w = 0;
  while ((kOpponentWaves[w] & kOpponentRowMask) != r) ++w;
  _opponent_wave = w;
  _opponent_send();
}

static void test_missions(void) {
  flight_step_shift = 0;
  kFlightSubstepMask = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;

  // Every leg in reach, in every row.
  for (int m = 0; m < kOpponentRows; ++m) {
    for (int k = 0; k < kOpponentMax; ++k) {
      const opponent_path_t &p = kOpponentPaths[m][k];
      const opponent_pair_t &q = kOpponentPairs[m];
      if (p.kind == kOpponentNone) continue;
      CHECK(q.fail == kOpponentNoFail || q.fail < q.count, "row %d slot %d: fail at %d of %d",
            m, k, q.fail, q.count);
      CHECK(p.wp + q.count <= kOpponentWpCount, "row %d slot %d: waypoints %d-%d past the pool",
            m, k, p.wp, p.wp + q.count - 1);
      uint16_t px = p.start_x, py = p.start_y;
      for (int w = 0; w < q.count; ++w) {
        int dx = (int16_t)(wp_x(p, w) - px), dy = (int16_t)(wp_y(p, w) - py);
        // x repeats every 128 units, 0x8000 in 8.8.
        dx = ((dx + 0x4000) & 0x7FFF) - 0x4000;
        CHECK(abs(dx) < 0x4000 - 0x400 && abs(dy) < 0x4000 - 0x400,
              "row %d slot %d: leg %d is %.1f, %.1f units", m, k, w, dx / 256.0,
              dy / 256.0);
        px = wp_x(p, w);
        py = wp_y(p, w);
      }
    }
  }

  // Each row's roles and behaviours (docs/opponent_ai.md §3), which strays,
  // and the mode each slot starts in: ATTACK for a hunter, PATH for the rest.
  {
    // And the heading each starts on, as the unit vector it used to be given.
    static const uint8_t F = kOpponentFighter, B = kOpponentBomber, S = kOpponentStray;
    static const struct {
      uint8_t role, behaviour;
      int16_t hx, hy;
    } kSent[kOpponentRows] = {
        {B, kOpponentPath, -256, 0},     {B, kOpponentHunt, -256, 0},
        {F, kOpponentPath, 0, -256},     {F, kOpponentPass, 256, 0},
        {F, kOpponentHunt, 0, -256},     {F, kOpponentHunt, -256, 0},
        {F, kOpponentHunt, 256, 0},      {B, kOpponentPath, -256, 0},
        {F | S, kOpponentProvoked, 0, 256}, {F | S, kOpponentProvoked, -256, 0},
        {F, kOpponentPath, 256, 0},      {F | S, kOpponentHunt, -256, 0},
        {F, kOpponentHunt, 256, 0},
    };
    for (int m = 0; m < kOpponentRows; ++m) {
      send_row((uint8_t)m);
      for (int k = 0; k < kOpponentMax; ++k) {
        const opponent_path_t &p = kOpponentPaths[m][k];
        CHECK(_opponent_on[k] == (p.kind != kOpponentNone), "row %d slot %d: on %d", m, k,
              _opponent_on[k]);
        // 01's recon flies alone.
        if (p.kind == kOpponentNone) {
          CHECK(m == 0 && k == 1, "row %d slot %d: empty", m, k);
          continue;
        }
        uint8_t behaviour = p.kind & kOpponentBehaviourMask;
        CHECK((p.kind & ~(kOpponentKindMask | kOpponentBehaviourMask |
                          (3 << kOpponentHeadShift))) == kSent[m].role &&
                  behaviour == kSent[m].behaviour,
              "row %d slot %d: role %d, behaviour %d", m, k, p.kind & kOpponentBomber,
              behaviour >> 3);
        // A pair starts on one heading, but for 09's, from either side: slot
        // 1 on the opposite one.
        const int flip = m == 11 && k == 1 ? -1 : 1;
        CHECK(_opponent_head[k].x == flip * kSent[m].hx &&
                  _opponent_head[k].y == flip * kSent[m].hy &&
                  _opponent_head[k].z == 0,
              "row %d slot %d: starts heading %d, %d", m, k, _opponent_head[k].x,
              _opponent_head[k].y);
        CHECK(!!(_opponent_attack & _opponent_bit(k)) == (behaviour == kOpponentHunt),
              "row %d slot %d: starts in the wrong mode", m, k);
      }
    }
  }

  // 01: the recon.
  {
    _msgs = 0;
    opponent_reset(0);
    CHECK(_opponent_on[0] && !_opponent_on[1], "recon: one aircraft");
    CHECK(kMissionWpConstraint[kMissionWpBegin[0]] == WP_TARGETS_DOWN,
          "recon: the targets' navpoint is not 1");
    int city_at = -1, gone_at = -1, failed_at = -1;
    for (int n = 0; n < 4000 && gone_at < 0; ++n) {
      uint8_t w = _opponent_next[0];
      opponent_step();
      if (!_opponent_on[0]) {
        gone_at = n;
      } else if (w == 2 && _opponent_next[0] == 3) {
        city_at = n;
      }
      if (_opponent_failed && failed_at < 0) failed_at = n;
      if (_opponent_on[0]) {
        // Navpoint 1 on the recon, to the half metre.
        CHECK(flight_nav_point_x[0] == (uint16_t)(_opponent_x[0] >> 8) &&
                  flight_nav_point_y[0] == (uint16_t)(_opponent_y[0] >> 8),
              "recon: navpoint 1 at %04x %04x, the recon at %04x %04x", flight_nav_point_x[0],
              flight_nav_point_y[0], (unsigned)(_opponent_x[0] >> 8) & 0xFFFF,
              (unsigned)(_opponent_y[0] >> 8) & 0xFFFF);
        CHECK(_opponent_z[0] == (int32_t)kOpponent5000Ft << 8, "recon: changed height");
      }
    }
    // Head-on past the player's start, over the city at ~5.5 min, off the
    // south edge at ~6.3.
    CHECK(fabs(city_at / 6.25 / 60 - 5.5) < 0.4, "recon: over the city at %.1f min",
          city_at / 6.25 / 60);
    CHECK(gone_at > 0 && fabs(gone_at / 6.25 / 60 - 6.3) < 0.4, "recon: gone at %.1f min",
          gone_at / 6.25 / 60);
    CHECK(failed_at == gone_at && _msgs == 1 && !strcmp(_msg, kOpponentFailText[0]),
          "recon: failed at step %d, gone at %d, %d messages, \"%s\"", failed_at, gone_at, _msgs,
          _msg);
    CHECK(!opponent_targets_down(), "recon: escaped, yet the targets are down");
    // Gone, the navpoint stays where it was last: near the south edge.
    uint16_t last_x = flight_nav_point_x[0];
    for (int n = 0; n < 50; ++n) opponent_step();
    CHECK(flight_nav_point_x[0] == last_x && (last_x < 0x0400 || last_x > 0x7C00),
          "recon: navpoint 1 at %04x after it left", flight_nav_point_x[0]);
    printf("  recon: over the city at %.1f min, gone at %.1f min\n", city_at / 6.25 / 60,
           gone_at / 6.25 / 60);

    // Shot down before it got away - by hand, as nothing shoots yet.
    opponent_reset(0);
    CHECK(!opponent_targets_down(), "recon: down at the start");
    _opponent_down = 1;
    CHECK(opponent_targets_down(), "recon: shot down, but the targets are not down");
  }

  // 02: the bombers.
  {
    _msgs = 0;
    opponent_reset(1);
    CHECK(_opponent_on[0] && _opponent_on[1], "bombers: two aircraft");
    CHECK(kMissionWpConstraint[kMissionWpBegin[1]] == WP_TARGETS_DOWN,
          "bombers: the targets' navpoint is not 1");
    double close = 1e9, far = 0;
    int city_at = -1, failed_at = -1, gone = 0;
    for (int n = 0; n < 6000 && gone < 2; ++n) {
      uint8_t w = _opponent_next[0];
      opponent_step();
      if (w == 1 && _opponent_next[0] == 2) city_at = n;
      if (_opponent_failed && failed_at < 0) failed_at = n;
      gone = !_opponent_on[0] + !_opponent_on[1];
      if (_opponent_on[0] && _opponent_on[1]) {
        // Navpoint 1 midway between them, give or take the halving's half metre.
        int mx = (int)((_opponent_x[0] + _opponent_x[1]) / 2 >> 8);
        int my = (int)((_opponent_y[0] + _opponent_y[1]) / 2 >> 8);
        CHECK(abs((int16_t)(flight_nav_point_x[0] - mx)) <= 1 &&
                  abs((int16_t)(flight_nav_point_y[0] - my)) <= 1,
              "bombers: navpoint 1 at %04x %04x, midway is %04x %04x", flight_nav_point_x[0],
              flight_nav_point_y[0], mx & 0xFFFF, my & 0xFFFF);
      } else if (_opponent_on[1]) {
        CHECK(flight_nav_point_y[0] == (uint16_t)(_opponent_y[1] >> 8),
              "bombers: navpoint 1 not on the one left");
      }
      if (city_at < 0 && _opponent_on[0] && _opponent_on[1]) {
        double dx = (_opponent_x[1] - _opponent_x[0]) / 512.0;
        double dy = (_opponent_y[1] - _opponent_y[0]) / 512.0;
        double d = sqrt(dx * dx + dy * dy);
        close = std::min(close, d);
        far = std::max(far, d);
      }
    }
    // 75 m and 100 m apart: 125 m, and both turn alike.
    CHECK(close > 100 && far < 150, "bombers: %.0f-%.0f m apart on the way in", close, far);
    CHECK(fabs(city_at / 6.25 / 60 - 4.4) < 0.4, "bombers: over the city at %.1f min",
          city_at / 6.25 / 60);
    CHECK(failed_at == city_at && _msgs == 1 && !strcmp(_msg, kOpponentFailText[1]),
          "bombers: failed at step %d, over the city at %d, %d messages", failed_at, city_at,
          _msgs);
    CHECK(gone == 2, "bombers: never left");
    _opponent_down = 3;
    CHECK(!opponent_targets_down(), "bombers: the city was bombed, yet the targets are down");
    opponent_reset(1);
    _opponent_down = 1;
    CHECK(!opponent_targets_down(), "bombers: one down is not both");
    _opponent_down = 3;
    CHECK(opponent_targets_down(), "bombers: both down, but the targets are not down");
    printf("  bombers: %.0f-%.0f m apart, over the city at %.1f min\n", close, far,
           city_at / 6.25 / 60);
  }

  // 03: the ambush.
  {
    _msgs = 0;
    opponent_reset(2);
    int32_t x0 = _opponent_x[0], y1 = _opponent_y[1];
    CHECK(_opponent_y[0] == _opponent_y[1] && _opponent_head[0].y == -256 &&
              _opponent_head[1].y == -256,
          "ambush: not side by side, facing the runway");
    for (int n = 0; n < 3000; ++n) opponent_step();
    CHECK(_opponent_on[0] && _opponent_on[1] && _opponent_x[0] == x0 && _opponent_y[1] == y1 &&
              _opponent_turn[0] == 0 && !_opponent_failed && _msgs == 0,
          "ambush: the parked aircraft moved");
    CHECK(units(_opponent_x[0]) > 0x5C && units(_opponent_x[0]) < 0x64 &&
              units(_opponent_y[0]) > 0xC0 && units(_opponent_y[1]) < 0xC0 + 1.0,
          "ambush: not beside runway 2");
    CHECK(!opponent_targets_down(), "ambush: down at the start");
    // Midway between the two, beside runway 2.
    CHECK(flight_nav_point_x[0] == 0x5D00 && flight_nav_point_y[0] == 0xC096,
          "ambush: navpoint 1 at %04x %04x", flight_nav_point_x[0], flight_nav_point_y[0]);
    // One shot down: on the other.
    _opponent_down = 1;
    opponent_step();
    CHECK(flight_nav_point_x[0] == 0x5D64, "ambush: navpoint 1 at %04x with one down",
          flight_nav_point_x[0]);
  }

  // 04: the scramble. A pass and then a hunt: both fly north on their lines
  // either side of runway 1's, low and level, the second 184 m behind the
  // first, past the player to their waypoints ~600 m beyond it, ~16 s out;
  // there both turn to ATTACK, within a step of each other, and come back
  // for the player still parked on the runway inside a minute, never
  // failing the mission or leaving; firing only on the way back, at the
  // player ahead, not as the pass ends with it behind.
  {
    _msgs = 0;
    opponent_reset(3);
    CHECK(_opponent_on[0] && _opponent_on[1] &&
              kMissionWpConstraint[kMissionWpBegin[3]] == WP_TARGETS_DOWN,
          "scramble: not two aircraft, or the targets' navpoint is not 1");
    model_on_ground = true;
    flight_eye_x = 0x1C00L << 8;
    flight_eye_y = 0x3F80L << 8;
    flight_eye_z = 32 << 8;
    int at[2] = {-1, -1}, back = -1, fired = 0;
    for (int n = 0; n < 1500 && back < 0; ++n) {
      opponent_step();
      for (int i = 0; i < 2; ++i) {
        if (at[i] < 0 && (_opponent_attack & _opponent_bit((uint8_t)i))) at[i] = n;
        if (at[i] < 0)
          CHECK(_opponent_z[i] == (int32_t)0x0060 << 8 &&
                    _opponent_y[i] == (i ? 0x3F20 : 0x3FE0) << 8,
                "scramble: aircraft %d left its line at %.1f, height %.1f", i + 1,
                units(_opponent_y[i]), units(_opponent_z[i]));
      }
      if (at[0] < 0)
        CHECK(((_opponent_x[0] - _opponent_x[1]) >> 8 & 0x7FFF) == 0x170,
              "scramble: the second is %.1f units behind", units(_opponent_x[0] - _opponent_x[1]));
      if (at[1] >= 0 && to_eye(0, nullptr) < 600 && to_eye(1, nullptr) < 600) back = n;
      // Every burst at the player ahead of the nose: none as the pass ends,
      // with the player ~600 m behind.
      for (int i = 0; i < 2; ++i) {
        if (!(_opponent_firing & _opponent_bit((uint8_t)i))) continue;
        ++fired;
        double dx;
        to_eye(i, &dx);
        double dy = (double)(flight_eye_y - _opponent_y[i]) / 256.0;
        double ahead = (dx * _opponent_head[i].x + dy * _opponent_head[i].y) / 512.0;
        CHECK(ahead > 0, "scramble: aircraft %d fires at %.1f s, the player %.0f m behind",
              i + 1, n / 6.25, -ahead);
      }
      _opponent_firing = 0;
    }
    CHECK(fabs(at[0] / 6.25 - 16) < 4 && abs(at[0] - at[1]) <= 1,
          "scramble: ATTACK at %.1f s and %.1f s", at[0] / 6.25, at[1] / 6.25);
    // Round from ~600 m past it at 2,000: ~36 s.
    CHECK(back >= 0 && back - at[1] < 60 * 6.25, "scramble: back within 300 m %.0f s after",
          (back - at[1]) / 6.25);
    CHECK(_opponent_on[0] && _opponent_on[1] && !_opponent_failed && _msgs == 0,
          "scramble: an aircraft left, or the mission failed");
    CHECK(fired > 0, "scramble: no strafing run fired");
    printf("  scramble: ATTACK at %.0f s, both back within 300 m of the player %.0f s later,"
           " %d rounds, all at it ahead\n",
           at[0] / 6.25, (back - at[1]) / 6.25, fired);
    model_on_ground = false;
    _opponent_down = 3;
    // 04 has a second pair still to come.
    CHECK(_opponent_none() && !_opponent_last && !opponent_targets_down(),
          "scramble: both down, and the second pair not to come");
  }

  // The guns' side (gun.cc): three strikes shoot a slot down, and it falls
  // (_opponent_fall()), still drawn; at the ground it is down, neither a
  // target nor drawn, and both down is the goal.
  {
    _msgs = 0;
    opponent_reset(3);
    int32_t p[3];
    CHECK(opponent_target(0, p) && p[0] == _opponent_x[0] && p[2] == _opponent_z[0],
          "strikes: slot 0 is not a target at its own position");
    // 100 m behind it, looking at it: in view.
    world_cam.front = make_vector(256, 0, 0);
    world_cam.left = make_vector(0, 256, 0);
    world_cam.up = make_vector(0, 0, 256);
    flight_eye_x = _opponent_x[0] - (100L << 9);
    flight_eye_y = _opponent_y[0];
    flight_eye_z = _opponent_z[0];
    for (int k = 1; k < kOpponentHitsToDown; ++k) opponent_strike(0);
    CHECK(opponent_target(0, p) && _msgs == 0 && (_opponent_cull() & 1),
          "strikes: down, or out of view, before the last hit");
    opponent_strike(0);
    CHECK((_opponent_down & 1 << kOpponentFalling) && _msgs == 1 && !strcmp(_msg, "OPPONENT SHOT DOWN"),
          "strikes: the last hit did not shoot it down (%d messages)", _msgs);
    CHECK(!opponent_targets_down(), "strikes: one of two down, and the targets are down");
    CHECK(_opponent_cull() & 1, "strikes: a falling aircraft is not drawn");
    // It falls, slot 0 diving and slot 1 spinning as well: nose down at
    // kOpponentDive, lower every step, its smoke trailing it, no more
    // hits counted - and down, neither drawn nor a target, at the ground.
    for (int k = 0; k < kOpponentHitsToDown; ++k) opponent_strike(1);
    _opponent_down = (uint8_t)((_opponent_down & ~(3 << kOpponentSpin)) | 2 << kOpponentSpin);
    int16_t hx[2] = {_opponent_head[0].x, _opponent_head[1].x};
    bool turned[2] = {false, false};
    int steps = 0;
    while ((_opponent_down & 3) != 3 && steps < 100) {
      int32_t z[2] = {_opponent_z[0], _opponent_z[1]};
      opponent_strike(0);
      opponent_step();
      ++steps;
      for (int i = 0; i < 2; ++i) {
        if (_opponent_down & (1 << i)) continue;
        CHECK(_opponent_z[i] < z[i] && _opponent_pitch[i] == kOpponentDive &&
                  _opponent_hits[i] == kOpponentHitsToDown,
              "falls: slot %d at step %d: %.1f m from %.1f, pitch %d, %d hits", i, steps,
              _opponent_z[i] / 512.0, z[i] / 512.0, _opponent_pitch[i], _opponent_hits[i]);
        turned[i] |= _opponent_head[i].x != hx[i];
      }
      int16_t sh[2];
      CHECK((_opponent_down & 3) == 3 || opponent_smoke(p, sh), "falls: no smoke");
    }
    CHECK((_opponent_down & 3) == 3 && !(_opponent_down & 3 << kOpponentFalling) && !turned[0] &&
              turned[1],
          "falls: down %d after %d steps, turned %d %d", _opponent_down, steps, turned[0],
          turned[1]);
    CHECK(!opponent_target(0, p) && !(_opponent_cull() & 3), "falls: down, but drawn or a target");
    CHECK(_opponent_none() && !opponent_targets_down() && !opponent_none_left(),
          "strikes: both down, and 04's second pair not to come");
    _opponent_last = true;
    CHECK(opponent_targets_down() && opponent_none_left(),
          "strikes: both of the last pair down, but the targets are not down or some left");
    opponent_reset(3);
    CHECK(opponent_target(0, p) && !opponent_targets_down() && !opponent_none_left(),
          "strikes: a reset left them down");
  }

  // The smoke: none until a hit, then behind the last slot hit for kSmokeHit
  // frames, each hit starting them again; a slot shot down trails it as it
  // falls, and stops where it hits the ground, its smoke hanging there
  // kSmokeDown frames.
  {
    opponent_reset(3);
    int32_t p[3];
    int16_t h[2];
    CHECK(!opponent_smoke(p, h), "smoke: up before any hit");
    opponent_strike(1);
    CHECK(opponent_smoke(p, h) && p[0] == _opponent_x[1] && h[0] == 256 && h[1] == 0,
          "smoke: not on the slot hit, heading north");
    opponent_strike(0);
    CHECK(opponent_smoke(p, h) && p[0] == _opponent_x[0], "smoke: not on the last slot hit");
    for (int n = 0; n < kSmokeHit - 1; ++n) opponent_step();
    CHECK(opponent_smoke(p, h), "smoke: gone before %d frames after a hit", kSmokeHit);
    opponent_step();
    CHECK(!opponent_smoke(p, h), "smoke: still up %d frames after a hit", kSmokeHit);
    for (int k = 1; k < kOpponentHitsToDown; ++k) opponent_strike(0);
    // It falls to the ground, its smoke on it all the way; an extra strike
    // on it is ignored and keeps that smoke.
    for (int n = 0; n < 100 && !(_opponent_down & 1); ++n) {
      opponent_strike(0);
      opponent_step();
      CHECK(opponent_smoke(p, h) && p[0] == _opponent_x[0], "smoke: not on the falling slot");
    }
    for (int n = 0; n < kSmokeDown - 1; ++n) opponent_step();
    CHECK(opponent_smoke(p, h) && p[0] == _opponent_x[0], "smoke: gone before it lingered");
    int32_t fell = _opponent_x[0];
    opponent_step();
    CHECK(!opponent_smoke(p, h), "smoke: still up %d frames after the slot went down",
          kSmokeDown);
    CHECK(_opponent_x[0] == fell, "smoke: a downed slot moved on");

    // Into the ground: down as if shot down, with its message and the
    // longer smoke where it fell, and counted towards the goal. A path
    // holds its height, so it is put there by hand, 8 m under, deeper than
    // the step's climb back towards the path's height; ATTACK's floors
    // (step 6) keep a hunter off it too.
    opponent_reset(3);
    _msgs = 0;
    _opponent_z[1] = 1 << 8;
    opponent_step();
    CHECK(!(_opponent_down & 2), "ground: down half a metre up");
    _opponent_z[1] = -(16 << 8);
    opponent_step();
    CHECK((_opponent_down & 2) && _msgs == 1 && !strcmp(_msg, "ENEMY CRASHED") &&
              opponent_smoke(p, h) && p[0] == _opponent_x[1] && _opponent_smoke_t == kSmokeDown,
          "ground: not down on reaching it (%d messages, \"%s\")", _msgs, _msg);
    CHECK(!opponent_target(1, p) && !(_opponent_cull() & 2), "ground: still a target, or drawn");
    for (int k = 0; k < kOpponentHitsToDown; ++k) opponent_strike(0);
    for (int n = 0; n < 100 && !(_opponent_down & 1); ++n) opponent_step();
    _opponent_last = true;
    CHECK(opponent_targets_down(), "ground: one crashed, one shot down, but the targets are up");

    // A damaged one-way aircraft that escapes leaves no smoke behind it.
    opponent_reset(0);
    while (_opponent_next[0] < 3) opponent_step();
    opponent_strike(0);
    CHECK(opponent_smoke(p, h), "smoke: not up after hitting the recon");
    while (_opponent_on[0]) opponent_step();
    CHECK(!opponent_smoke(p, h), "smoke: still up after the recon escaped");
  }

  // The colour: near, a bomber brown and a fighter dark grey; far, both
  // black.
  {
    static const struct {
      int mission;
      double back;
      uint8_t color;
    } kColors[] = {{1, 150, kColorBrown}, {3, 150, kColorOpponentNear}, {1, 1500, kColorOpponentFar}};
    for (const auto &k : kColors) {
      opponent_reset((uint8_t)k.mission);
      _opponent_on[1] = false;
      world_cam.front = make_vector(_opponent_head[0].x, _opponent_head[0].y, 0);
      world_cam.left = make_vector(-_opponent_head[0].y, _opponent_head[0].x, 0);
      world_cam.up = make_vector(0, 0, 256);
      flight_eye_x = _opponent_x[0] - (int32_t)(k.back * 512) * _opponent_head[0].x / 256;
      flight_eye_y = _opponent_y[0];
      flight_eye_z = _opponent_z[0];
      _offer[0].on = false;
      for (int n = 0; n < 4; ++n) {
        _offer[0].on = false;
        opponent_add_sprites();
      }
      CHECK(_offer[0].on && _offer[0].color == k.color,
            "colour: mission %d at %.0f m: offered %d, colour %d, not %d", k.mission + 1, k.back,
            _offer[0].on, _offer[0].color, k.color);
    }
  }

  // The budget with smoke and a tracer: in front, four and two and one of
  // them; with both, or behind with either, both aircraft held to two.
  {
    opponent_reset(3);
    _opponent_cs[0] = make_vector(800, 0, 0);
    _opponent_cs[1] = make_vector(1600, 0, 0);
    _opponent_far = 0;
    static const struct {
      view_state_t view;
      bool smoke, tracer;
      uint8_t capped;
    } kCases[] = {
        {VIEW_CENTER, false, false, 2}, {VIEW_CENTER, true, false, 2},
        {VIEW_CENTER, false, true, 2},  {VIEW_CENTER, true, true, 3},
        {VIEW_BACK, false, false, 2},   {VIEW_BACK, false, true, 3},
        {VIEW_BACK, true, false, 3},
    };
    for (const auto &k : kCases) {
      view_state = k.view;
      _opponent_smoke = k.smoke ? 1 : kOpponentNoSmoke;
      _tracer_up = k.tracer;
      _opponent_nearer = 0;
      _opponent_cap();
      CHECK(_opponent_capped == k.capped, "budget: view %d smoke %d tracer %d holds %d, not %d",
            k.view, k.smoke, k.tracer, _opponent_capped, k.capped);
    }
    view_state = VIEW_CENTER;
    _tracer_up = false;
    opponent_reset(3);
  }
}

// --- 8. Hunting ----------------------------------------------------------------------
//
// docs/opponent_ai.md §5: a fighter in ATTACK steers for the eye, at the
// climb level that points it there, and keeps above kOpponentFloor. The eye
// is moved here by hand, straight and level at the player's half-throttle
// trim, 1,476.

static const test_path_t kHunter = {{0, kOpponentNoFail, 2000},
    {kOpponentLoop | kOpponentFighter | kOpponentHunt, 0, 0x2000, 0x8080, 0x0600}};
// kSquare's lap, once, then the player.
static const test_path_t kSquarePass = {{4, kOpponentNoFail, 1500},
    {kOpponentLoop | kOpponentFighter | kOpponentPass, kOpponentWpCount, 0x0A00, 0x6200,
     0x0700}};

static void hunt(int slot, const test_path_t *p) {
  fly(slot, p);
  _opponent_attack |= _opponent_bit((uint8_t)slot);
}

// Half metres from slot i to the eye, and how far ahead of it the eye is
// along x, north; the short way round x's 128 units.
static double to_eye(int i, double *ahead) {
  double dx = (double)(flight_eye_x - _opponent_x[i]) / 256.0;
  double dy = (double)(flight_eye_y - _opponent_y[i]) / 256.0;
  double dz = (double)(flight_eye_z - _opponent_z[i]) / 256.0;
  dx = fmod(dx + 16384.0 + 4 * 32768.0, 32768.0) - 16384.0;
  if (ahead) *ahead = dx;
  return sqrt(dx * dx + dy * dy + dz * dz);
}

static void test_hunting(void) {
  flight_step_shift = 0;
  kFlightSubstepMask = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  _msgs = 0;

  // The chase: from 2 km behind and 300 m below the player, then from 300 m
  // above; caught, within 300 m behind it and within 50 m of its height,
  // inside 3.5 min.
  for (int dir = -1; dir <= 1; dir += 2) {
    opponent_reset(0);
    _opponent_on[0] = false;
    hunt(1, &kHunter);
    flight_eye_x = _opponent_x[1] + (4000 << 8);
    flight_eye_y = _opponent_y[1];
    flight_eye_z = _opponent_z[1] - dir * (600 << 8);
    int caught = -1, steep = 0;
    for (int n = 0; n < (int)(210 * 6.25) && caught < 0; ++n) {
      flight_eye_x += 1476 * 2;
      opponent_step();
      if (abs(_opponent_climb[1]) >= 2) ++steep;
      double ahead, d = to_eye(1, &ahead);
      double dz = (double)(flight_eye_z - _opponent_z[1]) / 512.0;
      if (d < 600 && ahead > 0 && fabs(dz) < 50) caught = n;
    }
    CHECK(caught >= 0, "hunting: never caught a player %s it", dir < 0 ? "above" : "below");
    printf("  hunting: caught a player 300 m %s in %.0f s, %d steps steeper than level 1\n",
           dir < 0 ? "above" : "below", caught / 6.25, steep);
  }

  // Following the player's height: the hunter, already behind it, climbs
  // and descends after it as it changes height by 200 m, up then down. A
  // hunter slower than the player, so that it stays behind: a faster one
  // overtakes, breaking off over it (below), and turns back.
  {
    static const test_path_t kSlow = {{0, kOpponentNoFail, 1400},
        {kOpponentLoop | kOpponentFighter | kOpponentHunt, 0, 0x2000, 0x8080, 0x0600}};
    opponent_reset(0);
    _opponent_on[0] = false;
    hunt(1, &kSlow);
    flight_eye_x = _opponent_x[1] + (1000 << 8);
    flight_eye_y = _opponent_y[1];
    flight_eye_z = _opponent_z[1];
    int up = 0, down = 0;
    for (int n = 0; n < 1200; ++n) {
      flight_eye_x += 1476 * 2;
      if (n >= 200 && n < 600) flight_eye_z += 256;
      if (n >= 700 && n < 1100) flight_eye_z -= 256;
      opponent_step();
      if (_opponent_climb[1] > 0) ++up;
      if (_opponent_climb[1] < 0) ++down;
    }
    double dz = (double)(flight_eye_z - _opponent_z[1]) / 512.0;
    CHECK(up > 50 && down > 50 && fabs(dz) < 50,
          "hunting: %d steps up, %d down, %.0f m off the player's height", up, down, dz);
  }

  // The floor: a player 20 m up, and the hunter never under kOpponentFloor
  // over 5 min of passes.
  {
    opponent_reset(0);
    _opponent_on[0] = false;
    hunt(1, &kHunter);
    flight_eye_x = _opponent_x[1] + (2000 << 8);
    flight_eye_y = _opponent_y[1];
    flight_eye_z = 40 << 8;
    int32_t lowest = _opponent_z[1];
    for (int n = 0; n < 1875; ++n) {
      flight_eye_x += 1476 * 2;
      opponent_step();
      lowest = std::min(lowest, _opponent_z[1]);
    }
    CHECK(lowest >= (int32_t)kOpponentFloor << 8 && !(_opponent_down & 2),
          "hunting: down to %.0f m over a low player", lowest / 512.0);
    printf("  hunting: over a player 20 m up, never under %.0f m\n", lowest / 512.0);
  }

  // A pass: kSquare's lap, the player ignored, then the player. ATTACK
  // starts at the last waypoint, and from there the range closes.
  {
    opponent_reset(0);
    _opponent_on[0] = false;
    fly(1, &kSquarePass);
    flight_eye_x = 0x1000 << 8;
    flight_eye_y = 0x6800 << 8;
    flight_eye_z = (int32_t)kSquarePass.path.z << 8;
    int reached = 0, at = -1;
    uint8_t next = _opponent_next[1];
    for (int n = 0; n < 3000 && at < 0; ++n) {
      opponent_step();
      if (_opponent_next[1] != next) {
        ++reached;
        next = _opponent_next[1];
      }
      if (_opponent_attack & 2) at = n;
    }
    CHECK(at >= 0 && reached == 4, "pass: ATTACK at step %d, after %d waypoints", at, reached);
    double before = to_eye(1, nullptr), nearest = before;
    for (int n = 0; n < 300; ++n) {
      opponent_step();
      nearest = std::min(nearest, to_eye(1, nullptr));
    }
    CHECK(nearest < 200, "pass: from %.0f m, no nearer than %.0f m", before / 2, nearest / 2);
  }
  // Strafing: a player parked on the ground, its eye 16 m up, and a hunter
  // coming in from 2 km at 200 m. Runs, again and again, each down under
  // kOpponentFloor but never under kOpponentStrafeFloor, each pull-up back
  // over kOpponentFloor before the next run, and never into the ground. Then
  // the player takes off, and the hunter keeps to kOpponentFloor.
  {
    opponent_reset(0);
    _opponent_on[0] = false;
    hunt(1, &kHunter);
    model_on_ground = true;
    flight_eye_x = _opponent_x[1] + (4000 << 8);
    flight_eye_y = _opponent_y[1];
    flight_eye_z = 32 << 8;
    _opponent_z[1] = 400 << 8;
    int runs = 0, nearest_run = 1 << 30;
    bool low = false;
    int32_t lowest = _opponent_z[1];
    const int32_t floor = (int32_t)kOpponentFloor << 8;
    for (int n = 0; n < 3750; ++n) {
      opponent_step();
      int32_t z = _opponent_z[1];
      lowest = std::min(lowest, z);
      if (!low && z < floor) {
        low = true;
        ++runs;
      } else if (low && z >= floor) {
        low = false;
      }
      if (low) nearest_run = std::min(nearest_run, (int)to_eye(1, nullptr));
    }
    CHECK(runs >= 4 && lowest >= (int32_t)kOpponentStrafeFloor << 8 && !(_opponent_down & 2),
          "strafing: %d runs in 10 min, down to %.0f m", runs, lowest / 512.0);
    printf("  strafing: %d runs in 10 min, down to %.0f m, the nearest %.0f m from the player\n",
           runs, lowest / 512.0, nearest_run / 2.0);
    // Off the ground: back to the permanent floor.
    model_on_ground = false;
    for (int n = 0; n < 300; ++n) opponent_step();
    lowest = _opponent_z[1];
    for (int n = 0; n < 1500; ++n) {
      opponent_step();
      lowest = std::min(lowest, _opponent_z[1]);
    }
    CHECK(lowest >= floor, "strafing: down to %.0f m with the player off the ground",
          lowest / 512.0);
  }
  // Breaking off: head-on at the player flying straight at it, at its height
  // or 8 m above or below, the hunter never comes within gun.cc's 12 m
  // collision box on all three axes at once.
  flight_cam.front = make_vector(256, 0, 0);
  for (int up = -16; up <= 16; up += 16) {
    opponent_reset(0);
    _opponent_on[0] = false;
    hunt(1, &kHunter);
    _opponent_head[1] = make_vector(-256, 0, 0);
    flight_eye_x = _opponent_x[1] - (1500 << 9);
    flight_eye_y = _opponent_y[1];
    flight_eye_z = _opponent_z[1] + (up << 8);
    double closest = 1e9;
    for (int n = 0; n < 400; ++n) {
      flight_eye_x += 1476 * 2;
      opponent_step();
      double dx = (double)(flight_eye_x - _opponent_x[1]) / 512.0;
      dx = fmod(dx + 8192.0 + 4 * 16384.0, 16384.0) - 8192.0;
      double dy = (double)(flight_eye_y - _opponent_y[1]) / 512.0;
      double dz = (double)(flight_eye_z - _opponent_z[1]) / 512.0;
      closest = std::min(closest, std::max(fabs(dx), std::max(fabs(dy), fabs(dz))));
    }
    CHECK(closest > 12, "breaking off: %d m above, within %.1f m on every axis", up / 2, closest);
    printf("  breaking off, the player %d m above: never within %.0f m on every axis\n", up / 2,
           closest);
  }
  CHECK(_msgs == 0, "hunting: %d messages, the last \"%s\"", _msgs, _msg);
}

// --- 8b. Fired upon -----------------------------------------------------------------
//
// A Provoked slot keeps to its path, calm, until it is fired upon - a hit,
// or gun.cc's near miss - and then hunts. A Path slot is never provoked.

static void test_provoked(void) {
  flight_step_shift = 0;
  kFlightSubstepMask = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  model_on_ground = false;
  static const test_path_t kCalm = {{4, kOpponentNoFail, 1500},
      {kOpponentLoop | kOpponentFighter | kOpponentProvoked, kOpponentWpCount, 0x0A00, 0x6200,
       0x0700}};
  // Fired upon (0), a near miss (1), a Path slot (2), and the player seen
  // within kOpponentSpot (3). Until then the player is out of sight, 4 km
  // north of the square.
  for (int how = 0; how < 4; ++how) {
    opponent_reset(0);
    _opponent_on[0] = false;
    fly(1, how == 2 ? &kSquare : &kCalm);
    flight_eye_x = (int32_t)0x4000 << 8;
    flight_eye_y = (int32_t)0x6800 << 8;
    flight_eye_z = (int32_t)kCalm.path.z << 8;
    CHECK(opponent_calm(1) == (how != 2), "provoked %d: calm %d at the start", how,
          opponent_calm(1));
    for (int n = 0; n < 200; ++n) opponent_step();
    CHECK(!(_opponent_attack & 2), "provoked %d: attacked unprovoked", how);
    flight_eye_x = (int32_t)0x1000 << 8;
    if (how == 0) {
      opponent_strike(1);
    } else if (how == 3) {
      opponent_step();
    } else {
      opponent_provoke(1);
    }
    bool attacking = (_opponent_attack & 2) != 0;
    CHECK(attacking == (how != 2) && !opponent_calm(1),
          "provoked %d: attacking %d, calm %d after it was fired upon", how, attacking,
          opponent_calm(1));
    // Then, provoked, it closes on the player.
    double before = to_eye(1, nullptr), nearest = before;
    for (int n = 0; n < 600; ++n) {
      opponent_step();
      nearest = std::min(nearest, to_eye(1, nullptr));
    }
    CHECK(how == 2 || nearest < 200, "provoked %d: from %.0f m, no nearer than %.0f m", how,
          before / 2, nearest / 2);
  }
}

// --- 9. The fighters' guns ---------------------------------------------------------
//
// docs/opponent_ai.md §6: a hunter with the player within ~7 degrees of its
// nose and ~500 m fires three rounds, a whole step apart and one every
// second, along its nose, and then none until kOpponentGunCycle whole steps
// after the first. Off the nose, too far, or on a path, it does not fire.

// What each slot fires, counted, and the last round's velocity.
static int _fired[2];
static int16_t _fired_v[3];

// A hunter at kHunter's height, `deg` degrees off the bearing to a
// stationary eye `m` metres north of it.
static void aim_at(double m, double deg) {
  opponent_reset(0);
  _opponent_on[0] = false;
  hunt(1, &kHunter);
  flight_eye_x = _opponent_x[1] + (int32_t)(m * 512);
  flight_eye_y = _opponent_y[1];
  flight_eye_z = _opponent_z[1];
  _opponent_head[1] = make_vector((int16_t)lround(256 * cos(deg * M_PI / 180)),
                                  (int16_t)lround(256 * sin(deg * M_PI / 180)), 0);
  _fired[0] = _fired[1] = 0;
}

// One model step, then what gun_step() would ask for.
static void fire_step(void) {
  opponent_step();
  for (uint8_t i = 0; i < 2; ++i) {
    if (opponent_fires(i, _fired_v)) ++_fired[i];
  }
  int16_t v[3];
  CHECK(!opponent_fires(0, v) && !opponent_fires(1, v), "guns: a slot fired twice in a step");
}

static void test_guns(void) {
  flight_step_shift = 0;
  kFlightSubstepMask = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  model_on_ground = false;

  // Nose on at 400 m: a burst of three, rounds along the nose, then quiet
  // until the cycle comes round, and another.
  aim_at(400, 0);
  int at[8], n_at = 0;
  for (int n = 0; n < kOpponentGunCycle + 2; ++n) {
    int before = _fired[1];
    fire_step();
    if (_fired[1] != before && n_at < 8) at[n_at++] = n;
  }
  CHECK(n_at == 4 && at[0] == 0 && at[1] == 2 && at[2] == 4 && at[3] == kOpponentGunCycle,
        "guns: fired %d rounds, at steps %d %d %d %d", n_at, at[0], at[1], at[2], at[3]);
  CHECK(abs(_fired_v[0] - 768) <= 3 * 28 && abs(_fired_v[1]) <= 3 * 28 && abs(_fired_v[2]) <= 3 * 12,
        "guns: fired at %d %d %d, not 768 along the nose, scattered", _fired_v[0], _fired_v[1],
        _fired_v[2]);

  // 5 degrees off: the first step fires; 10 degrees off, it does not - it
  // turns onto the player first.
  aim_at(400, 5);
  fire_step();
  CHECK(_fired[1] == 1, "guns: did not fire 5 degrees off the nose");
  aim_at(400, 10);
  fire_step();
  CHECK(_fired[1] == 0, "guns: fired 10 degrees off the nose");
  // 700 m off: out of range.
  aim_at(700, 0);
  fire_step();
  CHECK(_fired[1] == 0, "guns: fired at 700 m");
  // 300 m above at 400 m: off the steepest level's slope, so no.
  aim_at(400, 0);
  flight_eye_z += 600 << 8;
  fire_step();
  CHECK(_fired[1] == 0, "guns: fired at a player 37 degrees above the nose");
  // On a path: never, the player dead ahead or not.
  aim_at(400, 0);
  _opponent_attack = 0;
  for (int n = 0; n < 50; ++n) fire_step();
  CHECK(_fired[1] == 0, "guns: fired on a path");
  printf("  guns: a burst at steps %d, %d and %d, the next at %d\n", at[0], at[1], at[2], at[3]);

  // The rear gunner: 02's first bomber, heading south, and 01's recon, a
  // bomber on a path - every bomber's gunner fires, whatever its
  // behaviour. At a player behind it within 45 degrees to either side, from
  // 45 degrees above its wings to ~10 below and inside 600 m, on the gun's
  // cycle, along the line to the player, scattered by at most
  // kOpponentScatter; never at one ahead, wider, higher, lower or farther.
  static const struct {
    int mission;
    double back, side, up;
    bool fires;
  } kTail[] = {{1, 300, 0, 0, true},     {0, 300, 0, 0, true},     {1, -300, 0, 0, false},
               {1, 300, 175, 0, true},   {1, 300, -250, 0, true},  {1, 300, 330, 0, false},
               {1, 300, 0, 100, true},   {1, 300, 0, 250, true},   {1, 300, 0, 330, false},
               {1, 300, 0, -1, true},    {1, 300, 0, -40, true},   {1, 300, 0, -75, false},
               {1, 150, 20, 20, true},   {1, 150, 20, -20, true},  {1, 550, 0, 0, true},
               {1, 700, 0, 0, false},    {1, 100, 90, 90, true}};
  for (const auto &k : kTail) {
    opponent_reset((uint8_t)k.mission);
    _opponent_on[1] = false;
    _fired[0] = 0;
    int16_t worst = 0;
    for (int n = 0; n < kOpponentGunCycle; ++n) {
      // Behind is north, the bomber heading south; left of its tail is west, +y.
      flight_eye_x = _opponent_x[0] + (int32_t)(k.back * 512);
      flight_eye_y = _opponent_y[0] + (int32_t)(k.side * 512);
      flight_eye_z = _opponent_z[0] + (int32_t)(k.up * 512);
      int before = _fired[0];
      fire_step();
      if (_fired[0] != before) {
        // The line to the player from where the bomber fired, after its
        // step, 768 long, and how far the round's velocity is off it beyond
        // the scatter: x and y by up to 3 x 28, z by 3 x 12, and
        // vec_normalize()'s square root, good to ~2%: the round's speed, not
        // its line.
        double d[3] = {(double)(flight_eye_x - _opponent_x[0]),
                       (double)(flight_eye_y - _opponent_y[0]),
                       (double)(flight_eye_z - _opponent_z[0])};
        double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        double e[3] = {768 * d[0] / l, 768 * d[1] / l, 768 * d[2] / l};
        int16_t off[3] = {(int16_t)(abs(_fired_v[0] - lround(e[0])) - 3 * 28),
                          (int16_t)(abs(_fired_v[1] - lround(e[1])) - 3 * 28),
                          (int16_t)(abs(_fired_v[2] - lround(e[2])) - 3 * 12)};
        for (int16_t o : off) worst = std::max(worst, o);
      }
    }
    CHECK(k.fires ? _fired[0] == 3 && worst <= 18 : _fired[0] == 0,
          "gunner: mission %d, %.0f m back, %.0f across, %.0f up: %d rounds, %d off the line",
          k.mission + 1, k.back, k.side, k.up, _fired[0], worst);
  }

  // Parked, a bomber never fires: on the ground it is not flying, whatever
  // sits behind it. 03's pair are fighters, so one of them as a bomber.
  static const test_path_t kParkedBomber = {{0, kOpponentNoFail, 0},
      {kOpponentParked | kOpponentBomber | kOpponentHunt, 0, 0x5C9C, 0xC096, 0x000A}};
  opponent_reset(2);
  _opponent_on[1] = false;
  fly(0, &kParkedBomber);
  _opponent_attack = 1;
  _fired[0] = 0;
  for (int n = 0; n < 3 * kOpponentGunCycle; ++n) {
    flight_eye_x = _opponent_x[0] - (int32_t)(200 * 512);
    flight_eye_y = _opponent_y[0];
    flight_eye_z = _opponent_z[0] + (int32_t)(30 * 512);
    fire_step();
  }
  CHECK(_fired[0] == 0, "gunner: parked, fired %d rounds", _fired[0]);
}

// --- 8c. Waves -----------------------------------------------------------------------
//
// Each mission's pairs in turn (kOpponentWaves): the next once the last is
// down and its smoke gone, and the goals met once every one is down - or
// the mission's share of them. 09's hunters give up once the player is
// 3 km off; 07's patrols attack once it is within 1.5 km, and give up too;
// and 06 fails when too few of its bombers go down.

// The row the pair up is from.
static int row_up(void) {
  return (int)((_opponent_path[0] - &kOpponentPaths[0][0]) / kOpponentMax);
}

static void eye_at(uint8_t x, uint8_t y, uint8_t z) {
  flight_eye_x = (int32_t)x << 16;
  flight_eye_y = ((int32_t)y << 16) + 0x8000;
  flight_eye_z = (int32_t)z << 16;
}

static void test_waves(void) {
  flight_step_shift = 0;
  kFlightSubstepMask = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  model_on_ground = false;
  static const struct {
    int rows[3];
    int need;
  } kWaves[kMissionCount] = {
      {{0, -1, -1}, 0},  {{1, -1, -1}, 0}, {{2, -1, -1}, 0}, {{3, 4, -1}, 0},
      {{5, 6, -1}, 0},   {{7, 7, 7}, 3},   {{8, 9, -1}, 0},  {{10, 10, 10}, 5},
      {{11, -1, -1}, 0}, {{12, -1, -1}, 0},
  };
  for (int m = 0; m < kMissionCount; ++m) {
    // Out of everyone's way: 6 km from anything any mission sends.
    eye_at(0x70, 0xF0, 0x10);
    opponent_reset((uint8_t)m);
    CHECK(_opponent_need == kWaves[m].need, "waves %d: %d to go down, not %d", m + 1,
          _opponent_need, kWaves[m].need);
    int kills = 0;
    for (int w = 0; w < 3 && kWaves[m].rows[w] >= 0; ++w) {
      CHECK(row_up() == kWaves[m].rows[w] && _opponent_on[0], "waves %d: wave %d is row %d, not %d",
            m + 1, w + 1, row_up(), kWaves[m].rows[w]);
      bool last = w == 2 || kWaves[m].rows[w + 1] < 0;
      CHECK(_opponent_last == last, "waves %d: wave %d last %d", m + 1, w + 1, _opponent_last);
      for (uint8_t i = 0; i < kOpponentMax; ++i) {
        if (!_opponent_live(i)) continue;
        bool met = opponent_targets_down();
        CHECK(met == (kWaves[m].need && kills >= kWaves[m].need),
              "waves %d: goals met %d with %d down", m + 1, met, kills);
        _opponent_destroy(i, nullptr);
        ++kills;
      }
      // The smoke hangs kSmokeDown steps, and then the next pair comes.
      int n = 0;
      for (; n < 100 && _opponent_none(); ++n) opponent_step();
      CHECK(last ? n == 100 : n == kSmokeDown,
            "waves %d: after wave %d, %d steps to the next", m + 1, w + 1, n);
      CHECK(_opponent_kills == kills, "waves %d: %d down, not %d", m + 1, _opponent_kills, kills);
    }
    CHECK(opponent_targets_down() && !_opponent_failed, "waves %d: goals not met, all down",
          m + 1);
  }

  // 08: five of the six, and the sixth still flying.
  {
    eye_at(0x70, 0xF0, 0x10);
    opponent_reset(7);
    for (int k = 0; k < 5; ++k) {
      CHECK(!opponent_targets_down(), "turkey shoot: goals met with %d down", k);
      uint8_t i = _opponent_live(0) ? 0 : 1;
      _opponent_destroy(i, nullptr);
      while (_opponent_none()) opponent_step();
    }
    CHECK(opponent_targets_down() && _opponent_live(1), "turkey shoot: goals not met with 5 down");
  }

  // 09: the hunters stay with a player 2 km ahead, east, and give up on
  // one 3 km ahead: gone, not down, and the goals met.
  {
    eye_at(0x20, 0xA0, 0x04);
    opponent_reset(8);
    flight_eye_y = _opponent_y[0] - (int32_t)2000 * 512;
    for (int n = 0; n < 10; ++n) opponent_step();
    CHECK(_opponent_live(0) && _opponent_live(1) && !opponent_targets_down(),
          "escape: the hunters gave up on a player 2 km ahead");
    flight_eye_y = _opponent_y[0] - (int32_t)3000 * 512;
    _msgs = 0;
    opponent_step();
    CHECK(_opponent_none() && _opponent_kills == 0 && opponent_targets_down() && _msgs == 0,
          "escape: on %d %d, %d down, goals %d, %d messages", _opponent_on[0], _opponent_on[1],
          _opponent_kills, opponent_targets_down(), _msgs);
  }

  // 07: the first patrol does not see a player 3 km off, but sees one 1 km
  // off and attacks, then gives up when it is 3 km off again; the second
  // patrol follows, calm.
  {
    eye_at(0x60, 0x74, 0x04);
    opponent_reset(6);
    for (int n = 0; n < 50; ++n) opponent_step();
    CHECK(_opponent_attack == 0, "patrol: attacked a player 4 km off");
    flight_eye_x = _opponent_x[0] + (int32_t)1000 * 512;
    flight_eye_y = _opponent_y[0];
    opponent_step();
    CHECK(_opponent_attack == 3, "patrol: attack %d, a player 1 km off", _opponent_attack);
    for (int n = 0; n < 20; ++n) opponent_step();
    flight_eye_x = _opponent_x[0] + (int32_t)3000 * 512;
    opponent_step();
    CHECK(_opponent_none() && !_opponent_last, "patrol: the first did not give up");
    opponent_step();
    CHECK(row_up() == 9 && _opponent_live(0) && _opponent_attack == 0 && _opponent_last,
          "patrol: the second is row %d, attack %d", row_up(), _opponent_attack);
  }

  // 06: every bomber leaves unhit, and the mission fails once the third pair
  // has gone, with the city bombed.
  {
    eye_at(0x60, 0xE0, 0x10);
    opponent_reset(5);
    _msgs = 0;
    int n = 0;
    for (; n < 6000 && !_opponent_failed; ++n) opponent_step();
    CHECK(_opponent_failed && _opponent_last && _opponent_kills == 0 && _msgs == 1 &&
              strcmp(_msg, kOpponentFailText[1]) == 0,
          "big week: failed %d after %d steps, \"%s\"", _opponent_failed, n, _msg);
    // ~80 s a pair at 6.25 steps a second, and ~5 s of smoke between.
    CHECK(n > 3 * 450 && n < 3 * 600, "big week: three pairs gone after %d steps", n);
  }
}

int main() {
  test_look_back();
  test_through_the_dot();
  test_diagonal();
  test_random();
  test_two();
  test_both_close();
  test_paths();
  test_flight_model();
  test_missions();
  test_hunting();
  test_provoked();
  test_waves();
  test_guns();
  if (_fails) {
    printf("opponent_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("opponent_test: passed\n");
  return 0;
}
