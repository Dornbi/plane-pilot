#include "chardefs.h"
#include "poly.h"

#include "benchmark.h"
#include "cia.h"
#include "gfx.h"
#include "keys.h"
#include "mem.h"
#include <string.h>

static const vertex_t polys[][4] = {{{20, 2}, {24, 6}, {20, 10}, {16, 6}},
                                    {{1, 1}, {38, 1}, {38, 12}, {1, 12}},
                                    {{0, 0}, {39, 0}, {39, 13}, {0, 13}},
                                    {{18, 3}, {22, 3}, {30, 9}, {10, 9}},
                                    {{36, 3}, {44, 3}, {70, 17}, {10, 17}},
                                    {{36, 3}, {44, 3}, {70, 19}, {10, 15}},
                                    {{20, 2}, {35, 8}, {20, 12}, {5, 6}},
                                    {{10, 5}, {12, 5}, {12, 7}, {10, 7}}};

static void _clear_screen() {
  memset(mem_screen_ram, kCharSolidGround, kViewportHeight * kScreenWidth);
  memset(mem_screen_ram + kViewportHeight * kScreenWidth, kCharSolid11,
         (kScreenHeight - kViewportHeight) * kScreenWidth);
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
    _clear_screen();
    poly_fill(polys[idx], 4, kQuadCharStart);
    mem_switch_buffer();
    _clear_screen();
    poly_fill(polys[idx], 4, kQuadCharStart);
    mem_switch_buffer();

    keyb_poll();
    if (key_pressed(KSCAN_SPACE)) {
      idx = (idx + 1) % (sizeof(polys) / sizeof(polys[0]));
    }
  };

  return 0;
}