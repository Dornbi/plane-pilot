#include "planes.h"

#include <stdint.h>
#include <string.h>

#include "planedef.h"

// A profiling build (-D__PLANES_PROFILE__, with benchmark.h's counters on)
// times each step of planes_render() onto the screen.
#ifdef __PLANES_PROFILE__
#include "benchmark.h"
#define PROFILE_START() bm_start()
#define PROFILE_END(row, label) bm_end((row) * 40 + 20, label)
static const char kProfCentre[] = SCREEN_STR("centre ");
static const char kProfProject[] = SCREEN_STR("project ");
static const char kProfLayout[] = SCREEN_STR("layout ");
static const char kProfKey[] = SCREEN_STR("key ");
static const char kProfFill[] = SCREEN_STR("fill ");
static const char kProfMags[] = SCREEN_STR("mags ");
static const char kProfPairs[] = SCREEN_STR("pairs ");
static const char kProfVerts[] = SCREEN_STR("verts ");
static const char kProfBody[] = SCREEN_STR("body ");
#else
#define PROFILE_START()
#define PROFILE_END(row, label)
#endif

// A line-by-line port of render() in lib/planes.py. The comments name the
// step there; the reasons are in docs/planes.md and not repeated here.

static const uint8_t kCols = 24;
static const uint8_t kRows = 21;

// The model's vertices in screen pixels: wing, tailplane and fin, then the
// fuselage outline, then the end-on disc. Polygon i is vertices
// _poly_start[i] .. _poly_start[i + 1] - 1.
static const uint8_t kMaxVerts = 32;
static const uint8_t kMaxPolys = 5;
static int16_t _vx[kMaxVerts], _vy[kMaxVerts];
static uint8_t _poly_start[kMaxPolys + 1];
static uint8_t _poly_count;
static uint8_t _vert_count;

// Magnitudes times k, and each (axis, magnitude) product's screen offset.
// Index 0 of the offsets is the zero product, so a vertex reference of 0
// reads it.
static int16_t _px[kPlaneMagCount];
static int16_t _ox[kPlanePairCount + 1], _oy[kPlanePairCount + 1];

// One row's extent while a polygon is traced, and the key the frame would
// cache under.
static int16_t _lo[2 * kRows], _hi[2 * kRows];
static uint8_t _key[kPlaneKeyMax];

// The largest extent a layout holds: xs * (24 * cols - 1) and
// ys * (21 * rows - 1), by [expansion - 1][count - 1]. Tables rather than
// products: oscar64's runtime multiply is not linked (tools/check_mul_div.py).
static const uint8_t kFitW[2][2] = {{23, 47}, {46, 94}};
static const uint8_t kFitH[2][2] = {{20, 41}, {40, 82}};

// ---------------------------------------------------------------------------
// Fixed point, as lib/planes.py spells it.

static inline int16_t _abs16(int16_t a) { return a < 0 ? (int16_t)-a : a; }

// a * b / 256 rounded to nearest, the sign applied last.
static int16_t _smul(int16_t a, int16_t b) {
  int16_t p = (int16_t)((vec_fastmul8p8((int16_t)(_abs16(a) << 1), _abs16(b)) + 1) >> 1);
  return ((a < 0) != (b < 0)) ? (int16_t)-p : p;
}

// trunc(a * 256 / b) for |a| < 128: the fuselage normal.
static int16_t _div8p8_small(int16_t a, int16_t b) {
  uint16_t q = (uint16_t)((uint16_t)_abs16(a) << 8) / (uint16_t)_abs16(b);
  return ((a < 0) != (b < 0)) ? (int16_t)-(int16_t)q : (int16_t)q;
}

// trunc(a * 256 / b), exactly: the projected centre. vec_div8p8 would do,
// but it narrows the divisor to 8 bits and would not match the reference.
static int16_t _div8p8_exact(int16_t a, int16_t b) {
  uint32_t q = ((uint32_t)(uint16_t)_abs16(a) << 8) / (uint16_t)_abs16(b);
  return ((a < 0) != (b < 0)) ? (int16_t)-(int16_t)q : (int16_t)q;
}

// |(x, y)| within ~3%: max(hi, 7/8 hi + 1/2 lo).
static int16_t _norm2(int16_t x, int16_t y) {
  x = _abs16(x);
  y = _abs16(y);
  int16_t hi = x > y ? x : y, lo = x > y ? y : x;
  int16_t n = (int16_t)(hi - (hi >> 3) + (lo >> 1));
  return n > hi ? n : hi;
}

static inline int16_t _ref(const int16_t *o, int8_t r) {
  return r >= 0 ? o[r] : (int16_t)-o[-r];
}

// ---------------------------------------------------------------------------
// The rasteriser: sprite blocks, spans, polygons.

static uint8_t *const *_blocks;
static uint8_t _buf_cols, _buf_height, _buf_width;

// OR pixels a..b of row y.
static void _fill_span(int16_t y, int16_t a, int16_t b) {
  if (y < 0 || y >= _buf_height) {
    return;
  }
  if (a > b) {
    int16_t t = a;
    a = b;
    b = t;
  }
  if (a < 0) {
    a = 0;
  }
  if (b > _buf_width - 1) {
    b = _buf_width - 1;
  }
  if (b < a) {
    return;
  }
  // Row y is line y % 21 of block row y / 21; its bytes 0-2 are in the left
  // block of that row, 3-5 in the right.
  uint8_t line = (uint8_t)y, first = 0;
  if (line >= kRows) {
    line -= kRows;
    first = _buf_cols;
  }
  uint8_t off = (uint8_t)((line << 1) + line);
  uint8_t *left = _blocks[first] + off;
  uint8_t *right = _buf_cols == 2 ? _blocks[first + 1] + off : left;
  uint8_t i0 = (uint8_t)(a >> 3), i1 = (uint8_t)(b >> 3);
  for (uint8_t i = i0; i <= i1; ++i) {
    uint8_t m = 0xFF;
    if (i == i0) {
      m &= (uint8_t)(0xFF >> (a & 7));
    }
    if (i == i1) {
      m &= (uint8_t)(0xFF << (7 - (b & 7)));
    }
    if (i < 3) {
      left[i] |= m;
    } else {
      right[i - 3] |= m;
    }
  }
}

// Edge-inclusive convex fill with vertices at pixel centres (lib/planes.py
// fill_poly()).
static void _fill_poly(const int16_t *px, const int16_t *py, uint8_t n) {
  int16_t min_y = py[0], max_y = py[0], min_x = px[0], max_x = px[0];
  for (uint8_t i = 1; i < n; ++i) {
    if (py[i] < min_y) min_y = py[i];
    if (py[i] > max_y) max_y = py[i];
    if (px[i] < min_x) min_x = px[i];
    if (px[i] > max_x) max_x = px[i];
  }
  int16_t y0 = min_y < 0 ? 0 : min_y;
  int16_t y1 = max_y > _buf_height - 1 ? (int16_t)(_buf_height - 1) : max_y;
  if (y0 > y1 || max_x < 0 || min_x >= _buf_width) {
    return;
  }
  for (int16_t y = y0; y <= y1; ++y) {
    _lo[y] = 0x3FFF;
    _hi[y] = -0x3FFF;
  }
  for (uint8_t i = 0; i < n; ++i) {
    uint8_t j = (uint8_t)(i + 1 == n ? 0 : i + 1);
    // End points picked top to bottom, not swapped: oscar64 -O2 loses the
    // second use of a swap temporary (bugs/swap-temp-reuse), and every edge
    // running upward vanished.
    int16_t xa0, ya, xb0, yb;
    if (py[j] < py[i]) {
      xa0 = px[j];
      ya = py[j];
      xb0 = px[i];
      yb = py[i];
    } else {
      xa0 = px[i];
      ya = py[i];
      xb0 = px[j];
      yb = py[j];
    }
    if (ya == yb) {
      if (ya >= y0 && ya <= y1) {
        int16_t a = xa0 < xb0 ? xa0 : xb0, b = xa0 < xb0 ? xb0 : xa0;
        if (a < _lo[ya]) _lo[ya] = a;
        if (b > _hi[ya]) _hi[ya] = b;
      }
      continue;
    }
    int16_t dy = (int16_t)(yb - ya), dx = (int16_t)(xb0 - xa0);
    int16_t slope;
    if (dy == 1) {
      slope = (int16_t)((uint16_t)dx << 8);
    } else if (dy == 2) {
      slope = (int16_t)((uint16_t)dx << 7);
    } else {
      slope = vec_fastmul8p8(dx, (int16_t)kPlaneRecip[dy]);
    }
    int16_t xa = (int16_t)(((uint16_t)xa0 << 8) + 128), xb;
    for (int16_t y = ya; y <= yb; ++y) {
      if (y == yb) {
        xb = (int16_t)(((uint16_t)xb0 << 8) + 128);
      } else if (y == ya) {
        xb = (int16_t)(xa + (slope >> 1));
      } else {
        xb = (int16_t)(xa + slope);
      }
      if (y >= y0 && y <= y1) {
        int16_t a = (int16_t)((xa < xb ? xa : xb) >> 8);
        int16_t b = (int16_t)((xa < xb ? xb : xa) >> 8);
        if (a < _lo[y]) _lo[y] = a;
        if (b > _hi[y]) _hi[y] = b;
      }
      xa = xb;
    }
  }
  for (int16_t y = y0; y <= y1; ++y) {
    if (_lo[y] <= _hi[y]) {
      _fill_span(y, _lo[y], _hi[y]);
    }
  }
}

// ---------------------------------------------------------------------------
// Projection.

static int16_t _cx, _cy;

static void _add_vertex(int16_t x, int16_t y) {
  _vx[_vert_count] = x;
  _vy[_vert_count] = y;
  ++_vert_count;
}

static void _begin_poly(void) { _poly_start[_poly_count] = _vert_count; }

static void _end_poly(void) {
  ++_poly_count;
  _poly_start[_poly_count] = _vert_count;
}

// lib/planes.py project_model(), with the products shared.
static void _project(uint16_t k, const mat3_t *axes) {
  PROFILE_START();
  for (uint8_t m = 0; m < kPlaneMagCount; ++m) {
    _px[m] = (int16_t)((vec_fastmul8p8((int16_t)(kPlaneMags[m] << 1), (int16_t)k) + 1) >> 1);
  }
  PROFILE_END(8, kProfMags);
  PROFILE_START();
  _ox[0] = _oy[0] = 0;
  for (uint8_t p = 0; p < kPlanePairCount; ++p) {
    const vec3_t *axis = kPlanePairAxis[p] == 0   ? &axes->front
                         : kPlanePairAxis[p] == 1 ? &axes->left
                                                  : &axes->up;
    int16_t v = _px[kPlanePairMag[p]];
    _ox[p + 1] = vec_fastmul8p8(axis->y, v);
    _oy[p + 1] = vec_fastmul8p8(axis->z, v);
  }

  PROFILE_END(9, kProfPairs);
  PROFILE_START();
  _vert_count = 0;
  _poly_count = 0;
  for (uint8_t part = 0; part < 3; ++part) {
    _begin_poly();
    for (uint8_t i = kPlanePolyStart[part]; i < kPlanePolyStart[part + 1]; ++i) {
      const int8_t *r = kPlaneVerts[i];
      _add_vertex((int16_t)(_cx - _ref(_ox, r[0]) - _ref(_ox, r[1]) - _ref(_ox, r[2])),
                  (int16_t)(_cy - _ref(_oy, r[0]) - _ref(_oy, r[1]) - _ref(_oy, r[2])));
    }
    _end_poly();
  }

  PROFILE_END(10, kProfVerts);
  PROFILE_START();
  // The fuselage: each station offset along the normal to the projected axis.
  int16_t sx[kPlaneBodyCount], sy[kPlaneBodyCount], sr[kPlaneBodyCount];
  uint8_t widest = 0;
  for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
    sx[i] = (int16_t)(_cx - _ref(_ox, kPlaneBodyFore[i]));
    sy[i] = (int16_t)(_cy - _ref(_oy, kPlaneBodyFore[i]));
    sr[i] = _px[kPlaneBodyRadius[i]];
    if (sr[i] > sr[widest]) {
      widest = i;
    }
  }
  int16_t fx = (int16_t)(sx[0] - sx[kPlaneBodyCount - 1]);
  int16_t fy = (int16_t)(sy[0] - sy[kPlaneBodyCount - 1]);
  int16_t n = _norm2(fx, fy);
  if (n > 0) {
    int16_t ux = _div8p8_small((int16_t)-fy, n), uy = _div8p8_small(fx, n);
    int16_t ox[kPlaneBodyCount], oy[kPlaneBodyCount];
    for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
      ox[i] = _smul(ux, sr[i]);
      oy[i] = _smul(uy, sr[i]);
    }
    _begin_poly();
    for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
      _add_vertex((int16_t)(sx[i] + ox[i]), (int16_t)(sy[i] + oy[i]));
    }
    for (uint8_t i = kPlaneBodyCount; i-- > 0;) {
      _add_vertex((int16_t)(sx[i] - ox[i]), (int16_t)(sy[i] - oy[i]));
    }
    _end_poly();
  }
  // End-on: the cross-section at the widest station.
  if (n < (int16_t)(sr[widest] << 2)) {
    int16_t r = sr[widest] > 1 ? sr[widest] : 1;
    // r * 106 / 256, rounded up: 106 = 64 + 32 + 8 + 2.
    int16_t h = (int16_t)(((r << 6) + (r << 5) + (r << 3) + (r << 1) + 255) >> 8);
    int16_t x = sx[widest], y = sy[widest];
    _begin_poly();
    _add_vertex((int16_t)(x + r), (int16_t)(y + h));
    _add_vertex((int16_t)(x + h), (int16_t)(y + r));
    _add_vertex((int16_t)(x - h), (int16_t)(y + r));
    _add_vertex((int16_t)(x - r), (int16_t)(y + h));
    _add_vertex((int16_t)(x - r), (int16_t)(y - h));
    _add_vertex((int16_t)(x - h), (int16_t)(y - r));
    _add_vertex((int16_t)(x + h), (int16_t)(y - r));
    _add_vertex((int16_t)(x + r), (int16_t)(y - h));
    _end_poly();
  }
  PROFILE_END(11, kProfBody);
}

// ---------------------------------------------------------------------------
// Level and layout.

// 0 at or below the first limit, 1 at or below the second, else 2.
static uint8_t _ladder(uint8_t d, uint8_t lim0, uint8_t lim1) {
  return d <= lim0 ? 0 : (d <= lim1 ? 1 : 2);
}

static uint8_t _pick_level(uint8_t prev, uint8_t d) {
  uint8_t up = _ladder(d, kPlaneDDot, kPlaneD1x);
  uint8_t down = _ladder(d, kPlaneDDotDown, kPlaneD1xDown);
  uint8_t level = prev > up ? prev : up;
  return level < down ? level : down;
}

void planes_state_init(planes_state_t *state) {
  state->level = kPlaneLevelDot;
  state->ys = state->cols = state->rows = 1;
  state->key_valid = false;
  state->key_len = 0;
}

void planes_dot_bitmap(uint8_t *block) {
  memset(block, 0, kPlaneBlockBytes);
  // Columns 12 and 13 are bits 3 and 2 of the row's middle byte.
  block[3 * kPlaneDotY + 1] = 0x0C;
  block[3 * (kPlaneDotY + 1) + 1] = 0x0C;
}

void planes_render(planes_state_t *state, const planes_view_t *view,
                   const vec3_t *c, const mat3_t *axes, uint8_t *const *back,
                   planes_frame_t *frame) {
  frame->hidden = kPlaneShown;
  frame->cached = false;
  frame->slid = 0;
  if (c->x <= 64) {
    frame->hidden = kPlaneBehindCamera;
    return;
  }
  if (c->x > 16000) {
    frame->hidden = kPlaneOutOfRange;
    return;
  }

  // 5. centre, 6. perspective scale, 6b. the size cap
  PROFILE_START();
  _cx = (int16_t)(view->cx0 - _div8p8_exact(c->y, c->x));
  _cy = (int16_t)(view->cy0 - _div8p8_exact(c->z, c->x));
  uint16_t k = (uint16_t)32768u / (uint16_t)c->x;
  frame->clamped = k > kPlaneKMax;
  if (frame->clamped) {
    k = kPlaneKMax;
  }
  frame->k = k;
  PROFILE_END(2, kProfCentre);

  // 7. pixel size, from distance alone
  // R * k / 128, as one fast multiply: trunc(2R * k / 256).
  uint8_t d = (uint8_t)vec_fastmul8p8((int16_t)(kPlaneMaxRadius << 1), (int16_t)k);
  frame->d = d;
  uint8_t level = _pick_level(state->level, d);
  frame->level = level;

  if (level == kPlaneLevelDot) {
    state->level = kPlaneLevelDot;
    state->ys = state->cols = state->rows = 1;
    state->key_valid = false;
    frame->xs = frame->ys = frame->cols = frame->rows = 1;
    frame->x = (int16_t)(_cx - kPlaneDotX);
    frame->y = (int16_t)(_cy - kPlaneDotY);
    if (frame->y > view->cut1) {
      frame->hidden = kPlaneBelowCut;
    }
    return;
  }

  // 8. body axes are the caller's, 9. the polygons in screen pixels
  _project(k, axes);
  PROFILE_START();
  int16_t minx = _vx[0], maxx = _vx[0], miny = _vy[0], maxy = _vy[0];
  for (uint8_t i = 1; i < _vert_count; ++i) {
    if (_vx[i] < minx) minx = _vx[i];
    if (_vx[i] > maxx) maxx = _vx[i];
    if (_vy[i] < miny) miny = _vy[i];
    if (_vy[i] > maxy) maxy = _vy[i];
  }
  int16_t bw = (int16_t)(maxx - minx), bh = (int16_t)(maxy - miny);

  // 10. Y-expansion and layout
  uint8_t xs = level == kPlaneLevel1x ? 1 : 2;
  uint8_t ys = 1;
  if (level == kPlaneLevelX &&
      (bh > kPlaneYLimit || (state->level == level && state->ys == 2 && bh > kPlaneYHold))) {
    ys = 2;
  }
  bool same = state->level == level && state->ys == ys;
  uint8_t cols = bw <= kFitW[xs - 1][0] ? 1 : 2;
  if (same && state->cols == 2 && cols == 1 && bw > kPlaneColsHold[xs]) {
    cols = 2;
  }
  uint8_t rows = bh <= kFitH[ys - 1][0] ? 1 : 2;
  if (same && state->rows == 2 && rows == 1 && bh > kPlaneRowsHold[ys]) {
    rows = 2;
  }
  state->level = level;
  state->ys = ys;
  state->cols = cols;
  state->rows = rows;
  frame->xs = xs;
  frame->ys = ys;
  frame->cols = cols;
  frame->rows = rows;

  // the centring anchor: the bounding box's centre, or the wing hub
  int16_t wpx = (int16_t)(kCols << ((cols - 1) + (xs - 1)));
  int16_t hpx = (int16_t)(kRows << ((rows - 1) + (ys - 1)));
  bool fits = bw <= kFitW[xs - 1][cols - 1] && bh <= kFitH[ys - 1][rows - 1];
  int16_t ax, ay;
  if (fits) {
    ax = (int16_t)((minx + maxx + 1) >> 1);
    ay = (int16_t)((miny + maxy + 1) >> 1);
  } else {
    int16_t hub = _px[kPlaneHubMag];
    int16_t hx = vec_fastmul8p8(axes->front.y, hub), hy = vec_fastmul8p8(axes->front.z, hub);
    if (kPlaneHubSign < 0) {
      hx = (int16_t)-hx;
      hy = (int16_t)-hy;
    }
    ax = (int16_t)(_cx - hx);
    ay = (int16_t)(_cy - hy);
  }
  int16_t ox = (int16_t)(ax - (wpx >> 1)), oy = (int16_t)(ay - (hpx >> 1));

  // 11. slide, don't reject
  int16_t cut = ys == 2 ? view->cut2 : view->cut1;
  int16_t over = (int16_t)(oy + (rows == 2 ? (int16_t)(kRows << (ys - 1)) : 0) - cut);
  if (over > 0) {
    oy = (int16_t)(oy - over);
    frame->slid = (uint8_t)over;
  }
  frame->x = ox;
  frame->y = oy;

  PROFILE_END(4, kProfLayout);

  // local coordinates, flooring onto expanded pixels, and the cache key
  PROFILE_START();
  uint8_t xshift = (uint8_t)(xs - 1), yshift = (uint8_t)(ys - 1);
  uint8_t *key = _key;
  *key++ = level;
  *key++ = ys;
  *key++ = cols;
  *key++ = rows;
  for (uint8_t i = 0; i < _vert_count; ++i) {
    _vx[i] = (int16_t)((_vx[i] - ox) >> xshift);
    _vy[i] = (int16_t)((_vy[i] - oy) >> yshift);
    *key++ = (uint8_t)_vx[i];
    *key++ = (uint8_t)(_vx[i] >> 8);
    *key++ = (uint8_t)_vy[i];
    *key++ = (uint8_t)(_vy[i] >> 8);
  }
  uint8_t key_len = (uint8_t)(key - _key);

  // 12. cache
  PROFILE_END(5, kProfKey);
  if (state->key_valid && state->key_len == key_len && memcmp(state->key, _key, key_len) == 0) {
    frame->cached = true;
    return;
  }
  memcpy(state->key, _key, key_len);
  state->key_len = key_len;
  state->key_valid = true;

  // 13. fill into the back buffer
  PROFILE_START();
  _blocks = back;
  _buf_cols = cols;
  _buf_width = (uint8_t)(kCols << (cols - 1));
  _buf_height = (uint8_t)(kRows << (rows - 1));
  uint8_t blocks = (uint8_t)(rows << (cols - 1));
  for (uint8_t b = 0; b < blocks; ++b) {
    memset(back[b], 0, kPlaneBlockBytes);
  }
  for (uint8_t p = 0; p < _poly_count; ++p) {
    uint8_t s = _poly_start[p];
    _fill_poly(_vx + s, _vy + s, (uint8_t)(_poly_start[p + 1] - s));
  }
  PROFILE_END(6, kProfFill);
}
