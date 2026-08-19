// oscar64 miscompile: selecting between two *differently typed* struct tables
// in the two arms of an if, where the index comes from a bounded search loop.
//
//   oscar64 -O2 oscar64_bug_struct_select.c
//
// Expected: "22 32 15 16  PASS"    Actual (-O1/-O2/-O3/-Os): "22 255 255 255  FAIL"
//
// Only the first field survives; the other three arguments are folded to the
// *other* arm's constant (0xff).  -O0 is correct.
#include <stdio.h>
#include <stdint.h>

typedef struct { uint8_t a; int8_t px; int8_t py; } m1_t;             /* 3 bytes */
typedef struct { uint8_t a; uint8_t b; int8_t px; int8_t py; } m2_t;  /* 4 bytes */

const int16_t tbl[10] = {900, 700, 550, 430, 340, 270, 210, 165, 130, 100};
const m1_t t1[5] = {{10, 1, 2}, {11, 3, 4}, {12, 5, 6}, {13, 7, 8}, {14, 9, 10}};
const m2_t t2[5] = {{20, 30, 11, 12}, {21, 31, 13, 14}, {22, 32, 15, 16},
                    {23, 33, 17, 18}, {24, 34, 19, 20}};

volatile uint8_t out[4];
volatile int16_t vdepth = 150; /* selects rung 7, i.e. t2[2] */

__noinline void sink(uint8_t a, uint8_t b, int8_t px, int8_t py) {
  out[0] = a;
  out[1] = b;
  out[2] = (uint8_t)px;
  out[3] = (uint8_t)py;
}

__noinline void f(int16_t depth) {
  uint8_t rung = 0;
  while (rung < 9 && depth <= tbl[rung + 1]) {
    ++rung;
  }

  uint8_t a, b;
  int8_t px, py;
  if (rung < 5) {
    const m1_t *m = &t1[rung];
    a = m->a;
    b = 0xff;
    px = m->px;
    py = m->py;
  } else {
    const m2_t *m = &t2[rung - 5];
    a = m->a;
    b = m->b;
    px = m->px;
    py = m->py;
  }
  sink(a, b, px, py);
}

int main(void) {
  f(vdepth);
  printf("%d %d %d %d  %s\n", (int)out[0], (int)out[1], (int)out[2],
         (int)out[3],
         (out[0] == 22 && out[1] == 32 && out[2] == 15 && out[3] == 16)
             ? "PASS"
             : "FAIL");
  for (;;) {
  }
  return 0;
}
