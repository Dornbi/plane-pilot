#ifndef POLY_ASM_H
#define POLY_ASM_H

#include <stdint.h>

#include "bool.h"
#include "fmath.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "chardefs.h"
#include "mem.h"
#else
static const uint8_t kScreenWidth = 40;
static const uint8_t kViewportWidth = 40;
static const uint8_t kCharSolidGround = 128;
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
#endif

#ifndef POLY_H
#ifndef _ASM_POLY_MAX_VERTS_DEF
#define _ASM_POLY_MAX_VERTS_DEF
static const uint8_t kPolyMaxVertices = 6;
#endif
static const uint8_t kPolyMax2dVertices =
    kPolyMaxVertices + 6; // +5 derived, +6 for safety
#endif

// The assembly half of poly.cc:
//   - _poly_frustum_all_out: 3D frustum early rejection (poly_asm.cc)
//   - _poly_project_vertices: near clip, frustum reject, projection, 2D clip,
//     and packed vertex output (poly_asm.cc)
//   - _poly_bounds_and_clear: vertical sub-row extent + buffer clear (inline)
//   - _poly_trace_edges: polygon edge walk + Bresenham span trace (inline)
//   - _poly_scan_lines2: full quad-cell scanline rasteriser (inline)
// poly.cc holds the pure C equivalents (#ifndef __OSCAR64__) for the host and
// host tests.

// Working buffers defined in poly_asm.cc (under __OSCAR64__, in scratch/bss2)
// or poly.cc (on the host).
extern vec3_t _poly_clip3_buf[];
extern uint8_t _poly_min_x[2 * kViewportHeight];
extern uint8_t _poly_max_x[2 * kViewportHeight];

// Fast core fill helpers shared by _poly_scan_lines2() (6502 and host).
static inline void _poly_set_line(uint8_t *dst, uint8_t val, int8_t cnt) {
  for (int8_t i = cnt - 1; i >= 0; --i) {
    if (dst[i] < kCharSolidGround) {
      continue;
    }
    dst[i] = val;
  }
}

static inline void _poly_set_color_line(uint8_t *dst, uint8_t val,
                                        uint8_t *color_dst, uint8_t color,
                                        int8_t cnt) {
  for (int8_t i = cnt - 1; i >= 0; --i) {
    if (dst[i] < kCharSolidGround) {
      continue;
    }
    dst[i] = val;
    color_dst[i] = color;
  }
}

#ifdef __OSCAR64__
#ifndef __TARGET_TEST__
static_assert(kScreenWidth == 40 && kScreenHeight == 25 &&
                  kViewportWidth == 40 && kViewportHeight == 14 &&
                  kCharSolidGround == 128,
              "poly_asm constants mismatch");
#endif

// Implemented in poly_asm.cc.
bool _poly_frustum_all_out(uint8_t end3);
uint8_t _poly_project_vertices(const vec3_t *vertices_3d, uint8_t num_vertices,
                               int8_t *vertices_2d);
#pragma compile("poly_asm.cc")

// Scans vertices[0 .. num_vertices - 1] (num_vertices >= 3, packed {x, y}
// byte pairs) for [min_y, max_y] and clears _poly_min_x/_poly_max_x across the
// spanned whole character rows [*py_start, *py_end].
static inline void _poly_bounds_and_clear(const int8_t *verts,
                                          uint8_t num_vertices,
                                          uint8_t *py_start, uint8_t *py_end) {
  // clang-format off
  __asm {
        lda verts;
        sta vec_proj_mul_a+0;
        lda verts+1;
        sta vec_proj_mul_a+1;
        lda num_vertices;
        asl;
        sta vec_proj_tmp1;

        ldy #$01;
        lda (vec_proj_mul_a), y;
        sta vec_v+0;
        sta vec_v+2;
        ldy #$03;
    L_pb_loop:
        lda (vec_proj_mul_a), y;
        cmp vec_v+0;
        bcs L_pb_chk_max;
        sta vec_v+0;
        bcc L_pb_next;
    L_pb_chk_max:
        cmp vec_v+2;
        bcc L_pb_next;
        sta vec_v+2;
    L_pb_next:
        iny;
        iny;
        cpy vec_proj_tmp1;
        bcc L_pb_loop;

        lda vec_v+0;
        and #$fe;
        tay;
        lsr;
        sta vec_v+0;
        lda vec_v+2;
        ora #$01;
        tax;
        lsr;
        sta vec_v+2;
        inx;
        stx vec_proj_tmp1;
        ldx #$00;
    L_pb_clr:
        lda #$50;
        sta _poly_min_x, y;
        txa;
        sta _poly_max_x, y;
        iny;
        cpy vec_proj_tmp1;
        bcc L_pb_clr;
  }
  // clang-format on
  *py_start = (uint8_t)vec_v.x;
  *py_end = (uint8_t)vec_v.y;
}

// Traces all edges of vertices[0 .. num_vertices - 1] (packed {x, y} byte
// pairs with x in [0, 79] and y in [0, 27]) into _poly_min_x/_poly_max_x.
static inline void _poly_trace_edges(const int8_t *verts,
                                     uint8_t num_vertices) {
  // clang-format off
  __asm {
        lda verts;
        sta vec_proj_mul_a+0;
        lda verts+1;
        sta vec_proj_mul_a+1;
        lda num_vertices;
        asl;
        sta vec_mul8_b;

        ldy #$00;
        sty vec_mul8_lo;
    L_te_edge:
        lda (vec_proj_mul_a), y;
        sta vec_v+0;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_v+1;
        iny;
        cpy vec_mul8_b;
        bcc L_te_load_b;
        ldy #$00;
    L_te_load_b:
        lda (vec_proj_mul_a), y;
        sta vec_v+2;
        iny;
        lda (vec_proj_mul_a), y;
        cmp vec_v+1;
        beq L_te_skip;
        bcc L_te_swap;
        sta vec_v+3;
        bcs L_te_ordered;
    L_te_skip:
        jmp L_te_next;
    L_te_swap:
        ldx vec_v+1;
        stx vec_v+3;
        sta vec_v+1;
        ldx vec_v+0;
        lda vec_v+2;
        sta vec_v+0;
        stx vec_v+2;
    L_te_ordered:
        lda vec_v+3;
        sec;
        sbc vec_v+1;
        sta vec_v+4;
        lsr;
        sta vec_v+5;

        ldx vec_v+0;
        lda vec_v+2;
        sec;
        sbc vec_v+0;
        bcs L_te_rise_dx;
        txa;
        sec;
        sbc vec_v+2;
        ldy #$80;
        bmi L_te_chk_maj;
    L_te_rise_dx:
        ldy #$00;
    L_te_chk_maj:
        sta vec_sx+0;
        sty vec_sx+1;
        lda vec_v+4;
        cmp vec_sx+0;
        bcc L_xm_start;

        // Y-major (dx <= dy): step y in [y1, y2).
        ldy vec_v+1;
    L_ym_loop:
        txa;
        cmp _poly_min_x, y;
        bcs L_ym_nomin;
        sta _poly_min_x, y;
    L_ym_nomin:
        cmp _poly_max_x, y;
        bcc L_ym_nomax;
        sta _poly_max_x, y;
    L_ym_nomax:
        lda vec_v+5;
        clc;
        adc vec_sx+0;
        cmp vec_v+4;
        bcc L_ym_nostep;
        sbc vec_v+4;
        bit vec_sx+1;
        bmi L_ym_dec;
        inx;
        bpl L_ym_nostep;
    L_ym_dec:
        dex;
    L_ym_nostep:
        sta vec_v+5;
        iny;
        cpy vec_v+3;
        bcc L_ym_loop;
        jmp L_te_endpt;

    L_xm_start:
        // X-major (dx > dy): dx = quot * dy + rem, then carry-folded row step.
        lda vec_sx+0;
        ldy #$00;
        sec;
    L_xm_div:
        sbc vec_v+4;
        iny;
        cmp vec_v+4;
        bcs L_xm_div;
        sta vec_sx+0;
        lda vec_sx+1;
        sty vec_sx+1;
        ldy vec_v+1;
        asl;
        bcs L_xm_fall;

    L_xm_rise:
        txa;
        cmp _poly_min_x, y;
        bcs L_xmr_nomin;
        sta _poly_min_x, y;
    L_xmr_nomin:
        lda vec_v+5;
        clc;
        adc vec_sx+0;
        cmp vec_v+4;
        bcc L_xmr_nostep;
        sbc vec_v+4;
    L_xmr_nostep:
        sta vec_v+5;
        txa;
        adc vec_sx+1;
        tax;
        cmp _poly_max_x, y;
        bcc L_xmr_nomax;
        sta _poly_max_x, y;
    L_xmr_nomax:
        iny;
        cpy vec_v+3;
        bcc L_xm_rise;
        bcs L_te_endpt;

    L_xm_fall:
        txa;
        cmp _poly_max_x, y;
        bcc L_xmf_nomax;
        sta _poly_max_x, y;
    L_xmf_nomax:
        lda vec_v+5;
        clc;
        adc vec_sx+0;
        cmp vec_v+4;
        bcc L_xmf_nostep;
        sbc vec_v+4;
        clc;
        bcc L_xmf_sub;
    L_xmf_nostep:
        sec;
    L_xmf_sub:
        sta vec_v+5;
        txa;
        sbc vec_sx+1;
        tax;
        cmp _poly_min_x, y;
        bcs L_xmf_nomin;
        sta _poly_min_x, y;
    L_xmf_nomin:
        iny;
        cpy vec_v+3;
        bcc L_xm_fall;

    L_te_endpt:
        txa;
        cmp _poly_min_x, y;
        bcs L_te_end_nomin;
        sta _poly_min_x, y;
    L_te_end_nomin:
        cmp _poly_max_x, y;
        bcc L_te_next;
        sta _poly_max_x, y;
    L_te_next:
        lda vec_mul8_lo;
        clc;
        adc #$02;
        sta vec_mul8_lo;
        tay;
        cmp vec_mul8_b;
        beq L_te_done;
        jmp L_te_edge;
    L_te_done:
  }
  // clang-format on
}

// Rasterises polygon scanlines in character rows [py_start, py_end] (inclusive)
// from _poly_min_x/_poly_max_x.
static inline void _poly_scan_lines2(uint8_t fill_char_start_idx,
                                     uint8_t color, uint8_t py_start,
                                     uint8_t py_end) {
  if (py_start > py_end) {
    return;
  }
  // clang-format off
  __asm {
        lda fill_char_start_idx;
        sta vec_mul8_b;
        lda color;
        sta vec_proj_recip;
        lda py_start;
        sta vec_sx+0;
        ldx py_end;
        inx;
        stx vec_sx+1;
    L_sl_row:
        lda vec_sx+0;
        asl;
        tax;
        lda _poly_min_x, x;
        sta vec_v+0;
        ldy _poly_min_x+1, x;
        sty vec_v+1;
        cpy vec_v+0;
        bcs L_sl_min_ok;
        ldy vec_v+0;
        lda vec_v+1;
    L_sl_min_ok:
        cmp #$50;
        bne L_sl_active;
        jmp L_sl_next_row;
    L_sl_active:
        lsr;
        sta vec_mul8_lo;
        iny;
        tya;
        lsr;
        sta vec_v+4;

        lda _poly_max_x, x;
        sta vec_v+2;
        ldy _poly_max_x+1, x;
        sty vec_v+3;
        cpy vec_v+2;
        bcs L_sl_max_ok;
        ldy vec_v+2;
        lda vec_v+3;
    L_sl_max_ok:
        clc;
        adc #$01;
        lsr;
        sta vec_v+5;
        tya;
        lsr;
        tay;
        iny;
        sty vec_proj_tmp1;

        ldx vec_sx+0;
        lda mem_screen_row_ptrs, x;
        sta vec_proj_mul_a+0;
        lda mem_screen_row_ptrs+25, x;
        sta vec_proj_mul_a+1;
        lda mem_color_row_ptrs, x;
        sta vec_proj_mul_b+0;
        lda mem_color_row_ptrs+14, x;
        sta vec_proj_mul_b+1;

        // Solid core [overlap_start, overlap_stop).
        ldy vec_v+4;
        cpy vec_v+5;
        bcc L_sl_has_core;
        lda #$28;
        sta vec_v+4;
        bne L_fr_start;
    L_sl_has_core:
        lda vec_mul8_b;
        ora #$0f;
        tax;
        lda vec_proj_recip;
        beq L_core_nocolor;
    L_core_color:
        lda (vec_proj_mul_a), y;
        bpl L_cc_skip;
        txa;
        sta (vec_proj_mul_a), y;
        lda vec_proj_recip;
        sta (vec_proj_mul_b), y;
    L_cc_skip:
        iny;
        cpy vec_v+5;
        bcc L_core_color;
        bcs L_fr_start;
    L_core_nocolor:
        lda (vec_proj_mul_a), y;
        bpl L_cn_skip;
        txa;
        sta (vec_proj_mul_a), y;
    L_cn_skip:
        iny;
        cpy vec_v+5;
        bcc L_core_nocolor;

        // Fringe pixels outside [overlap_start, overlap_stop).
    L_fr_start:
        ldy vec_mul8_lo;
    L_fr_loop:
        cpy vec_v+4;
        bne L_fr_chk;
        ldy vec_v+5;
        cpy vec_proj_tmp1;
        bcs L_sl_next_row;
    L_fr_chk:
        lda (vec_proj_mul_a), y;
        bpl L_fr_next;
        tya;
        asl;
        ldx #$00;

        // Top sub-row [t_min, t_max] -> bits 0 (sx_left) and 1 (sx_left+1).
        cmp vec_v+2;
        beq L_t_eq;
        bcs L_t_done;
        cmp vec_v+0;
        bcc L_t_chk_r;
        ldx #$03;
        bne L_t_done;
    L_t_eq:
        cmp vec_v+0;
        bcc L_t_done;
        ldx #$01;
        bcs L_t_done;
    L_t_chk_r:
        ora #$01;
        cmp vec_v+0;
        bcc L_t_restore;
        ldx #$02;
    L_t_restore:
        and #$fe;
    L_t_done:

        // Bottom sub-row [b_min, b_max] -> bits 2 and 3.
        cmp vec_v+3;
        beq L_b_eq;
        bcs L_b_chk_zero;
        cmp vec_v+1;
        bcc L_b_chk_r;
        txa;
        ora #$0c;
        bne L_draw;
    L_b_eq:
        cmp vec_v+1;
        bcc L_b_chk_zero;
        txa;
        ora #$04;
        bne L_draw;
    L_b_chk_r:
        ora #$01;
        cmp vec_v+1;
        bcc L_b_chk_zero;
        txa;
        ora #$08;
        bne L_draw;
    L_b_chk_zero:
        txa;
        beq L_fr_next;
    L_draw:
        clc;
        adc vec_mul8_b;
        sta (vec_proj_mul_a), y;
        lda vec_proj_recip;
        beq L_fr_next;
        sta (vec_proj_mul_b), y;
    L_fr_next:
        iny;
        cpy vec_proj_tmp1;
        bcc L_fr_loop;
    L_sl_next_row:
        inc vec_sx+0;
        lda vec_sx+0;
        cmp vec_sx+1;
        beq L_sl_done;
        jmp L_sl_row;
    L_sl_done:
  }
  // clang-format on
}
#else
bool _poly_frustum_all_out(uint8_t end3);
uint8_t _poly_project_vertices(const vec3_t *vertices_3d, uint8_t num_vertices,
                               int8_t *vertices_2d);
void _poly_bounds_and_clear(const int8_t *verts, uint8_t num_vertices,
                            uint8_t *py_start, uint8_t *py_end);
void _poly_trace_edges(const int8_t *verts, uint8_t num_vertices);
void _poly_scan_lines2(uint8_t fill_char_start_idx, uint8_t color,
                       uint8_t py_start, uint8_t py_end);
#endif // __OSCAR64__

#endif
