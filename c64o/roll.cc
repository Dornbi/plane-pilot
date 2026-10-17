#include "roll.h"

#include <stdint.h>

// Only the first half-turn is stored: angle a + kRollMax / 2 is angle a with
// (dx, dy) negated, which leaves dx / dy and the period unchanged.
#define _VALUES_FOR_ANGLES                                                     \
  {                                                                            \
      _VALUE0(8, 0),    _VALUE(16, -1),  _VALUE(8, -1),   _VALUE(8, -2),       \
      _VALUE(8, -3),    _VALUE(8, -4),   _VALUE(8, -5),   _VALUE(8, -6),       \
      _VALUE(8, -8),    _VALUE(6, -8),   _VALUE(10, -16), _VALUE(4, -8),       \
      _VALUE(6, -16),   _VALUE(2, -8),   _VALUE(2, -16),  _VALUE(0, -8),       \
      _VALUE(-2, -16),  _VALUE(-2, -8),  _VALUE(-6, -16), _VALUE(-4, -8),      \
      _VALUE(-10, -16), _VALUE(-6, -8),  _VALUE(-8, -8),  _VALUE(-8, -6),      \
      _VALUE(-8, -5),   _VALUE(-8, -4),  _VALUE(-8, -3),  _VALUE(-8, -2),      \
      _VALUE(-8, -1),   _VALUE(-16, -1),                                       \
  }

#define _VALUE(x, y) (x)
#define _VALUE0(x, y) _VALUE(x, y)
const int8_t kRollDx[kRollMax / 2] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

#define _VALUE(x, y) (y)
#define _VALUE0(x, y) _VALUE(x, y)
const int8_t kRollDy[kRollMax / 2] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

// Could not get this to work with a macro. Should be something like:
#define _VALUE(x, y) (x) * 16 / (y)
#define _VALUE0(x, y) 0
__striped const int kRollDxDivDy[kRollMax / 2] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

uint8_t roll_angle;
int8_t roll_dx;
int8_t roll_dy;
int16_t roll_dx_div_dy;
uint8_t roll_period;
bool roll_x_is_major;
bool roll_shift_2chars;

static void *_get_mul_func(int8_t x) { return (void *)roll_mul_funcs[x + 16]; }

// Note there is a discrepancy between this and the code in roll_angle.py:
// - For rendering (here), we always cap roll_period at 8
// - To find boxes (in roll_angle.py) we need the real period to find boxes.
void roll_update_state() {
  uint8_t a = roll_angle;
  int8_t dx, dy;
  if (a >= kRollMax / 2) {
    a -= kRollMax / 2;
    dx = -kRollDx[a];
    dy = -kRollDy[a];
  } else {
    dx = kRollDx[a];
    dy = kRollDy[a];
  }
  roll_dx = dx;
  roll_dy = dy;
  roll_dx_div_dy = (int16_t)kRollDxDivDy[a];
  // Negating (dx, dy) keeps every test below: the low bits of -v have as many
  // trailing zeros as v's, and bit 3 of -8 is set while that of -16 is not.
  bool x_maj = dx != 0 && (dx & 7) == 0;
  roll_x_is_major = x_maj;
  bool shift2 = ((x_maj ? dx : dy) & 8) == 0;
  roll_shift_2chars = shift2;
  uint8_t m = (uint8_t)(dx | dy);
  uint8_t p = 8;
  if (!shift2) {
    if ((m & 7) == 0) {
      p = 1;
    } else if ((m & 3) == 0) {
      p = 2;
    } else if ((m & 1) == 0) {
      p = 4;
    }
  }
  roll_period = p;
  roll_mul_dx = _get_mul_func(dx);
  roll_mul_dy = _get_mul_func(dy);
}
