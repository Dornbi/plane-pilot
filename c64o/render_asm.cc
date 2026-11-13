#include "render_asm.h"

#include <stdint.h>

// The assembly half of render.cc (render_asm.h): Pass 139's horizon snap loop
// and self-modifying screen-RAM span sled.

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

#if defined(__OSCAR64__) && !defined(__TARGET_TEST__)
static_assert(kViewportHeight == 14 && kViewportStartX == 0 &&
                  kViewportStartY == 0 && kCharSolid11 == 0,
              "render_asm constants mismatch");
#endif

#ifdef __OSCAR64__
void _render_snap_center_asm(void) {
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

// 46-byte self-modifying STA abs,x screen-RAM sled ($E800 main, toggled to $EC00
// alt in place via EOR #$04 on each high byte). Patching `dex; bpl rel; rts` at
// end_row runs X = 39..0 once across [start_row, end_row). ch == 1 is a no-op.
__noinline void _render_flush_span(uint8_t ch, uint8_t start_row,
                                   uint8_t end_row) {
  if (end_row > kViewportHeight) {
    end_row = kViewportHeight;
  }
  if ((ch & 1) || start_row >= end_row) {
    return;
  }
  uint8_t sky_col = COLOR_SKY8;
  // clang-format off
  __asm {
        lda ch;
        bne L_scr;
        lda start_row;
        cmp _render_sky_rows;
        bne L_srows_done;
        ldx end_row;
        stx _render_sky_rows;
    L_srows_done:
        cmp render_sky_rows;
        bcs L_clr_chk;
        lda render_sky_rows;
    L_clr_chk:
        cmp end_row;
        bcs L_scr;
        tax;
    L_clr_row:
        lda mem_color_row_ptrs, x;
        sta L_cs0 + 1;
        clc;
        adc #20;
        sta L_cs1 + 1;
        lda mem_color_row_ptrs + 14, x;
        sta L_cs0 + 2;
        adc #0;
        sta L_cs1 + 2;
        lda sky_col;
        ldy #19;
    L_cs0:
        sta $d800, y;
    L_cs1:
        sta $d814, y;
        dey;
        bpl L_cs0;
        inx;
        cpx end_row;
        bcc L_clr_row;

    L_scr:
        lda mem_using_alt_buffer;
        asl;
        asl;
        eor L_sled + 2;
        and #4;
        beq L_tog_done;
        ldx #39;
    L_tog:
        lda L_sled + 2, x;
        eor #4;
        sta L_sled + 2, x;
        dex;
        dex;
        dex;
        bpl L_tog;
    L_tog_done:

        lda end_row;
        asl;
        adc end_row;
        tay;
        sty end_row;
        lda start_row;
        asl;
        adc start_row;
        tax;
        sec;
        sbc end_row;
        sbc #2;
        sta end_row;
        txa;
        clc;
        adc #<L_sled;
        sta L_call + 1;
        lda #>L_sled;
        adc #0;
        sta L_call + 2;

        ldx #4;
    L_sv:
        lda L_sled, y;
        pha;
        iny;
        dex;
        bne L_sv;

        lda #$ca;
        sta L_sled - 4, y;
        lda #$10;
        sta L_sled - 3, y;
        lda end_row;
        sta L_sled - 2, y;
        lda #$60;
        sta L_sled - 1, y;

        lda ch;
        ldx #39;
    L_call:
        jsr L_sled;

        ldx #4;
    L_rs:
        dey;
        pla;
        sta L_sled, y;
        dex;
        bne L_rs;
        rts;

    L_sled:
        sta $e800, x;
        sta $e828, x;
        sta $e850, x;
        sta $e878, x;
        sta $e8a0, x;
        sta $e8c8, x;
        sta $e8f0, x;
        sta $e918, x;
        sta $e940, x;
        sta $e968, x;
        sta $e990, x;
        sta $e9b8, x;
        sta $e9e0, x;
        sta $ea08, x;
        dex;
        bpl L_sled;
        rts;
  }
  // clang-format on
}
#endif // __OSCAR64__

#pragma optimize(pop)
