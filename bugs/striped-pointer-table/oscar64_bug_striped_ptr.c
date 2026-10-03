// oscar64 miscompile: a __striped array of pointers initialised with
// address-of expressions (&object, &array[n]) is emitted unstriped, as plain
// little-endian words, while the code reads it as striped.
//
//   oscar64 -O2 -e oscar64_bug_striped_ptr.c
//
// Expected: "PASS"   Actual (every optimisation level, -O0 included): "FAIL ..."
#include <stdint.h>
#include <stdio.h>

#ifdef __GNUC__
#define __striped
#endif

struct box {
  uint8_t w, h;
};

static const struct box b0 = {1, 2};
static const struct box b1 = {3, 4};
static const struct box b2 = {5, 6};
static const struct box b3 = {7, 8};

// Addresses of objects: broken.
__striped const struct box *const tab[4] = {&b0, &b1, &b2, &b3};
// The same with null entries between: broken, differently.
__striped const struct box *const gaps[4] = {&b0, 0, &b2, 0};

// Addresses inside an array, written as array + n: correct.
static uint8_t buf[64];
__striped uint8_t *const rows[4] = {buf + 0, buf + 16, buf + 32, buf + 48};

volatile uint8_t idx;

static uint8_t check(const char *name, uint16_t got, uint16_t want) {
  if (got == want) {
    return 0;
  }
  printf("%s[%d]: %04x not %04x\n", name, idx, got, want);
  return 1;
}

int main(void) {
  static const struct box *const want_tab[4] = {&b0, &b1, &b2, &b3};
  static const struct box *const want_gaps[4] = {&b0, 0, &b2, 0};
  uint8_t bad = 0;
  // Read back through a volatile index, so nothing folds.
  for (idx = 0; idx < 4; ++idx) {
    bad |= check("tab", (uint16_t)(uintptr_t)tab[idx],
                 (uint16_t)(uintptr_t)want_tab[idx]);
    bad |= check("gaps", (uint16_t)(uintptr_t)gaps[idx],
                 (uint16_t)(uintptr_t)want_gaps[idx]);
    bad |= check("rows", (uint16_t)(uintptr_t)rows[idx],
                 (uint16_t)(uintptr_t)(buf + 16 * idx));
  }
  printf(bad ? "FAIL\n" : "PASS\n");
  return bad;
}
