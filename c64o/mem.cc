#include "mem.h"

#include <string.h>

#ifdef __OSCAR64__
#include <c64/memmap.h>
#include <oscar.h>
#else
#define MMAP_ROM 0x37
#define MMAP_NO_BASIC 0x36
#define MMAP_NO_ROM 0x35
#define MMAP_RAM 0x30
#define MMAP_CHAR_ROM 0x31
#define MMAP_ALL_ROM 0x33
static void mmap_trampoline() {}
static char mmap_set(char pla) { return pla; }
static const char *oscar_expand_lzo(char *dp, const char *sp) { return sp; }
#endif

#include "benchmark.h"
#include "chardefs.h"
#include "color.h"
#include "gfx.h"
#include "sprites.h"
#include "vic.h"
#include "view.h"

#pragma bss(bss2)
static uint8_t *const kSpriteData = (uint8_t *)0xD7C0;

uint8_t mem_box_char_start;
bool mem_debug_enabled;
bool mem_using_alt_buffer;

static uint8_t *const kScreenRowPtrsMain[kScreenHeight] = {
    kScreenRamMain + kScreenWidth * 0,  kScreenRamMain + kScreenWidth * 1,
    kScreenRamMain + kScreenWidth * 2,  kScreenRamMain + kScreenWidth * 3,
    kScreenRamMain + kScreenWidth * 4,  kScreenRamMain + kScreenWidth * 5,
    kScreenRamMain + kScreenWidth * 6,  kScreenRamMain + kScreenWidth * 7,
    kScreenRamMain + kScreenWidth * 8,  kScreenRamMain + kScreenWidth * 9,
    kScreenRamMain + kScreenWidth * 10, kScreenRamMain + kScreenWidth * 11,
    kScreenRamMain + kScreenWidth * 12, kScreenRamMain + kScreenWidth * 13,
    kScreenRamMain + kScreenWidth * 14, kScreenRamMain + kScreenWidth * 15,
    kScreenRamMain + kScreenWidth * 16, kScreenRamMain + kScreenWidth * 17,
    kScreenRamMain + kScreenWidth * 18, kScreenRamMain + kScreenWidth * 19,
    kScreenRamMain + kScreenWidth * 20, kScreenRamMain + kScreenWidth * 21,
    kScreenRamMain + kScreenWidth * 22, kScreenRamMain + kScreenWidth * 23,
    kScreenRamMain + kScreenWidth * 24};

static uint8_t *const kScreenRowPtrsAlt[kScreenHeight] = {
    kScreenRamAlt + kScreenWidth * 0,  kScreenRamAlt + kScreenWidth * 1,
    kScreenRamAlt + kScreenWidth * 2,  kScreenRamAlt + kScreenWidth * 3,
    kScreenRamAlt + kScreenWidth * 4,  kScreenRamAlt + kScreenWidth * 5,
    kScreenRamAlt + kScreenWidth * 6,  kScreenRamAlt + kScreenWidth * 7,
    kScreenRamAlt + kScreenWidth * 8,  kScreenRamAlt + kScreenWidth * 9,
    kScreenRamAlt + kScreenWidth * 10, kScreenRamAlt + kScreenWidth * 11,
    kScreenRamAlt + kScreenWidth * 12, kScreenRamAlt + kScreenWidth * 13,
    kScreenRamAlt + kScreenWidth * 14, kScreenRamAlt + kScreenWidth * 15,
    kScreenRamAlt + kScreenWidth * 16, kScreenRamAlt + kScreenWidth * 17,
    kScreenRamAlt + kScreenWidth * 18, kScreenRamAlt + kScreenWidth * 19,
    kScreenRamAlt + kScreenWidth * 20, kScreenRamAlt + kScreenWidth * 21,
    kScreenRamAlt + kScreenWidth * 22, kScreenRamAlt + kScreenWidth * 23,
    kScreenRamAlt + kScreenWidth * 24};

uint8_t *mem_screen_ram;
uint8_t *mem_screen_row_ptrs[kScreenHeight];

// uint8_t mem_color_buffer[kViewportWidth * kViewportHeight];
// Reuse kSpriteDataCompressed.
uint8_t *const mem_color_buffer = (uint8_t *const)kSpriteDataCompressed;
uint8_t *const mem_color_row_ptrs[kViewportHeight] = {
    kSpriteDataCompressed + kViewportWidth * 0,
    kSpriteDataCompressed + kViewportWidth * 1,
    kSpriteDataCompressed + kViewportWidth * 2,
    kSpriteDataCompressed + kViewportWidth * 3,
    kSpriteDataCompressed + kViewportWidth * 4,
    kSpriteDataCompressed + kViewportWidth * 5,
    kSpriteDataCompressed + kViewportWidth * 6,
    kSpriteDataCompressed + kViewportWidth * 7,
    kSpriteDataCompressed + kViewportWidth * 8,
    kSpriteDataCompressed + kViewportWidth * 9,
    kSpriteDataCompressed + kViewportWidth * 10,
    kSpriteDataCompressed + kViewportWidth * 11,
    kSpriteDataCompressed + kViewportWidth * 12,
    kSpriteDataCompressed + kViewportWidth * 13};

// cia2.pra should be volatile __memmap
#define cia2_pra (*((volatile __memmap byte *)0xDD00))

// VIC Memory Pointers:
// - Bank starts at $C000
// - Character ram at $2000 offset from bank start (bits 1..3)
// - Don't use character ROM (bit 3))
// - Screen ram at $3800 offset from bank start (bits 4..7)
// static uint8_t *const kScreenRamMain = (uint8_t *)0xE800;
static const uint8_t kVicMemScreenMain = 0xA8;

// VIC Memory Pointers:
// - Bank starts at $C000
// - Character ram at $2000 offset from bank start (bits 1..3)
// - Don't use character ROM (bit 3))
// - Screen ram at $3C00 offset from bank start (bits 4..7)
// static uint8_t *const kScreenRamAlt = (uint8_t *)0xEC00;
static const uint8_t kVicMemScreenAlt = 0xB8;

// Only mem_use_main_buffer() writes $d018 from here, and only to establish a
// starting state. Per frame the register belongs to gfx.cc: _switch_to_panel_top
// forces the alt value at raster 161 so the panel bitmap always reads its colors
// from $EC00, and _switch_to_terrain puts the double-buffered value back at 250.
// mem_switch_buffer() deliberately does not touch it - a write of its own would
// have to land inside the panel and would repoint the panel's video matrix
// mid-screen. Toggling mem_using_alt_buffer and letting raster 250 latch it is
// the same flip, one frame-accurate instead of two writes racing.

// Startup and mode-switch helpers; none of these run per frame.
#pragma optimize(push, outline)

void mem_init(void) {
  mmap_trampoline();

  __asm {
    sei;
  }

  // CIA2 Port A: Set Bank 3 ($C000-$FFFF)
  //   Bits 0-1 of $DD00: 00 = Bank 3, 01 = Bank 2, 10 = Bank 1, 11 = Bank 0
  cia2_pra &= 0xFC;

  // No BASIC or KERNAL or I/O, but can copy CHAR ROM
  char prev = mmap_set(MMAP_CHAR_ROM);

  // Expand sprite data to the $D000 range.
  mmap_set(MMAP_RAM);
  oscar_expand_lzo((char *)kSpriteData, kSpriteDataCompressed);

  // I/O and COLOR RAM in $d000-$dfff block, rest is RAM
  mmap_set(MMAP_NO_ROM);
  __asm {
    cli;
  }

  //  Fill Color buffer with sky color.
  memset(mem_color_buffer, kColorSky | 0x08, kViewportWidth * kViewportHeight);

  // Fill Color RAM ($D800) with kColorBg.
  // Top part in multicolor mode.
  if (kViewportWidth < kScreenWidth) {
    memset(kColorRam, kColorBg | 0x08, kScreenWidth * kViewportEndY);
  }
  memset(kScreenRamMain + kViewportHeight * kScreenWidth, kCharSolid11,
         (kScreenHeight - kViewportHeight) * kScreenWidth);

  view_state = VIEW_CENTER;
  view_refresh_panel();

  mem_using_alt_buffer = false;
  mem_debug_enabled = false;
}

#pragma optimize(pop)

static void _copy_color_ram(void) {
  uint8_t *dst = kColorRam + kViewportStartX;
  const uint8_t *src = mem_color_buffer;
  uint8_t x = kViewportWidth - 1;
  do {
#pragma unroll(full)
    for (uint8_t y = 0; y < kViewportHeight; ++y) {
      dst[x + y * kScreenWidth] = src[x + y * kViewportWidth];
    }
  } while (x--);
}

void mem_switch_buffer(void) {
  gfx_wait_flip_window();

  // The toggle goes before the copy, and the order is load-bearing. Only one
  // of the two has a near deadline: _switch_to_terrain() latches
  // mem_using_alt_buffer into $d018 at raster 250, and if the toggle missed
  // that latch the frame would be shown with the previous buffer's characters
  // and this frame's colors. The copy has until the next frame's first
  // viewport line, hundreds of cycles later, so it is the one that waits.
  //
  // mem_using_alt_buffer is also the only thing here the interrupt reads, and
  // it is a single byte, so the store is atomic on its own. That is what the
  // sei/cli pair around this used to be for. The three below are main-line
  // state that no handler touches, and they can take as long as they like.
  mem_using_alt_buffer = !mem_using_alt_buffer;

  if (mem_using_alt_buffer) {
    // Now rendering into the alt buffer; _switch_to_terrain() will put the
    // main buffer on screen.
    mem_screen_ram = kScreenRamAlt;
    mem_screen_row_ptrs = kScreenRowPtrsAlt;
    mem_box_char_start = 0x61;
  }
  else {
    mem_screen_ram = kScreenRamMain;
    mem_screen_row_ptrs = kScreenRowPtrsMain;
    mem_box_char_start = 0x01;
  }

  bm_view_start();
  _copy_color_ram();
  bm_view_end(950, "COL:");
}

// Startup and mode-switch helpers; none of these run per frame.
#pragma optimize(push, outline)

void mem_use_main_buffer(void) {
  __asm {
    sei;
  }
  vic_memptr = kVicMemScreenMain;
  mem_screen_ram = kScreenRamMain;
  mem_screen_row_ptrs = kScreenRowPtrsMain;
  mem_box_char_start = 0x01;
  mem_using_alt_buffer = false;
}

inline void mem_set_mccm_mode(void) {
  // Switch to multicolor character mode
  vic.ctrl1 = 0x1b;
  vic.ctrl2 = 0xd8;
}

void mem_init_mccm(void) {
  vic.color_border = kColorBg;
  vic.color_back = kColorGrad2;
  vic.color_back1 = kColorGround;
  vic.color_back2 = kColorGrndObj;
  mem_set_mccm_mode();
}

void mem_init_mcbm(void) {
  vic.color_back = kColorPanelBg;
  vic.ctrl1 = 0x3b;
  vic.ctrl2 = 0xd8;
}

__noinline void mem_clear_screen(void) {
  // Fill Screen RAM with character 66 (Border/Solid)
  memset(mem_screen_ram, kCharSolid11, kViewportHeight * kScreenWidth);
}

void mem_switch_debug(bool debug) {
  mem_debug_enabled = debug;
  if (debug) {
    mem_init_mccm();
    memset(kColorRam + kScreenWidth * kViewportEndY, kColorBg,
           kScreenWidth * (kScreenHeight - kViewportEndY));
    // Reset the bottom rows of both screen buffers: the debug text area
    // alternates between them, and the map view may have overwritten the
    // main buffer's rows (originally set by mem_init).
    memset(kScreenRamMain + kViewportHeight * kScreenWidth, kCharSolid11,
           (kScreenHeight - kViewportHeight) * kScreenWidth);
    memset(kScreenRamAlt + kViewportHeight * kScreenWidth, kCharSolid11,
           (kScreenHeight - kViewportHeight) * kScreenWidth);
  } else {
    view_refresh_panel();
  }
}

#pragma optimize(pop)
