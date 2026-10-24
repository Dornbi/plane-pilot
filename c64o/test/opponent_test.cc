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

#include "../flight.h"
#include "../mission.h"
#include "../planes.h"
#include "../sprites.h"
#include "../vec.h"
#include "../view.h"
#include "../world.h"

// --- What opponent.cc reads ---------------------------------------------------

bool flight_paused = false;
view_state_t view_state = VIEW_CENTER;
enum FlightStatus flight_status;
uint8_t flight_step_shift = 0;
uint8_t kFlightSubstepMask = 0;
int32_t flight_eye_x, flight_eye_y, flight_eye_z;
mat3_t world_cam;
uint16_t flight_nav_point_x[kMaxNavPoints];
uint16_t flight_nav_point_y[kMaxNavPoints];

static uint8_t _bank[0x4000];
uint8_t *kOpponentBank = _bank;

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

// Where the aircraft is: `range` quarter metres ahead of the camera, `px` and
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
  // Quarter metres to 24.8 metres.
  flight_eye_x = 40L << 16;
  flight_eye_y = 100L << 16;
  flight_eye_z = 8L << 16;
  _opponent_x[slot] = flight_eye_x + (lround(rel[0]) << 6);
  _opponent_y[slot] = flight_eye_y + (lround(rel[1]) << 6);
  _opponent_z[slot] = flight_eye_z + (lround(rel[2]) << 6);
}

// planes.cc's answer for slot `slot` this frame, from its latches as they
// were before opponent.cc drew the frame, and under the view it chose.
static seen_t reference(int slot, const planes_state_t &latches, const planes_view_t &view) {
  seen_t now = {false, false, false, {0, 0}};
  uint8_t range = _opponent_camera((uint8_t)slot);
  vec3_t c = _opponent_c;
  if (range != kOpponentOut) {
    const planes_view_t &v = range == kOpponentFar ? kViewFar : view;
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
  _opponent_cap(_opponent_cull());
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
    now[k] = reference(k, latches[k], (_opponent_capped & (1 << k)) ? kViewTwo : kView);
  }

  // The stack's seven, less the fin's two in the back view: both always fit.
  int sprites = sprites_of(_offer[0]) + sprites_of(_offer[1]);
  int budget = kSpriteTerrainSlots - (view_state == VIEW_BACK ? 2 : 0);
  CHECK(sprites <= budget, "%s: frame %d offers %d sprites, %d fit", what, sc.frames, sprites,
        budget);
  if (_offer[0].on && _offer[1].on && sprites > 4) ++sc.shared;
  // In front the one held to two is the farther, give or take the eighth
  // that keeps two at about the same range from trading.
  if (_opponent_capped == 1 || _opponent_capped == 2) {
    int far = _opponent_capped == 1 ? 0 : 1;
    int16_t dn = _opponent_cs[far ^ 1].x, df = _opponent_cs[far].x;
    // Both in one range, by depth; else the one in the far range, whose
    // depth is in metres.
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
// to 600 m; then in view again. The first frame back may show the dot, never
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
// Both inside ~125 m, where either alone would be 2 x 2, walking in and out
// past each other so that the nearer changes, in the front view and the back.
// The budget check in frame() is the point: in front the farther is held to
// two sprites, behind both are.

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
    if (v == VIEW_BACK) {
      CHECK(sc.four_and_two == 0, "%s: %d frames with one at 2 x 2", what, sc.four_and_two);
    } else {
      CHECK(sc.four_and_two > 300, "%s: only %d frames of one at 2 x 2 and one at two", what,
            sc.four_and_two);
    }
    printf("  %s: %d frames, %d with more than four sprites between them, %d of 2 x 2 and "
           "two\n",
           what, sc.frames, sc.shared, sc.four_and_two);
  }
  view_state = VIEW_CENTER;
}

// --- 6. The paths ---------------------------------------------------------------

// Metres from a waypoint coordinate, 8.8 units, to a position, 24.8.
static double metres(uint16_t w, int32_t pos, bool wrap) {
  double d = ((double)((int32_t)w << 8) - (double)pos) / 256.0;
  if (wrap) {
    d = fmod(d + 16384.0 + 65536.0, 32768.0) - 16384.0;
  }
  return d;
}

// Two paths of the test's own, flown as opponent_reset() starts a mission's:
// dead straight north for ever, the world repeating every 128 units of x,
// and a 3 km square anticlockwise.
static const opponent_path_t kStraight = {kOpponentLoop, 4, kOpponentNoFail, 1500,
                                          0x1A70, 0x3FE0, 256, 0, 0x0060,
                                          {0x3A70, 0x5A70, 0x7A70, 0x9A70},
                                          {0x3FE0, 0x3FE0, 0x3FE0, 0x3FE0}};
static const opponent_path_t kSquare = {kOpponentLoop, 4, kOpponentNoFail, 1500,
                                        0x0A00, 0x6200, 256, 0, 0x0700,
                                        {0x1600, 0x1600, 0x0A00, 0x0A00},
                                        {0x6200, 0x6E00, 0x6E00, 0x6200}};

static void fly(int slot, const opponent_path_t *p) {
  _opponent_on[slot] = true;
  _opponent_path[slot] = p;
  _opponent_next[slot] = 0;
  _opponent_x[slot] = (int32_t)p->start_x << 8;
  _opponent_y[slot] = (int32_t)p->start_y << 8;
  _opponent_z[slot] = (int32_t)p->z << 8;
  _opponent_head[slot] = make_vector(p->head_x, p->head_y, 0);
  _opponent_turn[slot] = _opponent_bank[slot] = 0;
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
    const opponent_path_t &patrol = kSquare;

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
      CHECK(_opponent_y[0] == y0 && _opponent_x[0] - x0 == ((kStraight.speed << 1) >> shift),
            "paths: the pass moved %d, %d at step %d", (int)(_opponent_x[0] - x0),
            (int)(_opponent_y[0] - y0), n);

      // The patrol: waypoints in order, and the wings never rocked from one
      // side to the other.
      if (_opponent_next[1] != next) {
        CHECK(_opponent_next[1] == (next + 1) % patrol.count,
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
      CHECK(_opponent_z[1] == (int32_t)patrol.z << 8, "paths: the patrol changed height");
      // How far outside its square, in metres.
      double nx = metres(patrol.x[0], _opponent_x[1], true);
      double sx = metres(patrol.x[2], _opponent_x[1], true);
      double wy = metres(patrol.y[1], _opponent_y[1], false);
      double ey = metres(patrol.y[0], _opponent_y[1], false);
      double out = std::max(std::max(-nx, sx), std::max(-wy, ey));
      worst_out = std::max(worst_out, out);
    }
    CHECK(reached >= 4 * (int)patrol.count, "paths: shift %d, %d waypoints in %d steps", shift,
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
    // Corners are cut by the reach and rounded by the turn, ~375 m: not more
    // than a kilometre outside.
    CHECK(worst_out < 1000, "paths: shift %d, %.0f m outside the square", shift, worst_out);
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
  for (int t = -1; t <= 1; ++t) {
    _opponent_bank[0] = (int8_t)t;
    mat3_t a;
    _opponent_axes(0, &a);
    CHECK(a.front.y == 0 && a.front.z == 0, "bank %d: the front moved, %d %d", t, a.front.y,
          a.front.z);
    CHECK(a.left.z == -t * kOpponentBankSin && a.up.y == t * kOpponentBankSin,
          "bank %d: left.z %d, up.y %d", t, a.left.z, a.up.y);
    CHECK(a.left.y == (t ? kOpponentBankCos : 256) && a.up.z == (t ? kOpponentBankCos : 256),
          "bank %d: left.y %d, up.z %d", t, a.left.y, a.up.z);
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

static void test_missions(void) {
  flight_step_shift = 0;
  kFlightSubstepMask = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;

  // Every leg in reach.
  for (int m = 0; m < kMissionCount; ++m) {
    for (int k = 0; k < kOpponentMax; ++k) {
      const opponent_path_t &p = kOpponentPaths[m][k];
      if (p.kind == kOpponentNone) continue;
      CHECK(p.fail == kOpponentNoFail || p.fail < p.count, "mission %d slot %d: fail at %d of %d",
            m + 1, k, p.fail, p.count);
      uint16_t px = p.start_x, py = p.start_y;
      for (int w = 0; w < p.count; ++w) {
        int dx = (int16_t)(p.x[w] - px), dy = (int16_t)(p.y[w] - py);
        // x repeats every 128 units, 0x8000 in 8.8.
        dx = ((dx + 0x4000) & 0x7FFF) - 0x4000;
        CHECK(abs(dx) < 0x4000 - 0x400 && abs(dy) < 0x4000 - 0x400,
              "mission %d slot %d: leg %d is %.1f, %.1f units", m + 1, k, w, dx / 256.0,
              dy / 256.0);
        px = p.x[w];
        py = p.y[w];
      }
    }
  }

  // 01: the recon.
  {
    _msgs = 0;
    opponent_reset(0);
    CHECK(_opponent_on[0] && !_opponent_on[1], "recon: one aircraft");
    CHECK(_opponent_nav == 0, "recon: the targets' navpoint is %d, not 1", _opponent_nav + 1);
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
        // Navpoint 1 on the recon, to the metre.
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
    CHECK(_opponent_nav == 0, "bombers: the targets' navpoint is %d, not 1", _opponent_nav + 1);
    double close = 1e9, far = 0;
    int city_at = -1, failed_at = -1, gone = 0;
    for (int n = 0; n < 6000 && gone < 2; ++n) {
      uint8_t w = _opponent_next[0];
      opponent_step();
      if (w == 1 && _opponent_next[0] == 2) city_at = n;
      if (_opponent_failed && failed_at < 0) failed_at = n;
      gone = !_opponent_on[0] + !_opponent_on[1];
      if (_opponent_on[0] && _opponent_on[1]) {
        // Navpoint 1 midway between them, give or take the halving's metre.
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
        double dx = (_opponent_x[1] - _opponent_x[0]) / 256.0;
        double dy = (_opponent_y[1] - _opponent_y[0]) / 256.0;
        double d = sqrt(dx * dx + dy * dy);
        close = std::min(close, d);
        far = std::max(far, d);
      }
    }
    // 150 m and 200 m apart: 250 m, and both turn alike.
    CHECK(close > 200 && far < 300, "bombers: %.0f-%.0f m apart on the way in", close, far);
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

  // 04: the scramble. Two flying loops for ever: never failing, never gone,
  // and navpoint 1 midway between them.
  {
    _msgs = 0;
    opponent_reset(3);
    CHECK(_opponent_on[0] && _opponent_on[1] && _opponent_nav == 0,
          "scramble: two aircraft, navpoint %d", _opponent_nav + 1);
    for (int n = 0; n < 6000; ++n) opponent_step();
    CHECK(_opponent_on[0] && _opponent_on[1] && !_opponent_failed && _msgs == 0,
          "scramble: an aircraft left, or the mission failed");
    CHECK(_opponent_z[1] == (int32_t)0x0700 << 8, "scramble: the patrol changed height");
    // Still round the city at (0x10, 0x68), within its 3 km square.
    CHECK(fabs(units(_opponent_x[1]) - 0x10) < 8 && fabs(units(_opponent_y[1]) - 0x68) < 8,
          "scramble: the patrol is at %.1f, %.1f", units(_opponent_x[1]), units(_opponent_y[1]));
    _opponent_down = 3;
    CHECK(opponent_targets_down(), "scramble: both down, but the targets are not down");
  }
}

int main() {
  test_look_back();
  test_through_the_dot();
  test_random();
  test_two();
  test_both_close();
  test_paths();
  test_missions();
  if (_fails) {
    printf("opponent_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("opponent_test: passed\n");
  return 0;
}
