#ifdef __OSCAR64__

#include <stddef.h>

#include "world_asm.h"
#ifndef __TARGET_TEST__
#include "color.h"
#endif

#pragma optimize(push, nooutline)

#ifndef __TARGET_TEST__
static_assert(kViewportHeightPixels == 112 && kViewportWidthPixels == 320 &&
                  kViewportStartX == 0 && kScreenHeight == 25 &&
                  kViewportHeight == 14 && kGfxDotGround == 8 &&
                  kGfxGroundPoints == 224 && kGfxColorPoints == 240,
              "world_asm constants mismatch");
#ifdef __ENABLE_COLOR_SCHEMES__
static_assert(offsetof(ColorScheme, dot) == 8,
              "world_asm expects ColorScheme::dot at offset 8");
#endif
#endif

static_assert(sizeof(vec3_t) == 6 && offsetof(vec3_t, x) == 0 &&
                  offsetof(vec3_t, y) == 2 && offsetof(vec3_t, z) == 4,
              "world_asm expects packed vec3_t");
static_assert(sizeof(mat3_t) == 18 && offsetof(mat3_t, front) == 0 &&
                  offsetof(mat3_t, left) == 6 && offsetof(mat3_t, up) == 12,
              "world_asm expects packed mat3_t");
static_assert(kWorldMapHeight == 16 && kWorldMapWidth == 32 &&
                  kWorldMapObjStart == 7 && kPolyMaxVertices == 6,
              "world_asm map/poly geometry mismatch");

// Grid walk state that must survive across poly_draw_3d() calls. Lives in
// fpilot's dead-row scratch (alongside _world_d4 and _world_poly_verts), bss2
// in ppilot/polydemo, or plain bss in target_test. Dot loop temporaries live
// in ZP vec_sx+0 (idx), vec_sx+1 (cnt), vec_sy+0 (col) since vec_project_mul()
// does not touch vec_sx/vec_sy.
#ifdef __TARGET_TEST__
#pragma bss(bss)
#elif defined(__FPILOT__)
#pragma bss(scratch)
#else
#pragma bss(bss2)
#endif

uint8_t _world_obj_idx;
static int16_t _world_wg_bailout;
static uint8_t _world_wg_rxz_hi;
static int8_t _world_wg_x;
static int8_t _world_wg_y;
static uint8_t _world_wg_abs_x;
static uint8_t _world_wg_cx;
static uint8_t _world_wg_row_off;
static uint8_t _world_wg_cy;

#ifdef __TARGET_TEST__
#pragma bss(bss)
#else
#pragma bss(bss2)
#endif

// Radius 2/3 uses [0..3], radius 4 uses [4..8]; index is (radius & 4) + d.
static const uint8_t kWorldNumPoints[9] = {8, 4, 2, 1, 4, 2, 2, 1, 1};

// Grouped Mitchell precompute tables matching world.cc's kMitchellPointsX/Y.
// Processed in 4 groups by dx4 offset via L_wi_mitch_grp:
//   - dx4[4] (24 == 0): 8 single-term/zero entries (i=9 adds 0 + d4[24] == 0)
//   - dx4[0] ( 0): 3 entries (i=0, 7, 2)
//   - dx4[2] (12): 3 entries (i=11, 3, 5)
//   - dx4[6] (36): 2 entries (i=1, 8)
static const uint8_t kWorldMitchGrpX[4] = {24, 0, 12, 36};
static const uint8_t kWorldMitchGrpCnt[4] = {8, 3, 3, 2};
static const uint8_t kWorldMitchDy[16] = {
    36, 54, 24, 0, 66, 66, 12, 90, 54, 66, 90, 54, 66, 90, 54, 90};
static const uint8_t kWorldMitchDst[16] = {
    4, 6, 9, 10, 12, 13, 14, 15, 0, 7, 2, 11, 3, 5, 1, 8};

// ---------------------------------------------------------------------------
// _world_init_start_dx_dy():
//   1. Altitude -> _world_grid_radius (2/3/4) and _world_p_start.z = -(eye_z>>9)
//   2. X and Y axis splits into _world_d4[0..17], step/start/dvec/p_start.{x,y}
//   3. Zero-skipping grouped Mitchell precompute into _world_mitch_{x,y,z}[16]
//   4. vec_transform_inv(&world_cam, &_world_p_start, &_world_p_start)
// ---------------------------------------------------------------------------
void _world_init_start_dx_dy(void) {
  // clang-format off
  __asm {
    // --- 1. n = (uint16_t)(flight_eye_z >> 9); _world_p_start.z = -n ---
    lda flight_eye_z + 3
    lsr
    lda flight_eye_z + 2
    ror
    sta vec_proj_tmp1             // n_hi
    lda flight_eye_z + 1
    ror
    sta vec_proj_recip            // n_lo
    sec
    lda #$00
    sbc vec_proj_recip
    sta _world_p_start + 4
    lda #$00
    sbc vec_proj_tmp1
    sta _world_p_start + 5

    // radius = (n >= 256) ? 4 : (n >= 64) ? 3 : 2
    ldx #$04
    lda vec_proj_tmp1
    bne L_wi_set_r
    dex
    lda vec_proj_recip
    cmp #$40
    bcs L_wi_set_r
    dex
  L_wi_set_r:
    stx _world_grid_radius

    // --- 2a. X axis ---
    lda flight_eye_x + 3
    lsr
    lda flight_eye_x + 2
    ror
    sta vec_proj_mul_b + 1        // m_hi
    lda flight_eye_x + 1
    ror
    sta vec_proj_mul_b + 0        // m_lo
    ldx #$00                      // _world_d4[0..8]
    ldy #$00                      // world_cam col 0
    jsr L_wi_init_axis
    ldx #$05
  L_wi_cp_dx:
    lda vec_v, x
    sta _world_dx_vec, x
    dex
    bpl L_wi_cp_dx
    lda vec_proj_tmp1
    sta _world_step_x
    lda vec_proj_recip
    sta _world_start_cx
    lda vec_proj_mul_a + 0
    sta _world_p_start + 0
    lda vec_proj_mul_a + 1
    sta _world_p_start + 1

    // --- 2b. Y axis ---
    lda flight_eye_y + 3
    lsr
    lda flight_eye_y + 2
    ror
    sta vec_proj_mul_b + 1        // m_hi
    lda flight_eye_y + 1
    ror
    sta vec_proj_mul_b + 0        // m_lo
    ldx #$36                      // 54 = 9 * 6 -> _world_d4[9..17]
    ldy #$02                      // world_cam col 1
    jsr L_wi_init_axis
    ldx #$05
  L_wi_cp_dy:
    lda vec_v, x
    sta _world_dy_vec, x
    dex
    bpl L_wi_cp_dy
    lda vec_proj_tmp1
    sta _world_step_y
    lda vec_proj_recip
    sta _world_start_cy
    lda vec_proj_mul_a + 0
    sta _world_p_start + 2
    lda vec_proj_mul_a + 1
    sta _world_p_start + 3

    // --- 3. Mitchell 16-entry precompute in 4 groups (dx4[4]=0, 0, 2, 6) ---
    lda #$00
    sta vec_proj_recip            // entry index 0..15
    sta vec_sx + 0                // group index 0..3
  L_wi_mg_outer:
    ldy vec_sx + 0
    ldx kWorldMitchGrpX, y
    lda kWorldMitchGrpCnt, y
    sta vec_proj_tmp1
    txa
    tay
    ldx #$00
  L_wi_mg_ld:
    lda _world_d4, y
    sta vec_v, x
    iny
    inx
    cpx #$06
    bne L_wi_mg_ld
  L_wi_mg_loop:
    ldy vec_proj_recip
    ldx kWorldMitchDy, y
    lda kWorldMitchDst, y
    tay
    clc
    lda vec_v + 0
    adc _world_d4 + 0, x
    sta _world_mitch_x, y
    lda vec_v + 1
    adc _world_d4 + 1, x
    sta _world_mitch_x + 16, y
    clc
    lda vec_v + 2
    adc _world_d4 + 2, x
    sta _world_mitch_y, y
    lda vec_v + 3
    adc _world_d4 + 3, x
    sta _world_mitch_y + 16, y
    clc
    lda vec_v + 4
    adc _world_d4 + 4, x
    sta _world_mitch_z, y
    lda vec_v + 5
    adc _world_d4 + 5, x
    sta _world_mitch_z + 16, y
    inc vec_proj_recip
    dec vec_proj_tmp1
    bne L_wi_mg_loop
    inc vec_sx + 0
    lda vec_sx + 0
    cmp #$04
    bne L_wi_mg_outer
    jmp L_wi_done

  // Shared axis initializer (uses ZP vec_sx+0/+1, vec_sy+0 as local temps).
  L_wi_init_axis:
    stx vec_sx + 0                // save d4_base
    sty vec_sy + 0                // save cam_col
    lda vec_proj_mul_b + 0
    sta vec_v + 0
    lda vec_proj_mul_b + 1
    sta vec_v + 1

    lda #$03
    sta vec_proj_tmp1             // component countdown
  L_wi_sp_loop:
    lda world_cam + 0, y
    sta vec_proj_mul_a + 0
    sta _world_d4 + 36, x         // d9[6].lo = val
    asl
    sta vec_proj_mul_b + 0        // dbl_lo
    sta _world_d4 + 48, x         // d9[8].lo = dbl
    lda world_cam + 1, y
    sta vec_proj_mul_a + 1
    sta _world_d4 + 37, x         // d9[6].hi = val
    rol
    sta vec_proj_mul_b + 1        // dbl_hi
    sta _world_d4 + 49, x         // d9[8].hi = dbl

    // d9[4] = 0
    lda #$00
    sta _world_d4 + 24, x
    sta _world_d4 + 25, x

    // d9[0] = -dbl
    sec
    sbc vec_proj_mul_b + 0
    sta _world_d4 + 0, x
    lda #$00
    sbc vec_proj_mul_b + 1
    sta _world_d4 + 1, x

    // hlf = val >> 1 (arithmetic) -> d9[5] and vec_proj_mul_b
    lda vec_proj_mul_a + 1
    cmp #$80
    ror
    sta vec_proj_mul_b + 1        // hlf_hi
    sta _world_d4 + 31, x         // d9[5].hi
    lda vec_proj_mul_a + 0
    ror
    sta vec_proj_mul_b + 0        // hlf_lo
    sta _world_d4 + 30, x         // d9[5].lo

    // d9[3] = -hlf
    sec
    lda #$00
    sbc vec_proj_mul_b + 0
    sta _world_d4 + 18, x
    lda #$00
    sbc vec_proj_mul_b + 1
    sta _world_d4 + 19, x

    // d9[2] = -val
    sec
    lda #$00
    sbc vec_proj_mul_a + 0
    sta _world_d4 + 12, x
    lda #$00
    sbc vec_proj_mul_a + 1
    sta _world_d4 + 13, x

    // d9[7] = val + hlf, d9[1] = -d9[7]
    clc
    lda vec_proj_mul_a + 0
    adc vec_proj_mul_b + 0
    sta vec_proj_recip            // sum_lo
    sta _world_d4 + 42, x
    lda vec_proj_mul_a + 1
    adc vec_proj_mul_b + 1
    sta _world_d4 + 43, x
    sta vec_proj_mul_b + 1        // sum_hi
    sec
    lda #$00
    sbc vec_proj_recip
    sta _world_d4 + 6, x
    lda #$00
    sbc vec_proj_mul_b + 1
    sta _world_d4 + 7, x

    inx
    inx
    tya
    clc
    adc #$06
    tay
    dec vec_proj_tmp1
    beq L_wi_sp_done
    jmp L_wi_sp_loop
  L_wi_sp_done:

    // Restore m into vec_proj_mul_b and compute grid = (m_hi + 2) >> 2.
    lda vec_v + 0
    sta vec_proj_mul_b + 0
    lda vec_v + 1
    sta vec_proj_mul_b + 1
    clc
    adc #$02
    lsr
    lsr
    sta vec_sx + 1                // grid

    // Build dvec = d9[8] << 1 into vec_v[0..5] (3 components).
    ldy vec_sx + 0
    ldx #$00
  L_wi_shl_dv:
    lda _world_d4 + 48, y
    asl
    sta vec_v, x
    lda _world_d4 + 49, y
    rol
    sta vec_v + 1, x
    iny
    iny
    inx
    inx
    cpx #$06
    bne L_wi_shl_dv

    // Precompute p_hi_mag = _world_grid_radius << 2 in vec_proj_tmp1 (saved).
    // Check world_cam.front[cam_col] > 0.
    ldy vec_sy + 0
    lda _world_grid_radius
    asl
    asl
    sta vec_sy + 0                // p_hi = +(r << 2)
    lda world_cam + 1, y
    bmi L_wi_ax_le
    ora world_cam + 0, y
    beq L_wi_ax_le

    // cam[0] > 0: negate vec_v, step = -1, start = grid + r, p_hi = +(r << 2).
    ldx #$00
  L_wi_neg_dv:
    sec
    lda #$00
    sbc vec_v, x
    sta vec_v, x
    lda #$00
    sbc vec_v + 1, x
    sta vec_v + 1, x
    inx
    inx
    cpx #$06
    bne L_wi_neg_dv
    lda #$ff
    sta vec_proj_tmp1             // step = -1
    lda vec_sx + 1
    clc
    adc _world_grid_radius
    sta vec_proj_recip            // start = grid + r
    jmp L_wi_ax_p

  L_wi_ax_le:
    lda #$01
    sta vec_proj_tmp1             // step = +1
    lda vec_sx + 1
    sec
    sbc _world_grid_radius
    sta vec_proj_recip            // start = grid - r
    sec
    lda #$00
    sbc vec_sy + 0
    sta vec_sy + 0                // p_hi = -(r << 2)

  L_wi_ax_p:
    // vec_proj_mul_a = ((p_hi + (grid << 2)) << 8) - m
    lda vec_sx + 1
    asl
    asl
    clc
    adc vec_sy + 0
    tax
    sec
    lda #$00
    sbc vec_proj_mul_b + 0
    sta vec_proj_mul_a + 0
    txa
    sbc vec_proj_mul_b + 1
    sta vec_proj_mul_a + 1
    rts

  L_wi_done:
  }
  // clang-format on
  vec_transform_inv(&world_cam, &_world_p_start, &_world_p_start);
}

// ---------------------------------------------------------------------------
// _world_build_obj_verts(obj_idx):
//   Single-pass 3D vertex builder into `_world_poly_verts[]`, tracking whether
//   any vertex has x >= 8. Returns num_verts (in vec_proj_tmp1) if at least
//   one vertex is past the near plane, or 0 to skip poly_draw_3d().
// ---------------------------------------------------------------------------
uint8_t _world_build_obj_verts(uint8_t obj_idx) {
  // clang-format off
  __asm {
    ldx obj_idx
    lda kWorldObjNumVerts, x
    sta vec_proj_tmp1             // num_verts (return value if any_in != 0)
    sta vec_proj_mul_b + 0        // countdown
    ldy kWorldObjFirstVert, x     // y = k
    lda #$00
    sta vec_proj_mul_b + 1        // any_in flag
    sta vec_proj_mul_a + 0        // out_off = 0, 6, 12, ...

  L_wo_loop:
    ldx kWorldObjX, y
    clc
    lda _world_vec_v + 0
    adc _world_d4 + 0, x
    sta vec_v + 0
    lda _world_vec_v + 1
    adc _world_d4 + 1, x
    sta vec_v + 1
    clc
    lda _world_vec_v + 2
    adc _world_d4 + 2, x
    sta vec_v + 2
    lda _world_vec_v + 3
    adc _world_d4 + 3, x
    sta vec_v + 3
    clc
    lda _world_vec_v + 4
    adc _world_d4 + 4, x
    sta vec_v + 4
    lda _world_vec_v + 5
    adc _world_d4 + 5, x
    sta vec_v + 5

    ldx kWorldObjY, y
    clc
    lda vec_v + 0
    adc _world_d4 + 54, x
    sta vec_v + 0
    lda vec_v + 1
    adc _world_d4 + 55, x
    sta vec_v + 1
    clc
    lda vec_v + 2
    adc _world_d4 + 56, x
    sta vec_v + 2
    lda vec_v + 3
    adc _world_d4 + 57, x
    sta vec_v + 3
    clc
    lda vec_v + 4
    adc _world_d4 + 58, x
    sta vec_v + 4
    lda vec_v + 5
    adc _world_d4 + 59, x
    sta vec_v + 5

    ldx vec_proj_mul_a + 0
    lda vec_v + 0
    sta _world_poly_verts + 0, x
    lda vec_v + 1
    sta _world_poly_verts + 1, x
    bmi L_wo_x_out
    bne L_wo_x_in
    lda vec_v + 0
    cmp #$08
    bcc L_wo_x_out
  L_wo_x_in:
    inc vec_proj_mul_b + 1
  L_wo_x_out:
    lda vec_v + 2
    sta _world_poly_verts + 2, x
    lda vec_v + 3
    sta _world_poly_verts + 3, x
    lda vec_v + 4
    sta _world_poly_verts + 4, x
    lda vec_v + 5
    sta _world_poly_verts + 5, x

    txa
    clc
    adc #$06
    sta vec_proj_mul_a + 0
    iny
    dec vec_proj_mul_b + 0
    beq L_wo_done
    jmp L_wo_loop
  L_wo_done:
    lda vec_proj_mul_b + 1
    bne L_wo_ret
    sta vec_proj_tmp1             // 0: all vertices behind near plane
  L_wo_ret:
  }
  // clang-format on
  return vec_proj_tmp1;
}

// ---------------------------------------------------------------------------
// _world_draw_box_points_core:
//   Inputs in ZP:
//     X          = start_idx & 0x0F (idx)
//     vec_sx + 1 = num_points (> 0) (cnt)
//     vec_sy + 0 = (color == kGfxDotGround) ? 0 : (0x08 | color) (col)
//   Keeps idx in X across all 4 frustum rejects, only saving to vec_sx+0 when
//   a dot survives all 4 checks and enters projection.
// ---------------------------------------------------------------------------
static __noinline void _world_draw_box_points_core(void) {
  // clang-format off
  __asm {
  L_wd_loop:
    // Forward cone check: vec_v.x = _world_vec_v.x + _world_mitch_x[idx] >= 9.
    clc
    lda _world_vec_v + 0
    adc _world_mitch_x, x
    sta vec_v + 0
    lda _world_vec_v + 1
    adc _world_mitch_x + 16, x
    bmi L_wd_fail
    sta vec_v + 1
    bne L_wd_x_ok
    lda vec_v + 0
    cmp #$09
    bcc L_wd_fail
  L_wd_x_ok:

    // Lateral cone check: |y| <= x; |y| -> vec_proj_mul_a, sign -> vec_v + 3.
    clc
    lda _world_vec_v + 2
    adc _world_mitch_y, x
    sta vec_proj_mul_a + 0
    lda _world_vec_v + 3
    adc _world_mitch_y + 16, x
    sta vec_v + 3
    bpl L_wd_y_pos
    sec
    lda #$00
    sbc vec_proj_mul_a + 0
    sta vec_proj_mul_a + 0
    lda #$00
    sbc vec_v + 3
  L_wd_y_pos:
    sta vec_proj_mul_a + 1
    cmp vec_v + 1
    bcc L_wd_y_ok
    bne L_wd_fail
    lda vec_v + 0
    cmp vec_proj_mul_a + 0
    bcs L_wd_y_ok
  L_wd_fail:
    dec vec_sx + 1
    bne L_wd_fail_adv
    rts
  L_wd_fail_adv:
    inx
    txa
    and #$0f
    tax
    jmp L_wd_loop
  L_wd_y_ok:

    // Vertical cone check: |z| <= (x >> 2); |z| -> vec_proj_mul_b, sign -> vec_v + 5.
    clc
    lda _world_vec_v + 4
    adc _world_mitch_z, x
    sta vec_proj_mul_b + 0
    lda _world_vec_v + 5
    adc _world_mitch_z + 16, x
    sta vec_v + 5
    bpl L_wd_z_pos
    sec
    lda #$00
    sbc vec_proj_mul_b + 0
    sta vec_proj_mul_b + 0
    lda #$00
    sbc vec_v + 5
  L_wd_z_pos:
    sta vec_proj_mul_b + 1
    lda vec_v + 1
    lsr
    sta vec_proj_recip
    lda vec_v + 0
    ror
    lsr vec_proj_recip
    ror
    sta vec_proj_tmp1
    lda vec_proj_recip
    cmp vec_proj_mul_b + 1
    bcc L_wd_fail
    bne L_wd_z_ok
    lda vec_proj_tmp1
    cmp vec_proj_mul_b + 0
    bcc L_wd_fail
  L_wd_z_ok:

    // Tightened lateral check for 320px viewport: |y| <= x - (x >> 2) (C=1 here).
    lda vec_v + 0
    sbc vec_proj_tmp1
    tay
    lda vec_v + 1
    sbc vec_proj_recip
    cmp vec_proj_mul_a + 1
    bcc L_wd_fail
    bne L_wd_proj
    cpy vec_proj_mul_a + 0
    bcc L_wd_fail
  L_wd_proj:
    stx vec_sx + 0

    // Normalize x into [128, 255] alongside |y| and |z|, then look up reciprocal.
    lda vec_v + 0
    sta vec_proj_tmp1
    lda vec_v + 1
    beq L_wd_shl_chk
  L_wd_shr:
    lsr
    ror vec_proj_tmp1
    lsr vec_proj_mul_a + 1
    ror vec_proj_mul_a + 0
    lsr vec_proj_mul_b + 1
    ror vec_proj_mul_b + 0
    cmp #$00
    bne L_wd_shr
    beq L_wd_lut
  L_wd_shl_chk:
    lda vec_proj_tmp1
    bmi L_wd_lut
  L_wd_shl:
    asl vec_proj_mul_a + 0
    asl vec_proj_mul_b + 0
    asl vec_proj_tmp1
    bpl L_wd_shl
  L_wd_lut:
    ldx vec_proj_tmp1
    lda vec_recip_lut - 128, x
    sta vec_proj_recip

    // Project horizontal coordinate -> vec_v+2 = hpx_lo = (160 - vec_sx) >> 1.
    lda vec_v + 3
    sta vec_mul8_lo
    lda vec_proj_mul_a + 0
    jsr vec_project_mul
    sta vec_proj_tmp1
    lda #$a0
    sec
    sbc vec_proj_tmp1
    sta vec_v + 2
    stx vec_proj_tmp1
    lda #$00
    sbc vec_proj_tmp1
    lsr
    ror vec_v + 2
    lda vec_v + 2
    cmp #$a0
    bcs L_wd_next

    // Project vertical coordinate -> vec_v+4 = py_lo = 56 - vec_sy_lo.
    lda vec_v + 5
    sta vec_mul8_lo
    lda vec_proj_mul_b + 0
    jsr vec_project_mul
    eor #$ff
    clc
    adc #$39
    cmp #$70
    bcs L_wd_next
    sta vec_v + 4

    // Plot into empty solid-ground cell only; vec_v+0/1 serves as scr_p.
    lsr
    lsr
    lsr
    tax
    lda mem_screen_row_ptrs, x
    sta vec_v + 0
    lda mem_screen_row_ptrs + 25, x
    sta vec_v + 1
    lda vec_v + 2
    lsr
    lsr
    tay
    lda (vec_v + 0), y
    cmp #$80
    bne L_wd_next
    lda vec_v + 2
    and #$03
    sta vec_proj_tmp1
    lda vec_v + 4
    and #$06
    asl
    ora vec_proj_tmp1
    sta vec_proj_tmp1
    lda vec_sy + 0
    bne L_wd_color
    lda vec_proj_tmp1
    ora #$e0
    sta (vec_v + 0), y
    bne L_wd_next
  L_wd_color:
    lda vec_proj_tmp1
    ora #$f0
    sta (vec_v + 0), y
    lda mem_color_row_ptrs, x
    sta vec_v + 0
    lda mem_color_row_ptrs + 14, x
    sta vec_v + 1
    lda vec_sy + 0
    sta (vec_v + 0), y
  L_wd_next:
    ldx vec_sx + 0
    jmp L_wd_fail
  }
  // clang-format on
}

void _world_draw_box_points(uint8_t start_idx, uint8_t num_points,
                            uint8_t color) {
  uint8_t idx = start_idx & 0x0F;
  uint8_t col = (color == kGfxDotGround) ? 0 : (uint8_t)(0x08 | color);
  // clang-format off
  __asm {
    lda num_points
    sta vec_sx + 1
    lda col
    sta vec_sy + 0
    ldx idx
    jsr _world_draw_box_points_core
  }
  // clang-format on
}

// ---------------------------------------------------------------------------
// _world_render_grid_walk():
//   Outer X/Y grid traversal over [-radius, +radius]^2 with:
//   - Direct 256-byte kWorldMap indexing: ((cx & 15) << 4) | ((cy & 31) >> 1)
//   - X-first bailout step on both Y and X walks.
// ---------------------------------------------------------------------------
void _world_render_grid_walk(void) {
  // clang-format off
  __asm {
    // 1. bailout = -max(|_world_d4[0].x|, |_world_d4[9].x|)
    ldx #$00
    jsr L_wg_abs_d4               // A:Y = |_world_d4[0].x|
    sty vec_proj_mul_a + 0
    sta vec_proj_mul_a + 1

    ldx #$36
    jsr L_wg_abs_d4               // A:Y = |_world_d4[9].x|
    cmp vec_proj_mul_a + 1
    bcc L_wg_max_ok
    bne L_wg_use_b
    cpy vec_proj_mul_a + 0
    bcc L_wg_max_ok
  L_wg_use_b:
    sty vec_proj_mul_a + 0
    sta vec_proj_mul_a + 1
  L_wg_max_ok:
    sec
    lda #$00
    sbc vec_proj_mul_a + 0
    sta _world_wg_bailout + 0
    lda #$00
    sbc vec_proj_mul_a + 1
    sta _world_wg_bailout + 1

    // 2. rxz_hi = (((183 + |_world_d4[0].z| + |_world_d4[9].z|) >> 8) + 2).
    //    For any orthonormal 8.8 world_cam, R_x <= 724 so (R_x >> 2) + 2 <= 183.
    ldx #$04
    jsr L_wg_abs_d4
    sty vec_proj_mul_a + 0
    sta vec_proj_mul_a + 1
    ldx #$3a                      // 54 + 4
    jsr L_wg_abs_d4
    sta vec_proj_tmp1             // b_hi
    clc
    tya
    adc vec_proj_mul_a + 0
    tax
    lda vec_proj_tmp1
    adc vec_proj_mul_a + 1
    tay
    clc
    txa
    adc #$b7                      // +183
    tya
    adc #$02                      // +2 high-byte rounding margin
    sta _world_wg_rxz_hi

    lda _world_start_cx
    sta _world_wg_cx
    sec
    lda #$00
    sbc _world_grid_radius
    sta _world_wg_x

  L_wg_x_loop:
    ldx #$05
  L_wg_cp_ps:
    lda _world_p_start, x
    sta _world_vec_v, x
    dex
    bpl L_wg_cp_ps

    lda _world_wg_x
    bpl L_wg_ax_pos
    eor #$ff
    clc
    adc #$01
  L_wg_ax_pos:
    sta _world_wg_abs_x

    lda _world_wg_cx
    asl
    asl
    asl
    asl
    sta _world_wg_row_off

    lda _world_start_cy
    sta _world_wg_cy
    sec
    lda #$00
    sbc _world_grid_radius
    sta _world_wg_y

  L_wg_y_loop:
    // map_type = kWorldMap[cx & 15][cy & 31] (nibble)
    lda _world_wg_cy
    and #$1f
    lsr                           // C = cy & 1, A = (cy & 31) >> 1
    ora _world_wg_row_off         // C preserved
    tay
    lda kWorldMap, y              // C preserved
    bcs L_wg_hi_nib
    and #$0f
    bcc L_wg_got_type
  L_wg_hi_nib:
    lsr
    lsr
    lsr
    lsr
  L_wg_got_type:
    beq L_wg_cull                 // MAP_NOTHING == 0
    tax                           // preserve map_type in X across 8-bit cull

    // Fast 8-bit high-byte lateral cull: for any orthonormal 8.8 world_cam,
    // R_x + R_y <= 1448 (< 6 * 256), so ((R_x + R_y) >> 8) + 2 <= 7.
    lda _world_vec_v + 1
    clc
    adc #$07
    bmi L_wg_cull
    sta vec_proj_tmp1
    lda _world_vec_v + 3
    bpl L_wg_cyh_pos
    eor #$ff
  L_wg_cyh_pos:
    cmp vec_proj_tmp1
    bcs L_wg_cull

    // Fast 8-bit high-byte vertical cull: |vz|_hi >= (max(vx_hi,0)>>2) + rxz_hi.
    lda _world_vec_v + 5
    bpl L_wg_czh_pos
    eor #$ff
  L_wg_czh_pos:
    beq L_wg_czh_ok               // |vz| < 256 -> fast pass (9c)
    sta vec_proj_tmp1
    lda _world_vec_v + 1
    bpl L_wg_vxh_pos
    lda #$00
  L_wg_vxh_pos:
    lsr
    lsr
    clc
    adc _world_wg_rxz_hi
    sec
    sbc vec_proj_tmp1
    beq L_wg_cull
    bpl L_wg_czh_ok
  L_wg_cull:
    jmp L_wg_y_step

  L_wg_czh_ok:
    txa                           // restore map_type
    cmp #$07                      // kWorldMapObjStart == 7
    bcs L_wg_obj

    // Dot cell (map_type in 1..6; X already holds map_type):
    // kGfxDotGround == 8 -> 0, palette color 0..7 -> 0x08 | color via EOR #$08.
#ifdef __ENABLE_COLOR_SCHEMES__
    lda color_scheme + 8, x
#else
    lda KWorldDotColors, x
#endif
    eor #$08
    sta vec_sy + 0                // wd_col

    // num_points = kWorldNumPoints[(radius & 4) + max(abs_x, abs(y))]
    lda _world_wg_y
    bpl L_wg_ay_pos
    eor #$ff
    clc
    adc #$01
  L_wg_ay_pos:
    cmp _world_wg_abs_x
    bcs L_wg_d_ok
    lda _world_wg_abs_x
  L_wg_d_ok:
    sta vec_proj_tmp1
    lda _world_grid_radius
    and #$04
    clc
    adc vec_proj_tmp1
    tay
    lda kWorldNumPoints, y
    sta vec_sx + 1                // wd_cnt

    lda _world_wg_cx
    asl
    clc
    adc _world_wg_cy
    and #$0f
    tax
    jsr _world_draw_box_points_core
    jmp L_wg_y_step

  L_wg_obj:
    sec
    sbc #$07
    sta _world_obj_idx
    jsr _world_render_object_cur

  L_wg_y_step:
    lda _world_wg_y
    cmp _world_grid_radius
    beq L_wg_x_step
    inc _world_wg_y

    // Step _world_vec_v.x FIRST and bail out immediately if < bailout.
    clc
    lda _world_vec_v + 0
    adc _world_dy_vec + 0
    sta _world_vec_v + 0
    tax
    lda _world_vec_v + 1
    adc _world_dy_vec + 1
    sta _world_vec_v + 1
    tay
    txa
    sec
    sbc _world_wg_bailout + 0
    tya
    sbc _world_wg_bailout + 1
    bmi L_wg_x_step

    clc
    lda _world_vec_v + 2
    adc _world_dy_vec + 2
    sta _world_vec_v + 2
    lda _world_vec_v + 3
    adc _world_dy_vec + 3
    sta _world_vec_v + 3
    clc
    lda _world_vec_v + 4
    adc _world_dy_vec + 4
    sta _world_vec_v + 4
    lda _world_vec_v + 5
    adc _world_dy_vec + 5
    sta _world_vec_v + 5
    lda _world_wg_cy
    clc
    adc _world_step_y
    sta _world_wg_cy
    jmp L_wg_y_loop

  L_wg_x_step:
    lda _world_wg_x
    cmp _world_grid_radius
    beq L_wg_done
    inc _world_wg_x

    // Step _world_p_start.x FIRST and bail out immediately if < bailout.
    clc
    lda _world_p_start + 0
    adc _world_dx_vec + 0
    sta _world_p_start + 0
    tax
    lda _world_p_start + 1
    adc _world_dx_vec + 1
    sta _world_p_start + 1
    tay
    txa
    sec
    sbc _world_wg_bailout + 0
    tya
    sbc _world_wg_bailout + 1
    bmi L_wg_done

    ldx #$02
  L_wg_x_yz:
    clc
    lda _world_p_start, x
    adc _world_dx_vec, x
    sta _world_p_start, x
    lda _world_p_start + 1, x
    adc _world_dx_vec + 1, x
    sta _world_p_start + 1, x
    inx
    inx
    cpx #$06
    bne L_wg_x_yz

    lda _world_wg_cx
    clc
    adc _world_step_x
    sta _world_wg_cx
    jmp L_wg_x_loop

  L_wg_abs_d4:
    ldy _world_d4 + 0, x
    lda _world_d4 + 1, x
    bpl L_wg_abs_ret
    sec
    lda #$00
    sbc _world_d4 + 0, x
    tay
    lda #$00
    sbc _world_d4 + 1, x
  L_wg_abs_ret:
    rts

  L_wg_done:
  }
  // clang-format on
}

#pragma optimize(pop)

#endif // __OSCAR64__
