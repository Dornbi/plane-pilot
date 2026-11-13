#ifndef WORLD_ASM_H
#define WORLD_ASM_H

#include <stdint.h>

#include "vec.h"
#ifndef __TARGET_TEST__
#include "gfx.h"
#include "mem.h"
#else
static const uint8_t kGfxDotGround = 0x08;
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
#endif

// The assembly half of world.cc: Pass 138's inner Mitchell terrain-dot loop
// (_world_draw_box_points). Marked static inline under __OSCAR64__ so
// world_render_grid() inlines it with zero cross-TU call overhead; world.cc
// holds the pure C version (#ifndef __OSCAR64__) for host builds and tests.

extern __striped int16_t _world_mitch_x[16];
extern __striped int16_t _world_mitch_y[16];
extern __striped int16_t _world_mitch_z[16];
extern vec3_t _world_vec_v;

#ifdef __OSCAR64__
#ifndef __TARGET_TEST__
static_assert(kViewportHeightPixels == 112 && kViewportWidthPixels == 320 &&
                  kViewportStartX == 0 && kScreenHeight == 25 &&
                  kViewportHeight == 14 && kGfxDotGround == 8 &&
                  kGfxGroundPoints == 224 && kGfxColorPoints == 240,
              "_world_draw_box_points asm constants mismatch");
#endif

// Projects and plots `num_points` Mitchell dots starting at `start_idx & 0x0F`
// around `_world_vec_v`. `color` is a palette index (< 8) or kGfxDotGround.
static inline void _world_draw_box_points(uint8_t start_idx,
                                          uint8_t num_points,
                                          uint8_t color) {
  uint8_t idx = start_idx & 0x0F;
  uint8_t cnt = num_points;
  uint8_t col = (color == kGfxDotGround) ? 0 : (uint8_t)(0x08 | color);
  // clang-format off
  __asm {
    ldx idx
  L_wd_loop:
    // Forward cone check: vec_v.x = _world_vec_v.x + _world_mitch_x[idx] >= 9.
    clc
    lda _world_vec_v + 0
    adc _world_mitch_x, x
    sta vec_v + 0
    lda _world_vec_v + 1
    adc _world_mitch_x + 16, x
    bmi L_wd_fail_mid
    sta vec_v + 1
    bne L_wd_x_ok
    lda vec_v + 0
    cmp #$09
    bcc L_wd_fail_mid
  L_wd_x_ok:

    // Lateral cone check: |y| <= x; store |y| in vec_proj_mul_a, sign in vec_v + 3.
    clc
    lda _world_vec_v + 2
    adc _world_mitch_y, x
    sta vec_proj_mul_a + 0
    lda _world_vec_v + 3
    adc _world_mitch_y + 16, x
    sta vec_v + 3
    bpl L_wd_y_pos
    sec
    lda #$00
    sbc vec_proj_mul_a + 0
    sta vec_proj_mul_a + 0
    lda #$00
    sbc vec_v + 3
  L_wd_y_pos:
    sta vec_proj_mul_a + 1
    cmp vec_v + 1
    bcc L_wd_y_ok
    bne L_wd_fail_mid
    lda vec_v + 0
    cmp vec_proj_mul_a + 0
    bcs L_wd_y_ok
  L_wd_fail_mid:
    jmp L_wd_next
  L_wd_y_ok:

    // Vertical cone check: |z| <= (x >> 2); store |z| in vec_proj_mul_b, sign in vec_v + 5.
    clc
    lda _world_vec_v + 4
    adc _world_mitch_z, x
    sta vec_proj_mul_b + 0
    lda _world_vec_v + 5
    adc _world_mitch_z + 16, x
    sta vec_v + 5
    bpl L_wd_z_pos
    sec
    lda #$00
    sbc vec_proj_mul_b + 0
    sta vec_proj_mul_b + 0
    lda #$00
    sbc vec_v + 5
  L_wd_z_pos:
    sta vec_proj_mul_b + 1
    lda vec_v + 1
    lsr
    sta vec_proj_recip
    lda vec_v + 0
    ror
    lsr vec_proj_recip
    ror
    sta vec_proj_tmp1
    lda vec_proj_recip
    cmp vec_proj_mul_b + 1
    bcc L_wd_fail_mid
    bne L_wd_z_ok
    lda vec_proj_tmp1
    cmp vec_proj_mul_b + 0
    bcc L_wd_fail_mid
  L_wd_z_ok:

    // Tightened lateral check for 320px viewport: |y| <= x - (x >> 2) (C=1 here).
    lda vec_v + 0
    sbc vec_proj_tmp1
    tay
    lda vec_v + 1
    sbc vec_proj_recip
    cmp vec_proj_mul_a + 1
    bcc L_wd_fail_mid
    bne L_wd_proj
    cpy vec_proj_mul_a + 0
    bcc L_wd_fail_mid
  L_wd_proj:

    // Normalize x into [128, 255] alongside |y| and |z|, then look up reciprocal.
    lda vec_v + 0
    sta vec_proj_tmp1
    lda vec_v + 1
    sta vec_proj_recip
    beq L_wd_shl_chk
  L_wd_shr:
    lsr vec_proj_recip
    ror vec_proj_tmp1
    lsr vec_proj_mul_a + 1
    ror vec_proj_mul_a + 0
    lsr vec_proj_mul_b + 1
    ror vec_proj_mul_b + 0
    lda vec_proj_recip
    bne L_wd_shr
    beq L_wd_lut
  L_wd_shl_chk:
    lda vec_proj_tmp1
    bmi L_wd_lut
  L_wd_shl:
    asl vec_proj_mul_a + 0
    asl vec_proj_mul_b + 0
    asl vec_proj_tmp1
    bpl L_wd_shl
  L_wd_lut:
    ldx vec_proj_tmp1
    lda vec_recip_lut - 128, x
    sta vec_proj_recip

    // Project horizontal coordinate -> vec_v+2 = hpx_lo = (160 - vec_sx) >> 1.
    // Note vec_v+0..3 are dead after vec_project_mul reads vec_v+3 for sign.
    lda vec_v + 3
    sta vec_mul8_lo
    lda vec_proj_mul_a + 0
    jsr vec_project_mul
    sta vec_proj_tmp1
    lda #$a0
    sec
    sbc vec_proj_tmp1
    sta vec_v + 2
    stx vec_proj_tmp1
    lda #$00
    sbc vec_proj_tmp1
    lsr
    ror vec_v + 2
    lda vec_v + 2
    cmp #$a0
    bcs L_wd_next

    // Project vertical coordinate -> vec_v+4 = py_lo = 56 - vec_sy_lo.
    lda vec_v + 5
    sta vec_mul8_lo
    lda vec_proj_mul_b + 0
    jsr vec_project_mul
    eor #$ff
    clc
    adc #$39
    cmp #$70
    bcs L_wd_next
    sta vec_v + 4

    // Plot into empty solid-ground cell only; vec_v+0/1 serves as scr_p.
    lsr
    lsr
    lsr
    tax
    lda mem_screen_row_ptrs, x
    sta vec_v + 0
    lda mem_screen_row_ptrs + 25, x
    sta vec_v + 1
    lda vec_v + 2
    lsr
    lsr
    tay
    lda (vec_v + 0), y
    cmp #$80
    bne L_wd_next
    lda vec_v + 2
    and #$03
    sta vec_proj_tmp1
    lda vec_v + 4
    and #$06
    asl
    ora vec_proj_tmp1
    sta vec_proj_tmp1
    lda col
    bne L_wd_color
    lda vec_proj_tmp1
    ora #$e0
    sta (vec_v + 0), y
    bne L_wd_next
  L_wd_color:
    lda vec_proj_tmp1
    ora #$f0
    sta (vec_v + 0), y
    lda mem_color_row_ptrs, x
    sta vec_v + 0
    lda mem_color_row_ptrs + 14, x
    sta vec_v + 1
    lda col
    sta (vec_v + 0), y
  L_wd_next:
    dec cnt
    beq L_wd_done
    ldx idx
    inx
    txa
    and #$0f
    sta idx
    tax
    jmp L_wd_loop
  L_wd_done:
  }
  // clang-format on
}
#else
void _world_draw_box_points(uint8_t start_idx, uint8_t num_points,
                            uint8_t color);
#endif // __OSCAR64__

#endif
