#include "cpu.h"
#include "gfx.h"

#include <string.h>

#include "chardefs.h"
#include "color.h"
#include "fmath.h"
#include "gun.h"
#include "mem.h"
#include "sound.h"
#include "sprites.h"
#include "vec.h"
#include "vic.h"
#include "view.h"

#ifdef __OSCAR64__
#include <c64/rasterirq.h>
// The split uses three raster interrupts, slots 0-2 in gfx_init_raster_irqs(),
// and the Makefile's -DNUM_IRQS=3 sizes the library's five tables for just
// those rather than its default sixteen. On the command line, not here:
// oscar64 compiles rasterirq.c without this file's defines.
static_assert(NUM_IRQS == 3, "gfx_init_raster_irqs() sets slots 0-2");
#include <oscar.h>
#else
#define RIRQ_SIZE 2
typedef struct RIRQCode {
  uint8_t size;
  uint8_t code[RIRQ_SIZE];
} RIRQCode;

static void _gfx_rirq_init(void) {}
static void rirq_build(RIRQCode *ic, uint8_t size) {}
static void rirq_call(RIRQCode *ic, uint8_t n, void *addr) {}
static void rirq_write(RIRQCode *ic, uint8_t n, void *addr, uint8_t data) {}
static void rirq_start(void) {}
static void rirq_stop(void) {}

static const char *oscar_expand_lzo(char *dp, const char *sp) { return sp; }
#endif

#pragma data(data_compr)
const char kGfxCharsCompressed[] = {
#embed 1792 lzo "gfx_chars.bin"
};
#pragma data(data)

// Raster IRQ handlers: cycle-counted (note the NOP padding below), so keep
// the outliner out of them as well.
#pragma optimize(push, noasm, nooutline)

// The padding in the two handlers below, in NOPs. It is what puts the three
// register writes in the horizontal blanking between rasters 162 and 163, so it
// is measured on a raster and never reasoned about - docs/clouds.md §1.8 and
// docs/supercpu.md.
//
// **Two numbers, because one cannot serve both machines.** Swept on the split
// itself, the window is ten NOPs wide on either machine and the two barely
// overlap: a stock C64 draws the frame correctly from **8 to 17** and xscpu64
// from **15 to 24**, because the interrupt entry and oscar64's rirq dispatcher
// ahead of the handler still run at 20 MHz on the accelerator. Any shared value
// is within a NOP or two of one machine's edge, and a NOP or two is what let a
// stock C64 tear one frame in a few hundred. docs/supercpu.md named this as the
// way out if the two ever needed different values; they do.
//
// 12 and 19 are the middles, with four NOPs of margin on the early side and
// five on the late one, which is the side the interrupt's entry jitter wanders
// towards.
//
// The choice is made once, in gfx_init_raster_irqs(), from the boot probe: two
// handlers, one rirq_call, no run-time branch inside a cycle-counted handler.
#ifndef GFX_PANEL_NOPS
#define GFX_PANEL_NOPS 12
#endif
#ifndef GFX_PANEL_NOPS_FAST
#define GFX_PANEL_NOPS_FAST 19
#endif

// The writes, and everything after them. Shared by both handlers so that the
// padding is the only difference between them; __noinline so the JSR is in both
// and the two counts stay comparable.
//
// The three values are loaded and *then* stored, rather than in load/store
// pairs. That is not tidying: all three have the same deadline, which is not
// "the blanking" in general but raster 163's badline - the VIC starts fetching
// the panel's first character row at cycle 15 and shifts its first pixel out at
// ~16, and $D018's video matrix, $D011's BMM bit and $D021 all have to be in
// before that. Three lda/sta pairs span the deadline over eighteen cycles;
// three loads and three stores span it over twelve. Six cycles of the window,
// for no bytes and no branch.
static __noinline void _gfx_panel_top_writes(void) {
  // clang-format off
  __asm {
    lda #$b8;
    ldx #$3b;
    ldy #$00;
    sta $d018;
    stx $d011;
    sty $d021;
  }
  // clang-format on
  sprites_show_panel_top_sprites();
  cpu_speed_fast();
}

static void _gfx_switch_to_panel_top() {
  // 1 MHz for the length of this handler, and only this handler. On a 20 MHz
  // accelerator the padding below would otherwise take a twentieth of the time
  // and the writes would miss the blanking, drawing one raster line of the
  // panel in the terrain's colours.
  //
  // This is not "run the game slower": the handler takes ~19 us either way, so
  // against leaving it at 20 MHz it costs about 18 us of a 19,656 us frame -
  // under a tenth of a percent, and the frame rate is unchanged.
  cpu_speed_slow();
#ifdef __ENABLE_DEBUG__
  if (mem_debug_enabled) {
    sprites_show_no_sprites();
    return;
  }
#else
  // Stands in for the test above, which is `lda zp / bne` not taken: five
  // cycles, so that the writes land where they do in a debug build, which is
  // where GFX_PANEL_NOPS was measured. Three NOPs were six, and put them a
  // cycle late - measured on the split's `sta $d018`, 53..58 of line 162 on
  // x64sc became 54..59, and 55 on xscpu64 became 56. The jmp to the next
  // instruction is the three: it reads nothing and sets no flags, where any
  // three-cycle read would be a zero page one, which check_irq_zp.py rightly
  // refuses in a handler.
  __asm {
    nop;
    jmp next;
  next:
  }
#endif
#assign num_nop GFX_PANEL_NOPS
#repeat
  __asm {
      nop;
  }
#assign num_nop num_nop - 1
#until num_nop == 0
#undef num_nop
  _gfx_panel_top_writes();
}

// The same handler for a machine whose interrupt entry runs faster than its
// padding does. Only the count differs; see the comment on the two constants.
static void _gfx_switch_to_panel_top_fast() {
  cpu_speed_slow();
#ifdef __ENABLE_DEBUG__
  if (mem_debug_enabled) {
    sprites_show_no_sprites();
    return;
  }
#else
  // Five cycles, as above.
  __asm {
    nop;
    jmp next;
  next:
  }
#endif
#assign num_nop GFX_PANEL_NOPS_FAST
#repeat
  __asm {
      nop;
  }
#assign num_nop num_nop - 1
#until num_nop == 0
#undef num_nop
  _gfx_panel_top_writes();
}

static void _switch_to_panel_bottom() {
  if (mem_debug_enabled) {
    return;
  }
  sprites_show_panel_bottom_sprites();
}

static void _gfx_switch_to_terrain() {
  sprites_show_terrain_sprites();
  vic.color_back = COLOR_GRAD2;
  vic.ctrl1 = 0x1b; // Multicolor character mode
  vic_memptr = mem_using_alt_buffer ? 0xA8 : 0xB8;

  // Last, and only after the $d018 latch above, which is the one write here
  // with a deadline. The blit is ~260 cycles, four raster lines, on the one
  // tick a frame that has a new shadow to copy, and 7 cycles on the others.
  // Nothing waits on a raster line past this one - gfx_wait_flip_window()
  // closes at 242 - so overrunning only delays the blit itself.
  sound_blit();
}

#ifdef __OSCAR64__
// The interrupt entry: oscar64's rirq_isr_ram_io from c64/rasterirq.c, copied
// so that it can bank I/O in for the handlers and put $01 back afterwards.
//
// The library's entry assumes I/O is already visible, so the main line could
// only bank it out - to reach the RAM under $D000-$DFFF - with interrupts
// masked, and a masked split is a torn panel. This one saves whatever the
// interrupted code left in $01, runs the handlers with $35 (I/O in, both ROMs
// out, which is what mem_init() sets) and restores the saved value before the
// RTI. So a main line that wants the RAM under I/O just writes $01 and carries
// on: an interrupt in the middle sees I/O, and hands the RAM back when it is
// done. That is what lets the title aircraft live at $D000 (mem.h), and it is
// the premise of the opponent sprites' buffers in docs/planes.md §5.
//
// It moves no handler, which is not obvious and matters for the panel-top
// padding above. The dispatcher arms each interrupt a line early, and every
// RIRQCode that rirq_build() generates opens with a `cmp $d012 / bcs` loop that
// spins until its own line starts - so what the entry costs comes out of that
// spin, and the handler still starts on the same cycle of the same line. The
// four added instructions are twelve cycles ahead of the dispatch, and a nop
// makes them fourteen: the loop is seven cycles, and a whole number of turns of
// it keeps the phase it is entered at, and with it the cycle it exits on.
// Measured on the panel split's `sta $d018` over 400 frames, before and after:
// x64sc lands on cycles 53 to 58 of line 162 either way, and xscpu64 on cycle
// 55 every time. Without the nop, x64sc's late edge was a cycle later.
//
// So the main line pays only for the five cycles on the way out, a handful of
// interrupts a frame. Nothing else differs from the library's, down to the
// order it reads its tables in.
//
// The tables and nextIRQ are rasterirq.c's own globals: not in its header, but
// not static either. _gfx_rirq_tables() below fills them.
extern byte rasterIRQIndex[];
extern byte rasterIRQNext[];
extern byte rasterIRQLow[];
extern byte rasterIRQHigh[];
extern volatile byte nextIRQ;

// clang-format off
__asm _gfx_isr
{
	sta plra + 1
	lda $01
	sta pl01 + 1
	lda #$35
	sta $01
	// Twelve cycles to here, fourteen with this: two turns of the RIRQCode's
	// seven-cycle spin. See above.
	nop
	stx plrx + 1
	sty plry + 1

	ldx nextIRQ
	bmi exi
l1:
	lda rasterIRQNext, x
	ldy rasterIRQIndex + 1, x
	ldx rasterIRQLow, y
	stx ji + 1
	ldx rasterIRQHigh, y
	stx ji + 2

ji:
	jsr $0000

	inc nextIRQ
	ldx nextIRQ

	ldy rasterIRQNext, x

	asl $d019

	cpy #$ff
	beq e2

	dey
	sty $d012
	dey
	cpy $d012
	bcc l1

plry:
	ldy #0
pl01:
	lda #$35
	sta $01
plra:
	lda #0
plrx:
	ldx #0
	rti

	// A spurious entry. Unlike the library's, A and Y are already saved and $01
	// already switched, so it leaves the same way as everything else.
exi:
	asl $d019
	jmp plry

	// No more interrupts to service this frame.
e2:
	inc rirq_count

	ldy rasterIRQNext
	dey
	sty $d012
	ldx #0
	stx nextIRQ
	beq plry
}
// clang-format on

// rirq_init(false), which is rirq_init_io(), with the entry above in place of
// the library's, and without its rirq_init_tables(): _gfx_rirq_tables() below
// writes every table entry the entry reads.
static void _gfx_rirq_init(void) {
  __asm {
    sei
  }
  *(void **)0xfffe = _gfx_isr;
  vic.intr_enable = 1;
  vic.ctrl1 &= 0x7f;
  vic.raster = 255;
}
#endif

#pragma optimize(pop)

// The one handler with a deadline, the panel's top, is one call:
// rirq_build(ic, 1) writes code[0..12], its RTS at 12, and rirq_call() for
// write 0 touches only 9-11 (c64/rasterirq.c). So 14 bytes, not the library's
// RIRQCode, whose 32 are sized for five writes; cast to it at the calls. The
// other two have no RIRQCode at all, see kGfxRasterRows.
static const uint8_t kGfxRirqCode1 = 13;
typedef struct {
  uint8_t size;
  uint8_t code[kGfxRirqCode1];
} gfx_rirq1_t;
#ifdef __OSCAR64__
static_assert(RIRQ_ADDR_0 + 2 <= kGfxRirqCode1 && RIRQ_DATA_0 < kGfxRirqCode1,
              "write 0 fits a one-write handler");
#endif

// In mem.h's irqcode region at $FFC0: built in full by
// gfx_init_raster_irqs() before the interrupts start, so it needs no clear.
#pragma bss(irqcode)
static gfx_rirq1_t _rirq_buf;
#pragma bss(bss)

#ifdef __OSCAR64__
// The split's three raster lines, top to bottom: the panel's top and bottom,
// and the terrain.
//
// No interrupt above them switches sprite DMA off for the cycle-counted panel
// top. A write of $d015 there could only stop a sprite from *starting* on the
// lines between it and the split, never one already being fetched, and no
// enabled sprite has a Y in that band: sprites_stack_add() rejects every
// entry whose last sprite would start below kSpriteVisibleEndYPixels, and the
// orientation mark and the tail fin sit on fixed lines above it (mem.h
// kSpritesOffLead, docs/clouds.md 1.8).
//
// _gfx_isr arms a slot's interrupt on the line above its row, and a RIRQCode
// then spins on $d012 until the line below its row starts: that spin is what
// takes the entry's jitter out, and the first slot, _rirq_buf, has a deadline
// that needs it. The panel's bottom and the terrain have none, so _gfx_isr
// calls their handlers directly, with no RIRQCode and no spin, about 70
// cycles less every time one runs. Their rows are lower to make up for it, so
// that neither starts on a line it could show on:
//
// - The panel's bottom, one lower. On a stock C64 the entry takes just over a
//   raster line, so the handler starts on the line it always did, the one
//   below kViewportEndYPixels + 24, within a few cycles of where the spin let
//   it go. On a SuperCPU the entry is over at once and it starts a line
//   earlier, which shows nothing: the needles it places are 15 lines further
//   down, and the band above has sprites 1-7 parked in the border.
// - The terrain, two lower, because it rewrites $d011, $d018 and $d021 and so
//   has to stay off the panel's last line on both machines: a SuperCPU starts
//   it on the first border line, as before, and a stock C64 a line later.
static const uint8_t kGfxRasterRows[3] = {
    kRasterScreenYStart + kViewportEndYPixels - 1,
    kRasterScreenYStart + kViewportEndYPixels + 24 + 1,
    kRasterScreenYStart + kScreenHeightPixels + 2};
static_assert(kViewportEndYPixels + 24 < kScreenHeightPixels &&
                  kRasterScreenYStart + kScreenHeightPixels + 2 < 0xff,
              "the slots' rows must ascend below 0xff");

// What _gfx_isr calls for slot i.
static inline void _gfx_rirq_entry(uint8_t i, const void *entry) {
  rasterIRQLow[i] = (uint8_t)(uint16_t)entry;
  rasterIRQHigh[i] = (uint8_t)((uint16_t)entry >> 8);
}

// rirq_set() for each slot and then rirq_sort(), written as the tables they
// leave behind. The rows already ascend with the slot, so the sort's order is
// the identity: rasterIRQIndex[i + 1] = i and rasterIRQNext[i] = row i, ended
// by 0xff. That is all _gfx_isr reads, together with each slot's handler
// address; the library's rasterIRQRows and the sort itself (118 B) drop out.
// The sort's write of $d012 is left out too: rirq_start() writes it next.
//
// The addresses are three spelt-out stores: read in the loop from a table of
// pointers, oscar64 indexed every table with the pointer table's byte offset.
static void _gfx_rirq_tables(void) {
  uint8_t i = 3;
  do {
    --i;
    rasterIRQIndex[i + 1] = i;
    rasterIRQNext[i] = kGfxRasterRows[i];
  } while (i != 0);
  _gfx_rirq_entry(0, _rirq_buf.code);
  _gfx_rirq_entry(1, (const void *)_switch_to_panel_bottom);
  _gfx_rirq_entry(2, (const void *)_gfx_switch_to_terrain);
  rasterIRQNext[3] = 0xff;
  nextIRQ = 0;
}
#else
static void _gfx_rirq_tables(void) {}
#endif

// Raster-IRQ setup and charset loading; one-off, not per frame. The IRQ
// handlers themselves are above and are deliberately left alone.
#pragma optimize(push, outline)

void gfx_init_raster_irqs(void) {
  // The split owns $d011 from here on and its writes are constants with DEN
  // set, so starting it is also the unblank for whatever transition led here
  // (screen.h). Putting mem_den back keeps the main line's mode setters
  // agreeing with what the hardware is about to be told.
  mem_den = 0x10;

  _gfx_rirq_init();

  // Interrupts are masked from here to rirq_start(). The first sound_blit()
  // then pushes the shadow, which sound_silence() zeroed on the way out, over
  // whatever the chip was left holding.
  sound_mark_dirty();

  // The panel top's NOPs are measured on lines with no sprite DMA. With a
  // sprite's fetches on them the three writes at the end miss the blanking,
  // and a band of terrain colour flickers across the top of the panel. The
  // sprite stack's cull is what keeps those lines clear (kGfxRasterRows,
  // mem.h kSpritesOffLead and docs/clouds.md §1.8).
  RIRQCode *panel_top = (RIRQCode *)&_rirq_buf;
  rirq_build(panel_top, 1);
  // Which of the two padding counts this machine needs, from the boot probe.
  // cpu_step_shift is 0 on a stock C64 and 2 on a SuperCPU (docs/supercpu.md),
  // and it is the only thing in the program that already knows the difference.
  rirq_call(panel_top, 0,
            cpu_step_shift ? (void *)_gfx_switch_to_panel_top_fast
                           : (void *)_gfx_switch_to_panel_top);
  // The panel's bottom and the terrain are called straight from _gfx_isr
  // (kGfxRasterRows).
  _gfx_rirq_tables();
  rirq_start();
}

// The sound driver's release point, and the reason it needs no other. Because
// sound_blit() lives in _switch_to_terrain, "raster interrupts masked" and
// "the driver has stopped running" are the same statement, and this is the
// only place that makes it true - reached from screen_enter_static_mccm()
// (menu, help) and map_enter(), and nowhere else. Silencing here therefore
// covers all three without any of them knowing about sound.
//
// Before the rirq_stop(), not after: map_enter() goes on to bank I/O out, and
// sound_silence() writes $D400 directly.
inline void gfx_stop_raster_irqs(void) {
  sound_silence();
  rirq_stop();
}

// __noinline: four call sites, and a busy wait has no cycles to save.
__noinline void gfx_wait_vsync(void) {
  while (vic.raster != 255)
    ;
  while (vic.raster == 255)
    ;
}

static inline void _gfx_init_solid_chars() {
  memset(kCharRam + kCharSolid11 * 8, 0xFF, 8);
}

void gfx_init_chars(void) {
  _gfx_init_solid_chars();
  oscar_expand_lzo((char *)kCharRam + kGfxCharStart * 8, kGfxCharsCompressed);
}

#pragma optimize(pop)

// The raster window in which mem_switch_buffer() may do its work. Two
// deadlines bracket it, and neither of them is the vertical blank:
//
//   Opens at 162. _copy_color_ram() rewrites $D800-$DA2F, the color RAM the
//   viewport fetches while it is on screen on lines 50..161. The first line
//   past the viewport is the earliest the copy can start without recoloring
//   the picture out from under the beam.
//
//   Closes at 242. _switch_to_terrain() runs at raster 250 and latches
//   mem_using_alt_buffer into $d018 for the next frame's viewport, so the
//   toggle has to land before 250 or the new colors are shown against the old
//   characters for a frame. Eight lines of margin, which is ~500 cycles for a
//   store that needs about ten.
//
// The copy itself is ~5760 cycles, near enough 92 lines, and it is allowed to
// run right through 250 and into the border - it only has to be finished by
// the next frame's line 50. Starting as late as 242 that lands around line 24
// of the following frame, 26 lines clear on PAL. NTSC has 263 lines rather
// than 312 and only 69 of them left at that point, so this constant would
// have to come down to about 215 there. It does not today: the old line-255
// wait left the copy just 57 lines on NTSC, so that path was already over
// budget before this change.
static const uint8_t kFlipWindowFirst =
    kRasterScreenYStart + kViewportEndYPixels;
static const uint8_t kFlipWindowLast =
    kRasterScreenYStart + kScreenHeightPixels - 8;
static const uint8_t kFlipWindowSpan = kFlipWindowLast - kFlipWindowFirst;

// A range test rather than an edge, which is the point of it. Waiting for one
// specific raster line means a handler that happens to straddle that line
// costs a whole frame; an 81-line window cannot be stepped over. $d012 is only
// the low byte of the raster counter, but every line in the window is below
// 256 and PAL's lines 256..311 read back as 0..55, so the unsigned compare
// rejects them along with the top border.
inline void gfx_wait_flip_window(void) {
  while ((uint8_t)(vic.raster - kFlipWindowFirst) > kFlipWindowSpan)
    ;
}

void gfx_draw_projected(uint8_t color) {
  uint8_t py = (uint8_t)(kViewportHeightPixels / 2) - (uint8_t)vec_sy;
  if (py < (uint8_t)kViewportHeightPixels) {
    uint8_t hpx = (uint8_t)((uint16_t)(kViewportWidthPixels / 2 - vec_sx) >> 1);
    if (hpx < (uint8_t)(kViewportWidthPixels / 2)) {
      uint8_t cx = hpx >> 2;
      uint8_t cy = py >> 3;
      uint8_t *p = mem_screen_row_ptrs[cy] + kViewportStartX + cx;
      if (*p == kCharSolidGround) {
        uint8_t off = (hpx & 0x03) + ((py & 0x06) << 1);
        if (color == kGfxDotGround) {
          *p = kGfxGroundPoints + off;
        } else {
          *p = kGfxColorPoints + off;
          mem_color_row_ptrs[cy][cx] = 0x08 | color;
        }
      }
    }
  }
}

static const char *const kGfxHeadingBitmaps[] = {
    (const char *const)0xF120,
    (const char *const)0xF0C0,
    (const char *const)0xF060,
    (const char *const)0xF000,
};
static char *const kGfxHeadingDest = (char *)0xF5C8;

// Last heading drawn into the panel bitmap; 0xFF (not a valid heading)
// forces a redraw after the bitmap has been re-expanded.
static uint8_t _gfx_heading_bitmap_cache = 0xFF;

void gfx_invalidate_panel_bitmap(void) {
  _gfx_heading_bitmap_cache = 0xFF;
#ifdef __ENABLE_OPPONENT__
  gun_ammo_shown = false;
#endif
}

inline void gfx_update_heading_bitmap(uint8_t heading) {
  if (mem_debug_enabled || view_state != VIEW_CENTER) {
    return;
  }
  // The heading strip lives in the (single) panel bitmap, so once drawn it
  // stays valid until the heading changes or the bitmap is re-expanded.
  if (heading == _gfx_heading_bitmap_cache) {
    return;
  }
  _gfx_heading_bitmap_cache = heading;

  static const uint8_t kHeadingCharMax = kHeadingMax / 4;
  uint8_t heading_ch = (heading >> 2) + 3;
  if (heading_ch >= kHeadingCharMax) {
    heading_ch -= kHeadingCharMax;
  }
  const char *src = kGfxHeadingBitmaps[heading & 0x03];
  uint8_t s = heading_ch << 3;
  for (uint8_t d = 0; d < 6 * 8;) {
    for (uint8_t y = 0; y < 8; ++y) {
      kGfxHeadingDest[d++] = src[s++];
    }
    if (s == kHeadingCharMax * 8) {
      s = 0;
    }
  }
}

#ifdef __ENABLE_OPPONENT__
// The digits 0-9, drawn in the panel art beside the heading strips: row 12,
// columns 22-31, in the head of the bitmap that the char-mode viewport covers.
// They are taken from there at build time rather than expanded with the rest,
// because the panel embed starts at $F000 and they would land below it, on
// the alt screen's RAM and sprite pointers. `make fpanel` gives these cells
// and the counter's the same slot layout (--pin-slots), so the bit pairs
// copied across draw the colours they were drawn in.
static const char kGfxAmmoDigits[] = {
#embed 80 4018 "fpanel.koa"
};
// The counter's window, row 18 columns 6-8.
static const uint8_t kGfxAmmoColumn = 6;
static char *const kGfxAmmoDest = (char *)0xE000 + 18 * 320 + kGfxAmmoColumn * 8;
// The left view keeps the panel's first kCopyWidthChars columns, moved to the
// right edge, so the window is still there with only its last digit cut off.
static const uint8_t kGfxAmmoLeftDigits = kCopyWidthChars - kGfxAmmoColumn;

inline void gfx_update_ammo(void) {
  if (mem_debug_enabled || gun_ammo_shown) {
    return;
  }
  char *dst = kGfxAmmoDest;
  uint8_t digits = kGunAmmoDigits;
  if (view_state == VIEW_LEFT) {
    dst += kFillWidthChars * 8;
    digits = kGfxAmmoLeftDigits;
  } else if (view_state != VIEW_CENTER) {
    return;
  }
  gun_ammo_shown = true;
  // Byte offsets rather than pointers: the window and the digits are each
  // well inside a page, so both indices fit a register.
  uint8_t d = 0;
  for (uint8_t i = 0; i < digits; ++i) {
    uint8_t s = gun_ammo[i] << 3;
    for (uint8_t y = 0; y < 8; ++y) {
      dst[d++] = kGfxAmmoDigits[s++];
    }
  }
}
#endif

// The indicator lamps are lit by recoloring one bit pair of their char cell,
// and that pair is 11 — color RAM, a whole byte per cell — so switching a lamp
// is a plain store with nothing to preserve.
//
// The two screen RAM colors would do the job too, but they share a byte, and
// which of them a cell's colors land in is png2koa.py's choice, not ours: it
// re-labels the slots for compression and has already swapped a lamp's nibbles
// once, silently. `make panel` therefore pins light red to color RAM in these
// five cells, by the same row and column as the pointers below, and
// tests/test_png2koa.py checks all three lists still agree. Only these cells
// are pinned; light red elsewhere in the panel is the encoder's business.
static inline void _set_lamp(uint8_t *color_ptr, bool on) {
  *color_ptr = on ? kColorLightRed : kColorBlack;
}

// Only the center view shows the lamps. A side view keeps 8 of the panel's 40
// columns and fills the rest with a pattern, and every lamp is in the filled
// part — where color RAM is what the fill draws its own pixels with, so a lamp
// written there would be a red speck on the gradient. The heading strip bails
// out on the same test, for the same kind of reason.
static inline bool _lamps_live(void) {
  return !mem_debug_enabled && view_state == VIEW_CENTER;
}

inline void gfx_update_nav_heading(uint8_t heading) {
  static uint8_t *const kHdgPtr = kColorRam + 15 * kScreenWidth + 21;
  if (_lamps_live()) {
    _set_lamp(kHdgPtr, heading == 0 || heading > kHeadingMax / 2);
    _set_lamp(kHdgPtr + 1, heading < kHeadingMax / 2);
  }
}

inline void gfx_update_stall(bool stall) {
  static uint8_t *const kStallPtr = kColorRam + 15 * kScreenWidth + 14;
  if (_lamps_live()) {
    _set_lamp(kStallPtr, stall);
  }
}

inline void gfx_update_flap(bool flap) {
  static uint8_t *const kFlapPtr = kColorRam + 17 * kScreenWidth + 14;
  if (_lamps_live()) {
    _set_lamp(kFlapPtr, flap);
  }
}

inline void gfx_update_gear(bool gear) {
  static uint8_t *const kGearPtr = kColorRam + 18 * kScreenWidth + 14;
  if (_lamps_live()) {
    _set_lamp(kGearPtr, gear);
  }
}
