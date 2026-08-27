#ifndef SPRITES_H
#define SPRITES_H

#include <stdint.h>

#include "bool.h"

extern char kSpriteDataCompressed[];

// The compressed sprite blob doubles as scratch RAM. mem_init() expands it to
// $D400 at boot and nothing reads the compressed copy afterwards, so from the
// first frame onwards it is 817 bytes of ordinary RAM that cost nothing --
// they are already in the image. mem.cc has aliased the viewport colour buffer
// onto the front of it for a while; the flight path takes the rest, and
// between them they fill it almost exactly: 816 of 817.
//
// One map, in one place, because the two tenants live in different files and
// nothing but arithmetic stops them claiming the same byte. sprites.cc holds
// the assert that the blob is long enough, because that is the only place its
// length is known -- it is whatever LZO makes of spritedef.bin, not a number
// anyone chose. Add a tenant here first, never by picking an offset locally.
//
// Two large tenants rather than a handful of small ones is deliberate. Every
// object moved changes the layout, and layout changes in this program have a
// history of waking up bugs that had nothing to do with them; see
// tools/check_rom_window.py for the one that cost the most.
//
// Nothing placed here is zeroed at startup: it holds LZO bytes until it is
// written, so a tenant must not read a slot before it has filled one.
static const uint16_t kSpriteScratchColor = 0;   // 560, mem.cc
static const uint16_t kSpriteScratchPath = 560;  // 256, flight.cc
static const uint16_t kSpriteScratchEnd = 816;

void sprites_init(void);

// Update instrument panel sprites.
void sprites_set_speed(uint8_t speed);
void sprites_set_alt(uint16_t alt);
void sprites_set_vspeed(int16_t vspeed);
void sprites_set_roll(uint8_t roll_angle);
void sprites_set_pitch(int8_t pitch_angle);
void sprites_set_throttle(uint8_t throttle);
void sprites_set_fuel(uint32_t fuel);

// --- The sprite stack ------------------------------------------------------
//
// A distance-sorted allocator for the eight hardware sprites during the
// viewport band. Designed in docs/clouds.md §1; the short version is that VIC
// sprite-to-sprite priority is fixed by index - sprite 0 draws in front of
// sprite 1, and so on - so "draw the closest object first" is implemented as
// "give the closest object the lowest index". Sort the candidates by distance,
// hand out indices in that order, and correct mutual occlusion comes out with
// no per-frame priority logic and nothing written to $D01B.
//
// It also fixes the overflow rule for free: when more objects want sprites
// than there are sprites, the ones that lose are at the end of the list, which
// is the farthest ones.
//
// Per frame: reset, add candidates in any order, commit. The committed frame
// is what the terrain raster handler programs.

static const uint8_t kSpriteStackSize = 8;

// How many hardware sprites the terrain band's *stack* may hand out. One short
// of the eight, because sprite 7 is the vertical-speed needle below the split
// and the orientation indicator above it - see sprites_set_orientation() below
// - so clouds and the sun share the other seven.
//
// It is *not* short because the needle would be corrupted otherwise, which is
// what this comment used to say and what docs/clouds.md §1.9 was written to
// justify. The needle's Y is never above raster 171 and mem.h's
// kSpritesOffLead now stops a viewport sprite starting below 139, so its DMA
// is over by 160 - eleven lines of margin, measured. See docs/clouds.md §1.9.
static const uint8_t kSpriteTerrainSlots = 7;

// bitmap2 value meaning "this entry is a single sprite".
static const uint8_t kSpriteNoBitmap = 0xFF;

// X-expanded: 48 screen pixels wide, one sprite pixel per world pixel.
static const uint8_t kSpriteFlagExpandX = 0x01;
// Snap the position onto the global dither lattice (docs/clouds.md §4): X even
// and (X >> 1) + Y even, so that overlapping checkerboard-dithered sprites keep
// their transparent pixels aligned instead of filling each other in. Clouds set
// it; the sun, which is solid, does not.
static const uint8_t kSpriteFlagAlignDither = 0x02;

// Begins a new frame's candidate list.
void sprites_stack_reset(void);

// Offers one object to the stack.
//
// (x, y) is the object's *centre* in viewport screen pixels - the same space
// gfx_project_and_draw() works in:
//     x = kScreenWidthPixels / 2 - vec_sx
//     y = kViewportEndYPixels / 2 - vec_sy
// The pivot is the bitmap's own, in *sprite* pixels as spritedef.cc stores it;
// this applies it, doubling pivot_x when kSpriteFlagExpandX is set, because an
// expanded sprite pixel is two screen pixels.
//
// bitmap2 is kSpriteNoBitmap for a single sprite, or the lower block of a
// 1 x 2 stack, in which case the entry claims two consecutive indices and the
// lower sprite is placed 21 raster lines below the upper one.
//
// depth is the camera-space forward distance, used only for ordering; INT16_MAX
// sorts an object behind everything, which is what the sun passes.
//
// Returns false if the object was culled or did not fit.
bool sprites_stack_add(int16_t depth, int16_t x, int16_t y, int8_t pivot_x,
                       int8_t pivot_y, uint8_t bitmap, uint8_t bitmap2,
                       uint8_t color, uint8_t flags);

// Assigns hardware indices nearest-first and publishes the result to the
// terrain raster handler. Anything that did not fit is dropped; unused indices
// are left disabled.
void sprites_stack_commit(void);

// --- The orientation indicator ---------------------------------------------
//
// A fixed mark in the middle of the viewport: a bar with a gap in it,
// xxxxxxxx--------xxxxxxxx, drawn on one row. It never moves. What moves is the
// horizon, and the mark is what gives it something to be read against - level
// flight lays the horizon along the bar and through the gap, pitch slides it
// off, roll tilts it across.
//
// It owns hardware sprite 7 for the length of the viewport band, which is the
// index the stack above never hands out. The panel band takes the same sprite
// back for the vertical-speed needle at the split, and may, because a viewport
// sprite's DMA is over by raster 160 and the needle's Y is never above 171 -
// docs/clouds.md §1.9, which reserved this slot for exactly this.
//
// The front view only: it says nothing about an attitude read out of a side
// window, so calling this in VIEW_LEFT or VIEW_RIGHT clears the mark rather
// than setting it.
//
// Per frame, like the stack: sprites_stack_reset() clears the mark, this sets
// it, sprites_stack_commit() publishes it. A frame that never calls this leaves
// sprite 7 disabled, which is what keeps the mark out of the frames the debug
// view and the band handlers program for themselves.
void sprites_set_orientation(void);

void sprites_show_terrain_sprites();
void sprites_show_no_sprites();
void sprites_show_panel_top_sprites();
void sprites_show_panel_bottom_sprites();

#pragma compile("sprites.cc")

#endif
