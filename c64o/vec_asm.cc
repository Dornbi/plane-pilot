#include "vec.h"

#include <stddef.h>
#include <stdlib.h>

#include "fmath.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

extern const uint8_t vec_recip_lut[];
extern const uint8_t vec_sqr_lo[];
extern const uint8_t vec_sqr_hi[];

#ifndef __OSCAR64__
#define __zeropage
#endif

__zeropage vec3_t vec_v;
__zeropage int16_t vec_sx;
__zeropage int16_t vec_sy;

// These are used for temporary storage. vec_fastmul8p8 reuses tmp1, tmp2 and
// vec_mul8's two; they are never live across calls (single-threaded, the
// raster IRQs do no vector math).
__zeropage uint8_t vec_proj_tmp1, vec_proj_recip;
__zeropage uint8_t vec_mul8_b, vec_mul8_lo;
__zeropage int16_t vec_proj_mul_a;
__zeropage int16_t vec_proj_mul_b;
#define tmp1 vec_proj_tmp1
#define tmp2 vec_proj_recip
#define project_mul_a vec_proj_mul_a
#define project_mul_b vec_proj_mul_b
__zeropage int16_t vec_mul_res;
#define mul_res vec_mul_res

// Unsigned 8 x 8 -> 16 multiply on the quarter-square tables, for assembly:
// A times vec_mul8_b, returning the high byte in A and the low byte in
// vec_mul8_lo. Clobbers X, Y and tmp2. vec_fastmul8p8 builds its products
// from it, and planes.cc calls it directly.
void vec_mul8(void) {
  // clang-format off
  __asm {
        sta tmp2;
        sec;
        sbc vec_mul8_b;
        bcs L_diff_pos_m8;
        eor #$ff;
        adc #$01;
    L_diff_pos_m8:
        tax;
        lda tmp2;
        clc;
        adc vec_mul8_b;
        tay;
        bcs L_sum_hi_m8;
        lda vec_sqr_lo, y;
        sec;
        sbc vec_sqr_lo, x;
        sta vec_mul8_lo;
        lda vec_sqr_hi, y;
        sbc vec_sqr_hi, x;
        rts;
    L_sum_hi_m8:
        lda vec_sqr_lo+256, y;
        sec;
        sbc vec_sqr_lo, x;
        sta vec_mul8_lo;
        lda vec_sqr_hi+256, y;
        sbc vec_sqr_hi, x;
        rts;
  }
  // clang-format on
}

// a * b for bytes, exactly.
uint16_t vec_mul8x8(uint8_t a, uint8_t b) {
  // clang-format off
  __asm {
        lda b;
        sta vec_mul8_b;
        lda a;
        jsr vec_mul8;
        sta mul_res+1;
        lda vec_mul8_lo;
        sta mul_res;
  }
  // clang-format on
  return (uint16_t)mul_res;
}

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
//
// Partial products that are zero are not computed: none for an operand of 0
// or +-256, two for another multiple of 256, and two for an |a| below 256,
// or one when |b| is below 256 as well. The short operand, a camera row's
// component or a byte of a fraction, is a in vec_dot(), vec_mulfrac() and
// most other callers, so only a is tested.
// Shared 8.8 multiply core for assembly callers (vec_asm.cc, opponent_asm.cc):
// vec_proj_mul_a * vec_proj_mul_b -> vec_mul_res (exact trunc((a * b) / 256),
// wrapping to 16 bits on overflow). Fast-paths a in {0, +256, -256} and b in
// {0, +256} before any sign/abs work.
// Preserves: vec_v, vec_sx, vec_sy, frac_q0..frac_d1.
__noinline void vec_mul_ab(void) {
  // clang-format off
  __asm {
        // Fast-path a == 0, +256, -256 before taking |a| or |b|.
        lda project_mul_a;
        bne L_a_lo_nz_fm;
        lda project_mul_a + 1;
        beq L_zero_fm;
        cmp #1;
        bne L_a_m256_fm;
        lda project_mul_b;
        sta mul_res;
        lda project_mul_b + 1;
        sta mul_res + 1;
        rts;
    L_a_m256_fm:
        cmp #$ff;
        bne L_sign_abs_fm;
        sec;
        lda #0;
        sbc project_mul_b;
        sta mul_res;
        lda #0;
        sbc project_mul_b + 1;
        sta mul_res + 1;
        rts;
    L_zero_fm:
        sta mul_res;
        sta mul_res + 1;
        rts;

    L_a_lo_nz_fm:
        // Fast-path b == 0, +256 before taking |a| or |b|.
        lda project_mul_b;
        bne L_sign_abs_fm;
        lda project_mul_b + 1;
        beq L_zero_fm;
        cmp #1;
        bne L_sign_abs_fm;
        lda project_mul_a;
        sta mul_res;
        lda project_mul_a + 1;
        sta mul_res + 1;
        rts;

    L_sign_abs_fm:
        // Sign of the result in bit 7 of tmp1.
        lda project_mul_a + 1;
        eor project_mul_b + 1;
        sta tmp1;

        // a = |a|
        lda project_mul_a + 1;
        bpl L_abs_a_done_fm;
        sec;
        lda #0;
        sbc project_mul_a;
        sta project_mul_a;
        lda #0;
        sbc project_mul_a + 1;
        sta project_mul_a + 1;
    L_abs_a_done_fm:
        // b = |b|
        lda project_mul_b + 1;
        bpl L_abs_b_done_fm;
        sec;
        lda #0;
        sbc project_mul_b;
        sta project_mul_b;
        lda #0;
        sbc project_mul_b + 1;
        sta project_mul_b + 1;
    L_abs_b_done_fm:

        ldx project_mul_b + 1;
        lda project_mul_a + 1;
        bne L_full_fm;
        // |a| < 256: ah's two products are 0.
        txa;
        beq L_al_bh_fm;
        stx vec_mul8_b;
        lda project_mul_a;
        jsr vec_mul8;
        ldx vec_mul8_lo;
    L_al_bh_fm:
        sta mul_res + 1;
        stx mul_res;
        lda project_mul_b;
        beq L_sign_fm;
        sta vec_mul8_b;
        jmp L_al_bl_fm;

    L_full_fm:
        // ah*bh, low byte into the result high byte (A = ah, X = bh).
        stx vec_mul8_b;
        jsr vec_mul8;
        lda vec_mul8_lo;
        sta mul_res + 1;
        // al*bh (vec_mul8_b still bh).
        lda project_mul_a;
        jsr vec_mul8;
        tax;
        lda vec_mul8_lo;
        sta mul_res;
        txa;
        clc;
        adc mul_res + 1;
        sta mul_res + 1;
        // ah*bl
        lda project_mul_b;
        sta vec_mul8_b;
        lda project_mul_a + 1;
        jsr vec_mul8;
        tax;
        lda vec_mul8_lo;
        clc;
        adc mul_res;
        sta mul_res;
        txa;
        adc mul_res + 1;
        sta mul_res + 1;
        // al*bl, high byte only (vec_mul8_b still bl).
    L_al_bl_fm:
        lda project_mul_a;
        jsr vec_mul8;
        clc;
        adc mul_res;
        sta mul_res;
        bcc L_sign_fm;
        inc mul_res + 1;

    L_sign_fm:
        bit tmp1;
        bpl L_done_fm;
        sec;
        lda #0;
        sbc mul_res;
        sta mul_res;
        lda #0;
        sbc mul_res + 1;
        sta mul_res + 1;

    L_done_fm:
  }
  // clang-format on
}

__noinline int16_t vec_fastmul8p8(int16_t a, int16_t b) {
  // clang-format off
  __asm {
        lda a;
        sta project_mul_a;
        lda a + 1;
        sta project_mul_a + 1;
        lda b;
        sta project_mul_b;
        lda b + 1;
        sta project_mul_b + 1;
        jsr vec_mul_ab;
  }
  // clang-format on
  return mul_res;
}

__noinline uint16_t vec_fastsqr8p8(int16_t a) {
  // clang-format off
  __asm {
        lda a;
        sta project_mul_a;
        sta project_mul_b;
        lda a + 1;
        sta project_mul_a + 1;
        sta project_mul_b + 1;
        jsr vec_mul_ab;
  }
  // clang-format on
  return (uint16_t)mul_res;
}

void vec_project_mul(void) {
  // clang-format off
  __asm {
        // A = val_norm
        // tmp2 = R
        // vec_mul8_lo = original sign (bit 7)
        // returns X=high, A=low of result
        sta vec_mul8_b; // save val_norm

        // 8x8 multiply: A * tmp2 -> high byte of product
        sec;
        sbc tmp2;
        bcs L_pos_mul;
        eor #$FF;
        adc #$01;
    L_pos_mul:
        tax; // |A - tmp2|

        clc;
        lda vec_mul8_b;
        adc tmp2;
        tay;
        bcc L_nc_mul;
        lda vec_sqr_lo+256, y;
        sec;
        sbc vec_sqr_lo, x;
        // We only need the high byte
        lda vec_sqr_hi+256, y;
        sbc vec_sqr_hi, x;
        jmp L_add_mul;
    L_nc_mul:
        lda vec_sqr_lo, y;
        sec;
        sbc vec_sqr_lo, x;
        lda vec_sqr_hi, y;
        sbc vec_sqr_hi, x;

    L_add_mul:
        // A is now EXACT (val_norm * R) >> 8
        clc;
        adc vec_mul8_b;
        ldx #0;
        bcc L_apply_sign_mul;
        inx;
        
    L_apply_sign_mul:
        bit vec_mul8_lo;
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
  }
  // clang-format on
}

// PERF: inline -> bytes: +290 bytes, cycles: -1000
static inline void _vec_project_internal() {
  // clang-format off
  __asm {
        lda vec_v;
        sta tmp1;
        lda vec_v+1;
        sta tmp2;
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
        asl project_mul_a;
        asl project_mul_b;
        asl tmp1;
        bpl L_shift_left_proj;
    L_normalized_proj:
        
        // Read R = LUT[X_norm] into tmp2
        ldx tmp1;
        lda vec_recip_lut - 128, x;
        sta tmp2;

        // Project Y
        lda vec_v+3;
        sta vec_mul8_lo;          // top bit has sign of Y
        lda project_mul_a; // Y_norm
        jsr vec_project_mul;
        sta vec_sx;
        stx vec_sx+1;

        // Project Z
        lda vec_v+5;
        sta vec_mul8_lo;          // top bit has sign of Z
        lda project_mul_b; // Z_norm
        jsr vec_project_mul;
        sta vec_sy;
        stx vec_sy+1;
  }
  // clang-format on
}

// PERF: inline -> +500 bytes, -2000 cycles
inline bool vec_project() {
  if (vec_v.x <= 8) {
    return false;
  }

  // Compared unsigned: _abs16(-32768) is -32768, which a signed compare
  // would let through as in range.
  project_mul_a = _abs16(vec_v.y);
  if ((uint16_t)project_mul_a > (uint16_t)vec_v.x) {
    return false;
  }
  project_mul_b = _abs16(vec_v.z);
  if ((uint16_t)project_mul_b > (uint16_t)vec_v.x) {
    return false;
  }

  _vec_project_internal();
  return true;
}

// vec_project_viewport() in its three steps, each needing one more component
// of vec_v than the one before, so the grid adds y and z only for a point the
// earlier steps let through.
//
// The last step also narrows the sides to |y| <= x - x / 4, the viewport's
// 320 pixels rather than the cone's 512: every point it turns away would
// project to |vec_sx| >= 190, past the plot's (-160, 160] (checked for every
// x from 9 up against vec_recip_lut), and a projection is about 400 cycles.
// It is here and not in vec_viewport_beside() because it needs x / 4, which
// the vertical test has to compute anyway: tested there, it cost every point
// ahead of the camera more than it saved.
inline bool vec_viewport_ahead() { return vec_v.x > 8; }

inline bool vec_viewport_beside() {
  project_mul_a = _abs16(vec_v.y);
  return (uint16_t)project_mul_a <= (uint16_t)vec_v.x;
}

inline bool vec_viewport_project() {
  project_mul_b = _abs16(vec_v.z);
  const uint16_t x = (uint16_t)vec_v.x;
  const uint16_t q = x >> 2;
  if ((uint16_t)project_mul_b > q) {
    return false;
  }
  if ((uint16_t)project_mul_a > (uint16_t)(x - q)) {
    return false;
  }

  _vec_project_internal();
  return true;
}

// One out-of-line copy for everything but the grid: the horizon and the sun in
// world.cc, the cloud blobs, and vec_project_nocull. Only the grid points keep
// projection inline; each inlined copy is about 270 bytes.
__noinline bool vec_project_once() { return vec_project(); }

// Restoring division, one iteration per quotient bit. For |a| < |b| <= 0x8000,
// vec_frac8 produces 8 quotient bits in frac_q0 using a sentinel bit rotated
// out on the eighth iteration, keeping r0 in A across iterations and taking a
// single-byte loop whenever d < 256; |a| >= |b| saturates in vec_frac16 /
// vec_fracn, and shifts integer bits first in vec_div8p8.
__zeropage uint8_t frac_q0, frac_q1, frac_r0, frac_r1;
__zeropage uint8_t frac_d0, frac_d1;

// Eight fractional bits of (frac_r << 8) / frac_d into frac_q0, with
// 0 <= frac_r < frac_d <= 0x8000, updating frac_r to the new remainder and
// preserving frac_d, frac_q1, and tmp1. JSR to it; not callable from C.
void vec_frac8(void) {
  // clang-format off
  __asm {
        lda frac_r0;
        ora frac_r1;
        beq L_f8_zero;
        lda #1;
        sta frac_q0;
        lda frac_r0;
        ldx frac_d1;
        bne L_f8_16;
    L_f8_8:
        asl;
        bcs L_f8_8_sub;
        cmp frac_d0;
        bcc L_f8_8_next;
    L_f8_8_sub:
        sbc frac_d0;
        sec;
    L_f8_8_next:
        rol frac_q0;
        bcc L_f8_8;
        sta frac_r0;
        rts;
    L_f8_zero:
        sta frac_q0;
        rts;
    L_f8_16:
        asl;
        rol frac_r1;
        tax;
        sec;
        sbc frac_d0;
        tay;
        lda frac_r1;
        sbc frac_d1;
        bcc L_f8_16_no;
        sta frac_r1;
        tya;
        rol frac_q0;
        bcc L_f8_16;
        sta frac_r0;
        rts;
    L_f8_16_no:
        txa;
        rol frac_q0;
        bcc L_f8_16;
        sta frac_r0;
        rts;
  }
  // clang-format on
}

void vec_frac_setup(void) {
  // clang-format off
  __asm {
        sta frac_r1;
        sty frac_d1;
        eor frac_d1;
        sta tmp1;
        lda frac_r1;
        bpl L_a_pos_fs;
        sec;
        lda #0;
        sbc frac_r0;
        sta frac_r0;
        lda #0;
        sbc frac_r1;
        sta frac_r1;
    L_a_pos_fs:
        lda frac_d1;
        bpl L_b_pos_fs;
        sec;
        lda #0;
        sbc frac_d0;
        sta frac_d0;
        lda #0;
        sbc frac_d1;
        sta frac_d1;
    L_b_pos_fs:
        lda #0;
        sta frac_q0;
        sta frac_q1;
        rts;
  }
  // clang-format on
}

void vec_frac16_run(void) {
  // clang-format off
  __asm {
        jsr vec_frac_setup;
        lda frac_r0;
        cmp frac_d0;
        lda frac_r1;
        sbc frac_d1;
        bcc L_f16_ok;
        lda #$ff;
        sta frac_q0;
        sta frac_q1;
        rts;
    L_f16_ok:
        jsr vec_frac8;
        lda frac_q0;
        sta frac_q1;
        jmp vec_frac8;
  }
  // clang-format on
}

uint16_t vec_frac16(int16_t a, int16_t b) {
  // clang-format off
  __asm {
        lda a;
        sta frac_r0;
        lda b;
        sta frac_d0;
        lda a + 1;
        ldy b + 1;
        jsr vec_frac16_run;
  }
  // clang-format on
  return ((uint16_t)frac_q1 << 8) | frac_q0;
}

uint16_t vec_fracn(int16_t a, int16_t b, uint8_t bits) {
  // clang-format off
  __asm {
        lda a;
        sta frac_r0;
        lda b;
        sta frac_d0;
        lda a + 1;
        ldy b + 1;
        jsr vec_frac_setup;
        ldx bits;
        lda frac_r0;
        cmp frac_d0;
        lda frac_r1;
        sbc frac_d1;
        bcc L_fn_lt;
    L_fn_ones:
        sec;
        rol frac_q0;
        rol frac_q1;
        dex;
        bne L_fn_ones;
        beq L_fn_done;
    L_fn_lt:
        cpx #8;
        bcc L_fn_rem;
        txa;
        sbc #8;
        pha;
        jsr vec_frac8;
        pla;
        beq L_fn_done;
        tax;
    L_fn_rem:
        asl frac_q0;
        rol frac_q1;
        asl frac_r0;
        rol frac_r1;
        lda frac_r0;
        sec;
        sbc frac_d0;
        tay;
        lda frac_r1;
        sbc frac_d1;
        bcc L_fn_skip;
        sty frac_r0;
        sta frac_r1;
        inc frac_q0;
    L_fn_skip:
        dex;
        bne L_fn_rem;
    L_fn_done:
  }
  // clang-format on
  return ((uint16_t)frac_q1 << 8) | frac_q0;
}

// x / y in 8.8, exactly. For |a| < |b| it is eight fraction bits from
// vec_frac8; for |a| >= |b| it shifts |a|'s low bits into frac_q1 (at most
// seven: an eighth integer bit is a quotient of 128 or more, which saturates),
// runs those integer restoring steps into frac_q1, and finishes the eight
// fraction bits through vec_frac8.
//
// The remainder never overflows: it stays below |b| <= 32768, so doubling it
// fits sixteen bits. The sign goes on at the end, in here, where the C tail
// that oscar64 from 1616138 on gets wrong (bugs/negate-tail-high-byte) cannot
// reach it.
int16_t vec_div8p8(int16_t a, int16_t b) {
  // clang-format off
  __asm {
        lda a;
        sta frac_r0;
        lda b;
        sta frac_d0;
        lda a + 1;
        ldy b + 1;
        jsr vec_frac_setup;

        // |b| == 64: the quotient is |a| << 2, saturating from 8192 up just as
        // the loop would, so no loop. It is the divisor of every vertex the
        // near clip lifts onto its plane (vec_project_nocull()), which paid
        // about 770 cycles here, twice a vertex.
        lda frac_d0;
        eor #64;
        ora frac_d1;
        bne L_any_dv;
        lda frac_r1;
        cmp #$20;
        bcs L_sat_dv;
        asl frac_r0;
        rol;
        asl frac_r0;
        rol;
        sta frac_q1;
        lda frac_r0;
        sta frac_q0;
        bcc L_sign_dv;

    L_any_dv:
        // |a| < |b|: a pure fraction, eight iterations in vec_frac8.
        lda frac_r0;
        cmp frac_d0;
        lda frac_r1;
        sbc frac_d1;
        bcc L_frac_dv;

        // b == 0: a saturates, and 0 / 0 is 0.
        lda frac_d0;
        ora frac_d1;
        bne L_int_init_dv;
        lda frac_r0;
        ora frac_r1;
        beq L_done_dv;
        bne L_sat_dv;

    L_int_init_dv:
        ldx #0;
    L_int_dv:
        lsr frac_r1;
        ror frac_r0;
        ror frac_q1;
        inx;
        lda frac_r0;
        cmp frac_d0;
        lda frac_r1;
        sbc frac_d1;
        bcc L_int_run_dv;
        cpx #7;
        bcc L_int_dv;

    L_sat_dv:
        lda #$ff;
        sta frac_q0;
        lda #$7f;
        sta frac_q1;
        bne L_sign_dv;

    L_int_run_dv:
        asl frac_q1;
        rol frac_r0;
        rol frac_r1;
        lda frac_r0;
        sec;
        sbc frac_d0;
        tay;
        lda frac_r1;
        sbc frac_d1;
        bcc L_int_skip_dv;
        sty frac_r0;
        sta frac_r1;
        inc frac_q1;
    L_int_skip_dv:
        dex;
        bne L_int_run_dv;

    L_frac_dv:
        jsr vec_frac8;

    L_sign_dv:
        bit tmp1;
        bpl L_done_dv;
        sec;
        lda #0;
        sbc frac_q0;
        sta frac_q0;
        lda #0;
        sbc frac_q1;
        sta frac_q1;
    L_done_dv:
  }
  // clang-format on
  return (int16_t)(((uint16_t)frac_q1 << 8) | frac_q0);
}

// t * d / 65536 = (t_hi * d) / 256 + (t_lo * d) / 65536, both halves through
// the 8.8 multiply that is already here rather than a second 16x16 routine.
// The bias rounds the low half. The high half is left with vec_fastmul8p8's
// truncation toward zero, and that turns out not to matter: replacing this
// whole routine with an exact 32-bit (a * b) / c moves test/poly_test.cc's
// sweep from 7.75 wrongly filled sub-pixels per clipped polygon to 7.64, and
// the worst case not at all. What is left of the error is in the projection
// - vec_div8p8 and the rounding of the /4 - not in the clip lerp.
int16_t vec_mulfrac(uint16_t t, int16_t d) {
  int16_t hi = vec_fastmul8p8((int16_t)(t >> 8), d);
  int16_t lo = vec_fastmul8p8((int16_t)(t & 0xFF), d);
  return hi + (int16_t)((lo + 128) >> 8);
}

// A vertex inside the view cone goes through the reciprocal table like a grid
// point, about 480 cycles; only one outside it, which the table cannot take,
// pays for the two exact divisions, about 1,230. The table is within 2 of
// trunc(256 * y / x), half a sub-pixel once poly.cc quarters it.
//
// The vertices the near clip makes are always outside the cone, and sit at
// x = 64 whenever the clip could lift them (poly.cc _clip_near_edge()): that
// is the divisor vec_div8p8 answers with a shift, about 110 cycles a call.
//
// No test for x < 8: the near clip is the only source of these vertices and
// passes nothing in front of its plane.
void vec_project_nocull() {
  if (vec_project_once()) {
    return;
  }

  vec_sx = vec_div8p8(vec_v.y, vec_v.x);
  vec_sy = vec_div8p8(vec_v.z, vec_v.x);
}

static_assert(sizeof(vec3_t) == 6 && offsetof(mat3_t, front) == 0 &&
                  offsetof(mat3_t, left) == 6 && offsetof(mat3_t, up) == 12 &&
                  sizeof(mat3_t) == 18 && VEC_STEP_PITCH_UP == 2 &&
                  VEC_STEP_YAW_LEFT == 4,
              "vec_asm layout assumptions");

// 8.8 fixed-point dot product of a and b. Preserves frac_d0, frac_d1, frac_q1
// (gun_asm.cc _gun_view_vec holds its divisor and axis index there across calls)
// as well as vec_v, vec_sx, vec_sy.
__noinline int16_t vec_dot(const vec3_t *a, const vec3_t *b) {
  // clang-format off
  __asm {
        lda #0;
        sta frac_r0;
        sta frac_r1;
        sta frac_q0;
    L_vd_loop:
        ldy frac_q0;
        lda (a), y;
        sta project_mul_a;
        lda (b), y;
        sta project_mul_b;
        iny;
        lda (a), y;
        sta project_mul_a + 1;
        lda (b), y;
        sta project_mul_b + 1;
        iny;
        sty frac_q0;
        jsr vec_mul_ab;
        clc;
        lda mul_res;
        adc frac_r0;
        sta frac_r0;
        lda mul_res + 1;
        adc frac_r1;
        sta frac_r1;
        lda frac_q0;
        cmp #6;
        bcc L_vd_loop;
        lda frac_r0;
        sta mul_res;
        lda frac_r1;
        sta mul_res + 1;
  }
  // clang-format on
  return mul_res;
}

// Inverted matrix transform: res = (mat->front . u, mat->left . u, mat->up . u).
// Safe when res aliases u or mat.
__noinline void vec_transform_inv(const mat3_t *mat, const vec3_t *u,
                                  vec3_t *res) {
  // clang-format off
  __asm {
        lda #0;
        sta frac_q1;            // mat byte offset: 0..17
    L_ti_row:
        lda #0;
        sta frac_r0;
        sta frac_r1;
        sta frac_q0;            // u byte offset: 0, 2, 4
    L_ti_comp:
        ldy frac_q1;
        lda (mat), y;
        sta project_mul_a;
        iny;
        lda (mat), y;
        sta project_mul_a + 1;
        iny;
        sty frac_q1;
        ldy frac_q0;
        lda (u), y;
        sta project_mul_b;
        iny;
        lda (u), y;
        sta project_mul_b + 1;
        iny;
        sty frac_q0;
        jsr vec_mul_ab;
        clc;
        lda mul_res;
        adc frac_r0;
        sta frac_r0;
        lda mul_res + 1;
        adc frac_r1;
        sta frac_r1;
        lda frac_q0;
        cmp #6;
        bcc L_ti_comp;

        lda frac_q1;
        cmp #18;
        bcs L_ti_store;
        lda frac_r0;
        pha;
        lda frac_r1;
        pha;
        jmp L_ti_row;

    L_ti_store:
        ldy #4;
        lda frac_r0;
        sta (res), y;
        iny;
        lda frac_r1;
        sta (res), y;
        ldy #3;
    L_ti_pop:
        pla;
        sta (res), y;
        dey;
        bpl L_ti_pop;
  }
  // clang-format on
}

static const uint8_t kVecCrossA[3] = {2, 4, 0};
static const uint8_t kVecCrossB[3] = {4, 0, 2};

// Cross product res = u x v in 8.8 fixed point.
__noinline void vec_cross(const vec3_t *u, const vec3_t *v, vec3_t *res) {
  // clang-format off
  __asm {
        lda #0;
        sta frac_q0;            // k = 0, 1, 2
        sta frac_q1;            // out_off = 0, 2, 4
    L_vc_loop:
        ldx frac_q0;
        ldy kVecCrossA, x;
        lda (u), y;
        sta project_mul_a;
        iny;
        lda (u), y;
        sta project_mul_a + 1;
        ldy kVecCrossB, x;
        lda (v), y;
        sta project_mul_b;
        iny;
        lda (v), y;
        sta project_mul_b + 1;
        jsr vec_mul_ab;
        lda mul_res;
        sta frac_r0;
        lda mul_res + 1;
        sta frac_r1;

        ldx frac_q0;
        ldy kVecCrossB, x;
        lda (u), y;
        sta project_mul_a;
        iny;
        lda (u), y;
        sta project_mul_a + 1;
        ldy kVecCrossA, x;
        lda (v), y;
        sta project_mul_b;
        iny;
        lda (v), y;
        sta project_mul_b + 1;
        jsr vec_mul_ab;

        ldy frac_q1;
        sec;
        lda frac_r0;
        sbc mul_res;
        sta (res), y;
        iny;
        lda frac_r1;
        sbc mul_res + 1;
        sta (res), y;
        iny;
        sty frac_q1;
        inc frac_q0;
        lda frac_q0;
        cmp #3;
        bcc L_vc_loop;
  }
  // clang-format on
}

// Small-angle step rotation on two rows of m: p[i] += s*q[i], q[i] -= s*p[i].
__noinline void vec_rotate_step(uint8_t step, mat3_t *m) {
  // clang-format off
  __asm {
        ldx step;
        lda #0;
        cpx #2;                 // VEC_STEP_PITCH_UP
        bcs L_vr_p0;
        lda #6;
    L_vr_p0:
        sta frac_q0;            // p_off
        lda #12;
        cpx #4;                 // VEC_STEP_YAW_LEFT
        bcc L_vr_q0;
        lda #6;
    L_vr_q0:
        sta frac_q1;            // q_off
        lda _vec_step, x;
        sta frac_d0;            // s lo
        ldy #0;
        cmp #0;
        bpl L_vr_spos;
        dey;
    L_vr_spos:
        sty frac_d1;            // s hi (sign-extended)
        lda #3;
        sta frac_r0;            // count

    L_vr_loop:
        // Save original a = m[p_off] on stack, compute s * q.
        ldy frac_q0;
        lda (m), y;
        pha;
        iny;
        lda (m), y;
        pha;
        ldy frac_q1;
        lda (m), y;
        sta project_mul_b;
        iny;
        lda (m), y;
        sta project_mul_b + 1;
        lda frac_d0;
        sta project_mul_a;
        lda frac_d1;
        sta project_mul_a + 1;
        jsr vec_mul_ab;         // mul_res = s * b

        // m[p_off] = a + mul_res (original a is still in m[p_off])
        ldy frac_q0;
        clc;
        lda (m), y;
        adc mul_res;
        sta (m), y;
        iny;
        lda (m), y;
        adc mul_res + 1;
        sta (m), y;
        iny;
        sty frac_q0;

        // Compute s * original a (popped from stack)
        pla;
        sta project_mul_b + 1;
        pla;
        sta project_mul_b;
        lda frac_d0;
        sta project_mul_a;
        lda frac_d1;
        sta project_mul_a + 1;
        jsr vec_mul_ab;         // mul_res = s * a

        // m[q_off] -= mul_res
        ldy frac_q1;
        sec;
        lda (m), y;
        sbc mul_res;
        sta (m), y;
        iny;
        lda (m), y;
        sbc mul_res + 1;
        sta (m), y;
        iny;
        sty frac_q1;

        dec frac_r0;
        bne L_vr_loop;
  }
  // clang-format on
}
#pragma optimize(pop)
