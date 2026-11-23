#include "box_asm.h"

#include <stddef.h>
#include <stdint.h>

// Out-of-line 6502 assembly half of box.cc (box_asm.h).

#ifdef __OSCAR64__

#ifndef __TARGET_TEST__
// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)
#endif

static_assert(offsetof(boxdef_t, w) == 0 && offsetof(boxdef_t, h) == 1 &&
                  offsetof(boxdef_t, step_x) == 2 &&
                  offsetof(boxdef_t, step_y) == 3 &&
                  offsetof(boxdef_t, rel_x) == 4 &&
                  offsetof(boxdef_t, rel_y) == 5 &&
                  offsetof(boxdef_t, grad1_color_start) == 6 &&
                  offsetof(boxdef_t, char_count) == 7 &&
                  offsetof(boxdef_t, flip_around) == 8,
              "boxdef_t layout changed");
static_assert(kCharDefPage1 == BOX_CHARDEF_PAGE1 &&
                  kCharDefPage2 == BOX_CHARDEF_PAGE2 &&
                  kCharSolidGround == BOX_CHAR_SOLID_GROUND &&
                  kCharSolidSky == BOX_CHAR_SOLID_SKY &&
                  kCharSolid11 == BOX_CHAR_SOLID_11,
              "box_asm chardef constants mismatch");

static_assert(BOX_CHAR_LUT_ADDR == BOX_SCRATCH_BASE + 0x60 &&
                  BOX_COLOR_LUT_ADDR == BOX_SCRATCH_BASE + 0x160,
              "box_asm LUT addresses mismatch");
#ifndef __TARGET_TEST__
static_assert(kViewportStartX == 0 && kViewportStartY == 0 &&
                  kViewportWidth == 40 && kViewportHeight == 14 &&
                  kScreenWidth == 40 && kScreenHeight == 25,
              "box_asm viewport/screen constants mismatch");
static_assert(MEM_BOOT_START == BOX_SCRATCH_BASE,
              "BOX_SCRATCH_BASE must match MEM_BOOT_START");
#endif

// Copies boxdef.char_count characters from chardef_bytes into char RAM at
// (BOX_CHAR_RAM_HI << 8) + (mem_box_char_start << 3), flipping characters in
// [flip_start, flip_end) vertically, builds _char_lut/_color_lut, and maps
// the w * h tile cells into _cur_box_chars and _cur_box_chars + 0x100.
__noinline void _box_prepare_fill(void) {
  vec_mul8_b = COLOR_SKY8;
  vec_mul8_lo = COLOR_GRAD1_8;
  // ZP scratch layout (none live across box_prepare):
  //   vec_v+0..1      = dst_ram pointer
  //   vec_v+2..3      = src_idx pointer (boxdef_char_idx, then cell map)
  //   vec_v+4         = flip_start
  //   vec_v+5         = flip_end
  //   vec_proj_mul_a  = current chardef_bytes source pointer
  //   vec_proj_tmp1   = char index i (0 .. char_count - 1)
  //   vec_mul8_b      = sky_col
  //   vec_mul8_lo     = grad1_col
  // clang-format off
  __asm {
        // dst_ram = (BOX_CHAR_RAM_HI << 8) + (mem_box_char_start << 3)
        lda mem_box_char_start;
        sta vec_v + 0;
        lda #0;
        asl vec_v + 0;
        rol;
        asl vec_v + 0;
        rol;
        asl vec_v + 0;
        rol;
        clc;
        adc #BOX_CHAR_RAM_HI;
        sta vec_v + 1;

        // src_idx = boxdef_char_idx
        lda boxdef_char_idx;
        sta vec_v + 2;
        lda boxdef_char_idx + 1;
        sta vec_v + 3;

        // flip_start = grad1_color_start - (flip_around & 0x0F)
        lda boxdef + 8;
        and #$0f;
        sta vec_v + 4;
        lda boxdef + 6;
        sec;
        sbc vec_v + 4;
        sta vec_v + 4;

        // flip_end = grad1_color_start + (flip_around >> 4)
        lda boxdef + 8;
        lsr;
        lsr;
        lsr;
        lsr;
        clc;
        adc boxdef + 6;
        sta vec_v + 5;

        // Stage 1: copy char_count unique characters to char RAM.
        lda #0;
        sta vec_proj_tmp1;
        lda boxdef + 7;
        beq L_char_done;
    L_char_loop:
        ldy vec_proj_tmp1;
        lda (vec_v + 2), y;       // c = src_idx[i]
        tax;
        clc;
        lda #<chardef_bytes;
        adc chardef_offset_lo, x;
        sta vec_proj_mul_a + 0;
        lda #>chardef_bytes;
        adc #0;
        cpx #BOX_CHARDEF_PAGE1;
        bcc L_pg_ok;
        adc #0;                   // C=1: +256
        cpx #BOX_CHARDEF_PAGE2;
        bcc L_pg_ok;
        adc #0;                   // C=1: +256
    L_pg_ok:
        sta vec_proj_mul_a + 1;

        // Y still == i. Flip if flip_start <= i < flip_end.
        cpy vec_v + 4;
        bcc L_do_norm;
        cpy vec_v + 5;
        bcs L_do_norm;

        lda vec_v + 0;
        sta L_fl_dst + 1;
        lda vec_v + 1;
        sta L_fl_dst + 2;
        ldy #7;
        ldx #0;
    L_fl_loop:
        lda (vec_proj_mul_a), y;
    L_fl_dst:
        sta $e000, x;
        inx;
        dey;
        bpl L_fl_loop;
        bmi L_char_next;          // always taken (Y == $FF)

    L_do_norm:
        ldy #7;
    L_cp_loop:
        lda (vec_proj_mul_a), y;
        sta (vec_v + 0), y;
        dey;
        bpl L_cp_loop;

    L_char_next:
        lda vec_v + 0;
        clc;
        adc #8;
        sta vec_v + 0;
        bcc L_dst_nc;
        inc vec_v + 1;
    L_dst_nc:
        inc vec_proj_tmp1;
        lda vec_proj_tmp1;
        cmp boxdef + 7;
        bcc L_char_loop;

    L_char_done:
        // Stage 2: populate _char_lut and _color_lut.
        lda #BOX_CHAR_SOLID_GROUND;
        sta BOX_CHAR_LUT_ADDR + 0;
        lda #BOX_CHAR_SOLID_SKY;
        sta BOX_CHAR_LUT_ADDR + 1;
        lda #BOX_CHAR_SOLID_11;
        sta BOX_CHAR_LUT_ADDR + 2;

        lda vec_mul8_lo;          // COLOR_GRAD1_8
        sta BOX_COLOR_LUT_ADDR + 0;
        sta BOX_COLOR_LUT_ADDR + 2;
        lda vec_mul8_b;           // COLOR_SKY8
        sta BOX_COLOR_LUT_ADDR + 1;

        ldx boxdef + 7;           // char_count
        beq L_lut_done;
        dex;
        txa;
        clc;
        adc mem_box_char_start;
        tay;                      // Y = mem_box_char_start + X
    L_lut_loop:
        tya;
        sta BOX_CHAR_LUT_ADDR + 3, x;
        lda vec_mul8_b;           // COLOR_SKY8
        cpx boxdef + 6;           // grad1_color_start
        bcc L_lut_col;
        lda vec_mul8_lo;          // COLOR_GRAD1_8
    L_lut_col:
        sta BOX_COLOR_LUT_ADDR + 3, x;
        dey;
        dex;
        bpl L_lut_loop;

    L_lut_done:
        // Stage 3: expand w * h cells through the LUTs into the slot caches.
        lda vec_v + 2;
        clc;
        adc boxdef + 7;
        sta vec_v + 2;
        bcc L_src_nc;
        inc vec_v + 3;
    L_src_nc:
        lda #0;
        ldx boxdef + 1;           // boxdef.h
        beq L_prep_done;
        clc;
    L_mul_wh:
        adc boxdef + 0;           // boxdef.w
        dex;
        bne L_mul_wh;
        tay;
        dey;                      // total_size - 1 (<= 95)
        bmi L_prep_done;

        lda _cur_box_chars;
        sta L_st_chr + 1;
        sta L_st_col + 1;
        lda _cur_box_chars + 1;
        sta L_st_chr + 2;
        clc;
        adc #1;
        sta L_st_col + 2;

    L_map_loop:
        lda (vec_v + 2), y;
        tax;
        lda BOX_CHAR_LUT_ADDR, x;
    L_st_chr:
        sta $0860, y;
        lda BOX_COLOR_LUT_ADDR, x;
    L_st_col:
        sta $0960, y;
        dey;
        bpl L_map_loop;
    L_prep_done:
  }
  // clang-format on
}

// Draws all horizon tile repetitions across the 40x14 viewport.
// - Fast path (boxdef.step_y == 0): every level box in boxdefs.cc (box_r8,
//   box_r8_alt, box_l8, box_l8_alt) has w == 4, |step_x| == 4, and uniform
//   rows, so each visible row is filled in one 4x-unrolled 40-byte pass for
//   screen RAM and one for color RAM.
// - General path (boxdef.step_y != 0): forward and backward tile walks with
//   inline bounds check, 8-bit clipping, and self-modifying 2D byte copy.
__noinline void _box_draw_asm(void) {
  // ZP scratch layout (none live across box_draw):
  //   Path A (step_y == 0):
  //     vec_v+2        = visible row count h
  //     vec_proj_recip = current screen row y
  //     vec_proj_tmp1  = byte offset into _cur_box_chars (+0x100 colors), r*4
  //     vec_mul8_b     = row char
  //     vec_mul8_lo    = row color
  //   Path B (step_y != 0):
  //     vec_v+0..1     = current tile (cx, cy)
  //     vec_v+2..5     = clipped (h, cy, cx, w) inside L_draw_one
  //     vec_sx+0..1    = (base_cx, base_cy)
  //     vec_sy+0..1    = (sx, sy)
  //     vec_proj_tmp1  = pass counter (2 -> 1)
  //     vec_proj_recip = clipped w - 1 inside L_draw_one
  //     vec_mul8_b     = clipped src_lo inside L_draw_one
  // clang-format off
  __asm {
        lda boxdef + 3;           // boxdef.step_y
        beq L_horiz;
        jmp L_general;
    L_horiz:
        // ---- Fast path A: level horizon (step_y == 0, w == 4, uniform rows) ----
        lda render_cy_chars;
        clc;
        adc boxdef + 5;           // base_cy = render_cy_chars + boxdef.rel_y
        sta vec_proj_recip;
        lda boxdef + 1;           // h = boxdef.h
        sta vec_v + 2;
        lda #0;
        sta vec_proj_tmp1;        // row_off = 0

        lda vec_proj_recip;
        bpl L_h_cy_pos;
        // cy < 0: h += cy; if h <= 0 return; row_off = (-cy) * 4; cy = 0.
        clc;
        adc vec_v + 2;
        bmi L_h_ret;
        beq L_h_ret;
        sta vec_v + 2;
        lda #0;
        sec;
        sbc vec_proj_recip;       // -cy (1..5)
        asl;
        asl;
        sta vec_proj_tmp1;
        lda #0;
        sta vec_proj_recip;
        beq L_h_chk_bot;          // always taken
    L_h_ret:
        rts;

    L_h_cy_pos:
        cmp #14;
        bcs L_h_ret;
    L_h_chk_bot:
        lda #14;
        sec;
        sbc vec_proj_recip;       // max_h = 14 - cy (1..14)
        cmp vec_v + 2;
        bcs L_h_h_ok;
        sta vec_v + 2;
    L_h_h_ok:
        lda vec_proj_recip;
        cmp render_sky_rows;
        bcs L_h_sky_ok;
        sta render_sky_rows;
    L_h_sky_ok:
        lda _cur_box_chars;
        sta L_h_ld_chr + 1;
        sta L_h_ld_col + 1;
        lda _cur_box_chars + 1;
        sta L_h_ld_chr + 2;
        clc;
        adc #1;
        sta L_h_ld_col + 2;

    L_h_row_loop:
        ldy vec_proj_tmp1;
    L_h_ld_chr:
        lda $0860, y;
        sta vec_mul8_b;
    L_h_ld_col:
        lda $0960, y;
        sta vec_mul8_lo;

        ldx vec_proj_recip;
        lda mem_screen_row_ptrs, x;
        ldy mem_screen_row_ptrs + 25, x;
        ldx vec_mul8_b;
        jsr L_fill40;

        ldx vec_proj_recip;
        lda mem_color_row_ptrs, x;
        ldy mem_color_row_ptrs + 14, x;
        ldx vec_mul8_lo;
        jsr L_fill40;

        lda vec_proj_tmp1;
        clc;
        adc #4;
        sta vec_proj_tmp1;
        inc vec_proj_recip;
        dec vec_v + 2;
        bne L_h_row_loop;
        rts;

    // 4x-unrolled 40-byte fill: A = dst_lo, Y = dst_hi, X = fill value.
    L_fill40:
        sta L_f0 + 1;
        sty L_f0 + 2;
        clc;
        adc #10;
        bcc L_f1_nc;
        iny;
        clc;
    L_f1_nc:
        sta L_f1 + 1;
        sty L_f1 + 2;
        adc #10;
        bcc L_f2_nc;
        iny;
        clc;
    L_f2_nc:
        sta L_f2 + 1;
        sty L_f2 + 2;
        adc #10;
        bcc L_f3_nc;
        iny;
    L_f3_nc:
        sta L_f3 + 1;
        sty L_f3 + 2;
        txa;
        ldy #9;
    L_f0:
        sta $e800, y;
    L_f1:
        sta $e80a, y;
    L_f2:
        sta $e814, y;
    L_f3:
        sta $e81e, y;
        dey;
        bpl L_f0;
        rts;

        // ---- General path B: tilted/vertical horizon (step_y != 0) ----
    L_general:
        lda render_cx_chars;
        clc;
        adc boxdef + 4;           // base_cx = render_cx_chars + boxdef.rel_x
        sta vec_sx + 0;
        sta vec_v + 0;
        lda render_cy_chars;
        clc;
        adc boxdef + 5;           // base_cy = render_cy_chars + boxdef.rel_y
        sta vec_sx + 1;
        sta vec_v + 1;
        lda boxdef + 2;           // sx = boxdef.step_x
        sta vec_sy + 0;
        lda boxdef + 3;           // sy = boxdef.step_y
        sta vec_sy + 1;
        lda #2;
        sta vec_proj_tmp1;        // pass = 2

        lda _cur_box_chars + 1;
        sta L_b_ld_chr + 2;
        clc;
        adc #1;
        sta L_b_ld_col + 2;

    L_pass_loop:
        jsr L_draw_one;
        lda vec_v + 0;
        clc;
        adc vec_sy + 0;
        sta vec_v + 0;
        lda vec_v + 1;
        clc;
        adc vec_sy + 1;
        sta vec_v + 1;

        // Inline _out_of_bounds(cx, cy, sx, sy)
        lda vec_sy + 0;
        bmi L_oob_sx_le;
        beq L_oob_sx_le;
        // sx > 0: out if (int8_t)cx >= 40
        lda vec_v + 0;
        bmi L_oob_chk_y;
        cmp #40;
        bcs L_pass_done;
        bcc L_oob_chk_y;
    L_oob_sx_le:
        // sx <= 0: out if (int8_t)(cx + boxdef.w) <= 0
        lda vec_v + 0;
        clc;
        adc boxdef + 0;
        bmi L_pass_done;
        beq L_pass_done;
    L_oob_chk_y:
        lda vec_sy + 1;
        bmi L_oob_sy_le;
        beq L_oob_sy_le;
        // sy > 0: out if (int8_t)cy >= 14
        lda vec_v + 1;
        bmi L_pass_loop;
        cmp #14;
        bcs L_pass_done;
        bcc L_pass_loop;
    L_oob_sy_le:
        // sy <= 0: out if (int8_t)(cy + boxdef.h) <= 0
        lda vec_v + 1;
        clc;
        adc boxdef + 1;
        bmi L_pass_done;
        beq L_pass_done;
        jmp L_pass_loop;

    L_pass_done:
        dec vec_proj_tmp1;
        beq L_all_done;
        // Second pass: reverse direction, start at (base_cx - sx, base_cy - sy).
        lda #0;
        sec;
        sbc vec_sy + 0;
        sta vec_sy + 0;
        clc;
        adc vec_sx + 0;
        sta vec_v + 0;
        lda #0;
        sec;
        sbc vec_sy + 1;
        sta vec_sy + 1;
        clc;
        adc vec_sx + 1;
        sta vec_v + 1;
        jmp L_pass_loop;
    L_all_done:
        rts;

    // Draw one clipped tile at (vec_v+0, vec_v+1). Preserves vec_v+0..1,
    // vec_sx, vec_sy, vec_proj_tmp1, and L_b_ld_chr+2 / L_b_ld_col+2.
    L_draw_one:
        lda _cur_box_chars;
        sta vec_mul8_b;           // src_lo (same low byte for chars & colors)
        lda boxdef + 1;           // h
        sta vec_v + 2;
        lda vec_v + 1;            // cy
        sta vec_v + 3;
        bpl L_b_cy_pos;
        // cy < 0: h += cy; if h <= 0 return; src_lo += (-cy) * w; cy = 0.
        clc;
        adc vec_v + 2;
        bmi L_b_ret;
        beq L_b_ret;
        sta vec_v + 2;
        lda #0;
        sec;
        sbc vec_v + 3;            // -cy (1..10)
        tax;
        lda vec_mul8_b;
        clc;
    L_b_skip_y:
        adc boxdef + 0;
        dex;
        bne L_b_skip_y;
        sta vec_mul8_b;
        lda #0;
        sta vec_v + 3;
        beq L_b_chk_bot;          // always taken
    L_b_ret:
        rts;

    L_b_cy_pos:
        cmp #14;
        bcs L_b_ret;
    L_b_chk_bot:
        lda #14;
        sec;
        sbc vec_v + 3;
        cmp vec_v + 2;
        bcs L_b_h_ok;
        sta vec_v + 2;
    L_b_h_ok:
        lda boxdef + 0;           // w
        sta vec_v + 5;
        lda vec_v + 0;            // cx
        sta vec_v + 4;
        bpl L_b_cx_pos;
        // cx < 0: w += cx; if w <= 0 return; src_lo -= cx; cx = 0.
        clc;
        adc vec_v + 5;
        bmi L_b_ret;
        beq L_b_ret;
        sta vec_v + 5;
        lda vec_mul8_b;
        sec;
        sbc vec_v + 4;
        sta vec_mul8_b;
        lda #0;
        sta vec_v + 4;
        beq L_b_chk_right;        // always taken
    L_b_cx_pos:
        cmp #40;
        bcs L_b_ret;
    L_b_chk_right:
        lda #40;
        sec;
        sbc vec_v + 4;
        cmp vec_v + 5;
        bcs L_b_w_ok;
        sta vec_v + 5;
    L_b_w_ok:
        // Lower render_sky_rows if cy < render_sky_rows.
        lda vec_v + 3;
        cmp render_sky_rows;
        bcs L_b_sky_ok;
        sta render_sky_rows;
    L_b_sky_ok:
        // Patch self-modifying pointers for the 2D copy loop.
        lda vec_mul8_b;
        sta L_b_ld_chr + 1;
        sta L_b_ld_col + 1;

        ldx vec_v + 3;            // cy
        lda mem_screen_row_ptrs, x;
        clc;
        adc vec_v + 4;            // + cx
        sta L_b_st_chr + 1;
        lda mem_screen_row_ptrs + 25, x;
        adc #0;
        sta L_b_st_chr + 2;

        lda mem_color_row_ptrs, x;
        clc;
        adc vec_v + 4;            // + cx
        sta L_b_st_col + 1;
        lda mem_color_row_ptrs + 14, x;
        adc #0;
        sta L_b_st_col + 2;

        ldx vec_v + 5;
        dex;
        stx vec_proj_recip;       // w - 1
        ldx vec_v + 2;            // row count h (>= 1)

    L_b_row:
        ldy vec_proj_recip;
    L_b_col:
    L_b_ld_chr:
        lda $0860, y;
    L_b_st_chr:
        sta $e800, y;
    L_b_ld_col:
        lda $0960, y;
    L_b_st_col:
        sta $d800, y;
        dey;
        bpl L_b_col;

        dex;
        beq L_b_done;

        lda L_b_ld_chr + 1;
        clc;
        adc boxdef + 0;
        sta L_b_ld_chr + 1;
        sta L_b_ld_col + 1;

        lda L_b_st_chr + 1;
        clc;
        adc #40;
        sta L_b_st_chr + 1;
        bcc L_b_chr_nc;
        inc L_b_st_chr + 2;
    L_b_chr_nc:
        lda L_b_st_col + 1;
        clc;
        adc #40;
        sta L_b_st_col + 1;
        bcc L_b_row;
        inc L_b_st_col + 2;
        bne L_b_row;              // always taken (high byte != 0)
    L_b_done:
        rts;
  }
  // clang-format on
}

#ifndef __TARGET_TEST__
#pragma optimize(pop)
#endif

#endif // __OSCAR64__
