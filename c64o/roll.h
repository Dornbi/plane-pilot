#ifndef ROLL_H
#define ROLL_H

#include <stdint.h>

#include "bool.h"

static const uint8_t kRollMax = 60;

extern uint8_t roll_angle;
extern int8_t roll_dx;
extern int8_t roll_dy;
extern int16_t roll_dx_div_dy;
extern uint8_t roll_period;
extern bool roll_x_is_major;
extern bool roll_shift_2chars;

void roll_update_state();

extern void *roll_mul_dx;
extern void *roll_mul_dy;
extern void (*roll_mul_funcs[])(void);

// Distance to line from nearest character.
// Optimized to avoid large multiplications by using (px, py) as anchor.
// dist = |roll_dy * (px - mx*8) - roll_dx * (py - my*8)|
uint16_t roll_get_dist(int8_t px, int8_t py);

#pragma compile("roll.cc")
#pragma compile("roll_asm.cc")

#endif