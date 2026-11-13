#include "render.h"

#include <stdint.h>
#include <string.h>

#include "benchmark.h"
#include "chardefs.h"
#include "color.h"
#include "fmath.h"
#include "mem.h"
#include "roll.h"
#include "vec.h"

int16_t render_cx_pixels;
int16_t render_cy_pixels;
int16_t render_px_pixels;
int16_t render_py_pixels;
int8_t render_cx_chars;
int8_t render_cy_chars;
bool render_alt_box;
int8_t render_alt_shift_x;
int8_t render_alt_shift_y;

// Skip so many lines, which will be filled by tiles.
// PERF: 4 -> cycles: -1000. Bytes: +260 with the old per-case fill loops; the
// span fill handles any value with the same code.
static const int8_t kSkipLines = 4;


// Plain 16x16 -> low 16 product, routed through the quarter-square routine
// instead of oscar64's mul16. vec_fastmul8p8 returns trunc(a * b / 256), so
// pre-shifting b by 8 recovers a * b exactly - the remainder is zero, so the
// truncation never rounds.
//
// b is int8_t, so b << 8 always fits and the whole int8_t range is usable -
// including -128. Callers must therefore pass the raw value and negate the
// product afterwards rather than negating the operand first: 0 - (-128) is
// 128, which does not survive the parameter, and the sign flip that follows
// is silent. That mistake is what put diagonal bands through the sky fill.
//
// The routine also builds the product from the magnitudes and applies the sign
// last, so it wraps sign-magnitude where mul16 wrapped two's complement. Both
// of these are checked rather than argued: each call site below was compared
// exhaustively against the expression it replaced, over the operand's full
// range, overflow included.
static inline int16_t _render_mul(int16_t a, int8_t b) {
  return vec_fastmul8p8(a, (int16_t)b << 8);
}

// _fill_sky_ground_* negates the _mul result in place of the subtraction the
// horizon term used to spell out. (oscar64 only takes static_assert at file
// scope, so it lives here rather than next to the code it guards.)
static_assert(kViewportStartY == 0, "the horizon term assumes it");

// Finds a point (px, py) on the horizon line that is shifted by an
// integer number of major axis steps to be close to the viewport center.
//
// The two axes are the same computation with x and y swapped, so they are
// swapped into major/minor locals once rather than spelled out twice.
static void _pull_to_center() {
  int16_t c_maj, c_min, t;
  int8_t d_maj, d_min;
  if (roll_x_is_major) {
    c_maj = render_cx_pixels;
    c_min = render_cy_pixels;
    t = 160;
    d_maj = roll_dx;
    d_min = roll_dy;
  } else {
    c_maj = render_cy_pixels;
    c_min = render_cx_pixels;
    t = 64;
    d_maj = roll_dy;
    d_min = roll_dx;
  }
  t += (roll_shift_2chars ? 64 : 32) - c_maj;
  int16_t d = t >> 6;
  if (roll_shift_2chars) {
    d >>= 1;
  }
  // _render_lshift(d), without the second shift chain.
  int16_t p_maj = c_maj + (t & (roll_shift_2chars ? -128 : -64));
  if (d_maj < 0) {
    d = -d;
  }
  int16_t p_min = c_min + (_render_mul(d, d_min) << 3);
#ifdef __OSCAR64__
  // Store directly into ZP best_px/best_py for render_snap_center_chars().
  if (roll_x_is_major) {
    vec_proj_mul_a = p_maj;
    vec_proj_mul_b = p_min;
  } else {
    vec_proj_mul_a = p_min;
    vec_proj_mul_b = p_maj;
  }
#else
  if (roll_x_is_major) {
    render_px_pixels = p_maj;
    render_py_pixels = p_min;
  } else {
    render_px_pixels = p_min;
    render_py_pixels = p_maj;
  }
#endif
}

static inline void _render_set_alt_shift() {
  if (roll_x_is_major) {
    render_alt_shift_x = 0;
    render_alt_shift_y = 4;
  } else {
    render_alt_shift_x = 4;
    render_alt_shift_y = 0;
  }
}

// Distance to line from nearest character.
// Optimized to avoid large multiplications by using (px, py) as anchor.
// dist = |roll_dy * (px - mx*8) - roll_dx * (py - my*8)|
// PERF: roll_get_dist() -> cycles: -120 bytes: -100
static uint16_t _unused_get_dist(int8_t px, int8_t py) {
  int8_t ex = px - ((px + 4) & 0xF8);
  int8_t ey = py - ((py + 4) & 0xF8);
  int16_t dist = (int16_t)roll_dy * ex - (int16_t)roll_dx * ey;
  if (dist < 0) {
    dist = -dist;
  }
  return dist;
}

void render_snap_center_chars() {
  bm_view_start();
  _pull_to_center();

#ifdef __OSCAR64__
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
#else
  int16_t px = render_px_pixels;
  int16_t py = render_py_pixels;
  int16_t best_px = px;
  int16_t best_py = py;
  render_alt_box = false;

  if (roll_period == 1) {
    _render_set_alt_shift();

    // 1. Main Lattice vs 2. Alt Lattice
    uint8_t min_dist = roll_get_dist(px, py);
    int16_t ax = px - (uint8_t)render_alt_shift_x;
    int16_t ay = py - (uint8_t)render_alt_shift_y;
    if (roll_get_dist(ax, ay) < min_dist) {
      best_px = ax;
      best_py = ay;
      render_alt_box = true;
    }
  } else {
    uint8_t min_dist = 0xff;
    for (uint8_t i = 0; i < roll_period; ++i) {
      uint8_t dist = roll_get_dist(px, py);
      if (dist < min_dist) {
        min_dist = dist;
        best_px = px;
        best_py = py;
      }
      px += roll_dx;
      py += roll_dy;
    }
  }
  render_cx_chars = (int8_t)((best_px + 4) >> 3);
  render_cy_chars = (int8_t)((best_py + 4) >> 3);
#endif
  bm_view_end(670, "SNP:");
}

// How many rows of the colour buffer, from the top, hold nothing but the sky
// colour, so that a row of sky among them needs only its characters: 40
// stores a row, a frame, that changed nothing. Everything else that colours
// the buffer is accounted for. The horizon's boxes lower the count to their
// top row themselves (box.cc). Polygons and dots colour only cells that hold
// ground characters, and a row counted here was all sky when it was filled
// and has had no box over it since. The message's cells on row 0 are put
// back by msg_restore_color() before every fill.
#pragma optimize(push, nooutline)

uint8_t render_sky_rows;
// This frame's rows of sky so far, counted from the top without a gap: the
// next frame's render_sky_rows.
static uint8_t _sky_rows;

static_assert(kViewportStartX == 0 && kViewportStartY == 0,
              "viewport row y is screen row y, from its first column");

// 46-byte self-modifying STA abs,x screen-RAM sled ($E800 main, toggled to $EC00
// alt in place via EOR #$04 on each high byte). Patching `dex; bpl rel; rts` at
// end_row runs X = 39..0 once across [start_row, end_row). ch == 1 is a no-op.
static __noinline void _flush_span(uint8_t ch, uint8_t start_row,
                                   uint8_t end_row) {
  if (end_row > kViewportHeight) {
    end_row = kViewportHeight;
  }
  if ((ch & 1) || start_row >= end_row) {
    return;
  }
#ifdef __OSCAR64__
  uint8_t sky_col = COLOR_SKY8;
  // clang-format off
  __asm {
        lda ch;
        bne L_scr;
        lda start_row;
        cmp _sky_rows;
        bne L_srows_done;
        ldx end_row;
        stx _sky_rows;
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
#else
  if (ch == kCharSolid11) {
    if (start_row == _sky_rows) {
      _sky_rows = end_row;
    }
    for (uint8_t y = start_row > render_sky_rows ? start_row : render_sky_rows;
         y < end_row; ++y) {
      uint8_t *dst = mem_color_row_ptrs[y];
      for (uint8_t x = 0; x < 40; ++x) {
        dst[x] = COLOR_SKY8;
      }
    }
  }
  for (uint8_t y = start_row; y < end_row; ++y) {
    uint8_t *dst = mem_screen_row_ptrs[y];
    for (uint8_t x = 0; x < 40; ++x) {
      dst[x] = ch;
    }
  }
#endif
}
#pragma optimize(pop)

static inline uint8_t _clamp_pos(int8_t row) {
  return row < 0 ? 0 : (uint8_t)row;
}

// One 12.4 position past the right edge of the viewport.
static const int16_t kFull = kViewportWidth << 4;

// Every row is a left span [0, l) and a right span [r, kViewportWidth), sky on
// one side and ground on the other, and the cells between them are left for
// the tiles. For a tilted horizon the spans come from two 12.4 positions per
// row, lo and hi = lo + w, where w is the kSkipLines band and lo walks by
// roll_dx_div_dy a row:
//
//   hi < 0x10    the whole row is the right side's
//   lo >= kFull  the whole row is the left side's
//   otherwise    l = lo >> 4 (0 below 0x10), r = hi >> 4 (the edge at kFull)
//
// The left span is always filled before the right one, the order the old
// per-case loops used, so even a wrapped lo/hi pair, where the spans overlap,
// writes the same cells.
void render_fill_sky_ground() {
  bm_view_start();
  _sky_rows = 0;

  if (roll_dy == 0) {
    // Level: whole rows only. Rows above e are the left side's, rows from
    // e + kSkipLines on the right side's, and the ones between are skipped.
    // The sky side's rows stop kSkipLines short of the horizon row, the
    // ground side's run up to it.
    bool sky_left = roll_dx > 0;
    int8_t e = render_cy_chars;
    uint8_t top = _clamp_pos(e);
    uint8_t bot;
    if (sky_left) {
      bot = top;
      top = _clamp_pos((int8_t)(bot - kSkipLines));
    } else {
      bot = _clamp_pos((int8_t)(e + kSkipLines));
      if (bot < top) {
        bot = top;
      }
    }
    uint8_t ch0 = sky_left ? kCharSolid11 : kCharSolidGround;
    _flush_span(ch0, 0, top);
    _flush_span((uint8_t)(ch0 ^ kCharSolidGround), bot, kViewportHeight);
  } else {
    bool sky_left = roll_dy < 0;
    // 12.4 fixpoint representation of the divider x between sky and ground.
    //
    // render_cy_chars is a truncating int8_t cast of a horizon that can sit far
    // off screen, so it really does reach -128 and the old product really did
    // overflow there. Negating the result rather than the operand keeps both
    // cases identical to the mul16 version; see the note on _mul.
    int16_t lo = -_render_mul(roll_dx_div_dy, render_cy_chars) +
                 ((render_cx_chars - kViewportStartX) << 4);
    if (roll_dx_div_dy > 0) {
      // Hack to make sure the boxes always cover the horizon.
      lo += 8;
    }
    if (render_alt_box) {
      if (render_alt_shift_x) {
        lo += 8;
      }
      if (render_alt_shift_y) {
        lo -= (roll_dx_div_dy >> 1);
      }
    }
    int16_t w = _abs16(roll_dx_div_dy) * kSkipLines;
    // The divider is the ground side's edge of the band: hi when the sky is
    // on the left, lo when it is on the right.
    if (sky_left) {
      lo -= w;
    }

    // Batch consecutive full rows so each top/bottom run uses one sled call.
    uint8_t run_ch = 1;
    uint8_t run_start = 0;
    for (uint8_t y = 0; y < kViewportHeight; ++y) {
      int16_t hi = lo + w;
      if (hi < 0x10 || lo >= kFull) {
        uint8_t ch =
            ((hi < 0x10) != sky_left) ? kCharSolid11 : kCharSolidGround;
        if (ch != run_ch) {
          _flush_span(run_ch, run_start, y);
          run_ch = ch;
          run_start = y;
        }
      } else {
        if (run_ch != 1) {
          _flush_span(run_ch, run_start, y);
          run_ch = 1;
        }
        // The row's spans, in the screen and in the colour buffer.
        uint8_t *const row_dst = mem_screen_row_ptrs[y];
        uint8_t *const row_color = mem_color_row_ptrs[y];
        if (lo >= 0x10) {
          int8_t i = (int8_t)((uint16_t)lo >> 4) - 1;
          if (sky_left) {
            do {
              row_dst[i] = kCharSolid11;
              row_color[i] = COLOR_SKY8;
            } while (--i >= 0);
          } else {
            do {
              row_dst[i] = kCharSolidGround;
            } while (--i >= 0);
          }
        }
        if (hi < kFull) {
          uint8_t i = (uint8_t)((uint16_t)hi >> 4);
          if (!sky_left) {
            do {
              row_dst[i] = kCharSolid11;
              row_color[i] = COLOR_SKY8;
            } while (++i < kViewportWidth);
          } else {
            do {
              row_dst[i] = kCharSolidGround;
            } while (++i < kViewportWidth);
          }
        }
      }
      lo += roll_dx_div_dy;
    }
    _flush_span(run_ch, run_start, kViewportHeight);
  }
  render_sky_rows = _sky_rows;

  bm_view_end(710, "BGR:");
}
