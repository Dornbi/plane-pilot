#ifdef __OSCAR64__

#include <stddef.h>

#include "poly_asm.h"

static_assert(sizeof(vec3_t) == 6 && offsetof(vec3_t, x) == 0 &&
                  offsetof(vec3_t, y) == 2 && offsetof(vec3_t, z) == 4,
              "poly_asm expects packed vec3_t");
static_assert(kViewportWidth == 40 && kViewportHeight == 14 &&
                  kPolyMaxVertices == 6 && kPolyMax2dVertices == 12,
              "poly_asm geometry mismatch");

// Per-call scratch buffers for 3D-to-2D clipping, projection, and scanline
// rasterization. Lives in fpilot's dead-row scratch ($EA30-$EBE7), bss2 in
// ppilot/polydemo, or cld_bss in target_test.
#ifdef __TARGET_TEST__
#pragma bss(cld_bss)
#elif defined(__FPILOT__)
#pragma bss(scratch)
#else
#pragma bss(bss2)
#endif

vec3_t _poly_clip3_buf[kPolyMaxVertices + 4];
uint8_t _poly_min_x[2 * kViewportHeight];
uint8_t _poly_max_x[2 * kViewportHeight];

// Unified striped ping-pong buffers for 2D projected/clipped vertices:
//   X coords: indices [0 .. 23]  (buf 0: 0..11, buf 1: 12..23)
//   Y coords: indices [24 .. 47] (buf 0: 24..35, buf 1: 36..47)
// Projection writes directly into buf 0 (merging the old proj_buf into buf 0),
// and _poly_clip_2d_edge handles both X and Y edges with one shared body via
// axis_off / other_off in {0, 24}.
static const uint8_t kPoly2dAxisY = 2 * kPolyMax2dVertices; // 24
static uint8_t _poly_2d_lo[4 * kPolyMax2dVertices];
static uint8_t _poly_2d_hi[4 * kPolyMax2dVertices];

// Packed into a single struct so oscar64 -Oz keeps all per-call polygon state
// in scratch instead of promoting scalars into zero page (which would evict
// hotter world/opponent/flight variables out of ZP).
struct poly_state_t {
  uint8_t in_lo;          // 0
  uint8_t in_hi;          // 1
  uint8_t end3;           // 2
  uint8_t idx;            // 3  (also clip limit in _poly_clip_2d_edge)
  uint8_t cnt;            // 4
  uint8_t all_out;        // 5
  uint8_t edges;          // 6
  uint8_t prev;           // 7
  uint8_t curr;           // 8
  uint8_t out_off;        // 9
  uint8_t prev_in;        // 10
  uint8_t curr_in;        // 11
  uint8_t edge_id;        // 12
  uint8_t src_base;       // 13
  uint8_t dst_base;       // 14
  uint8_t src_end;        // 15
  volatile uint8_t flag;  // 16 (volatile: C reads after __asm updates)
  uint8_t mag_or;         // 17 (other_off in _poly_clip_2d_edge)
  uint8_t axis_off;       // 18 (axis_off in _poly_clip_2d_edge)
  uint8_t nx;             // 19 (nx, nx_hi, py, pz = contiguous 6-byte vec3_t)
  uint8_t nx_hi;          // 20 (always 0 during _poly_clip_near_mixed)
  int16_t py;             // 21 (also saved prev+other_off in _poly_clip_2d_edge)
  int16_t pz;             // 23
  int16_t dy;             // 25
  int16_t dz;             // 27
};

static poly_state_t _ps;

#define PS_IN_LO 0
#define PS_IN_HI 1
#define PS_END3 2
#define PS_IDX 3
#define PS_CNT 4
#define PS_ALL_OUT 5
#define PS_EDGES 6
#define PS_PREV 7
#define PS_CURR 8
#define PS_OUT_OFF 9
#define PS_PREV_IN 10
#define PS_CURR_IN 11
#define PS_EDGE_ID 12
#define PS_SRC_BASE 13
#define PS_DST_BASE 14
#define PS_SRC_END 15
#define PS_FLAG 16
#define PS_MAG_OR 17
#define PS_NX 18
#define PS_OUT_X 19
#define PS_OUT_X_HI 20
#define PS_PY 21
#define PS_PZ 23
#define PS_DY 25
#define PS_DZ 27

static_assert(offsetof(poly_state_t, in_lo) == PS_IN_LO &&
                  offsetof(poly_state_t, in_hi) == PS_IN_HI &&
                  offsetof(poly_state_t, end3) == PS_END3 &&
                  offsetof(poly_state_t, idx) == PS_IDX &&
                  offsetof(poly_state_t, cnt) == PS_CNT &&
                  offsetof(poly_state_t, all_out) == PS_ALL_OUT &&
                  offsetof(poly_state_t, edges) == PS_EDGES &&
                  offsetof(poly_state_t, prev) == PS_PREV &&
                  offsetof(poly_state_t, curr) == PS_CURR &&
                  offsetof(poly_state_t, out_off) == PS_OUT_OFF &&
                  offsetof(poly_state_t, prev_in) == PS_PREV_IN &&
                  offsetof(poly_state_t, curr_in) == PS_CURR_IN &&
                  offsetof(poly_state_t, edge_id) == PS_EDGE_ID &&
                  offsetof(poly_state_t, src_base) == PS_SRC_BASE &&
                  offsetof(poly_state_t, dst_base) == PS_DST_BASE &&
                  offsetof(poly_state_t, src_end) == PS_SRC_END &&
                  offsetof(poly_state_t, flag) == PS_FLAG &&
                  offsetof(poly_state_t, mag_or) == PS_MAG_OR &&
                  offsetof(poly_state_t, axis_off) == PS_NX &&
                  offsetof(poly_state_t, nx) == PS_OUT_X &&
                  offsetof(poly_state_t, nx_hi) == PS_OUT_X_HI &&
                  offsetof(poly_state_t, py) == PS_PY &&
                  offsetof(poly_state_t, pz) == PS_PZ &&
                  offsetof(poly_state_t, dy) == PS_DY &&
                  offsetof(poly_state_t, dz) == PS_DZ,
              "poly_state_t offsets mismatch");

#pragma bss(bss)

static const uint8_t kPolyClipLim[4] = {0, 79, 0, 27};

// Shared 3D frustum early-reject core.
// Reads _ps.end3 bytes (6 per vertex) from (_ps.in_lo, _ps.in_hi).
// Every vertex has x >= 8. Screen x in [0, 79] requires |y|/x <= ~0.633 < 0.75
// (yx = ux - (ux >> 2)), and screen y in [0, 27] requires |z|/x <= ~0.227 <
// 0.25 (zx = ux >> 2). Returns non-zero (in vec_v+2) iff every vertex lies past
// the same frustum plane.
static __noinline uint8_t _poly_frustum_core(void) {
  // clang-format off
  __asm {
        lda _ps+PS_IN_LO;
        sta vec_proj_mul_a+0;
        lda _ps+PS_IN_HI;
        sta vec_proj_mul_a+1;
        lda _ps+PS_END3;
        sta vec_v+0;        // vec_v+0 = end3
        lda #$0f;
        sta vec_v+2;        // vec_v+2 = out3
        ldy #$00;
    L_out3_loop:
        // Load x = (lo, hi) -> vec_proj_mul_b; zx = ux >> 2 -> vec_sx.
        lda (vec_proj_mul_a), y;
        sta vec_proj_mul_b+0;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_proj_mul_b+1;
        lsr;
        sta vec_sx+1;
        lda vec_proj_mul_b+0;
        ror;
        lsr vec_sx+1;
        ror;
        sta vec_sx+0;       // vec_sx = zx = ux >> 2

        // yx = ux - zx -> vec_sy (0.75 * ux <= yx <= 0.75 * ux + 0.75).
        sec;
        lda vec_proj_mul_b+0;
        sbc vec_sx+0;
        sta vec_sy+0;
        lda vec_proj_mul_b+1;
        sbc vec_sx+1;
        sta vec_sy+1;

        // Load y -> (vec_v+4, (vec_proj_mul_a),y).
        ldx #$00;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+4;
        iny;
        lda (vec_proj_mul_a), y;

        bmi L_y_neg;
        sec;
        lda vec_sy+0;
        sbc vec_v+4;
        lda vec_sy+1;
        sbc (vec_proj_mul_a), y;
        bcs L_chk_z;
        inx;
        bne L_chk_z;
    L_y_neg:
        clc;
        lda vec_sy+0;
        adc vec_v+4;
        lda vec_sy+1;
        adc (vec_proj_mul_a), y;
        bcs L_chk_z;
        ldx #$02;
    L_chk_z:
        // Load z -> (vec_v+4, (vec_proj_mul_a),y).
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+4;
        iny;
        lda (vec_proj_mul_a), y;

        bmi L_z_neg;
        sec;
        lda vec_sx+0;
        sbc vec_v+4;
        lda vec_sx+1;
        sbc (vec_proj_mul_a), y;
        bcs L_next_v;
        txa;
        ora #$04;
        tax;
        bne L_next_v;
    L_z_neg:
        clc;
        lda vec_sx+0;
        adc vec_v+4;
        lda vec_sx+1;
        adc (vec_proj_mul_a), y;
        bcs L_next_v;
        txa;
        ora #$08;
        tax;
    L_next_v:
        txa;
        and vec_v+2;
        sta vec_v+2;
        beq L_out3_done;
        iny;
        cpy vec_v+0;
        bne L_out3_loop;
    L_out3_done:
  }
  // clang-format on
  return (uint8_t)vec_v.y;
}

bool _poly_frustum_all_out(uint8_t end3) {
  // clang-format off
  __asm {
        lda #<_poly_clip3_buf;
        sta _ps+PS_IN_LO;
        lda #>_poly_clip3_buf;
        sta _ps+PS_IN_HI;
        lda end3;
        sta _ps+PS_END3;
  }
  // clang-format on
  return _poly_frustum_core() != 0;
}

// Clips mixed 3D polygon (_ps.in_lo/hi, length _ps.end3) against x = 8 into
// _poly_clip3_buf, then points _ps.in_lo/hi at _poly_clip3_buf and updates
// _ps.end3 to the clipped byte length.
static __noinline void _poly_clip_near_mixed(void) {
  // clang-format off
  __asm {
        lda _ps+PS_IN_LO;
        sta vec_proj_mul_a+0;
        lda _ps+PS_IN_HI;
        sta vec_proj_mul_a+1;

        lda _ps+PS_END3;
        sec;
        sbc #$06;
        sta _ps+PS_PREV;
        sta _ps+PS_CURR;
        lda #$ff;
        sta _ps+PS_PREV_IN;     // sentinel: first loop test sets prev_in

    L_cn_loop:
        ldy _ps+PS_CURR;
        iny;
        lda (vec_proj_mul_a), y;
        bmi L_cn_ci0;
        bne L_cn_ci1;
        dey;
        lda (vec_proj_mul_a), y;
        cmp #$08;
        bcs L_cn_ci1;
    L_cn_ci0:
        lda #$00;
        beq L_cn_cmp;
    L_cn_ci1:
        lda #$01;
    L_cn_cmp:
        bit _ps+PS_PREV_IN;     // N=1 only on initial prev test ($ff)
        bpl L_cn_not_init;
        sta _ps+PS_PREV_IN;
        lda #$00;
        sta _ps+PS_CURR;
        sta _ps+PS_OUT_OFF;
        sta _ps+PS_OUT_X_HI;
        beq L_cn_loop;
    L_cn_not_init:
        cmp _ps+PS_PREV_IN;
        bne L_cn_cross;

        lsr;
        bcc L_cn_adv;
    L_cn_copy:
        ldy _ps+PS_CURR;
        ldx _ps+PS_OUT_OFF;
        lda #$06;
        sta vec_proj_tmp1;
    L_cn_cp:
        lda (vec_proj_mul_a), y;
        sta _poly_clip3_buf, x;
        iny;
        inx;
        dec vec_proj_tmp1;
        bne L_cn_cp;
        stx _ps+PS_OUT_OFF;
    L_cn_adv:
        lda _ps+PS_CURR;
        sta _ps+PS_PREV;
        clc;
        adc #$06;
        sta _ps+PS_CURR;
        cmp _ps+PS_END3;
        bne L_cn_loop;
        jmp L_cn_finish;

    L_cn_mag:
        bpl L_cn_mag_pos;
        eor #$ff;
    L_cn_mag_pos:
        ora vec_proj_tmp1;
        sta vec_proj_tmp1;
        rts;

    // Adds vec_mulfrac((frac_q1 << 8) | frac_q0, _ps.dy[X]) to _ps.py[X]
    // (X = 0 for Y, 2 for Z), skipping zero deltas and preserving frac_q0/q1.
    L_cn_add_mf:
        lda _ps+PS_DY+0, x;
        sta vec_sx+0;
        lda _ps+PS_DY+1, x;
        sta vec_sx+1;
        ora vec_sx+0;
        beq L_cn_amf_ret;
        stx vec_sy+0;
        jsr vec_mulfrac_core;
        ldx vec_sy+0;
        clc;
        lda _ps+PS_PY+0, x;
        adc vec_mul_res+0;
        sta _ps+PS_PY+0, x;
        lda _ps+PS_PY+1, x;
        adc vec_mul_res+1;
        sta _ps+PS_PY+1, x;
    L_cn_amf_ret:
        rts;

    L_cn_cross:
        sta _ps+PS_CURR_IN;
        // Load prev->x into vec_v[0..1] and prev->y/z into _ps.py/_ps.pz,
        // accumulating _mag_hi(py) and _mag_hi(pz) into vec_proj_tmp1.
        ldy _ps+PS_PREV;
        lda (vec_proj_mul_a), y;
        sta vec_v+0;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+1;
        iny;
        lda (vec_proj_mul_a), y;
        sta _ps+PS_PY+0;
        iny;
        lda (vec_proj_mul_a), y;
        sta _ps+PS_PY+1;
        bpl L_cn_m1;
        eor #$ff;
    L_cn_m1:
        sta vec_proj_tmp1;
        iny;
        lda (vec_proj_mul_a), y;
        sta _ps+PS_PZ+0;
        iny;
        lda (vec_proj_mul_a), y;
        sta _ps+PS_PZ+1;
        jsr L_cn_mag;

        // a = 8 - prev->x -> (frac_r0, vec_v+2).
        sec;
        lda #$08;
        sbc vec_v+0;
        sta frac_r0;
        lda #$00;
        sbc vec_v+1;
        sta vec_v+2;

        // dx = curr->x - prev->x -> (frac_d0, vec_v+3), dy -> dy, dz -> dz,
        // accumulating _mag_hi of curr->y, dy, curr->z, dz into vec_proj_tmp1.
        ldy _ps+PS_CURR;
        sec;
        lda (vec_proj_mul_a), y;
        sbc vec_v+0;
        sta frac_d0;
        iny;
        lda (vec_proj_mul_a), y;
        sbc vec_v+1;
        sta vec_v+3;

        iny;
        sec;
        lda (vec_proj_mul_a), y;
        sbc _ps+PS_PY+0;
        sta _ps+PS_DY+0;
        iny;
        lda (vec_proj_mul_a), y;
        tax;
        sbc _ps+PS_PY+1;
        sta _ps+PS_DY+1;
        jsr L_cn_mag;
        txa;
        jsr L_cn_mag;

        iny;
        sec;
        lda (vec_proj_mul_a), y;
        sbc _ps+PS_PZ+0;
        sta _ps+PS_DZ+0;
        iny;
        lda (vec_proj_mul_a), y;
        tax;
        sbc _ps+PS_PZ+1;
        sta _ps+PS_DZ+1;
        jsr L_cn_mag;
        txa;
        jsr L_cn_mag;

        // Lift by 8 when all six magnitudes fit (mag_or < 15), BEFORE calling
        // vec_frac16_run (which uses vec_proj_tmp1 for sign).
        ldx #$08;
        cmp #$0f;
        bcs L_cn_nolift;
        ldx #$40;
        ldy #$03;
    L_cn_shl:
        asl _ps+PS_PY+0;
        rol _ps+PS_PY+1;
        asl _ps+PS_PZ+0;
        rol _ps+PS_PZ+1;
        asl _ps+PS_DY+0;
        rol _ps+PS_DY+1;
        asl _ps+PS_DZ+0;
        rol _ps+PS_DZ+1;
        dey;
        bne L_cn_shl;
    L_cn_nolift:
        stx _ps+PS_OUT_X;

        // Skip vec_frac16_run and both vec_mulfrac_core calls when dy == 0 && dz == 0
        // (e.g. runway side edges aligned with camera X on level ground).
        lda _ps+PS_DY+0;
        ora _ps+PS_DY+1;
        ora _ps+PS_DZ+0;
        ora _ps+PS_DZ+1;
        beq L_cn_emit_start;
        // t = vec_frac16(a, dx) -> (frac_q0, frac_q1).
        lda vec_v+2;
        ldy vec_v+3;
        jsr vec_frac16_run;
        ldx #$00;
        jsr L_cn_add_mf;
        ldx #$02;
        jsr L_cn_add_mf;

    L_cn_emit_start:
        ldx _ps+PS_OUT_OFF;
        ldy #$00;
    L_cn_emit:
        lda _ps+PS_OUT_X, y;
        sta _poly_clip3_buf, x;
        inx;
        iny;
        cpy #$06;
        bne L_cn_emit;
        stx _ps+PS_OUT_OFF;
        // Reload input pointer (vec_mul_ab clobbered vec_proj_mul_a).
        lda _ps+PS_IN_LO;
        sta vec_proj_mul_a+0;
        lda _ps+PS_IN_HI;
        sta vec_proj_mul_a+1;
        lda _ps+PS_CURR_IN;
        sta _ps+PS_PREV_IN;
        lsr;
        bcc L_cn_to_adv;
        jmp L_cn_copy;
    L_cn_to_adv:
        jmp L_cn_adv;

    L_cn_finish:
        lda #<_poly_clip3_buf;
        sta _ps+PS_IN_LO;
        lda #>_poly_clip3_buf;
        sta _ps+PS_IN_HI;
        lda _ps+PS_OUT_OFF;
        sta _ps+PS_END3;
  }
  // clang-format on
}

// Clips the 2D polygon in buf[_ps.src_base .. _ps.src_base + _ps.cnt) against
// viewport edge _ps.edge_id (0: x>=0, 1: x<=79, 2: y>=0, 3: y<=27), writing to
// buf[_ps.dst_base ..) and updating _ps.cnt. Uses unified _poly_2d_lo/hi[48]
// (X at +0, Y at +24) so all four edges share one compact clipping body.
static __noinline void _poly_clip_2d_edge(void) {
  // clang-format off
  __asm {
        // Cache hot loop constants in free vec_v ZP (preserved by vec_div_core,
        // vec_mul_ab, vec_frac16_run, and vec_mulfrac_core).
        ldx _ps+PS_EDGE_ID;
        stx vec_v+0;
        lda kPolyClipLim, x;
        sta _ps+PS_IDX;
        lda #$00;
        ldy #$18;               // kPoly2dAxisY = 24
        cpx #$02;
        bcc L_c2_ax_ok;
        lda #$18;
        ldy #$00;
    L_c2_ax_ok:
        sta _ps+PS_NX;          // axis_off (0 or 24)
        sty _ps+PS_MAG_OR;      // other_off (24 or 0)

        lda _ps+PS_SRC_BASE;
        sta _ps+PS_CURR;
        clc;
        adc _ps+PS_CNT;
        sta _ps+PS_SRC_END;
        sta vec_v+3;
        tax;
        dex;                    // X = last vertex (initial prev)
        stx _ps+PS_PREV;
        lda _ps+PS_SRC_BASE;
        eor #$0c;               // kPolyMax2dVertices = 12
        sta _ps+PS_DST_BASE;
        tay;
        lda #$ff;
        sta vec_v+2;            // sentinel: first test initializes prev_in

    L_c2_loop:
        // Fast inline 4-edge inside test (also used once for initial prev).
        lda vec_v+0;
        lsr;
        bcs L_c2_lmax;
        bne L_c2_le2;
        lda _poly_2d_hi, x;
        bpl L_c2_lin;
        bmi L_c2_lout;
    L_c2_le2:
        lda _poly_2d_hi+24, x;
        bpl L_c2_lin;
        bmi L_c2_lout;
    L_c2_lmax:
        bne L_c2_le3;
        lda _poly_2d_hi, x;
        bmi L_c2_lin;
        bne L_c2_lout;
        lda _poly_2d_lo, x;
        cmp #$50;
        bcc L_c2_lin;
        bcs L_c2_lout;
    L_c2_le3:
        lda _poly_2d_hi+24, x;
        bmi L_c2_lin;
        bne L_c2_lout;
        lda _poly_2d_lo+24, x;
        cmp #$1c;
        bcc L_c2_lin;
    L_c2_lout:
        lda #$00;
        beq L_c2_lcmp;
    L_c2_lin:
        lda #$01;
    L_c2_lcmp:
        bit vec_v+2;            // N=1 only on initial prev test ($ff)
        bpl L_c2_not_init;
        sta vec_v+2;
        sta _ps+PS_PREV_IN;
        ldx _ps+PS_CURR;
        bpl L_c2_loop;          // curr is always in [0..23] (< 128)
    L_c2_not_init:
        cmp vec_v+2;
        bne L_c2_cross;
        // Same side: if inside (A == 1), copy vertex X -> Y (both X and Y coords).
        lsr;
        bcc L_c2_next;
    L_c2_copy:
        lda _poly_2d_lo, x;
        sta _poly_2d_lo, y;
        lda _poly_2d_hi, x;
        sta _poly_2d_hi, y;
        lda _poly_2d_lo+24, x;
        sta _poly_2d_lo+24, y;
        lda _poly_2d_hi+24, x;
        sta _poly_2d_hi+24, y;
        iny;
    L_c2_next:
        stx _ps+PS_PREV;
        inx;
        cpx vec_v+3;
        bne L_c2_loop;
        sty _ps+PS_OUT_OFF;
        jmp L_c2_done;

    L_c2_cross:
        sta _ps+PS_CURR_IN;
        stx _ps+PS_CURR;
        sty _ps+PS_OUT_OFF;

        // Write clipped axis coordinate = lim at dst + axis_off.
        tya;
        clc;
        adc _ps+PS_NX;
        tay;
        lda _ps+PS_IDX;
        sta _poly_2d_lo, y;
        lda #$00;
        sta _poly_2d_hi, y;

        // Y = prev + other_off (saved in _ps.py for post-lerp), X = curr + other_off.
        txa;
        clc;
        adc _ps+PS_MAG_OR;
        tax;
        lda _ps+PS_PREV;
        clc;
        adc _ps+PS_MAG_OR;
        sta _ps+PS_PY;
        tay;
        // d = other[curr] - other[prev] -> vec_sx (preserved by vec_div_core / vec_frac16_run).
        sec;
        lda _poly_2d_lo, x;
        sbc _poly_2d_lo, y;
        sta vec_sx+0;
        lda _poly_2d_hi, x;
        sbc _poly_2d_hi, y;
        sta vec_sx+1;
        beq L_c2_d0;
        cmp #$ff;
        bne L_c2_dbig;
        ldx vec_sx+0;
        bne L_c2_dsm;
    L_c2_dbig:
        lda #$03;
        bne L_c2_df;
    L_c2_d0:
        ldx vec_sx+0;
        bne L_c2_dsm;
        stx vec_mul_res+0;
        stx vec_mul_res+1;
        jmp L_c2_store_other;
    L_c2_dsm:
        lda #$02;
    L_c2_df:
        sta vec_sy+1;

        // Y = prev + axis_off, X = curr + axis_off.
        lda _ps+PS_CURR;
        clc;
        adc _ps+PS_NX;
        tax;
        lda _ps+PS_PREV;
        clc;
        adc _ps+PS_NX;
        tay;
        // arg_a = lim - axis[prev] -> (frac_r0, vec_sy+0).
        sec;
        lda _ps+PS_IDX;
        sbc _poly_2d_lo, y;
        sta frac_r0;
        lda #$00;
        sbc _poly_2d_hi, y;
        sta vec_sy+0;
        // arg_b = axis[curr] - axis[prev] -> (frac_d0, Y).
        sec;
        lda _poly_2d_lo, x;
        sbc _poly_2d_lo, y;
        sta frac_d0;
        lda _poly_2d_hi, x;
        sbc _poly_2d_hi, y;
        tay;

        lda vec_sy+1;
        cmp #$02;
        bne L_c2_big_frac;
        // Fast 8.8 path: vec_fastmul8p8(vec_div8p8(arg_a, arg_b), d).
        lda vec_sy+0;
        jsr vec_div_core;
        lda frac_q0;
        sta vec_proj_mul_a+0;
        lda frac_q1;
        sta vec_proj_mul_a+1;
        lda vec_sx+0;
        sta vec_proj_mul_b+0;
        lda vec_sx+1;
        sta vec_proj_mul_b+1;
        jsr vec_mul_ab;
        jmp L_c2_store_other;

    L_c2_big_frac:
        // Exact 16-bit frac path: vec_mulfrac(vec_frac16(arg_a, arg_b), d).
        lda vec_sy+0;
        jsr vec_frac16_run;
        jsr vec_mulfrac_core;

    L_c2_store_other:
        // Store dest[other] = prev[other] + vec_mul_res, advance out_off.
        ldx _ps+PS_PY;
        lda _ps+PS_OUT_OFF;
        clc;
        adc _ps+PS_MAG_OR;
        tay;
        clc;
        lda _poly_2d_lo, x;
        adc vec_mul_res+0;
        sta _poly_2d_lo, y;
        lda _poly_2d_hi, x;
        adc vec_mul_res+1;
        sta _poly_2d_hi, y;
        inc _ps+PS_OUT_OFF;
        lda _ps+PS_CURR_IN;
        sta vec_v+2;
        ldx _ps+PS_CURR;
        ldy _ps+PS_OUT_OFF;
        lsr;
        bcc L_c2_to_next;
        jmp L_c2_copy;
    L_c2_to_next:
        jmp L_c2_next;

    L_c2_done:
        lda _ps+PS_OUT_OFF;
        sec;
        sbc _ps+PS_DST_BASE;
        sta _ps+PS_CNT;
  }
  // clang-format on
}

// Full 3D-to-2D polygon clipping & projection pipeline:
//   1. Fast near-plane pre-scan (skips copy when all x >= 8; rejects if all
//      x < 8; calls _poly_clip_near_mixed only on near-crossing polygons).
//   2. 3D frustum early-reject (_poly_frustum_core).
//   3. Vertex projection (vec_project_nocull) + sub-pixel rounding + outcodes
//      directly into striped buf 0.
//   4. 2D viewport edge clipping (_poly_clip_2d_edge) ping-ponging buf 0/1.
//   5. Pack surviving vertices into packed {x, y} vertices_2d.
uint8_t _poly_project_vertices(const vec3_t *vertices_3d, uint8_t num_vertices,
                               int8_t *vertices_2d) {
  if (num_vertices < 3) {
    return 0;
  }

  // Save input pointer, compute _ps.end3 = num_vertices * 6, and pre-scan
  // x >= 8. Returns status in _ps.flag: 0 = all out, 1 = all in, 2 = mixed.
  // clang-format off
  __asm {
        lda vertices_3d+0;
        sta _ps+PS_IN_LO;
        sta vec_proj_mul_a+0;
        lda vertices_3d+1;
        sta _ps+PS_IN_HI;
        sta vec_proj_mul_a+1;

        lda num_vertices;
        asl;
        sta vec_proj_tmp1;      // 2*n
        asl;                    // 4*n
        clc;
        adc vec_proj_tmp1;      // 6*n
        sta _ps+PS_END3;

        // Pre-scan x >= 8 across vertices_3d[0..n-1].
        ldx #$00;               // X = in_cnt
        ldy #$01;               // offset of x_hi
    L_ps_loop:
        lda (vec_proj_mul_a), y;
        bmi L_ps_next;
        bne L_ps_in;
        dey;
        lda (vec_proj_mul_a), y;
        iny;
        cmp #$08;
        bcc L_ps_next;
    L_ps_in:
        inx;
    L_ps_next:
        tya;
        clc;
        adc #$06;
        tay;
        cmp _ps+PS_END3;
        bcc L_ps_loop;

        txa;
        beq L_ps_store;         // 0 -> all out
        cmp num_vertices;
        beq L_ps_all_in;
        lda #$02;               // mixed
        bne L_ps_store;
    L_ps_all_in:
        lda #$01;
    L_ps_store:
        sta _ps+PS_FLAG;
  }
  // clang-format on

  uint8_t fl = _ps.flag;
  if (fl == 0) {
    return 0;
  }
  if (fl == 2) {
    _poly_clip_near_mixed();
    if (_ps.end3 < (uint8_t)(3 * sizeof(vec3_t))) {
      return 0;
    }
  }
  if (_poly_frustum_core()) {
    return 0;
  }

  // Project every vertex in (_ps.in_lo/hi)[0.._ps.end3) into striped buf 0.
  // Kept in a single __asm block with `jsr vec_project_nocull` so oscar64
  // cannot dead-store-eliminate vec_v+3..5 across an __asm/C boundary, and to
  // eliminate per-vertex C loop overhead. Call-graph safety: _poly_clip_2d_edge
  // already exposes vec_div8p8() in _poly_project_vertices's C call graph, and
  // no oscar64 temporaries are live across this block.
  // clang-format off
  __asm {
        lda #$00;
        sta _ps+PS_IDX;
        sta _ps+PS_CNT;
        sta _ps+PS_EDGES;
        lda #$0f;
        sta _ps+PS_ALL_OUT;

    L_pr_loop:
        lda _ps+PS_IN_LO;
        sta vec_proj_mul_a+0;
        lda _ps+PS_IN_HI;
        sta vec_proj_mul_a+1;
        ldy _ps+PS_IDX;
        lda (vec_proj_mul_a), y;
        sta vec_v+0;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+1;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+2;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+3;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+4;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+5;
        iny;
        sty _ps+PS_IDX;

        jsr vec_project_nocull;

        ldy _ps+PS_CNT;

        // px = (80 - (vec_sx >> 1)) >> 1 -> _poly_2d_lo/hi[0 + y]
        lda vec_sx+1;
        cmp #$80;
        ror;
        tax;
        lda vec_sx+0;
        ror;
        eor #$ff;
        sec;
        adc #$50;
        sta vec_proj_tmp1;
        txa;
        eor #$ff;
        adc #$00;
        cmp #$80;
        ror;
        sta _poly_2d_hi, y;
        tax;
        lda vec_proj_tmp1;
        ror;
        sta _poly_2d_lo, y;

        // X outcode -> vec_v+0 (0, 1, or 2); X is already 0 when in bounds.
        cpx #$00;
        bne L_pr_xout;
        cmp #$50;
        bcc L_pr_xok;
        bcs L_pr_xgt;
    L_pr_xout:
        txa;
        bmi L_pr_xlt;
    L_pr_xgt:
        ldx #$02;
        bne L_pr_xok;
    L_pr_xlt:
        ldx #$01;
    L_pr_xok:
        stx vec_v+0;

        // py = (28 - (vec_sy >> 1)) >> 1 -> _poly_2d_lo/hi[24 + y]
        lda vec_sy+1;
        cmp #$80;
        ror;
        tax;
        lda vec_sy+0;
        ror;
        eor #$ff;
        sec;
        adc #$1c;
        sta vec_proj_tmp1;
        txa;
        eor #$ff;
        adc #$00;
        cmp #$80;
        ror;
        sta _poly_2d_hi+24, y;
        tax;
        lda vec_proj_tmp1;
        ror;
        sta _poly_2d_lo+24, y;

        // Y outcode ORed with X outcode in vec_v+0 (4 or 8).
        cpx #$00;
        bne L_pr_yout;
        cmp #$1c;
        bcc L_pr_yok0;
        bcs L_pr_ygt;
    L_pr_yout:
        txa;
        bmi L_pr_ylt;
    L_pr_ygt:
        lda #$08;
        bne L_pr_yor;
    L_pr_ylt:
        lda #$04;
    L_pr_yor:
        ora vec_v+0;
        bne L_pr_yacc;          // always taken: bit 2 or 3 is set
    L_pr_yok0:
        lda vec_v+0;
    L_pr_yacc:
        tax;
        and _ps+PS_ALL_OUT;
        sta _ps+PS_ALL_OUT;
        txa;
        ora _ps+PS_EDGES;
        sta _ps+PS_EDGES;

        iny;
        sty _ps+PS_CNT;
        lda _ps+PS_IDX;
        cmp _ps+PS_END3;
        beq L_pr_done;
        jmp L_pr_loop;
    L_pr_done:
  }
  // clang-format on

  if (_ps.all_out) {
    return 0;
  }

  _ps.src_base = 0;
  if (_ps.edges) {
    _ps.edge_id = 0;
    do {
      if (_ps.edges & 1) {
        _poly_clip_2d_edge();
        if (_ps.cnt < 3) {
          return 0;
        }
        _ps.src_base ^= kPolyMax2dVertices;
      }
      ++_ps.edge_id;
      _ps.edges >>= 1;
    } while (_ps.edges);
  }

  // Pack surviving _ps.cnt vertices from striped buf[_ps.src_base..]
  // into vertices_2d as {int8_t x, int8_t y} pairs.
  // clang-format off
  __asm {
        lda vertices_2d+0;
        sta vec_proj_mul_a+0;
        lda vertices_2d+1;
        sta vec_proj_mul_a+1;
        ldx _ps+PS_SRC_BASE;
        lda _ps+PS_CNT;
        sta vec_proj_tmp1;
        ldy #$00;
    L_pk_loop:
        lda _poly_2d_lo, x;
        sta (vec_proj_mul_a), y;
        iny;
        lda _poly_2d_lo+24, x;
        sta (vec_proj_mul_a), y;
        iny;
        inx;
        dec vec_proj_tmp1;
        bne L_pk_loop;
  }
  // clang-format on

  return _ps.cnt;
}
#endif // __OSCAR64__
