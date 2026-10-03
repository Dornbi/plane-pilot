// oscar64 miscompile: a swap through a temporary, inside a loop, loses the
// assignment from the temporary.
//
//   oscar64 -O2 -e oscar64_bug_swap_loop.c
//
// Expected: "PASS"   Actual (-O1 and up): "FAIL ..."   (-O0: "PASS")
//
// Each loop puts the end points of the four edges of a closed path in
// top-to-bottom order. Edges 2 and 3 run upwards and need the swap.
#include <stdint.h>
#include <stdio.h>

#ifdef __GNUC__
#define __noinline __attribute__((noinline))
#endif

static const uint8_t ys[4] = {2, 6, 10, 6};
static const int8_t xs[4] = {20, 24, 20, 16};

uint8_t out_a[8];
int8_t out_b[16];
static uint8_t k;

// Case A: the swap written out in the loop body.
__noinline void case_a(void) {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t a = ys[i];
    uint8_t b = ys[(i + 1) & 3];
    if (a > b) {
      uint8_t t = a;
      a = b;
      b = t;
    }
    out_a[2 * i] = a;
    out_a[2 * i + 1] = b;
  }
}

// Case B: the swap in an inline function's parameters, with a temporary
// each. Out of line it is correct; inlined into the loop below it is not.
static inline void edge(int8_t x1, uint8_t y1, int8_t x2, uint8_t y2) {
  if (y1 > y2) {
    int8_t tmp_x = x1;
    x1 = x2;
    x2 = tmp_x;

    uint8_t tmp_y = y1;
    y1 = y2;
    y2 = tmp_y;
  }
  out_b[k++] = x1;
  out_b[k++] = (int8_t)y1;
  out_b[k++] = x2;
  out_b[k++] = (int8_t)y2;
}

__noinline void case_b(void) {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t next = i + 1;
    if (next == 4) {
      next = 0;
    }
    edge(xs[i], ys[i], xs[next], ys[next]);
  }
}

static const uint8_t expect_a[8] = {2, 6, 6, 10, 6, 10, 2, 6};
static const int8_t expect_b[16] = {20, 2, 24, 6,  24, 6, 20, 10,
                                    16, 6, 20, 10, 20, 2, 16, 6};

int main(void) {
  case_a();
  case_b();

  uint8_t bad = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (out_a[i] != expect_a[i]) {
      bad |= 1;
    }
  }
  for (uint8_t i = 0; i < 16; i++) {
    if (out_b[i] != expect_b[i]) {
      bad |= 2;
    }
  }
  if (bad) {
    printf("FAIL\n");
    printf("A:");
    for (uint8_t i = 0; i < 8; i++) {
      printf(" %d", (int)out_a[i]);
    }
    printf("\nB:");
    for (uint8_t i = 0; i < 16; i++) {
      printf(" %d", (int)out_b[i]);
    }
    printf("\n");
  } else {
    printf("PASS\n");
  }
  return bad;
}
