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
#include "flight.h"
#include "gfx.h"
#include "mapdefs.h"
#include "mem.h"
#include "screen.h"
#include "spritedef.h"
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

// --- Overlay layer --------------------------------------------------------
//
// Bit pair 01 is white in every cell, so anything drawn as 01 lands on top of
// the object art without negotiating for a color first. Both overlay users --
// the navpoint digits here and the flight path in phase 5 -- go through the
// two routines below. Neither touches screen or color RAM, which is what lets
// pass B stay write-once.

// The 8 bitmap bytes of a character cell, addressed in whole-screen
// coordinates (0..24, 0..39).
static uint8_t *_screen_cell_bitmap(uint8_t row, uint8_t col) {
  return kMapBitmap + ((uint16_t)row * kScreenWidth + col) * 8;
}

// The same, for a cell inside the map, by *screen* row and column
// (0..15, 0..31 -- already rotated).
static uint8_t *_map_cell_bitmap(uint8_t screen_row, uint8_t screen_col) {
  return _screen_cell_bitmap(kMapOriginRow + screen_row,
                             kMapOriginCol + screen_col);
}

// Sets one map pixel of the overlay, px 0..127 across, py 0..127 down. Per
// map.md section 4: four multicolor pixels across and eight rows down per
// cell, so py >> 3 and px >> 2 are the cell and the remainders address the
// row and the bit pair within it.
void map_set_overlay_pixel(uint8_t px, uint8_t py) {
  uint8_t *dst = _map_cell_bitmap(py >> 3, px >> 2) + (py & 7);
  const uint8_t shift = (3 - (px & 3)) << 1;
  *dst = (*dst & ~(3 << shift)) | (1 << shift);
}

// Stamps an overlay stencil over one character cell.
//
// A glyph is 4 multicolor pixels wide and 8 rows tall -- exactly one cell,
// byte aligned -- so there is no shifting and no per-pixel addressing. The
// mask holds 11 in every ink pair, so `mask & 0x55` deposits 01 in exactly
// those pairs and `& ~mask` clears them first, leaving every other pair of
// whatever is underneath untouched.
//
// The cell's existing overlay pairs are reset to 00 before the stencil goes
// down. Without that, mission 07 -- whose waypoints 7 and 8 are both
// (0x60, 0xBF), fly inverted over runway 2 then land on it -- would draw '1'
// and '2' superimposed in cell [12][24] rather than letting the last one
// win. It also keeps a flight path crossing the cell from filling in the
// counters of the digit.
static void _map_draw_stencil(const uint8_t *mask, uint8_t row, uint8_t col) {
  uint8_t *dst = _screen_cell_bitmap(row, col);
  for (uint8_t r = 0; r < 8; ++r) {
    // b & ~(b >> 1) & 0x55 is 1 in the low bit of every pair that is 01 and
    // nowhere else, so clearing it turns 01 into 00 and leaves 10 and 11.
    uint8_t b = dst[r];
    b &= ~(b & ~(b >> 1) & 0x55);
    dst[r] = (b & ~mask[r]) | (mask[r] & 0x55);
  }
}

// N, E, S and W, one character clear of each side of the map, centred on it.
// They land in the surround, which is solid 11 over black color RAM, so the
// stencil's 01 pairs come out white against black with no extra work -- the
// same overlay layer the digits and the path use.
static void _map_draw_compass(void) {
  static const uint8_t kMapCenterRow = kMapOriginRow + kWorldMapHeight / 2;
  static const uint8_t kMapCenterCol = kMapOriginCol + kWorldMapWidth / 2;
  static const uint8_t kGap = 2; // one blank cell between map and letter

  _map_draw_stencil(kMapCompassMask[kMapCompassN], kMapOriginRow - kGap,
                    kMapCenterCol);
  _map_draw_stencil(kMapCompassMask[kMapCompassS],
                    kMapOriginRow + kWorldMapHeight + kGap - 1, kMapCenterCol);
  _map_draw_stencil(kMapCompassMask[kMapCompassW], kMapCenterRow,
                    kMapOriginCol - kGap);
  _map_draw_stencil(kMapCompassMask[kMapCompassE], kMapCenterRow,
                    kMapOriginCol + kWorldMapWidth + kGap - 1);
}

// Plots the recent flight path. flight.cc already stores map pixels, and
// consecutive samples are 4-neighbours by construction, so this is a plain
// walk with no line drawing between the points -- see kFlightPathLen in
// flight.h. Entries 0 .. count - 1 are the live ones whether or not the ring
// has wrapped.
static void _map_draw_path(void) {
  for (uint8_t i = 0; i < flight_path_count; ++i) {
    map_set_overlay_pixel(flight_path_px[i], flight_path_py[i]);
  }
}

// Places the active mission's navpoint digits. flight_init_from_mission()
// has already unpacked the waypoints into flight_nav_point_*, high byte =
// world unit, so the cell arithmetic is map.md section 4's, with the 180
// degree rotation applied to reach screen coordinates.
//
// Drawn ascending so that when two navpoints share a cell the higher number
// is the one left standing.
static void _map_draw_navpoints(void) {
  uint8_t n = flight_num_nav_points;
  if (n > kMapDigitCount) {
    n = kMapDigitCount;
  }
  for (uint8_t i = 0; i < n; ++i) {
    // x selects the row and y the column; + 4 centres cells on multiples
    // of 8 world units.
    const uint8_t row = ((uint8_t)((flight_nav_point_x[i] >> 8) + 0x04) >> 3) &
                        kWorldMapHeightMask;
    const uint8_t col = ((uint8_t)((flight_nav_point_y[i] >> 8) + 0x04) >> 3) &
                        kWorldMapWidthMask;
    _map_draw_stencil(kMapDigitMask[i],
                      kMapOriginRow + (kWorldMapHeight - 1) - row,
                      kMapOriginCol + (kWorldMapWidth - 1) - col);
  }
}

// --- Aircraft marker ------------------------------------------------------
//
// Sprites survive map mode: their bitmaps live at $D7C0..$DFFF, outside the
// $E000..$FF3F the map bitmap claims, so the pre-rotated instrument needles
// are still loaded and free to reuse. spr_multi is never set anywhere, so
// they are hires with an independent color each and can be light red without
// competing for a bit pair.
//
// The marker is two crossed long arms. Directions d and d ^ 16 share a
// bitmap_idx and differ only in pivot, so a bitmap is a line segment spanning
// between the two pivots and the pivot table only picks which end anchors to
// the hub. Placing the sprite by the *midpoint* of those two pivots therefore
// renders the whole 14 px segment instead of a half-length needle -- and that
// midpoint is (12, 10) for all 32 directions of both arm tables, so centring
// needs no table and no arithmetic.
static const uint8_t kMapSprBody = 0;
static const uint8_t kMapSprWing = 1;
static const uint8_t kMapSprCenterX = 12;
static const uint8_t kMapSprCenterY = 10;

// Sprite register coordinates of map pixel (0, 0). The registers are offset
// from the display by the 24 x 50 pixel border, and the marker is placed by
// its centre rather than its top left corner.
static const uint8_t kMapSprOriginX =
    kMapOriginCol * 8 + 24 - kMapSprCenterX; // 44
static const uint8_t kMapSprOriginY =
    kMapOriginRow * 8 + 50 - kMapSprCenterY; // 72

// Sprite pointers sit at video matrix + 1016, which in map mode is
// $D3F8..$D3FF -- RAM under I/O, so they are written in pass B with the rest
// of the screen RAM.
static const uint16_t kMapSprPtrOffset = 1016;

// Positions one sprite, handling the $D010 MSB. The map spans sprite x
// 44..298, so both sprites cross 255.
static void _map_set_sprite_pos(uint8_t idx, int16_t x, uint8_t y) {
  vic.spr_pos[idx].x = (uint8_t)x;
  vic.spr_pos[idx].y = y;
  if (x > 255) {
    vic.spr_msbx |= 1 << idx;
  } else {
    vic.spr_msbx &= ~(1 << idx);
  }
}

// Maps a WorldMapType to a tile index. Empty ground carries the dotted
// gridline, whose art has a two-cell period, so it picks one of the four
// grid variants by the parity of its *map* row and column -- matching
// tools/render_map_preview.py, which is the reference for what the finished
// screen should look like.
static uint8_t _map_tile_index(uint8_t cell, uint8_t row, uint8_t col) {
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
static void _map_draw_object_layer(void) {
  uint8_t *bm = kMapBitmap + kMapCharOffset * 8;
  uint8_t *cr = kColorRam + kMapCharOffset;
  for (uint8_t row = kWorldMapHeight; row-- != 0;) {
    for (uint8_t col = kWorldMapWidth; col-- != 0;) {
      const uint8_t idx = _map_tile_index(kWorldMap[row][col], row, col);
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
static void _map_draw_screen_layer(void) {
  uint8_t *sm = kMapScreenRam + kMapCharOffset;
  for (uint8_t row = kWorldMapHeight; row-- != 0;) {
    for (uint8_t col = kWorldMapWidth; col-- != 0;) {
      const uint8_t idx = _map_tile_index(kWorldMap[row][col], row, col);
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

  // Drop the instrument sprites; the aircraft marker turns two back on at the
  // end, once its pointers are in place.
  vic.spr_enable = 0x00;

  // Blank the display for the build. Three passes over 8000, 1000 and 1000
  // bytes are several frames long, and with DEN clear they show the black
  // border instead of the old character RAM reinterpreted as a bitmap --
  // and run faster, since a blanked screen has no badlines to steal cycles.
  vic.ctrl1 = 0x2b; // bitmap mode, screen off
  vic.ctrl2 = 0xd8; // multicolor
  vic_memptr = kVicMemMap;
  vic.color_back = kColorGreen; // bit pair 00, the ground everywhere
  vic.color_border = kColorBlack;

  // Pass A -- bitmap and color RAM.
  //
  // The 8-cell-wide surround outside the map is solid 11 over black color
  // RAM, the same idiom as kCharSolid11. Filling all 8000 bytes and then
  // overwriting the map area costs about a frame and is much simpler than
  // computing the non-contiguous border.
  memset(kMapBitmap, 0xFF, kMapBitmapSize);
  memset(kColorRam, kColorBlack, kMapScreenSize);
  _map_draw_object_layer();
  // Overlay, on top of the object art. Path first: a digit clears its own
  // cell's overlay before stencilling, so drawing it last keeps the trail
  // from filling in the glyph's counters.
  _map_draw_path();
  _map_draw_navpoints();
  _map_draw_compass();

  // The aircraft marker. _get_heading() runs 0..47 clockwise from north and
  // the sprite tables 0..31 clockwise from up, and the map puts north up, so
  // the conversion is just a change of scale: round(heading * 32 / 48), which
  // is (2 * heading + 1) / 3. map.md proposed a 48-byte lookup table to avoid
  // the divide, but this runs once per map_enter(), not per frame, so 48
  // bytes of the scarce region to save one division is the wrong way round.
  const uint8_t dir = (2 * flight_true_heading + 1) / 3;
  const sprite_meta_t *body = &kSpriteDefMetaLongArm[dir];
  const sprite_meta_t *wing = &kSpriteDefMetaLongArm[(dir + 8) & 0x1F];
  // pivot[d ^ 16] - centre is the fore vector at half the arm length, 7 px.
  // Halving it again puts the wing a little ahead of the body, which is what
  // makes the cross read as an aircraft rather than a plus sign -- and it
  // needs no table of its own.
  const sprite_meta_t *fore = &kSpriteDefMetaLongArm[dir ^ 16];
  const int8_t fx = (fore->pivot_x - kMapSprCenterX) >> 1;
  const int8_t fy = (fore->pivot_y - kMapSprCenterY) >> 1;

  const int16_t body_x = kMapSprOriginX + 2 * (int16_t)flight_map_px;
  const uint8_t body_y = kMapSprOriginY + flight_map_py;

  // Pass B -- screen RAM at $D000, which the CPU can only reach with I/O
  // banked out. Interrupts are already masked (see gfx_stop_raster_irqs()
  // above), so no handler can touch $D000..$DFFF while it is RAM.
  mmap_set(MMAP_RAM);
  memset(kMapScreenRam, kMapScreenSurround, kMapScreenSize);
  _map_draw_screen_layer();
  // Sprite pointers live past the 1000 bytes the memset covers, so only the
  // two in use are written; the other six are left as they are and stay
  // disabled.
  kMapScreenRam[kMapSprPtrOffset + kMapSprBody] = body->bitmap_idx;
  kMapScreenRam[kMapSprPtrOffset + kMapSprWing] = wing->bitmap_idx;
  mmap_set(MMAP_NO_ROM);

  // I/O is back, so the VIC registers are reachable again. The simulation is
  // frozen while the map is up, so this is the only time they are set.
  _map_set_sprite_pos(kMapSprBody, body_x, body_y);
  _map_set_sprite_pos(kMapSprWing, body_x + fx, body_y + fy);
  vic.spr_color[kMapSprBody] = kColorLightGreen;
  vic.spr_color[kMapSprWing] = kColorLightGreen;
  vic.spr_enable = (1 << kMapSprBody) | (1 << kMapSprWing);

  vic.ctrl1 = 0x3b; // bitmap mode, screen on

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
