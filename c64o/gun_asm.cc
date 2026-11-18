#include "gun_asm.h"

#include <stddef.h>
#include <stdint.h>

// The assembly half of gun.cc (gun_asm.h): shared round/smoke state in bss2
// and tscratch, plus 6502 routines for per-step tracking, hit/collision tests,
// round advancement, and per-frame smoke/tracer projection and needle choice.

#if defined(__OSCAR64__) && defined(__TARGET_TEST__)
#pragma section(gun_bss, 0, , , bss)
#pragma bss(gun_bss)
#elif defined(__FPILOT__)
#pragma bss(bss2)
#endif
__striped int16_t _round_p[kRoundAxes];
__striped int16_t _round_v[kRoundAxes];
gun_z_t _gun_z;
uint8_t _round_owner[kRoundMax];
// The eye the rounds are relative to, in flight_eye_*'s 24.8 half metres.
int32_t _gun_eye[3];
// Each opponent's offset from the eye on the last step, as _gun_t holds it,
// for the collision test, and bit i: slot i's is from the last step.
__striped int16_t _gun_prev[3 * kOpponentMax];
// The player's newest round, the one drawn while firing, or kRoundNone.
uint8_t _gun_last;
// Where the last round to hit the ground did, relative to the eye like a
// round; frames left of its smoke are in _gun_puff_t, 0 for none.
__striped int16_t _gun_puff[3];

#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
int32_t _gun_p[3];
// What a round is being tested against, relative to the eye like a round;
// and _gun_hits()' and gun_step()'s working, static rather than on oscar64's
// software stack.
__striped int16_t _gun_t[3];
__striped int16_t _gun_d[3];
__striped int16_t _gun_q[3];
__striped int16_t _gun_moved[3];
int16_t _gun_v[3];
vec3_t _gun_c;
int16_t _gun_s[2];
int16_t _gun_h[2];
#if defined(__FPILOT__) || (defined(__OSCAR64__) && defined(__TARGET_TEST__))
#pragma bss(bss)
#endif

#ifdef __OSCAR64__

#ifndef __TARGET_TEST__
// Per-frame / per-step path: the outliner (-Oo) would trade cycles for bytes.
#pragma optimize(push, nooutline)
#endif

static_assert(kOpponentMax == 2 && kGunCollide == 192 && kRoundMax == 8 &&
                  kRoundNone == 0xFF && kRoundPlayer == 0 &&
                  kRoundLife == 12 && kPuffFrames == 10,
              "gun_asm constants");
static_assert(offsetof(gun_z_t, age) == 0 && offsetof(gun_z_t, substep) == 11 &&
                  offsetof(gun_z_t, puff_t) == 12 && sizeof(gun_z_t) == 14,
              "gun_z_t layout changed");
static_assert(offsetof(vec3_t, x) == 0 && offsetof(vec3_t, y) == 2 &&
                  offsetof(vec3_t, z) == 4,
              "vec3_t layout changed");
#ifndef __TARGET_TEST__
static_assert(kScreenWidthPixels / 2 == 160 && kViewportEndYPixels / 2 == 56,
              "viewport centre constants");
#endif

// Screen centre low bytes at byte offsets 0 (x: 160) and 2 (y: 56).
static const uint8_t kGunCentreLo[3] = {160, 0, 56};

// Steps _gun_eye[0..2] to kFlightEye[0..2] in 32-sub-unit (1/16 m) quanta,
// stores each axis's 1/16 m step into _gun_moved[0..2], and zeroes _gun_t[0..2].
void _gun_track_all(void) {
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

// Axes vec_proj_recip..2 of _gun_p less _gun_eye, in sixteenths of a metre the
// short way round (x wraps mod 128, y mod 256), into _gun_t[c0..2]; leaves 1 in
// vec_proj_tmp1 on success, 0 past [-15, 14]. Preserves frac_*.
static void _gun_rel_run(void) {
  // clang-format off
  __asm {
        lda #0;
        sta vec_proj_tmp1;
        lda vec_proj_recip;
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
        inc vec_proj_tmp1;
    L_gr_done:
  }
  // clang-format on
}

uint8_t _gun_rel_from(uint8_t c0) {
  vec_proj_recip = c0;
  _gun_rel_run();
  return vec_proj_tmp1;
}

// Four-substep swept AABB test of round r against half-extent `box` around
// _gun_t, with early whole-step reach rejection on each axis.
uint8_t _gun_hits(uint8_t r, int16_t box) {
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
bool _gun_collides(uint8_t k, bool prev) {
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
  return vec_proj_tmp1;
}

// Copies _gun_t[0..2] and _gun_v[0..2] into slot r of _round_p / _round_v,
// setting age = 1 and owner.
void _gun_store_enemy_round(uint8_t r, uint8_t owner) {
  // clang-format off
  __asm {
        ldx r;
        lda #1;
        sta _gun_z, x;
        lda owner;
        sta _round_owner, x;
        ldy #0;
    L_gse_axis:
        lda _gun_t, y;
        sta _round_p, x;
        lda _gun_t + 3, y;
        sta _round_p + 24, x;
        sty vec_mul8_b;
        tya;
        asl;
        tay;
        lda _gun_v, y;
        sta _round_v, x;
        lda _gun_v + 1, y;
        sta _round_v + 24, x;
        txa;
        clc;
        adc #8;
        tax;
        ldy vec_mul8_b;
        iny;
        cpy #3;
        bcc L_gse_axis;
  }
  // clang-format on
}

// Advances _gun_substep, steps any active ground puff, moves all live rounds
// by their step-scaled velocity less _gun_moved, frees rounds that cross z = 0
// (spawning ground smoke) or exceed kRoundLife on whole steps, clears _gun_last
// once dead, and returns whether this call completed a whole model step.
uint8_t _gun_step_rounds(void) {
  // clang-format off
  __asm {
        ldy #0;
        inc _gun_z + 11;        // ++_gun_substep
        lda _gun_z + 11;
        and kFlightSubstepMask;
        bne L_ga_sub;
        iny;
    L_ga_sub:
        sty frac_q1;            // whole
        lda #1;
        sta frac_d0;
        lda #$80;
        sta frac_d1;            // ground = -32767
        lda #0;
        sta frac_q0;            // have_ground = false
        sta frac_r0;            // r = 0

        lda _gun_z + 12;        // _gun_puff_t
        beq L_ga_round;
        dec _gun_z + 12;
        ldx #2;
    L_ga_puff:
        sec;
        lda _gun_puff, x;
        sbc _gun_moved, x;
        sta _gun_puff, x;
        lda _gun_puff + 3, x;
        sbc _gun_moved + 3, x;
        sta _gun_puff + 3, x;
        dex;
        bpl L_ga_puff;

    L_ga_round:
        ldx frac_r0;
        lda _gun_z, x;          // _round_age[r]
        bne L_ga_live;
        jmp L_ga_next;

    L_ga_live:
        lda frac_q0;
        bne L_ga_step_init;
        inc frac_q0;
        lda #0;
        ldy #3;
    L_ga_zero_pz:
        sta _gun_p + 8, y;
        dey;
        bpl L_ga_zero_pz;
        lda #2;
        sta vec_proj_recip;
        jsr _gun_rel_run;
        lda vec_proj_tmp1;
        beq L_ga_step_init;
        lda _gun_t + 2;
        sta frac_d0;
        lda _gun_t + 5;
        sta frac_d1;

    L_ga_step_init:
        ldy frac_r0;
        ldx #0;
    L_ga_axis:
        stx frac_r1;
        lda _round_v, y;
        sta vec_mul8_lo;
        lda _round_v + 24, y;
        ldx flight_step_shift;
        beq L_ga_shifted;
    L_ga_shift:
        cmp #$80;
        ror;
        ror vec_mul8_lo;
        dex;
        bne L_ga_shift;
    L_ga_shifted:
        ldx frac_r1;
        sta vec_mul8_b;
        sec;
        lda vec_mul8_lo;
        sbc _gun_moved, x;
        sta vec_mul8_lo;
        lda vec_mul8_b;
        sbc _gun_moved + 3, x;
        sta vec_mul8_b;
        clc;
        lda _round_p, y;
        adc vec_mul8_lo;
        sta _round_p, y;
        lda _round_p + 24, y;
        adc vec_mul8_b;
        sta _round_p + 24, y;
        tya;
        clc;
        adc #8;
        tay;
        inx;
        cpx #3;
        bcc L_ga_axis;

        ldx frac_r0;
        lda _round_p + 16, x;
        cmp frac_d0;
        lda _round_p + 40, x;
        sbc frac_d1;
        bvc L_ga_no_ov;
        eor #$80;
    L_ga_no_ov:
        bpl L_ga_above;

        lda _round_p, x;
        sta _gun_puff;
        lda _round_p + 24, x;
        sta _gun_puff + 3;
        lda _round_p + 8, x;
        sta _gun_puff + 1;
        lda _round_p + 32, x;
        sta _gun_puff + 4;
        lda frac_d0;
        sta _gun_puff + 2;
        lda frac_d1;
        sta _gun_puff + 5;
        lda #10;                // kPuffFrames
        sta _gun_z + 12;
        lda #0;
        beq L_ga_kill;

    L_ga_above:
        lda frac_q1;
        beq L_ga_next;
        inc _gun_z, x;
        lda _gun_z, x;
        cmp #13;                // kRoundLife + 1
        bcc L_ga_next;
        lda #0;
    L_ga_kill:
        sta _gun_z, x;

    L_ga_next:
        inc frac_r0;
        lda frac_r0;
        cmp #8;
        bcs L_ga_done;
        jmp L_ga_round;
    L_ga_done:
        lda frac_q1;
        sta vec_proj_tmp1;
  }
  // clang-format on
  if (_gun_last != kRoundNone && _round_age[_gun_last] == 0) {
    _gun_last = kRoundNone;
  }
  return vec_proj_tmp1;
}

// The round worth drawing: _gun_last while firing, else the youngest enemy round.
uint8_t _gun_shown(void) {
  // clang-format off
  __asm {
        lda _gun_last;
        sta vec_proj_tmp1;
        cmp #$ff;
        bne L_gs_done;
        tay;
        ldx #0;
    L_gs_loop:
        lda _round_owner, x;
        beq L_gs_next;
        lda _gun_z, x;
        beq L_gs_next;
        sty vec_mul8_b;
        cmp vec_mul8_b;
        bcs L_gs_next;
        tay;
        stx vec_proj_tmp1;
    L_gs_next:
        inx;
        cpx #8;
        bcc L_gs_loop;
    L_gs_done:
  }
  // clang-format on
  return vec_proj_tmp1;
}

// Needle index 0..15 clockwise from straight up for screen line (dx, dy).
uint8_t _gun_dir(int16_t dx, int16_t dy) {
  // clang-format off
  __asm {
        lda dx;
        sta frac_d0;
        lda dx + 1;
        sta frac_d1;
        lda dy;
        sta frac_r0;
        lda dy + 1;
        sta frac_r1;
        bit frac_d1;
        bpl L_gd_dx_pos;
        sec;
        lda #0;
        sbc frac_d0;
        sta frac_d0;
        lda #0;
        sbc frac_d1;
        sta frac_d1;
        jsr L_gd_neg_r;
    L_gd_dx_pos:
        lda frac_r1;
        sta frac_q1;
        bpl L_gd_norm;
        jsr L_gd_neg_r;

    L_gd_norm:
        lda frac_r1;
        ora frac_d1;
        bne L_gd_shr;
        lda frac_r0;
        ora frac_d0;
        bpl L_gd_oct;
    L_gd_shr:
        lsr frac_r1;
        ror frac_r0;
        lsr frac_d1;
        ror frac_d0;
        jmp L_gd_norm;

    L_gd_oct:
        ldx frac_r0;
        ldy frac_d0;
        cpx frac_d0;
        bcc L_gd_flat;
        inc frac_d1;
        sty frac_r0;
        stx vec_mul8_b;
        bcs L_gd_tan;
    L_gd_flat:
        sty vec_mul8_b;

    L_gd_tan:
        lda #0;
        sta frac_q0;
    L_gd_loop:
        ldx frac_q0;
        lda kTracerTan, x;
        jsr vec_mul8;
        cmp frac_r0;
        bcs L_gd_tan_done;
        inc frac_q0;
        lda frac_q0;
        cmp #4;
        bcc L_gd_loop;

    L_gd_tan_done:
        lda frac_q0;
        ldx frac_d1;
        bne L_gd_steep_done;
        eor #$ff;
        clc;
        adc #9;
    L_gd_steep_done:
        bit frac_q1;
        bmi L_gd_ret;
        eor #$ff;
        clc;
        adc #1;
        and #15;
    L_gd_ret:
        sta vec_proj_tmp1;
        jmp L_gd_done;

    L_gd_neg_r:
        sec;
        lda #0;
        sbc frac_r0;
        sta frac_r0;
        lda #0;
        sbc frac_r1;
        sta frac_r1;
        rts;

    L_gd_done:
  }
  // clang-format on
  return vec_proj_tmp1;
}

// Cone-checks camera component `v` against positive `frac_d = _gun_c.x` (> 64),
// stores `v` at `_gun_c.y + frac_q1`, projects `kGunCentreLo - trunc(256 * v / x)`
// into `_gun_s + frac_q1` via vec_frac8, and steps `frac_q1` from 2 down to 0.
static __noinline uint8_t _gun_cone_div(int16_t v) {
  // clang-format off
  __asm {
        ldx frac_q1;
        lda v;
        sta _gun_c + 2, x;
        sta frac_r0;
        lda v + 1;
        sta _gun_c + 3, x;
        sta vec_proj_tmp1;
        bpl L_gcd_pos;
        sec;
        lda #0;
        sbc frac_r0;
        sta frac_r0;
        lda #0;
        sbc vec_proj_tmp1;
    L_gcd_pos:
        sta frac_r1;
        lda frac_r0;
        cmp frac_d0;
        lda frac_r1;
        sbc frac_d1;
        bcs L_gcd_out;

        jsr vec_frac8;
        ldx frac_q1;
        ldy #0;
        lda frac_q0;
        bit vec_proj_tmp1;
        bmi L_gcd_add;
        eor #$ff;
        clc;
        adc #1;
        bcs L_gcd_add;
        dey;
    L_gcd_add:
        clc;
        adc kGunCentreLo, x;
        sta _gun_s, x;
        tya;
        adc #0;
        sta _gun_s + 1, x;
        dex;
        dex;
        stx frac_q1;
        lda #1;
        bne L_gcd_done;
    L_gcd_out:
        lda #0;
    L_gcd_done:
        sta vec_proj_tmp1;
  }
  // clang-format on
  return vec_proj_tmp1;
}

static bool _gun_view_vec(void) {
  int16_t x = vec_dot(&world_cam.front, &vec_v);
  if (x <= 64) {
    return false;
  }
  _gun_c.x = x;
  // clang-format off
  __asm {
        lda x;
        sta frac_d0;
        lda x + 1;
        sta frac_d1;
        lda #2;
        sta frac_q1;
  }
  // clang-format on
  return _gun_cone_div(vec_dot(&world_cam.up, &vec_v)) &&
         _gun_cone_div(vec_dot(&world_cam.left, &vec_v));
}

// Staged camera transform + 45-degree cone cull + screen projection of _gun_c
// in place into _gun_c and _gun_s.
bool _gun_view(void) {
  // clang-format off
  __asm {
        ldx #5;
    L_gv_cp:
        lda _gun_c, x;
        sta vec_v, x;
        dex;
        bpl L_gv_cp;
  }
  // clang-format on
  return _gun_view_vec();
}

bool _gun_view_round(uint8_t r) {
  vec_proj_tmp1 = r;
  // clang-format off
  __asm {
        ldy vec_proj_tmp1;
        ldx #0;
    L_gvr_p:
        lda _round_p + 24, y;
        cmp #$80;
        ror;
        sta vec_v + 1, x;
        lda _round_p, y;
        ror;
        sta vec_v, x;
        tya;
        clc;
        adc #8;
        tay;
        inx;
        inx;
        cpx #6;
        bcc L_gvr_p;
  }
  // clang-format on
  return _gun_view_vec();
}

void _gun_vel_round(uint8_t r) {
  vec_proj_tmp1 = r;
  // clang-format off
  __asm {
        ldy vec_proj_tmp1;
        ldx #0;
    L_gvl_v:
        lda _round_v + 24, y;
        cmp #$80;
        ror;
        sta vec_v + 1, x;
        lda _round_v, y;
        ror;
        sta vec_v, x;
        tya;
        clc;
        adc #8;
        tay;
        inx;
        inx;
        cpx #6;
        bcc L_gvl_v;
  }
  // clang-format on
  vec_transform_inv(&world_cam, &vec_v, &vec_v);
}

#ifndef __TARGET_TEST__
#pragma optimize(pop)
#endif
#endif
