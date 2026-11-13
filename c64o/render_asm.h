#ifndef RENDER_ASM_H
#define RENDER_ASM_H

#include <stdint.h>

#include "bool.h"
#include "color.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "chardefs.h"
#include "mem.h"
#include "render.h"
#include "roll.h"
#else
static const uint8_t kCharSolid11 = 0;
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
extern bool mem_using_alt_buffer;
extern int8_t render_cx_chars;
extern int8_t render_cy_chars;
extern bool render_alt_box;
extern int8_t render_alt_shift_x;
extern int8_t render_alt_shift_y;
extern uint8_t render_sky_rows;
extern int8_t roll_dx;
extern int8_t roll_dy;
extern uint8_t roll_period;
extern bool roll_x_is_major;
extern int8_t roll_ex_dy[8];
extern int8_t roll_ey_dx[8];
#endif

// The assembly half of render.cc: Pass 139's horizon lattice snap
// (_render_snap_center_asm, static inline under __OSCAR64__ for zero
// cross-TU overhead) and self-modifying 14-row sky/ground span sled
// (_render_flush_span, __noinline in render_asm.cc). render.cc holds the
// pure C equivalents (#ifndef __OSCAR64__) for host builds and tests.

// Sky rows filled contiguously from row 0 so far this frame.
extern uint8_t _render_sky_rows;

#ifdef __OSCAR64__
// Picks the nearest horizon lattice point after _pull_to_center(), updating
// render_alt_box, render_alt_shift_x/y, and render_cx/cy_chars. Inputs arrive
// in vec_proj_mul_a/b.
static inline void _render_snap_center_asm(void) {
  // Reuse ZP projection scratch across this snap step (nothing is live across
  // render_snap_center_chars) so oscar64 cannot fold or alias C locals.
  //   vec_proj_mul_a  = best_px      vec_proj_mul_b = best_py
  //   vec_v.x         = px           vec_v.y        = py
  //   vec_proj_tmp1   = min_dist     vec_proj_recip = loop counter / shift tmp
  //   vec_mul8_lo     = L_dist temp
  // clang-format off
  __asm {
        lda #0;
        sta render_alt_box;

        lda roll_period;
        cmp #1;
        bne L_multi;

        ldx #0;
        ldy #4;
        lda roll_x_is_major;
        bne L_alt_set;
        ldx #4;
        ldy #0;
    L_alt_set:
        stx render_alt_shift_x;
        sty render_alt_shift_y;
        lda vec_proj_mul_a + 0;
        ldy vec_proj_mul_b + 0;
        jsr L_dist;
        sta vec_proj_tmp1;

        lda vec_proj_mul_a + 0;
        sec;
        sbc render_alt_shift_x;
        tax;
        lda vec_proj_mul_b + 0;
        sec;
        sbc render_alt_shift_y;
        tay;
        txa;
        jsr L_dist;
        cmp vec_proj_tmp1;
        bcs L_snap;
        inc render_alt_box;
        sec;
        lda vec_proj_mul_a + 0;
        sbc render_alt_shift_x;
        sta vec_proj_mul_a + 0;
        bcs L_sub_y;
        dec vec_proj_mul_a + 1;
    L_sub_y:
        sec;
        lda vec_proj_mul_b + 0;
        sbc render_alt_shift_y;
        sta vec_proj_mul_b + 0;
        bcs L_snap;
        dec vec_proj_mul_b + 1;
        jmp L_snap;

    L_multi:
        // roll.cc guarantees roll_period is 1, 2, 4, or 8 (never 0).
        sta vec_proj_recip;
        lda vec_proj_mul_a + 0;
        sta vec_v + 0;
        lda vec_proj_mul_a + 1;
        sta vec_v + 1;
        lda vec_proj_mul_b + 0;
        sta vec_v + 2;
        lda vec_proj_mul_b + 1;
        sta vec_v + 3;
        lda #$ff;
        sta vec_proj_tmp1;
    L_loop:
        lda vec_v + 0;
        ldy vec_v + 2;
        jsr L_dist;
        cmp vec_proj_tmp1;
        bcs L_next;
        sta vec_proj_tmp1;
        lda vec_v + 0;
        sta vec_proj_mul_a + 0;
        lda vec_v + 1;
        sta vec_proj_mul_a + 1;
        lda vec_v + 2;
        sta vec_proj_mul_b + 0;
        lda vec_v + 3;
        sta vec_proj_mul_b + 1;
    L_next:
        lda roll_dx;
        bpl L_dx_pos;
        dec vec_v + 1;
    L_dx_pos:
        clc;
        adc vec_v + 0;
        sta vec_v + 0;
        bcc L_dy_step;
        inc vec_v + 1;
    L_dy_step:
        lda roll_dy;
        bpl L_dy_pos;
        dec vec_v + 3;
    L_dy_pos:
        clc;
        adc vec_v + 2;
        sta vec_v + 2;
        bcc L_iter_done;
        inc vec_v + 3;
    L_iter_done:
        dec vec_proj_recip;
        bne L_loop;

    L_snap:
        lda vec_proj_mul_a + 0;
        ldy vec_proj_mul_a + 1;
        jsr L_div8;
        sta render_cx_chars;
        lda vec_proj_mul_b + 0;
        ldy vec_proj_mul_b + 1;
        jsr L_div8;
        sta render_cy_chars;
        jmp L_snap_done;

    L_div8:
        clc;
        adc #4;
        bcc L_div8_nc;
        iny;
    L_div8_nc:
        sty vec_proj_recip;
        lsr vec_proj_recip;
        ror;
        lsr vec_proj_recip;
        ror;
        lsr vec_proj_recip;
        ror;
        rts;

    L_dist:
        clc;
        adc #4;
        and #7;
        tax;
        lda roll_ex_dy, x;
        sta vec_mul8_lo;
        tya;
        clc;
        adc #4;
        and #7;
        tax;
        lda vec_mul8_lo;
        sec;
        sbc roll_ey_dx, x;
        bpl L_d_pos;
        eor #$ff;
        clc;
        adc #1;
    L_d_pos:
        rts;

    L_snap_done:
  }
  // clang-format on
}
#else
void _render_snap_center_asm(void);
#endif // __OSCAR64__

// Fills whole viewport rows [start_row, end_row) with `ch` (`ch & 1` is a
// no-op), updating _render_sky_rows and colour RAM when `ch == kCharSolid11`.
void _render_flush_span(uint8_t ch, uint8_t start_row, uint8_t end_row);

#ifdef __TARGET_TEST__
#pragma compile("render_asm.cc")
#endif

#endif
