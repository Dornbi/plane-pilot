#include "poly.h"

#include <string.h>

#include "benchmark.h"
#include "cia.h"
#include "keys.h"
#include "mem.h"

static const vertex_t polys[][4] = {{{20, 2}, {35, 8}, {20, 12}, {5, 6}},
                                    {{0, 0}, {35, 0}, {35, 12}, {0, 12}},
                                    {{10, 5}, {12, 5}, {12, 7}, {10, 7}}};

int main() {
  uint8_t idx = 0;
  cia_init();
  bm_init();
  mem_screen_ram = (uint8_t *)0x0400;
  memset(mem_screen_ram, ' ', 1000);
  mem_debug_enabled = true;

  // Clear the screen (basic spaces)
  for (int i = 0; i < 1000; i++) {
    mem_screen_ram[i] = 32;
  }

  // Infinite loop to keep the screen visible
  while (1) {
    fill_poly(polys[idx], 4, 81);
    keyb_poll();
    if (key_pressed(KSCAN_SPACE)) {
      idx = (idx + 1) % (sizeof(polys) / sizeof(polys[0]));
      memset(mem_screen_ram, ' ', 1000);
    }
  };

  return 0;
}