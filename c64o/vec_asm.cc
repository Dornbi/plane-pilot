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

// These are used for temporary storage.
static __zeropage uint8_t tmp1, tmp2, tmp3, tmp4;
static __zeropage int16_t project_mul_a;
static __zeropage int16_t project_mul_b;

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
