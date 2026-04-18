#include "roll.h"

// Helper for assembly dispatch
void *roll_mul_dx;
void *roll_mul_dy;

void _call_roll_mul_dx(void) {
  __asm { jmp(roll_mul_dx); }
}
void _call_roll_mul_dy(void) {
  __asm { jmp(roll_mul_dy); }
}

static uint16_t roll_get_dist_res;
static uint8_t roll_mul_tmp_val;

// Without this oscar likes to collapse all these funcs into one.
#pragma optimize(noasm)

void _roll_mul_plus16(void) {
  __asm {
    asl;
    asl;
    asl;
    asl;
    rts
  }
}
void _roll_mul_plus8(void) {
  __asm {
    asl;
    asl;
    asl;
    rts;
  }
}
void _roll_mul_plus6(void) {
  __asm {
    sta roll_mul_tmp_val;
    asl;
    clc;
    adc roll_mul_tmp_val;
    asl;
    rts; // 3*2
  }
}
void _roll_mul_plus10(void) {
  __asm {
    asl;
    sta roll_mul_tmp_val;
    asl;
    asl;
    clc;
    adc roll_mul_tmp_val;
    rts; // 2+8
  }
}
void _roll_mul_plus5(void) {
  __asm {
    sta roll_mul_tmp_val;
    asl;
    asl;
    clc;
    adc roll_mul_tmp_val;
    rts; // 4+1
  }
}
void _roll_mul_plus4(void) {
  __asm {
    asl;
    asl;
    rts
  }
}
void _roll_mul_plus3(void) {
  __asm {
    sta roll_mul_tmp_val;
    asl;
    clc;
    adc roll_mul_tmp_val;
    rts;
  }
}
void _roll_mul_plus2(void) {
  __asm {
    asl;
    rts;
  }
}
void _roll_mul_plus1(void) {
  __asm { rts; }
}
void _roll_mul_zero(void) {
  __asm {
    lda #0;
    rts;
  }
}
void _roll_mul_minus1(void) {
  __asm {
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus2(void) {
  __asm {
    asl;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus3(void) {
  __asm {
    sta roll_mul_tmp_val;
    asl;
    clc;
    adc roll_mul_tmp_val;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus4(void) {
  __asm {
    asl;
    asl;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus5(void) {
  __asm {
    sta roll_mul_tmp_val;
    asl;
    asl;
    clc;
    adc roll_mul_tmp_val;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus6(void) {
  __asm {
    sta roll_mul_tmp_val;
    asl;
    clc;
    adc roll_mul_tmp_val;
    asl;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus8(void) {
  __asm {
    asl;
    asl;
    asl;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus10(void) {
  __asm {
    jsr _roll_mul_plus10;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}
void _roll_mul_minus16(void) {
  __asm {
    asl;
    asl;
    asl;
    asl;
    eor #$ff;
    clc;
    adc #1;
    rts;
  }
}

void (*roll_mul_funcs[])(void) = {
    _roll_mul_minus16, _roll_mul_zero,   _roll_mul_zero,    _roll_mul_zero,
    _roll_mul_zero,    _roll_mul_zero,   _roll_mul_minus10, _roll_mul_zero,
    _roll_mul_minus8,  _roll_mul_zero,   _roll_mul_minus6,  _roll_mul_minus5,
    _roll_mul_minus4,  _roll_mul_minus3, _roll_mul_minus2,  _roll_mul_minus1,
    _roll_mul_zero,    _roll_mul_plus1,  _roll_mul_plus2,   _roll_mul_plus3,
    _roll_mul_plus4,   _roll_mul_plus5,  _roll_mul_plus6,   _roll_mul_zero,
    _roll_mul_plus8,   _roll_mul_zero,   _roll_mul_plus10,  _roll_mul_zero,
    _roll_mul_zero,    _roll_mul_zero,   _roll_mul_zero,    _roll_mul_zero,
    _roll_mul_plus16,
};

uint16_t roll_get_dist(int8_t px, int8_t py) {
  __asm {
    lda px;
    clc;
    adc #4;
    and #7;
    sec;
    sbc #4;
    // A = ex
    jsr _call_roll_mul_dy;
    tax; // X = dy_res

    lda py;
    clc;
    adc #4;
    and #7;
    sec;
    sbc #4;
    // A = ey
    jsr _call_roll_mul_dx;
    // A = dx_res

    // Now X = dy_res, A = dx_res
    sta roll_mul_tmp_val; // tmp = dx_res
    txa; // A = dy_res
    sec;
    sbc roll_mul_tmp_val;
    sta roll_get_dist_res; // low byte

    // High byte calc
    txa;                 // dy_res
    bmi L_neg_dy_roll;
    lda #0;
    beq L_ext_dy_roll;
  L_neg_dy_roll:
    lda #$FF;
  L_ext_dy_roll:
    // A is high byte of dy_res
    bit roll_mul_tmp_val; // test dx_res sign
    bmi L_neg_dx_roll;
    sbc #0;
    jmp L_sub_done_roll;
  L_neg_dx_roll:
    sbc #$FF;
  L_sub_done_roll:
    sta roll_get_dist_res + 1;

    // Abs value
    lda roll_get_dist_res + 1;
    bpl L_abs_done_roll;
    lda #0;
    sec;
    sbc roll_get_dist_res;
    sta roll_get_dist_res;
    lda #0;
    sbc roll_get_dist_res + 1;
    sta roll_get_dist_res + 1;
  L_abs_done_roll:
  }
  return roll_get_dist_res;
}
