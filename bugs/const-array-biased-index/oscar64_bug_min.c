// oscar64 1.32.272 - a const array of structs is read as the constant 0
// when the index carries a non-zero constant offset.
//
// Build:
//   oscar64 -ii=<oscar64>/include -O2 -Op -Oa -Oi -Oz -Oo -o=min.prg min.c
//
// Then read min.asm. Expected: the else arm loads from `tb`. Actual: it
// stores an immediate 0 and `tb` is not emitted into the binary at all.
//
// Every access below is in bounds and the code is well defined C - the
// subtraction only ever runs on the branch where i >= 4.

#include <stdint.h>

struct b_t {
  uint8_t pad[2];
  uint8_t v;
};

const struct b_t tb[4] = {
    {{0, 0}, 20}, {{0, 0}, 21}, {{0, 0}, 22}, {{0, 0}, 23},
};

volatile uint8_t out;

void f(uint8_t i) {  // i is 0..7
  uint8_t v;
  if (i < 4) {
    v = 99;
  } else {
    v = tb[i - 4].v;  // index is 0..3 here: always in bounds
  }
  out = v;
}

int main(void) {
  f(*(volatile uint8_t *)0xD012 & 7);  // raster line, so i is unknowable
  return 0;
}

// Generated for the else arm (oscar64 1.32.272, any -O level including -O0):
//
//   .s5:
//   0891 : a9 00 __ LDA #$00
//   0893 : 85 f7 __ STA $f7 ; (out + 0)
//
// and `tb` appears nowhere in the listing. Expected something like
//
//   LDA __multab3L,x
//   TAX
//   LDA tb+2,x
