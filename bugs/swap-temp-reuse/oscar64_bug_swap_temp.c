#include <stdint.h>

// Four edges of a rectangle, two of them running bottom to top. The swap puts
// every edge top to bottom; ya/yb of each edge land in out[].
volatile int16_t out[8];
static int16_t px[4] = {10, 20, 20, 10};
static int16_t py[4] = {0, 0, 9, 9};

static void trace(const int16_t *x, const int16_t *y, uint8_t n) {
  for (uint8_t i = 0; i < n; ++i) {
    uint8_t j = (uint8_t)(i + 1 == n ? 0 : i + 1);
    int16_t xa0 = x[i], ya = y[i], xb0 = x[j], yb = y[j];
    if (ya == yb) {
      continue;
    }
    if (yb < ya) {
      int16_t t = xa0;
      xa0 = xb0;
      xb0 = t;
      t = ya;
      ya = yb;
      yb = t;
    }
    out[2 * i] = ya;
    out[2 * i + 1] = (int16_t)(yb * 100 + xa0);
  }
}

int main(void) {
  trace(px, py, 4);
  for (;;) {
  }
  return 0;
}
