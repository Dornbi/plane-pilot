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
// fpilot keeps the per-call arrays in the screen's dead rows, over the
// terrain's own scratch (mem.h tscratch): written before they are read, every
// call, and dead between calls.
#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
uint8_t _planes_vx[kPlaneVertMax], _planes_vy[kPlaneVertMax];
// The fuselage's stations: centre and radius (planes_asm.h).
uint8_t _planes_sx[kPlaneBodyCount], _planes_sy[kPlaneBodyCount];
uint8_t _planes_sr[kPlaneBodyCount];
static uint8_t _poly_start[kMaxPolys + 1];
static uint8_t _poly_count;
static uint8_t _vert_count;
// planes_asm.h's: the pairs' products.
int8_t _planes_tx[32], _planes_ty[32];
#ifdef __FPILOT__
#pragma bss(bss)
#endif

// Magnitudes times k (planes_asm.h), and _px_k the k they hold, 0 for none.
// They depend on nothing else, so they are kept while the distance holds;
// so are the pairs' magnitudes times k.
uint8_t _planes_px[kPlaneMagCount];
static uint16_t _px_k;
uint8_t _planes_pv[16];

// planes_asm.h's: the vertices in buffer coordinates. Kept with the state
// they belong to, which planes_fill() skips copying from its key.
uint8_t _planes_lx[kPlaneVertMax], _planes_ly[kPlaneVertMax];

// The largest extent a layout holds: xs * (24 * cols - 1) and
// ys * (21 * rows - 1), by [expansion - 1][count - 1]. Tables rather than
// products: oscar64's runtime multiply is not linked (tools/check_mul_div.py).
static const uint8_t kFitW[2][2] = {{23, 47}, {46, 94}};
static const uint8_t kFitH[2][2] = {{20, 41}, {40, 82}};

// ---------------------------------------------------------------------------
// Fixed point, as lib/planes.py spells it, on vec.h's byte multiply, exact
// fraction and exact division. lib/planes.py's fdiv is vec_div8p8's
// trunc(a * 256 / b): the centre, and the fuselage's unit normal.

static inline int16_t _abs16(int16_t a) { return a < 0 ? (int16_t)-a : a; }

#ifndef __OSCAR64__
// lib/planes.py smul(u, r) for a unit component u (|u| <= 256) and 0 <= r < 256:
// u * r / 256 rounded, as the reference forms it from trunc(2|u| r / 256).
// The host's; planes_asm.cc has it inline in the fuselage.
static int8_t _smul(int16_t u, uint8_t r) {
  uint16_t m = (uint16_t)_abs16(u);
  uint16_t p = m >= 256 ? (uint16_t)(r << 8) : vec_mul8x8((uint8_t)m, r);
  int8_t q = (int8_t)(((p >> 7) + 1) >> 1);
  return u < 0 ? (int8_t)-q : q;
}
#endif

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

void _planes_tube(int16_t ux, int16_t uy, uint8_t at) {
  int8_t ox[kPlaneBodyCount], oy[kPlaneBodyCount];
  for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
    ox[i] = _smul(ux, _planes_sr[i]);
    oy[i] = _smul(uy, _planes_sr[i]);
  }
  for (uint8_t i = 0; i < kPlaneBodyCount; ++i, ++at) {
    _planes_vx[at] = (uint8_t)(_planes_sx[i] + ox[i]);
    _planes_vy[at] = (uint8_t)(_planes_sy[i] + oy[i]);
  }
  for (uint8_t i = kPlaneBodyCount; i-- > 0; ++at) {
    _planes_vx[at] = (uint8_t)(_planes_sx[i] - ox[i]);
    _planes_vy[at] = (uint8_t)(_planes_sy[i] - oy[i]);
  }
}

void _planes_mags(uint8_t k) {
  for (uint8_t m = 0; m < kPlaneMagCount; ++m) {
    // magnitude * k / 256 rounded, as trunc(2 * magnitude * k / 256)
    _planes_px[m] = (uint8_t)(((vec_mul8x8((uint8_t)(kPlaneMags[m] << 1), k) >> 8) + 1) >> 1);
  }
  for (uint8_t p = 1; p < 16; ++p) {
    _planes_pv[p] = _planes_px[kPlanePairMag[p]];
  }
}

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
  _planes_vx[_vert_count] = x;
  _planes_vy[_vert_count] = y;
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
    _planes_mags((uint8_t)k);
    _px_k = k;
  }
  PROFILE_END(8, kProfMags);
  PROFILE_START();
  // Term 0 is the zero every vertex without that axis adds; nothing else
  // writes it, and in fpilot the array is scratch (mem.h).
  _planes_tx[0] = 0;
  _planes_ty[0] = 0;
  _planes_axis_products(&axes->front, kPlaneAxisPairs[0], kPlaneAxisPairs[1]);
  _planes_axis_products(&axes->left, kPlaneAxisPairs[1], kPlaneAxisPairs[2]);
  _planes_axis_products(&axes->up, kPlaneAxisPairs[2], kPlaneAxisPairs[3]);
  PROFILE_END(9, kProfPairs);
  PROFILE_START();
  for (uint8_t i = 0; i < kPlaneVertCount; ++i) {
    uint8_t f = kPlaneVertFore[i], l = kPlaneVertLeft[i], u = kPlaneVertUp[i];
    _planes_vx[i] = (uint8_t)(kBias + _planes_tx[f] + _planes_tx[l] + _planes_tx[u]);
    _planes_vy[i] = (uint8_t)(kBias + _planes_ty[f] + _planes_ty[l] + _planes_ty[u]);
  }
  for (uint8_t p = 0; p < 4; ++p) {
    _poly_start[p] = kPlanePolyStart[p];
  }
  _poly_count = 3;
  _vert_count = kPlaneVertCount;
  PROFILE_END(10, kProfVerts);
  PROFILE_START();
  // The fuselage: each station offset along the normal to the projected axis.
  uint8_t *sx = _planes_sx, *sy = _planes_sy, *sr = _planes_sr;
  uint8_t widest = 0;
  for (uint8_t i = 0; i < kPlaneBodyCount; ++i) {
    sx[i] = (uint8_t)(kBias + _planes_tx[kPlaneBodyFore[i]]);
    sy[i] = (uint8_t)(kBias + _planes_ty[kPlaneBodyFore[i]]);
    sr[i] = _planes_px[kPlaneBodyRadius[i]];
    if (sr[i] > sr[widest]) {
      widest = i;
    }
  }
  int8_t fx = (int8_t)(sx[0] - sx[kPlaneBodyCount - 1]);
  int8_t fy = (int8_t)(sy[0] - sy[kPlaneBodyCount - 1]);
  uint8_t n = _norm2(fx, fy);
  if (n > 0) {
    _planes_tube(vec_div8p8((int16_t)-fy, n), vec_div8p8(fx, n), _vert_count);
    _vert_count += 2 * kPlaneBodyCount;
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
  state->lay.level = kPlaneLevelDot;
  state->lay.ys = state->lay.cols = state->lay.rows = 1;
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

// A bitmap is drawn slid by lay->slid, so it lands in place at that slide;
// when this centre needs more, it goes higher by the difference, a few lines
// out of place rather than a sprite starting below the DMA cut.
int16_t planes_place(const planes_layout_t *lay, const planes_view_t *view,
                     planes_frame_t *frame) {
  uint8_t level = lay->level;
  frame->level = level;
  if (level == kPlaneLevelDot) {
    frame->xs = frame->ys = frame->cols = frame->rows = 1;
    frame->x = (int16_t)(frame->cx - kPlaneDotX);
    frame->y = (int16_t)(frame->cy - kPlaneDotY);
    frame->slid = 0;
    if (frame->y > view->cut1) {
      frame->hidden = kPlaneBelowCut;
    }
    return 0;
  }
  uint8_t ys = lay->ys, rows = lay->rows;
  frame->xs = level == kPlaneLevel1x ? 1 : 2;
  frame->ys = ys;
  frame->cols = lay->cols;
  frame->rows = rows;
  frame->x = (int16_t)(frame->cx + lay->ox);
  int16_t top = (int16_t)(frame->cy + lay->oy);
  int16_t need = _slide(view, top, ys, rows);
  int16_t slid = need > lay->slid ? need : lay->slid;
  frame->y = (int16_t)(top - slid);
  frame->slid = (uint8_t)slid;
  return need;
}

// Steps 1-7 for the frame alone: what is hidden, the centre, the scale and
// the pixel size. Nothing here depends on the state.
bool planes_centre(const planes_view_t *view, const vec3_t *c, planes_frame_t *frame) {
  frame->hidden = kPlaneShown;
  frame->cached = false;
  frame->slid = 0;
  if (c->x <= 64) {
    frame->hidden = kPlaneBehindCamera;
    return false;
  }
  if (c->x > 16000 || _abs16(c->y) >= c->x || _abs16(c->z) >= c->x) {
    frame->hidden = kPlaneOutOfRange;
    return false;
  }

  // 5. centre, 6. perspective scale, 6b. the size cap
  PROFILE_START();
  frame->cx = (int16_t)(view->cx0 - vec_div8p8(c->y, c->x));
  frame->cy = (int16_t)(view->cy0 - vec_div8p8(c->z, c->x));
  // 32768 / x: the fraction 64 / x to nine bits, exact as x > 64.
  uint16_t k = vec_fracn(64, c->x, 9);
  frame->clamped = k > view->kmax;
  if (frame->clamped) {
    k = view->kmax;
  }
  frame->k = k;
  PROFILE_END(2, kProfCentre);

  // 7. pixel size, from distance alone: R * k / 128, as trunc(2R * k / 256)
  frame->d = (uint8_t)(vec_mul8x8(kPlaneMaxRadius << 1, (uint8_t)k) >> 8);
  return true;
}

// Which state's vertices _planes_lx and _planes_ly hold, for planes_fill().
static const planes_state_t *_planes_lx_state;

void planes_project(planes_state_t *state, const planes_view_t *view,
                    const vec3_t *c, const mat3_t *axes, planes_frame_t *frame) {
  if (!planes_centre(view, c, frame)) {
    return;
  }
  int16_t cx = frame->cx, cy = frame->cy;
  uint16_t k = frame->k;
  uint8_t level = _pick_level(state->lay.level, frame->d);
  frame->level = level;

  if (level == kPlaneLevelDot) {
    state->lay.level = kPlaneLevelDot;
    state->lay.ys = state->lay.cols = state->lay.rows = 1;
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
  if (state->key_valid && state->k == k && _same_axes(state->axes, axes) &&
      planes_place(&state->lay, view, frame) == state->lay.slid) {
    frame->cached = true;
    return;
  }

  // 8. body axes are the caller's, 9. the polygons in screen pixels
  _project(k, axes);
  PROFILE_START();
  uint8_t minx = _planes_vx[0], maxx = _planes_vx[0], miny = _planes_vy[0], maxy = _planes_vy[0];
  for (uint8_t i = 1; i < _vert_count; ++i) {
    if (_planes_vx[i] < minx) minx = _planes_vx[i];
    if (_planes_vx[i] > maxx) maxx = _planes_vx[i];
    if (_planes_vy[i] < miny) miny = _planes_vy[i];
    if (_planes_vy[i] > maxy) maxy = _planes_vy[i];
  }
  uint8_t bw = (uint8_t)(maxx - minx), bh = (uint8_t)(maxy - miny);

  // 10. Y-expansion and layout
  uint8_t xs = level == kPlaneLevel1x ? 1 : 2;
  uint8_t ys = 1;
  if (level == kPlaneLevelX &&
      (bh > kPlaneYLimit || (state->lay.level == level && state->lay.ys == 2 && bh > kPlaneYHold))) {
    ys = 2;
  }
  bool same = state->lay.level == level && state->lay.ys == ys;
  uint8_t cols = bw <= kFitW[xs - 1][0] ? 1 : 2;
  if (same && state->lay.cols == 2 && cols == 1 && bw > kPlaneColsHold[xs]) {
    cols = 2;
  }
  uint8_t rows = bh <= kFitH[ys - 1][0] ? 1 : 2;
  if (same && state->lay.rows == 2 && rows == 1 && bh > kPlaneRowsHold[ys]) {
    rows = 2;
  }
  // The latches are the cached vertices' layout until they change here.
  bool cached = state->key_valid && same && state->lay.cols == cols && state->lay.rows == rows &&
                state->key_count == _vert_count;
  state->lay.level = level;
  state->lay.ys = ys;
  state->lay.cols = cols;
  state->lay.rows = rows;
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
  state->lay.ox = ox;
  state->lay.oy = oy;
  state->lay.slid = slid;

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
    uint8_t x = (uint8_t)(_planes_vx[i] - bx);
    uint16_t u = (uint16_t)((uint8_t)(_planes_vy[i] - by) + s9);
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
  _planes_lx_state = state;
  state->key_count = _vert_count;
  state->key_poly_count = _poly_count;
  state->key_band = band;
  state->key_valid = true;
  PROFILE_END(5, kProfKey);
  frame->cached = cached;
}

void planes_fill(const planes_state_t *state, uint8_t *back) {
  // 13. fill into the back buffer. A slide of a whole buffer or more leaves
  // it empty, and y no longer fits a byte.
  PROFILE_START();
  uint8_t cols = state->lay.cols, rows = state->lay.rows;
  uint8_t blocks = (uint8_t)(rows << (cols - 1));
  memset(back, 0, (uint16_t)blocks << 6);
  uint8_t height = (uint8_t)(kRows << (rows - 1));
  if ((uint16_t)state->lay.slid >> (state->lay.ys - 1) < height) {
    // The vertices are the cache's key. The arrays the fill reads still
    // hold them unless another aircraft was projected since.
    uint8_t n = state->key_count;
    if (_planes_lx_state != state) {
      memcpy(_planes_lx, state->key_x, n);
      memcpy(_planes_ly, state->key_y, n);
      _planes_lx_state = state;
    }
    _planes_fill_begin(back, cols, height);
    // The wing, tailplane and fin, then the fuselage if there is one, then
    // the end-on disc if there is one: the last polygon ends at n.
    for (uint8_t p = 0; p < 3; ++p) {
      uint8_t first = kPlanePolyStart[p];
      _planes_fill_poly(first, (uint8_t)(kPlanePolyStart[p + 1] - first));
    }
    uint8_t first = kPlanePolyStart[3];
    if (state->key_poly_count == 5) {
      _planes_fill_poly(first, 2 * kPlaneBodyCount);
      first += 2 * kPlaneBodyCount;
    }
    if (state->key_poly_count >= 4) {
      _planes_fill_poly(first, (uint8_t)(n - first));
    }
  }
  PROFILE_END(6, kProfFill);
}

void planes_render(planes_state_t *state, const planes_view_t *view,
                   const vec3_t *c, const mat3_t *axes, uint8_t *back,
                   planes_frame_t *frame) {
  planes_project(state, view, c, axes, frame);
  if (frame->hidden == kPlaneShown && frame->level != kPlaneLevelDot && !frame->cached) {
    planes_fill(state, back);
  }
}


#pragma optimize(pop)
