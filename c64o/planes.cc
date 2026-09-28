#include "planes.h"

#include <stdint.h>
#include <string.h>

#include "planedef.h"

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
//   the centre, so the products and the vertices relative to the centre are
//   signed bytes, and once placed in the buffer they are unsigned ones.
// - It relies on the cap for the silhouette fitting its layout -- which
//   lib/planes.py only guards against, and tests/test_planes.py
//   (TestSizeClamp) shows never happens -- so it anchors on the box alone,
//   and fills without clipping to the buffer's sides.
// - A frame with the last one's scale and axes skips the projection.

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
static uint8_t _pv[16];
static uint16_t _px_k;

// Each pair's screen offset, x from the axis's y and y from its z: negated at
// p, as is at 16 + p, and zero at 0 -- the terms kPlaneVert* index.
static int8_t _tx[32], _ty[32];

// This frame's vertices in buffer coordinates, for the fill.
static uint8_t _lx[kPlaneVertMax], _ly[kPlaneVertMax];

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

// ---------------------------------------------------------------------------
// Projection.

#ifdef __OSCAR64__
// One axis's pairs, for _axis_products(): the loop bounds, and each screen
// component's magnitude split into a low byte and a 256, with a mask that is
// $00 for a negative component and $FF for a positive one.
static uint8_t _ap, _ap_end;
static uint8_t _ay_lo, _ay_hi, _ay_m, _az_lo, _az_hi, _az_m;

// For p in _ap .. _ap_end - 1, with v = _pv[p]: r = hi(lo * v), plus v if the
// component reached 256, is trunc(|c| * v / 256), and s = (r ^ m) - m is
// -r for a positive component and r for a negative -- the negated product.
static void _pair_products(void) {
  // clang-format off
  __asm {
        ldy _ap;
    L_pair:
        sty _ap;
        lda _pv, y;
        sta vec_mul8_b;

        lda _ay_lo;
        jsr vec_mul8;
        ldx _ay_hi;
        beq L_x_lo;
        clc;
        adc vec_mul8_b;
    L_x_lo:
        ldy _ap;
        eor _ay_m;
        sec;
        sbc _ay_m;
        sta _tx, y;
        eor #$ff;
        clc;
        adc #1;
        sta _tx + 16, y;

        lda _az_lo;
        jsr vec_mul8;
        ldx _az_hi;
        beq L_y_lo;
        clc;
        adc vec_mul8_b;
    L_y_lo:
        ldy _ap;
        eor _az_m;
        sec;
        sbc _az_m;
        sta _ty, y;
        eor #$ff;
        clc;
        adc #1;
        sta _ty + 16, y;

        iny;
        cpy _ap_end;
        bne L_pair;
  }
  // clang-format on
}
#endif

// The products of one axis's pairs, each trunc(component * v / 256) as
// vec_fastmul8p8 truncates: _tx from the axis's y, _ty from its z.
static void _axis_products(const vec3_t *a, uint8_t first, uint8_t end) {
#ifdef __OSCAR64__
  uint16_t my = (uint16_t)_abs16(a->y), mz = (uint16_t)_abs16(a->z);
  _ay_lo = (uint8_t)my;
  _ay_hi = (uint8_t)(my >> 8);
  _ay_m = a->y < 0 ? 0x00 : 0xFF;
  _az_lo = (uint8_t)mz;
  _az_hi = (uint8_t)(mz >> 8);
  _az_m = a->z < 0 ? 0x00 : 0xFF;
  _ap = first;
  _ap_end = end;
  _pair_products();
#else
  for (uint8_t p = first; p < end; ++p) {
    int8_t ox = (int8_t)vec_fastmul8p8(a->y, _pv[p]);
    int8_t oy = (int8_t)vec_fastmul8p8(a->z, _pv[p]);
    _tx[p] = (int8_t)-ox;
    _tx[16 + p] = ox;
    _ty[p] = (int8_t)-oy;
    _ty[16 + p] = oy;
  }
#endif
}

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
      _pv[p] = _px[kPlanePairMag[p]];
    }
    _px_k = k;
  }
  PROFILE_END(8, kProfMags);
  PROFILE_START();
  _axis_products(&axes->front, kPlaneAxisPairs[0], kPlaneAxisPairs[1]);
  _axis_products(&axes->left, kPlaneAxisPairs[1], kPlaneAxisPairs[2]);
  _axis_products(&axes->up, kPlaneAxisPairs[2], kPlaneAxisPairs[3]);
  PROFILE_END(9, kProfPairs);
  PROFILE_START();
  for (uint8_t i = 0; i < kPlaneVertCount; ++i) {
    uint8_t f = kPlaneVertFore[i], l = kPlaneVertLeft[i], u = kPlaneVertUp[i];
    _vx[i] = (uint8_t)(kBias + _tx[f] + _tx[l] + _tx[u]);
    _vy[i] = (uint8_t)(kBias + _ty[f] + _ty[l] + _ty[u]);
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
    sx[i] = (uint8_t)(kBias + _tx[kPlaneBodyFore[i]]);
    sy[i] = (uint8_t)(kBias + _ty[kPlaneBodyFore[i]]);
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
// The rasteriser: convex polygons into up to four sprite blocks 64 bytes
// apart. Vertices are bytes in buffer coordinates -- x inside the buffer, y
// possibly below it -- and a row at or below the buffer's height is skipped.
// Written twice: in C for the host, where test/planes_test.cc holds it to
// lib/planes.py, and in assembly for the C64, which test/target_test.cc holds
// to the same reference. The two follow each other step for step.

static __zeropage uint8_t *_blk;    // the back buffer's first block
static uint8_t _buf_h, _buf_w1;     // height, and width - 1
static uint8_t _bottom;             // offset of the lower row of blocks
static uint8_t _lo[2 * kRows], _hi[2 * kRows];
static uint8_t _fs, _fn;            // the polygon: first vertex, count

static const uint8_t kLeftMask[8] = {0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01};
static const uint8_t kRightMask[8] = {0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF};
// Byte j of a row: 0-2 in the left block, 3-5 in the one 64 bytes on.
static const uint8_t kByteOffset[6] = {0, 1, 2, 64, 65, 66};

#ifdef __OSCAR64__
static uint8_t _y0, _y1, _fi, _xt, _yt, _xe, _ye, _dy, _cnt, _row;
static uint8_t _dxl, _dxh, _sl, _sh, _hl, _hh, _xbl, _xbh;
static uint8_t _pa, _pb, _t, _base, _jb;

// Edge-inclusive convex fill with vertices at pixel centres (lib/planes.py
// fill_poly()): vertices _fs .. _fs + _fn - 1 of _lx, _ly.
static void _fill_poly(void) {
  // clang-format off
  __asm {
        // The polygon's rows, y0 .. y1; none if it starts below the buffer.
        ldx _fs;
        lda #$ff;
        sta _y0;
        lda #0;
        sta _y1;
        ldy _fn;
    L_rows:
        lda _ly, x;
        cmp _y0;
        bcs L_not_min;
        sta _y0;
    L_not_min:
        cmp _y1;
        bcc L_not_max;
        sta _y1;
    L_not_max:
        inx;
        dey;
        bne L_rows;
        lda _y0;
        cmp _buf_h;
        bcc L_visible;
        rts;
    L_visible:
        lda _y1;
        cmp _buf_h;
        bcc L_y1_in;
        ldx _buf_h;
        dex;
        stx _y1;
    L_y1_in:
        ldx _y0;
    L_init:
        lda #$ff;
        sta _lo, x;
        lda #0;
        sta _hi, x;
        cpx _y1;
        inx;
        bcc L_init;

        // Each edge: top and bottom end points, no swap.
        lda #0;
        sta _fi;
    L_edge:
        lda _fi;
        clc;
        adc _fs;
        tax;
        ldy _fi;
        iny;
        cpy _fn;
        bne L_b_next;
        ldy #0;
    L_b_next:
        tya;
        clc;
        adc _fs;
        tay;
        lda _ly, y;
        cmp _ly, x;
        bcs L_a_top;
        lda _lx, y;
        sta _xt;
        lda _ly, y;
        sta _yt;
        lda _lx, x;
        sta _xe;
        lda _ly, x;
        sta _ye;
        jmp L_ends;
    L_a_top:
        lda _lx, x;
        sta _xt;
        lda _ly, x;
        sta _yt;
        lda _lx, y;
        sta _xe;
        lda _ly, y;
        sta _ye;
    L_ends:
        lda _ye;
        sec;
        sbc _yt;
        sta _dy;
        bne L_slanted;
        // Level: one row, end to end.
        lda _yt;
        sta _row;
        lda _xt;
        sta _pa;
        lda _xe;
        sta _pb;
        jsr L_put;
        jmp L_next;

    L_slanted:
        // dx = xe - xt, and dx * 128 into the slope as the first guess.
        lda _xe;
        sec;
        sbc _xt;
        sta _dxl;
        lda #0;
        sbc #0;
        sta _dxh;
        cmp #$80;
        ror;
        lda _dxl;
        ror;
        sta _sh;
        lda #0;
        ror;
        sta _sl;
        lda _dy;
        cmp #1;
        bne L_not_one;
        // dy 1: no middle row, and the first row's half step is dx * 128.
        lda _sl;
        sta _hl;
        lda _sh;
        sta _hh;
        jmp L_first;
    L_not_one:
        cmp #2;
        beq L_halve;
        // dy 3 and on: slope = trunc(dx * 65536 / dy / 256) from the
        // reciprocal table, |dx| * hi + hi(|dx| * lo), the sign last.
        lda _dxl;
        ldx _dxh;
        bpl L_dx_pos;
        eor #$ff;
        clc;
        adc #1;
    L_dx_pos:
        sta _t;
        lda _dy;
        asl;
        tay;
        lda kPlaneRecip + 1, y;
        sta vec_mul8_b;
        lda kPlaneRecip, y;
        sta _pa;
        lda _t;
        jsr vec_mul8;
        sta _sh;
        lda vec_mul8_lo;
        sta _sl;
        lda _pa;
        sta vec_mul8_b;
        lda _t;
        jsr vec_mul8;
        clc;
        adc _sl;
        sta _sl;
        lda _sh;
        adc #0;
        sta _sh;
        lda _dxh;
        bpl L_halve;
        sec;
        lda #0;
        sbc _sl;
        sta _sl;
        lda #0;
        sbc _sh;
        sta _sh;
    L_halve:
        lda _sh;
        cmp #$80;
        ror;
        sta _hh;
        lda _sl;
        ror;
        sta _hl;

    L_first:
        // The top row takes half a step, from xt * 256 + 128. dx's sign says
        // which end of every row is the left one; rows only go down, so the
        // first at or below the buffer ends the edge.
        ldx _yt;
        cpx _buf_h;
        bcc L_first_in;
        jmp L_next;
    L_first_in:
        lda #128;
        clc;
        adc _hl;
        sta _xbl;
        lda _xt;
        adc _hh;
        sta _xbh;
        bit _dxh;
        bmi L_first_neg;
        lda _xt;
        cmp _lo, x;
        bcs L_fp_hi;
        sta _lo, x;
    L_fp_hi:
        lda _xbh;
        cmp _hi, x;
        bcc L_middle;
        sta _hi, x;
        jmp L_middle;
    L_first_neg:
        lda _xbh;
        cmp _lo, x;
        bcs L_fn_hi;
        sta _lo, x;
    L_fn_hi:
        lda _xt;
        cmp _hi, x;
        bcc L_middle;
        sta _hi, x;

        // The middle rows a whole step each, from the last row's end.
    L_middle:
        ldy _dy;
        dey;
        beq L_last;
        bit _dxh;
        bmi L_mid_neg;
    L_mid_pos:
        inx;
        cpx _buf_h;
        bcs L_mid_out;
        lda _xbh;
        cmp _lo, x;
        bcs L_mp_step;
        sta _lo, x;
    L_mp_step:
        clc;
        lda _xbl;
        adc _sl;
        sta _xbl;
        lda _xbh;
        adc _sh;
        sta _xbh;
        cmp _hi, x;
        bcc L_mp_next;
        sta _hi, x;
    L_mp_next:
        dey;
        bne L_mid_pos;
        jmp L_last;
    L_mid_out:
        jmp L_next;
    L_mid_neg:
        inx;
        cpx _buf_h;
        bcs L_mid_out;
        lda _xbh;
        cmp _hi, x;
        bcc L_mn_step;
        sta _hi, x;
    L_mn_step:
        clc;
        lda _xbl;
        adc _sl;
        sta _xbl;
        lda _xbh;
        adc _sh;
        sta _xbh;
        cmp _lo, x;
        bcs L_mn_next;
        sta _lo, x;
    L_mn_next:
        dey;
        bne L_mid_neg;

        // The bottom row ends on the end point.
    L_last:
        ldx _ye;
        cpx _buf_h;
        bcs L_next;
        bit _dxh;
        bmi L_last_neg;
        lda _xbh;
        cmp _lo, x;
        bcs L_lp_hi;
        sta _lo, x;
    L_lp_hi:
        lda _xe;
        cmp _hi, x;
        bcc L_next;
        sta _hi, x;
        jmp L_next;
    L_last_neg:
        lda _xe;
        cmp _lo, x;
        bcs L_ln_hi;
        sta _lo, x;
    L_ln_hi:
        lda _xbh;
        cmp _hi, x;
        bcc L_next;
        sta _hi, x;
    L_next:
        inc _fi;
        lda _fi;
        cmp _fn;
        beq L_spans;
        jmp L_edge;

        // Each row from its leftmost pixel to its rightmost.
    L_spans:
        ldx _y0;
    L_span_row:
        stx _row;
        lda _hi, x;
        cmp _lo, x;
        bcs L_span;
        jmp L_span_next;
    L_span:
        lda _lo, x;
        cmp _buf_w1;
        bcc L_a_in;
        lda _buf_w1;
    L_a_in:
        sta _pa;
        lda _hi, x;
        cmp _buf_w1;
        bcc L_b_in;
        lda _buf_w1;
    L_b_in:
        sta _pb;
        // The row's first byte: 3 * row in the upper blocks, _bottom on in
        // the lower.
        txa;
        cmp #21;
        bcc L_upper;
        sbc #21;
        sta _t;
        asl;
        adc _t;
        adc _bottom;
        jmp L_based;
    L_upper:
        sta _t;
        asl;
        adc _t;
    L_based:
        sta _base;
        lda _pa;
        lsr;
        lsr;
        lsr;
        tax;
        lda _pb;
        lsr;
        lsr;
        lsr;
        sta _jb;
        cpx _jb;
        bne L_bytes;
        // Most spans are inside one byte: both masks on it.
        lda _pa;
        and #7;
        tay;
        lda kLeftMask, y;
        sta _t;
        lda _pb;
        and #7;
        tay;
        lda kRightMask, y;
        and _t;
        jsr L_or;
        jmp L_span_next;
    L_bytes:
        lda _pa;
        and #7;
        tay;
        lda kLeftMask, y;
        jsr L_or;
        inx;
    L_full:
        cpx _jb;
        beq L_right;
        lda kByteOffset, x;
        clc;
        adc _base;
        tay;
        lda #$ff;
        sta (_blk), y;
        inx;
        jmp L_full;
    L_right:
        lda _pb;
        and #7;
        tay;
        lda kRightMask, y;
        jsr L_or;
    L_span_next:
        ldx _row;
        cpx _y1;
        inx;
        bcs L_done;
        jmp L_span_row;
    L_done:
        rts;

        // OR A into byte X of the row at _base. Keeps X.
    L_or:
        sta _t;
        lda kByteOffset, x;
        clc;
        adc _base;
        tay;
        lda (_blk), y;
        ora _t;
        sta (_blk), y;
        rts;

        // Widen row _row's extent to take pixels _pa and _pb, in either
        // order, if the row is inside the buffer.
    L_put:
        ldx _row;
        cpx _buf_h;
        bcs L_put_done;
        lda _pa;
        cmp _pb;
        bcc L_put_ab;
        lda _pb;
        cmp _lo, x;
        bcs L_put_ba_hi;
        sta _lo, x;
    L_put_ba_hi:
        lda _pa;
        cmp _hi, x;
        bcc L_put_done;
        sta _hi, x;
        rts;
    L_put_ab:
        cmp _lo, x;
        bcs L_put_ab_hi;
        sta _lo, x;
    L_put_ab_hi:
        lda _pb;
        cmp _hi, x;
        bcc L_put_done;
        sta _hi, x;
    L_put_done:
        rts;
  }
  // clang-format on
}
#else
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
  uint8_t lm = kLeftMask[a & 7], rm = kRightMask[b & 7];
  if (ja == jb) {
    _blk[base + kByteOffset[ja]] |= (uint8_t)(lm & rm);
    return;
  }
  _blk[base + kByteOffset[ja]] |= lm;
  for (uint8_t j = (uint8_t)(ja + 1); j < jb; ++j) {
    _blk[base + kByteOffset[j]] = 0xFF;
  }
  _blk[base + kByteOffset[jb]] |= rm;
}

// Edge-inclusive convex fill with vertices at pixel centres (lib/planes.py
// fill_poly()): vertices _fs .. _fs + _fn - 1 of _lx, _ly.
static void _fill_poly(void) {
  uint8_t s = _fs, n = _fn;
  uint8_t min_y = 255, max_y = 0;
  for (uint8_t i = 0; i < n; ++i) {
    uint8_t y = _ly[s + i];
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
    uint8_t top = _ly[b] < _ly[a] ? b : a, bot = _ly[b] < _ly[a] ? a : b;
    uint8_t xt = _lx[top], yt = _ly[top], xe = _lx[bot], ye = _ly[bot];
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
    _lx[i] = x;
    _ly[i] = y;
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
  _buf_h = (uint8_t)(kRows << (rows - 1));
  if ((uint16_t)slid >> (ys - 1) < _buf_h) {
    _blk = back;
    _buf_w1 = (uint8_t)((kCols << (cols - 1)) - 1);
    _bottom = (uint8_t)(cols << 6);
    for (uint8_t p = 0; p < _poly_count; ++p) {
      _fs = _poly_start[p];
      _fn = (uint8_t)(_poly_start[p + 1] - _fs);
      _fill_poly();
    }
  }
  PROFILE_END(6, kProfFill);
}

#pragma optimize(pop)
