#include "render_asm.h"

#include <stdint.h>

// The out-of-line assembly half of render.cc (render_asm.h): Pass 139's
// self-modifying 14-row screen-RAM span sled.

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

#if defined(__OSCAR64__) && !defined(__TARGET_TEST__)
static_assert(kViewportHeight == 14 && kViewportStartX == 0 &&
                  kViewportStartY == 0 && kCharSolid11 == 0,
              "render_asm constants mismatch");
#endif

#ifdef __OSCAR64__
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
