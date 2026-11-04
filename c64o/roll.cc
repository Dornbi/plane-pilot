#include "roll.h"

#include <stdint.h>

// Only the first quarter-turn and its last angle are stored, 0 to 15: angle
// a + kRollMax / 2 is angle a with (dx, dy) negated, which leaves dx / dy and
// the period unchanged, and angle kRollMax / 2 - a is angle a with dx negated,
// which negates dx / dy (rounded towards zero, so exactly) and leaves the
// period unchanged too. The second quarter was, from 16 on: (-2, -16),
// (-2, -8), (-6, -16), (-4, -8), (-10, -16), (-6, -8), (-8, -8), (-8, -6),
// (-8, -5), (-8, -4), (-8, -3), (-8, -2), (-8, -1), (-16, -1).
static const uint8_t kRollMirror = kRollMax / 4;
#define _VALUES_FOR_ANGLES                                                     \
  {                                                                            \
      _VALUE0(8, 0),    _VALUE(16, -1),  _VALUE(8, -1),   _VALUE(8, -2),       \
      _VALUE(8, -3),    _VALUE(8, -4),   _VALUE(8, -5),   _VALUE(8, -6),       \
      _VALUE(8, -8),    _VALUE(6, -8),   _VALUE(10, -16), _VALUE(4, -8),       \
      _VALUE(6, -16),   _VALUE(2, -8),   _VALUE(2, -16),  _VALUE(0, -8),       \
  }

#define _VALUE(x, y) (x)
#define _VALUE0(x, y) _VALUE(x, y)
const int8_t kRollDx[kRollMirror + 1] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

#define _VALUE(x, y) (y)
#define _VALUE0(x, y) _VALUE(x, y)
const int8_t kRollDy[kRollMirror + 1] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

// Could not get this to work with a macro. Should be something like:
#define _VALUE(x, y) (x) * 16 / (y)
#define _VALUE0(x, y) 0
__striped const int kRollDxDivDy[kRollMirror + 1] = _VALUES_FOR_ANGLES;
#undef _VALUE
#undef _VALUE0

uint8_t roll_angle;
int8_t roll_dx;
int8_t roll_dy;
int16_t roll_dx_div_dy;
uint8_t roll_period;
bool roll_x_is_major;
bool roll_shift_2chars;
int8_t roll_ex_dy[8];
int8_t roll_ey_dx[8];

// Note there is a discrepancy between this and the code in roll_angle.py:
// - For rendering (here), we always cap roll_period at 8
// - To find boxes (in roll_angle.py) we need the real period to find boxes.
void roll_update_state() {
  uint8_t a = roll_angle;
  // Each 0 or 0xFF, and v ^ n - n is v or -v: dy's for the half-turn, the
  // slope's for the mirror, and dx's for either but not both.
  uint8_t ny = 0;
  int16_t ns = 0;
  if (a >= kRollMax / 2) {
    a -= kRollMax / 2;
    ny = 0xFF;
  }
  if (a > kRollMirror) {
    a = kRollMax / 2 - a;
    ns = -1;
  }
  uint8_t nx = ny ^ (uint8_t)ns;
  int8_t dx = (int8_t)(((uint8_t)kRollDx[a] ^ nx) - nx);
  int8_t dy = (int8_t)(((uint8_t)kRollDy[a] ^ ny) - ny);
  roll_dx = dx;
  roll_dy = dy;
  roll_dx_div_dy = (int16_t)((kRollDxDivDy[a] ^ ns) - ns);
  // Negating dx, dy or both keeps every test below: the low bits of -v have as
  // many trailing zeros as v's, and bit 3 of -8 is set while that of -16 is
  // not.
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
  // roll_get_dist()'s products: -4 times dx and dy at e = 0, one more each e.
  int8_t vx = (int8_t)(dx * -4), vy = (int8_t)(dy * -4);
  for (uint8_t e = 0; e < 8; ++e) {
    roll_ey_dx[e] = vx;
    roll_ex_dy[e] = vy;
    vx += dx;
    vy += dy;
  }
}
