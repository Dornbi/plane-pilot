// What a single aircraft looks like on screen at the renderer's edge cases.
//
// planes_test.cc holds planes.cc to lib/planes.py byte for byte, which says
// the port is faithful but not that either is right. This says what is right
// in screen terms, whatever the reference does: the ink each frame puts on the
// screen - every set pixel of every sprite, at its expansion and position -
// is followed through the places where the layout changes under it:
//
//   - the dot, 1:1 and X tiers, and the switches between them;
//   - one sprite to two, across or down, and two to four;
//   - Y-expansion switching on and off;
//   - the slide at the bottom of the viewport, through the 128-line band
//     and out past a whole buffer;
//   - and the hysteresis at each of those, under a range that wobbles.
//
// At every frame the ink must be there, inside a layout of at most four
// sprites (two under kPlaneKMaxTwoSprites), with no vertex outside the buffer
// it was filled into, and a cache hit must stand for the bitmap a redraw
// would draw. From one frame to the next of a slow approach its box may move
// only a pixel or two, at a layout switch as anywhere else, and its ink grow
// or shrink only by the model's own rounding: a jump there is the aircraft
// visibly popping. A slid bitmap must show what an unslid one
// shows on the lines it covers.
//
// Host only: the C here is planes_asm.cc's, step for step, and target_test.cc
// holds the assembly to the same reference planes_test.cc does.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "../planedef.h"
#include "../planes.h"

static int _fails = 0;

#define CHECK(cond, ...)                   \
  do {                                     \
    if (!(cond)) {                         \
      if (++_fails <= 20) {                \
        printf("planes_edge_test: ");      \
        printf(__VA_ARGS__);               \
        printf("\n");                      \
      }                                    \
    }                                      \
  } while (0)

// ppilot's and fpilot's viewport (opponent.cc kView), and one with the cuts
// out of reach, for the slide's unslid twin.
static const int16_t kCx0 = 160, kCy0 = 56, kCut1 = 89, kCut2 = 68;
static const planes_view_t kView = {kCx0, kCy0, kCut1, kCut2, kPlaneKMax, kPlaneKnModel};
static const planes_view_t kViewTwo = {kCx0, kCy0, kCut1, kCut2, kPlaneKMaxTwoSprites, kPlaneKnModel};
static const planes_view_t kViewNoCut = {kCx0, kCy0, 10000, 10000, kPlaneKMax, kPlaneKnModel};

// ---------------------------------------------------------------------------
// Attitudes, as camera-space body axes in 8.8: heading off the view axis, then
// pitch, then roll about the nose. Camera x is forward, y left, z up.

struct attitude_t {
  int heading, pitch, roll;  // degrees
};

static mat3_t axes_for(const attitude_t &a) {
  const double r = M_PI / 180.0;
  double h = a.heading * r, p = a.pitch * r, b = a.roll * r;
  double f[3] = {cos(p) * cos(h), cos(p) * sin(h), sin(p)};
  double l0[3] = {-sin(h), cos(h), 0};
  double u0[3] = {-sin(p) * cos(h), -sin(p) * sin(h), cos(p)};
  double l[3], u[3];
  for (int i = 0; i < 3; ++i) {
    l[i] = cos(b) * l0[i] + sin(b) * u0[i];
    u[i] = -sin(b) * l0[i] + cos(b) * u0[i];
  }
  mat3_t m;
  m.front = make_vector((int16_t)lround(256 * f[0]), (int16_t)lround(256 * f[1]),
                        (int16_t)lround(256 * f[2]));
  m.left = make_vector((int16_t)lround(256 * l[0]), (int16_t)lround(256 * l[1]),
                       (int16_t)lround(256 * l[2]));
  m.up = make_vector((int16_t)lround(256 * u[0]), (int16_t)lround(256 * u[1]),
                     (int16_t)lround(256 * u[2]));
  return m;
}

static std::vector<attitude_t> attitudes(void) {
  std::vector<attitude_t> out;
  static const int kHeadings[] = {0, 30, 60, 90, 120, 150, 180, 225, 270, 315};
  static const int kPitches[] = {-60, -20, 0, 20, 60, 89};
  static const int kRolls[] = {0, 30, 60, 90, 135};
  for (int h : kHeadings) {
    for (int p : kPitches) {
      for (int b : kRolls) {
        out.push_back({h, p, b});
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// One aircraft rendered the way a program shows it: two block sets, the
// front one on screen.

struct plane_t {
  planes_state_t state;
  uint8_t sets[2][4][kPlaneBlockStride];
  int front;
  void init() {
    planes_state_init(&state);
    memset(sets, 0, sizeof(sets));
    front = 0;
  }
};

static uint8_t _dot[kPlaneBlockStride];

// The screen pixels a frame sets, and their box.
struct ink_t {
  int x0, x1, y0, y1;  // inclusive; x0 > x1 when empty
  int count;
  std::vector<uint32_t> px;  // (y + 512) << 12 | (x + 512), ascending
  bool empty() const { return count == 0; }
};

static void ink_add(ink_t &ink, int x, int y) {
  if (ink.count == 0 || x < ink.x0) ink.x0 = x;
  if (ink.count == 0 || x > ink.x1) ink.x1 = x;
  if (ink.count == 0 || y < ink.y0) ink.y0 = y;
  if (ink.count == 0 || y > ink.y1) ink.y1 = y;
  ++ink.count;
  ink.px.push_back((uint32_t)(y + 512) << 12 | (uint32_t)(x + 512));
}

// `blocks` is cols * rows sprite blocks, row-major, at the frame's position
// and expansion.
static ink_t ink_of(const planes_frame_t &f, const uint8_t (*blocks)[kPlaneBlockStride]) {
  ink_t ink = {1, 0, 1, 0, 0, {}};
  for (int b = 0; b < f.cols * f.rows; ++b) {
    int bc = b % f.cols, br = b / f.cols;
    for (int row = 0; row < 21; ++row) {
      for (int col = 0; col < 24; ++col) {
        if (!(blocks[b][row * 3 + (col >> 3)] & (0x80 >> (col & 7)))) {
          continue;
        }
        int sx = f.x + (bc * 24 + col) * f.xs, sy = f.y + (br * 21 + row) * f.ys;
        for (int dy = 0; dy < f.ys; ++dy) {
          for (int dx = 0; dx < f.xs; ++dx) {
            ink_add(ink, sx + dx, sy + dy);
          }
        }
      }
    }
  }
  std::sort(ink.px.begin(), ink.px.end());
  return ink;
}

// Render one frame, and check what holds of every frame on its own.
static planes_frame_t step(plane_t &p, const planes_view_t &view, const vec3_t &c,
                           const mat3_t &axes, ink_t *ink, const char *what) {
  planes_frame_t f;
  planes_render(&p.state, &view, &c, &axes, p.sets[p.front ^ 1][0], &f);
  ink->count = 0;
  ink->px.clear();
  if (f.hidden != kPlaneShown) {
    return f;
  }
  if (f.level == kPlaneLevelDot) {
    *ink = ink_of(f, (const uint8_t(*)[kPlaneBlockStride])_dot);
    return f;
  }
  if (!f.cached) {
    p.front ^= 1;
  }
  int sprites = f.cols * f.rows;
  CHECK(f.cols >= 1 && f.cols <= 2 && f.rows >= 1 && f.rows <= 2, "%s: layout %dx%d", what,
        f.cols, f.rows);
  CHECK(view.kmax > kPlaneKMaxTwoSprites || sprites <= 2,
        "%s: %d sprites under the two-sprite cap", what, sprites);
  CHECK(f.xs == (f.level == kPlaneLevelX ? 2 : 1), "%s: level %d with xs %d", what, f.level,
        f.xs);
  CHECK(f.level == kPlaneLevelX || f.ys == 1, "%s: Y-expanded at 1:1", what);

  // No vertex outside the buffer: the fill does not clip to its sides, and
  // would clamp a vertex past the right one onto the last column.
  const planes_state_t &s = p.state;
  for (int i = 0; i < s.key_count; ++i) {
    CHECK(s.key_x[i] < 24 * f.cols, "%s: vertex %d at x %d in a %d-column buffer", what, i,
          s.key_x[i], f.cols);
    if (f.slid == 0) {
      CHECK(s.key_y[i] < 21 * f.rows, "%s: vertex %d at y %d in a %d-row buffer", what, i,
            s.key_y[i], f.rows);
    }
  }

  // A hit stands for what a redraw would show: the same frame projected
  // afresh, from the same latches with the key thrown away, lands in the same
  // place with the same bitmap.
  if (f.cached) {
    static planes_state_t fresh;
    static uint8_t redraw[4][kPlaneBlockStride];
    fresh = p.state;
    fresh.key_valid = false;
    planes_frame_t g;
    planes_render(&fresh, &view, &c, &axes, redraw[0], &g);
    bool same = g.cols == f.cols && g.rows == f.rows && g.ys == f.ys && g.x == f.x &&
                g.y == f.y && !g.cached;
    for (int b = 0; same && b < sprites; ++b) {
      same = memcmp(redraw[b], p.sets[p.front][b], kPlaneBlockBytes) == 0;
    }
    CHECK(same, "%s: from the cache, but a redraw shows something else", what);
  }

  *ink = ink_of(f, p.sets[p.front]);
  // The slide may push all of it below the buffer; nothing else may.
  CHECK(!ink->empty() || f.slid > 0, "%s: shown, but no ink", what);
  return f;
}

static int sprites_of(const planes_frame_t &f) {
  return f.level == kPlaneLevelDot ? 0 : f.cols * f.rows;
}

static void describe(char *buf, size_t n, const attitude_t &a, int x) {
  snprintf(buf, n, "h%d p%d r%d at %d.%02d m", a.heading, a.pitch, a.roll, x / 4,
           (x % 4) * 25);
}

// ---------------------------------------------------------------------------
// 1. Approach and recede.
//
// From the dot to inside the size cap and back, 0.5% of the range a frame,
// on the view axis and off it. What changes between two frames, and where.

// The layout transitions, for the coverage count and the per-kind maxima.
enum { kDot1x, k1xX, kCols, kRows, kYs, kTransitionKinds, kNone = -1 };
static const char *const kTransitionNames[] = {"dot <-> 1:1", "1:1 <-> X", "1 <-> 2 across",
                                               "1 <-> 2 down", "Y-expansion"};

static int transition(const planes_frame_t &a, const planes_frame_t &b) {
  if (a.level != b.level) {
    return (a.level == kPlaneLevelDot || b.level == kPlaneLevelDot) ? kDot1x : k1xX;
  }
  if (a.ys != b.ys) return kYs;
  if (a.cols != b.cols) return kCols;
  if (a.rows != b.rows) return kRows;
  return kNone;
}

struct stats_t {
  int count[kTransitionKinds + 1];
  int edge_max[kTransitionKinds + 1];  // the largest move of any side of the box
  int centre_max[kTransitionKinds + 1];  // ... of its centre, in half pixels
  double area_max[kTransitionKinds + 1];  // the largest change in ink, as a ratio
};

// The most a side of the ink's box may move in one 0.5% step. An ordinary
// frame moves it by growth and by the floor onto two-pixel pixels, whose
// phase follows the box's centre: 4 at most, at 2 x 2 pixels. A layout switch
// must do no worse than that, and measures 3; more is a visible pop.
static const int kEdgeMove = 4;
static const int kEdgeMoveSwitch = 3;

// A 3 degree turn swings the wing tips, 41 pixels out at the cap, by 2: with
// the floor onto two-pixel pixels, 4 like any other frame. Measures 3.
static const int kEdgeTurn = 4;

// The most the ink may grow or shrink in one 0.5% step: by 4 pixels, or by
// 2.5 times. That much is the model's own quantisation - each magnitude
// rounds to its first whole pixel at one scale, and at k 22, ~185 m, the
// fuselage's radius does: the body goes from a line to three pixels wide, and
// nose- or tail-on the end-on disc appears, at once. lib/planes.py does the
// same. Anything larger is a part popping in or out.
static const double kAreaStep = 2.5;
static const int kAreaSlack = 4;

static int iabs(int v) { return v < 0 ? -v : v; }

static void sweep(const attitude_t &a, int off_x, int off_y, const planes_view_t &view,
                  stats_t &st) {
  mat3_t axes = axes_for(a);
  plane_t p;
  p.init();
  // 1,000 m in, past the dot's limit, to 20 m out, well inside the cap; then
  // back out.
  std::vector<int> range;
  for (double x = 8000; x > 160; x *= 0.995) range.push_back((int)x);
  for (int i = (int)range.size() - 2; i >= 0; --i) range.push_back(range[i]);

  planes_frame_t last = {};
  ink_t last_ink, ink;
  bool have_last = false;
  int last_level = -1, last_ys = 0, last_sprites = -1;
  bool approaching = true;
  for (size_t n = 0; n < range.size(); ++n) {
    int x = range[n];
    if (n > 0 && range[n] > range[n - 1]) approaching = false;
    // The centre's offset in pixels, held as the range changes.
    vec3_t c = make_vector((int16_t)x, (int16_t)(-(off_x * x) / 256),
                           (int16_t)(-(off_y * x) / 256));
    char what[96];
    describe(what, sizeof(what), a, x);
    planes_frame_t f = step(p, view, c, axes, &ink, what);
    CHECK(f.hidden == kPlaneShown, "%s: hidden (%d) on screen", what, f.hidden);
    if (f.hidden != kPlaneShown) {
      have_last = false;
      continue;
    }

    // Monotone: the approach never goes back down a tier or out of
    // Y-expansion, and the way back never up; within one tier and expansion
    // the sprite count goes one way too. A reversal is a flicker.
    int sprites = sprites_of(f);
    if (last_level >= 0) {
      if (approaching) {
        CHECK(f.level >= last_level, "%s: tier %d back to %d on the way in", what, last_level,
              f.level);
        CHECK(f.level != last_level || f.ys >= last_ys, "%s: Y-expansion off on the way in",
              what);
        CHECK(f.level != last_level || f.ys != last_ys || sprites >= last_sprites,
              "%s: %d sprites back to %d on the way in", what, last_sprites, sprites);
      } else {
        CHECK(f.level <= last_level, "%s: tier %d up to %d on the way out", what, last_level,
              f.level);
        CHECK(f.level != last_level || f.ys <= last_ys, "%s: Y-expansion on on the way out",
              what);
        CHECK(f.level != last_level || f.ys != last_ys || sprites <= last_sprites,
              "%s: %d sprites up to %d on the way out", what, last_sprites, sprites);
      }
    }
    last_level = f.level;
    last_ys = f.ys;
    last_sprites = sprites;

    if (have_last && !ink.empty() && !last_ink.empty()) {
      int t = transition(last, f);
      int k = t == kNone ? kTransitionKinds : t;
      int e = iabs(ink.x0 - last_ink.x0);
      e = std::max(e, iabs(ink.x1 - last_ink.x1));
      e = std::max(e, iabs(ink.y0 - last_ink.y0));
      e = std::max(e, iabs(ink.y1 - last_ink.y1));
      int cm = std::max(iabs(ink.x0 + ink.x1 - last_ink.x0 - last_ink.x1),
                        iabs(ink.y0 + ink.y1 - last_ink.y0 - last_ink.y1));
      CHECK(e <= (t == kNone ? kEdgeMove : kEdgeMoveSwitch),
            "%s: box side moved %d px%s%s", what, e, t == kNone ? "" : " at ",
            t == kNone ? "" : kTransitionNames[t]);
      double area = (double)std::max(ink.count, last_ink.count) /
                    std::min(ink.count, last_ink.count);
      // The dot is a fixed 2 x 2 block and the smallest 1:1 silhouettes a
      // few pixels, so that switch is held to its box alone.
      CHECK(t == kDot1x || area <= kAreaStep || iabs(ink.count - last_ink.count) <= kAreaSlack,
            "%s: ink went from %d to %d pixels%s%s", what,
            last_ink.count, ink.count, t == kNone ? "" : " at ",
            t == kNone ? "" : kTransitionNames[t]);
      if (t != kDot1x) st.area_max[k] = std::max(st.area_max[k], area);
      ++st.count[k];
      st.edge_max[k] = std::max(st.edge_max[k], e);
      st.centre_max[k] = std::max(st.centre_max[k], cm);
    }
    last = f;
    last_ink = ink;
    have_last = true;
  }
}

static void test_sweeps(void) {
  stats_t st = {};
  stats_t st_two = {};
  for (const attitude_t &a : attitudes()) {
    sweep(a, 0, 0, kView, st);
    sweep(a, 70, -25, kView, st);
    sweep(a, -40, 18, kView, st);
    sweep(a, 0, 0, kViewTwo, st_two);
  }
  for (int k = 0; k < kTransitionKinds; ++k) {
    CHECK(st.count[k] > 0, "the sweeps never cross %s", kTransitionNames[k]);
  }
  CHECK(st_two.count[kRows] > 0, "the two-sprite sweeps never stack two sprites");
  for (int k = 0; k <= kTransitionKinds; ++k) {
    printf("  %-16s %7d frames, box sides move <= %d px, centre <= %d half px, ink x %.2f\n",
           k < kTransitionKinds ? kTransitionNames[k] : "no change", st.count[k],
           st.edge_max[k], st.centre_max[k], st.area_max[k]);
  }
  for (int k = 0; k <= kTransitionKinds; ++k) {
    printf("  two-sprite cap: %-16s %6d frames, sides <= %d, centre <= %d\n",
           k < kTransitionKinds ? kTransitionNames[k] : "no change", st_two.count[k],
           st_two.edge_max[k], st_two.centre_max[k]);
  }
}

// ---------------------------------------------------------------------------
// 2. A range that wobbles on a switch.
//
// A chase holds the range, give or take, so each switch the sweeps found is
// approached from 30% out and then held within 2% of it for a second and a
// half: one change at most, where the switch itself happens, and none back.
// The latches demote at 0.87 of each limit (planedef.h), so a 2% wobble that
// changed the layout twice would be a flicker.

struct layout_key_t {
  int level, ys, cols, rows;
  bool operator!=(const layout_key_t &o) const {
    return level != o.level || ys != o.ys || cols != o.cols || rows != o.rows;
  }
};

static layout_key_t key_of(const planes_frame_t &f) {
  return {f.level, f.ys, f.level == kPlaneLevelDot ? 1 : f.cols,
          f.level == kPlaneLevelDot ? 1 : f.rows};
}

static void wobble(const attitude_t &a, const planes_view_t &view, int *held) {
  mat3_t axes = axes_for(a);
  // Find the switches: one fresh approach.
  std::vector<int> at;
  {
    plane_t p;
    p.init();
    ink_t ink;
    layout_key_t last = {-1, 0, 0, 0};
    for (double x = 8000; x > 160; x *= 0.995) {
      vec3_t c = make_vector((int16_t)x, 0, 0);
      planes_frame_t f = step(p, view, c, axes, &ink, "wobble search");
      layout_key_t k = key_of(f);
      if (last.level >= 0 && k != last) at.push_back((int)x);
      last = k;
    }
  }
  for (int xt : at) {
    plane_t p;
    p.init();
    ink_t ink;
    char what[96];
    for (double x = xt * 1.3; x > xt; x *= 0.99) {
      vec3_t c = make_vector((int16_t)x, 0, 0);
      step(p, view, c, axes, &ink, "wobble approach");
    }
    describe(what, sizeof(what), a, xt);
    layout_key_t last = {-1, 0, 0, 0};
    int changes = 0;
    for (int n = 0; n < 75; ++n) {
      // +2%, -2%, 0, +1%, -1%, ...: a slow beat round the switch.
      static const int kPerMille[] = {20, -20, 0, 10, -10, 5, -5};
      int x = xt + xt * kPerMille[n % 7] / 1000;
      vec3_t c = make_vector((int16_t)x, 0, 0);
      layout_key_t k = key_of(step(p, view, c, axes, &ink, what));
      if (last.level >= 0 && k != last) ++changes;
      last = k;
    }
    CHECK(changes <= 1, "%s: the layout changed %d times under a 2%% wobble", what, changes);
    ++*held;
  }
}

static void test_wobble(void) {
  int held = 0;
  for (const attitude_t &a : attitudes()) {
    wobble(a, kView, &held);
    wobble(a, kViewTwo, &held);
  }
  printf("  %d switches held under a 2%% wobble\n", held);
}

// ---------------------------------------------------------------------------
// 3. The bottom of the viewport.
//
// No sprite may start below the DMA cut, so a buffer whose last row would is
// moved up to it and the silhouette drawn lower inside it (docs/planes.md
// section 11). On the lines the moved buffer covers, the screen must show what
// an unmoved one would - the same aircraft rendered with the cut out of reach
// - and its last row must start on the cut exactly. Followed a line a frame
// from the centre to 45 degrees below it, which passes slides of 128 lines
// and more and slides that leave the buffer empty.
//
// One exception, and it is not a defect: Y-expanded rows are two lines, and a
// slide by an odd number of lines puts their boundaries a line from the
// unmoved buffer's. The rows are the same; each may start a line earlier.

static bool row_covered(const planes_frame_t &f, int y) {
  return y >= f.y && y < f.y + 21 * f.rows * f.ys;
}

static void slide(const attitude_t &a, int x, int *frames, int *partial, int *band,
                  int *emptied) {
  mat3_t axes = axes_for(a);
  plane_t p, twin;
  p.init();
  twin.init();
  ink_t ink, twin_ink;
  // A line a frame down, then jumps: a view switch, or a pull through the
  // vertical, moves the centre a long way at once, and a slide that changes by
  // a whole 128-line band leaves the vertex bytes, which hold y modulo 256,
  // as they were (planes.h).
  uint32_t seed = 12345u + (uint32_t)x + (uint32_t)(a.heading * 7 + a.pitch * 3 + a.roll);
  for (int n = 0; n < 255 + 64; ++n) {
    int down = n;
    if (n >= 255) {
      seed = seed * 1103515245u + 12345u;
      down = (int)((seed >> 16) % 255);
    }
    vec3_t c = make_vector((int16_t)x, 0, (int16_t)(-(down * x) / 256));
    char what[128];
    char at[96];
    describe(at, sizeof(at), a, x);
    snprintf(what, sizeof(what), "%s, %d lines down", at, down);
    planes_frame_t f = step(p, kView, c, axes, &ink, what);
    planes_frame_t g = step(twin, kViewNoCut, c, axes, &twin_ink, what);
    if (f.hidden != kPlaneShown || g.hidden != kPlaneShown) {
      CHECK(f.hidden == g.hidden || f.hidden == kPlaneBelowCut, "%s: hidden %d, unslid %d",
            what, f.hidden, g.hidden);
      break;
    }
    ++*frames;
    CHECK(!(key_of(f) != key_of(g)), "%s: the slide changed the layout", what);
    CHECK(g.slid == 0, "%s: slid with no cut", what);
    if (f.level == kPlaneLevelDot) {
      continue;
    }
    int cut = f.ys == 2 ? kCut2 : kCut1;
    int last_row = f.y + 21 * (f.rows - 1) * f.ys;
    CHECK(last_row <= cut, "%s: last sprite row starts on line %d, below the cut %d", what,
          last_row, cut);
    CHECK(f.slid == 0 || last_row == cut, "%s: slid %d but the last row starts on %d, not %d",
          what, f.slid, last_row, cut);
    if (f.slid >= 128) ++*band;
    if (f.slid > 0 && ink.empty()) ++*emptied;
    if (f.slid > 0 && !ink.empty()) ++*partial;

    // What the unslid twin shows on the lines the slid buffer covers.
    std::vector<uint32_t> want;
    for (uint32_t px : twin_ink.px) {
      if (row_covered(f, (int)(px >> 12) - 512)) want.push_back(px);
    }
    if (f.ys == 1 || (f.slid & 1) == 0) {
      CHECK(want == ink.px, "%s: slid %d shows %zu pixels where unslid shows %zu", what,
            f.slid, ink.px.size(), want.size());
    } else {
      // Each expanded row a line early, or in place.
      for (uint32_t px : ink.px) {
        bool found = std::binary_search(twin_ink.px.begin(), twin_ink.px.end(), px) ||
                     std::binary_search(twin_ink.px.begin(), twin_ink.px.end(), px + 4096) ||
                     std::binary_search(twin_ink.px.begin(), twin_ink.px.end(), px - 4096);
        CHECK(found, "%s: slid %d sets a pixel nowhere near the unslid ones", what, f.slid);
        if (!found) break;
      }
      CHECK(iabs((int)ink.px.size() - (int)want.size()) <= 2 * 2 * 48,
            "%s: slid %d shows %zu pixels where unslid shows %zu", what, f.slid,
            ink.px.size(), want.size());
    }
  }
}

static void test_slide(void) {
  int frames = 0, partial = 0, band = 0, emptied = 0;
  // Ranges for each tier and layout: 1:1, X in one sprite, and the 2 x 2 at
  // and inside the cap, where Y-expansion comes in at the steeper attitudes.
  static const int kRanges[] = {3000, 1000, 700, 400, 250, 160};
  for (const attitude_t &a : attitudes()) {
    for (int x : kRanges) {
      slide(a, x, &frames, &partial, &band, &emptied);
    }
  }
  CHECK(partial > 0, "no slide left part of a silhouette showing");
  CHECK(band > 0, "no slide reached the second 128-line band");
  CHECK(emptied > 0, "no slide pushed a silhouette out of its buffer");
  printf("  %d frames at the bottom edge: %d slid with ink showing, %d by 128 lines or "
         "more, %d slid out of the buffer\n",
         frames, partial, band, emptied);
}

// ---------------------------------------------------------------------------
// 4. Turning at a fixed range.
//
// The sweeps hold the attitude, so the cache's other half - the axes - is
// only exercised here: each attitude of a full roll, and of a full turn, held
// for two frames, which the second must take from the cache and the first
// must not; and a roll through 180 degrees side-on, where the up axis alone
// tells the two apart. Across the layouts, since the attitude alone moves the box
// across the one- and two-sprite limits.

static void turn(int x, bool roll, const planes_view_t &view, int *frames, int *hits,
                 int *switches, int *edge_max) {
  plane_t p;
  p.init();
  ink_t ink, last_ink;
  planes_frame_t last = {};
  bool have_last = false;
  for (int deg = 0; deg <= 720; deg += 3) {
    attitude_t a = roll ? attitude_t{20, 10, deg} : attitude_t{deg, 15, 40};
    mat3_t axes = axes_for(a);
    vec3_t c = make_vector((int16_t)x, 0, 0);
    char what[96];
    describe(what, sizeof(what), a, x);
    for (int hold = 0; hold < 2; ++hold) {
      planes_frame_t f = step(p, view, c, axes, &ink, what);
      ++*frames;
      if (f.level == kPlaneLevelDot) continue;
      if (hold == 1) {
        CHECK(f.cached, "%s: the same attitude twice, drawn twice", what);
        *hits += f.cached;
      }
      if (have_last && !ink.empty() && !last_ink.empty()) {
        int e = std::max(std::max(iabs(ink.x0 - last_ink.x0), iabs(ink.x1 - last_ink.x1)),
                         std::max(iabs(ink.y0 - last_ink.y0), iabs(ink.y1 - last_ink.y1)));
        int t = transition(last, f);
        if (t != kNone) ++*switches;
        CHECK(e <= kEdgeTurn, "%s: box side moved %d px in a 3 degree turn%s%s", what, e,
              t == kNone ? "" : " at ", t == kNone ? "" : kTransitionNames[t]);
        *edge_max = std::max(*edge_max, e);
      }
      last = f;
      last_ink = ink;
      have_last = true;
    }
  }
}

static void test_turns(void) {
  int frames = 0, hits = 0, switches = 0, edge_max = 0;
  static const int kRanges[] = {1000, 400, 300, 250, 200, 160};
  for (int x : kRanges) {
    for (int roll = 0; roll < 2; ++roll) {
      turn(x, roll != 0, kView, &frames, &hits, &switches, &edge_max);
      turn(x, roll != 0, kViewTwo, &frames, &hits, &switches, &edge_max);
    }
  }
  // Side-on, upright and then rolled through 180 degrees: the nose and the
  // wing - whose screen components are 0 - are where they were, and only the
  // up axis says the fin now points down. Each must be drawn, never taken
  // from the cache.
  for (int x : kRanges) {
    plane_t p;
    p.init();
    ink_t ink;
    for (int n = 0; n < 8; ++n) {
      attitude_t a = {90, 0, (n & 1) ? 180 : 0};
      vec3_t c = make_vector((int16_t)x, 0, 0);
      char what[96];
      describe(what, sizeof(what), a, x);
      planes_frame_t f = step(p, kView, c, axes_for(a), &ink, what);
      CHECK(!f.cached, "%s: rolled over, but from the cache", what);
      ++frames;
    }
  }
  CHECK(switches > 0, "no turn switched the layout");
  printf("  %d frames turning, %d from the cache, %d layout switches, box sides move <= %d px\n",
         frames, hits, switches, edge_max);
}

int main() {
  planes_dot_bitmap(_dot);
  test_sweeps();
  test_wobble();
  test_slide();
  test_turns();
  if (_fails) {
    printf("planes_edge_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("planes_edge_test: passed\n");
  return 0;
}
