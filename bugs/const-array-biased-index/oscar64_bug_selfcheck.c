// oscar64 1.32.272 - self-checking version of the const-struct-array bug.
// Run it on a C64 or in VICE; it prints one line per case and PASS/FAIL.
//
// Build:
//   oscar64 -ii=<oscar64>/include -O2 -Op -Oa -Oi -Oz -Oo \
//           -o=selfcheck.prg selfcheck.c
//
// Every array access here is in bounds. The indices come from a volatile
// array so the compiler cannot know them, which is the only thing the cases
// need in order to be honest.

#include <stdint.h>
#include <stdio.h>

struct small_t {  // stride 3
  uint8_t pad[2];
  uint8_t v;
};

// Four entries, indexed 0..3 after subtracting 4 from an index of 4..7.
const struct small_t kBiased[4] = {
    {{0, 0}, 20}, {{0, 0}, 21}, {{0, 0}, 22}, {{0, 0}, 23},
};

// Eight entries, indexed directly by 0..7.
const struct small_t kDirect[8] = {
    {{0, 0}, 10}, {{0, 0}, 11}, {{0, 0}, 12}, {{0, 0}, 13},
    {{0, 0}, 20}, {{0, 0}, 21}, {{0, 0}, 22}, {{0, 0}, 23},
};

// The same eight entries, only without the const.
struct small_t kDirectRW[8] = {
    {{0, 0}, 10}, {{0, 0}, 11}, {{0, 0}, 12}, {{0, 0}, 13},
    {{0, 0}, 20}, {{0, 0}, 21}, {{0, 0}, 22}, {{0, 0}, 23},
};

// Case A: explicit subtraction in the index. i - 4 is 0..3 on this branch.
uint8_t case_a(uint8_t i) {
  uint8_t v;
  if (i < 4) {
    v = 99;
  } else {
    v = kBiased[i - 4].v;
  }
  return v;
}

// Case B: no subtraction anywhere. The branch tells the compiler i >= 4, and
// that is enough - it appears to rebase the index and hit the same path.
uint8_t case_b(uint8_t i) {
  uint8_t v;
  if (i < 4) {
    v = 99;
  } else {
    v = kDirect[i].v;
  }
  return v;
}

// Case C: identical to B without the const. This one is correct today, and is
// here so a run tells the two apart.
uint8_t case_c(uint8_t i) {
  uint8_t v;
  if (i < 4) {
    v = 99;
  } else {
    v = kDirectRW[i].v;
  }
  return v;
}

volatile uint8_t idx[8];

int main(void) {
  uint8_t k;
  uint8_t bad = 0;

  for (k = 0; k < 8; ++k) {
    idx[k] = k;
  }

  printf("i   A(exp) got   B(exp) got   C(exp) got\n");
  for (k = 4; k < 8; ++k) {
    uint8_t i = idx[k];  // volatile: the compiler must re-read it
    uint8_t want = 20 + (k - 4);
    uint8_t a = case_a(i);
    uint8_t b = case_b(i);
    uint8_t c = case_c(i);
    printf("%d      %d  %d       %d  %d       %d  %d\n", (int)k, (int)want,
           (int)a, (int)want, (int)b, (int)want, (int)c);
    if (a != want || b != want || c != want) {
      bad = 1;
    }
  }

  printf(bad ? "FAIL\n" : "PASS\n");
  return 0;
}

// oscar64 1.32.272 prints 0 for every A and B column and the correct value for
// every C column, i.e.
//
//   i   A(exp) got   B(exp) got   C(exp) got
//   4      20  0       20  0       20  20
//   5      21  0       21  0       21  21
//   6      22  0       22  0       22  22
//   7      23  0       23  0       23  23
//   FAIL
