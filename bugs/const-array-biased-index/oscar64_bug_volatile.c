// oscar64 1.32.272 - a volatile global is constant-folded and never read.
//
// Build:
//   oscar64 -ii=<oscar64>/include -O2 -Op -Oa -Oi -Oz -Oo -o=vol.prg vol.c
//
// Expected: a load from `which`, then an increment.
// Actual:   LDA #$08 / STA out. `which` is never read and its symbol is not
//           emitted, so nothing outside the program can influence the result.

#include <stdint.h>

volatile uint8_t which = 7;
volatile uint8_t out;

int main(void) {
  out = which + 1;
  return 0;
}
