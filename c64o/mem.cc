#include "mem.h"
#include "panel.h"

#include <string.h>

#ifdef __OSCAR64__
#include <c64/memmap.h>
#include <c64/rasterirq.h>
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

#define RIRQ_SIZE 2
typedef struct RIRQCode {
  uint8_t size;
  uint8_t code[RIRQ_SIZE];
} RIRQCode;

static void rirq_init(bool kernalIRQ) {}
static void rirq_build(RIRQCode *ic, uint8_t size) {}
static void rirq_call(RIRQCode *ic, uint8_t n, void *addr) {}
static void rirq_set(uint8_t n, uint8_t raster, RIRQCode *ic) {}
static void rirq_sort(void) {}
static void rirq_start(void) {}
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

static bool _mem_use_alt_buffer;

// cia2.pra should be volatile __memmap
#define cia2_pra (*((volatile __memmap byte *)0xDD00))

// vic.memptr should be volatile __memmap
#define vic_memptr (*((volatile __memmap byte *)0xD018))

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

  _mem_use_alt_buffer = false;
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
  if (_mem_use_alt_buffer) {
    // Switch to main buffer.
    vic_memptr = kVicMemScreenAlt;
    mem_screen_ram = kScreenRamMain;
    mem_box_char_start = 0x02;
  }
  else {
    // Switch to alt buffer.
    vic_memptr = kVicMemScreenMain;
    mem_screen_ram = kScreenRamAlt;
    mem_box_char_start = 0x62;
  }
  __asm {
    cli;
  }
  _mem_use_alt_buffer = !_mem_use_alt_buffer;
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

#pragma optimize(push, noasm)
static void _switch_to_panel() {
  if (mem_debug_enabled) {
    sprites_show_no_sprites();
    return;
  }
#assign num_nop 16
#repeat
  __asm {
      nop;
  }
#assign num_nop num_nop - 1
#until num_nop == 0
#undef num_nop

  // clang-format off
  __asm {
    lda #$b8;
    sta $d018;
    lda #$3b;
    sta $d011;
    lda #$00;
    sta $d021;
  }
/*
    // Hide sun immediately after mode switch.
    lda #$00;
    sta $d00e;
    lda $d010;
    and #$7f;
    sta $d010;
  }
*/
  // clang-format on
  sprites_show_no_sprites();
}

static void _switch_to_instruments() {
  sprites_show_instrument_sprites();
}

static void _switch_to_terrain() {
  sprites_show_terrain_sprites();
  vic.color_back = kColorGrad2;
  vic.ctrl1 = 0x1b; // Multicolor character mode
  vic_memptr = _mem_use_alt_buffer ? 0xA8 : 0xB8;
}

#pragma optimize(pop)

RIRQCode to_panel, to_instruments, to_terrain;

void mem_init_rirq(void) {
  rirq_init(/*kernalIRQ=*/false);
  rirq_build(&to_panel, 1);
  rirq_call(&to_panel, 0, (void *)_switch_to_panel);
  rirq_set(0, kRasterScreenYStart + kViewportEndYPixels - 1, &to_panel);
  rirq_build(&to_instruments, 1);
  rirq_call(&to_instruments, 0, (void *)_switch_to_instruments);
  rirq_set(1, kRasterScreenYStart + kViewportEndYPixels +24, &to_instruments);
  rirq_build(&to_terrain, 1);
  rirq_call(&to_terrain, 0, (void *)_switch_to_terrain);
  rirq_set(2, kRasterScreenYStart + kScreenHeightPixels, &to_terrain);
  rirq_sort();
  rirq_start();
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
