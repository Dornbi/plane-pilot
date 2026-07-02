#include "vec.h"

#include "fmath.h"
#include <stdlib.h>

extern const uint8_t vec_recip_lut[];
extern const uint8_t vec_sqr_lo[];
extern const uint8_t vec_sqr_hi[];

#ifndef __OSCAR64__
#define __zeropage
#endif

__zeropage vec3_t vec_v;
__zeropage int16_t vec_sx;
__zeropage int16_t vec_sy;

// These are used for temporary storage. vec_fastmul8p8 reuses tmp1..tmp4;
// they are never live across calls (single-threaded, the raster IRQs do no
// vector math).
static __zeropage uint8_t tmp1, tmp2, tmp3, tmp4;
static __zeropage int16_t project_mul_a;
static __zeropage int16_t project_mul_b;
static __zeropage int16_t mul_res;

// PERF: This is 200 bytes smaller and about the same speed as the C version.
//
// Full 16x16 -> middle-16 multiply built on the quarter square tables:
// x * y = T(x + y) - T(|x - y|) with T(i) = i*i/4 is exact for 8-bit x, y
// because the floor errors cancel (the two indices have the same parity).
// The 16-bit product is assembled from the four 8x8 partial products:
//   (a * b) >> 8 = (al*bl >> 8) + al*bh + ah*bl + (ah*bh << 8)
// computed on |a| and |b| with the sign applied at the end, which matches
// trunc((a * b) / 256) exactly, wrapping to 16 bits on overflow. Unlike
// the old C quarter-square path this has no |a| + |b| < 4096 constraint.
int16_t vec_fastmul8p8(int16_t a, int16_t b) {
  // clang-format off
  __asm {
        // Sign of the result in bit 7 of tmp1.
        lda a+1;
        eor b+1;
        sta tmp1;

        // a = |a|
        lda a+1;
        bpl L_abs_a_done_fm;
        sec;
        lda #0;
        sbc a;
        sta a;
        lda #0;
        sbc a+1;
        sta a+1;
    L_abs_a_done_fm:
        // b = |b|
        lda b+1;
        bpl L_abs_b_done_fm;
        sec;
        lda #0;
        sbc b;
        sta b;
        lda #0;
        sbc b+1;
        sta b+1;
    L_abs_b_done_fm:

        lda a;
        bne L_a_lo_nonzero_fm;

        // |a| is a multiple of 256.
        lda a+1;
        bne L_a_hi_nonzero_fm;
        // a == 0
        sta mul_res;
        sta mul_res+1;
        jmp L_done_fm;
    L_a_hi_nonzero_fm:
        cmp #1;
        bne L_a_hi_mul_fm;
        // |a| == 256: result = |b|
        lda b;
        sta mul_res;
        lda b+1;
        sta mul_res+1;
        jmp L_sign_fm;
    L_a_hi_mul_fm:
        // result = ah * |b| = ah*bl + (ah*bh << 8)
        lda b;
        sta tmp3;
        lda a+1;
        jsr L_mul8_fm;
        sta mul_res+1;
        lda tmp4;
        sta mul_res;
        lda b+1;
        sta tmp3;
        lda a+1;
        jsr L_mul8_fm;
        lda tmp4;
        clc;
        adc mul_res+1;
        sta mul_res+1;
        jmp L_sign_fm;

    L_a_lo_nonzero_fm:
        lda b;
        bne L_b_lo_nonzero_fm;

        // |b| is a multiple of 256.
        lda b+1;
        bne L_b_hi_nonzero_fm;
        // b == 0
        sta mul_res;
        sta mul_res+1;
        jmp L_done_fm;
    L_b_hi_nonzero_fm:
        cmp #1;
        bne L_b_hi_mul_fm;
        // |b| == 256: result = |a|
        lda a;
        sta mul_res;
        lda a+1;
        sta mul_res+1;
        jmp L_sign_fm;
    L_b_hi_mul_fm:
        // result = bh * |a| = bh*al + (bh*ah << 8)
        sta tmp3;
        lda a;
        jsr L_mul8_fm;
        sta mul_res+1;
        lda tmp4;
        sta mul_res;
        lda a+1;
        jsr L_mul8_fm;
        lda tmp4;
        clc;
        adc mul_res+1;
        sta mul_res+1;
        jmp L_sign_fm;

    L_b_lo_nonzero_fm:
        lda b+1;
        sta tmp3;
        ora a+1;
        bne L_full_fm;
        // Both high bytes zero: result = al*bl >> 8.
        lda b;
        sta tmp3;
        lda a;
        jsr L_mul8_fm;
        sta mul_res;
        lda #0;
        sta mul_res+1;
        jmp L_sign_fm;

    L_full_fm:
        // ah*bh, low byte into the result high byte (tmp3 = bh).
        lda a+1;
        jsr L_mul8_fm;
        lda tmp4;
        sta mul_res+1;
        // al*bh (tmp3 still bh).
        lda a;
        jsr L_mul8_fm;
        tax;
        lda tmp4;
        sta mul_res;
        txa;
        clc;
        adc mul_res+1;
        sta mul_res+1;
        // ah*bl
        lda b;
        sta tmp3;
        lda a+1;
        jsr L_mul8_fm;
        tax;
        lda tmp4;
        clc;
        adc mul_res;
        sta mul_res;
        txa;
        adc mul_res+1;
        sta mul_res+1;
        // al*bl, high byte only (tmp3 still bl).
        lda a;
        jsr L_mul8_fm;
        clc;
        adc mul_res;
        sta mul_res;
        bcc L_sign_fm;
        inc mul_res+1;

    L_sign_fm:
        bit tmp1;
        bpl L_done_fm;
        sec;
        lda #0;
        sbc mul_res;
        sta mul_res;
        lda #0;
        sbc mul_res+1;
        sta mul_res+1;
        jmp L_done_fm;

        // 8x8 -> 16 multiply via the quarter square tables.
        // In: A = x, tmp3 = y. Out: A = high byte, tmp4 = low byte.
        // Clobbers X, Y, tmp2.
    L_mul8_fm:
        sta tmp2;
        sec;
        sbc tmp3;
        bcs L_m_diff_pos_fm;
        eor #$ff;
        adc #$01;
    L_m_diff_pos_fm:
        tax;
        lda tmp2;
        clc;
        adc tmp3;
        tay;
        bcs L_m_sum_hi_fm;
        lda vec_sqr_lo, y;
        sec;
        sbc vec_sqr_lo, x;
        sta tmp4;
        lda vec_sqr_hi, y;
        sbc vec_sqr_hi, x;
        rts;
    L_m_sum_hi_fm:
        lda vec_sqr_lo+256, y;
        sec;
        sbc vec_sqr_lo, x;
        sta tmp4;
        lda vec_sqr_hi+256, y;
        sbc vec_sqr_hi, x;
        rts;

    L_done_fm:
  }
  // clang-format on
  return mul_res;
}

// PERF: inline -> bytes: +290 bytes, cycles: -1000
static inline void _vec_project_internal() {
  // clang-format off
  __asm {
        lda vec_v;
        sta tmp1;
        lda vec_v+1;
        sta tmp2;

    L_normalize_proj:
        lda tmp2;
        beq L_check_left_proj;
    L_shift_right_loop_proj:
        lsr tmp2;
        ror tmp1;
        lsr project_mul_a+1;
        ror project_mul_a;
        lsr project_mul_b+1;
        ror project_mul_b;
        lda tmp2;
        bne L_shift_right_loop_proj;
        beq L_normalized_proj; // already normalized after right shifts
    L_check_left_proj:
        lda tmp1;
        bmi L_normalized_proj; // already >= 128
    L_shift_left_proj:
        asl tmp1;
        asl project_mul_a;
        asl project_mul_b;
        lda tmp1;
        bpl L_shift_left_proj;
    L_normalized_proj:
        
        // Read R = LUT[X_norm] into tmp2
        ldx tmp1;
        lda vec_recip_lut - 128, x;
        sta tmp2;

        // Project Y
        lda vec_v+3;
        sta tmp4;          // top bit has sign of Y
        lda project_mul_a; // Y_norm
        jsr L_do_project_mul;
        sta vec_sx;
        stx vec_sx+1;

        // Project Z
        lda vec_v+5;
        sta tmp4;          // top bit has sign of Z
        lda project_mul_b; // Z_norm
        jsr L_do_project_mul;
        sta vec_sy;
        stx vec_sy+1;

        jmp L_done_proj;

    L_do_project_mul:
        // A = val_norm
        // tmp2 = R
        // tmp4 = original sign (bit 7)
        // returns X=high, A=low of result
        sta tmp3; // save val_norm

        // 8x8 multiply: A * tmp2 -> high byte of product
        sec;
        sbc tmp2;
        bcs L_pos_mul;
        eor #$FF;
        adc #$01;
    L_pos_mul:
        tax; // |A - tmp2|

        clc;
        lda tmp3;
        adc tmp2;
        bcc L_nc_mul;
        tay;
        lda vec_sqr_lo+256, y;
        sec;
        sbc vec_sqr_lo, x;
        // We only need the high byte
        lda vec_sqr_hi+256, y;
        sbc vec_sqr_hi, x;
        jmp L_add_mul;
    L_nc_mul:
        tay;
        lda vec_sqr_lo, y;
        sec;
        sbc vec_sqr_lo, x;
        lda vec_sqr_hi, y;
        sbc vec_sqr_hi, x;

    L_add_mul:
        // A is now EXACT (val_norm * R) >> 8
        clc;
        adc tmp3;
        ldx #0;
        bcc L_apply_sign_mul;
        inx;
        
    L_apply_sign_mul:
        bit tmp4;
        bpl L_done_mul;
        // negate (X, A) -> 0 - (X, A)
        eor #$FF;
        clc;
        adc #$01;
        tay;        // save low byte
        txa;
        eor #$FF;
        adc #$00;
        tax;        // high byte
        tya;        // low byte
    L_done_mul:
        rts;

    L_done_proj:
  }
  // clang-format on
}

// PERF: inline -> +500 bytes, -2000 cycles
inline bool vec_project() {
  if (vec_v.x <= 8) {
    return false;
  }

  project_mul_a = _abs16(vec_v.y);
  project_mul_b = _abs16(vec_v.z);
  if (project_mul_a > vec_v.x || project_mul_b > vec_v.x) {
    return false;
  }

  _vec_project_internal();
  return true;
}

bool vec_project_nocull() {
  if (vec_v.x < 8) {
    return false;
  }

  vec_sx = vec_div8p8(vec_v.y, vec_v.x);
  vec_sy = vec_div8p8(vec_v.z, vec_v.x);
  return true;
}
