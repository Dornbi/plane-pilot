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
#pragma section(data_box, 0, , , data)
#pragma section(data_compr, 0, , , data)
// Startup code is 0x0801 .. 0x0853, use everything before the VIC
// range as RAM. In theory we could use everything until kSpriteData
// but it overlaps with VIC control registers so we would have to
// switch back and forth.
#pragma region( main, 0x0860, 0xD000, , , {code, data, data_box, data_compr, bss, heap} )
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

// Sprites are switched off this many raster lines above the panel split, so
// this is also the last line in the viewport a sprite may *start* on.
//
// The switch to the panel in gfx.cc is cycle counted - nineteen NOPs and then
// three register writes that have to land in the blanking between raster 162
// and 163 - and oscar64's raster IRQ has no stabiliser, so every cycle the VIC
// steals on those two lines moves them. It steals two for each sprite whose
// data it is fetching, about nineteen for all eight, which is far wider than
// the window.
//
// Twenty-two, and unlike every earlier value here this one is derived rather
// than flown. **A sprite's DMA runs for 21 raster lines from the line its Y
// matches, and clearing $D015 does not stop one already in flight** - it only
// stops sprites that have not started yet. So the last line a sprite may begin
// on is 21 lines above the switch, plus one line of margin:
//
//     161 - 21 - 1 = 139, which is 22 lines above the split at 161.
//
// Measured in x64sc with seven sprites parked at a swept Y and a breakpoint on
// the handler's `sta $d018` (docs/clouds.md §1.8):
//
//     Y <= 139   lands exactly where it lands with no sprites at all
//     Y  = 140   up to 4 cycles late, and the last value that is harmless
//     Y  = 141   up to 55 cycles late
//     Y  = 142   ~70 late;  Y = 143, 144: ~85 late - the $d018 write misses
//                the badline on 163 and a whole character row of terrain
//                charset is drawn across the top of the panel
//
// One sprite alone in that band is 9 cycles late, which is inside the window
// but barely. That is why the old value of 17 - which let a sprite start as
// low as 144 - flickered only sometimes, and worse the more sprites were near
// the panel: the band that breaks it is five raster lines wide and objects
// pass through it.
static const uint8_t kSpritesOffLead = 22;
static const uint16_t kSpriteVisibleEndYPixels =
    kViewportEndYPixels - kSpritesOffLead;

#ifdef __OSCAR64__
static uint8_t *const kScreenRamMain = (uint8_t *)0xE800;
static uint8_t *const kScreenRamAlt = (uint8_t *)0xEC00;
static uint8_t *const kColorRam = (uint8_t *)0xD800;
#else
// Host builds point these at ordinary arrays, like sid.h and vic.h do for the
// chips. Nothing that builds on the host uses them in a static initializer,
// which is the one thing losing the constness would break.
extern uint8_t *kScreenRamMain;
extern uint8_t *kScreenRamAlt;
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

// $d011's DEN bit (0x10), or zero while a screen transition is rebuilding
// what is on display. Both mode setters below compose it into their $d011
// write, so a mem_init_mccm() in the middle of a transition leaves the screen
// blanked instead of switching it back on over half-built graphics.
// screen_blank() clears it; screen_unblank() and gfx_init_raster_irqs() put it
// back. The raster split's own $d011 writes are constants with DEN set and
// deliberately do not consult this - see screen.h.
//
// volatile, and that is not decoration. Without it this is an ordinary global
// that the optimizer may schedule freely against the $d011 writes it feeds:
// oscar64 was seen hoisting screen_unblank()'s store of it to before the
// mem_set_mccm_mode() that is supposed to read the blanked value, which would
// switch the display back on over a half-built page. volatile puts it in the
// same ordering class as the VIC registers it is composed into. It is read
// once per mode write and never in a loop, so it costs nothing measurable.
extern volatile uint8_t mem_den;

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

#ifdef __ENABLE_DEBUG__
extern bool mem_debug_enabled;
void mem_switch_debug(bool debug);
#else
static const bool mem_debug_enabled = false;
inline void mem_switch_debug(bool debug) {}
#endif

// vic.memptr should be volatile __memmap
#define vic_memptr (*((volatile __memmap byte *)0xD018))

#pragma compile("mem.cc")

#endif
