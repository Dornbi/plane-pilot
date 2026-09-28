#include "planes.h"

#include <stdint.h>
#include <string.h>

#include "planedef.h"
#include "planes_asm.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

// A profiling build (-D__PLANES_PROFILE__, with benchmark.h's counters on)
// times each step of planes_render() onto the screen.
#ifdef __PLANES_PROFILE__
#include "benchmark.h"
#define PROFILE_START() bm_start()
#define PROFILE_END(row, label) bm_end((row) * 40 + 20, label)
static const char kProfCentre[] = SCREEN_STR("centre ");
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

// A port of render() in lib/planes.py. The comments name the step there; the
// reasons are in docs/planes.md and not repeated here. Three things are the
// port's own (docs/planes.md section 11):
//
// - It works in bytes. The size cap keeps every vertex within about 41 px of
//   the centre, so the products are signed bytes, the vertices relative to
//   the centre unsigned ones offset by 128, and once placed in the buffer
//   plain unsigned ones.
// - It relies on the cap for the silhouette fitting its layout -- which
//   lib/planes.py only guards against, and tests/test_planes.py
//   (TestSizeClamp) shows never happens -- so it anchors on the box alone,
//   and fills without clipping to the buffer's sides.
// - A frame with the last one's scale and axes skips the projection.
//
// On the C64 the axis products and the fill are assembly, in planes_asm.cc;
// the C for them here is the host's (planes_asm.h).

static const uint8_t kCols = 24;
static const uint8_t kRows = 21;
static const uint8_t kMaxPolys = 5;

// The model's vertices relative to the centre, in screen pixels and offset by
// 128, so that they are unsigned bytes and compare as such: wing, tailplane
// and fin, then the fuselage outline, then the end-on disc. Polygon i is
// vertices _poly_start[i] .. _poly_start[i + 1] - 1.
static const uint8_t kBias = 128;
static uint8_t _vx[kPlaneVertMax], _vy[kPlaneVertMax];
static uint8_t _poly_start[kMaxPolys + 1];
static uint8_t _poly_count;
static uint8_t _vert_count;

// Magnitudes times k, and each pair's; _px_k is the k they hold, 0 for none.
// They depend on nothing else, so they are kept while the distance holds.
static uint8_t _px[kPlaneMagCount];
static uint16_t _px_k;

// planes_asm.h's: the pairs' magnitudes times k and their products, and the
// vertices in buffer coordinates.
uint8_t _planes_pv[16];
int8_t _planes_tx[32], _planes_ty[32];
uint8_t _planes_lx[kPlaneVertMax], _planes_ly[kPlaneVertMax];

// The largest extent a layout holds: xs * (24 * cols - 1) and
// ys * (21 * rows - 1), by [expansion - 1][count - 1]. Tables rather than
// products: oscar64's runtime multiply is not linked (tools/check_mul_div.py).
static const uint8_t kFitW[2][2] = {{23, 47}, {46, 94}};
static const uint8_t kFitH[2][2] = {{20, 41}, {40, 82}};

// ---------------------------------------------------------------------------
// Fixed point, as lib/planes.py spells it, on vec.h's byte multiply and exact
// fraction.

static inline int16_t _abs16(int16_t a) { return a < 0 ? (int16_t)-a : a; }

// trunc(a * 256 / b) for 0 < b and |a| <= b: the centre, and the fuselage's
// unit normal. vec_fracn's exact fraction to eight bits; a whole one
// saturates there, so it is the one case taken aside.
static int16_t _div8p8(int16_t a, int16_t b) {
  int16_t m = _abs16(a);
  int16_t q = m == b ? 256 : (int16_t)vec_fracn(m, b, 8);
  return a < 0 ? (int16_t)-q : q;
}

// lib/planes.py smul(u, r) for a unit component u (|u| <= 256) and 0 <= r < 256:
// u * r / 256 rounded, as the reference forms it from trunc(2|u| r / 256).
static int8_t _smul(int16_t u, uint8_t r) {
  uint16_t m = (uint16_t)_abs16(u);
  uint16_t p = m >= 256 ? (uint16_t)(r << 8) : vec_mul8x8((uint8_t)m, r);
  int8_t q = (int8_t)(((p >> 7) + 1) >> 1);
  return u < 0 ? (int8_t)-q : q;
}

// |(x, y)| within ~3%: max(hi, 7/8 hi + 1/2 lo).
static uint8_t _norm2(int8_t x, int8_t y) {
  uint8_t ax = (uint8_t)(x < 0 ? -x : x), ay = (uint8_t)(y < 0 ? -y : y);
  uint8_t hi = ax > ay ? ax : ay, lo = ax > ay ? ay : ax;
  uint8_t n = (uint8_t)(hi - (hi >> 3) + (lo >> 1));
  return n > hi ? n : hi;
}

#ifndef __OSCAR64__
// ---------------------------------------------------------------------------
// planes_asm.h in C, for the host: what planes_asm.cc does on the C64, step
// for step.

void _planes_axis_products(const vec3_t *a, uint8_t first, uint8_t end) {
  for (uint8_t p = first; p < end; ++p) {
    int8_t ox = (int8_t)vec_fastmul8p8(a->y, _planes_pv[p]);
    int8_t oy = (int8_t)vec_fastmul8p8(a->z, _planes_pv[p]);
    _planes_tx[p] = (int8_t)-ox;
    _planes_tx[16 + p] = ox;
    _planes_ty[p] = (int8_t)-oy;
    _planes_ty[16 + p] = oy;
  }
}

static uint8_t *_blk;               // the back buffer's first block
static uint8_t _buf_h, _buf_w1;     // height, and width - 1
static uint8_t _bottom;             // offset of the lower row of blocks
static uint8_t _lo[2 * kRows], _hi[2 * kRows];

void _planes_fill_begin(uint8_t *back, uint8_t cols, uint8_t height) {
  _blk = back;
  _buf_h = height;
  _buf_w1 = (uint8_t)((kCols << (cols - 1)) - 1);
  _bottom = (uint8_t)(cols << 6);
}

// Widen row y's extent to take pixels a..b, in either order.
static void _put(uint8_t y, uint8_t a, uint8_t b) {
  if (y >= _buf_h) {
    return;
  }
  uint8_t l = a < b ? a : b, h = a < b ? b : a;
  if (l < _lo[y]) _lo[y] = l;
  if (h > _hi[y]) _hi[y] = h;
}

// OR pixels a..b of row y; a <= b <= width - 1.
static void _fill_span(uint8_t y, uint8_t a, uint8_t b) {
  uint8_t base = y < kRows ? (uint8_t)(y * 3) : (uint8_t)(_bottom + (y - kRows) * 3);
  uint8_t ja = a >> 3, jb = b >> 3;
  uint8_t lm = kPlaneLeftMask[a & 7], rm = kPlaneRightMask[b & 7];
  if (ja == jb) {
    _blk[base + kPlaneByteOffset[ja]] |= (uint8_t)(lm & rm);
    return;
  }
  _blk[base + kPlaneByteOffset[ja]] |= lm;
  for (uint8_t j = (uint8_t)(ja + 1); j < jb; ++j) {
    _blk[base + kPlaneByteOffset[j]] = 0xFF;
  }
  _blk[base + kPlaneByteOffset[jb]] |= rm;
}

void _planes_fill_poly(uint8_t s, uint8_t n) {
  uint8_t min_y = 255, max_y = 0;
  for (uint8_t i = 0; i < n; ++i) {
    uint8_t y = _planes_ly[s + i];
    if (y < min_y) min_y = y;
    if (y > max_y) max_y = y;
  }
  if (min_y >= _buf_h) {
    return;
  }
  uint8_t y1 = max_y < _buf_h ? max_y : (uint8_t)(_buf_h - 1);
  for (uint8_t y = min_y; y <= y1; ++y) {
    _lo[y] = 255;
    _hi[y] = 0;
  }
  for (uint8_t i = 0; i < n; ++i) {
    uint8_t a = (uint8_t)(s + i), b = (uint8_t)(i + 1 == n ? s : s + i + 1);
    uint8_t top = _planes_ly[b] < _planes_ly[a] ? b : a, bot = _planes_ly[b] < _planes_ly[a] ? a : b;
    uint8_t xt = _planes_lx[top], yt = _planes_ly[top], xe = _planes_lx[bot], ye = _planes_ly[bot];
    if (yt == ye) {
      _put(yt, xt, xe);
      continue;
    }
    uint8_t dy = (uint8_t)(ye - yt);
    int16_t dx = (int16_t)xe - (int16_t)xt;
    int16_t slope = (int16_t)((uint16_t)dx << 7), half;
    if (dy == 1) {
      half = slope;
    } else {
      if (dy > 2) {
        slope = vec_fastmul8p8(dx, (int16_t)kPlaneRecip[dy]);
      }
      half = (int16_t)(slope >> 1);
    }
    uint16_t xb = (uint16_t)((xt << 8) + 128 + half);
    uint8_t y = yt;
    _put(y, xt, (uint8_t)(xb >> 8));
    for (uint8_t r = 1; r < dy; ++r) {
      uint8_t prev = (uint8_t)(xb >> 8);
      xb = (uint16_t)(xb + slope);
      _put(++y, prev, (uint8_t)(xb >> 8));
    }
    _put(ye, (uint8_t)(xb >> 8), xe);
  }
  for (uint8_t y = min_y; y <= y1; ++y) {
    uint8_t a = _lo[y], b = _hi[y];
    if (a <= b) {
      _fill_span(y, a < _buf_w1 ? a : _buf_w1, b < _buf_w1 ? b : _buf_w1);
    }
  }
}
#endif

// ---------------------------------------------------------------------------
// Projection.

static void _add_vertex(uint8_t x, uint8_t y) {
  _vx[_vert_count] = x;
  _vy[_vert_count] = y;
  ++_vert_count;
}

static void _end_poly(void) {
  ++_poly_count;
  _poly_start[_poly_count] = _vert_count;
}

// lib/planes.py project_model(), with the products shared, relative to the
// centre.
static void _project(uint16_t k, const mat3_t *axes) {
  PROFILE_START();
  if (k != _px_k) {
    for (uint8_t m = 0; m < kPlaneMagCount; ++m) {
      // magnitude * k / 256 rounded, as trunc(2 * magnitude * k / 256)
      _px[m] = (uint8_t)(((vec_mul8x8((uint8_t)(kPlaneMags[m] << 1), (uint8_t)k) >> 8) + 1) >> 1);
    }
    for (uint8_t p = 1; p < 16; ++p) {
      _planes_pv[p] = _px[kPlanePairMag[p]];
    }
    _px_k = k;
  }
  PROFILE_END(8, kProfMags);
  PROFILE_START();
  _planes_axis_products(&axes->front, kPlaneAxisPairs[0], kPlaneAxisPairs[1]);
  _planes_axis_products(&axes->left, kPlaneAxisPairs[1], kPlaneAxisPairs[2]);
  _planes_axis_products(&axes->up, kPlaneAxisPairs[2], kPlaneAxisPairs[3]);
  PROFILE_END(9, kProfPairs);
  PROFILE_START();
  for (uint8_t i = 0; i < kPlaneVertCount; ++i) {
    uint8_t f = kPlaneVertFore[i], l = kPlaneVertLeft[i], u = kPlaneVertUp[i];
    _vx[i] = (uint8_t)(kBias + _planes_tx[f] + _planes_tx[l] + _planes_tx[u]);
    _vy[i] = (uint8_t)(kBias + _planes_ty[f] + _planes_ty[l] + _planes_ty[u]);
  }
  for (uint8_t p = 0; p < 4; ++p) {
    _poly_start[p] = kPlanePolyStart[p];
  }
  _poly_count = 3;
  _vert_count = kPlaneVertCount;
  PROFILE_END(10, kProfVerts);
  PROFILE_START();
  // The fuselage: each station offset along the normal to the projected axis.
  uint8_t sx[kPlaneBodyCount], sy[kPlaneBodyCount];
  uint8_t sr[kPlaneBodyCount];
  uint8_t widest = 0;
  for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
    sx[i] = (uint8_t)(kBias + _planes_tx[kPlaneBodyFore[i]]);
    sy[i] = (uint8_t)(kBias + _planes_ty[kPlaneBodyFore[i]]);
    sr[i] = _px[kPlaneBodyRadius[i]];
    if (sr[i] > sr[widest]) {
      widest = i;
    }
  }
  int8_t fx = (int8_t)(sx[0] - sx[kPlaneBodyCount - 1]);
  int8_t fy = (int8_t)(sy[0] - sy[kPlaneBodyCount - 1]);
  uint8_t n = _norm2(fx, fy);
  if (n > 0) {
    int16_t ux = _div8p8((int16_t)-fy, n), uy = _div8p8(fx, n);
    int8_t ox[kPlaneBodyCount], oy[kPlaneBodyCount];
    for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
      ox[i] = _smul(ux, sr[i]);
      oy[i] = _smul(uy, sr[i]);
    }
    for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
      _add_vertex((uint8_t)(sx[i] + ox[i]), (uint8_t)(sy[i] + oy[i]));
    }
    for (uint8_t i = kPlaneBodyCount; i-- > 0;) {
      _add_vertex((uint8_t)(sx[i] - ox[i]), (uint8_t)(sy[i] - oy[i]));
    }
    _end_poly();
  }
  // End-on: the cross-section at the widest station, an octagon of radius r
  // whose short offset is r * 106 / 256 rounded up. Its x offsets go round
  // as r, h, -h, -r, -r, -h, h, r, and each y is the x two corners on.
  //
  // A table and a loop rather than eight calls spelt out: those were smaller
  // only in the source, and oscar64 -O2 lost r there -- it doubled it in place
  // for the product and then used what was left as x + r.
  if (n < (uint16_t)(sr[widest] << 2)) {
    int8_t r = (int8_t)(sr[widest] > 1 ? sr[widest] : 1);
    int8_t h = (int8_t)((vec_mul8x8((uint8_t)r, 106) + 255) >> 8);
    int8_t off[8];
    off[0] = off[7] = r;
    off[1] = off[6] = h;
    off[2] = off[5] = (int8_t)-h;
    off[3] = off[4] = (int8_t)-r;
    uint8_t x = sx[widest], y = sy[widest];
    for (uint8_t i = 0; i < 8; ++i) {
      _add_vertex((uint8_t)(x + off[i]), (uint8_t)(y + off[(i + 6) & 7]));
    }
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
  state->key_count = 0;
}

// Whether the axes' screen components are the ones in `last`.
static bool _same_axes(const int16_t *last, const mat3_t *axes) {
  return last[0] == axes->front.y && last[1] == axes->front.z && last[2] == axes->left.y &&
         last[3] == axes->left.z && last[4] == axes->up.y && last[5] == axes->up.z;
}

// 11. slide, don't reject: how far a buffer whose top is on line oy has to
// move up for its last sprite row to start on or above the DMA cut.
static int16_t _slide(const planes_view_t *view, int16_t oy, uint8_t ys, uint8_t rows) {
  int16_t cut = ys == 2 ? view->cut2 : view->cut1;
  int16_t over = (int16_t)(oy + (rows == 2 ? (int16_t)(kRows << (ys - 1)) : 0) - cut);
  return over > 0 ? over : 0;
}

void planes_dot_bitmap(uint8_t *block) {
  memset(block, 0, kPlaneBlockBytes);
  // Columns 12 and 13 are bits 3 and 2 of the row's middle byte.
  block[3 * kPlaneDotY + 1] = 0x0C;
  block[3 * (kPlaneDotY + 1) + 1] = 0x0C;
}

void planes_render(planes_state_t *state, const planes_view_t *view,
                   const vec3_t *c, const mat3_t *axes, uint8_t *back,
                   planes_frame_t *frame) {
  frame->hidden = kPlaneShown;
  frame->cached = false;
  frame->slid = 0;
  if (c->x <= 64) {
    frame->hidden = kPlaneBehindCamera;
    return;
  }
  if (c->x > 16000 || _abs16(c->y) >= c->x || _abs16(c->z) >= c->x) {
    frame->hidden = kPlaneOutOfRange;
    return;
  }

  // 5. centre, 6. perspective scale, 6b. the size cap
  PROFILE_START();
  int16_t cx = (int16_t)(view->cx0 - _div8p8(c->y, c->x));
  int16_t cy = (int16_t)(view->cy0 - _div8p8(c->z, c->x));
  // 32768 / x: the fraction 64 / x to nine bits, exact as x > 64.
  uint16_t k = vec_fracn(64, c->x, 9);
  frame->clamped = k > kPlaneKMax;
  if (frame->clamped) {
    k = kPlaneKMax;
  }
  frame->k = k;
  PROFILE_END(2, kProfCentre);

  // 7. pixel size, from distance alone: R * k / 128, as trunc(2R * k / 256)
  uint8_t d = (uint8_t)(vec_mul8x8(kPlaneMaxRadius << 1, (uint8_t)k) >> 8);
  frame->d = d;
  uint8_t level = _pick_level(state->level, d);
  frame->level = level;

  if (level == kPlaneLevelDot) {
    state->level = kPlaneLevelDot;
    state->ys = state->cols = state->rows = 1;
    state->key_valid = false;
    frame->xs = frame->ys = frame->cols = frame->rows = 1;
    frame->x = (int16_t)(cx - kPlaneDotX);
    frame->y = (int16_t)(cy - kPlaneDotY);
    if (frame->y > view->cut1) {
      frame->hidden = kPlaneBelowCut;
    }
    return;
  }

  // The scale and axes the last frame projected: the same silhouette, only
  // moved with the centre, and the same layout -- the hysteresis settles in
  // one frame. Only the slide, which depends on where the centre is, can
  // still change what is in the buffer.
  if (state->key_valid && state->k == k && _same_axes(state->axes, axes)) {
    int16_t ox = (int16_t)(cx + state->ox), oy = (int16_t)(cy + state->oy);
    int16_t slid = _slide(view, oy, state->ys, state->rows);
    if (slid == state->slid) {
      frame->xs = level == kPlaneLevel1x ? 1 : 2;
      frame->ys = state->ys;
      frame->cols = state->cols;
      frame->rows = state->rows;
      frame->x = ox;
      frame->y = (int16_t)(oy - slid);
      frame->slid = (uint8_t)slid;
      frame->cached = true;
      return;
    }
  }

  // 8. body axes are the caller's, 9. the polygons in screen pixels
  _project(k, axes);
  PROFILE_START();
  uint8_t minx = _vx[0], maxx = _vx[0], miny = _vy[0], maxy = _vy[0];
  for (uint8_t i = 1; i < _vert_count; ++i) {
    if (_vx[i] < minx) minx = _vx[i];
    if (_vx[i] > maxx) maxx = _vx[i];
    if (_vy[i] < miny) miny = _vy[i];
    if (_vy[i] > maxy) maxy = _vy[i];
  }
  uint8_t bw = (uint8_t)(maxx - minx), bh = (uint8_t)(maxy - miny);

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
  // The latches are the cached vertices' layout until they change here.
  bool cached = state->key_valid && same && state->cols == cols && state->rows == rows &&
                state->key_count == _vert_count;
  state->level = level;
  state->ys = ys;
  state->cols = cols;
  state->rows = rows;
  frame->xs = xs;
  frame->ys = ys;
  frame->cols = cols;
  frame->rows = rows;

  // the centring anchor: the bounding box's centre. The buffer's top left,
  // relative to the centre, is ox and oy; bx and by are the same with the
  // vertices' offset, which is what a vertex is measured from.
  uint8_t wpx = (uint8_t)(kCols << ((cols - 1) + (xs - 1)));
  uint8_t hpx = (uint8_t)(kRows << ((rows - 1) + (ys - 1)));
  uint8_t bx = (uint8_t)(((minx + maxx + 1) >> 1) - (wpx >> 1));
  uint8_t by = (uint8_t)(((miny + maxy + 1) >> 1) - (hpx >> 1));
  int16_t ox = (int16_t)bx - kBias, oy = (int16_t)by - kBias;

  // 11. slide, don't reject
  int16_t top = (int16_t)(cy + oy);
  int16_t slid = _slide(view, top, ys, rows);

  // what the next frame needs to tell whether it can skip all of this
  state->k = k;
  int16_t *last = state->axes;
  last[0] = axes->front.y;
  last[1] = axes->front.z;
  last[2] = axes->left.y;
  last[3] = axes->left.z;
  last[4] = axes->up.y;
  last[5] = axes->up.z;
  state->ox = ox;
  state->oy = oy;
  state->slid = slid;

  frame->slid = (uint8_t)slid;
  frame->x = (int16_t)(cx + ox);
  frame->y = (int16_t)(top - slid);

  PROFILE_END(4, kProfLayout);

  // local coordinates, flooring onto expanded pixels; 12. the vertex cache
  // hits if they are all the cached ones. Compared and stored in one pass: a
  // vertex that matches is already stored. y is (u + slid) >> (ys - 1) with u
  // the pixel's line in the unslid buffer, modulo 256 (planes.h).
  PROFILE_START();
  uint8_t band = (uint8_t)((uint16_t)slid >> 7);
  if (state->key_band != band) {
    cached = false;
  }
  uint16_t s9 = (uint16_t)slid & 0x1FF;
  uint8_t *kx = state->key_x, *ky = state->key_y;
  for (uint8_t i = 0; i < _vert_count; ++i) {
    uint8_t x = (uint8_t)(_vx[i] - bx);
    uint16_t u = (uint16_t)((uint8_t)(_vy[i] - by) + s9);
    uint8_t y;
    if (xs == 2) {
      x >>= 1;
    }
    if (ys == 2) {
      y = (uint8_t)(u >> 1);
    } else {
      y = (uint8_t)u;
    }
    _planes_lx[i] = x;
    _planes_ly[i] = y;
    if (x != kx[i] || y != ky[i]) {
      kx[i] = x;
      ky[i] = y;
      cached = false;
    }
  }
  state->key_count = _vert_count;
  state->key_band = band;
  state->key_valid = true;
  PROFILE_END(5, kProfKey);
  if (cached) {
    frame->cached = true;
    return;
  }

  // 13. fill into the back buffer. A slide of a whole buffer or more leaves
  // it empty, and y no longer fits a byte.
  PROFILE_START();
  uint8_t blocks = (uint8_t)(rows << (cols - 1));
  memset(back, 0, (uint16_t)blocks << 6);
  uint8_t height = (uint8_t)(kRows << (rows - 1));
  if ((uint16_t)slid >> (ys - 1) < height) {
    _planes_fill_begin(back, cols, height);
    for (uint8_t p = 0; p < _poly_count; ++p) {
      uint8_t first = _poly_start[p];
      _planes_fill_poly(first, (uint8_t)(_poly_start[p + 1] - first));
    }
  }
  PROFILE_END(6, kProfFill);
}

#pragma optimize(pop)
