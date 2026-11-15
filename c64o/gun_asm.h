#ifndef GUN_ASM_H
#define GUN_ASM_H

#include <stdint.h>

#include "bool.h"
#include "vec.h"

#ifdef __OSCAR64__
static_assert(kOpponentMax == 2 && kGunCollide == 192 && kRoundMax == 8,
              "gun_asm layout constants");

// Steps _gun_eye[0..2] to kFlightEye[0..2] in 32-sub-unit (1/16 m) quanta,
// stores each axis's 1/16 m step into _gun_moved[0..2], and zeroes _gun_t[0..2].
static __noinline void _gun_track_all(void) {
  // clang-format off
  __asm {
        ldx #2;
        stx vec_proj_tmp1;
    L_gt_axis:
        txa;
        asl;
        tay;
        lda kFlightEye, y;
        sta vec_proj_mul_a;
        lda kFlightEye + 1, y;
        sta vec_proj_mul_a + 1;
        tya;
        asl;
        tax;

        ldy #0;
        lda (vec_proj_mul_a), y;
        sec;
        sbc _gun_eye, x;
        and #$e0;
        sta vec_mul8_lo;
        iny;
        lda (vec_proj_mul_a), y;
        sbc _gun_eye + 1, x;
        sta vec_mul8_b;

        ldy #0;
        lda vec_mul8_b;
        bpl L_gt_pos;
        dey;
    L_gt_pos:
        clc;
        lda _gun_eye, x;
        adc vec_mul8_lo;
        sta _gun_eye, x;
        lda _gun_eye + 1, x;
        adc vec_mul8_b;
        sta _gun_eye + 1, x;
        tya;
        adc _gun_eye + 2, x;
        sta _gun_eye + 2, x;
        tya;
        adc _gun_eye + 3, x;
        sta _gun_eye + 3, x;

        tya;
        ldy #3;
    L_gt_sh:
        asl vec_mul8_lo;
        rol vec_mul8_b;
        rol;
        dey;
        bne L_gt_sh;

        ldx vec_proj_tmp1;
        sta _gun_moved + 3, x;
        lda vec_mul8_b;
        sta _gun_moved, x;
        lda #0;
        sta _gun_t, x;
        sta _gun_t + 3, x;
        dex;
        stx vec_proj_tmp1;
        bpl L_gt_axis;
  }
  // clang-format on
}

// Axes c0..2 of _gun_p less _gun_eye, in sixteenths of a metre the short way
// round (x wraps mod 128, y mod 256), into _gun_t[c0..2]; 0 past [-15, 14].
static __noinline uint8_t _gun_rel_from(uint8_t c0) {
  // clang-format off
  __asm {
        lda #0;
        sta vec_proj_tmp1;
        lda c0;
        sta vec_proj_recip;
    L_gr_axis:
        asl;
        asl;
        tax;
        lda _gun_p, x;
        sec;
        sbc _gun_eye, x;
        sta vec_mul8_lo;
        lda _gun_p + 1, x;
        sbc _gun_eye + 1, x;
        sta vec_mul8_b;
        lda _gun_p + 2, x;
        sbc _gun_eye + 2, x;

        ldx vec_proj_recip;
        bne L_gr_not_x;
        asl;
        cmp #$80;
        ror;
    L_gr_not_x:
        clc;
        adc #15;
        cmp #30;
        bcs L_gr_done;
        sbc #14;

        ldy #3;
    L_gr_sh:
        asl vec_mul8_lo;
        rol vec_mul8_b;
        rol;
        dey;
        bne L_gr_sh;

        sta _gun_t + 3, x;
        lda vec_mul8_b;
        sta _gun_t, x;

        inx;
        stx vec_proj_recip;
        txa;
        cmp #3;
        bcc L_gr_axis;
        lda #1;
        sta vec_proj_tmp1;
    L_gr_done:
  }
  // clang-format on
  return vec_proj_tmp1;
}

static inline uint8_t _gun_rel3_to(void) { return _gun_rel_from(0); }
static inline uint8_t _gun_rel2(void) { return _gun_rel_from(2); }

// Four-substep swept AABB test of round r against half-extent `box` around
// _gun_t, with early whole-step reach rejection on each axis.
static __noinline uint8_t _gun_hits(uint8_t r, int16_t box) {
  // clang-format off
  __asm {
        lda #0;
        sta vec_proj_tmp1;
        ldx r;
        ldy #0;
    L_gh_p1:
        lda _round_v, x;
        sta vec_mul8_lo;
        lda flight_step_shift;
        sta vec_proj_recip;
        lda _round_v + 24, x;
    L_gh_v_chk:
        dec vec_proj_recip;
        bmi L_gh_v_done;
        cmp #$80;
        ror;
        ror vec_mul8_lo;
        jmp L_gh_v_chk;
    L_gh_v_done:
        sta vec_proj_mul_a + 1;
        lda vec_mul8_lo;
        sta vec_proj_mul_a;
        lda vec_proj_mul_a + 1;
        jsr L_gh_abs;
        clc;
        pha;
        lda vec_mul8_lo;
        adc box;
        sta vec_proj_mul_b;
        pla;
        adc box + 1;
        sta vec_proj_mul_b + 1;

        lda vec_proj_mul_a + 1;
        cmp #$80;
        ror;
        ror vec_proj_mul_a;
        cmp #$80;
        ror;
        ror vec_proj_mul_a;
        sta _gun_q + 3, y;
        lda vec_proj_mul_a;
        sta _gun_q, y;

        sec;
        lda _gun_t, y;
        sbc _round_p, x;
        sta _gun_d, y;
        sta vec_mul8_lo;
        lda _gun_t + 3, y;
        sbc _round_p + 24, x;
        sta _gun_d + 3, y;
        jsr L_gh_abs;
        sta vec_mul8_b;
        lda vec_proj_mul_b;
        cmp vec_mul8_lo;
        lda vec_proj_mul_b + 1;
        sbc vec_mul8_b;
        bcc L_gh_done;

        txa;
        clc;
        adc #8;
        tax;
        iny;
        cpy #3;
        bcc L_gh_p1;

        ldx #4;
    L_gh_step:
        lda #1;
        sta vec_proj_recip;
        ldy #2;
    L_gh_axis:
        sec;
        lda _gun_d, y;
        sbc _gun_q, y;
        sta _gun_d, y;
        sta vec_mul8_lo;
        lda _gun_d + 3, y;
        sbc _gun_q + 3, y;
        sta _gun_d + 3, y;
        jsr L_gh_abs;
        sta vec_mul8_b;
        lda box;
        cmp vec_mul8_lo;
        lda box + 1;
        sbc vec_mul8_b;
        bcs L_gh_in;
        lda #0;
        sta vec_proj_recip;
    L_gh_in:
        dey;
        bpl L_gh_axis;
        lda vec_proj_recip;
        bne L_gh_hit;
        dex;
        bne L_gh_step;
        beq L_gh_done;
    L_gh_hit:
        sta vec_proj_tmp1;
        bne L_gh_done;

    L_gh_abs:
        ora #0;
        bpl L_gh_abs_pos;
        sta vec_mul8_b;
        sec;
        lda #0;
        sbc vec_mul8_lo;
        sta vec_mul8_lo;
        lda #0;
        sbc vec_mul8_b;
    L_gh_abs_pos:
        rts;

    L_gh_done:
  }
  // clang-format on
  return vec_proj_tmp1;
}

// Opponent-player box collision test at _gun_t and midway from _gun_prev[k..],
// updating _gun_prev[k..] with _gun_t.
static __noinline bool _gun_collides(uint8_t k, bool prev) {
  // clang-format off
  __asm {
        ldx #1;
        lda prev;
        beq L_gc_init;
        ldx #3;
    L_gc_init:
        stx vec_proj_tmp1;
        ldx #0;
        ldy k;
    L_gc_axis:
        lda _gun_t + 3, x;
        sta vec_mul8_b;
        lda _gun_t, x;
        jsr L_gc_out192;
        bcc L_gc_now_ok;
        lda vec_proj_tmp1;
        and #$02;
        sta vec_proj_tmp1;
    L_gc_now_ok:
        lda _gun_t, x;
        clc;
        adc _gun_prev, y;
        sta vec_proj_recip;
        lda _gun_t + 3, x;
        adc _gun_prev + 6, y;
        cmp #$80;
        ror;
        sta vec_mul8_b;
        lda _gun_t, x;
        sta _gun_prev, y;
        lda _gun_t + 3, x;
        sta _gun_prev + 6, y;
        lda vec_proj_recip;
        ror;
        jsr L_gc_out192;
        bcc L_gc_mid_ok;
        lda vec_proj_tmp1;
        and #$01;
        sta vec_proj_tmp1;
    L_gc_mid_ok:
        iny;
        iny;
        inx;
        cpx #3;
        bcc L_gc_axis;
        beq L_gc_done;

    L_gc_out192:
        clc;
        adc #$c0;
        pha;
        lda vec_mul8_b;
        adc #0;
        sta vec_mul8_b;
        pla;
        cmp #$81;
        lda vec_mul8_b;
        sbc #1;
        rts;

    L_gc_done:
  }
  // clang-format on
  return (bool)vec_proj_tmp1;
}
#endif

#endif
