#include "opponent_asm.h"

#include <stddef.h>
#include <stdint.h>

// The assembly half of opponent.cc (opponent_asm.h): shared opponent state in
// bss2 and tscratch, plus 6502 routines for world-to-eye wrapping/scaling,
// waypoint/eye half-metre deltas, 3-axis position reads, struct copies,
// 6-bit steering reduction + 2D heading cross/dot products, position
// integration, camera-space body axes, and staged frustum culling.

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
__striped int16_t _opponent_speed[2];

#if defined(__OSCAR64__) && defined(__TARGET_TEST__)
#pragma bss(opp_bss)
#elif defined(__FPILOT__)
#pragma bss(tscratch)
#endif
vec3_t _opponent_c;
vec3_t _opponent_cs[2];
mat3_t _opponent_axes_m;
#ifdef __OSCAR64__
static uint8_t _wd_d0[5];
#endif
#if defined(__FPILOT__) || (defined(__OSCAR64__) && defined(__TARGET_TEST__))
#pragma bss(bss)
#endif

const int16_t kOpponentPitchSin[8] = {-114, -62, -32, 0,
                                      32,   62,  114, -154};

#ifdef __OSCAR64__

static const uint8_t kOpponentPitchCos[8] = {229, 248, 254, 0,
                                             254, 248, 229, 205};
static const uint8_t kOpponentBankCos[5] = {128, 222, 0, 222, 128};
__striped static const int16_t kOpponentBankSin[5] = {222, 128, 0, -128, -222};

#ifndef __TARGET_TEST__
// Per-frame / per-step path: the outliner (-Oo) would trade cycles for bytes.
#pragma optimize(push, nooutline)
#endif

static_assert(sizeof(_opponent_xyz[0]) == 4, "world delta stride");
static_assert(offsetof(opponent_fresh_t, head) == 0 &&
                  offsetof(opponent_fresh_t, bank) == 16 &&
                  offsetof(opponent_fresh_t, pitch) == 20 &&
                  sizeof(vec3_t) == 6,
              "opponent_fresh_t layout");
static_assert(offsetof(mat3_t, front) == 0 && offsetof(mat3_t, left) == 6 &&
                  offsetof(mat3_t, up) == 12 && sizeof(mat3_t) == 18,
              "mat3_t layout");
static_assert(offsetof(planes_state_t, lay) == 0 &&
                  sizeof(planes_state_t) == 84 && sizeof(planes_layout_t) == 8,
              "planes_state_t / planes_layout_t layout");

// Internal world-delta core: A = slot i (0 or 1), outputs vec_v and returns
// range (0=near, 1=far, 2=out) in A and vec_proj_tmp1. Preserves frac_*.
static __noinline void _opp_wd_a(void) {
  // clang-format off
  __asm {
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
        lda #0;
        rts;

    L_wd_out:
        lda #2;                 // kOpponentOut
        sta vec_proj_tmp1;
    L_wd_done:
  }
  // clang-format on
}

// World-space offset from the eye to slot i in vec_v, in eighth metres
// (kOpponentNear) or half metres (kOpponentFar), or kOpponentOut past 32 units.
uint8_t _opponent_world_delta(uint8_t i) {
  // clang-format off
  __asm {
        lda i;
        jsr _opp_wd_a;
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

// Internal move core: adds flight_step_s(trunc(h * (_opponent_speed[i]<<1)/256))
// to 32-bit _opponent_xyz[idx], calling shared vec_mul_ab.
// Inputs: vec_proj_mul_a = h, frac_q0 = i (0..1), frac_q1 = idx (0..5).
// Preserves: frac_q0, frac_r1.
static __noinline void _opp_move_core(void) {
  // clang-format off
  __asm {
        lda vec_proj_mul_a;
        ora vec_proj_mul_a + 1;
        beq L_mv_ret;

        ldx frac_q0;
        lda _opponent_speed, x;
        asl;
        sta vec_proj_mul_b;
        lda _opponent_speed + 2, x;
        rol;
        sta vec_proj_mul_b + 1;

        jsr vec_mul_ab;         // result in vec_mul_res

        ldy flight_step_shift;
        beq L_mv_add;
    L_mv_shr:
        lda vec_mul_res + 1;
        cmp #$80;
        ror;
        sta vec_mul_res + 1;
        ror vec_mul_res;
        dey;
        bne L_mv_shr;

    L_mv_add:
        ldy #0;
        lda vec_mul_res + 1;
        bpl L_mv_sext;
        dey;
    L_mv_sext:
        sty frac_r0;
        lda frac_q1;
        asl;
        asl;
        tax;
        clc;
        lda _opponent_xyz, x;
        adc vec_mul_res;
        sta _opponent_xyz, x;
        lda _opponent_xyz + 1, x;
        adc vec_mul_res + 1;
        sta _opponent_xyz + 1, x;
        lda _opponent_xyz + 2, x;
        adc frac_r0;
        sta _opponent_xyz + 2, x;
        lda _opponent_xyz + 3, x;
        adc frac_r0;
        sta _opponent_xyz + 3, x;
    L_mv_ret:
  }
  // clang-format on
}

__noinline void _opponent_move(uint8_t i, uint8_t idx, int16_t h) {
  // clang-format off
  __asm {
        lda i;
        sta frac_q0;
        lda idx;
        sta frac_q1;
        lda h;
        sta vec_proj_mul_a;
        lda h + 1;
        sta vec_proj_mul_a + 1;
        jmp _opp_move_core;
  }
  // clang-format on
}

__noinline void _opponent_ahead(uint8_t i) {
  // clang-format off
  __asm {
        lda i;
        sta frac_q0;
        sta frac_q1;
        ldy #0;
        cmp #0;
        beq L_ah_h0;
        ldy #6;
    L_ah_h0:
        sty frac_r1;
        lda _opponent_fresh, y;
        sta vec_proj_mul_a;
        lda _opponent_fresh + 1, y;
        sta vec_proj_mul_a + 1;
        jsr _opp_move_core;

        lda frac_q0;
        clc;
        adc #2;
        sta frac_q1;
        ldy frac_r1;
        lda _opponent_fresh + 2, y;
        sta vec_proj_mul_a;
        lda _opponent_fresh + 3, y;
        sta vec_proj_mul_a + 1;
        jmp _opp_move_core;
  }
  // clang-format on
}

// Slot i's body axes in camera space into _opponent_axes_m (front/left/up y,z),
// calling shared vec_mul_ab directly.
__noinline void _opponent_axes(uint8_t i) {
  // ZP layout (preserved across vec_mul_ab):
  //   vec_v+0..1 = ra            vec_v+2..3 = rb
  //   vec_v+4..5 = new_ra        vec_sy+0..1 = rot_s
  //   frac_q0    = head_off (0 or 6)
  //   frac_q1    = pitch (0..7)  frac_r0    = turn (0..4)
  //   frac_r1    = row_off (6, then 12)
  //   frac_d0    = out_off (0, then 2)
  //   frac_d1    = rot_c (uint8_t)
  // clang-format off
  __asm {
        ldx i;
        lda _opponent_fresh + 16, x;    // _opponent_bank[i]
        clc;
        adc #2;
        sta frac_r0;                    // turn
        lda _opponent_fresh + 20, x;    // _opponent_pitch[i]
        clc;
        adc #3;
        sta frac_q1;                    // pitch
        ldy #0;
        cpx #0;
        beq L_ax_h0;
        ldy #6;
    L_ax_h0:
        sty frac_q0;                    // head_off (0 or 6)
        lda #6;
        sta frac_r1;                    // row_off = offsetof(mat3_t, left)
        lda #0;
        sta frac_d0;                    // out_off = 0 (.y), then 2 (.z)

    L_ax_col:
        // ra = row->x * hx + row->y * hy
        ldx frac_r1;
        ldy frac_q0;
        jsr L_ax_mul_rh;                // vec_mul_res = row->x * hx
        lda vec_mul_res;
        sta vec_v + 0;
        lda vec_mul_res + 1;
        sta vec_v + 1;
        ldx frac_r1;
        inx;
        inx;
        ldy frac_q0;
        iny;
        iny;
        jsr L_ax_mul_rh;                // vec_mul_res = row->y * hy
        ldy #0;
        jsr L_ax_add_v;

        // rb = row->z
        ldx frac_r1;
        lda world_cam + 4, x;
        sta vec_v + 2;
        lda world_cam + 5, x;
        sta vec_v + 3;

        // if (pitch != 3) rot(kOpponentPitchCos[pitch], kOpponentPitchSin[pitch])
        ldy frac_q1;
        cpy #3;
        beq L_ax_no_pitch;
        lda kOpponentPitchCos, y;
        sta frac_d1;
        tya;
        asl;
        tay;
        lda kOpponentPitchSin, y;
        sta vec_sy;
        lda kOpponentPitchSin + 1, y;
        sta vec_sy + 1;
        jsr L_ax_rot;
    L_ax_no_pitch:
        // _opponent_axes_m.front.{y,z} = ra (offset 2 + out_off)
        ldx frac_d0;
        lda vec_v + 0;
        sta _opponent_axes_m + 2, x;
        lda vec_v + 1;
        sta _opponent_axes_m + 3, x;

        // ra = row->y * hx - row->x * hy
        ldx frac_r1;
        inx;
        inx;
        ldy frac_q0;
        jsr L_ax_mul_rh;                // row->y * hx
        lda vec_mul_res;
        sta vec_v + 0;
        lda vec_mul_res + 1;
        sta vec_v + 1;
        ldx frac_r1;
        ldy frac_q0;
        iny;
        iny;
        jsr L_ax_mul_rh;                // row->x * hy
        ldy #0;
        jsr L_ax_sub_v;

        // if (turn != 2) rot(kOpponentBankCos[turn], kOpponentBankSin[turn])
        ldy frac_r0;
        cpy #2;
        beq L_ax_no_bank;
        lda kOpponentBankCos, y;
        sta frac_d1;
        lda kOpponentBankSin, y;
        sta vec_sy;
        lda kOpponentBankSin + 5, y;
        sta vec_sy + 1;
        jsr L_ax_rot;
    L_ax_no_bank:
        // _opponent_axes_m.left.{y,z} = ra (offset 8 + out_off),
        // _opponent_axes_m.up.{y,z} = rb (offset 14 + out_off).
        ldx frac_d0;
        lda vec_v + 0;
        sta _opponent_axes_m + 8, x;
        lda vec_v + 1;
        sta _opponent_axes_m + 9, x;
        lda vec_v + 2;
        sta _opponent_axes_m + 14, x;
        lda vec_v + 3;
        sta _opponent_axes_m + 15, x;

        lda frac_r1;
        clc;
        adc #6;
        sta frac_r1;
        inc frac_d0;
        inc frac_d0;
        lda frac_d0;
        cmp #4;
        bcs L_ax_done;
        jmp L_ax_col;

    L_ax_done:
        rts;

        // Helper: vec_mul_res = world_cam[X] * _opponent_fresh[Y]
    L_ax_mul_rh:
        lda world_cam, x;
        sta vec_proj_mul_a;
        lda world_cam + 1, x;
        sta vec_proj_mul_a + 1;
        lda _opponent_fresh, y;
        sta vec_proj_mul_b;
        lda _opponent_fresh + 1, y;
        sta vec_proj_mul_b + 1;
        jmp vec_mul_ab;

        // Helper: vec_v[Y..Y+1] += vec_mul_res
    L_ax_add_v:
        clc;
        lda vec_v, y;
        adc vec_mul_res;
        sta vec_v, y;
        lda vec_v + 1, y;
        adc vec_mul_res + 1;
        sta vec_v + 1, y;
        rts;

        // Helper: vec_v[Y..Y+1] -= vec_mul_res
    L_ax_sub_v:
        sec;
        lda vec_v, y;
        sbc vec_mul_res;
        sta vec_v, y;
        lda vec_v + 1, y;
        sbc vec_mul_res + 1;
        sta vec_v + 1, y;
        rts;

        // Helper: vec_mul_res = frac_d1 * vec_v[Y]
    L_ax_mul_cv:
        lda frac_d1;
        sta vec_proj_mul_a;
        lda #0;
        sta vec_proj_mul_a + 1;
        beq L_ax_mul_v;

        // Helper: vec_mul_res = vec_sy * vec_v[Y]
    L_ax_mul_sv:
        lda vec_sy;
        sta vec_proj_mul_a;
        lda vec_sy + 1;
        sta vec_proj_mul_a + 1;
    L_ax_mul_v:
        lda vec_v, y;
        sta vec_proj_mul_b;
        lda vec_v + 1, y;
        sta vec_proj_mul_b + 1;
        jmp vec_mul_ab;

        // Helper: rot (ra=vec_v+0..1, rb=vec_v+2..3) by (c=frac_d1, s=vec_sy).
    L_ax_rot:
        // new_ra (vec_v+4..5) = c * ra + s * rb
        ldy #0;
        jsr L_ax_mul_cv;
        lda vec_mul_res;
        sta vec_v + 4;
        lda vec_mul_res + 1;
        sta vec_v + 5;
        ldy #2;
        jsr L_ax_mul_sv;
        ldy #4;
        jsr L_ax_add_v;

        // rb = c * rb - s * ra
        ldy #2;
        jsr L_ax_mul_cv;
        lda vec_mul_res;
        sta vec_v + 2;
        lda vec_mul_res + 1;
        sta vec_v + 3;
        ldy #0;
        jsr L_ax_mul_sv;
        ldy #2;
        jsr L_ax_sub_v;

        // ra = new_ra
        lda vec_v + 4;
        sta vec_v + 0;
        lda vec_v + 5;
        sta vec_v + 1;
        rts;
  }
  // clang-format on
}

// Dot product world_cam[row_off] . vec_v into vec_sy, calling shared vec_mul_ab.
// Input: A = row byte offset in world_cam (0=front, 6=left, 12=up).
// Preserves: vec_v, frac_r0, frac_r1, frac_d0, frac_d1. Clobbers: frac_q0, frac_q1.
static __noinline void _opp_dot_cam_v(void) {
  // clang-format off
  __asm {
        sta frac_q0;            // cam_off
        lda #0;
        sta frac_q1;            // axis = 0, 2, 4
        sta vec_sy;
        sta vec_sy + 1;
    L_dcv_loop:
        ldx frac_q0;
        lda world_cam, x;
        sta vec_proj_mul_a;
        lda world_cam + 1, x;
        sta vec_proj_mul_a + 1;
        ldy frac_q1;
        lda vec_v, y;
        sta vec_proj_mul_b;
        lda vec_v + 1, y;
        sta vec_proj_mul_b + 1;
        jsr vec_mul_ab;
        clc;
        lda vec_mul_res;
        adc vec_sy;
        sta vec_sy;
        lda vec_mul_res + 1;
        adc vec_sy + 1;
        sta vec_sy + 1;
        inc frac_q0;
        inc frac_q0;
        inc frac_q1;
        inc frac_q1;
        lda frac_q1;
        cmp #6;
        bcc L_dcv_loop;
  }
  // clang-format on
}

// Per-slot world-delta + staged camera-space frustum cull into _opponent_cs[i].
// Returns 0 if culled, 1 if in view at kOpponentNear, 2 if at kOpponentFar.
__noinline uint8_t _opponent_cull_slot(uint8_t i) {
  // clang-format off
  __asm {
        lda i;
        sta frac_d0;
        jsr _opp_wd_a;          // vec_v = world delta; A = range (0,1,2)
        cmp #2;
        bcc L_cs_in_range;
    L_cs_out_near:
        jmp L_cs_out;
    L_cs_in_range:
        clc;
        adc #1;
        sta frac_d1;            // 1 = near, 2 = far

        ldx #0;
        lda frac_d0;
        beq L_cs_off0;
        ldx #6;
    L_cs_off0:
        stx frac_d0;

        // cx = dot(world_cam.front, vec_v); require cx > 64 (signed).
        lda #0;
        jsr _opp_dot_cam_v;     // vec_sy = cx
        lda vec_sy + 1;
        bmi L_cs_out_near;
        bne L_cs_cx_ok;
        lda vec_sy;
        cmp #65;
        bcc L_cs_out_near;
    L_cs_cx_ok:
        ldy frac_d0;
        lda vec_sy;
        sta _opponent_cs, y;
        lda vec_sy + 1;
        sta _opponent_cs + 1, y;

        // cz = dot(world_cam.up, vec_v)
        lda #12;
        jsr _opp_dot_cam_v;     // vec_sy = cz

        // Compute lim_z (vec_proj_mul_a) and lim_y (frac_r0..r1) from cx.
        ldy frac_d0;
        lda _opponent_cs, y;
        sta vec_proj_mul_b;
        lda _opponent_cs + 1, y;
        sta vec_proj_mul_b + 1;

        // half = cx >> 1 -> lim_y
        jsr L_cs_shr_b;
        lda vec_proj_mul_b;
        sta frac_r0;
        lda vec_proj_mul_b + 1;
        sta frac_r1;

        // quar = half >> 1 -> lim_z
        jsr L_cs_shr_b;
        lda vec_proj_mul_b;
        sta vec_proj_mul_a;
        lda vec_proj_mul_b + 1;
        sta vec_proj_mul_a + 1;

        // eighth = quar >> 1; lim_y += eighth
        jsr L_cs_shr_b;
        lda #0;
        jsr L_cs_add_ly;

        // u32 = eighth >> 2; lim_z -= u32
        jsr L_cs_shr_b;
        jsr L_cs_shr_b;
        sec;
        lda vec_proj_mul_a;
        sbc vec_proj_mul_b;
        sta vec_proj_mul_a;
        lda vec_proj_mul_a + 1;
        sbc vec_proj_mul_b + 1;
        sta vec_proj_mul_a + 1;

        // px4 = u32 >> 1; lim_z += px4 + 101; lim_y += px4 + 135
        jsr L_cs_shr_b;
        clc;
        lda vec_proj_mul_b;
        adc #101;
        tay;
        lda vec_proj_mul_b + 1;
        adc #0;
        tax;
        tya;
        clc;
        adc vec_proj_mul_a;
        sta vec_proj_mul_a;
        txa;
        adc vec_proj_mul_a + 1;
        sta vec_proj_mul_a + 1;

        lda #135;
        jsr L_cs_add_ly;

        // Test cz (vec_sy) against lim_z (vec_proj_mul_a).
        jsr L_cs_in_lim;
        bcc L_cs_out;
        ldy frac_d0;
        lda vec_sy;
        sta _opponent_cs + 4, y;
        lda vec_sy + 1;
        sta _opponent_cs + 5, y;

        // cy = dot(world_cam.left, vec_v); test against lim_y (frac_r0..r1).
        lda #6;
        jsr _opp_dot_cam_v;
        lda frac_r0;
        sta vec_proj_mul_a;
        lda frac_r1;
        sta vec_proj_mul_a + 1;
        jsr L_cs_in_lim;
        bcc L_cs_out;
        ldy frac_d0;
        lda vec_sy;
        sta _opponent_cs + 2, y;
        lda vec_sy + 1;
        sta _opponent_cs + 3, y;
        jmp L_cs_done;

    L_cs_out:
        lda #0;
        sta frac_d1;
        jmp L_cs_done;

    L_cs_shr_b:
        lsr vec_proj_mul_b + 1;
        ror vec_proj_mul_b;
        rts;

    L_cs_add_ly:
        clc;
        adc vec_proj_mul_b;
        tay;
        lda vec_proj_mul_b + 1;
        adc #0;
        tax;
        tya;
        clc;
        adc frac_r0;
        sta frac_r0;
        txa;
        adc frac_r1;
        sta frac_r1;
        rts;

        // C = 1 iff (uint16_t)(vec_sy + vec_proj_mul_a) <= (vec_proj_mul_a << 1).
    L_cs_in_lim:
        clc;
        lda vec_sy;
        adc vec_proj_mul_a;
        sta vec_sx;
        lda vec_sy + 1;
        adc vec_proj_mul_a + 1;
        sta vec_sx + 1;
        lda vec_proj_mul_a;
        asl;
        sta vec_proj_mul_b;
        lda vec_proj_mul_a + 1;
        rol;
        sta vec_proj_mul_b + 1;
        sec;
        lda vec_proj_mul_b;
        sbc vec_sx;
        lda vec_proj_mul_b + 1;
        sbc vec_sx + 1;
        rts;

    L_cs_done:
  }
  // clang-format on
  return frac_d1;
}

#ifndef __TARGET_TEST__
#pragma optimize(pop)
#endif

#endif
