#include "render_asm.h"

#include <stddef.h>
#include <stdint.h>

// The out-of-line 6502 assembly half of render.cc (render_asm.h):
//   - _render_pull_and_snap_asm / _render_snap_center_asm: pulls the horizon
//     point near the viewport center and snaps to the nearest main/alt lattice.
//   - _render_fill_sky_ground_asm / _render_flush_span: level and tilted
//     sky/ground fill with a 14-row screen-RAM sled (unrolled high-byte sync,
//     no-restore fast path for end_row == 14) and a 2x-unrolled partial-row
//     span helper.

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

#ifdef __OSCAR64__

#ifndef __TARGET_TEST__
static_assert(kViewportHeight == 14 && kViewportWidth == 40 &&
                  kViewportStartX == 0 && kViewportStartY == 0 &&
                  kScreenHeight == 25 && kCharSolid11 == 0 &&
                  kCharSolidGround == 0x80,
              "render_asm constants mismatch");
#endif
#ifdef __ENABLE_COLOR_SCHEMES__
static_assert(offsetof(ColorScheme, sky8) == 0,
              "color_scheme.sky8 offset changed");
#else
static_assert((kColorSky | 0x08) == 14, "COLOR_SKY8 constant changed");
#endif

// High-nibble lookup for (v >> 4) when v in [0x0010, 0x027F].
static const uint8_t kRenderHiNibble[3] = {0, 16, 32};

// Snaps (px, py) already loaded in vec_proj_mul_a/b to the nearest main/alt
// lattice point, updating render_alt_box, render_alt_shift_x/y, and
// render_cx/cy_chars.
__noinline void _render_snap_center_asm(void) {
  // Reuse ZP projection scratch across this snap step (nothing is live across
  // render_snap_center_chars):
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
        rts;

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
  }
  // clang-format on
}

// Pulls (render_cx_pixels, render_cy_pixels) along the horizon line near the
// viewport center into vec_proj_mul_a/b and tail-calls _render_snap_center_asm.
__noinline void _render_pull_and_snap_asm(void) {
  // ZP layout (vec_v and vec_sx are preserved across vec_mul_ab):
  //   vec_v+0..1  = c_maj
  //   vec_v+2..3  = c_min
  //   vec_v+4     = d_maj
  //   vec_v+5     = d_min
  //   vec_sx+0..1 = p_maj
  //   vec_sy+0..1 = t
  // clang-format off
  __asm {
        lda render_cx_pixels + 0;
        sta vec_v + 0;
        lda render_cx_pixels + 1;
        sta vec_v + 1;
        lda render_cy_pixels + 0;
        sta vec_v + 2;
        lda render_cy_pixels + 1;
        sta vec_v + 3;
        lda roll_dx;
        sta vec_v + 4;
        lda roll_dy;
        sta vec_v + 5;
        lda #160;
        ldx roll_x_is_major;
        bne L_maj_done;
        // Swap major/minor when y is major (ZP direct: smaller & faster).
        lda vec_v + 0;
        ldy vec_v + 2;
        sta vec_v + 2;
        sty vec_v + 0;
        lda vec_v + 1;
        ldy vec_v + 3;
        sta vec_v + 3;
        sty vec_v + 1;
        lda vec_v + 4;
        ldy vec_v + 5;
        sta vec_v + 5;
        sty vec_v + 4;
        lda #64;
    L_maj_done:
        clc;
        ldx roll_shift_2chars;
        bne L_s2_off;
        adc #32;
        ldy #$c0;
        bne L_sub_cmaj;
    L_s2_off:
        adc #64;
        ldy #$80;
    L_sub_cmaj:
        // A is 96, 128, 192, or 224 (high byte always 0).
        sec;
        sbc vec_v + 0;
        sta vec_sy + 0;
        lda #0;
        sbc vec_v + 1;
        sta vec_sy + 1;

        // p_maj = c_maj + (t & mask), with mask_lo in Y and mask_hi = $ff.
        tya;
        and vec_sy + 0;
        clc;
        adc vec_v + 0;
        sta vec_sx + 0;
        lda vec_sy + 1;
        adc vec_v + 1;
        sta vec_sx + 1;

        // Level / axis-aligned fast path: d_min == 0 => p_min = c_min.
        lda vec_v + 5;
        bne L_need_pmin;
        lda vec_v + 2;
        sta vec_proj_mul_b + 0;
        lda vec_v + 3;
        sta vec_proj_mul_b + 1;
        jmp L_store_maj;

    L_need_pmin:
        // d = t >> (roll_shift_2chars ? 7 : 6), arithmetic signed shift via
        // 1 or 2 left rotates of (sign : t_hi : t_lo).
        ldx #0;
        lda vec_sy + 1;
        bpl L_t_pos;
        dex;
    L_t_pos:
        stx vec_proj_mul_a + 1;
        asl vec_sy + 0;
        rol vec_sy + 1;
        lda roll_shift_2chars;
        bne L_d_shifted;
        asl vec_sy + 0;
        rol vec_sy + 1;
        rol vec_proj_mul_a + 1;
    L_d_shifted:
        lda vec_sy + 1;
        sta vec_proj_mul_a + 0;

        // If d_maj < 0, d = -d.
        bit vec_v + 4;
        bpl L_d_pos;
        sec;
        lda #0;
        sbc vec_proj_mul_a + 0;
        sta vec_proj_mul_a + 0;
        lda #0;
        sbc vec_proj_mul_a + 1;
        sta vec_proj_mul_a + 1;
    L_d_pos:
        // vec_mul_res = _render_mul(d, d_min) = vec_mul_ab(d, d_min << 8).
        lda #0;
        sta vec_proj_mul_b + 0;
        lda vec_v + 5;
        sta vec_proj_mul_b + 1;
        jsr vec_mul_ab;

        // p_min = c_min + (vec_mul_res << 3) directly into vec_proj_mul_b.
        asl vec_mul_res + 0;
        rol vec_mul_res + 1;
        asl vec_mul_res + 0;
        rol vec_mul_res + 1;
        asl vec_mul_res + 0;
        rol vec_mul_res + 1;
        clc;
        lda vec_mul_res + 0;
        adc vec_v + 2;
        sta vec_proj_mul_b + 0;
        lda vec_mul_res + 1;
        adc vec_v + 3;
        sta vec_proj_mul_b + 1;

    L_store_maj:
        lda vec_sx + 0;
        sta vec_proj_mul_a + 0;
        lda vec_sx + 1;
        sta vec_proj_mul_a + 1;
        lda roll_x_is_major;
        bne L_go_snap;
        lda vec_proj_mul_a + 0;
        ldy vec_proj_mul_b + 0;
        sta vec_proj_mul_b + 0;
        sty vec_proj_mul_a + 0;
        lda vec_proj_mul_a + 1;
        ldy vec_proj_mul_b + 1;
        sta vec_proj_mul_b + 1;
        sty vec_proj_mul_a + 1;
    L_go_snap:
        jmp _render_snap_center_asm;
  }
  // clang-format on
}

// Shared internal whole-row span flusher (pure asm, zero C locals/prologue):
//   Inputs:  A = ch (even; odd ch is a no-op), X = start_row, Y = end_row.
//   Clobbers: A, X, Y, vec_proj_tmp1, vec_proj_recip, vec_mul8_b, vec_mul8_lo,
//             vec_proj_mul_a, vec_proj_mul_b.
//   Preserves: vec_v, vec_sx, vec_sy, vec_mul_res.
//
// 46-byte self-modifying 14-row STA abs,x screen-RAM sled ($E800 main / $EC00
// alt) with unrolled 14-store high-byte sync (64 cycles/frame vs 280 for the
// old EOR loop) and a zero-save/restore tail-jump fast path when end_row == 14.
static __noinline void _render_flush_span_core(void) {
  // ZP temporaries (all dead across flush calls):
  //   vec_proj_mul_a+0 = ch
  //   vec_proj_mul_a+1 = start_row
  //   vec_proj_mul_b+0 = end_row (clamped to 14)
  //   vec_proj_mul_b+1 = bpl rel offset
  // clang-format off
  __asm {
        lsr;
        bcs L_fl_ret;
        asl;
        sta vec_proj_mul_a + 0; // ch
        cpy #15;
        bcc L_end_ok;
        ldy #14;
    L_end_ok:
        stx vec_proj_mul_a + 1; // start_row
        sty vec_proj_mul_b + 0; // end_row
        cpx vec_proj_mul_b + 0;
        bcc L_nonempty;
    L_fl_ret:
        rts;

    L_nonempty:
        // Sky rows update _render_sky_rows and colour RAM for rows >=
        // render_sky_rows.
        cmp #0;
        bne L_scr;
        txa;
        cmp _render_sky_rows;
        bne L_srows_done;
        tya;
        sta _render_sky_rows;
        txa;
    L_srows_done:
        cmp render_sky_rows;
        bcs L_clr_chk;
        lda render_sky_rows;
    L_clr_chk:
        cmp vec_proj_mul_b + 0;
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
#ifdef __ENABLE_COLOR_SCHEMES__
        lda color_scheme;
#else
        lda #14;
#endif
        ldy #19;
    L_cs0:
        sta $d800, y;
    L_cs1:
        sta $d814, y;
        dey;
        bpl L_cs0;
        inx;
        cpx vec_proj_mul_b + 0;
        bcc L_clr_row;

    L_scr:
        // Sync sled high bytes ($E8/$E9/$EA vs $EC/$ED/$EE) with a 6-step
        // pair loop (+2..+17 and +23..+38) plus the 2 trailing rows (+20, +41).
        lda #$e8;
        ldx mem_using_alt_buffer;
        beq L_chk_hi;
        lda #$ec;
    L_chk_hi:
        cmp L_sled + 2;
        beq L_tog_done;
        ldx #15;
    L_tog:
        sta L_sled + 2, x;
        eor #1;
        sta L_sled + 23, x;
        eor #1;
        dex;
        dex;
        dex;
        bpl L_tog;
        sta L_sled + 20;
        clc;
        adc #2;
        sta L_sled + 41;
    L_tog_done:

        // start_off = start_row * 3. Patch shared entry L_call_sub+1/+2 once.
        lda vec_proj_mul_a + 1;
        asl;
        adc vec_proj_mul_a + 1;
        tax;                    // X = start_off (0..39)
        clc;
        adc #<L_sled;
        sta L_call_sub + 1;
        lda #>L_sled;
        adc #0;
        sta L_call_sub + 2;

        lda vec_proj_mul_b + 0;
        cmp #14;
        bne L_patch_mid;

        // Fast path: end_row == 14 (end_off == 42). Tail is already
        // `dex; bpl rel; rts` — patch only L_sled + 44 and tail-jump!
        txa;
        sec;
        sbc #42;
        sbc #2;                 // C=0 (start_off < 42) => start_off - 45
        sta L_sled + 44;
        lda vec_proj_mul_a + 0;
        ldx #39;
    L_call_sub:
        jmp L_sled;

    L_patch_mid:
        asl;
        adc vec_proj_mul_b + 0;
        tay;                    // Y = end_off (3..39)
        sty vec_proj_mul_b + 1;
        txa;
        sec;
        sbc vec_proj_mul_b + 1;
        sbc #2;                 // C=0 => start_off - end_off - 3
        sta vec_proj_mul_b + 1;

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
        lda vec_proj_mul_b + 1;
        sta L_sled - 2, y;
        lda #$60;
        sta L_sled - 1, y;

        lda vec_proj_mul_a + 0;
        ldx #39;
        jsr L_call_sub;

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

__noinline void _render_flush_span(uint8_t ch, uint8_t start_row,
                                   uint8_t end_row) {
  // clang-format off
  __asm {
        lda ch;
        ldx start_row;
        ldy end_row;
        jmp _render_flush_span_core;
  }
  // clang-format on
}

// Full level + tilted sky/ground viewport fill in 6502 __asm.
// Reuses vec_v, vec_sx, vec_sy, vec_proj_mul_a/b, vec_mul_res (all dead across
// render_fill_sky_ground).
__noinline void _render_fill_sky_ground_asm(void) {
  // ZP layout in tilted loop (all preserved across _render_flush_span_core):
  //   vec_v+0..1    = lo (12.4 signed)
  //   vec_v+2..3    = w  (12.4 positive = |roll_dx_div_dy| * 4)
  //   vec_v+4..5    = hi (lo + w)
  //   vec_sx+0      = run_ch (1 = none, 0 = sky, $80 = ground)
  //   vec_sx+1      = run_start
  //   vec_sy+0      = y (0..13)
  //   vec_sy+1      = scratch (next ch / span start col r)
  //   vec_mul_res+0 = partial span last index X (0..38)
  //   vec_proj_tmp1 = partial span fill byte
  // clang-format off
  __asm {
        lda #0;
        sta _render_sky_rows;

        lda roll_dy;
        beq L_level;
        jmp L_tilted;

        // ---- Level horizon (roll_dy == 0) ----
    L_level:
        lda render_cy_chars;
        bpl L_lvl_top_ok;
        lda #0;
    L_lvl_top_ok:
        sta vec_sx + 0;         // top = _clamp_pos(e)
        lda roll_dx;
        bmi L_lvl_gnd_top;
        beq L_lvl_gnd_top;

        // sky_left (roll_dx > 0): ch0 = $00, bot = top, top = clamp(top - 4).
        lda vec_sx + 0;
        sta vec_sx + 1;         // bot
        sec;
        sbc #4;
        bpl L_lvl_sky_ok;
        lda #0;
    L_lvl_sky_ok:
        tay;                    // Y = top
        lda #0;                 // ch0 = kCharSolid11
        beq L_lvl_flush;

    L_lvl_gnd_top:
        // !sky_left (roll_dx <= 0): ch0 = $80, bot = max(top, clamp(e + 4)).
        lda render_cy_chars;
        clc;
        adc #4;
        bpl L_lvl_bot_pos;
        lda #0;
    L_lvl_bot_pos:
        cmp vec_sx + 0;
        bcs L_lvl_bot_ok;
        lda vec_sx + 0;
    L_lvl_bot_ok:
        sta vec_sx + 1;         // bot
        ldy vec_sx + 0;         // Y = top
        lda #$80;               // ch0 = kCharSolidGround

    L_lvl_flush:
        sta vec_sy + 0;         // save ch0
        ldx #0;
        jsr _render_flush_span_core;
        lda vec_sy + 0;
        eor #$80;
        ldx vec_sx + 1;         // bot
        ldy #14;
        jmp L_flush_and_done;

        // ---- Tilted horizon (roll_dy != 0) ----
    L_tilted:
        // prod = _render_mul(roll_dx_div_dy, render_cy_chars)
        lda roll_dx_div_dy + 0;
        sta vec_proj_mul_a + 0;
        lda roll_dx_div_dy + 1;
        sta vec_proj_mul_a + 1;
        lda #0;
        sta vec_proj_mul_b + 0;
        lda render_cy_chars;
        sta vec_proj_mul_b + 1;
        jsr vec_mul_ab;

        // cx16 = (int16_t)render_cx_chars << 4; lo = cx16 - vec_mul_res.
        lda render_cx_chars;
        tax;
        lsr;
        lsr;
        lsr;
        lsr;
        cpx #$80;
        bcc L_cx_pos;
        ora #$f0;
    L_cx_pos:
        tay;                    // Y = cx16_hi
        txa;
        asl;
        asl;
        asl;
        asl;                    // A = cx16_lo
        sec;
        sbc vec_mul_res + 0;
        sta vec_v + 0;          // lo_lo
        tya;
        sbc vec_mul_res + 1;
        sta vec_v + 1;          // lo_hi

        // if (roll_dx_div_dy > 0) lo += 8;
        lda roll_dx_div_dy + 1;
        bmi L_no_hack;
        ora roll_dx_div_dy + 0;
        beq L_no_hack;
        jsr L_lo_add8;
    L_no_hack:

        // if (render_alt_box) { if (shift_x) lo += 8; if (shift_y) lo -= d >> 1; }
        lda render_alt_box;
        beq L_alt_done;
        lda render_alt_shift_x;
        beq L_alt_y;
        jsr L_lo_add8;
    L_alt_y:
        lda render_alt_shift_y;
        beq L_alt_done;
        lda roll_dx_div_dy + 1;
        cmp #$80;
        ror;
        tax;
        lda roll_dx_div_dy + 0;
        ror;
        sta vec_proj_tmp1;
        sec;
        lda vec_v + 0;
        sbc vec_proj_tmp1;
        sta vec_v + 0;
        txa;
        eor #$ff;               // lo_hi - x = lo_hi + ~x + C
        adc vec_v + 1;
        sta vec_v + 1;
    L_alt_done:

        // w = |roll_dx_div_dy| * 4 (kSkipLines == 4).
        lda roll_dx_div_dy + 1;
        bpl L_w_pos;
        sec;
        lda #0;
        sbc roll_dx_div_dy + 0;
        sta vec_v + 2;
        lda #0;
        sbc roll_dx_div_dy + 1;
        jmp L_w_shift;
    L_w_pos:
        ldx roll_dx_div_dy + 0;
        stx vec_v + 2;
    L_w_shift:
        asl vec_v + 2;
        rol;
        asl vec_v + 2;
        rol;
        sta vec_v + 3;

        // if (sky_left) lo -= w; (sky_left iff roll_dy < 0)
        bit roll_dy;
        bpl L_lo_ready;
        sec;
        lda vec_v + 0;
        sbc vec_v + 2;
        sta vec_v + 0;
        lda vec_v + 1;
        sbc vec_v + 3;
        sta vec_v + 1;
    L_lo_ready:

        lda #1;
        sta vec_sx + 0;         // run_ch = 1 (none)
        lda #0;
        sta vec_sx + 1;         // run_start = 0
        sta vec_sy + 0;         // y = 0

    L_row_loop:
        // hi = lo + w
        clc;
        lda vec_v + 0;
        adc vec_v + 2;
        sta vec_v + 4;
        lda vec_v + 1;
        adc vec_v + 3;
        sta vec_v + 5;

        // Test hi < 0x0010 (signed).
        bmi L_hi_lt10;
        bne L_chk_lo_full;
        lda vec_v + 4;
        cmp #$10;
        bcc L_hi_lt10;

    L_chk_lo_full:
        // Test lo >= 0x0280 (signed).
        lda vec_v + 1;
        bmi L_partial;
        cmp #2;
        bcc L_partial;
        bne L_lo_ge_full;
        bit vec_v + 0;
        bpl L_partial;

    L_lo_ge_full:
        // Full row, left side's: sky if roll_dy < 0 ($00), ground otherwise ($80).
        lda roll_dy;
        eor #$80;
        and #$80;
        jmp L_full_row;

    L_hi_lt10:
        // Full row, right side's: ground if roll_dy < 0 ($80), sky otherwise ($00).
        lda roll_dy;
        and #$80;

    L_full_row:
        cmp vec_sx + 0;
        bne L_full_new;
        jmp L_row_next;
    L_full_new:
        sta vec_sy + 1;         // save new ch
        lda vec_sx + 0;
        ldx vec_sx + 1;
        ldy vec_sy + 0;
        jsr _render_flush_span_core;
        lda vec_sy + 1;
        sta vec_sx + 0;
        lda vec_sy + 0;
        sta vec_sx + 1;
        jmp L_row_next;

    L_partial:
        // Flush any open full-row run before drawing this partial row.
        lda vec_sx + 0;
        cmp #1;
        beq L_part_go;
        ldx vec_sx + 1;
        ldy vec_sy + 0;
        jsr _render_flush_span_core;
        lda #1;
        sta vec_sx + 0;
    L_part_go:
        // Left span: if (lo >= 0x0010), fill [0 .. (lo >> 4) - 1].
        lda vec_v + 1;
        bmi L_right_chk;
        bne L_left_go;
        lda vec_v + 0;
        cmp #$10;
        bcc L_right_chk;
    L_left_go:
        lda vec_v + 0;
        lsr;
        lsr;
        lsr;
        lsr;
        ldx vec_v + 1;
        ora kRenderHiNibble, x;
        tax;
        dex;                    // X = (lo >> 4) - 1 (0..38)
        stx vec_mul_res + 0;
        lda #0;
        sta vec_sy + 1;         // start column r = 0
        lda roll_dy;
        eor #$80;
        and #$80;               // $00 if sky_left (roll_dy < 0), else $80
        jsr L_fill_span_row;

    L_right_chk:
        // Right span: if (hi < 0x0280), fill [r .. 39], r = hi >> 4 (1..39).
        lda vec_v + 5;
        cmp #2;
        bcc L_right_go;
        bne L_row_next;
        bit vec_v + 4;
        bmi L_row_next;
    L_right_go:
        lda vec_v + 4;
        lsr;
        lsr;
        lsr;
        lsr;
        ldx vec_v + 5;
        ora kRenderHiNibble, x;
        sta vec_sy + 1;         // start column r (1..39)
        lda #39;
        sec;
        sbc vec_sy + 1;
        sta vec_mul_res + 0;    // count - 1 = 39 - r (0..38)
        lda roll_dy;
        and #$80;               // $80 if sky_left (roll_dy < 0), else $00
        jsr L_fill_span_row;

    L_row_next:
        clc;
        lda vec_v + 0;
        adc roll_dx_div_dy + 0;
        sta vec_v + 0;
        lda vec_v + 1;
        adc roll_dx_div_dy + 1;
        sta vec_v + 1;
        inc vec_sy + 0;
        lda vec_sy + 0;
        cmp #14;
        bcs L_tilted_end;
        jmp L_row_loop;

    L_tilted_end:
        lda vec_sx + 0;
        ldx vec_sx + 1;
        ldy #14;
    L_flush_and_done:
        jsr _render_flush_span_core;
        lda _render_sky_rows;
        sta render_sky_rows;
        rts;

    L_lo_add8:
        clc;
        lda vec_v + 0;
        adc #8;
        sta vec_v + 0;
        bcc L_a8_r;
        inc vec_v + 1;
    L_a8_r:
        rts;

        // Fills row vec_sy+0 starting at col vec_sy+1 for vec_mul_res+0 + 1
        // cells with A ($00 sky or $80 ground), plus sky colour RAM if A == 0
        // and row >= render_sky_rows.
    L_fill_span_row:
        sta vec_proj_tmp1;
        ldy vec_sy + 0;
        lda mem_screen_row_ptrs + 0, y;
        ldx mem_screen_row_ptrs + 25, y;
        jsr L_patch_fill;
        lda vec_proj_tmp1;
        bne L_fsr_ret;
        cpy render_sky_rows;
        bcc L_fsr_ret;
#ifdef __ENABLE_COLOR_SCHEMES__
        lda color_scheme;
#else
        lda #14;
#endif
        sta vec_proj_tmp1;
        lda mem_color_row_ptrs + 0, y;
        ldx mem_color_row_ptrs + 14, y;
    L_patch_fill:
        clc;
        adc vec_sy + 1;
        bcc L_pf_nc;
        inx;
    L_pf_nc:
        sta L_fb0 + 1;
        sta L_fb1 + 1;
        stx L_fb0 + 2;
        stx L_fb1 + 2;
        ldx vec_mul_res + 0;
        txa;
        lsr;
        lda vec_proj_tmp1;
        bcc L_fb1;              // X even => odd count => 1 store then pairs
    L_fb0:
        sta $e800, x;
        dex;
    L_fb1:
        sta $e800, x;
        dex;
        bpl L_fb0;
    L_fsr_ret:
        rts;
  }
  // clang-format on
}

#endif // __OSCAR64__

#pragma optimize(pop)
