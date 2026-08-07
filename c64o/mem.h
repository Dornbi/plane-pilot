#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#include "bool.h"

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
#pragma region( stack, 0x0200, 0x0280, , , {stack} )
// Additional bss.
#pragma section(bss2, 0, , , bss)
#pragma region( bss2, 0x280, 0x800, , , {bss2} )
#pragma section(data_compr, 0, , , data)
// Startup code is 0x0801 .. 0x0853, use everything before the VIC
// range as RAM. In theory we could use everything until kSpriteData
// but it overlaps with VIC control registers so we would have to
// switch back and forth.
#pragma region( main, 0x0860, 0xD000, , , {code, data, data_compr, bss, heap} )
#endif
// Zero page. Wider than oscar64's 0x80..0xFF default, in both directions.
//
// Downward: the KERNAL and BASIC are banked out (MMAP_NO_ROM) and no KERNAL
// interrupt runs, so their zero page is ours - but oscar64's own runtime
// already lives down there. With -xz (extended zero page, see the Makefile)
// it lays out: 0x00-0x01 the 6510 port, 0x02-0x06 WORK, 0x0D-0x24 FPARAMS,
// 0x25 IP, 0x27 ACCU, 0x2B ADDR, 0x2F sp, 0x31 LOCALS, 0x33-0x52 TMP, and
// from 0x53 upward the spilled temporaries. That last area is the constraint:
// it grows with the call graph and is not bounded by the compiler, so the
// safe floor depends on the program rather than being a fixed address.
// 0x53 is the hard floor regardless - BC_REG_TMP_SAVED is compiled into
// oscar64 and no pragma moves it.
//
// The measured high water mark for the spilled temporaries is 0x5A (ppilot;
// 0x58 polydemo, 0x54 vecdemo and vectest), so 0x60 leaves a few bytes of
// headroom. Nothing checks this: if the spill area ever reaches into the
// region, oscar64 silently writes over the globals allocated here. That is
// what tools/check_zeropage.py guards, and c64o/Makefile runs it on every
// build - if it fails, raise this start address, do not lower it.
//
// Upward: the end is exclusive, so the 0xFF the default spelling implies is
// never actually allocated. 0x100 claims it. The startup code clears the
// region with an X loop that compares against the low byte of the end, and
// wrapping to 0 terminates it correctly.
#pragma region( zeropage, 0x60, 0x100, , , {zeropage} )

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
#ifdef __OSCAR64__
static uint8_t *const kColorRam = (uint8_t *)0xD800;
#else
extern uint8_t *kColorRam;
#endif

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

// Sets the VIC-II control registers for Multicolor Character Mode (MCCM),
// without touching the background/multi-color registers. Used by screens
// that want MCCM text but not the simulation's terrain colors (menu, help).
void mem_set_mccm_mode(void);

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
