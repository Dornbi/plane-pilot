#include "mem.h"
#include "panel.h"

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
#include "sprites.h"
#include "vic.h"

static uint8_t *const kColorRam = (uint8_t *)0xD800;
static uint8_t *const kSpriteData = (uint8_t *)0xD7C0;

uint8_t *mem_screen_ram;
uint8_t mem_box_char_start;
uint8_t mem_color_buffer[kViewportWidth * kViewportHeight];
bool mem_debug_enabled;
bool mem_using_alt_buffer;

static uint8_t *const kScreenRowPtrsMain[kViewportHeight] = {
    kScreenRamMain + 0,   kScreenRamMain + 40,  kScreenRamMain + 80,
    kScreenRamMain + 120, kScreenRamMain + 160, kScreenRamMain + 200,
    kScreenRamMain + 240, kScreenRamMain + 280, kScreenRamMain + 320,
    kScreenRamMain + 360, kScreenRamMain + 400, kScreenRamMain + 440,
    kScreenRamMain + 480, kScreenRamMain + 520};

static uint8_t *const kScreenRowPtrsAlt[kViewportHeight] = {
    kScreenRamAlt + 0,   kScreenRamAlt + 40,  kScreenRamAlt + 80,
    kScreenRamAlt + 120, kScreenRamAlt + 160, kScreenRamAlt + 200,
    kScreenRamAlt + 240, kScreenRamAlt + 280, kScreenRamAlt + 320,
    kScreenRamAlt + 360, kScreenRamAlt + 400, kScreenRamAlt + 440,
    kScreenRamAlt + 480, kScreenRamAlt + 520};

uint8_t *mem_screen_row_ptrs[kViewportHeight];

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

static inline void _init_single_point_chars() {
  static const uint8_t alt_lines[] = {0x95, 0x65, 0x59, 0x56};
  uint8_t *p = kCharRam + kSinglePointCharStart * 8;
  for (uint8_t y = 0; y < 4; ++y) {
    for (uint8_t x = 0; x < 4; ++x) {
      for (uint8_t i = 0; i < 8; ++i) {
        p[i] = 0x55;
      }
      p[y * 2] = alt_lines[x];
      p[y * 2 + 1] = alt_lines[x];
      p += 8;
    }
  }
}

static void _expand_panel_screen_color() {
  oscar_expand_lzo((char *)0xee30, kPanelScreenCompressed);
  oscar_expand_lzo((char *)0xda30, kPanelColorCompressed);
}

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
  // Copy 2k from ROM ($D000-$D7FF) to RAM ($C000-$C7FF)
  memcpy(kCharRam, (const uint8_t *)0xD800, 0x0800);

  // Expand sprite data to the $D000 range.
  mmap_set(MMAP_RAM);
  oscar_expand_lzo((char *)kSpriteData, kSpriteDataCompressed);

  // I/O and COLOR RAM in $d000-$dfff block, rest is RAM
  mmap_set(MMAP_NO_ROM);
  __asm {
    cli;
  }

  memset(kCharRam + kCharSolidGround * 8, 0x55, 8);
  memset(kCharRam + kCharSolidGrad1 * 8, 0xAA, 8);
  memset(kCharRam + kCharSolid11 * 8, 0xFF, 8);
  _init_single_point_chars();

  //  Fill Color buffer with sky color.
  memset(mem_color_buffer, kColorSky | 0x08, sizeof(mem_color_buffer));

  // Fill Color RAM ($D800) with kColorBg.
  // Top part in multicolor mode.
  memset(kColorRam, kColorBg | 0x08, kScreenWidth * kViewportEndY);
  memset(kScreenRamMain + kViewportHeight * kScreenWidth, kCharSolid11,
         (kScreenHeight - kViewportHeight) * kScreenWidth);

  oscar_expand_lzo((char *)0xF000, kPanelBitmapCompressed);
  _expand_panel_screen_color();

  mem_using_alt_buffer = false;
  mem_debug_enabled = false;

  sprites_set_speed(0);
  // sprites_show(true);
}

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

static void _wait_vsync() {
  // Wait for the raster to reach line 255
  while (vic.raster != 255)
    ;
  // Wait for it to leave line 255 to avoid double-triggering
  while (vic.raster == 255)
    ;
}

void mem_switch_buffer(void) {
  _wait_vsync();

  bm_horiz_start();
  _copy_color_ram();
  bm_horiz_end(910, SCREEN_STR("COL:"));

  __asm {
    sei;
  }
  if (mem_using_alt_buffer) {
    // Switch to main buffer.
    vic_memptr = kVicMemScreenAlt;
    mem_screen_ram = kScreenRamMain;
    mem_screen_row_ptrs = kScreenRowPtrsMain;
    mem_box_char_start = 0x02;
  }
  else {
    // Switch to alt buffer.
    vic_memptr = kVicMemScreenMain;
    mem_screen_ram = kScreenRamAlt;
    mem_screen_row_ptrs = kScreenRowPtrsAlt;
    mem_box_char_start = 0x62;
  }
  __asm {
    cli;
  }
  mem_using_alt_buffer = !mem_using_alt_buffer;
}

void mem_init_mccm(void) {
  vic.color_border = kColorBg;
  vic.color_back = kColorGrad2;
  vic.color_back1 = kColorGround;
  vic.color_back2 = kColorGrndObj;
  // Switch to multicolor character mode
  vic.ctrl1 = 0x1b;
  vic.ctrl2 = 0xd8;
}

void mem_init_mcbm(void) {
  vic.color_back = kColorPanelBg;
  vic.ctrl1 = 0x3b;
  vic.ctrl2 = 0xd8;
}

void mem_clear_screen(void) {
  // Fill Screen RAM with character 66 (Border/Solid)
  memset(mem_screen_ram, kCharSolid11, kViewportHeight * kScreenWidth);
}

void mem_switch_debug(bool debug) {
  if (debug) {
    mem_init_mccm();
    memset(kColorRam + kScreenWidth * kViewportEndY, kColorBg,
           kScreenWidth * (kScreenHeight - kViewportEndY));
    memset(kScreenRamAlt + kViewportHeight * kScreenWidth, kCharSolid11,
           (kScreenHeight - kViewportHeight) * kScreenWidth);
  } else {
    _expand_panel_screen_color();
  }
  mem_debug_enabled = debug;
}
