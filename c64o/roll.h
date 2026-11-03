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

// The two products roll_get_dist() needs, for e = 0..7: roll_dy * (e - 4)
// and roll_dx * (e - 4), the offset from the nearest char being (p + 4) & 7,
// less 4. At most 4 * 16 either way, so each fits an int8_t.
extern int8_t roll_ex_dy[8];
extern int8_t roll_ey_dx[8];

// Distance to line from nearest character.
// Optimized to avoid large multiplications by using (px, py) as anchor.
// dist = |roll_dy * (px - mx*8) - roll_dx * (py - my*8)|, at most 128.
uint8_t roll_get_dist(int8_t px, int8_t py);

#pragma compile("roll.cc")
#pragma compile("roll_asm.cc")

#endif