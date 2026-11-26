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
#ifdef __OSCAR64__
#include "sprites_asm.h"
#endif
#include "vec.h"
#include "vic.h"
#include "view.h"

#ifdef __OSCAR64__
#include <oscar.h>
#else
static const char *oscar_expand_lzo(char *dp, const char *sp) { return sp; }
#endif

#pragma data(data_compr)
const char kGfxCharsCompressed[] = {
#embed 1792 lzo "gfx_chars.bin"
};
#pragma data(data)

// Raster lines for the three direct chained split handlers:
// - Panel top: armed on line 161 (not a badline; at most 1 tail-fin sprite
//   fetches on line 161 in VIEW_BACK), spins on $d012 until line 162 starts,
//   slows a SuperCPU, waits to blanking, switches VIC to panel bitmap + active
//   top sprite (sprite 0 in VIEW_CENTER, none otherwise).
// - Panel bottom: armed on line 186 (one line above kViewportEndYPixels + 24 +
//   1 = 187). Skipped completely in VIEW_BACK / VIEW_UNKNOWN (and debug mode),
//   chaining straight to terrain.
// - Terrain: armed on line 251 (one line above kScreenHeightPixels + 2 = 252).
static const uint8_t kGfxRasterPanelTop =
    kRasterScreenYStart + kViewportEndYPixels - 1;
static const uint8_t kGfxRasterPanelBottom =
    kRasterScreenYStart + kViewportEndYPixels + 24;
static const uint8_t kGfxRasterTerrain =
    kRasterScreenYStart + kScreenHeightPixels + 1;
static_assert(kGfxRasterPanelTop == 161 && kGfxRasterPanelBottom == 186 &&
                  kGfxRasterTerrain == 251,
              "gfx.cc direct raster IRQ line constants");

// In mem.h's irqcode region at $FFC0-$FFF7: initialized by
// gfx_init_raster_irqs() before interrupts start, so it needs no clear.
// _gfx_irq_vec holds `JMP <next_handler>` patched by each handler;
// _gfx_panel_fast records cpu_step_shift != 0 for SuperCPU split padding.
#ifdef __OSCAR64__
#pragma bss(irqcode)
static uint8_t _gfx_irq_vec[3];
static uint8_t _gfx_panel_fast;
#pragma bss(bss)
#endif

// Raster IRQ handlers: cycle-counted, so keep the outliner out of them.
#pragma optimize(push, noasm, nooutline)

#ifdef __OSCAR64__
static void _gfx_switch_to_panel_top(void);
static void _switch_to_panel_bottom(void);
static void _gfx_switch_to_terrain(void);

static void _gfx_switch_to_panel_top(void) {
#ifdef __ENABLE_DEBUG__
  if (mem_debug_enabled) {
    sprites_show_no_sprites();
    // clang-format off
    __asm {
        lda #<_gfx_switch_to_terrain;
        ldx #>_gfx_switch_to_terrain;
        ldy #251;               // kGfxRasterTerrain
        sta _gfx_irq_vec + 1;
        stx _gfx_irq_vec + 2;
        sty $d012;
    }
    // clang-format on
    return;
  }
#endif
  // Wait for line 162 to start, then slow SuperCPU and pad to horizontal
  // blanking (~53 cycles after L_pt_spin exits on C64, ~53.3 cycles on SCPU,
  // dead center of the 10-NOP window on both machines). PHA/PLA (7 cyc, 2 B)
  // has no branches and is immune to page-crossing jitter.
  // clang-format off
  __asm {
        lda #161;               // kGfxRasterPanelTop
    L_pt_spin:
        cmp $d012;
        bcs L_pt_spin;

        lda #0;
        sta $d07a;              // cpu_speed_slow()
        pha; pla;
        pha; pla;
        pha; pla;
        pha; pla;
        ldx _gfx_panel_fast;
        beq L_pt_go;
        pha; pla;
    L_pt_go:
        nop;
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
  // clang-format off
  __asm {
        // Only VIEW_LEFT (1), VIEW_CENTER (2), VIEW_RIGHT (3) have bottom
        // needles. VIEW_BACK (4) and VIEW_UNKNOWN (0) skip panel bottom
        // completely and chain straight to terrain.
        lda view_state;
        beq L_pt_arm_terrain;
        cmp #4;                 // VIEW_BACK
        beq L_pt_arm_terrain;
        lda #<_switch_to_panel_bottom;
        ldx #>_switch_to_panel_bottom;
        ldy #186;               // kGfxRasterPanelBottom
        bne L_pt_arm;
    L_pt_arm_terrain:
        lda #<_gfx_switch_to_terrain;
        ldx #>_gfx_switch_to_terrain;
        ldy #251;               // kGfxRasterTerrain
    L_pt_arm:
        sta _gfx_irq_vec + 1;
        stx _gfx_irq_vec + 2;
        sty $d012;
  }
  // clang-format on
}

static void _switch_to_panel_bottom(void) {
  sprites_show_panel_bottom_sprites();
  // clang-format off
  __asm {
        lda #<_gfx_switch_to_terrain;
        ldx #>_gfx_switch_to_terrain;
        ldy #251;               // kGfxRasterTerrain
        sta _gfx_irq_vec + 1;
        stx _gfx_irq_vec + 2;
        sty $d012;
  }
  // clang-format on
}

static void _gfx_switch_to_terrain(void) {
  sprites_show_terrain_sprites();
  vic.color_back = COLOR_GRAD2;
  vic.ctrl1 = 0x1b; // Multicolor character mode
  vic_memptr = mem_using_alt_buffer ? 0xA8 : 0xB8;

  // Last, and only after the $d018 latch above, which is the one write here
  // with a deadline.
  sound_blit();
  // clang-format off
  __asm {
        lda #<_gfx_switch_to_panel_top;
        ldx #>_gfx_switch_to_panel_top;
        ldy #161;               // kGfxRasterPanelTop
        sta _gfx_irq_vec + 1;
        stx _gfx_irq_vec + 2;
        sty $d012;
  }
  // clang-format on
}

// Direct raster IRQ entry: banks I/O in ($01 = $35) so main-line code can
// access RAM under $D000-$DFFF without masking interrupts, calls the active
// handler through _gfx_irq_vec (`JMP target`), acknowledges $D019, and
// restores registers and $01.
// clang-format off
__asm _gfx_isr
{
	sta plra + 1
	lda $01
	sta pl01 + 1
	lda #$35
	sta $01
	stx plrx + 1
	sty plry + 1
	jsr _gfx_irq_vec
	asl $d019
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
}
// clang-format on
#endif

#pragma optimize(pop)

// Raster-IRQ setup and charset loading; one-off, not per frame. The IRQ
// handlers themselves are above and are deliberately left alone.
#pragma optimize(push, outline)

void gfx_init_raster_irqs(void) {
  // The split owns $d011 from here on and its writes are constants with DEN
  // set, so starting it is also the unblank for whatever transition led here
  // (screen.h). Putting mem_den back keeps the main line's mode setters
  // agreeing with what the hardware is about to be told.
  mem_den = 0x10;

#ifdef __OSCAR64__
  __asm {
    sei
  }
  // Interrupts are masked from here to CLI below. The first sound_blit()
  // then pushes the shadow, which sound_silence() zeroed on the way out, over
  // whatever the chip was left holding.
  sound_mark_dirty();

  _gfx_panel_fast = cpu_step_shift;
  _gfx_irq_vec[0] = 0x4C; // JMP abs
  _gfx_irq_vec[1] = (uint8_t)(uint16_t)_gfx_switch_to_terrain;
  _gfx_irq_vec[2] = (uint8_t)((uint16_t)_gfx_switch_to_terrain >> 8);
  *(void **)0xfffe = _gfx_isr;
  vic.intr_enable = 1;
  vic.ctrl1 &= 0x7f;
  vic.raster = kGfxRasterTerrain;
  __asm {
    asl $d019;
    cli;
  }
#else
  sound_mark_dirty();
#endif
}

// The sound driver's release point, and the reason it needs no other. Because
// sound_blit() lives in _gfx_switch_to_terrain, "raster interrupts masked" and
// "the driver has stopped running" are the same statement, and this is the
// only place that makes it true - reached from screen_enter_static_mccm()
// (menu, help) and map_enter(), and nowhere else. Silencing here therefore
// covers all three without any of them knowing about sound.
//
// Before SEI, not after: map_enter() goes on to bank I/O out, and
// sound_silence() writes $D400 directly.
inline void gfx_stop_raster_irqs(void) {
  sound_silence();
#ifdef __OSCAR64__
  __asm {
    sei;
  }
#endif
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
inline bool gfx_in_flip_window(void) {
  return (uint8_t)(vic.raster - kFlipWindowFirst) <= kFlipWindowSpan;
}

inline void gfx_wait_flip_window(void) {
  while (!gfx_in_flip_window())
    ;
}

// Inline in the grid's dot loop, its one caller, and not outlined: a call
// and an outlined row pointer load are 30 cycles a dot.
#pragma optimize(push, nooutline)
inline void gfx_draw_projected(uint8_t color) {
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
#pragma optimize(pop)

static const char *const kGfxHeadingBitmaps[] = {
    (const char *const)0xF120,
    (const char *const)0xF0C0,
    (const char *const)0xF060,
    (const char *const)0xF000,
};
static char *const kGfxHeadingDest = (char *)0xF5C8;

// Last heading drawn into the panel bitmap; 0xFF (not a valid heading)
// forces a redraw after the bitmap has been re-expanded.
uint8_t _gfx_heading_bitmap_cache = 0xFF;

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
const char kGfxAmmoDigits[] = {
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
