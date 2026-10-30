#include "sprites.h"

#include "color.h"
#include "fmath.h"
#include "mem.h"
#include "msg.h"
#include "roll.h"
#include "spritedef.h"
#include "vec.h"
#include "vic.h"
#include "view.h"

#ifdef __OSCAR64__
#pragma data(data_compr)
char kSpriteDataCompressed[] = {
#embed 3072 lzo "spritedef.bin"
};
#pragma data(data)
#else
// The host build has no #embed and no LZO, and the test has no use for the
// sprite bitmaps. Only the size matters off-target, and only so that the
// assert below means the same thing in both builds.
char kSpriteDataCompressed[kSpriteScratchEnd];
#endif

// mem.cc and flight.cc overlay the scratch map from sprites.h onto this blob,
// both of them only once mem_init() has expanded it to $D400 and left the
// compressed copy as scrap. Neither can check the size from where it sits.
// Edit the sprite art and this is what tells you the tenants no longer fit.
static_assert(sizeof(kSpriteDataCompressed) >= kSpriteScratchEnd,
              "sprite blob too small for the scratch map in sprites.h");
static_assert(kSpriteScratchPath >= kViewportWidth * kViewportHeight,
              "flight path overlaps the viewport colour buffer");

static const uint8_t kSpriteOffsetX = 24;
static const uint8_t kSpriteOffsetY = 50;

// An unexpanded sprite.
static const uint8_t kSpriteWidthPixels = 24;
static const uint8_t kSpriteHeightPixels = 21;

// sprites_stack_add()'s one vertical cull serves both heights: the last line a
// Y-expanded sprite may start on, plus its 42 lines, is the same as for a plain
// one plus its 21.
static_assert(kViewportEndYPixels - kSpritesOffLeadExpandY +
                      2 * kSpriteHeightPixels ==
                  kSpriteVisibleEndYPixels + kSpriteHeightPixels,
              "the Y-expanded cut needs its own bound in sprites_stack_add()");

static const uint8_t kSpriteSpeedPivotX = 112;
static const uint8_t kSpriteSpeedPivotY = 176;
static const uint8_t kSpriteAltPivotX = 208;
static const uint8_t kSpriteAltPivotY = 176;
static const uint8_t kSpriteVSpeedPivotX = 214;
static const uint8_t kSpriteVSpeedPivotY = 136;
static const uint8_t kSpriteRollPivotX = 160;
static const uint8_t kSpriteRollPivotY = 176;
static const uint8_t kSpriteThrottlePivotX = 8; // 264 & 0xff
static const uint8_t kSpriteThrottlePivotY = 173;
static const uint8_t kSpriteFuelPivotX = 56;
static const uint8_t kSpriteFuelPivotY = 173;

// The vertical-speed needle is drawn above the split and the other seven
// below it, so it shares its index with the orientation indicator's
// (sprites.h).
static const uint8_t kSpriteIdxVSpeed = 0;
static const uint8_t kSpriteIdxSpeed = 1;
static const uint8_t kSpriteIdxRoll = 2;
static const uint8_t kSpriteIdxPitch = 3;
static const uint8_t kSpriteIdxAlt1 = 4;
static const uint8_t kSpriteIdxAlt2 = 5;
static const uint8_t kSpriteIdxThrottle = 6;
static const uint8_t kSpriteIdxFuel = 7;

struct sprite_xy_t {
  uint8_t x;
  uint8_t y;
};

// One offered object, before indices are handed out. Insertion-sorted by
// ascending depth, so entry 0 is the nearest.
struct sprite_cand_t {
  int16_t depth;
  int16_t x; // VIC sprite coordinates, top left, pivot already applied.
  uint8_t y;
  uint8_t bitmap;
  uint8_t bitmap2; // kSpriteNoBitmap for a single sprite.
  uint8_t color;
  uint8_t flags;
};

// The committed frame. The layout is chosen for the *interrupt* that reads it,
// not for the main line that writes it: `pos` mirrors $D000-$D00F byte for
// byte, x and y interleaved, so the handler copies it with one index register
// and one stride. Holding x and y in separate arrays would need two indices at
// different strides, and on a 6502 that is one live value more than there are
// registers - oscar64 spills the difference into its runtime zero page, which
// a raster handler may not touch. See _sprites_program_frame() below.
struct sprite_frame_t {
  uint8_t pos[16];
  uint8_t ptr[8];
  uint8_t color[8];
  uint8_t msbx;
  uint8_t expand;
  // $D017. Only the tail fin ever sets a bit here, but the field is per frame
  // rather than written once at the view switch: the terrain band programs the
  // whole of the sprite state from one frame, and a register it did not own
  // would be one more thing to unwind on the way back to the panel.
  uint8_t expand_y;
  uint8_t enable;
};

// $D000, as a literal rather than through the vic struct, so `[i]` is plainly
// absolute-indexed. spr_pos is the first member of struct VIC, so on the host
// the register block's own address is the same thing.
#ifdef __OSCAR64__
#define kVicSpritePos ((volatile uint8_t *)0xD000)
#else
#define kVicSpritePos ((volatile uint8_t *)vic_host)
#endif

#pragma bss(bss2)

static sprite_cand_t _sprites_cand[kSpriteStackSize];
static uint8_t _sprites_cand_count;

// Whether this frame carries the orientation indicator. One flag and no
// position: the marker does not move - see sprites_set_orientation().
static bool _sprites_orient_on;

// The same, for the tail fin - see sprites_set_fin(). The two are mutually
// exclusive by construction, since one is the front view and the other the
// back, but nothing here relies on that: they own different sprite indices.
static bool _sprites_fin_on;

// Double buffered, and that is not optional. _gfx_switch_to_terrain() reads the
// frame from an interrupt at raster 250 while sprites_stack_commit() writes it
// from the main line at an unrelated point. A torn read of a single object -
// which is all the old sun path could produce - is one object a frame stale;
// a torn read across eight is one object's X against another's Y, which is a
// sprite in the wrong place. Two named objects rather than an array of two,
// because the raster handler must reach them by a *constant* address - see the
// macro below.
//
// **Each frame belongs to a screen buffer**: A to the main one and B to the
// alt one. Commit fills the frame of the buffer being drawn into, and the
// handler shows the frame of the buffer it puts on screen, both decided by
// mem_using_alt_buffer. So there is no flip of its own: mem_switch_buffer()'s
// toggle of that byte puts the sprites and the characters one camera drew on
// screen at the same raster 250. With a flip of its own at the end of commit,
// as this had until docs/planes.md §13 step 6, the sprites went up a whole
// render before the terrain they were drawn against - the sun and the clouds
// from the next frame's camera over the last frame's ground.
//
// The frames stay volatile. They are written by the main line and read by the
// handler, and once that ordering was a flip index oscar64 hoisted to the top
// of commit, so that the handler programmed a half-filled frame.
static volatile sprite_frame_t _sprites_frame_a;
static volatile sprite_frame_t _sprites_frame_b;

static volatile sprite_xy_t _sprites_instrument_xy[8];
static volatile uint8_t _sprites_instrument_idx[8];

// Defined with the rest of the tail fin, below; sprites_init() is the only
// caller and it comes first in this file.
static void _sprites_draw_fin(void);

inline void sprites_init(void) {
  for (uint8_t i = 0; i < 8; i++) {
    vic.spr_color[i] = kColorInstrument;
  }

  // Both frames with nothing enabled, so a terrain interrupt arriving before
  // the first world_update_objects() programs no sprites rather than whatever
  // the last flight or bss left there. The rest of a frame is not looked at
  // while its enable mask is clear.
  _sprites_frame_a.enable = 0;
  _sprites_frame_b.enable = 0;

  // Nothing else in the program writes these four, and up to now that worked
  // only because they are zero after a reset. $D01D became a live register when
  // the stack learned to X-expand, so leaning on the reset state is no longer
  // defensible for any of them. See sprite_objects.md §0: hires only, never
  // Y-expanded, and sprites always in front of the terrain.
  vic.spr_expand_x = 0;
  vic.spr_expand_y = 0;
  vic.spr_multi = 0;
  vic.spr_priority = 0;
  vic.spr_enable = 0xFF;

  // The two blocks at $FF40, which mem_init()'s expansion of the blob does not
  // reach. Here rather than there because the art is this file's, and because
  // this runs on every entry to the simulation and every return from the map -
  // one of which, one day, will be the screen that learns to borrow those two
  // blocks the way the map borrows the panel bitmap.
  _sprites_draw_fin();
}

// dir is 0..31, clockwise from up. The arm tables hold the first half-turn
// only: dir + 16 draws bitmap dir with its pivot mirrored through the centre.
// __noinline: six gauges a frame; the mirror inlined at each cost 70 bytes.
static __noinline void _sprites_set_instrument_sprite(uint8_t idx,
                                           const sprite_arm_t *arm,
                                           uint8_t bitmap, uint8_t dir,
                                           uint8_t pivot_x, uint8_t pivot_y) {
  const sprite_arm_t *a = &arm[dir & (kSpriteDefArmCount - 1)];
  int8_t px = a->pivot_x;
  int8_t py = a->pivot_y;
  if (dir & kSpriteDefArmCount) {
    px = 2 * kSpriteDefArmCenterX - px;
    py = 2 * kSpriteDefArmCenterY - py;
  }
  _sprites_instrument_xy[idx].x = pivot_x - px;
  _sprites_instrument_xy[idx].y = pivot_y - py;
  _sprites_instrument_idx[idx] = bitmap + (dir & (kSpriteDefArmCount - 1));
}

inline void sprites_set_speed(uint8_t speed) {
  _sprites_set_instrument_sprite(
      kSpriteIdxSpeed, kSpriteDefLongArm, kSpriteDefLongArmBitmap,
      (speed >> 1) & 0x1f,
      kSpriteOffsetX + kSpriteSpeedPivotX, kSpriteOffsetY + kSpriteSpeedPivotY);
}

inline void sprites_set_alt(uint16_t alt) {
  _sprites_set_instrument_sprite(
      kSpriteIdxAlt1, kSpriteDefLongArm, kSpriteDefLongArmBitmap,
      (alt >> 4) & 0x1f,
      kSpriteOffsetX + kSpriteAltPivotX, kSpriteOffsetY + kSpriteAltPivotY);
  // ((alt >> 4) * 205) >> 11 needs a 32-bit intermediate; vec_fastmul8p8
  // returns the exact middle 16 bits ((t * 205) >> 8), so shifting 3 more
  // is bit-identical without pulling in the mul32 runtime.
  _sprites_set_instrument_sprite(
      kSpriteIdxAlt2, kSpriteDefShortArm, kSpriteDefShortArmBitmap,
      ((uint16_t)vec_fastmul8p8(alt >> 4, 205) >> 3) & 0x1f,
      kSpriteOffsetX + kSpriteAltPivotX, kSpriteOffsetY + kSpriteAltPivotY);
}

inline void sprites_set_vspeed(int16_t vspeed) {
  if (vspeed > 0x300) {
    vspeed = 0x300;
  } else if (vspeed < -0x300) {
    vspeed = -0x300;
  }
  uint8_t dir = (0x18 + (vspeed >> 6)) & 0x1f;
  _sprites_set_instrument_sprite(kSpriteIdxVSpeed, kSpriteDefShortArm,
                                 kSpriteDefShortArmBitmap, dir,
                                 kSpriteOffsetX + kSpriteVSpeedPivotX,
                                 kSpriteOffsetY + kSpriteVSpeedPivotY);
}

// The roll arm is drawn through its hub, so only its bitmap turns, and a
// bitmap already covers both ends of the arm: half a turn of roll comes back
// to the same block.
static const uint8_t _sprites_roll_to_bitmap[kRollMax / 2] = {
    8,  7,  7,  6,  6,  5,  5,  4,  4,  3,  3,  2,  2,  1,  1,
    0,  15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9,  9};

inline void sprites_set_pitch(int8_t pitch_angle) {
  _sprites_instrument_xy[kSpriteIdxPitch].x =
      kSpriteOffsetX + kSpriteRollPivotX - 12;
  _sprites_instrument_xy[kSpriteIdxPitch].y =
      kSpriteOffsetY + kSpriteRollPivotY - 10 - (pitch_angle >> 2);
  _sprites_instrument_idx[kSpriteIdxPitch] = kSpriteDefLongArmBitmap + 8;
}

inline void sprites_set_roll(uint8_t roll_angle) {
  _sprites_instrument_xy[kSpriteIdxRoll].x =
      kSpriteOffsetX + kSpriteRollPivotX - 12;
  _sprites_instrument_xy[kSpriteIdxRoll].y =
      kSpriteOffsetY + kSpriteRollPivotY - 10;
  if (roll_angle >= kRollMax / 2) {
    roll_angle -= kRollMax / 2;
  }
  _sprites_instrument_idx[kSpriteIdxRoll] =
      kSpriteDefLongArmBitmap + _sprites_roll_to_bitmap[roll_angle];
}

inline void sprites_set_throttle(uint8_t throttle) {
  _sprites_set_instrument_sprite(
      kSpriteIdxThrottle, kSpriteDefShortArm, kSpriteDefShortArmBitmap,
      (0x14 + throttle) & 0x1f,
      (kSpriteOffsetX + kSpriteThrottlePivotX) & 0xff,
      kSpriteOffsetY + kSpriteThrottlePivotY);
}

inline void sprites_set_fuel(uint32_t fuel) {
  _sprites_set_instrument_sprite(kSpriteIdxFuel, kSpriteDefShortArm,
                                 kSpriteDefShortArmBitmap,
                                 (0x18 + (uint8_t)(fuel >> 13)) & 0x1f,
                                 (kSpriteOffsetX + kSpriteFuelPivotX) & 0xff,
                                 kSpriteOffsetY + kSpriteFuelPivotY);
}

// A terrain sprite that would land on the message text hides for as long as
// the message is up, rather than drawing over it. The test is a box overlap
// against the message span, so a message narrow enough — or a sprite far
// enough to the side — leaves the sprite alone. Coordinates are VIC sprite
// coordinates, i.e. already shifted by kSpriteOffsetX / kSpriteOffsetY.
static const uint8_t kSpritesMsgBoxBottom = kSpriteOffsetY + kMsgHeightPixels;

static bool _sprites_hidden_by_msg(int16_t x, uint8_t vy, uint8_t width) {
  if (!msg_active() || vy >= kSpritesMsgBoxBottom) {
    return false;
  }
  int16_t x0 = kSpriteOffsetX + (int16_t)msg_span_x0;
  int16_t x1 = kSpriteOffsetX + (int16_t)msg_span_x1;
  return x < x1 && x + width > x0;
}

void sprites_stack_reset(void) {
  _sprites_cand_count = 0;
  _sprites_orient_on = false;
  _sprites_fin_on = false;
}

bool sprites_stack_add(int16_t depth, int16_t x, int16_t y, int8_t pivot_x,
                       int8_t pivot_y, uint8_t bitmap, uint8_t bitmap2,
                       uint8_t color, uint8_t flags) {
  // An expanded sprite pixel is two screen pixels, so the horizontal pivot
  // doubles with it and the vertical one does not.
  uint8_t width = kSpriteWidthPixels;
  uint8_t px = (uint8_t)pivot_x;
  if (flags & kSpriteFlagExpandX) {
    width <<= 1;
    px <<= 1;
  }
  x -= px;
  if (flags & kSpriteFlagWide) {
    width <<= 1;
  }
  uint8_t pitch = kSpriteHeightPixels;
  if (flags & kSpriteFlagExpandY) {
    pitch <<= 1;
  }
  uint8_t h_m1 = pitch - 1;
  if (bitmap2 != kSpriteNoBitmap) {
    h_m1 += pitch;
  }
  y -= (uint8_t)pivot_y;
  // (x, y) is now the sprite's top left, still in viewport screen pixels.

  // The dither lattice, in screen space. X_vic = x + 24 and Y_vic = y + 50,
  // and both offsets are even, so "X_vic even and (X_vic >> 1) + Y_vic even"
  // is the same condition as "x even and (x >> 1) + y even" here.
  if (flags & kSpriteFlagAlignDither) {
    x &= ~1;
    y = (y & ~1) | ((x >> 1) & 1);
  }

  if (x >= (int16_t)kScreenWidthPixels || x + width <= 0) {
    return false;
  }
  // Vertically the test is on the *last* hardware sprite of the entry, not on
  // the entry's bottom edge, and the difference is the whole point.
  //
  // The VIC only begins fetching a sprite on the line its Y matches, and
  // clearing $D015 above the split (mem.h) stops any sprite that has not begun
  // by then. A single sprite reaching the cut is therefore clipped, which is
  // what we want. A 1 x 2 stack is not: its lower half is a separate hardware
  // sprite whose Y is 21 lines further down, so if that line falls below the
  // cut the lower half never starts at all and the cloud draws as a flat-topped
  // half - the same symptom as the oscar64 miscompile of §3.4, from an
  // unrelated cause. Rejecting the entry is better than drawing half of it.
  //
  // Both `last_sprite_top >= kSpriteVisibleEndYPixels` and `y + height <= 0`
  // bound `y + height - 1` to `[0, kSpriteVisibleEndYPixels + 20)`. A
  // Y-expanded entry's cut is 21 lines higher and its last sprite 21 lines
  // taller, so the same bound is the Y-expanded cut too (the assert below).
  if ((uint16_t)(y + h_m1) >=
      kSpriteVisibleEndYPixels + kSpriteHeightPixels - 1) {
    return false;
  }

  int16_t vx = x + (int16_t)kSpriteOffsetX;
  uint8_t vy = (uint8_t)y + kSpriteOffsetY;
  // No X wrap yet — clouds.md §1.6 and phase 8. Until then a sprite that would
  // need a negative register position is dropped rather than clipped, which is
  // what sprites_set_sun_position() did before the stack existed.
  if (vx < 0) {
    return false;
  }
  if (_sprites_hidden_by_msg(vx, vy, width)) {
    return false;
  }

  uint8_t i = _sprites_cand_count;
  if (i == kSpriteStackSize) {
    // Full. Only a nearer object earns a place, and it takes the farthest
    // one's.
    if ((uint16_t)depth >=
        (uint16_t)_sprites_cand[kSpriteStackSize - 1].depth) {
      return false;
    }
    i = kSpriteStackSize - 1;
  } else {
    _sprites_cand_count = i + 1;
  }
  while (i > 0) {
    uint8_t prev = i - 1;
    if ((uint16_t)_sprites_cand[prev].depth <= (uint16_t)depth) {
      break;
    }
    _sprites_cand[i] = _sprites_cand[prev];
    i = prev;
  }
  _sprites_cand[i].depth = depth;
  _sprites_cand[i].x = vx;
  _sprites_cand[i].y = vy;
  _sprites_cand[i].bitmap = bitmap;
  _sprites_cand[i].bitmap2 = bitmap2;
  _sprites_cand[i].color = color;
  _sprites_cand[i].flags = flags;
  return true;
}

// --- The orientation indicator ---------------------------------------------

// Hardware sprite 0, and the bit that is its own in $D015: in front of
// every sprite the stack hands out, and the needle's index below the split.
static const uint8_t kSpriteIdxOrient = 0;
static const uint8_t kSpriteOrientBit = 1 << kSpriteIdxOrient;
static_assert(kSpriteIdxOrient == kSpriteIdxVSpeed,
              "the mark and the needle share the one index the stack skips");
static_assert(kSpriteStackFirst == kSpriteIdxOrient + 1 &&
                  kSpriteStackFirst + kSpriteTerrainSlots == kSpriteStackSize,
              "the stack hands out every index but the mark's");

// The mark is a bar with a gap in the middle - xxxxxxxx--------xxxxxxxx on one
// row - drawn by tools/generate_sprites.py into the first block of the sprite
// blob. That block is the one the cloud size ladder's rung 0 used to hold and
// which no cloud can reach: clouds.cc draws a distant group as a single blob a
// rung *larger* than its own, so the smallest row it can index is rung 1's
// (docs/clouds.md §3.5). The blob's 48 blocks are otherwise full, so this dead
// one is the only place the mark could have come from.
//
// The pivot is spelled out rather than read from kSpriteDefOrient because the
// position below is worked out at build time and a `const` struct member is not
// a constant expression. sprites_test.cc fails if the two ever disagree.
static const uint8_t kSpriteOrientPivotX = 12;
static const uint8_t kSpriteOrientPivotY = 10;

// Dead centre of the viewport, in VIC coordinates: the pivot and then the
// VIC's own origin, the same two steps sprites_stack_add() takes per object -
// done once here, since this object never moves.
//
// Not X-expanded, unlike the clouds, so pivot_x does not double and the bar is
// its own 24 pixels wide. Both coordinates fit a byte, so there is no ninth X
// bit to carry and $D010 is left alone.
static const uint8_t kSpriteOrientVX =
    kScreenWidthPixels / 2 - kSpriteOrientPivotX + kSpriteOffsetX;
static const uint8_t kSpriteOrientVY =
    kViewportEndYPixels / 2 - kSpriteOrientPivotY + kSpriteOffsetY;

// No message test, unlike every other viewport sprite: the message strip is
// kMsgHeightPixels tall at the top of the viewport and the bar sits 56 pixels
// below its top, so the two cannot meet. No cull either, and no position - the
// mark is the one thing on screen that does not move.
//
// The front view only. Looking left or right the camera is 90 degrees off the
// nose, so a mark fixed to the middle of the screen would be a reference for an
// attitude nobody is flying: the horizon out of the side window rises and falls
// with *roll*, and reading pitch off it is exactly backwards. Looking back it
// is worse than useless - pitch and roll both read reversed there. The panel
// says the same thing: a side view draws one instrument, the back view draws
// none, and neither draws the roll and pitch dials
// (sprites_show_panel_bottom_sprites()).
//
// The test is here rather than at the call site so that no caller can put the
// mark in a side or back view by forgetting it.
void sprites_set_orientation(void) {
  _sprites_orient_on = view_state == VIEW_CENTER;
}

// --- The tail fin -----------------------------------------------------------
//
// Two hardware sprites in a column down the middle of the viewport, both
// Y-expanded, drawn from two bitmaps: a tapered tip on top and a straight shaft
// below it. Like the orientation indicator it is furniture
// rather than an object - it is bolted to the aeroplane, so it never moves and
// never culls - and like it, it is a flag and a set of constants rather than a
// stack entry.
//
// **Why the lowest two indices.** VIC sprite-to-sprite priority is index
// order, and the fin is four metres away while everything else the stack
// hands out is a cloud, the sun or an opponent. Taking 0 and 1 is what puts it
// in front of all of them; anything higher and a distant cloud would draw over
// the tail. 0 is the orientation indicator's in the front view, and the mark
// is never up in the back. The stack starts above the fin for as long as it
// is up, so the back view has six slots for clouds instead of seven - the
// entries that lose are the farthest, which is the stack's ordinary overflow
// rule.

static const uint8_t kSpriteFinFirst = 0;
static const uint8_t kSpriteFinCount = 2;
static const uint8_t kSpriteFinMask = ((1 << kSpriteFinCount) - 1)
                                      << kSpriteFinFirst;

// A Y-expanded sprite is 42 raster lines tall, so the two tile the column
// with no seam when they are 42 apart.
static const uint8_t kSpriteFinPitch = 2 * kSpriteHeightPixels;

// The whole DMA argument below is written in raster lines and read out of
// sprite Y registers, which is only the same number because these two agree.
static_assert(kSpriteOffsetY == kRasterScreenYStart,
              "sprite Y and raster line no longer count from the same place");

// Where the column sits. X is the sprite's own middle on the screen's, the same
// two steps kSpriteOrientVX takes. Y is fixed at the bottom by the hardware:
// mem.h derives the lowest line a Y-expanded sprite may start on from its 42
// lines of DMA and the cycle-counted panel split below it.
//
// **And then two lines lower than that, which closes the gap between the shaft
// and the panel.** Left on the line mem.h names, the shaft stops on raster 159
// and the panel starts at 162, so two lines of ground show between the tail and
// the instruments. These two lines are what remove them: the shaft's last line
// becomes 161, the viewport's own last line, and the fin meets the panel.
//
// **Why that is allowed here when kSpritesOffLead forbids it everywhere else.**
// mem.h's limit is derived from, and measured with, *seven* sprites parked at
// the swept Y, all seven of them still fetching across the split - that is the
// case where the handler came out 55 cycles late and a row of terrain charset
// was drawn over the panel. The fin is not that case. It is two sprites, and
// they are indices 0 and 1, whose data for a line is fetched in the tail
// cycles of the line *before* it; the five slots the stack can still hand out
// in this view are culled against kSpritesOffLead as they always were, so they
// are finished by raster 159 and nothing of theirs is in flight here.
//
// Measured rather than argued, which is the rule this file inherited from
// mem.h: with the stack full and every entry of it starting on the lowest line
// kSpritesOffLead allows, the panel is byte-identical to the build with the fin
// two lines higher - over six frames in x64sc and three in xscpu64, whose
// handler has its own NOP count and its own window. The fin never moves, so
// unlike an object passing through the band there is no "sometimes" for it to
// hide in: it is the same sprites on the same lines every frame. (Measured
// with a three-sprite fin on indices 0-2; the two-sprite fin's DMA is a subset
// of that, and the slot it gave back to the stack is culled like the others.
// Measured again for the stack's six slots on 2-7, eight sprites in the band:
// all six on the lowest line beside the fin, and the panel pixel identical to
// the frame without them, in x64sc and xscpu64.)
//
// None of that generalises. It is an exemption for one fixed object on the
// lowest indices, not a new limit - docs/sprite_objects.md 0 says what a cloud
// would have to re-derive before it could do the same.
//
// The tip sprite sits one pitch above it, at 78, and is inked from its first
// row, so the fin is the full 84 lines two Y-expanded sprites can give.
static const uint8_t kSpriteFinPivotX = 12;
static const uint8_t kSpriteFinVX =
    kScreenWidthPixels / 2 - kSpriteFinPivotX + kSpriteOffsetX;
static const uint8_t kSpriteFinDropLines = 2;
static const uint8_t kSpriteFinBottomVY = kSpriteOffsetY + kViewportEndYPixels -
                                          1 - kSpritesOffLeadExpandY +
                                          kSpriteFinDropLines;
static const uint8_t kSpriteFinTopVY =
    kSpriteFinBottomVY - (kSpriteFinCount - 1) * kSpriteFinPitch;

// The last line the bottom sprite draws on, against the viewport's last line.
// Equal is the whole point - that is the gap being closed - and one lower is a
// sprite drawing into the panel, which is both a fin poking below the horizon
// strip and a sprite whose DMA really does run into the handler's window. This
// is the check kSpriteFinDropLines has to pass now that it has spent mem.h's
// spare line and the margin line under it.
static_assert(kSpriteFinBottomVY + kSpriteFinPitch - 1 <=
                  kSpriteOffsetY + kViewportEndYPixels - 1,
              "the fin's lowest sprite draws past the viewport into the panel");

// --- The art ---------------------------------------------------------------
//
// Drawn at run time rather than embedded, because it has to be: the two blocks
// live at $FF40 (mem.h kFinSpriteData), outside the $D400 blob and past the end
// of what a .prg can load, so something has to write them either way. Every row
// of both bitmaps is a horizontal run centred in the sprite, so "either way" is
// nine numbers rather than 126 bytes of bitmap.
//
// Both sprites are inked from their first row to their last, which is the
// tallest fin two sprites can make: the taper is the top rows of the tip
// sprite, and the rest of the tip is the shaft. The shaft is a constant width,
// so the seam between the two sprites is invisible.
static const uint8_t kSpriteFinBodyWidth = 12;

// The taper, from the tip sprite's row 0 down. Last entry spelled as the
// shaft's width rather than as 12, so the taper meets the shaft by
// construction. A `const` array is not a constant expression on either
// compiler, so this is the only way to say it at compile time; sprites_test.cc
// checks the rest of the shape.
static const uint8_t kSpriteFinTipWidths[] = {2,  4,  6, 6,
                                              8, 10, 10, kSpriteFinBodyWidth};
static const uint8_t kSpriteFinTipRows = sizeof(kSpriteFinTipWidths);

// The fin's first inked line, which with the tip inked from row 0 is the top
// sprite's own Y. That is what keeps the fin out of the message strip: the
// message occupies row 0 of the viewport and nothing else does, so a fin whose
// ink starts below it can never draw over the text. The orientation indicator
// clears the same strip the same way, by sitting below it rather than by
// testing for it - which is worth more than a test here, because the
// alternative for a fixed object is to blink the whole tail out whenever a
// message appears.
static const uint8_t kSpriteFinInkTopVY = kSpriteFinTopVY;

static_assert(kSpriteFinTipRows <= kSpriteHeightPixels,
              "the fin tip's taper is taller than the sprite");
static_assert(kSpriteFinBodyWidth <= kSpriteWidthPixels,
              "the fin is wider than a sprite");
static_assert(kSpriteFinInkTopVY >= kSpriteOffsetY + kMsgHeightPixels,
              "the fin's tip reaches into the message row");

// One row of one bitmap: `width` pixels centred in the sprite's 24, and
// nothing else.
static void _sprites_draw_fin_row(uint8_t *dst, uint8_t width) {
  dst[0] = 0;
  dst[1] = 0;
  dst[2] = 0;
  uint8_t x = (kSpriteWidthPixels - width) >> 1;
  for (uint8_t n = width; n != 0; --n) {
    dst[x >> 3] |= kBit[(x & 7) ^ 7];
    ++x;
  }
}

// Both blocks. Called from sprites_init(), which is every entry to the
// simulation and every return from the map - more often than the once this
// needs, and cheap enough at a thousand-odd loop iterations on a screen
// transition that pinning it to boot would buy nothing.
//
// **Two passes: shaft everywhere, then the taper painted over the tip's top
// rows.** Both arrays are indexed from zero that way, which is worth keeping
// even now the taper starts on row 0: a one-pass version with a guarded,
// biased index into kSpriteFinTipWidths is what oscar64 v1.32.272-117
// miscompiled when the taper started on row 13 (the guard was discarded and
// the rows above the taper read the bytes in front of the array). See
// bugs/const-array-ternary-guard/.
static void _sprites_draw_fin(void) {
  uint8_t *tip = kFinSpriteData;
  uint8_t *shaft = kFinSpriteData + 64;
  for (uint8_t row = 0; row < kSpriteHeightPixels; ++row) {
    _sprites_draw_fin_row(tip, kSpriteFinBodyWidth);
    _sprites_draw_fin_row(shaft, kSpriteFinBodyWidth);
    tip += 3;
    shaft += 3;
  }
  uint8_t *ink = kFinSpriteData;
  for (uint8_t i = 0; i < kSpriteFinTipRows; ++i) {
    _sprites_draw_fin_row(ink, kSpriteFinTipWidths[i]);
    ink += 3;
  }
}

// The back view only, and by the same argument that keeps the orientation
// indicator to the front view: this is the aeroplane's own tail, so it belongs
// in the one view the tail is in front of the camera in. The test is here
// rather than at the call site so that no caller can leave the fin standing in
// a view that has flown round it.
//
// Per frame, like the stack and the mark: sprites_stack_reset() clears this,
// world.cc sets it, sprites_stack_commit() publishes it.
void sprites_set_fin(void) { _sprites_fin_on = view_state == VIEW_BACK; }

void sprites_stack_commit(void) {
  // Main line, so a pointer is fine here - the restriction above is on the
  // interrupt side only.
  // The frame of the buffer being drawn into (see the frames above).
  volatile sprite_frame_t *f =
      mem_using_alt_buffer ? &_sprites_frame_b : &_sprites_frame_a;

  // Above the mark's index, or the fin's two when it is up, so that those
  // keep the lowest indices and with them the priority - see the tail fin
  // section. The shift is by a constant either way, which is what the two
  // assignments are for.
  uint8_t idx = kSpriteStackFirst;
  uint8_t bit = 1 << kSpriteStackFirst;
  if (_sprites_fin_on) {
    idx = kSpriteFinFirst + kSpriteFinCount;
    bit = 1 << (kSpriteFinFirst + kSpriteFinCount);
  }
  uint8_t msbx = 0;
  uint8_t expand = 0;
  uint8_t expand_y = 0;
  uint8_t enable = 0;

  for (uint8_t i = 0; i < _sprites_cand_count; ++i) {
    const sprite_cand_t *c = &_sprites_cand[i];
    uint8_t flags = c->flags;
    uint8_t cols = (flags & kSpriteFlagWide) ? 2 : 1;
    uint8_t slots =
        (c->bitmap2 == kSpriteNoBitmap) ? cols : (uint8_t)(cols << 1);
    // Up to the last index: the one skipped, 0, belongs to the mark and the
    // vertical-speed needle (sprites.h).
    if (idx + slots > kSpriteStackSize) {
      // The list is sorted, so everything after this is farther still.
      break;
    }
    // Row by row, left to right: the right-hand sprite of a wide entry is
    // one sprite width on and shows the next block, and each row after the
    // first starts at bitmap2, one sprite height down.
    uint8_t ptr = c->bitmap;
    uint8_t sy = c->y;
    uint8_t col_w = (flags & kSpriteFlagExpandX) ? 2 * kSpriteWidthPixels
                                                 : kSpriteWidthPixels;
    uint8_t pitch = (flags & kSpriteFlagExpandY) ? 2 * kSpriteHeightPixels
                                                 : kSpriteHeightPixels;
    for (uint8_t s = 0; s < slots; ++s) {
      uint16_t sx = (uint16_t)c->x;
      uint8_t p = ptr;
      if ((cols & 2) && (s & 1)) {
        sx += col_w;
        ++p;
      }
      f->pos[idx << 1] = (uint8_t)sx;
      f->pos[(idx << 1) + 1] = sy;
      f->ptr[idx] = p;
      f->color[idx] = c->color;
      enable |= bit;
      if (sx & 0x100) {
        msbx |= bit;
      }
      if (flags & kSpriteFlagExpandX) {
        expand |= bit;
      }
      if (flags & kSpriteFlagExpandY) {
        expand_y |= bit;
      }
      if (s + 1 == cols) {
        ptr = c->bitmap2;
        sy += pitch;
      }
      bit <<= 1;
      ++idx;
    }
  }

  // Unused indices are left disabled rather than parked at x = 0. Parking
  // hides a sprite but still costs its DMA on every line it would have been
  // displayed on; clearing the enable bit costs neither, and it is one store
  // in the handler instead of eight.
  while (idx < kSpriteStackSize) {
    f->pos[idx << 1] = 0;
    f->pos[(idx << 1) + 1] = 0;
    f->ptr[idx] = kSpriteDefSun.bitmap_idx;
    f->color[idx] = kColorInstrument;
    ++idx;
  }
  // Sprite 0, the one index the stack never hands out, so the orientation
  // indicator takes it whether the stack overflowed or was empty. A frame
  // that sets no marker parks it like the fill above, disabled and in the
  // needle's colour.
  uint8_t orient_x = 0, orient_y = 0;
  uint8_t orient_ptr = kSpriteDefSun.bitmap_idx;
  uint8_t orient_color = kColorInstrument;
  if (_sprites_orient_on) {
    orient_x = kSpriteOrientVX;
    orient_y = kSpriteOrientVY;
    orient_ptr = kSpriteDefOrient.bitmap_idx;
    orient_color = kColorOrientation;
    enable |= kSpriteOrientBit;
  }
  f->pos[kSpriteIdxOrient << 1] = orient_x;
  f->pos[(kSpriteIdxOrient << 1) + 1] = orient_y;
  f->ptr[kSpriteIdxOrient] = orient_ptr;
  f->color[kSpriteIdxOrient] = orient_color;

  // The fin, into the two indices the loop above skipped - over the mark's
  // parked 0 - so it is written last, and reaches the same frame whether the
  // stack overflowed or was empty. One pointer variable rather than a table: the top sprite is the tip
  // and everything below it is the shaft, which is the whole of the art.
  if (_sprites_fin_on) {
    uint8_t sy = kSpriteFinTopVY;
    uint8_t ptr = kFinSpriteBlock;
    for (uint8_t i = kSpriteFinFirst; i < kSpriteFinFirst + kSpriteFinCount;
         ++i) {
      f->pos[i << 1] = kSpriteFinVX;
      f->pos[(i << 1) + 1] = sy;
      f->ptr[i] = ptr;
      f->color[i] = kColorAircraft;
      sy += kSpriteFinPitch;
      ptr = kFinSpriteBlock + 1;
    }
    enable |= kSpriteFinMask;
    expand_y |= kSpriteFinMask;
  }

  f->msbx = msbx;
  f->expand = expand;
  f->expand_y = expand_y;
  f->enable = enable;
}

// Programs one frame into the VIC. A macro, and a duplicated loop, rather than
// the obvious helper taking a `const sprite_frame_t *` - which is what this was
// first, and it corrupted the whole screen.
//
// **Nothing reached from a raster interrupt may touch oscar64's runtime zero
// page.** A pointer parameter makes the compiler stage the address in ACCU
// ($27) and T1..T3 ($33-$38) and read through `(zp),y`. Those bytes are the
// runtime's, shared with the main line, and this runs at raster 250 straight
// through whatever the renderer was in the middle of. Naming the frame instead
// makes every base address a link-time constant, so the loop compiles to
// absolute-indexed loads and stores and touches no zero page at all.
//
// tools/check_irq_zp.py enforces this on every build; mem.h's zero page comment
// is where the $00-$5F layout it checks against comes from.
// Two loops rather than one, for the same register-pressure reason as the
// layout above: each carries a single index and a single value, which is what
// a 6502 has room for. Neither is unrolled - this sits at raster 250 in the
// bottom border with ~58 PAL lines of slack after sound_blit(), so the cycles
// are raster lines nobody is waiting on, and unrolling two copies of it would
// cost around 500 bytes.
#define _sprites_program_frame(F)                                              \
  do {                                                                         \
    /* Counts down. Counting up made oscar64 increment the index before the  */ \
    /* store, so it kept the pre-increment copy in X and shuffled the two    */ \
    /* through ACCU - which is exactly the zero page this must not touch.    */ \
    /* Downward, one register indexes both sides and nothing spills.         */ \
    uint8_t i = 16;                                                            \
    do {                                                                       \
      --i;                                                                     \
      kVicSpritePos[i] = (F).pos[i];                                           \
    } while (i != 0);                                                          \
    for (uint8_t i = 0; i < 8; i++) {                                          \
      *(kScreenRamMain + 1016 + i) = (F).ptr[i];                               \
      *(kScreenRamAlt + 1016 + i) = (F).ptr[i];                                \
      vic.spr_color[i] = (F).color[i];                                         \
    }                                                                          \
    vic.spr_msbx = (F).msbx;                                                   \
    vic.spr_expand_x = (F).expand;                                             \
    vic.spr_expand_y = (F).expand_y;                                           \
    vic.spr_enable = (F).enable;                                               \
  } while (0)

// The frame of the buffer going on screen: the main one while the main line
// draws into the alt one, and the other way round.
inline void sprites_show_terrain_sprites() {
  if (mem_using_alt_buffer) {
    _sprites_program_frame(_sprites_frame_a);
  } else {
    _sprites_program_frame(_sprites_frame_b);
  }
}

inline void sprites_show_no_sprites() {
  vic.spr_enable = 0;
  vic.spr_expand_x = 0;
  vic.spr_expand_y = 0;
  vic.spr_msbx = 0;
}

inline void sprites_show_panel_top_sprites() {
  // Everything the terrain band set up has to be undone before the panel is
  // drawn, and two of these are easy to miss:
  //
  // - $D01D. Parking at x = 0 hides a 24 pixel sprite, because the left border
  //   ends at 24 and the VIC compares X per raster line. It does *not* hide an
  //   X-expanded one, which is 48 wide and would poke 24 pixels into the panel.
  // - Sprite 0's colour. It is the vertical speed needle, the one instrument
  //   drawn in this band, and the terrain handler now writes all eight colours.
  // - $D017, for the same reason as $D01D: the back view's tail fin leaves
  //   sprites 0 and 1 Y-expanded, and two of the instruments are those
  //   two. Nothing shows it in the back view itself, which draws no
  //   instruments at all - it shows on the frame the view switches away on,
  //   where the fin's expansion would still be set while the fuel, speed and
  //   roll needles are drawn.
  vic.spr_enable = 0xFF;
  vic.spr_expand_x = 0;
  vic.spr_expand_y = 0;
  vic.spr_color[kSpriteIdxVSpeed] = kColorInstrument;
// Guarded because clang - which is what `g++` is on macOS - *recognises*
// `#pragma unroll` and then rejects `full` as its argument, so it is a hard
// error rather than something -Wno-unknown-pragmas can wave through. gcc treats
// the whole pragma as unknown and ignores it, so the host build only breaks on
// one of the two. oscar64 has no _Pragma, so this cannot be hidden in a macro.
#ifdef __OSCAR64__
#pragma unroll(full)
#endif
  for (uint8_t i = 1; i < 8; i++) {
    vic.spr_pos[i].x = 0;
  }
  vic.spr_msbx = 0;
  if (view_state == VIEW_CENTER) {
    vic.spr_pos[kSpriteIdxVSpeed].x =
        _sprites_instrument_xy[kSpriteIdxVSpeed].x;
    vic.spr_pos[kSpriteIdxVSpeed].y =
        _sprites_instrument_xy[kSpriteIdxVSpeed].y;
  } else {
    vic.spr_pos[kSpriteIdxVSpeed].x = 0;
  }
  *(kScreenRamMain + 1016 + kSpriteIdxVSpeed) =
      _sprites_instrument_idx[kSpriteIdxVSpeed];
  *(kScreenRamAlt + 1016 + kSpriteIdxVSpeed) =
      _sprites_instrument_idx[kSpriteIdxVSpeed];
}

inline void sprites_show_panel_bottom_sprites() {
  // All eight colours, unconditionally. This band is below the split and is not
  // cycle critical, and doing it here is what frees the terrain handler to
  // write every colour without a handshake with the panel code - which is why
  // the old kIdxThrottle == kIdxSun and kIdxFuel == kIdxSun special cases are
  // gone.
  uint8_t i = 8;
  do {
    --i;
    vic.spr_color[i] = kColorInstrument;
  } while (i != 0);
  if (view_state == VIEW_CENTER) {
    // Sprites 1-7: 0 is the vertical-speed needle the band above placed.
    uint8_t j = 16;
    do {
      --j;
      kVicSpritePos[j] = ((volatile uint8_t *)_sprites_instrument_xy)[j];
    } while (j != 2);
    uint8_t k = 8;
    do {
      --k;
      uint8_t ptr = _sprites_instrument_idx[k];
      *(kScreenRamMain + 1016 + k) = ptr;
      *(kScreenRamAlt + 1016 + k) = ptr;
    } while (k != 1);
    vic.spr_msbx = (1 << kSpriteIdxThrottle);
  } else if (view_state == VIEW_LEFT) {
    vic.spr_pos[kSpriteIdxFuel].x = _sprites_instrument_xy[kSpriteIdxFuel].x;
    vic.spr_pos[kSpriteIdxFuel].y = _sprites_instrument_xy[kSpriteIdxFuel].y;
    *(kScreenRamMain + 1016 + kSpriteIdxFuel) =
        _sprites_instrument_idx[kSpriteIdxFuel];
    *(kScreenRamAlt + 1016 + kSpriteIdxFuel) =
        _sprites_instrument_idx[kSpriteIdxFuel];
    vic.spr_msbx = (1 << kSpriteIdxFuel);
  } else if (view_state == VIEW_RIGHT) {
    vic.spr_pos[kSpriteIdxThrottle].x =
        _sprites_instrument_xy[kSpriteIdxThrottle].x;
    vic.spr_pos[kSpriteIdxThrottle].y =
        _sprites_instrument_xy[kSpriteIdxThrottle].y;
    *(kScreenRamMain + 1016 + kSpriteIdxThrottle) =
        _sprites_instrument_idx[kSpriteIdxThrottle];
    *(kScreenRamAlt + 1016 + kSpriteIdxThrottle) =
        _sprites_instrument_idx[kSpriteIdxThrottle];
    // Assume vic.spr_msbx is already 0
  }
  // VIEW_BACK falls out with nothing done, which is the whole of it: the back
  // view shows no dashboard, so it shows no instrument either.
  // sprites_show_panel_top_sprites() has already parked all eight sprites at
  // x = 0, where the left border hides them, and left $D010 zero, so every
  // needle is off screen for this band and the sprite pointers at +1016 are
  // stale but never read.
}
