#include <stdint.h>

// A striped table of addresses of objects, as boxdefs.cc had it. Each entry is
// read back through a volatile index, so nothing folds, and compared with the
// address it was initialised from.
struct box { uint8_t w, h; };

static const struct box b0 = {1, 2};
static const struct box b1 = {3, 4};
static const struct box b2 = {5, 6};
static const struct box b3 = {7, 8};

__striped const struct box *const tab[4] = {&b0, &b1, &b2, &b3};
// The same with gaps, as alt_boxes had them.
__striped const struct box *const gaps[4] = {&b0, 0, &b2, 0};

// Addresses inside an array, as mem.cc's mem_color_row_ptrs has them. These
// come out right.
static uint8_t buf[64];
__striped uint8_t *const rows[4] = {buf + 0, buf + 16, buf + 32, buf + 48};

volatile uint16_t out[12];
volatile uint8_t idx;

int main(void) {
  for (idx = 0; idx < 4; ++idx) {
    out[idx] = (uint16_t)tab[idx];
    out[4 + idx] = (uint16_t)gaps[idx];
    out[8 + idx] = (uint16_t)rows[idx];
  }
  for (;;) {
  }
  return 0;
}
