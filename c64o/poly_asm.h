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

// The assembly half of poly.cc: Pass 134's 3D frustum early rejection
// (_poly_frustum_all_out) and Pass 137's quad-cell scanline pass
// (_poly_scan_lines2). Both are static inline under __OSCAR64__ so poly.cc
// inlines them directly with zero cross-TU call overhead; poly.cc holds the
// pure C equivalents (#ifndef __OSCAR64__) for the host and host tests.

// Working buffers defined in poly.cc (in scratch/bss2).
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

// Returns true when every vertex in _poly_clip3_buf[0 .. end3) (byte offset)
// lies past the same frustum plane and the polygon can be rejected before
// projection.
static inline bool _poly_frustum_all_out(uint8_t end3) {
  // 3D frustum early-reject before vec_project_nocull(): every _poly_clip3_buf
  // vertex has x >= 8. Screen x in [0, 79] requires vec_sx in [-160, 162]
  // (|y|/x <= ~0.633 < 1) and screen y in [0, 27] requires vec_sy in [-56, 58]
  // (|z|/x <= ~0.227 < 0.25). Any polygon with all vertices past the same edge
  // cannot intersect the viewport.
  // clang-format off
  vec_v.x = (int16_t)end3; // low byte at vec_v+0 holds end3
  __asm {
        lda #$0f;
        sta vec_v+2;        // vec_v+2 = out3
        ldy #$00;
    L_out3_loop:
        lda _poly_clip3_buf+1, y;
        lsr;
        sta vec_sx+1;
        lda _poly_clip3_buf+0, y;
        ror;
        lsr vec_sx+1;
        ror;
        sta vec_sx+0;       // vec_sx = zx = ux >> 2

        ldx #$00;
        lda _poly_clip3_buf+3, y;
        bmi L_y_neg;
        sec;
        lda _poly_clip3_buf+0, y;
        sbc _poly_clip3_buf+2, y;
        lda _poly_clip3_buf+1, y;
        sbc _poly_clip3_buf+3, y;
        bcs L_chk_z;
        ldx #$01;
        bne L_chk_z;
    L_y_neg:
        clc;
        lda _poly_clip3_buf+0, y;
        adc _poly_clip3_buf+2, y;
        lda _poly_clip3_buf+1, y;
        adc _poly_clip3_buf+3, y;
        bcs L_chk_z;
        ldx #$02;
    L_chk_z:
        lda _poly_clip3_buf+5, y;
        bmi L_z_neg;
        sec;
        lda vec_sx+0;
        sbc _poly_clip3_buf+4, y;
        lda vec_sx+1;
        sbc _poly_clip3_buf+5, y;
        bcs L_next_v;
        txa;
        ora #$04;
        tax;
        bne L_next_v;
    L_z_neg:
        clc;
        lda vec_sx+0;
        adc _poly_clip3_buf+4, y;
        lda vec_sx+1;
        adc _poly_clip3_buf+5, y;
        bcs L_next_v;
        txa;
        ora #$08;
        tax;
    L_next_v:
        txa;
        and vec_v+2;
        sta vec_v+2;
        beq L_out3_done;
        tya;
        clc;
        adc #$06;
        tay;
        cmp vec_v+0;
        bne L_out3_loop;
    L_out3_done:
  }
  // clang-format on
  return (uint8_t)vec_v.y != 0;
}

// Rasterises polygon scanlines in character rows [py_start, py_end] (inclusive)
// from _poly_min_x/_poly_max_x.
static inline void _poly_scan_lines2(uint8_t fill_char_start_idx,
                                     uint8_t color, uint8_t py_start,
                                     uint8_t py_end) {
  for (uint8_t py = py_start; py <= py_end; ++py) {
    uint8_t sy = py << 1;
    uint8_t t_min = _poly_min_x[sy];
    uint8_t b_min = _poly_min_x[sy + 1];
    uint8_t min_x = _min8(t_min, b_min);
    if (min_x == kScreenWidth * 2) {
      continue;
    }
    uint8_t t_max = _poly_max_x[sy];
    uint8_t b_max = _poly_max_x[sy + 1];

    uint8_t *row = mem_screen_row_ptrs[py];
    uint8_t *color_row = mem_color_row_ptrs[py];

    // 1. Find the absolute left and right bounds (in pixels) for this row.
    uint8_t min_px = min_x >> 1;
    uint8_t max_px = _max8(t_max, b_max) >> 1;

    // 2. Calculate the "Solid Core" [overlap_start, overlap_stop) where both
    // top and bottom are fully covered.
    uint8_t overlap_start = (uint8_t)(_max8(t_min, b_min) + 1) >> 1;
    uint8_t overlap_stop = (uint8_t)(_min8(t_max, b_max) + 1) >> 1;

    // 3. Fast path: Memset the Solid Core (No read-modify-write!)
    if (overlap_stop > overlap_start) {
      uint8_t cnt = overlap_stop - overlap_start;
      if (color) {
        _poly_set_color_line(row + overlap_start, fill_char_start_idx | 0xF,
                             color_row + overlap_start, color, cnt);
      } else {
        _poly_set_line(row + overlap_start, fill_char_start_idx | 0xF, cnt);
      }
    } else {
      overlap_start = kViewportWidth;
    }

    // 4. Process the fringe (every pixel outside the solid core).
    uint8_t max_px_p1 = max_px + 1;
    // clang-format off
    __asm {
        ldy min_px;
    L_fr_loop:
        cpy overlap_start;
        bne L_fr_chk;
        ldy overlap_stop;
        cpy max_px_p1;
        bcs L_fr_end;
    L_fr_chk:
        lda (row), y;
        bpl L_fr_next;
        tya;
        asl;
        ldx #0;

        // Top sub-row [t_min, t_max] -> bits 0 (sx_left) and 1 (sx_left+1).
        cmp t_max;
        beq L_t_eq;
        bcs L_t_done;
        cmp t_min;
        bcc L_t_chk_r;
        ldx #3;
        bne L_t_done;
    L_t_eq:
        cmp t_min;
        bcc L_t_done;
        ldx #1;
        bcs L_t_done;
    L_t_chk_r:
        ora #1;
        cmp t_min;
        bcc L_t_restore;
        ldx #2;
    L_t_restore:
        and #$fe;
    L_t_done:

        // Bottom sub-row [b_min, b_max] -> bits 2 and 3.
        cmp b_max;
        beq L_b_eq;
        bcs L_b_chk_zero;
        cmp b_min;
        bcc L_b_chk_r;
        txa;
        ora #12;
        bne L_draw;
    L_b_eq:
        cmp b_min;
        bcc L_b_chk_zero;
        txa;
        ora #4;
        bne L_draw;
    L_b_chk_r:
        ora #1;
        cmp b_min;
        bcc L_b_chk_zero;
        txa;
        ora #8;
        bne L_draw;
    L_b_chk_zero:
        txa;
        beq L_fr_next;
    L_draw:
        clc;
        adc fill_char_start_idx;
        sta (row), y;
        lda color;
        beq L_fr_next;
        sta (color_row), y;
    L_fr_next:
        iny;
        cpy max_px_p1;
        bcc L_fr_loop;
    L_fr_end:
    }
    // clang-format on
  }
}
#else
bool _poly_frustum_all_out(uint8_t end3);
void _poly_scan_lines2(uint8_t fill_char_start_idx, uint8_t color,
                       uint8_t py_start, uint8_t py_end);
#endif // __OSCAR64__

#endif
