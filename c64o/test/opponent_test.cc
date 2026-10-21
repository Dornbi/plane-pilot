// Host test for opponent.cc: what each frame offers the sprite stack.
//
// planes_edge_test.cc says planes.cc draws the right thing for each frame.
// This says opponent.cc shows it: a redraw takes two frames there (docs/
// planes.md §13, "The silhouette at half rate"), and until the new bitmap is
// filled the old one stands in, placed at the new centre. That is right only
// while the old one is recent. A bitmap left from the last time the aircraft
// was in view can be any size, and once was: a chase that looked away and
// back saw a frame of the aircraft as close as it had been.
//
// So the rule here, at every frame: the sprite offered is the dot, or a
// silhouette the size of one drawn for this frame or one of the two before it,
// with the aircraft on screen all the way since. "Drawn for" is planes.cc's
// own answer - a copy of the slot's state with its cache thrown away, rendered
// before opponent.cc renders the frame - so the hysteresis is the same and the
// sizes compare exactly.
//
// opponent.cc is #included rather than linked, for its statics; the flight,
// world and mission globals it reads, and the sprite stack it writes, are
// defined here. Its sprite blocks sit in kOpponentBank, which on the host is
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
#include "../world.h"

// --- What opponent.cc reads ---------------------------------------------------

bool flight_paused = false;
enum FlightStatus flight_status;
uint8_t flight_step_shift = 0;
int32_t flight_eye_x, flight_eye_y, flight_eye_z;
mat3_t world_cam;
const uint8_t kMissionStartX[kMissionCount] = {};
const uint8_t kMissionStartY[kMissionCount] = {};

static uint8_t _bank[0x4000];
uint8_t *kOpponentBank = _bank;

// --- What it writes -----------------------------------------------------------

struct offer_t {
  bool on;
  int16_t x, y;
  uint8_t bitmap, bitmap2, color, flags;
};
static offer_t _offer;
static int _offers;

bool sprites_stack_add(int16_t depth, int16_t x, int16_t y, int8_t pivot_x, int8_t pivot_y,
                       uint8_t bitmap, uint8_t bitmap2, uint8_t color, uint8_t flags) {
  (void)depth;
  (void)pivot_x;
  (void)pivot_y;
  _offer = {true, x, y, bitmap, bitmap2, color, flags};
  ++_offers;
  return true;
}

#include "../opponent.cc"

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
static size_t2 offered_extent(void) {
  int cols = (_offer.flags & kSpriteFlagWide) ? 2 : 1;
  int rows = _offer.bitmap2 != kSpriteNoBitmap ? 2 : 1;
  const uint8_t *blocks[4];
  blocks[0] = _opponent_block(_offer.bitmap);
  blocks[1] = _opponent_block((uint8_t)(_offer.bitmap + 1));
  int lower = cols;
  if (rows == 2) {
    blocks[lower] = _opponent_block(_offer.bitmap2);
    blocks[lower + 1] = _opponent_block((uint8_t)(_offer.bitmap2 + 1));
  }
  return extent(blocks, cols, rows, (_offer.flags & kSpriteFlagExpandX) ? 2 : 1,
                (_offer.flags & kSpriteFlagExpandY) ? 2 : 1);
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

// The frame's record, for the two after it.
struct seen_t {
  bool offered;   // anything at all was offered
  bool sized;     // planes.cc draws a silhouette here, not the dot
  size_t2 size;
};

struct scene_t {
  seen_t hist[3];  // this frame, the one before, the one before that
  int frames, silhouettes, dots_standing_in;
};

static void place(const where_t &w) {
  // Camera space, then world: the camera's axes are world_cam's rows.
  double cx = w.range;
  double cy = w.away ? 3.0 * w.range : -(double)w.px * w.range / 256.0;
  double cz = (double)w.py * w.range / 256.0;
  const mat3_t &m = world_cam;
  double rel[3];
  rel[0] = (cx * m.front.x + cy * m.left.x + cz * m.up.x) / 256.0;
  rel[1] = (cx * m.front.y + cy * m.left.y + cz * m.up.y) / 256.0;
  rel[2] = (cx * m.front.z + cy * m.left.z + cz * m.up.z) / 256.0;
  // Quarter metres to 24.8 metres, from an eye in the middle of the world.
  flight_eye_x = 40L << 16;
  flight_eye_y = 100L << 16;
  flight_eye_z = 8L << 16;
  _opponent_x[0] = flight_eye_x + (lround(rel[0]) << 6);
  _opponent_y[0] = flight_eye_y + (lround(rel[1]) << 6);
  _opponent_z[0] = flight_eye_z + (lround(rel[2]) << 6);
}

static void frame(scene_t &sc, const where_t &w, const char *what) {
  place(w);
  // planes.cc's answer for this frame, from the slot's latches as they are.
  seen_t now = {false, false, {0, 0}};
  {
    vec3_t rel, c;
    bool in = _opponent_quarter(_opponent_x[0] - flight_eye_x, 0x7F, &rel.x) &&
              _opponent_quarter(_opponent_y[0] - flight_eye_y, 0xFF, &rel.y) &&
              _opponent_quarter(_opponent_z[0] - flight_eye_z, 0xFF, &rel.z);
    if (in) {
      vec_transform_inv(&world_cam, &rel, &c);
      mat3_t axes;
      axes.front = make_vector(0, world_cam.left.x, world_cam.up.x);
      axes.left = make_vector(0, world_cam.left.y, world_cam.up.y);
      axes.up = make_vector(0, world_cam.left.z, world_cam.up.z);
      static planes_state_t fresh;
      static uint8_t blocks[4][kPlaneBlockStride];
      fresh = _opponent_planes[0];
      fresh.key_valid = false;
      planes_frame_t f;
      planes_render(&fresh, &kView, &c, &axes, blocks[0], &f);
      if (f.hidden == kPlaneShown && f.level != kPlaneLevelDot) {
        const uint8_t *b[4] = {blocks[0], blocks[1], blocks[2], blocks[3]};
        now.sized = true;
        now.size = extent(b, f.cols, f.rows, f.xs, f.ys);
      }
    }
  }

  _offer.on = false;
  opponent_add_sprites();
  now.offered = _offer.on;
  sc.hist[2] = sc.hist[1];
  sc.hist[1] = sc.hist[0];
  sc.hist[0] = now;
  ++sc.frames;
  if (!_offer.on || _offer.bitmap == kOpponentBlockDot) {
    if (_offer.on && now.sized) ++sc.dots_standing_in;
    return;
  }
  ++sc.silhouettes;
  size_t2 shown = offered_extent();
  bool recent = false;
  for (int n = 0; n < 3 && sc.hist[n].offered; ++n) {
    if (sc.hist[n].sized && sc.hist[n].size == shown) {
      recent = true;
      break;
    }
  }
  CHECK(recent,
        "%s: frame %d, %d m out, shows a %dx%d silhouette; the last three frames drew "
        "%dx%d, %dx%d, %dx%d",
        what, sc.frames, w.range / 4, shown.w, shown.h, sc.hist[0].size.w, sc.hist[0].size.h,
        sc.hist[1].offered ? sc.hist[1].size.w : -1, sc.hist[1].offered ? sc.hist[1].size.h : -1,
        sc.hist[2].offered ? sc.hist[2].size.w : -1, sc.hist[2].offered ? sc.hist[2].size.h : -1);
}

static void start(scene_t &sc, const mat3_t &cam) {
  world_cam = cam;
  _opponent_on[0] = true;
  opponent_invalidate();
  memset(&sc, 0, sizeof(sc));
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

int main() {
  test_look_back();
  test_through_the_dot();
  test_random();
  if (_fails) {
    printf("opponent_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("opponent_test: passed\n");
  return 0;
}
