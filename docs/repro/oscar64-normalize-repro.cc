// Minimal repro: oscar64 ae7ecb4 ("Optimize switch branch cascade") miscompiles
// the 16-bit `while (bit > L2)` cascade inside this integer sqrt.
// Expected (any oscar64 up to c020aff): every unit-length vector normalizes to 256.
#include <stdio.h>
#include <stdint.h>

struct vec3_t { int16_t x, y, z; };

static uint16_t sqr8p8(int16_t a) {
  if (a < 0) a = -a;
  return (uint16_t)(((uint32_t)a * (uint32_t)a) >> 8);
}

static int16_t div8p8(int16_t a, int16_t b) {
  if (a == 0 || b == 0) return 0;
  int32_t r = ((int32_t)a << 8) / b;
  return (int16_t)r;
}

void vec_normalize(vec3_t *v) {
  uint16_t L2 = sqr8p8(v->x) + sqr8p8(v->y) + sqr8p8(v->z);
  if (L2 == 0) return;
  L2 <<= 6;
  uint16_t res = 0;
  uint16_t bit = (uint16_t)1 << 14;
  while (bit > L2) bit >>= 2;
  while (bit != 0) {
    if (L2 >= res + bit) { L2 -= res + bit; res = (res >> 1) + bit; }
    else res >>= 1;
    bit >>= 2;
  }
  if (res == 0) return;
  v->x = div8p8(v->x, res << 1);
  v->y = div8p8(v->y, res << 1);
  v->z = div8p8(v->z, res << 1);
}

int main(void) {
  int16_t vals[7] = {0, 1, 17, 100, 256, -256, -37};
  for (uint8_t i = 0; i < 7; i++)
    for (uint8_t j = 0; j < 7; j++)
      for (uint8_t k = 0; k < 7; k++) {
        vec3_t v = {vals[i], vals[j], vals[k]};
        vec_normalize(&v);
        printf("%d %d %d -> %d %d %d\n", vals[i], vals[j], vals[k], v.x, v.y, v.z);
      }
  return 0;
}
