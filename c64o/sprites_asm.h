#ifndef SPRITES_ASM_H
#define SPRITES_ASM_H

#include <stdint.h>

#include "bool.h"
#include "spritedef.h"
#include "sprites.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "mem.h"
#include "msg.h"
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

#pragma compile("sprites_asm.cc")
#endif

#endif
