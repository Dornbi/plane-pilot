#include "map.h"

#include <string.h>

#ifdef __OSCAR64__
#include <c64/memmap.h>
#else
#define MMAP_NO_ROM 0x35
#define MMAP_RAM 0x30
static char mmap_set(char pla) { return pla; }
#endif

#include "box.h"
#include "color.h"
#include "gfx.h"
#include "mapdefs.h"
#include "mem.h"
#include "screen.h"
#include "vic.h"
#include "view.h"
#include "world.h"

// The routines below only run on screen transitions (menu, help, map) or at
// startup, never inside the per-frame simulation loop, so the outliner's
// size-for-a-JSR trade costs nothing that matters here. It stays off
// globally so the renderer and the raster IRQ handlers keep their
// straight-line code.
#pragma optimize(push, outline)

bool map_mode = false;

// The map is a multicolor bitmap; see docs/map.md for the full plan.
//
//   $D000..$D3E7  screen RAM      bit pair 01 (high nibble) and 10 (low)
//   $D800..$DBE7  color RAM       bit pair 11
//   $E000..$FF3F  bitmap          destroys char RAM, both screen buffers
//                                 and the panel bitmap
//
// $D000 is RAM under I/O -- useless for almost anything except things the
// VIC reads, and outside the scarce $0860..$D000 main region. Only
// $D000..$D7BF is free: mem_init() expands the sprite bitmaps to $D7C0,
// which runs to $DFFF exactly. Screen RAM needs 1000 bytes and fits with
// 984 to spare.
static uint8_t *const kMapBitmap = (uint8_t *)0xE000;
static uint8_t *const kMapScreenRam = (uint8_t *)0xD000;
static const uint16_t kMapBitmapSize = 8000;
static const uint16_t kMapScreenSize = kScreenWidth * kScreenHeight;

// VIC memory pointers, bank 3 ($C000):
// - video matrix at $D000 -> offset $1000, bits 4..7 = 0100
// - bitmap at $E000       -> offset $2000, bits 1..3 = 100
static const uint8_t kVicMemMap = 0x48;

// The map occupies character rows 4..19, columns 4..35 of the 40x25 screen.
static const uint8_t kMapOriginRow = 4;
static const uint8_t kMapOriginCol = 4;
static const uint16_t kMapCharOffset =
    kMapOriginRow * kScreenWidth + kMapOriginCol;
// Skip from the last cell of one map row to the first of the next.
static const uint8_t kMapCharGap = kScreenWidth - kWorldMapWidth;

// Bit pair 01 is pinned to white in every one of the 1000 cells, which is
// what makes it a screen-wide overlay layer the flight path and the navpoint
// digits can be drawn into without any per-cell color negotiation (phases 4
// and 5). Bit pair 10 is the tile's own second color, so the low nibble
// varies per cell; the surround outside the map keeps black.
static const uint8_t kMapScreenSurround = (kColorWhite << 4) | kColorBlack;

// Maps a WorldMapType to a tile index. Empty ground carries the dotted
// gridline, whose art has a two-cell period, so it picks one of the four
// grid variants by the parity of its *map* row and column -- matching
// tools/render_map_preview.py, which is the reference for what the finished
// screen should look like.
static uint8_t _tile_index(uint8_t cell, uint8_t row, uint8_t col) {
  if (cell == MAP_DOT_GROUND) {
    return kMapTileGrid + (((row & 1) << 1) | (col & 1));
  }
  if (cell < kWorldMapObjStart) {
    return kMapTileDot + (cell - MAP_DOT_BLACK);
  }
  return kMapTileObj + (cell - MAP_OBJ_RUNWAY);
}

// world_map.cc is stored with N down and W right; the display is rotated 180
// degrees to put N up and W left. Both passes below therefore walk the
// screen forwards while walking kWorldMap backwards, which keeps the
// rotation to two decrementing loop counters and no address arithmetic:
//
//   screen_row = 15 - map_row      screen_col = 31 - map_col
//
// Pass A -- the object layer. I/O is banked in, so color RAM is writable and
// the bitmap at $E000 is plain RAM either way.
static void _draw_object_layer(void) {
  uint8_t *bm = kMapBitmap + kMapCharOffset * 8;
  uint8_t *cr = kColorRam + kMapCharOffset;
  for (uint8_t row = kWorldMapHeight; row-- != 0;) {
    for (uint8_t col = kWorldMapWidth; col-- != 0;) {
      const uint8_t idx = _tile_index(kWorldMap[row][col], row, col);
      // Eight indexed loads sharing one index register, off a
      // compile-time-constant base per row. This is what kMapTileRows is
      // transposed for: a [tile][8] layout would need a multiply by 8 and a
      // pointer per cell.
      bm[0] = kMapTileRows[0][idx];
      bm[1] = kMapTileRows[1][idx];
      bm[2] = kMapTileRows[2][idx];
      bm[3] = kMapTileRows[3][idx];
      bm[4] = kMapTileRows[4][idx];
      bm[5] = kMapTileRows[5][idx];
      bm[6] = kMapTileRows[6][idx];
      bm[7] = kMapTileRows[7][idx];
      bm += 8;
      *cr++ = kMapTileCol[idx];
    }
    bm += kMapCharGap * 8;
    cr += kMapCharGap;
  }
}

// Pass B -- the screen RAM layer, with I/O banked out so $D000 is RAM. The
// tile index is recomputed rather than carried over from pass A: there is no
// 512-byte scratch buffer to spare in main, and this runs once per
// map_enter(), not per frame.
static void _draw_screen_layer(void) {
  uint8_t *sm = kMapScreenRam + kMapCharOffset;
  for (uint8_t row = kWorldMapHeight; row-- != 0;) {
    for (uint8_t col = kWorldMapWidth; col-- != 0;) {
      const uint8_t idx = _tile_index(kWorldMap[row][col], row, col);
      *sm++ = (kColorWhite << 4) | kMapTileLo[idx];
    }
    sm += kMapCharGap;
  }
}

void map_enter() {
  // Stop the raster split first: it rewrites $d011, $d018 and $d021 three
  // times a frame and would fight every register below.
  //
  // Note what "stop" means here. oscar64's rirq_stop() is nothing but an
  // `sei`; it leaves vic.intr_enable set, and rirq_start() is the matching
  // `cli`. So the raster IRQs are only masked, and **nothing in map mode may
  // execute a cli** -- one would restart the split on the next raster
  // compare, and the map would be redrawn out from under itself in three
  // horizontal bands. Interrupts stay masked from here until
  // map_exit()'s gfx_init_raster_irqs(). That also makes the I/O banking in
  // pass B safe for free.
  gfx_stop_raster_irqs();

  // Disable sprites. The aircraft marker (phase 6) turns two back on.
  vic.spr_enable = 0x00;

  // Blank the display for the build. Three passes over 8000, 1000 and 1000
  // bytes are several frames long, and with DEN clear they show the black
  // border instead of the old character RAM reinterpreted as a bitmap --
  // and run faster, since a blanked screen has no badlines to steal cycles.
  vic.ctrl1 = 0x2b;  // bitmap mode, screen off
  vic.ctrl2 = 0xd8;  // multicolor
  vic_memptr = kVicMemMap;
  vic.color_back = kColorGreen;  // bit pair 00, the ground everywhere
  vic.color_border = kColorBlack;

  // Pass A -- bitmap and color RAM.
  //
  // The 8-cell-wide surround outside the map is solid 11 over black color
  // RAM, the same idiom as kCharSolid11. Filling all 8000 bytes and then
  // overwriting the map area costs about a frame and is much simpler than
  // computing the non-contiguous border.
  memset(kMapBitmap, 0xFF, kMapBitmapSize);
  memset(kColorRam, kColorBlack, kMapScreenSize);
  _draw_object_layer();

  // Pass B -- screen RAM at $D000, which the CPU can only reach with I/O
  // banked out. Interrupts are already masked (see gfx_stop_raster_irqs()
  // above), so no handler can touch $D000..$DFFF while it is RAM.
  mmap_set(MMAP_RAM);
  memset(kMapScreenRam, kMapScreenSurround, kMapScreenSize);
  _draw_screen_layer();
  mmap_set(MMAP_NO_ROM);

  vic.ctrl1 = 0x3b;  // bitmap mode, screen on

  map_mode = true;
}

void map_exit() {
  // Rebuild what the map view destroyed, before restoring the simulation.
  // The map takes over $E000..$FF3F wholesale -- character RAM, both screen
  // buffers and the panel bitmap -- and none of this is reachable from
  // screen_restore_simulation(). Getting it wrong shows up as corrupt
  // graphics rather than a crash.
  //
  // gfx_init_chars() restores characters 0 and 32..255; the two caches below
  // own the gradient slots and the panel bitmap, and would otherwise report
  // memory they no longer hold as still valid.
  gfx_init_chars();
  box_invalidate();
  view_invalidate_bitmap();

  // screen_restore_simulation() re-applies the debug or default view's VIC
  // mode and colors (map_enter wiped the color RAM and the bottom of the
  // main screen), and restarts sprites and the raster IRQ split. It must run
  // after view_invalidate_bitmap(), since it is what calls
  // view_refresh_panel(). $d018 needs no explicit restore: the split's
  // _switch_to_panel_top() and _switch_to_terrain() both set it every frame.
  screen_restore_simulation();

  map_mode = false;
}

#pragma optimize(pop)
