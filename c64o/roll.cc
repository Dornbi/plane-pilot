#include "roll.h"

#include <stdint.h>

#define _VALUES_FOR_ANGLES                                                     \
  {                                                                            \
      _VALUE0(8, 0),    _VALUE(16, -1),  _VALUE(8, -1),   _VALUE(8, -2),       \
      _VALUE(8, -3),    _VALUE(8, -4),   _VALUE(8, -5),   _VALUE(8, -6),       \
      _VALUE(8, -8),    _VALUE(6, -8),   _VALUE(10, -16), _VALUE(4, -8),       \
      _VALUE(6, -16),   _VALUE(2, -8),   _VALUE(2, -16),  _VALUE(0, -8),       \
      _VALUE(-2, -16),  _VALUE(-2, -8),  _VALUE(-6, -16), _VALUE(-4, -8),      \
      _VALUE(-10, -16), _VALUE(-6, -8),  _VALUE(-8, -8),  _VALUE(-8, -6),      \
      _VALUE(-8, -5),   _VALUE(-8, -4),  _VALUE(-8, -3),  _VALUE(-8, -2),      \
      _VALUE(-8, -1),   _VALUE(-16, -1), _VALUE0(-8, 0),  _VALUE(-16, 1),      \
      _VALUE(-8, 1),    _VALUE(-8, 2),   _VALUE(-8, 3),   _VALUE(-8, 4),       \
      _VALUE(-8, 5),    _VALUE(-8, 6),   _VALUE(-8, 8),   _VALUE(-6, 8),       \
      _VALUE(-10, 16),  _VALUE(-4, 8),   _VALUE(-6, 16),  _VALUE(-2, 8),       \
      _VALUE(-2, 16),   _VALUE(0, 8),    _VALUE(2, 16),   _VALUE(2, 8),        \
      _VALUE(6, 16),    _VALUE(4, 8),    _VALUE(10, 16),  _VALUE(6, 8),        \
      _VALUE(8, 8),     _VALUE(8, 6),    _VALUE(8, 5),    _VALUE(8, 4),        \
      _VALUE(8, 3),     _VALUE(8, 2),    _VALUE(8, 1),    _VALUE(16, 1),       \
  }

#define _VALUE(x, y) (x)
#define _VALUE0(x, y) _VALUE(x, y)
const int8_t kRollDx[kRollMax] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

#define _VALUE(x, y) (y)
#define _VALUE0(x, y) _VALUE(x, y)
const int8_t kRollDy[kRollMax] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

// Could not get this to work with a macro. Should be something like:
#define _VALUE(x, y) (x) * 16 / (y)
#define _VALUE0(x, y) 0
const int kRollDxDivDy[kRollMax] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

// Note there is a discrepancy between this and the code in roll_angle.py:
// - For rendering (here), we always cap it at 8
// - To find boxes (in roll_angle.py) we need the real period to find boxes.
static const uint8_t kRollPeriod[] = {
    1, 8, 8, 4, 8, 2, 8, 4, 1, 4, 8, 2, 8, 4, 8, 1, 8, 4, 8, 2,
    8, 4, 1, 4, 8, 2, 8, 4, 8, 8, 1, 8, 8, 4, 8, 2, 8, 4, 1, 4,
    8, 2, 8, 4, 8, 1, 8, 4, 8, 2, 8, 4, 1, 4, 8, 2, 8, 4, 8, 8,
};

uint8_t roll_angle;
int8_t roll_dx;
int8_t roll_dy;
int16_t roll_dx_div_dy;
uint8_t roll_period;
bool roll_x_is_major;
bool roll_shift_2chars;

static void *_get_mul_func(int8_t x) { return (void *)roll_mul_funcs[x + 16]; }

void roll_update_state() {
  roll_dx = kRollDx[roll_angle];
  roll_dy = kRollDy[roll_angle];
  roll_dx_div_dy = (int16_t)kRollDxDivDy[roll_angle];
  roll_period = kRollPeriod[roll_angle];
  roll_x_is_major =
      (roll_dx == -16 || roll_dx == -8 || roll_dx == 8 || roll_dx == 16);
  roll_shift_2chars =
      (roll_dx == -16 || roll_dx == 16 || roll_dy == -16 || roll_dy == 16);
  roll_mul_dx = _get_mul_func(roll_dx);
  roll_mul_dy = _get_mul_func(roll_dy);
}
