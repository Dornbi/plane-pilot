#include "planes_asm.h"

#include <stdint.h>

#include "planedef.h"

// The assembly half of planes.cc (planes_asm.h): the axis products and the
// polygon fill. Both multiply through vec_mul8, the quarter-square step
// vec_fastmul8p8 is built from.

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

// ---------------------------------------------------------------------------
// Products.

// One axis's pairs, for _planes_axis_products(): the loop bounds, and each
// screen component's magnitude split into a low byte and a 256, with a mask
// that is $00 for a negative component and $FF for a positive one.
// fpilot keeps these per-call statics, and the fill's below, in the screen's
// dead rows (mem.h tscratch), as planes.cc does its arrays.
#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
static uint8_t _ap, _ap_end;
static uint8_t _ay_lo, _ay_hi, _ay_m, _az_lo, _az_hi, _az_m;

// For p in _ap .. _ap_end - 1, with v = _planes_pv[p]: r = hi(lo * v), plus v
// if the component reached 256, is trunc(|c| * v / 256), and s = (r ^ m) - m
// is -r for a positive component and r for a negative -- the negated product.
static void _pair_products(void) {
  // clang-format off
  __asm {
        ldy _ap;
    L_pair:
        sty _ap;
        lda _planes_pv, y;
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
        sta _planes_tx, y;
        eor #$ff;
        clc;
        adc #1;
        sta _planes_tx + 16, y;

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
        sta _planes_ty, y;
        eor #$ff;
        clc;
        adc #1;
        sta _planes_ty + 16, y;

        iny;
        cpy _ap_end;
        bne L_pair;
  }
  // clang-format on
}

static_assert(kPlaneMagCount == 16, "planes_asm.h sizes _planes_px by hand");

// _planes_mags(): sixteen products on vec_mul8, k in vec_mul8_b throughout,
// which vec_mul8 only reads. A magnitude doubled still fits a byte (the
// largest is 66), and (hi + 1) >> 1 takes the carry into bit 7 for hi 255.
static uint8_t _mi;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

static void _mags(void) {
  // clang-format off
  __asm {
        ldy #15;
    L_mag:
        sty _mi;
        lda kPlaneMags, y;
        asl;
        jsr vec_mul8;
        clc;
        adc #1;
        ror;
        ldy _mi;
        sta _planes_px, y;
        dey;
        bpl L_mag;

        // Each pair's magnitude times k, pairs 15 .. 1.
        ldy #15;
    L_pair_mag:
        ldx kPlanePairMag, y;
        lda _planes_px, x;
        sta _planes_pv, y;
        dey;
        bne L_pair_mag;
  }
  // clang-format on
}

void _planes_mags(uint8_t k) {
  vec_mul8_b = k;
  _mags();
}

// ---------------------------------------------------------------------------
// The flat surfaces' vertices, and the bounding box.

static_assert(kPlaneVertCount == 16, "_planes_flat() counts 15 .. 0");

// Each term's product, summed, plus 128 - which is an eor of bit 7.
void _planes_flat(void) {
  // clang-format off
  __asm {
        ldx #15;
    L_flat:
        ldy kPlaneVertFore, x;
        lda _planes_tx, y;
        ldy kPlaneVertLeft, x;
        clc;
        adc _planes_tx, y;
        ldy kPlaneVertUp, x;
        clc;
        adc _planes_tx, y;
        eor #$80;
        sta _planes_vx, x;
        ldy kPlaneVertFore, x;
        lda _planes_ty, y;
        ldy kPlaneVertLeft, x;
        clc;
        adc _planes_ty, y;
        ldy kPlaneVertUp, x;
        clc;
        adc _planes_ty, y;
        eor #$80;
        sta _planes_vy, x;
        dex;
        bpl L_flat;
  }
  // clang-format on
}

uint8_t _planes_box_x0, _planes_box_x1, _planes_box_y0, _planes_box_y1;
#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
static uint8_t _bn;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

void _planes_box(uint8_t count) {
  _bn = count;
  // clang-format off
  __asm {
        lda _planes_vx;
        sta _planes_box_x0;
        sta _planes_box_x1;
        lda _planes_vy;
        sta _planes_box_y0;
        sta _planes_box_y1;
        ldy #1;
        cpy _bn;
        beq L_box_done;
    L_box:
        lda _planes_vx, y;
        cmp _planes_box_x0;
        bcs L_x_not_min;
        sta _planes_box_x0;
    L_x_not_min:
        cmp _planes_box_x1;
        bcc L_x_not_max;
        sta _planes_box_x1;
    L_x_not_max:
        lda _planes_vy, y;
        cmp _planes_box_y0;
        bcs L_y_not_min;
        sta _planes_box_y0;
    L_y_not_min:
        cmp _planes_box_y1;
        bcc L_y_not_max;
        sta _planes_box_y1;
    L_y_not_max:
        iny;
        cpy _bn;
        bne L_box;
    L_box_done:
  }
  // clang-format on
}

// ---------------------------------------------------------------------------
// The vertex cache's key.

static_assert(kPlaneVertMax == 32, "_key() finds key_y 32 bytes after key_x");

#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
static uint8_t _kkl, _kkh, _kn, _kbx, _kby, _ks9l, _kxs, _kys, _ks9h, _kdiff;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

// Self-modifying: the call's constants are written into the loop before it
// runs - the key's two addresses, bx, by and the slide's low byte as
// operands, and the expansions as opcodes - so that a vertex costs its
// loads, stores and compares and nothing else. Y-expanded, y is bits 1-8 of
// the nine-bit (v - by) + s9: ror brings the sum's carry in as bit 7, and
// eor flips it when the slide's own bit 8 is set.
static void _key(void) {
  // clang-format off
  __asm {
        lda _kkl;
        sta L_cx + 1;
        sta L_sx + 1;
        clc;
        adc #32;
        sta L_cy + 1;
        sta L_sy + 1;
        lda _kkh;
        sta L_cx + 2;
        sta L_sx + 2;
        adc #0;
        sta L_cy + 2;
        sta L_sy + 2;
        lda _kbx;
        sta L_bx + 1;
        lda _kby;
        sta L_by + 1;
        lda _ks9l;
        sta L_s9 + 1;
        lda _kn;
        sta L_n + 1;

        lda #$ea;               // nop
        ldx _kxs;
        cpx #2;
        bne L_x_one;
        lda #$4a;               // lsr a
    L_x_one:
        sta L_xsh;
        lda #$ea;
        ldx #0;
        ldy _kys;
        cpy #2;
        bne L_y_one;
        lda #$6a;               // ror a
        ldy _ks9h;
        beq L_y_one;
        ldx #$80;
    L_y_one:
        sta L_ysh;
        stx L_yeor + 1;

        lda #0;
        sta _kdiff;
        ldy #0;
    L_vert:
        lda _planes_vx, y;
        sec;
    L_bx:
        sbc #0;
    L_xsh:
        nop;
        sta _planes_lx, y;
    L_cx:
        cmp $1234, y;
        beq L_x_same;
        inc _kdiff;
    L_x_same:
    L_sx:
        sta $1234, y;

        lda _planes_vy, y;
        sec;
    L_by:
        sbc #0;
        clc;
    L_s9:
        adc #0;
    L_ysh:
        nop;
    L_yeor:
        eor #0;
        sta _planes_ly, y;
    L_cy:
        cmp $1234, y;
        beq L_y_same;
        inc _kdiff;
    L_y_same:
    L_sy:
        sta $1234, y;
        iny;
    L_n:
        cpy #0;
        bne L_vert;
  }
  // clang-format on
}

bool _planes_key(uint8_t *key, uint8_t count, uint8_t bx, uint8_t by, uint16_t s9,
                 uint8_t xs, uint8_t ys) {
  _kkl = (uint8_t)(uint16_t)key;
  _kkh = (uint8_t)((uint16_t)key >> 8);
  _kn = count;
  _kbx = bx;
  _kby = by;
  _ks9l = (uint8_t)s9;
  _ks9h = (uint8_t)(s9 >> 8);
  _kxs = xs;
  _kys = ys;
  _key();
  return _kdiff != 0;
}

// ---------------------------------------------------------------------------
// The fuselage outline.

static_assert(kPlaneBodyCount == 3, "_tube() walks three stations");

#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
// |ux| and |uy| as a low byte and a 256, each sign as 0 or $FF, the offsets
// r * u per station, and the loop's station and vertex indices.
static uint8_t _ux_lo, _ux_hi, _ux_neg, _uy_lo, _uy_hi, _uy_neg;
static int8_t _tox[3], _toy[3];
static uint8_t _ti, _tat;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

// smul(u, r) per station: q = (|u| * r + 128) >> 8, which is the reference's
// ((p >> 7) + 1) >> 1 - the high byte plus bit 7 of the low one - and just r
// when |u| is 256; negated when u is.
static void _tube(void) {
  // clang-format off
  __asm {
        ldy #2;
    L_station:
        sty _ti;
        lda _planes_sr, y;
        sta vec_mul8_b;

        lda _ux_hi;
        beq L_ox_mul;
        lda vec_mul8_b;
        jmp L_ox_sign;
    L_ox_mul:
        lda _ux_lo;
        jsr vec_mul8;
        asl vec_mul8_lo;
        adc #0;
    L_ox_sign:
        ldx _ux_neg;
        beq L_ox_pos;
        eor #$ff;
        clc;
        adc #1;
    L_ox_pos:
        ldy _ti;
        sta _tox, y;

        lda _uy_hi;
        beq L_oy_mul;
        lda vec_mul8_b;
        jmp L_oy_sign;
    L_oy_mul:
        lda _uy_lo;
        jsr vec_mul8;
        asl vec_mul8_lo;
        adc #0;
    L_oy_sign:
        ldx _uy_neg;
        beq L_oy_pos;
        eor #$ff;
        clc;
        adc #1;
    L_oy_pos:
        ldy _ti;
        sta _toy, y;
        dey;
        bpl L_station;

        // Stations 0 .. 2 plus the offset, then 2 .. 0 minus it.
        ldx _tat;
        ldy #0;
    L_plus:
        lda _planes_sx, y;
        clc;
        adc _tox, y;
        sta _planes_vx, x;
        lda _planes_sy, y;
        clc;
        adc _toy, y;
        sta _planes_vy, x;
        inx;
        iny;
        cpy #3;
        bne L_plus;
        ldy #2;
    L_minus:
        lda _planes_sx, y;
        sec;
        sbc _tox, y;
        sta _planes_vx, x;
        lda _planes_sy, y;
        sec;
        sbc _toy, y;
        sta _planes_vy, x;
        inx;
        dey;
        bpl L_minus;
  }
  // clang-format on
}

void _planes_tube(int16_t ux, int16_t uy, uint8_t at) {
  _ux_neg = ux < 0 ? 0xFF : 0x00;
  _uy_neg = uy < 0 ? 0xFF : 0x00;
  uint16_t mx = (uint16_t)(ux < 0 ? -ux : ux), my = (uint16_t)(uy < 0 ? -uy : uy);
  _ux_lo = (uint8_t)mx;
  _ux_hi = (uint8_t)(mx >> 8);
  _uy_lo = (uint8_t)my;
  _uy_hi = (uint8_t)(my >> 8);
  _tat = at;
  _tube();
}

void _planes_axis_products(const vec3_t *a, uint8_t first, uint8_t end) {
  uint16_t my = (uint16_t)(a->y < 0 ? -a->y : a->y), mz = (uint16_t)(a->z < 0 ? -a->z : a->z);
  _ay_lo = (uint8_t)my;
  _ay_hi = (uint8_t)(my >> 8);
  _ay_m = a->y < 0 ? 0x00 : 0xFF;
  _az_lo = (uint8_t)mz;
  _az_hi = (uint8_t)(mz >> 8);
  _az_m = a->z < 0 ? 0x00 : 0xFF;
  _ap = first;
  _ap_end = end;
  _pair_products();
}

// ---------------------------------------------------------------------------
// The fill: convex polygons into up to four sprite blocks 64 bytes apart.

static __zeropage uint8_t *_blk;    // the back buffer's first block
#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
static uint8_t _buf_h, _buf_w1;     // height, and width - 1
static uint8_t _bottom_adj;         // offset of the lower row of blocks, less 64
static uint8_t _lo[42], _hi[42];    // each row's extent while a polygon is traced
static uint8_t _fs, _fn;            // the polygon: first vertex, count

static uint8_t _y0, _y1, _fi, _xt, _yt, _xe, _ye, _dy, _cnt, _row;
static uint8_t _dxl, _dxh, _sl, _sh, _hl, _hh, _xbl, _xbh;
static uint8_t _pa, _pb, _t, _base, _jb;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

// kPlaneRecip is striped, its high bytes kPlaneRecipCount after the low ones,
// and the inline assembler takes only a literal offset - so the fill spells
// it 42, and this holds the generator to it.
static_assert(kPlaneRecipCount == 42, "update kPlaneRecip + 42 in the fill");

// _planes_fill_poly() on _fs and _fn.
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
        lda _planes_ly, x;
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
        lda _planes_ly, y;
        cmp _planes_ly, x;
        bcs L_a_top;
        lda _planes_lx, y;
        sta _xt;
        lda _planes_ly, y;
        sta _yt;
        lda _planes_lx, x;
        sta _xe;
        lda _planes_ly, x;
        sta _ye;
        jmp L_ends;
    L_a_top:
        lda _planes_lx, x;
        sta _xt;
        lda _planes_ly, x;
        sta _yt;
        lda _planes_lx, y;
        sta _xe;
        lda _planes_ly, y;
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
        ldy _dy;
        lda kPlaneRecip + 42, y;  // striped: the high bytes, see below
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
        // The row's first byte: 3 * row, and in the lower blocks _bottom on
        // from row 21, which is 3 * row + _bottom - 63: the compare's carry
        // is the 1 that _bottom_adj, _bottom - 64, is short of it.
        stx _t;
        txa;
        asl;
        adc _t;
        cpx #21;
        bcc L_upper;
        adc _bottom_adj;
    L_upper:
        sta _base;
        lda _hi, x;
        cmp _buf_w1;
        bcc L_b_in;
        lda _buf_w1;
    L_b_in:
        sta _pb;
        lsr;
        lsr;
        lsr;
        sta _jb;
        lda _lo, x;
        cmp _buf_w1;
        bcc L_a_in;
        lda _buf_w1;
    L_a_in:
        // The left end's byte into X, its mask into A.
        tay;
        lsr;
        lsr;
        lsr;
        tax;
        tya;
        and #7;
        tay;
        lda kPlaneLeftMask, y;
        cpx _jb;
        bne L_bytes;
        // Most spans are inside one byte: both masks on it.
        sta _t;
        lda _pb;
        and #7;
        tay;
        lda kPlaneRightMask, y;
        and _t;
        sta _t;
        lda kPlaneByteOffset, x;
        clc;
        adc _base;
        tay;
        lda (_blk), y;
        ora _t;
        sta (_blk), y;
        jmp L_span_next;
    L_bytes:
        sta _t;
        lda kPlaneByteOffset, x;
        clc;
        adc _base;
        tay;
        lda (_blk), y;
        ora _t;
        sta (_blk), y;
        inx;
        cpx _jb;
        beq L_right;
    L_full:
        lda kPlaneByteOffset, x;
        clc;
        adc _base;
        tay;
        lda #$ff;
        sta (_blk), y;
        inx;
        cpx _jb;
        bne L_full;
    L_right:
        lda _pb;
        and #7;
        tay;
        lda kPlaneRightMask, y;
        sta _t;
        lda kPlaneByteOffset, x;
        clc;
        adc _base;
        tay;
        lda (_blk), y;
        ora _t;
        sta (_blk), y;
    L_span_next:
        ldx _row;
        cpx _y1;
        inx;
        bcs L_done;
        jmp L_span_row;
    L_done:
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

void _planes_fill_begin(uint8_t *back, uint8_t cols, uint8_t height) {
  _blk = back;
  _buf_h = height;
  // 24 doubled for two columns: not 24 << (cols - 1), which links a 16 B
  // shift table (planes.cc _twice_if()).
  _buf_w1 = cols == 2 ? 2 * 24 - 1 : 24 - 1;
  _bottom_adj = (uint8_t)((cols << 6) - 64);
}

void _planes_fill_poly(uint8_t first, uint8_t count) {
  _fs = first;
  _fn = count;
  _fill_poly();
}

#pragma optimize(pop)
