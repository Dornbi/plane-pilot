#include "roll.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

static uint8_t roll_get_dist_res;
static uint8_t roll_mul_tmp_val;

// Both products are looked up rather than multiplied: roll_update_state()
// fills roll_ex_dy and roll_ey_dx for the frame's (dx, dy). Each is within
// +-64, so their difference is within +-128 and its low byte, negated when
// bit 7 is set, is the distance: 0x80 is 128 either way.
uint8_t roll_get_dist(int8_t px, int8_t py) {
  __asm {
    lda py;
    clc;
    adc #4;
    and #7;
    tax;
    lda roll_ey_dx, x;
    sta roll_mul_tmp_val; // tmp = dx_res

    lda px;
    clc;
    adc #4;
    and #7;
    tax;
    lda roll_ex_dy, x; // A = dy_res
    sec;
    sbc roll_mul_tmp_val;
    bpl L_abs_done_roll;
    eor #$ff;
    clc;
    adc #1;
  L_abs_done_roll:
    sta roll_get_dist_res;
  }
  return roll_get_dist_res;
}
#pragma optimize(pop)
