#include "map.h"
#include "color.h"
#include "gfx.h"
#include "mem.h"
#include "screen.h"
#include "vic.h"
#include "world.h"

#include <string.h>

bool map_mode = false;

void map_enter() {
  gfx_stop_raster_irqs();

  // Disable sprites
  vic.spr_enable = 0x00;

  // Set screen pointers: Screen RAM at $E800, Char RAM at $E000
  vic_memptr = 0xA8;

  // Single color character mode
  vic.ctrl1 = 0x1b; // 25 rows, text mode, screen on
  vic.ctrl2 = 0xc8; // Multicolor off
  vic.color_back = kColorBlack;
  vic.color_border = kColorBlack;

  // Clear screen RAM at $E800 with space character (32)
  memset(kScreenRamMain, 32, 1000);

  // Clear color RAM at $D800 with kColorDarkGray
  memset(kColorRam, kColorDarkGray, 1000);

  // Render the 32x16 map
  for (uint8_t y = 0; y < kWorldMapHeight; ++y) {
    for (uint8_t x = 0; x < kWorldMapWidth; ++x) {
      uint8_t cell = kWorldMap[y][x];
      uint16_t offset = (y + 4) * 40 + (x + 4);
      if (cell == MAP_DOT_GROUND) {
        kScreenRamMain[offset] = 0; // Solid block character
        kColorRam[offset] = kColorGreen;
      } else {
        // Placeholder for other characters
        kScreenRamMain[offset] = 46; // '.'
        kColorRam[offset] = kColorWhite;
      }
    }
  }

  map_mode = true;
}

void map_exit() {
  // screen_restore_simulation() re-applies the debug or default view's VIC
  // mode and colors (map_enter wiped the color RAM and the bottom of the
  // main screen), and restarts sprites and the raster IRQ split.
  screen_restore_simulation();

  map_mode = false;
}
