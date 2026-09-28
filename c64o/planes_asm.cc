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
static uint8_t _buf_h, _buf_w1;     // height, and width - 1
static uint8_t _bottom;             // offset of the lower row of blocks
static uint8_t _lo[42], _hi[42];    // each row's extent while a polygon is traced
static uint8_t _fs, _fn;            // the polygon: first vertex, count

static uint8_t _y0, _y1, _fi, _xt, _yt, _xe, _ye, _dy, _cnt, _row;
static uint8_t _dxl, _dxh, _sl, _sh, _hl, _hh, _xbl, _xbh;
static uint8_t _pa, _pb, _t, _base, _jb;

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
        lda kPlaneLeftMask, y;
        sta _t;
        lda _pb;
        and #7;
        tay;
        lda kPlaneRightMask, y;
        and _t;
        jsr L_or;
        jmp L_span_next;
    L_bytes:
        lda _pa;
        and #7;
        tay;
        lda kPlaneLeftMask, y;
        jsr L_or;
        inx;
    L_full:
        cpx _jb;
        beq L_right;
        lda kPlaneByteOffset, x;
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
        lda kPlaneRightMask, y;
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
        lda kPlaneByteOffset, x;
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

void _planes_fill_begin(uint8_t *back, uint8_t cols, uint8_t height) {
  _blk = back;
  _buf_h = height;
  _buf_w1 = (uint8_t)((24 << (cols - 1)) - 1);
  _bottom = (uint8_t)(cols << 6);
}

void _planes_fill_poly(uint8_t first, uint8_t count) {
  _fs = first;
  _fn = count;
  _fill_poly();
}

#pragma optimize(pop)
