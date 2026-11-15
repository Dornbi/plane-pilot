#ifndef OPPONENT_ASM_H
#define OPPONENT_ASM_H

#include <stdint.h>

#include "vec.h"

enum opponent_range_t { kOpponentNear, kOpponentFar, kOpponentOut };
static_assert(kOpponentNear == 0 && kOpponentFar == 1 && kOpponentOut == 2,
              "world delta range constants");

#ifdef __OSCAR64__
#ifndef __TARGET_TEST__
#pragma bss(tscratch)
#endif
static uint8_t _wd_d0[5];
#ifndef __TARGET_TEST__
#pragma bss(bss)
#endif

// World-space offset from the eye to slot i in vec_v, in eighth metres
// (kOpponentNear) or half metres (kOpponentFar), or kOpponentOut past 32 units.
static __noinline uint8_t _opponent_world_delta(uint8_t i) {
  // clang-format off
  __asm {
        lda i;
        asl;
        asl;
        tax;
        lda #0;
        sta vec_proj_tmp1;      // range = kOpponentNear
        sta vec_proj_recip;     // yc = 0 (0, 2, 4)
    L_wd_axis:
        ldy vec_proj_recip;
        lda kFlightEye, y;
        sta vec_proj_mul_a;
        lda kFlightEye + 1, y;
        sta vec_proj_mul_a + 1;

        ldy #0;
        lda _opponent_xyz, x;
        sec;
        sbc (vec_proj_mul_a), y;
        sta vec_mul8_b;         // d0
        iny;
        lda _opponent_xyz + 1, x;
        sbc (vec_proj_mul_a), y;
        sta vec_mul8_lo;        // d1
        iny;
        lda _opponent_xyz + 2, x;
        sbc (vec_proj_mul_a), y; // u = d2

        ldy vec_proj_recip;
        bne L_wd_not_x;
        asl;
        cmp #$80;
        ror;
    L_wd_not_x:
        sta vec_v + 1, y;
        clc;
        adc #31;
        cmp #63;
        bcs L_wd_out;
        cmp #16;
        bcc L_wd_far;
        cmp #47;
        bcc L_wd_store;
    L_wd_far:
        lda #1;                 // kOpponentFar
        sta vec_proj_tmp1;
    L_wd_store:
        lda vec_mul8_b;
        sta _wd_d0, y;
        lda vec_mul8_lo;
        sta vec_v, y;
        txa;
        clc;
        adc #8;
        tax;
        iny;
        iny;
        sty vec_proj_recip;
        cpy #6;
        bcc L_wd_axis;

        lda vec_proj_tmp1;
        bne L_wd_done;
        ldx #4;
    L_wd_near:
        lda _wd_d0, x;
        asl;
        rol vec_v, x;
        rol vec_v + 1, x;
        asl;
        rol vec_v, x;
        rol vec_v + 1, x;
        dex;
        dex;
        bpl L_wd_near;
        jmp L_wd_done;

    L_wd_out:
        lda #2;                 // kOpponentOut
        sta vec_proj_tmp1;
    L_wd_done:
  }
  // clang-format on
  return vec_proj_tmp1;
}
#endif

#endif
