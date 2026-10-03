#include <stdint.h>

// Each edge of a closed path, put in top-to-bottom order by a swap. Edges 2
// and 3 run upwards and need it; their (a, b) land in out[].
static const uint8_t ys[4] = {2, 6, 10, 6};
volatile uint8_t out[8];

int main(void) {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t a = ys[i];
    uint8_t b = ys[(i + 1) & 3];
    if (a > b) {
      uint8_t t = a;
      a = b;
      b = t;
    }
    out[2 * i] = a;
    out[2 * i + 1] = b;
  }
  for (;;) {
  }
  return 0;
}
