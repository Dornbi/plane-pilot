#include "chardefs.h"
#include "poly.h"

#include "benchmark.h"
#include "cia.h"
#include "gfx.h"
#include "keys.h"
#include "mem.h"
#include <string.h>

static const vertex_t polys[][4] = {{{20, 2}, {35, 8}, {20, 12}, {5, 6}},
                                    {{0, 0}, {35, 0}, {35, 12}, {0, 12}},
                                    {{10, 5}, {12, 5}, {12, 7}, {10, 7}}};

static void _clear_screen() {
  memset(mem_screen_ram, kCharSolidGround, kViewportHeight * kScreenWidth);
}

int main() {
  cia_init();
  bm_init();

  mem_init();
  mem_switch_buffer();
  _clear_screen();
  mem_switch_buffer();
  _clear_screen();

  mem_init_mccm();
  gfx_init_chars();
  gfx_init_raster_irqs();

  uint8_t idx = 0;
  mem_switch_debug(true);

  while (1) {
    fill_poly(polys[idx], 4, kQuadCharStart);
    keyb_poll();
    if (key_pressed(KSCAN_SPACE)) {
      idx = (idx + 1) % (sizeof(polys) / sizeof(polys[0]));
      _clear_screen();
    }
    mem_switch_buffer();
  };

  return 0;
}