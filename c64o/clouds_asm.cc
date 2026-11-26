#include "clouds_asm.h"

#include <stddef.h>
#include <stdint.h>

// The assembly half of clouds.cc (clouds_asm.h): shared cloud scratch state in
// tscratch (fpilot) / bss2 (ppilot), plus 6502 routines for basis setup,
// staged camera-row dot products, group offset application, and the 5x5 scan.

#if defined(__OSCAR64__) && defined(__TARGET_TEST__)
#pragma section(cld_bss, 0, , , bss)
#pragma bss(cld_bss)
#elif defined(__FPILOT__)
#pragma bss(tscratch)
#else
#pragma bss(bss2)
#endif
int8_t _clouds_half_basis[9];
clouds_zero_t _clouds_zero;
int16_t _clouds_dz;
int16_t _clouds_z[3];
vec3_t _clouds_centre;
uint8_t _clouds_pat_off;
#ifdef __OSCAR64__
// Contiguous frac_x (+0..+1) and frac_y (+2..+3) for indexed world-box cull.
static uint16_t _cld_frac[2];
static uint8_t _cld_cx;
static uint8_t _cld_cy;
static uint8_t _cld_start_cy;
static uint8_t _cld_hx;
#define _cld_ux_hi (_clouds_zero.ux_hi)
#define _cld_uy_hi (_clouds_zero.uy_hi)
#endif
#pragma bss(bss)

#ifdef __OSCAR64__

#ifndef __TARGET_TEST__
// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)
#endif

static_assert(sizeof(clouds_zero_t) == 6 &&
                  offsetof(clouds_zero_t, basis_valid) == 0 &&
                  offsetof(clouds_zero_t, z_valid) == 1 &&
                  offsetof(clouds_zero_t, ux_hi) == 4 &&
                  offsetof(clouds_zero_t, uy_hi) == 5,
              "clouds_zero_t layout mismatch");

static_assert(kCloudCellU == 2048 && kCloudCellShift == 11 &&
                  kCloudCellMaskX == 7 && kCloudCellMaskY == 15 &&
                  kCloudScanRadius == 2,
              "clouds_asm grid constants mismatch");
static_assert(kCloudGateBits == 0x1F && kCloudGateLimit == 10 &&
                  kCloudJitterShift == 7,
              "clouds_asm gate/jitter constants mismatch");
static_assert(kCloudDeckU == 700 && kCloudOffsetU == 32 &&
                  kCloudCullU == 4096 && kCloudRungCount == 10 &&
                  kCloudRungCollapsed == 3 && kCloudBlobsPerGroup == 3,
              "clouds_asm deck/rung constants mismatch");
static_assert(sizeof(vec3_t) == 6 && offsetof(mat3_t, front) == 0 &&
                  offsetof(mat3_t, left) == 6 && offsetof(mat3_t, up) == 12,
              "clouds_asm mat3_t/vec3_t layout mismatch");

// Half-basis in camera-row order: trunc(world_cam[i] / 16) for |c| <= 256.
// Exact equivalent of vec_fastmul8p8(c, kCloudOffsetU >> 1) (k = 16).
void _clouds_build_basis(void) {
  // clang-format off
  __asm {
        ldx #16;
        ldy #8;
    L_cb_loop:
        lda world_cam + 1, x;
        bmi L_cb_neg;
        bne L_cb_p16;
        lda world_cam, x;
        lsr;
        lsr;
        lsr;
        lsr;
        bpl L_cb_store;         // always taken (0..15)
    L_cb_p16:
        lda #16;
        bne L_cb_store;         // always taken
    L_cb_neg:
        lda world_cam, x;
        beq L_cb_m16;
        eor #$ff;
        clc;
        adc #1;
        lsr;
        lsr;
        lsr;
        lsr;
        eor #$ff;
        clc;
        adc #1;
        jmp L_cb_store;
    L_cb_m16:
        lda #$f0;               // -16
    L_cb_store:
        sta _clouds_half_basis, y;
        dex;
        dex;
        dey;
        bpl L_cb_loop;
  }
  // clang-format on
}

// vec_v = _clouds_centre + pat[axis] * _clouds_half_basis[axis + {0,3,6}],
// advancing _clouds_pat_off by 3 and returning the updated vec_v.x. Every pat
// entry is in {-2, -1, 0, +1, +2}: bit 0 is 0 iff |coeff| == 2, bit 7 is 1
// iff coeff < 0.
int16_t _clouds_apply_offset(void) {
  // clang-format off
  __asm {
        ldx #5;
    L_ao_copy:
        lda _clouds_centre, x;
        sta vec_v, x;
        dex;
        bpl L_ao_copy;
        lda #0;
        sta vec_proj_tmp1;      // axis = 0, 1, 2
        ldy _clouds_pat_off;
    L_ao_axis:
        lda kCloudGroupOffset, y;
        beq L_ao_skip;          // test before INY (INY clobbers Z)
        sta vec_proj_recip;     // coeff
        iny;
        sty frac_q0;            // save pat_off (1..36, nonzero)
        ldy vec_proj_tmp1;      // hb_idx = axis, axis+3, axis+6
        ldx #0;                 // vec_v byte offset = 0, 2, 4
    L_ao_comp:
        lda vec_proj_recip;
        lsr;                    // C = bit 0 of coeff (0 => |coeff| == 2)
        lda _clouds_half_basis, y;
        bcs L_ao_no_dbl;
        asl;                    // [-32, +32], still signed int8_t
    L_ao_no_dbl:
        bit vec_proj_recip;
        bpl L_ao_add;
        eor #$ff;
        clc;
        adc #1;
    L_ao_add:
        sta vec_mul8_b;         // save sign of signed delta
        clc;
        adc vec_v, x;
        sta vec_v, x;
        bit vec_mul8_b;
        bpl L_ao_pos;
        bcs L_ao_comp_next;
        dec vec_v + 1, x;
        bcc L_ao_comp_next;     // C still 0
    L_ao_pos:
        bcc L_ao_comp_next;
        inc vec_v + 1, x;
    L_ao_comp_next:
        iny;
        iny;
        iny;
        inx;
        inx;
        cpx #6;
        bcc L_ao_comp;
        ldy frac_q0;            // restore pat_off
        bne L_ao_next_axis;     // always taken
    L_ao_skip:
        iny;
    L_ao_next_axis:
        inc vec_proj_tmp1;
        lda vec_proj_tmp1;
        cmp #3;
        bcc L_ao_axis;
        sty _clouds_pat_off;
  }
  // clang-format on
  return vec_v.x;
}

// Internal camera-row dot product:
//   Input: Y = axis (0 front, 1 left, 2 up), vec_v.{x,y} = world delta.
//   Output: vec_proj_mul_a = _clouds_z[axis] + row.x * vec_v.x + row.y * vec_v.y.
//   Preserves vec_v, vec_sx, vec_sy.
static void _clouds_dot_raw(void) {
  // clang-format off
  __asm {
        tya;
        asl;
        sta frac_d1;            // z_off = 0, 2, 4
        sta vec_proj_mul_b;
        asl;
        clc;
        adc vec_proj_mul_b;
        sta vec_proj_mul_b;     // row_off = 0, 6, 12

        lda _clouds_zero + 1, y;
        bne L_cd_z_load;
        lda #1;
        sta _clouds_zero + 1, y;
        lda #0;
        sta vec_proj_mul_a;
        sta vec_proj_mul_a + 1;
        lda _clouds_dz;
        sta frac_r0;
        lda _clouds_dz + 1;
        sta frac_r1;
        lda vec_proj_mul_b;
        clc;
        adc #4;
        tax;                    // row->z offset
        jsr L_mul_cam16_add;
        ldy frac_d1;
        lda vec_proj_mul_a;
        sta _clouds_z, y;
        lda vec_proj_mul_a + 1;
        sta _clouds_z + 1, y;
        jmp L_cd_xy;
    L_cd_z_load:
        ldy frac_d1;
        lda _clouds_z, y;
        sta vec_proj_mul_a;
        lda _clouds_z + 1, y;
        sta vec_proj_mul_a + 1;

    L_cd_xy:
        // + row->x * vec_v.x, then fall through for + row->y * vec_v.y
        lda vec_v;
        sta frac_r0;
        lda vec_v + 1;
        sta frac_r1;
        ldx vec_proj_mul_b;
        jsr L_mul_cam16_add;
        lda vec_v + 2;
        sta frac_r0;
        lda vec_v + 3;
        sta frac_r1;
        ldx vec_proj_mul_b;
        inx;
        inx;

    // Input: X = byte offset in world_cam (|c| <= 256), frac_r0/r1 = int16 v.
    // Adds vec_fastmul8p8(c, v) into vec_proj_mul_a.
    // Preserves vec_v, vec_proj_mul_b, frac_d1, vec_sx, vec_sy.
    L_mul_cam16_add:
        lda world_cam + 1, x;
        eor frac_r1;
        sta vec_proj_tmp1;      // bit 7 = sign of result
        lda world_cam + 1, x;
        bpl L_mc_cpos;
        sec;
        lda #0;
        sbc world_cam, x;
        beq L_mc_256;           // c == -256
        bne L_mc_cl_ok;         // c in [-255, -1]
    L_mc_cpos:
        bne L_mc_256;           // c == +256
        lda world_cam, x;
        beq L_mc_rts;           // c == 0: add nothing
    L_mc_cl_ok:
        sta frac_d0;            // |c| in [1, 255]
        lda frac_r1;
        bpl L_mc_vpos;
        sec;
        lda #0;
        sbc frac_r0;
        sta frac_r0;
        lda #0;
        sbc frac_r1;
        sta frac_r1;
    L_mc_vpos:
        lda frac_r1;
        beq L_mc_vh0;
        sta vec_mul8_b;
        lda frac_d0;
        jsr vec_mul8;           // hi in A, lo in vec_mul8_lo
        sta frac_q1;
        lda vec_mul8_lo;
        sta frac_q0;
        jmp L_mc_vl;
    L_mc_vh0:
        sta frac_q0;            // A == 0
        sta frac_q1;
    L_mc_vl:
        lda frac_r0;
        beq L_mc_sign;
        sta vec_mul8_b;
        lda frac_d0;
        jsr vec_mul8;           // (|c| * vl) >> 8 in A
        clc;
        adc frac_q0;
        sta frac_q0;
        bcc L_mc_sign;
        inc frac_q1;
    L_mc_sign:
        bit vec_proj_tmp1;
        bmi L_mc_sub;
    L_mc_add:
        clc;
        lda vec_proj_mul_a;
        adc frac_q0;
        sta vec_proj_mul_a;
        lda vec_proj_mul_a + 1;
        adc frac_q1;
        sta vec_proj_mul_a + 1;
    L_mc_rts:
        rts;
    L_mc_sub:
        sec;
        lda vec_proj_mul_a;
        sbc frac_q0;
        sta vec_proj_mul_a;
        lda vec_proj_mul_a + 1;
        sbc frac_q1;
        sta vec_proj_mul_a + 1;
        rts;

    L_mc_256:
        lda frac_r0;
        sta frac_q0;
        lda frac_r1;
        sta frac_q1;
        lda world_cam + 1, x;
        bmi L_mc_sub;
        bpl L_mc_add;
  }
  // clang-format on
}

int16_t _clouds_dot(uint8_t axis) {
  // clang-format off
  __asm {
        ldy axis;
        jsr _clouds_dot_raw;
  }
  // clang-format on
  return vec_proj_mul_a;
}

// Per-frame setup for the 5x5 cloud grid scan.
void _clouds_scan_init(void) {
  // clang-format off
  __asm {
        ldx #5;
        lda #0;
    L_si_clr:
        sta _clouds_zero, x;
        dex;
        bpl L_si_clr;

        // _clouds_dz = 700 ($02BC) - (int16_t)((uint32_t)flight_eye_z >> 9)
        lda flight_eye_z + 1;
        ldx flight_eye_z + 2;
        ldy flight_eye_z + 3;
        jsr L_si_down9;
        sec;
        lda #$bc;
        sbc frac_r0;
        sta _clouds_dz;
        lda #$02;
        sbc frac_r1;
        sta _clouds_dz + 1;

        // mx -> _cld_cx, _cld_hx, _cld_frac[0]
        lda flight_eye_x + 1;
        ldx flight_eye_x + 2;
        ldy flight_eye_x + 3;
        jsr L_si_down9;
        ldx #0;
        jsr L_si_axis;
        sta _cld_cx;
        and #$07;
        tax;
        lda kCloudHashX, x;
        sta _cld_hx;

        // my -> _cld_start_cy, _cld_cy, _cld_frac[1]
        lda flight_eye_y + 1;
        ldx flight_eye_y + 2;
        ldy flight_eye_y + 3;
        jsr L_si_down9;
        ldx #2;
        jsr L_si_axis;
        sta _cld_start_cy;
        sta _cld_cy;
        rts;

    // Input: A = byte+1, X = byte+2, Y = byte+3. Output: frac_r0/r1 = val >> 9.
    L_si_down9:
        sta frac_r0;
        stx frac_r1;
        tya;
        lsr;
        ror frac_r1;
        ror frac_r0;
        rts;

    // Input: frac_r0/r1 = m, X = 0 (x) or 2 (y).
    // Stores -(m & 0x07FF) into _cld_frac[X/2] and returns (m_hi >> 3) - 2 in A.
    L_si_axis:
        lda frac_r1;
        and #$07;
        sta frac_d0;
        sec;
        lda #0;
        sbc frac_r0;
        sta _cld_frac, x;
        lda #0;
        sbc frac_d0;
        sta _cld_frac + 1, x;
        lda frac_r1;
        lsr;
        lsr;
        lsr;
        sec;
        sbc #2;
        rts;
  }
  // clang-format on
}

// Advances the 5x5 cloud scan until a group passes the hash gate, world box,
// depth range, and vertical/lateral wedge culls. Stores camera-space centre in
// _clouds_centre and pattern byte offset in _clouds_pat_off, and returns the
// rung index (0..9), or 0xFF when the scan is complete.
uint8_t _clouds_scan_next(void) {
  // clang-format off
  __asm {
        lda _clouds_zero + 4;
        cmp #80;
        bcc L_sn_load;
    L_sn_none:
        lda #$ff;
        sta vec_proj_tmp1;
        jmp L_sn_ret;
    L_sn_load:
        // Keep hot inner-loop state in ZP (preserved by _clouds_dot_raw):
        //   vec_sx + 0 = uy_hi, vec_sx + 1 = cy,
        //   vec_sy + 0 = hx,    vec_sy + 1 = ha.
        lda _clouds_zero + 5;
        sta vec_sx;
        lda _cld_cy;
        sta vec_sx + 1;
        lda _cld_hx;
        sta vec_sy;

    L_sn_inner:
        lda vec_sx + 1;
        and #$0f;
        tax;
        lda kCloudHashY, x;
        eor vec_sy;
        and #$1f;
        tay;                    // idx
        lda kCloudHashA, y;
        sta vec_sy + 1;         // ha
        and #$1f;
        cmp #10;
        bcc L_sn_gate_ok;

    L_sn_next_y:
        inc vec_sx + 1;         // ++cy
        lda vec_sx;
        clc;
        adc #16;
        sta vec_sx;
        cmp #80;
        bcc L_sn_inner;
        jsr L_sn_next_row;
        bcc L_sn_inner;
        bcs L_sn_none;

    L_sn_gate_ok:
        // World-box X cull: off_x = frac_x + ((ux_hi | (hb & 0x0F)) << 7)
        lda kCloudHashB, y;
        sta frac_q0;            // hb
        and #$0f;
        ora _clouds_zero + 4;
        lsr;
        tax;                    // hi = jx >> 1
        lda #0;
        ror;                    // lo = (jx & 1) << 7
        clc;
        adc _cld_frac;
        sta vec_v;
        txa;
        adc _cld_frac + 1;
        cmp #$20;
        bcc L_sn_x_ok;
        bne L_sn_next_y;
        ldx vec_v;
        bne L_sn_next_y;
    L_sn_x_ok:
        sec;
        sbc #$10;               // off_x - 4096
        sta vec_v + 1;

        // World-box Y cull: off_y = frac_y + ((uy_hi | (hb >> 4)) << 7)
        lda frac_q0;
        lsr;
        lsr;
        lsr;
        lsr;
        ora vec_sx;
        lsr;
        tax;
        lda #0;
        ror;
        clc;
        adc _cld_frac + 2;
        sta vec_v + 2;
        txa;
        adc _cld_frac + 3;
        cmp #$20;
        bcc L_sn_y_ok;
        bne L_sn_next_y;
        ldx vec_v + 2;
        bne L_sn_next_y;
    L_sn_y_ok:
        sec;
        sbc #$10;               // off_y - 4096
        sta vec_v + 3;

        // centre.x = _clouds_dot(0); require 9 <= centre.x <= 4096
        // i.e. (uint16_t)(centre.x - 9) <= 4087 ($0FF7).
        ldy #0;
        jsr _clouds_dot_raw;
        lda vec_proj_mul_a;
        sta _clouds_centre;
        sec;
        sbc #9;
        tay;
        lda vec_proj_mul_a + 1;
        sta _clouds_centre + 1;
        sbc #0;
        cmp #$10;
        bcs L_sn_next_y_far;
        cmp #$0f;
        bcc L_sn_cx_ok;
        cpy #$f8;
        bcc L_sn_cx_ok;
    L_sn_next_y_far:
        jmp L_sn_next_y;

    L_sn_cx_ok:
        // centre.z = _clouds_dot(2); vert = (centre.x >> 2) + 128
        ldy #2;
        jsr _clouds_dot_raw;
        lda vec_proj_mul_a;
        sta _clouds_centre + 4;
        lda vec_proj_mul_a + 1;
        sta _clouds_centre + 5;

        // Stash centre.x >> 1 in unused vec_v.z for lat below.
        lda _clouds_centre + 1;
        lsr;
        sta vec_v + 5;
        sta frac_r1;
        lda _clouds_centre;
        ror;
        sta vec_v + 4;
        lsr frac_r1;
        ror;
        jsr L_sn_outside_128;
        bcc L_sn_next_y_far;

        // centre.y = _clouds_dot(1);
        // lat = (x >> 1) + (vert >> 1) + 64 == (x >> 1) + (x >> 3) + 128.
        ldy #1;
        jsr _clouds_dot_raw;
        lda vec_proj_mul_a;
        sta _clouds_centre + 2;
        lda vec_proj_mul_a + 1;
        sta _clouds_centre + 3;

        lda vec_v + 5;
        lsr;
        sta frac_r1;
        lda vec_v + 4;
        ror;
        lsr frac_r1;
        ror;
        clc;
        adc vec_v + 4;
        tax;
        lda frac_r1;
        adc vec_v + 5;
        sta frac_r1;
        txa;
        jsr L_sn_outside_128;
        bcc L_sn_next_y_far;

        // Rung walk on striped kCloudRungDepth[10]:
        // while (rung < 9 && centre.x <= kCloudRungDepth[rung + 1]) ++rung;
        ldx #0;
    L_sn_rung:
        cpx #9;
        beq L_sn_rung_done;
        lda kCloudRungDepth + 1, x;
        sec;
        sbc _clouds_centre;
        lda kCloudRungDepth + 10 + 1, x;
        sbc _clouds_centre + 1;
        bcc L_sn_rung_done;
        inx;
        bne L_sn_rung;
    L_sn_rung_done:
        stx vec_proj_tmp1;

        // Advance scan cursor to the next cell and persist to tscratch/bss2.
        inc vec_sx + 1;
        lda vec_sx;
        clc;
        adc #16;
        sta vec_sx;
        cmp #80;
        bcc L_sn_save;
        jsr L_sn_next_row;
    L_sn_save:
        lda vec_sx;
        sta _clouds_zero + 5;
        lda vec_sx + 1;
        sta _cld_cy;
        lda vec_sy;
        sta _cld_hx;

        // Collapsed groups (< 3) project _clouds_centre directly in vec_v;
        // uncollapsed groups lazily build the half-basis and set _clouds_pat_off.
        lda vec_proj_tmp1;
        cmp #3;
        bcc L_sn_collapsed;
        lda vec_sy + 1;
        rol;
        rol;
        rol;
        and #$03;
        sta frac_q0;
        asl;
        asl;
        asl;
        clc;
        adc frac_q0;
        sta _clouds_pat_off;
        lda _clouds_zero;
        bne L_sn_done_basis;
        inc _clouds_zero;
        jsr _clouds_build_basis;
    L_sn_done_basis:
        jmp L_sn_ret;

    L_sn_collapsed:
        ldx #5;
    L_sn_copy_v:
        lda _clouds_centre, x;
        sta vec_v, x;
        dex;
        bpl L_sn_copy_v;
        jmp L_sn_ret;

    // Advance outer row (ux_hi, cx) and reset inner (uy_hi, cy, hx).
    // Returns C=1 if ux_hi reached 80 (scan finished), C=0 otherwise.
    L_sn_next_row:
        inc _cld_cx;
        lda _clouds_zero + 4;
        clc;
        adc #16;
        sta _clouds_zero + 4;
        cmp #80;
        bcs L_sn_nr_rts;
        lda _cld_cx;
        and #$07;
        tax;
        lda kCloudHashX, x;
        sta vec_sy;
        lda _cld_start_cy;
        sta vec_sx + 1;
        lda #0;
        sta vec_sx;
#ifndef __TARGET_TEST__
        // Check once per outer grid row so a long run of culled cells cannot
        // step past the 81-line flip window. Preserves vec_sx/vec_sy/tscratch.
        jsr mem_try_flip;
#endif
        clc;
    L_sn_nr_rts:
        rts;

    // Adds 128 to (frac_r1, A), then checks (uint16_t)(v + r) <= (uint16_t)(2 * r).
    // Returns C=1 if inside, C=0 if outside. v in vec_proj_mul_a.
    L_sn_outside_128:
        clc;
        adc #$80;
        sta frac_r0;
        bcc L_sn_o128_nc;
        inc frac_r1;
    L_sn_o128_nc:
        clc;
        lda vec_proj_mul_a;
        adc frac_r0;
        sta frac_q0;
        lda vec_proj_mul_a + 1;
        adc frac_r1;
        sta frac_q1;
        asl frac_r0;
        rol frac_r1;
        lda frac_r0;
        sec;
        sbc frac_q0;
        lda frac_r1;
        sbc frac_q1;
        rts;

    L_sn_ret:
  }
  // clang-format on
  return vec_proj_tmp1;
}

#ifndef __TARGET_TEST__
#pragma optimize(pop)
#endif

#endif
