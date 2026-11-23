#ifndef SPRITES_ASM_H
#define SPRITES_ASM_H

#include <stddef.h>
#include <stdint.h>

#include "bool.h"
#include "spritedef.h"
#include "sprites.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "color.h"
#include "mem.h"
#include "msg.h"
#include "view.h"
#else
static const uint16_t kScreenWidthPixels = 320;
static const uint16_t kViewportEndYPixels = 112;
static const uint8_t kSpritesOffLead = 22;
static const uint8_t kSpritesOffLeadExpandY = 43;
static const uint16_t kSpriteVisibleEndYPixels =
    kViewportEndYPixels - kSpritesOffLead;
static const uint8_t kFinSpriteBlock = 253;
static const uint8_t kMsgHeightPixels = 8;
extern uint16_t msg_span_x0;
extern uint16_t msg_span_x1;
bool msg_active(void);
extern bool mem_using_alt_buffer;
#endif

// The assembly half of sprites.cc, in sprites_asm.cc: the distance-sorted
// viewport sprite stack (sprites_stack_add, sprites_stack_commit) and the
// per-frame instrument gauge updates (_sprites_set_instrument_sprite,
// sprites_set_alt). sprites.cc keeps the C counterparts under #ifndef
// __OSCAR64__ for test/sprites_test.cc, and test/target_test.cc holds the 6502
// assembly to 16-bit C references on the emulated 6510.

static const uint8_t kSpriteOffsetX = 24;
static const uint8_t kSpriteOffsetY = 50;

// An unexpanded sprite.
static const uint8_t kSpriteWidthPixels = 24;
static const uint8_t kSpriteHeightPixels = 21;

static const uint8_t kSpriteSpeedPivotX = 112;
static const uint8_t kSpriteSpeedPivotY = 176;
static const uint8_t kSpriteAltPivotX = 208;
static const uint8_t kSpriteAltPivotY = 176;
static const uint8_t kSpriteVSpeedPivotX = 214;
static const uint8_t kSpriteVSpeedPivotY = 136;
static const uint8_t kSpriteRollPivotX = 160;
static const uint8_t kSpriteRollPivotY = 176;
static const uint8_t kSpriteThrottlePivotX = 8; // 264 & 0xff
static const uint8_t kSpriteThrottlePivotY = 173;
static const uint8_t kSpriteFuelPivotX = 56;
static const uint8_t kSpriteFuelPivotY = 173;

// The vertical-speed needle is drawn above the split and the other seven
// below it, so it shares its index with the orientation indicator's
// (sprites.h).
static const uint8_t kSpriteIdxVSpeed = 0;
static const uint8_t kSpriteIdxSpeed = 1;
static const uint8_t kSpriteIdxRoll = 2;
static const uint8_t kSpriteIdxPitch = 3;
static const uint8_t kSpriteIdxAlt1 = 4;
static const uint8_t kSpriteIdxAlt2 = 5;
static const uint8_t kSpriteIdxThrottle = 6;
static const uint8_t kSpriteIdxFuel = 7;

// Hardware sprite 0, and the bit that is its own in $D015: in front of
// every sprite the stack hands out, and the needle's index below the split.
static const uint8_t kSpriteIdxOrient = 0;
static const uint8_t kSpriteOrientBit = 1 << kSpriteIdxOrient;
static const uint8_t kSpriteOrientPivotX = 12;
static const uint8_t kSpriteOrientPivotY = 10;
static const uint8_t kSpriteOrientVX =
    kScreenWidthPixels / 2 - kSpriteOrientPivotX + kSpriteOffsetX;
static const uint8_t kSpriteOrientVY =
    kViewportEndYPixels / 2 - kSpriteOrientPivotY + kSpriteOffsetY;

// Tail fin layout on hardware sprites 0 and 1 in the back view.
static const uint8_t kSpriteFinFirst = 0;
static const uint8_t kSpriteFinCount = 2;
static const uint8_t kSpriteFinMask = ((1 << kSpriteFinCount) - 1)
                                      << kSpriteFinFirst;
static const uint8_t kSpriteFinPitch = 2 * kSpriteHeightPixels;
static const uint8_t kSpriteFinPivotX = 12;
static const uint8_t kSpriteFinVX =
    kScreenWidthPixels / 2 - kSpriteFinPivotX + kSpriteOffsetX;
static const uint8_t kSpriteFinDropLines = 2;
static const uint8_t kSpriteFinBottomVY = kSpriteOffsetY + kViewportEndYPixels -
                                          1 - kSpritesOffLeadExpandY +
                                          kSpriteFinDropLines;
static const uint8_t kSpriteFinTopVY =
    kSpriteFinBottomVY - (kSpriteFinCount - 1) * kSpriteFinPitch;

static const uint8_t kSpritesMsgBoxBottom = kSpriteOffsetY + kMsgHeightPixels;

// Any value but 0: a frame that never sets the mark or fin shows neither.
static const uint8_t kSpritesOff = 0xFF;

struct sprite_xy_t {
  uint8_t x;
  uint8_t y;
};

// One offered object, before indices are handed out. Insertion-sorted by
// ascending depth, so entry 0 is the nearest.
struct sprite_cand_t {
  int16_t depth;
  int16_t x; // VIC sprite coordinates, top left, pivot already applied.
  uint8_t y;
  uint8_t bitmap;
  uint8_t bitmap2; // kSpriteNoBitmap for a single sprite.
  uint8_t color;
  uint8_t flags;
};

// The committed frame. The layout is chosen for the *interrupt* that reads it,
// not for the main line that writes it: `pos` mirrors $D000-$D00F byte for
// byte, x and y interleaved, so the handler copies it with one index register
// and one stride. Holding x and y in separate arrays would need two indices at
// different strides, and on a 6502 that is one live value more than there are
// registers - oscar64 spills the difference into its runtime zero page, which
// a raster handler may not touch. See _sprites_program_frame() in sprites.cc.
struct sprite_frame_t {
  uint8_t pos[16];
  uint8_t ptr[8];
  uint8_t color[8];
  uint8_t msbx;
  uint8_t expand;
  // $D017. Only the tail fin ever sets a bit here, but the field is per frame
  // rather than written once at the view switch: the terrain band programs the
  // whole of the sprite state from one frame, and a register it did not own
  // would be one more thing to unwind on the way back to the panel.
  uint8_t expand_y;
  uint8_t enable;
  // The last index in use: at least 0, the mark's or the fin's. The handler
  // copies the pointers and colours up to it and no further; the rest are
  // disabled, and nothing reads a disabled sprite's. A zeroed frame (bss2;
  // polydemo never calls sprites_init()) makes it copy one.
  uint8_t last;
};

static const uint8_t kSpriteCandSize = sizeof(sprite_cand_t);
static const uint8_t kSpriteCandBytes = kSpriteStackSize * kSpriteCandSize;
static_assert(kSpriteStackSize * sizeof(sprite_cand_t) < 256,
              "the candidates' byte offsets fit in uint8_t");
#define SPRITE_CAND(o) ((sprite_cand_t *)((uint8_t *)_sprites_cand + (o)))

extern const uint8_t kSpriteInstrumentPivotX[8];
extern const uint8_t kSpriteInstrumentPivotY[8];

extern sprite_cand_t _sprites_cand[kSpriteStackSize];
extern uint8_t _sprites_cand_end;
extern uint8_t _sprites_orient_off;
extern uint8_t _sprites_fin_off;

extern volatile sprite_frame_t _sprites_frames[2];
#define _sprites_frame_a (_sprites_frames[0])
#define _sprites_frame_b (_sprites_frames[1])

extern volatile sprite_xy_t _sprites_instrument_xy[8];
extern volatile uint8_t _sprites_instrument_idx[8];

void _sprites_set_instrument_sprite(uint8_t idx, uint8_t dir);

#ifdef __OSCAR64__
inline void sprites_set_alt(uint16_t alt) {
  vec_proj_mul_a = (int16_t)(alt >> 4);
  _sprites_set_instrument_sprite(kSpriteIdxAlt1, (uint8_t)vec_proj_mul_a);
  // ((alt >> 4) * 205) >> 11: the middle 16 bits' low byte is
  // ((a_lo * 205) >> 8) + (uint8_t)(a_hi * 205) mod 256, shifted right by 3,
  // matching ((uint8_t)vec_fastmul8p8(alt >> 4, 205)) >> 3 without 16x16 mul.
  // clang-format off
  __asm {
        lda #205;
        sta vec_mul8_b;
        lda vec_proj_mul_a;
        jsr vec_mul8;
        ldx vec_proj_mul_a + 1;
        beq L_sa2_shift;
        sta vec_proj_tmp1;
        txa;
        jsr vec_mul8;
        lda vec_mul8_lo;
        clc;
        adc vec_proj_tmp1;
    L_sa2_shift:
        lsr;
        lsr;
        lsr;
        sta vec_proj_tmp1;
  }
  // clang-format on
  _sprites_set_instrument_sprite(kSpriteIdxAlt2, vec_proj_tmp1);
}

inline void sprites_set_vspeed(int16_t vspeed) {
  vec_proj_mul_a = vspeed;
  // +/-0x300 are exact multiples of 256 (hi == +3 and hi == -3), and
  // vspeed == 0x300 shifts to +12, so clamping on the high byte matches
  // clamping vspeed to [-0x300, 0x300] across every int16_t.
  // clang-format off
  __asm {
        lda vec_proj_mul_a + 1;
        bmi L_vs_neg;
        cmp #3;
        bcc L_vs_in;
        lda #$24;
        bne L_vs_done;
    L_vs_neg:
        cmp #$fd;
        bcs L_vs_in;
        lda #$0c;
        bne L_vs_done;
    L_vs_in:
        asl vec_proj_mul_a;
        rol;
        asl vec_proj_mul_a;
        rol;
        clc;
        adc #$18;
    L_vs_done:
        sta vec_proj_tmp1;
  }
  // clang-format on
  _sprites_set_instrument_sprite(kSpriteIdxVSpeed, vec_proj_tmp1);
}

#if !defined(__TARGET_TEST__) || defined(__TARGET_TEST_PLANES__)
// Raster-IRQ sprite programmers (zero runtime ZP < $60 touched; verified by
// tools/check_irq_zp.py and target_planes_test.prg). Host C counterparts live
// in sprites.cc under #ifndef __OSCAR64__.
#ifndef __TARGET_TEST__
static_assert(VIEW_UNKNOWN == 0 && VIEW_LEFT == 1 && VIEW_CENTER == 2 &&
                  VIEW_RIGHT == 3 && VIEW_BACK == 4,
              "sprites_asm view_state_t values");
static_assert(kColorInstrument == 1, "sprites_asm kColorInstrument");
#endif
static_assert(kSpriteIdxVSpeed == 0 && kSpriteIdxThrottle == 6 &&
                  kSpriteIdxFuel == 7,
              "sprites_asm instrument indices");
static_assert(offsetof(sprite_frame_t, pos) == 0 &&
                  offsetof(sprite_frame_t, ptr) == 16 &&
                  offsetof(sprite_frame_t, color) == 24 &&
                  offsetof(sprite_frame_t, msbx) == 32 &&
                  offsetof(sprite_frame_t, expand) == 33 &&
                  offsetof(sprite_frame_t, expand_y) == 34 &&
                  offsetof(sprite_frame_t, enable) == 35 &&
                  offsetof(sprite_frame_t, last) == 36 &&
                  sizeof(sprite_frame_t) == 37,
              "sprites_asm sprite_frame_t layout");

// Programs the active terrain frame (Frame A -> $EBF8 when drawing into alt,
// Frame B -> $EFF8 when drawing into main). Writes $D015 first so unused
// sprites are disabled before raster 256, skips all register copies when
// enable == 0, and copies only active indices 0..last.
inline void sprites_show_terrain_sprites(void) {
  // clang-format off
  __asm {
        lda mem_using_alt_buffer;
        bne L_st_a;
        ldx #37;                    // _sprites_frame_b offset
        lda #$ef;                   // hi byte of $EFF8 (kScreenRamAlt + 1016)
        bne L_st_go;
    L_st_a:
        ldx #0;                     // _sprites_frame_a offset
        lda #$eb;                   // hi byte of $EBF8 (kScreenRamMain + 1016)
    L_st_go:
        sta L_st_ptr_st + 2;
        lda _sprites_frames + 35, x; // enable
        sta $d015;
        beq L_st_done;

        lda _sprites_frames + 32, x; // msbx
        sta $d010;
        lda _sprites_frames + 33, x; // expand_x
        sta $d01d;
        lda _sprites_frames + 34, x; // expand_y
        sta $d017;

        ldy _sprites_frames + 36, x; // Y = last (0..7)
        txa;
        clc;
        adc _sprites_frames + 36, x; // base + last (<= 44)
        tax;
    L_st_pc:
        lda _sprites_frames + 16, x;
    L_st_ptr_st:
        sta $ebf8, y;
        lda _sprites_frames + 24, x;
        sta $d027, y;
        dex;
        dey;
        bpl L_st_pc;

        inx;                        // X = base (0 or 37)
        lda _sprites_frames + 36, x; // last (0..7)
        asl;                        // 2 * last (0..14), C = 0
        tay;                        // Y = 2 * last
        txa;                        // base, C = 0
        adc _sprites_frames + 36, x; // base + last, C = 0
        adc _sprites_frames + 36, x; // base + 2 * last
        tax;
    L_st_pos:
        lda _sprites_frames + 1, x;
        sta $d001, y;
        lda _sprites_frames, x;
        sta $d000, y;
        dex;
        dex;
        dey;
        dey;
        bpl L_st_pos;
    L_st_done:
  }
  // clang-format on
}

inline void sprites_show_no_sprites(void) {
  // clang-format off
  __asm {
        lda #0;
        sta $d015;
        sta $d01d;
        sta $d017;
        sta $d010;
  }
  // clang-format on
}

// Top panel band (rasters 163..186): only sprite 0 (vertical speed needle) is
// shown in VIEW_CENTER; all other views disable sprites via $D015 so disabled
// sprites pay zero VIC-II DMA.
inline void sprites_show_panel_top_sprites(void) {
  // clang-format off
  __asm {
        ldy #0;
        sty $d01d;                  // spr_expand_x = 0
        sty $d017;                  // spr_expand_y = 0
        sty $d010;                  // spr_msbx = 0
        lda view_state;
        cmp #2;                     // VIEW_CENTER
        beq L_pt_center;
        sty $d015;                  // disable all sprites
        bne L_pt_done;              // always taken (Z = 0 from cmp #2)
    L_pt_center:
        lda #1;                     // (1 << kSpriteIdxVSpeed) == kColorInstrument
        sta $d015;
        sta $d027;
        lda _sprites_instrument_xy;
        sta $d000;
        lda _sprites_instrument_xy + 1;
        sta $d001;
        lda _sprites_instrument_idx;
        sta $eff8;
    L_pt_done:
  }
  // clang-format on
}

// Bottom panel band (rasters 187..250): enables only the sprites visible in the
// current view (1..7 + VSpeed 0 in VIEW_CENTER, Fuel 7 in VIEW_LEFT, Throttle 6
// in VIEW_RIGHT, none in VIEW_BACK).
inline void sprites_show_panel_bottom_sprites(void) {
  // clang-format off
  __asm {
        lda view_state;
        cmp #2;                     // VIEW_CENTER
        beq L_pb_center;
        cmp #1;                     // VIEW_LEFT
        beq L_pb_left;
        cmp #3;                     // VIEW_RIGHT
        bne L_pb_done;

        // VIEW_RIGHT: Throttle (sprite 6); $d010 is already 0 from panel top.
        lda #1;                     // kColorInstrument
        sta $d02d;
        lda _sprites_instrument_xy + 12;
        sta $d00c;
        lda _sprites_instrument_xy + 13;
        sta $d00d;
        lda _sprites_instrument_idx + 6;
        sta $effe;
        lda #$40;                   // 1 << kSpriteIdxThrottle
        sta $d015;
        bne L_pb_done;

    L_pb_left:
        // VIEW_LEFT: Fuel (sprite 7)
        lda #1;                     // kColorInstrument
        sta $d02e;
        lda _sprites_instrument_xy + 14;
        sta $d00e;
        lda _sprites_instrument_xy + 15;
        sta $d00f;
        lda _sprites_instrument_idx + 7;
        sta $efff;
        lda #$80;                   // 1 << kSpriteIdxFuel
        sta $d010;
        sta $d015;
        bne L_pb_done;

    L_pb_center:
        ldx #14;
    L_pb_pos:
        lda _sprites_instrument_xy + 1, x;
        sta $d001, x;
        lda _sprites_instrument_xy, x;
        sta $d000, x;
        dex;
        dex;
        bne L_pb_pos;

        ldx #7;
    L_pb_pc:
        lda _sprites_instrument_idx, x;
        sta $eff8, x;
        lda #1;                     // kColorInstrument
        sta $d027, x;
        dex;
        bne L_pb_pc;

        lda #$40;                   // 1 << kSpriteIdxThrottle
        sta $d010;
        lda #$ff;
        sta $d015;
    L_pb_done:
  }
  // clang-format on
}
#endif

#pragma compile("sprites_asm.cc")
#endif

#endif
