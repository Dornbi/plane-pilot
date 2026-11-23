#include "opponent_asm.h"

#include <stddef.h>
#include <stdint.h>

// The assembly half of opponent.cc (opponent_asm.h): shared opponent state in
// bss2 and tscratch, plus 6502 routines for world-to-eye wrapping/scaling,
// waypoint/eye half-metre deltas, 3-axis position reads, struct copies,
// 6-bit steering reduction + 2D heading cross/dot products, position
// integration, camera-space body axes, staged frustum culling, per-slot AI /
// flight / fall steps, and 2-opponent sprite-budget depth hysteresis/capping.

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
uint8_t _opponent_down;
uint8_t _opponent_pullup;
uint8_t _opponent_firing;
int8_t _opponent_step_want;
uint8_t _opponent_pending;
opponent_vis_t _opponent_vis;
#ifdef __OSCAR64__
static uint8_t _opp_step_i;
static uint8_t _opp_step_flags;
static uint8_t _opp_step_floor;
static uint8_t _opp_step_tmp[2];
#endif

#if defined(__OSCAR64__) && defined(__TARGET_TEST__)
#pragma bss(opp_bss)
#elif defined(__FPILOT__)
#pragma bss(tscratch)
#endif
vec3_t _opponent_c;
vec3_t _opponent_cs[2];
mat3_t _opponent_axes_m;
bool _opponent_aim;
bool _opponent_break;
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
static const int8_t kOppClimbSpeed[7] = {32, 16, 8, 0, -8, -16, -32};
static const uint8_t kOppMinSpeedLo[2] = {1300 & 0xFF, 1100 & 0xFF};
static const uint8_t kOppMinSpeedHi[2] = {1300 >> 8, 1100 >> 8};
static const uint8_t kOppMaxSpeedLo[2] = {2800 & 0xFF, 2000 & 0xFF};
static const uint8_t kOppMaxSpeedHi[2] = {2800 >> 8, 2000 >> 8};

#ifndef __TARGET_TEST__
// Per-frame / per-step path: the outliner (-Oo) would trade cycles for bytes.
#pragma optimize(push, nooutline)
#endif

static_assert(sizeof(_opponent_xyz[0]) == 4, "world delta stride");
static_assert(offsetof(opponent_fresh_t, head) == 0 &&
                  offsetof(opponent_fresh_t, next) == 12 &&
                  offsetof(opponent_fresh_t, turn) == 14 &&
                  offsetof(opponent_fresh_t, bank) == 16 &&
                  offsetof(opponent_fresh_t, climb) == 18 &&
                  offsetof(opponent_fresh_t, pitch) == 20 &&
                  offsetof(opponent_fresh_t, gun) == 24 &&
                  sizeof(vec3_t) == 6,
              "opponent_fresh_t layout");
static_assert(offsetof(mat3_t, front) == 0 && offsetof(mat3_t, left) == 6 &&
                  offsetof(mat3_t, up) == 12 && sizeof(mat3_t) == 18,
              "mat3_t layout");
static_assert(offsetof(planes_state_t, lay) == 0 &&
                  offsetof(planes_state_t, key_valid) == 8 &&
                  offsetof(planes_layout_t, level) == 0 &&
                  offsetof(planes_layout_t, cols) == 2 &&
                  offsetof(planes_layout_t, rows) == 3 &&
                  kPlaneLevelDot == 0 &&
                  sizeof(planes_state_t) == 84 && sizeof(planes_layout_t) == 8,
              "planes_state_t / planes_layout_t layout");
static_assert(offsetof(opponent_vis_t, nearer) == 3 &&
                  offsetof(opponent_vis_t, capped) == 4,
              "opponent_vis_t layout");

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

// Internal steering core: frac_q1 = slot i (0 or 1), vec_v = (dx, dy, dz).
// Reduces vec_v until |dx| < 64 and |dy| < 64, leaves cross in vec_sx, dot in
// vec_sy, and halving count `cut` in frac_r0.
static __noinline void _opp_steer_core(void) {
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
}

// Halves (dx, dy, dz) in ZP vec_v until |dx| < 64 and |dy| < 64, leaves the
// reduced vector in vec_v, forms exact 2D products with _opponent_head[i]
// (cross = dy * hx - dx * hy in ZP vec_sx, dot = dx * hx + dy * hy in ZP
// vec_sy, matching vec_fastmul8p8(d << 8, h)), and returns halving count `cut`.
uint8_t _opponent_steer(uint8_t i) {
  frac_q1 = i;
  _opp_steer_core();
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

// Internal ahead core: A = slot i (0 or 1). Advances x and y along head[i].
static __noinline void _opp_ahead_a(void) {
  // clang-format off
  __asm {
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

__noinline void _opponent_ahead(uint8_t i) {
  // clang-format off
  __asm {
        lda i;
        jmp _opp_ahead_a;
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

// Internal height compare: A = lim (uint8), X = i << 2 (0 or 4).
// Output: C = 0 iff (int16_t)_opponent_hm(&_opponent_z[i]) < (int16_t)lim.
// Preserves: X, Y. Clobbers: A, vec_proj_tmp1.
static __noinline void _opp_below(void) {
  // clang-format off
  __asm {
        sta vec_proj_tmp1;
        lda _opponent_xyz + 18, x;
        bmi L_bl_yes;
        bne L_bl_no;
        lda _opponent_xyz + 17, x;
        cmp vec_proj_tmp1;
        rts;
    L_bl_yes:
        clc;
        rts;
    L_bl_no:
        sec;
  }
  // clang-format on
}

// Per-slot AI steering, strafe/floor/break-off, climb target, gun burst, and
// turn/bank decision for opponent_step().
// Inputs: i (0 or 1), flags (kOppFlagHunt | kOppFlagTurns | kOppFlagBomber),
//         ZP vec_v = unreduced (dx, dy, dz) in half metres.
// Outputs: updates _opponent_pullup, _opponent_aim, _opponent_break,
//          _opponent_gun[i], _opponent_firing, _opponent_bank[i],
//          _opponent_turn[i], and _opponent_step_want; returns
//          -turn * kOpponentTurn (-16, -8, 0, +8, +16) when kOppFlagTurns is
//          set, or 0 otherwise.
__noinline int16_t _opponent_ai_step_asm(uint8_t i, uint8_t flags) {
  // clang-format off
  __asm {
        lda i;
        sta _opp_step_i;
        sta frac_q1;
        lda flags;
        sta _opp_step_flags;

        // Path climb target from unshifted dz before _opp_steer_core halves it.
        and #1;                 // kOppFlagHunt
        bne L_ai_steer;
        ldy #0;                 // want = 0
        lda vec_v + 5;
        bmi L_ai_dz_neg;
        bne L_ai_want_p1;       // hi > 0 -> dz >= 256 > 40
        lda vec_v + 4;
        cmp #41;
        bcc L_ai_path_want;     // 0..40 -> 0
    L_ai_want_p1:
        iny;                    // +1
        bne L_ai_path_want;
    L_ai_dz_neg:
        cmp #$ff;
        bne L_ai_want_m1;       // hi < $FF -> dz <= -257 < -40
        lda vec_v + 4;
        cmp #$d8;               // -40 = $FFD8
        bcs L_ai_path_want;     // -40..-1 -> 0
    L_ai_want_m1:
        dey;                    // -1 ($FF)
    L_ai_path_want:
        sty _opponent_step_want;

    L_ai_steer:
        jsr _opp_steer_core;    // vec_v reduced, vec_sx=cross, vec_sy=dot, frac_r0=cut

        // across = |vec_sx| into frac_d0..frac_d1.
        lda vec_sx + 1;
        bpl L_ai_ac_pos;
        sec;
        lda #0;
        sbc vec_sx;
        sta frac_d0;
        lda #0;
        sbc vec_sx + 1;
        sta frac_d1;
        jmp L_ai_ac_done;
    L_ai_ac_pos:
        lda vec_sx;
        sta frac_d0;
        lda vec_sx + 1;
        sta frac_d1;
    L_ai_ac_done:
        // Shared ZP map (preserved across vec_mul_ab & _opp_below):
        //   frac_q0 = bit (i+1), frac_q1 = straight (0 or nonzero),
        //   frac_r0 = cut, frac_r1 = i << 2, frac_d0..d1 = across.
        lda _opp_step_i;
        clc;
        adc #1;
        sta frac_q0;
        lda _opp_step_i;
        asl;
        asl;
        sta frac_r1;
        lda #0;
        sta frac_q1;

        lda _opp_step_flags;
        and #1;                 // kOppFlagHunt
        bne L_ai_hunt;

        // !hunt: _opponent_aim = bomber, straight = 0, want already set.
        lda _opp_step_flags;
        and #4;                 // kOppFlagBomber
        beq L_ai_not_bmb;
        lda #1;
    L_ai_not_bmb:
        sta _opponent_aim;
        jmp L_ai_guns;

    L_ai_hunt:
        // --- _opponent_strafe ---
        lda #120;               // kOpponentFloor
        sta _opp_step_floor;
        ldx frac_r1;
        lda _opponent_pullup;
        and frac_q0;
        beq L_stf_not_pu;
        lda #120;
        jsr _opp_below;
        bcc L_stf_pu_done;
        lda model_on_ground;
        beq L_stf_clr_pu;
        lda frac_r0;
        cmp #6;
        bcc L_stf_pu_done;
    L_stf_clr_pu:
        lda frac_q0;
        eor #$ff;
        and _opponent_pullup;
        sta _opponent_pullup;
        jmp L_stf_pu_done;

    L_stf_not_pu:
        lda model_on_ground;
        beq L_stf_chk_fl;
        lda frac_r0;
        cmp #3;
        bcc L_stf_chk_fl;
        jsr L_ai_dot_pos;
        beq L_stf_chk_fl;
        jsr L_ai_dot_shr2;
        jsr L_ai_ac_lt_mula;
        bcs L_stf_chk_fl;
        ldx frac_r1;
        lda #77;                // kOpponentStrafeFloor + kOpponentFloorMargin + 1
        jsr _opp_below;
        bcc L_stf_chk_fl;
        lda #60;                // kOpponentStrafeFloor
        sta _opp_step_floor;
        bne L_stf_pu_done;

    L_stf_chk_fl:
        ldx frac_r1;
        lda #120;
        jsr _opp_below;
        bcs L_stf_pu_done;
        lda _opponent_pullup;
        ora frac_q0;
        sta _opponent_pullup;

    L_stf_pu_done:
        // _opponent_aim and straight = (_opponent_pullup & bit).
        lda #0;
        sta _opponent_aim;
        lda _opponent_pullup;
        and frac_q0;
        sta frac_q1;            // straight
        bne L_stf_aim_done;
        lda frac_r0;
        cmp #5;
        bcs L_stf_aim_done;
        jsr L_ai_dot_pos;
        beq L_stf_aim_done;
        jsr L_ai_dot_shr2;
        lsr vec_proj_mul_a + 1;
        ror vec_proj_mul_a;     // dot >> 3
        jsr L_ai_ac_lt_mula;
        bcs L_stf_aim_done;
        inc _opponent_aim;

    L_stf_aim_done:
        // _opponent_break
        lda #0;
        sta _opponent_break;
        lda model_on_ground;
        bne L_stf_brk_done;
        jsr L_ai_dot_pos;
        beq L_stf_brk_done;
        lda frac_r0;
        cmp #2;
        bcc L_stf_brk_yes;
        bne L_stf_brk_done;
        // cut == 2: dot(flight_cam.front.{x,y}, head[i].{x,y}) < 0
        ldy #0;
        lda frac_r1;
        beq L_brk_h0;
        ldy #6;
    L_brk_h0:
        sty _opp_step_tmp;      // temporary head_off
        lda flight_cam + 0;
        sta vec_proj_mul_a;
        lda flight_cam + 1;
        sta vec_proj_mul_a + 1;
        lda _opponent_fresh + 0, y;
        sta vec_proj_mul_b;
        lda _opponent_fresh + 1, y;
        sta vec_proj_mul_b + 1;
        jsr vec_mul_ab;
        ldy _opp_step_tmp;
        lda vec_mul_res;
        sta _opp_step_tmp;
        lda vec_mul_res + 1;
        sta _opp_step_tmp + 1;

        lda flight_cam + 2;
        sta vec_proj_mul_a;
        lda flight_cam + 3;
        sta vec_proj_mul_a + 1;
        lda _opponent_fresh + 2, y;
        sta vec_proj_mul_b;
        lda _opponent_fresh + 3, y;
        sta vec_proj_mul_b + 1;
        jsr vec_mul_ab;
        clc;
        lda vec_mul_res;
        adc _opp_step_tmp;
        lda vec_mul_res + 1;
        adc _opp_step_tmp + 1;
        bpl L_stf_brk_done;
    L_stf_brk_yes:
        lda #1;
        sta _opponent_break;

    L_stf_brk_done:
        // --- _opponent_want (or straight ? 1) ---
        lda frac_q1;
        beq L_ai_want;
        lda #1;
        sta _opponent_step_want;
        jmp L_ai_chk_brk;

    L_ai_want:
        // az = min(|vec_v.z|, 255) -> vec_proj_recip
        lda vec_v + 5;
        bpl L_wt_zpos;
        sec;
        lda #0;
        sbc vec_v + 4;
        tax;
        lda #0;
        sbc vec_v + 5;
        bne L_wt_z255;
        txa;
        jmp L_wt_zset;
    L_wt_zpos:
        bne L_wt_z255;
        lda vec_v + 4;
        jmp L_wt_zset;
    L_wt_z255:
        lda #255;
    L_wt_zset:
        sta vec_proj_recip;

        // ax = |(int8_t)vec_v.x|, ay = |(int8_t)vec_v.y| (<= 63)
        lda vec_v;
        bpl L_wt_axp;
        eor #$ff;
        clc;
        adc #1;
    L_wt_axp:
        sta vec_mul8_b;         // ax
        lda vec_v + 2;
        bpl L_wt_ayp;
        eor #$ff;
        clc;
        adc #1;
    L_wt_ayp:
        cmp vec_mul8_b;
        bcc L_wt_ax_gt;
        lsr vec_mul8_b;
        clc;
        adc vec_mul8_b;
        bcc L_wt_have_h;        // h <= 94, always clear
    L_wt_ax_gt:
        lsr;
        clc;
        adc vec_mul8_b;
    L_wt_have_h:
        lsr;
        lsr;
        sta vec_proj_mul_a;     // h4 = h >> 2
        lsr;
        sta vec_proj_mul_a + 1; // h8 = h >> 3
        lsr;
        sta vec_mul8_b;         // h16 = h >> 4

        ldy #0;
        cmp vec_proj_recip;     // h16 >= az?
        bcs L_wt_sl_done;
        iny;
        clc;
        adc vec_proj_mul_a + 1; // h8 + h16
        cmp vec_proj_recip;
        bcs L_wt_sl_done;
        iny;
        lda vec_proj_mul_a;
        clc;
        adc vec_proj_mul_a + 1; // h4 + h8
        cmp vec_proj_recip;
        bcs L_wt_sl_done;
        iny;
    L_wt_sl_done:
        lda _opponent_break;
        beq L_wt_sign;
        ldy #3;
        sec;
        lda #0;
        sbc vec_v + 4;
        sta vec_v + 4;
        lda #0;
        sbc vec_v + 5;
        sta vec_v + 5;
    L_wt_sign:
        tya;
        bit vec_v + 5;
        bpl L_wt_fl;
        eor #$ff;
        clc;
        adc #1;
    L_wt_fl:
        tay;
        ldx frac_r1;
        lda _opp_step_floor;
        jsr _opp_below;
        bcs L_wt_above;
        tya;
        beq L_wt_w1;
        bpl L_wt_store;
    L_wt_w1:
        lda #1;
        bne L_wt_store;
    L_wt_above:
        tya;
        bpl L_wt_store;
        lda _opp_step_floor;
        clc;
        adc #16;                // kOpponentFloorMargin
        jsr _opp_below;
        bcs L_wt_keep;
        lda #0;
        beq L_wt_store;
    L_wt_keep:
        tya;
    L_wt_store:
        sta _opponent_step_want;

    L_ai_chk_brk:
        lda _opponent_break;
        beq L_ai_guns;
        sec;
        lda #0;
        sbc vec_sx;
        sta vec_sx;
        lda #0;
        sbc vec_sx + 1;
        sta vec_sx + 1;
        lda #$ff;
        sta vec_sy;
        sta vec_sy + 1;

    L_ai_guns:
        // --- _opponent_guns (when kOppFlagTurns set) ---
        lda _opp_step_flags;
        and #2;
        beq L_ai_turn;
        ldx _opp_step_i;
        lda _opponent_fresh + 24, x;
        beq L_gn_z;
        sec;
        sbc #1;
    L_gn_z:
        tay;
        lda _opponent_aim;
        beq L_gn_st;
        lda _opponent_step_want;
        cmp _opponent_fresh + 18, x;
        bne L_gn_st;
        tya;
        bne L_gn_nz;
        ldy #26;                // kOpponentGunCycle
    L_gn_nz:
        cpy #22;                // kOpponentGunCycle - 4
        bcc L_gn_st;
        tya;
        and #1;
        bne L_gn_st;
        lda _opponent_firing;
        ora frac_q0;
        sta _opponent_firing;
    L_gn_st:
        tya;
        sta _opponent_fresh + 24, x;

    L_ai_turn:
        // --- turn & bank ---
        lda #0;
        ldx frac_q1;            // straight?
        bne L_ai_st_turn;
        lda vec_sy + 1;
        bmi L_ai_do_turn;       // dot < 0
        // (uint16_t)dot >> 5 = ((dot << 3) >> 8), dot in 0..32767.
        sta vec_proj_mul_a;
        lda #0;
        sta vec_proj_mul_a + 1;
        lda vec_sy;
        asl;
        rol vec_proj_mul_a;
        asl;
        rol vec_proj_mul_a;
        rol vec_proj_mul_a + 1;
        asl;
        rol vec_proj_mul_a;
        rol vec_proj_mul_a + 1;
        // across > (dot >> 5) iff (dot >> 5) < across.
        lda vec_proj_mul_a;
        cmp frac_d0;
        lda vec_proj_mul_a + 1;
        sbc frac_d1;
        bcc L_ai_do_turn;
        lda #0;
        beq L_ai_st_turn;

    L_ai_do_turn:
        lda _opp_step_flags;
        and #1;
        clc;
        adc #1;                 // 2 if hunt, 1 if !hunt
        bit vec_sx + 1;
        bpl L_ai_st_turn;
        eor #$ff;
        clc;
        adc #1;

    L_ai_st_turn:
        ldx _opp_step_i;
        tay;
        cmp _opponent_fresh + 14, x; // _opponent_turn[i]
        beq L_ai_bk_ok;
        lda #0;
    L_ai_bk_ok:
        sta _opponent_fresh + 16, x; // _opponent_bank[i]
        tya;
        sta _opponent_fresh + 14, x; // _opponent_turn[i]

        lda _opp_step_flags;
        and #2;                 // kOppFlagTurns
        beq L_ai_ret0;
        sec;
        lda #0;
        sbc _opponent_fresh + 14, x; // -turn
        asl;
        asl;
        asl;                    // -turn * kOpponentTurn (-16, -8, 0, +8, +16)
    L_ai_ret0:
        sta vec_proj_mul_a;
        ldx #0;
        cmp #0;
        bpl L_ai_rpos;
        dex;
    L_ai_rpos:
        stx vec_proj_mul_a + 1;
        jmp L_ai_done;

        // Z = 0 iff signed 16-bit vec_sy (dot) > 0.
    L_ai_dot_pos:
        lda vec_sy + 1;
        bmi L_ai_dp0;
        ora vec_sy;
        rts;
    L_ai_dp0:
        lda #0;
        rts;

    L_ai_dot_shr2:
        lda vec_sy;
        sta vec_proj_mul_a;
        lda vec_sy + 1;
        lsr;
        ror vec_proj_mul_a;
        lsr;
        ror vec_proj_mul_a;
        sta vec_proj_mul_a + 1;
        rts;

        // C = 0 iff across (frac_d0..d1) < vec_proj_mul_a.
    L_ai_ac_lt_mula:
        lda frac_d0;
        cmp vec_proj_mul_a;
        lda frac_d1;
        sbc vec_proj_mul_a + 1;
        rts;

    L_ai_done:
  }
  // clang-format on
  return vec_proj_mul_a;
}

// Shared tail for _opponent_fly_and_move_asm() and _opponent_fall_asm():
// Input: frac_q0 = slot i (0 or 1). Checks _opponent_z[i] <= 0; if so sets
// vec_proj_tmp1 = 1 (and A = 1, Z = 0); otherwise advances horizontally via
// _opp_ahead_a and sets vec_proj_tmp1 = 0 (A = 0, Z = 1).
static __noinline void _opp_chk_z_and_ahead(void) {
  // clang-format off
  __asm {
        lda frac_q0;
        asl;
        asl;
        tax;
        lda _opponent_xyz + 19, x;
        bmi L_za_dead;
        ora _opponent_xyz + 18, x;
        ora _opponent_xyz + 17, x;
        ora _opponent_xyz + 16, x;
        beq L_za_dead;
        lda frac_q0;
        jsr _opp_ahead_a;
        lda #0;
        beq L_za_ret;
    L_za_dead:
        lda #1;
    L_za_ret:
        sta vec_proj_tmp1;
  }
  // clang-format on
}

// Per-slot flight speed/climb model, z integration, ground-crash check, and
// horizontal advance for opponent_step(). Reads _opponent_step_want.
// Returns 1 if _opponent_z[i] <= 0 (crashed), 0 otherwise.
__noinline uint8_t _opponent_fly_and_move_asm(uint8_t i, uint8_t flags,
                                              int16_t cruise) {
  // clang-format off
  __asm {
        ldx i;
        stx frac_q0;
        lda _opponent_fresh + 18, x; // climb = _opponent_climb[i]
        sta frac_r1;

        lda flags;
        and #2;                 // kOppFlagTurns (whole)
        bne L_fl_whole;
        jmp L_fl_move_z;
    L_fl_whole:

        // role = (flags & kOppFlagBomber) ? 1 : 0
        ldy #0;
        lda flags;
        and #4;
        beq L_fl_r0;
        iny;
    L_fl_r0:
        lda _opponent_speed, x;
        sta frac_d0;            // speed_lo
        lda _opponent_speed + 2, x;
        sta frac_d1;            // speed_hi

        lda _opponent_step_want;
        sta frac_r1;            // climb = want
        beq L_fl_lim_ok;
        bmi L_fl_chk_max;
        // climb > 0: if (speed <= kOppMinSpeed[role]) climb = 0
        lda kOppMinSpeedLo, y;
        cmp frac_d0;
        lda kOppMinSpeedHi, y;
        sbc frac_d1;
        bcc L_fl_lim_ok;
        lda #0;
        sta frac_r1;
        beq L_fl_lim_ok;
    L_fl_chk_max:
        // climb < 0: if (speed >= kOppMaxSpeed[role]) climb = 0
        lda frac_d0;
        cmp kOppMaxSpeedLo, y;
        lda frac_d1;
        sbc kOppMaxSpeedHi, y;
        bcc L_fl_lim_ok;
        lda #0;
        sta frac_r1;

    L_fl_lim_ok:
        // Pull speed toward cruise by kOpponentLevelAccel (8).
        lda frac_d0;
        cmp cruise;
        lda frac_d1;
        sbc cruise + 1;
        bcc L_fl_acc;
        lda frac_d0;
        cmp cruise;
        bne L_fl_dec;
        lda frac_d1;
        cmp cruise + 1;
        beq L_fl_add_cl;
    L_fl_dec:
        sec;
        lda frac_d0;
        sbc #8;
        sta frac_d0;
        bcs L_fl_add_cl;
        dec frac_d1;
        jmp L_fl_add_cl;
    L_fl_acc:
        clc;
        lda frac_d0;
        adc #8;
        sta frac_d0;
        bcc L_fl_add_cl;
        inc frac_d1;

    L_fl_add_cl:
        // _opponent_speed[i] = speed + kOppClimbSpeed[climb + 3]
        lda frac_r1;
        clc;
        adc #3;
        tay;
        ldx #0;
        lda kOppClimbSpeed, y;
        bpl L_fl_cs_pos;
        dex;
    L_fl_cs_pos:
        clc;
        adc frac_d0;
        ldy frac_q0;
        sta _opponent_speed, y;
        txa;
        adc frac_d1;
        sta _opponent_speed + 2, y;

        // if (climb == _opponent_climb[i]) _opponent_pitch[i] = climb;
        // _opponent_climb[i] = climb;
        lda frac_r1;
        cmp _opponent_fresh + 18, y;
        bne L_fl_no_pitch;
        sta _opponent_fresh + 20, y;
    L_fl_no_pitch:
        sta _opponent_fresh + 18, y;

    L_fl_move_z:
        lda frac_r1;
        beq L_fl_chk_z;
        clc;
        adc #3;
        tay;
        sec;
        lda #0;
        sbc kOppClimbSpeed, y;  // -kOppClimbSpeed[climb+3] in [-32, +32]
        sta vec_proj_mul_a;
        ldx #0;
        cmp #0;
        bpl L_fl_sl_p;
        dex;
    L_fl_sl_p:
        stx vec_proj_mul_a + 1;
        asl vec_proj_mul_a;
        rol vec_proj_mul_a + 1;
        asl vec_proj_mul_a;
        rol vec_proj_mul_a + 1;
        lda frac_q0;
        clc;
        adc #4;
        sta frac_q1;
        jsr _opp_move_core;

    L_fl_chk_z:
        jsr _opp_chk_z_and_ahead;
  }
  // clang-format on
  return vec_proj_tmp1;
}

// Per-slot falling dive/spin step for opponent_step().
// Updates _opponent_pitch[i], spins heading/_opponent_bank[i] when spinning on
// a whole step, integrates z at kOpponentFallZ (-192), checks ground/zero-speed
// destruction (clearing the falling bit in _opponent_down and returning 1), or
// advances horizontally via _opp_ahead_a and returns 0.
__noinline uint8_t _opponent_fall_asm(uint8_t i, uint8_t whole) {
  // clang-format off
  __asm {
        ldx i;
        stx frac_q0;
        lda #4;                 // kOpponentDive
        sta _opponent_fresh + 20, x;

        lda whole;
        beq L_fa_mvz;
        lda #16;                // (0 + 1) << kOpponentSpin
        cpx #0;
        beq L_fa_sp0;
        lda #32;                // (1 + 1) << kOpponentSpin
    L_fa_sp0:
        and _opponent_down;
        beq L_fa_mvz;

        ldy #0;
        cpx #0;
        beq L_fa_h0;
        ldy #6;
    L_fa_h0:
        lda _opponent_fresh + 0, y;
        sta vec_proj_mul_a;
        lda _opponent_fresh + 1, y;
        sta vec_proj_mul_a + 1;
        sec;
        lda #0;
        sbc _opponent_fresh + 2, y;
        sta _opponent_fresh + 0, y;
        lda #0;
        sbc _opponent_fresh + 3, y;
        sta _opponent_fresh + 1, y;
        lda vec_proj_mul_a;
        sta _opponent_fresh + 2, y;
        lda vec_proj_mul_a + 1;
        sta _opponent_fresh + 3, y;
        lda #2;
        sta _opponent_fresh + 16, x;

    L_fa_mvz:
        lda #$40;               // -192 = $FF40 (kOpponentFallZ)
        sta vec_proj_mul_a;
        lda #$ff;
        sta vec_proj_mul_a + 1;
        lda frac_q0;
        clc;
        adc #4;
        sta frac_q1;
        jsr _opp_move_core;

        ldx frac_q0;
        lda _opponent_speed, x;
        ora _opponent_speed + 2, x;
        beq L_fa_dead;
        jsr _opp_chk_z_and_ahead;
        beq L_fa_ret;

    L_fa_dead:
        lda #$fb;               // ~((0 + 1) << kOpponentFalling) = ~4
        ldx frac_q0;
        beq L_fa_d0;
        lda #$f7;               // ~((1 + 1) << kOpponentFalling) = ~8
    L_fa_d0:
        and _opponent_down;
        sta _opponent_down;
        lda #1;
        sta vec_proj_tmp1;
    L_fa_ret:
  }
  // clang-format on
  return vec_proj_tmp1;
}

// Updates _opponent_nearer and _opponent_capped when both slots are in view,
// and drops any newly capped >2-sprite layout back to dot so it reprojects
// under kViewTwo without exceeding the sprite stack budget.
__noinline void _opponent_cap_asm(uint8_t far, uint8_t cap_all) {
  // clang-format off
  __asm {
        ldx far;
        beq L_cp_cmp;
        cpx #3;
        beq L_cp_cmp;
        txa;
        and #1;
        sta _opponent_vis + 3;  // _opponent_nearer
        jmp L_cp_mask;

    L_cp_cmp:
        ldx #0;                 // off_n
        ldy #6;                 // off_o
        lda _opponent_vis + 3;
        beq L_cp_n0;
        ldx #6;
        ldy #0;
    L_cp_n0:
        // dn = _opponent_cs[n].x -> vec_proj_mul_a; dn >> 3 -> vec_proj_mul_b.
        lda _opponent_cs, x;
        sta vec_proj_mul_a;
        sta vec_proj_mul_b;
        lda _opponent_cs + 1, x;
        sta vec_proj_mul_a + 1;
        lsr;
        ror vec_proj_mul_b;
        lsr;
        ror vec_proj_mul_b;
        lsr;
        ror vec_proj_mul_b;
        sta vec_proj_mul_b + 1;

        // thresh = dn - (dn >> 3)
        sec;
        lda vec_proj_mul_a;
        sbc vec_proj_mul_b;
        sta vec_proj_mul_a;
        lda vec_proj_mul_a + 1;
        sbc vec_proj_mul_b + 1;
        sta vec_proj_mul_a + 1;

        // if ((uint16_t)_opponent_cs[o].x < thresh) _opponent_nearer ^= 1;
        lda _opponent_cs, y;
        cmp vec_proj_mul_a;
        lda _opponent_cs + 1, y;
        sbc vec_proj_mul_a + 1;
        bcs L_cp_mask;
        lda _opponent_vis + 3;
        eor #1;
        sta _opponent_vis + 3;

    L_cp_mask:
        lda #3;
        ldx cap_all;
        bne L_cp_have;
        lda #2;
        sec;
        sbc _opponent_vis + 3;  // _opponent_bit(_opponent_nearer ^ 1)
    L_cp_have:
        sta vec_proj_tmp1;      // capped
        tax;
        lda _opponent_vis + 4;  // old _opponent_capped
        stx _opponent_vis + 4;  // _opponent_capped = capped
        eor #$ff;
        and vec_proj_tmp1;      // newly = capped & ~old
        beq L_cp_done;

        sta frac_q0;            // newly (1..3)
        ldx #0;                 // planes_state offset (0, 84)
        ldy #0;                 // shown offset (0, 8)
        lda #1;                 // bit (1, 2)
    L_cp_slot:
        sta vec_proj_tmp1;
        and frac_q0;
        beq L_cp_next;
        lda _opponent_planes, x; // lay.level
        beq L_cp_next;          // == kPlaneLevelDot (0)
        lda _opponent_planes + 2, x;
        clc;
        adc _opponent_planes + 3, x;
        cmp #3;
        bcc L_cp_next;          // cols + rows <= 2
        lda #0;
        sta _opponent_shown, y;
        sta _opponent_planes, x;
        sta _opponent_planes + 8, x; // key_valid = false
        lda vec_proj_tmp1;
        eor #$ff;
        and _opponent_pending;
        sta _opponent_pending;
    L_cp_next:
        lda vec_proj_tmp1;
        cmp #2;
        beq L_cp_done;
        ldx #84;
        ldy #8;
        lda #2;
        bne L_cp_slot;
    L_cp_done:
  }
  // clang-format on
}

#ifndef __TARGET_TEST__
#pragma optimize(pop)
#endif

#endif
