#ifndef MEM_H
#define MEM_H

#include "bool.h"
#include <stdint.h>

#ifndef __OSCAR64__
#define __memmap
#define __noinline
#define __zeropage
#endif

// We barely use the stack, make it smaller than the default.
#pragma stacksize(0x80)
#ifdef __MAX_RAM__
// Since the screen ram is moved to 0xE800 and 0xEC00, we can
// use the original location for stack.
#pragma region( stack, 0x0400, 0x0480, , , {stack} )
// Additional bss.
#pragma section(bss2, 0, , , bss)
#pragma region( bss2, 0x480, 0x800, , , {bss2} )
// Startup code is 0x0801 .. 0x0853, use everything before the VIC
// range as RAM. In theory we could use everything until kSpriteData
// but it overlaps with VIC control registers so we would have to
// switch back and forth.
#pragma region( main, 0x0860, 0xD000, , , {code, data, bss, heap} )
#endif
// Region 0x5F..0xFF should be possible, except that the irq trap
// handles 0x80. This probably depends on that the irq routines
// should not touch anything below 0x80.
#pragma region( zeropage, 0x80, 0xFF, , , {zeropage} )

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
static const uint8_t kRasterScreenYStart = 50;

static uint8_t *const kScreenRamMain = (uint8_t *)0xE800;
static uint8_t *const kScreenRamAlt = (uint8_t *)0xEC00;
static uint8_t *const kColorRam = (uint8_t *)0xD800;

// Current screen ram.
extern uint8_t *mem_screen_ram;
// Multiply by 40 for screen row offset for the viewport.
extern uint8_t *mem_screen_row_ptrs[kScreenHeight];

// Color buffer. Unlike the screen ram, this is fixed.
// extern uint8_t mem_color_buffer[kViewportWidth * kViewportHeight];
extern uint8_t *const mem_color_buffer;
extern uint8_t *const mem_color_row_ptrs[kViewportHeight];

// The starting character for the box characters.
extern uint8_t mem_box_char_start;

// Sets CHAR_RAM and SCREEN_RAM to start at 0xC000 and 0xC800 respectively.
// @result screen_ram
void mem_init(void);

// Switches SCREEN_RAM back and forth between 0xC800 and 0xCC00.
// @result screen_ram
extern bool mem_using_alt_buffer;
void mem_switch_buffer(void);
void mem_use_main_buffer(void);

// Initializes the VIC-II for Multicolor Character Mode (MCCM).
// Sets the background and multi-color registers.
void mem_init_mccm(void);
void mem_init_mcbm(void);

// Initializes the solid characters used for Ground, Sky, and Border.
// Maps to characters 64, 65, and 66.
void mem_init_char_ram(void);

// Fills mem_screen_ram with character 66 (Border)
// and the Color RAM at $D800 with Black.
void mem_clear_screen(void);

extern bool mem_debug_enabled;
void mem_switch_debug(bool debug);

// vic.memptr should be volatile __memmap
#define vic_memptr (*((volatile __memmap byte *)0xD018))

#pragma compile("mem.cc")

#endif
