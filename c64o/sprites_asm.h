#ifndef SPRITES_ASM_H
#define SPRITES_ASM_H

#include <stddef.h>
#include <stdint.h>

#include "bool.h"
#include "color.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "msg.h"
#include "spritedef.h"
#else
static const uint8_t kMsgHeightPixels = 8;
struct sprite_meta_t {
  int8_t pivot_x;
  int8_t pivot_y;
  uint8_t bitmap_idx;
};
extern uint16_t msg_span_x0;
extern uint16_t msg_span_x1;
bool msg_active(void);
extern const sprite_meta_t kSpriteDefSun;
extern const sprite_meta_t kSpriteDefOrient;
extern bool mem_using_alt_buffer;
#endif

#ifdef __OSCAR64__
#ifdef __TARGET_TEST__
static const uint8_t kSpriteOffsetX = 24;
static const uint8_t kSpriteOffsetY = 50;
static const uint8_t kSpriteWidthPixels = 24;
static const uint8_t kSpriteHeightPixels = 21;
static const uint16_t kScreenWidthPixels = 320;
static const uint16_t kViewportEndYPixels = 112;
static const uint8_t kSpritesOffLead = 22;
static const uint8_t kSpritesOffLeadExpandY = 43;
static const uint16_t kSpriteVisibleEndYPixels =
    kViewportEndYPixels - kSpritesOffLead;
static const uint8_t kSpritesMsgBoxBottom = kSpriteOffsetY + kMsgHeightPixels;
static const uint8_t kSpriteStackSize = 8;
static const uint8_t kSpriteStackFirst = 1;
static const uint8_t kSpriteNoBitmap = 0xFF;
static const uint8_t kSpriteFlagExpandX = 0x01;
static const uint8_t kSpriteFlagAlignDither = 0x02;
static const uint8_t kSpriteFlagWide = 0x04;
static const uint8_t kSpriteFlagExpandY = 0x08;
static const uint8_t kSpriteIdxOrient = 0;
static const uint8_t kSpriteOrientBit = 0x01;
static const uint8_t kSpriteOrientVX = 172;
static const uint8_t kSpriteOrientVY = 96;
static const uint8_t kSpriteFinFirst = 0;
static const uint8_t kSpriteFinCount = 2;
static const uint8_t kSpriteFinMask = 0x03;
static const uint8_t kSpriteFinVX = 172;
static const uint8_t kSpriteFinTopVY = 78;
static const uint8_t kSpriteFinBottomVY = 120;
static const uint8_t kFinSpriteBlock = 253;

struct sprite_cand_t {
  int16_t depth;
  int16_t x;
  uint8_t y;
  uint8_t bitmap;
  uint8_t bitmap2;
  uint8_t color;
  uint8_t flags;
};

struct sprite_frame_t {
  uint8_t pos[16];
  uint8_t ptr[8];
  uint8_t color[8];
  uint8_t msbx;
  uint8_t expand;
  uint8_t expand_y;
  uint8_t enable;
  uint8_t last;
};

static const uint8_t kSpriteCandSize = sizeof(sprite_cand_t);
static const uint8_t kSpriteCandBytes = kSpriteStackSize * kSpriteCandSize;
static sprite_cand_t _sprites_cand[kSpriteStackSize];
static uint8_t _sprites_cand_end;
static uint8_t _sprites_orient_off;
static uint8_t _sprites_fin_off;
static volatile sprite_frame_t _sprites_frames[2];
#endif

static_assert(kSpriteOffsetX == 24 && kSpriteOffsetY == 50 &&
                  kSpriteWidthPixels == 24 && kSpriteHeightPixels == 21 &&
                  kScreenWidthPixels == 320 &&
                  kSpriteVisibleEndYPixels + kSpriteHeightPixels - 1 == 110 &&
                  kSpritesMsgBoxBottom == 58 && kSpriteNoBitmap == 0xFF,
              "sprites_asm geometry constants");
static_assert(kSpriteFlagExpandX == 0x01 && kSpriteFlagAlignDither == 0x02 &&
                  kSpriteFlagWide == 0x04 && kSpriteFlagExpandY == 0x08,
              "sprites_asm flag bits");
static_assert(kSpriteStackSize == 8 && kSpriteStackFirst == 1 &&
                  kSpriteCandSize == 9 && kSpriteCandBytes == 72,
              "sprites_asm stack sizing");
static_assert(offsetof(sprite_cand_t, depth) == 0 &&
                  offsetof(sprite_cand_t, x) == 2 &&
                  offsetof(sprite_cand_t, y) == 4 &&
                  offsetof(sprite_cand_t, bitmap) == 5 &&
                  offsetof(sprite_cand_t, bitmap2) == 6 &&
                  offsetof(sprite_cand_t, color) == 7 &&
                  offsetof(sprite_cand_t, flags) == 8,
              "sprite_cand_t layout");
static_assert(offsetof(sprite_frame_t, pos) == 0 &&
                  offsetof(sprite_frame_t, ptr) == 16 &&
                  offsetof(sprite_frame_t, color) == 24 &&
                  offsetof(sprite_frame_t, msbx) == 32 &&
                  offsetof(sprite_frame_t, expand) == 33 &&
                  offsetof(sprite_frame_t, expand_y) == 34 &&
                  offsetof(sprite_frame_t, enable) == 35 &&
                  offsetof(sprite_frame_t, last) == 36 &&
                  sizeof(sprite_frame_t) == 37 &&
                  sizeof(_sprites_frames) == 74,
              "sprite_frame_t layout");
static_assert(kSpriteIdxOrient == 0 && kSpriteOrientBit == 0x01 &&
                  kSpriteOrientVX == 172 && kSpriteOrientVY == 96 &&
                  kColorInstrument == 1 && kColorOrientation == 1 &&
                  offsetof(sprite_meta_t, bitmap_idx) == 2,
              "sprites_asm orientation constants");
static_assert(kSpriteFinFirst == 0 && kSpriteFinCount == 2 &&
                  kSpriteFinMask == 0x03 && kSpriteFinVX == 172 &&
                  kSpriteFinTopVY == 78 && kSpriteFinBottomVY == 120 &&
                  kFinSpriteBlock == 253 && kColorAircraft == 12,
              "sprites_asm fin constants");
#ifdef __ENABLE_COLOR_SCHEMES__
static_assert(offsetof(ColorScheme, fin) == 7, "ColorScheme::fin offset");
#endif

bool sprites_stack_add(int16_t depth, int16_t x, int16_t y, int8_t pivot_x,
                       int8_t pivot_y, uint8_t bitmap, uint8_t bitmap2,
                       uint8_t color, uint8_t flags) {
  // clang-format off
  __asm {
        lda #0;
        sta vec_proj_tmp1;      // result = false

        // Reject immediately if the stack is full and depth >= farthest entry.
        lda _sprites_cand_end;
        cmp #72;
        bcc L_sa_room;
        lda depth + 1;
        cmp _sprites_cand + 64;
        bne L_sa_full_cmp;
        lda depth;
        cmp _sprites_cand + 63;
    L_sa_full_cmp:
        bcc L_sa_room;
        jmp L_sa_done;

    L_sa_room:
        lda pivot_x;
        sta vec_proj_mul_b;     // px
        lda #24;                // kSpriteWidthPixels
        sta vec_mul8_b;         // width
        ldy #21;                // kSpriteHeightPixels
        lda flags;
        beq L_sa_dims_done;
        lsr;                    // C = ExpandX (0x01)
        bcc L_sa_no_expx;
        asl vec_proj_mul_b;     // px <<= 1 (uint8_t)
        asl vec_mul8_b;         // width = 48
    L_sa_no_expx:
        tax;                    // X = flags >> 1
        and #$02;               // Wide (0x04 >> 1)
        beq L_sa_no_wide;
        asl vec_mul8_b;         // width <<= 1
    L_sa_no_wide:
        txa;
        and #$04;               // ExpandY (0x08 >> 1)
        beq L_sa_dims_done;
        ldy #42;
    L_sa_dims_done:
        sty frac_q0;            // pitch
        dey;                    // pitch - 1
        ldx bitmap2;
        inx;                    // 0xFF -> 0
        beq L_sa_1row;
        tya;
        clc;
        adc frac_q0;
        tay;
    L_sa_1row:
        sty frac_q0;            // h_m1

        lda x;
        sec;
        sbc vec_proj_mul_b;
        sta vec_proj_mul_a;     // x_lo
        lda x + 1;
        sbc #0;
        sta vec_proj_mul_a + 1; // x_hi

        lda y;
        sec;
        sbc pivot_y;
        tay;                    // Y = y_lo
        lda y + 1;
        sbc #0;
        sta vec_proj_mul_b + 1; // y_hi

        lda flags;
        and #$02;               // kSpriteFlagAlignDither
        beq L_sa_no_dither;
        lda vec_proj_mul_a;     // x_lo
        and #$fe;
        sta vec_proj_mul_a;
        and #$02;
        lsr;                    // (x >> 1) & 1
        sta vec_proj_mul_b;
        tya;
        and #$fe;
        ora vec_proj_mul_b;
        tay;
    L_sa_no_dither:
        // Vertical cull: (uint16_t)(y + h_m1) < 110.
        tya;
        clc;
        adc frac_q0;
        tax;
        lda vec_proj_mul_b + 1;
        adc #0;
        bne L_sa_ret0;
        cpx #110;
        bcs L_sa_ret0;
        // Carry is 0 (X < 110).
        tya;
        adc #50;                // vy = (uint8_t)y + kSpriteOffsetY
        sta vec_proj_recip;

        // Horizontal culls via vx = x + 24:
        //   vx < 0 || x >= 320  <=>  (uint16_t)vx >= 344 ($0158)
        //   x + width <= 0 (given vx >= 0)  <=>  vx == 0 && width == 24
        lda vec_proj_mul_a;
        clc;
        adc #24;
        sta vec_proj_mul_a;     // vx_lo
        tay;
        lda vec_proj_mul_a + 1;
        adc #0;
        sta vec_proj_mul_a + 1; // vx_hi
        beq L_sa_vx_hi0;
        cmp #1;
        bne L_sa_ret0;
        cpy #88;                // 344 - 256
        bcs L_sa_ret0;
        bcc L_sa_vx_ok;
    L_sa_vx_hi0:
        tya;
        bne L_sa_vx_ok;
        lda vec_mul8_b;
        cmp #24;
        bne L_sa_vx_ok;
    L_sa_ret0:
        jmp L_sa_done;

    L_sa_vx_ok:
        // Message box overlap: vy < 58 && msg_active() && vx < x1 && vx + w > x0.
        lda vec_proj_recip;
        cmp #58;
        bcs L_sa_no_msg;
        jsr msg_active;
        ora #0;
        beq L_sa_no_msg;
        lda msg_span_x1;
        clc;
        adc #24;
        sta vec_proj_mul_b;
        lda msg_span_x1 + 1;
        adc #0;
        sta vec_proj_mul_b + 1;
        lda vec_proj_mul_a;
        cmp vec_proj_mul_b;
        lda vec_proj_mul_a + 1;
        sbc vec_proj_mul_b + 1;
        bcs L_sa_no_msg;
        lda vec_proj_mul_a;
        clc;
        adc vec_mul8_b;
        sta vec_proj_mul_b;
        lda vec_proj_mul_a + 1;
        adc #0;
        sta vec_proj_mul_b + 1;
        lda msg_span_x0;
        clc;
        adc #24;
        tax;
        lda msg_span_x0 + 1;
        adc #0;
        cpx vec_proj_mul_b;
        sbc vec_proj_mul_b + 1;
        bcc L_sa_ret0;

    L_sa_no_msg:
        // Distance-sorted insertion into _sprites_cand (9 B stride).
        // If _sprites_cand_end == 72, L_sa_room already verified depth < slot 7.
        ldx _sprites_cand_end;
        cpx #72;
        bcc L_sa_grow;
        ldx #63;
        bne L_sa_ins;
    L_sa_grow:
        txa;
        adc #9;                 // Carry is 0 from BCC
        sta _sprites_cand_end;
    L_sa_ins:
        txa;
        beq L_sa_store;
        tay;                    // Y = o (9..63)
        sec;
        sbc #9;
        tax;                    // X = prev = o - 9
        lda depth + 1;
        cmp _sprites_cand + 1, x;
        bne L_sa_dcmp;
        lda depth;
        cmp _sprites_cand, x;
    L_sa_dcmp:
        bcs L_sa_store_y;
        lda _sprites_cand, x;
        sta _sprites_cand + 9, x;
        lda _sprites_cand + 1, x;
        sta _sprites_cand + 10, x;
        lda _sprites_cand + 2, x;
        sta _sprites_cand + 11, x;
        lda _sprites_cand + 3, x;
        sta _sprites_cand + 12, x;
        lda _sprites_cand + 4, x;
        sta _sprites_cand + 13, x;
        lda _sprites_cand + 5, x;
        sta _sprites_cand + 14, x;
        lda _sprites_cand + 6, x;
        sta _sprites_cand + 15, x;
        lda _sprites_cand + 7, x;
        sta _sprites_cand + 16, x;
        lda _sprites_cand + 8, x;
        sta _sprites_cand + 17, x;
        jmp L_sa_ins;
    L_sa_store_y:
        tya;
        tax;
    L_sa_store:
        lda depth;
        sta _sprites_cand, x;
        lda depth + 1;
        sta _sprites_cand + 1, x;
        lda vec_proj_mul_a;
        sta _sprites_cand + 2, x;
        lda vec_proj_mul_a + 1;
        sta _sprites_cand + 3, x;
        lda vec_proj_recip;
        sta _sprites_cand + 4, x;
        lda bitmap;
        sta _sprites_cand + 5, x;
        lda bitmap2;
        sta _sprites_cand + 6, x;
        lda color;
        sta _sprites_cand + 7, x;
        lda flags;
        sta _sprites_cand + 8, x;
        inc vec_proj_tmp1;
    L_sa_done:
  }
  // clang-format on
  return (bool)vec_proj_tmp1;
}

void sprites_stack_commit(void) {
  // clang-format off
  __asm {
        lda #0;
        sta frac_q0;            // msbx
        sta frac_q1;            // expand
        sta frac_r0;            // expand_y
        sta frac_r1;            // enable
        ldx mem_using_alt_buffer;
        beq L_sc_base_ok;
        lda #37;                // sizeof(sprite_frame_t)
    L_sc_base_ok:
        sta vec_proj_mul_a;     // base (0 or 37)

        ldx #1;                 // idx = kSpriteStackFirst
        ldy #2;                 // bit = 1 << 1
        lda _sprites_fin_off;
        bne L_sc_init_ok;
        inx;                    // idx = 2
        ldy #4;                 // bit = 1 << 2
    L_sc_init_ok:
        stx frac_d1;            // idx
        sty frac_d0;            // bit
        txa;
        clc;
        adc vec_proj_mul_a;
        sta vec_proj_mul_b;     // ptr_off = base + idx
        txa;
        asl;                    // 2 * idx (Carry = 0)
        adc vec_proj_mul_a;
        sta vec_proj_mul_a + 1; // pos_off = base + 2 * idx

        // X holds cand_off (0, 9, ...) throughout the candidate loop.
        ldx #0;
    L_sc_cand:
        cpx _sprites_cand_end;
        bcc L_sc_check_slots;
    L_sc_exit:
        jmp L_sc_cands_done;
    L_sc_check_slots:
        lda _sprites_cand + 8, x;
        sta vec_mul8_lo;        // flags (bits 0..3; bit 7 is 0)
        and #$04;               // Wide
        beq L_sc_1col;
        lda #2;
        bne L_sc_cols_ok;
    L_sc_1col:
        lda #1;
    L_sc_cols_ok:
        ldy _sprites_cand + 6, x;
        sty vec_proj_tmp1;      // next_bitmap2 (0xFF if single row)
        iny;                    // 0xFF -> 0
        beq L_sc_1row;
        asl;
    L_sc_1row:
        clc;
        adc frac_d1;
        cmp #9;                 // idx + slots > 8?
        bcs L_sc_exit;

        lda #0;
        sta vec_proj_mul_b + 1; // cand_mask = 0
        lda _sprites_cand + 4, x;
        sta vec_proj_recip;     // sy
        lda _sprites_cand + 5, x;
        sta vec_mul8_b;         // cur_ptr

    L_sc_row:
        lda vec_mul8_lo;
        and #$04;               // Wide?
        beq L_sc_col0;
        lda vec_mul8_lo;
        ora #$80;               // mark right column pending in bit 7
        sta vec_mul8_lo;
    L_sc_col0:
        lda _sprites_cand + 3, x;
        lsr;                    // C = bit 8 of c->x
        lda _sprites_cand + 2, x;

    L_sc_slot:
        ldy vec_proj_mul_a + 1; // pos_off (preserves A and C)
        sta _sprites_frames, y;
        lda vec_proj_recip;
        sta _sprites_frames + 1, y;
        iny;
        iny;
        sty vec_proj_mul_a + 1;
        bcc L_sc_no_msbx;
        lda frac_d0;
        ora frac_q0;
        sta frac_q0;
    L_sc_no_msbx:
        lda frac_d0;
        ora vec_proj_mul_b + 1;
        sta vec_proj_mul_b + 1;
        asl frac_d0;
        inc frac_d1;
        ldy vec_proj_mul_b;
        inc vec_proj_mul_b;
        lda vec_mul8_b;
        sta _sprites_frames + 16, y;
        lda _sprites_cand + 7, x;
        sta _sprites_frames + 24, y;

        lda vec_mul8_lo;
        bpl L_sc_row_done;
        and #$7f;
        sta vec_mul8_lo;
        inc vec_mul8_b;         // right column: next bitmap block
        lsr;                    // C = ExpandX (bit 0)
        lda #24;
        bcc L_sc_colw;
        asl;                    // 48
    L_sc_colw:
        clc;
        adc _sprites_cand + 2, x;
        tay;
        lda _sprites_cand + 3, x;
        adc #0;
        lsr;                    // C = bit 8 of (c->x + col_w)
        tya;
        jmp L_sc_slot;

    L_sc_row_done:
        ldy vec_proj_tmp1;
        iny;                    // 0xFF -> 0
        beq L_sc_masks;
        dey;
        sty vec_mul8_b;         // cur_ptr = bitmap2
        ldy #$ff;
        sty vec_proj_tmp1;      // mark second row as final
        and #$08;               // A still holds flags: test ExpandY
        beq L_sc_p21;
        lda #42;
        bne L_sc_pitch_ok;
    L_sc_p21:
        lda #21;
    L_sc_pitch_ok:
        clc;
        adc vec_proj_recip;
        sta vec_proj_recip;
        jmp L_sc_row;

    L_sc_masks:
        lda vec_proj_mul_b + 1;
        ora frac_r1;
        sta frac_r1;
        lda vec_mul8_lo;
        lsr;                    // C = ExpandX
        bcc L_sc_no_expx;
        lda vec_proj_mul_b + 1;
        ora frac_q1;
        sta frac_q1;
    L_sc_no_expx:
        lda vec_mul8_lo;
        and #$08;               // ExpandY
        beq L_sc_next_cand;
        lda vec_proj_mul_b + 1;
        ora frac_r0;
        sta frac_r0;
    L_sc_next_cand:
        txa;
        clc;
        adc #9;
        tax;
        jmp L_sc_cand;

    L_sc_cands_done:
        ldx vec_proj_mul_a;     // base (0 or 37)
        ldy frac_d1;            // idx
        dey;
        tya;
        sta _sprites_frames + 36, x;

        txa;
        clc;
        adc #16;
        sta vec_mul8_b;         // pos_end = base + 16
        ldy vec_proj_mul_a + 1; // pos_off = base + 2 * idx
        lda #0;
    L_sc_park:
        cpy vec_mul8_b;
        bcs L_sc_park_done;
        sta _sprites_frames, y;
        sta _sprites_frames + 1, y;
        iny;
        iny;
        bne L_sc_park;
    L_sc_park_done:

        lda _sprites_fin_off;
        bne L_sc_not_fin;
        lda #172;               // kSpriteFinVX
        sta _sprites_frames, x;
        sta _sprites_frames + 2, x;
        lda #78;                // kSpriteFinTopVY
        sta _sprites_frames + 1, x;
        lda #120;               // kSpriteFinBottomVY
        sta _sprites_frames + 3, x;
        lda #253;               // kFinSpriteBlock
        sta _sprites_frames + 16, x;
        lda #254;               // kFinSpriteBlock + 1
        sta _sprites_frames + 17, x;
#ifdef __ENABLE_COLOR_SCHEMES__
        lda color_scheme + 7;
#else
        lda #12;                // kColorAircraft
#endif
        sta _sprites_frames + 24, x;
        sta _sprites_frames + 25, x;
        lda frac_r0;
        ora #$03;               // kSpriteFinMask
        sta frac_r0;
        lda frac_r1;
        ora #$03;               // kSpriteFinMask
        sta frac_r1;
        bne L_sc_ctrl;

    L_sc_not_fin:
        lda #1;                 // kColorInstrument == kColorOrientation
        sta _sprites_frames + 24, x;
        lda _sprites_orient_off;
        beq L_sc_orient_on;
        lda #0;
        sta _sprites_frames, x;
        sta _sprites_frames + 1, x;
        lda kSpriteDefSun + 2;
        jmp L_sc_orient_ptr;
    L_sc_orient_on:
        inc frac_r1;            // enable |= kSpriteOrientBit
        lda #172;               // kSpriteOrientVX
        sta _sprites_frames, x;
        lda #96;                // kSpriteOrientVY
        sta _sprites_frames + 1, x;
        lda kSpriteDefOrient + 2;
    L_sc_orient_ptr:
        sta _sprites_frames + 16, x;

    L_sc_ctrl:
        lda frac_q0;            // msbx
        sta _sprites_frames + 32, x;
        lda frac_q1;            // expand
        sta _sprites_frames + 33, x;
        lda frac_r0;            // expand_y
        sta _sprites_frames + 34, x;
        lda frac_r1;            // enable
        sta _sprites_frames + 35, x;
  }
  // clang-format on
}
#endif

#endif
