#include "opponent_asm.h"

#include <stddef.h>
#include <stdint.h>

// The assembly half of opponent.cc (opponent_asm.h): shared opponent state in
// bss2 and tscratch, plus 6502 routines for world-to-eye wrapping/scaling,
// waypoint/eye half-metre deltas, 3-axis position reads, struct copies, and
// 6-bit steering reduction + 2D heading cross/dot products.

#if defined(__OSCAR64__) && defined(__TARGET_TEST__)
#pragma section(opp_bss, 0, , , bss)
#pragma bss(opp_bss)
#elif defined(__FPILOT__)
#pragma bss(bss2)
#endif
opponent_fresh_t _opponent_fresh;
int32_t _opponent_xyz[6];
planes_state_t _opponent_planes[2];
planes_layout_t _opponent_shown[2];

#if defined(__OSCAR64__) && defined(__TARGET_TEST__)
#pragma bss(opp_bss)
#elif defined(__FPILOT__)
#pragma bss(tscratch)
#endif
vec3_t _opponent_c;
vec3_t _opponent_cs[2];
#ifdef __OSCAR64__
static uint8_t _wd_d0[5];
#endif
#if defined(__FPILOT__) || (defined(__OSCAR64__) && defined(__TARGET_TEST__))
#pragma bss(bss)
#endif

#ifdef __OSCAR64__

#ifndef __TARGET_TEST__
// Per-frame / per-step path: the outliner (-Oo) would trade cycles for bytes.
#pragma optimize(push, nooutline)
#endif

static_assert(sizeof(_opponent_xyz[0]) == 4, "world delta stride");
static_assert(offsetof(opponent_fresh_t, head) == 0 && sizeof(vec3_t) == 6,
              "opponent_fresh_t head layout");
static_assert(offsetof(planes_state_t, lay) == 0 &&
                  sizeof(planes_state_t) == 84 && sizeof(planes_layout_t) == 8,
              "planes_state_t / planes_layout_t layout");

// World-space offset from the eye to slot i in vec_v, in eighth metres
// (kOpponentNear) or half metres (kOpponentFar), or kOpponentOut past 32 units.
uint8_t _opponent_world_delta(uint8_t i) {
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

// Waypoint or eye half-metre coordinate w (8.8 units) less _opponent_xyz[idx]
// (24.8), wrapped to +-16,384 half metres (bit 22 sign-extended into bit 23).
int16_t _opponent_toward(uint16_t w, uint8_t idx) {
  // clang-format off
  __asm {
        lda idx;
        asl;
        asl;
        tax;
        lda #0;
        cmp _opponent_xyz, x;
        lda w;
        sbc _opponent_xyz + 1, x;
        sta vec_proj_mul_a;
        lda w + 1;
        sbc _opponent_xyz + 2, x;
        asl;
        cmp #$80;
        ror;
        sta vec_proj_mul_a + 1;
  }
  // clang-format on
  return vec_proj_mul_a;
}

// Copies slot i's three 24.8 positions (_opponent_xyz[i, i+2, i+4]) into p[0..2].
void _opponent_pos(uint8_t i, int32_t *p) {
  vec_proj_mul_a = (int16_t)p;
  // clang-format off
  __asm {
        lda i;
        asl;
        asl;
        tax;
        ldy #0;
    L_pos_byte:
        lda _opponent_xyz, x;
        sta (vec_proj_mul_a), y;
        inx;
        iny;
        tya;
        and #3;
        bne L_pos_byte;
        txa;
        clc;
        adc #4;
        tax;
        cpy #12;
        bcc L_pos_byte;
  }
  // clang-format on
}

// Copies _opponent_planes[i].lay (8 bytes) into _opponent_shown[i].
void _opponent_latch_shown(uint8_t i) {
  // clang-format off
  __asm {
        ldx #7;
        lda i;
        bne L_ls_1;
    L_ls_0:
        lda _opponent_planes, x;
        sta _opponent_shown, x;
        dex;
        bpl L_ls_0;
        rts;
    L_ls_1:
        lda _opponent_planes + 84, x;
        sta _opponent_shown + 8, x;
        dex;
        bpl L_ls_1;
  }
  // clang-format on
}

// Copies _opponent_cs[i] (6 bytes) into _opponent_c.
void _opponent_copy_cs(uint8_t i) {
  // clang-format off
  __asm {
        ldx #5;
        lda i;
        bne L_cs_1;
    L_cs_0:
        lda _opponent_cs, x;
        sta _opponent_c, x;
        dex;
        bpl L_cs_0;
        rts;
    L_cs_1:
        lda _opponent_cs + 6, x;
        sta _opponent_c, x;
        dex;
        bpl L_cs_1;
  }
  // clang-format on
}

// Halves (dx, dy, dz) in ZP vec_v until |dx| < 64 and |dy| < 64, leaves the
// reduced vector in vec_v, forms exact 2D products with _opponent_head[i]
// (cross = dy * hx - dx * hy in ZP vec_sx, dot = dx * hx + dy * hy in ZP
// vec_sy, matching vec_fastmul8p8(d << 8, h)), and returns halving count `cut`.
uint8_t _opponent_steer(uint8_t i) {
  frac_q1 = i;
  // clang-format off
  __asm {
        lda #0;
        sta frac_r0;            // cut = 0

    L_st_chk:
        ldx #2;                 // check dy (offset 2), then dx (offset 0)
    L_st_within:
        clc;
        lda vec_v, x;
        adc #63;
        tay;
        lda vec_v + 1, x;
        adc #0;
        bne L_st_shr;
        cpy #127;
        bcs L_st_shr;
        dex;
        dex;
        bpl L_st_within;
        bmi L_st_shr_done;

    L_st_shr:
        ldx #4;                 // arithmetic >>= 1 on dz, dy, dx in ZP
    L_st_shr_ax:
        lda vec_v + 1, x;
        cmp #$80;
        ror;
        sta vec_v + 1, x;
        ror vec_v, x;
        dex;
        dex;
        bpl L_st_shr_ax;
        inc frac_r0;
        bne L_st_chk;

    L_st_shr_done:
        ldy #0;
        lda frac_q1;
        beq L_st_h0;
        ldy #6;
    L_st_h0:
        sty frac_q1;            // byte offset of hx in _opponent_fresh
        lda vec_v + 2;          // dy8
        jsr L_mul_dh;
        stx vec_sx;
        sta vec_sx + 1;

        lda vec_v;              // dx8
        jsr L_mul_dh;
        stx vec_sy;
        sta vec_sy + 1;

        inc frac_q1;
        inc frac_q1;            // byte offset of hy in _opponent_fresh
        sec;
        lda #0;
        sbc vec_v;              // -dx8
        jsr L_mul_dh;
        txa;
        clc;
        adc vec_sx;
        sta vec_sx;
        lda vec_proj_mul_a + 1;
        adc vec_sx + 1;
        sta vec_sx + 1;

        lda vec_v + 2;          // dy8
        jsr L_mul_dh;
        txa;
        clc;
        adc vec_sy;
        sta vec_sy;
        lda vec_proj_mul_a + 1;
        adc vec_sy + 1;
        sta vec_sy + 1;
        jmp L_st_done;

    // Input: A = signed d8 in [-63, 63], frac_q1 = byte offset of h in
    // _opponent_fresh. Output: signed 16-bit d8 * h with low byte in X and
    // high byte in A and vec_proj_mul_a + 1; frac_q1 preserved.
    L_mul_dh:
        ldy frac_q1;
        tax;
        beq L_dh_zero;
        eor _opponent_fresh + 1, y;
        sta frac_q0;            // sign = d8 ^ h_hi
        txa;
        bpl L_dh_ud;
        eor #$ff;
        clc;
        adc #1;
    L_dh_ud:
        sta frac_d0;            // ud = |d8| in [1, 63]
        lda _opponent_fresh + 1, y;
        bpl L_dh_hpos;
        sec;
        lda #0;
        sbc _opponent_fresh, y;
        bne L_dh_mul;           // h in [-255, -1]: A = |h|_lo
        beq L_dh_256;           // h == -256: |h| == 256
    L_dh_hpos:
        bne L_dh_256;           // h == +256
        lda _opponent_fresh, y;
        beq L_dh_zero;
        bne L_dh_mul;
    L_dh_256:
        lda frac_d0;
        ldx #0;
        beq L_dh_sign;
    L_dh_mul:
        sta vec_mul8_b;
        lda frac_d0;
        jsr vec_mul8;           // hi in A, lo in vec_mul8_lo
        ldx vec_mul8_lo;
    L_dh_sign:
        sta vec_proj_mul_a + 1;
        bit frac_q0;
        bpl L_dh_rts;
        stx vec_proj_mul_a;
        sec;
        lda #0;
        sbc vec_proj_mul_a;
        tax;
        lda #0;
        sbc vec_proj_mul_a + 1;
        sta vec_proj_mul_a + 1;
    L_dh_rts:
        rts;
    L_dh_zero:
        ldx #0;
        sta vec_proj_mul_a + 1;
        rts;

    L_st_done:
  }
  // clang-format on
  return frac_r0;
}

#ifndef __TARGET_TEST__
#pragma optimize(pop)
#endif

#endif
