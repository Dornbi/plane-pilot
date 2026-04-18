#ifndef MEM_H
#define MEM_H

#include "bool.h"
#include <stdint.h>

#ifndef __OSCAR64__
#define __memmap
#define __zeropage
#endif

static const uint8_t kScreenWidth = 40;
static const uint8_t kScreenHeight = 25;
static const uint16_t kScreenWidthPixels = kScreenWidth * 8;
static const uint16_t kScreenHeightPixels = kScreenHeight * 8;

static const uint8_t kViewportWidth = 40;
static const uint16_t kViewportWidthPixels = kViewportWidth * 8;
static const uint8_t kViewportHeight = 14;
static const uint16_t kViewportHeightPixels = kViewportHeight * 8;
static const uint8_t kViewportStartX = (kScreenWidth - kViewportWidth) / 2;
static const uint8_t kViewportEndX = kViewportStartX + kViewportWidth;
static const uint8_t kViewportStartY = 0;
static const uint8_t kViewportEndY = kViewportStartY + kViewportHeight;
static const uint16_t kViewportStartYPixels = kViewportStartY * 8;
static const uint16_t kViewportEndYPixels = kViewportEndY * 8;

static uint8_t *const kCharRam = (uint8_t *)0xE000;
static const uint8_t kSinglePointCharStart = 128;
static const uint8_t kRasterScreenYStart = 50;

static uint8_t *const kScreenRamMain = (uint8_t *)0xE800;
static uint8_t *const kScreenRamAlt = (uint8_t *)0xEC00;

extern uint8_t *mem_screen_ram;
extern uint8_t mem_box_char_start;
extern uint8_t mem_color_buffer[kViewportWidth * kViewportHeight];

// Sets CHAR_RAM and SCREEN_RAM to start at 0xC000 and 0xC800 respectively.
// @result screen_ram
void mem_init(void);

// Switches SCREEN_RAM back and forth between 0xC800 and 0xCC00.
// @result screen_ram
void mem_switch_buffer(void);

// Initializes the VIC-II for Multicolor Character Mode (MCCM).
// Sets the background and multi-color registers.
void mem_init_mccm(void);
void mem_init_mcbm(void);

// Initializes the raster IRQ to switch between multicolor bitmap and character
// mode.
void mem_init_rirq(void);

// Initializes the solid characters used for Ground, Sky, and Border.
// Maps to characters 64, 65, and 66.
void mem_init_char_ram(void);

// Fills mem_screen_ram with character 66 (Border)
// and the Color RAM at $D800 with Black.
void mem_clear_screen(void);

extern bool mem_debug_enabled;
void mem_switch_debug(bool debug);

#pragma compile("mem.cc")

#endif
