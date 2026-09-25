#include "sprites.h"

#include "color.h"
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

static const uint8_t kSpriteSpeedPivotX = 112;
static const uint8_t kSpriteSpeedPivotY = 176;
static const uint8_t kSpriteAltPivotX = 208;
static const uint8_t kSpriteAltPivotY = 176;
static const uint8_t kSpriteVSpeedPivotX = 230;
static const uint8_t kSpriteVSpeedPivotY = 138;
static const uint8_t kSpriteRollPivotX = 160;
static const uint8_t kSpriteRollPivotY = 176;
static const uint8_t kSpriteThrottlePivotX = 8; // 264 & 0xff
static const uint8_t kSpriteThrottlePivotY = 166;
static const uint8_t kSpriteFuelPivotX = 56;
static const uint8_t kSpriteFuelPivotY = 166;

static const uint8_t kSpriteIdxFuel = 0;
static const uint8_t kSpriteIdxSpeed = 1;
static const uint8_t kSpriteIdxRoll = 2;
static const uint8_t kSpriteIdxPitch = 3;
static const uint8_t kSpriteIdxAlt1 = 4;
static const uint8_t kSpriteIdxAlt2 = 5;
static const uint8_t kSpriteIdxThrottle = 6;
static const uint8_t kSpriteIdxVSpeed = 7;

struct sprite_xy_t {
  uint8_t x;
  uint8_t y;
};

// The stack's storage stays in the main bss rather than in bss2. bss2 is the
// 1.4 KB gap at $0280-$07FF and it is already full - poly.cc's scratch buffers
// are what gets evicted if anything else moves in - while neither of these
// needs to be anywhere in particular. Only the instrument arrays below stay
// there, and only because they were there first.
//
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
// sprite in the wrong place. Commit fills the back frame and then stores the
// index, and a single byte store is atomic on a 6502, so no sei is needed.
// Two named objects rather than an array of two, because the raster handler
// must reach them by a *constant* address - see the macro below.
//
// **The frames are volatile as well as the index, and "then" depends on it.**
// C orders a volatile access only against other volatile accesses, so with
// plain frames oscar64 was free to move the index store - the last statement
// of sprites_stack_commit() - up to the top of it, next to the load it is
// computed from, and it did: the flip landed before a single byte of the back
// frame was written. Whenever raster 250 fell inside the ~1,000 cycles of
// commit, the handler programmed a half-filled frame whose enable mask and
// most of whose sprites were two game frames old. One PAL frame of that is a
// near cloud drawn with a stale bitmap and a far one blinking in beside it -
// the cloud flicker that flying straight at a cloud group shows best.
// Volatile frames make every write into them ordered against the flip.
static volatile sprite_frame_t _sprites_frame_a;
static volatile sprite_frame_t _sprites_frame_b;
static volatile uint8_t _sprites_frame_shown;

#pragma bss(bss2)

static volatile sprite_xy_t _sprites_instrument_xy[8];
static volatile uint8_t _sprites_instrument_idx[8];

// Defined with the rest of the tail fin, below; sprites_init() is the only
// caller and it comes first in this file.
static void _sprites_draw_fin(void);

inline void sprites_init(void) {
  for (uint8_t i = 0; i < 8; i++) {
    vic.spr_color[i] = kColorInstrument;
  }

  // Commit an empty stack twice. Both frames end up holding "no objects", so a
  // terrain interrupt arriving before the first world_update_objects() programs
  // nothing rather than whatever bss happened to contain - and the two flips
  // leave the front buffer back where it started. Cheaper in bytes than writing
  // the two frames out by hand, and it cannot drift from what commit() means by
  // an empty frame.
  sprites_stack_reset();
  sprites_stack_commit();
  sprites_stack_commit();

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

static void _sprites_set_instrument_sprite(uint8_t idx,
                                           const sprite_meta_t *meta_array,
                                           uint8_t dir, uint8_t pivot_x,
                                           uint8_t pivot_y) {
  const sprite_meta_t *meta = &meta_array[dir];
  _sprites_instrument_xy[idx].x = pivot_x - meta->pivot_x;
  _sprites_instrument_xy[idx].y = pivot_y - meta->pivot_y;
  _sprites_instrument_idx[idx] = meta->bitmap_idx;
}

inline void sprites_set_speed(uint8_t speed) {
  _sprites_set_instrument_sprite(
      kSpriteIdxSpeed, kSpriteDefMetaLongArm, (speed >> 1) & 0x1f,
      kSpriteOffsetX + kSpriteSpeedPivotX, kSpriteOffsetY + kSpriteSpeedPivotY);
}

inline void sprites_set_alt(uint16_t alt) {
  _sprites_set_instrument_sprite(
      kSpriteIdxAlt1, kSpriteDefMetaLongArm, (alt >> 4) & 0x1f,
      kSpriteOffsetX + kSpriteAltPivotX, kSpriteOffsetY + kSpriteAltPivotY);
  // ((alt >> 4) * 205) >> 11 needs a 32-bit intermediate; vec_fastmul8p8
  // returns the exact middle 16 bits ((t * 205) >> 8), so shifting 3 more
  // is bit-identical without pulling in the mul32 runtime.
  _sprites_set_instrument_sprite(
      kSpriteIdxAlt2, kSpriteDefMetaShortArm,
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
  _sprites_set_instrument_sprite(kSpriteIdxVSpeed, kSpriteDefMetaLongArm, dir,
                                 kSpriteOffsetX + kSpriteVSpeedPivotX,
                                 kSpriteOffsetY + kSpriteVSpeedPivotY);
}

static const uint8_t _sprites_roll_to_dir[kRollMax] = {
    8,  7,  7,  6,  6,  5,  5,  4,  4,  3,  3,  2,  2,  1,  1,
    0,  31, 31, 30, 30, 29, 29, 28, 28, 27, 27, 26, 26, 25, 25,
    24, 23, 23, 22, 22, 21, 21, 20, 20, 19, 19, 18, 18, 17, 17,
    16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9,  9};

inline void sprites_set_pitch(int8_t pitch_angle) {
  const sprite_meta_t *meta = &kSpriteDefMetaLongArm[8];
  _sprites_instrument_xy[kSpriteIdxPitch].x =
      kSpriteOffsetX + kSpriteRollPivotX - 12;
  _sprites_instrument_xy[kSpriteIdxPitch].y =
      kSpriteOffsetY + kSpriteRollPivotY - 10 - (pitch_angle >> 2);
  _sprites_instrument_idx[kSpriteIdxPitch] = meta->bitmap_idx;
}

inline void sprites_set_roll(uint8_t roll_angle) {
  const sprite_meta_t *meta =
      &kSpriteDefMetaLongArm[_sprites_roll_to_dir[roll_angle]];
  _sprites_instrument_xy[kSpriteIdxRoll].x =
      kSpriteOffsetX + kSpriteRollPivotX - 12;
  _sprites_instrument_xy[kSpriteIdxRoll].y =
      kSpriteOffsetY + kSpriteRollPivotY - 10;
  _sprites_instrument_idx[kSpriteIdxRoll] = meta->bitmap_idx;
}

inline void sprites_set_throttle(uint8_t throttle) {
  _sprites_set_instrument_sprite(
      kSpriteIdxThrottle, kSpriteDefMetaShortArm, (0x14 + throttle) & 0x1f,
      (kSpriteOffsetX + kSpriteThrottlePivotX) & 0xff,
      kSpriteOffsetY + kSpriteThrottlePivotY);
}

inline void sprites_set_fuel(uint32_t fuel) {
  _sprites_set_instrument_sprite(kSpriteIdxFuel, kSpriteDefMetaShortArm,
                                 (0x18 + (uint8_t)(fuel >> 13)) & 0x1f,
                                 (kSpriteOffsetX + kSpriteFuelPivotX) & 0xff,
                                 kSpriteOffsetY + kSpriteFuelPivotY);
}

// A terrain sprite that would land on the message text hides for as long as
// the message is up, rather than drawing over it. The test is a box overlap
// against the message span, so a message narrow enough — or a sprite far
// enough to the side — leaves the sprite alone. Coordinates are VIC sprite
// coordinates, i.e. already shifted by kSpriteOffsetX / kSpriteOffsetY.
static const int16_t kSpritesMsgBoxTop = kSpriteOffsetY;
static const int16_t kSpritesMsgBoxBottom = kSpriteOffsetY + kMsgHeightPixels;

static bool _sprites_hidden_by_msg(int16_t x, int16_t y, uint8_t width,
                                   uint8_t height) {
  if (!msg_active()) {
    return false;
  }
  if (y >= kSpritesMsgBoxBottom || y + (int16_t)height <= kSpritesMsgBoxTop) {
    return false;
  }
  int16_t x0 = kSpriteOffsetX + (int16_t)msg_span_x0;
  int16_t x1 = kSpriteOffsetX + (int16_t)msg_span_x1;
  return x < x1 && x + (int16_t)width > x0;
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
  if (flags & kSpriteFlagExpandX) {
    width <<= 1;
    x -= (int16_t)pivot_x << 1;
  } else {
    x -= (int16_t)pivot_x;
  }
  uint8_t height = kSpriteHeightPixels;
  if (bitmap2 != kSpriteNoBitmap) {
    height <<= 1;
  }
  y -= (int16_t)pivot_y;
  // (x, y) is now the sprite's top left, still in viewport screen pixels.

  // The dither lattice, in screen space. X_vic = x + 24 and Y_vic = y + 50,
  // and both offsets are even, so "X_vic even and (X_vic >> 1) + Y_vic even"
  // is the same condition as "x even and (x >> 1) + y even" here.
  if (flags & kSpriteFlagAlignDither) {
    x &= ~1;
    y = (y & ~1) | ((x >> 1) & 1);
  }

  if (x >= (int16_t)kScreenWidthPixels || x + (int16_t)width <= 0) {
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
  // For a single sprite this is exactly `y >= kSpriteVisibleEndYPixels`; for a
  // stack it is 21 lines stricter.
  const int16_t last_sprite_top =
      y + (int16_t)height - (int16_t)kSpriteHeightPixels;
  if (last_sprite_top >= (int16_t)kSpriteVisibleEndYPixels ||
      y + (int16_t)height <= 0) {
    return false;
  }

  int16_t vx = x + (int16_t)kSpriteOffsetX;
  int16_t vy = y + (int16_t)kSpriteOffsetY;
  // No X wrap yet — clouds.md §1.6 and phase 8. Until then a sprite that would
  // need a negative register position is dropped rather than clipped, which is
  // what sprites_set_sun_position() did before the stack existed.
  if (vx < 0) {
    return false;
  }
  if (_sprites_hidden_by_msg(vx, vy, width, height)) {
    return false;
  }

  uint8_t i = _sprites_cand_count;
  if (i == kSpriteStackSize) {
    // Full. Only a nearer object earns a place, and it takes the farthest
    // one's.
    if (depth >= _sprites_cand[kSpriteStackSize - 1].depth) {
      return false;
    }
    i = kSpriteStackSize - 1;
  } else {
    _sprites_cand_count = i + 1;
  }
  while (i > 0 && _sprites_cand[i - 1].depth > depth) {
    _sprites_cand[i] = _sprites_cand[i - 1];
    --i;
  }
  _sprites_cand[i].depth = depth;
  _sprites_cand[i].x = vx;
  _sprites_cand[i].y = (uint8_t)vy;
  _sprites_cand[i].bitmap = bitmap;
  _sprites_cand[i].bitmap2 = bitmap2;
  _sprites_cand[i].color = color;
  _sprites_cand[i].flags = flags;
  return true;
}

// --- The orientation indicator ---------------------------------------------

// Hardware sprite 7, and the bit that is its own in $D015.
static const uint8_t kSpriteIdxOrient = 7;
static const uint8_t kSpriteOrientBit = 1 << kSpriteIdxOrient;

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
// Three hardware sprites in a column down the middle of the viewport, all three
// Y-expanded, drawn from two bitmaps: a tapered tip on top and a straight shaft
// used for the two below it. Like the orientation indicator it is furniture
// rather than an object - it is bolted to the aeroplane, so it never moves and
// never culls - and like it, it is a flag and a set of constants rather than a
// stack entry.
//
// **Why the lowest three indices.** VIC sprite-to-sprite priority is index
// order, and the fin is four metres away while everything else the stack hands
// out is a cloud or the sun. Taking 0, 1 and 2 is what puts it in front of all
// of them; anything higher and a distant cloud would draw over the tail. The
// stack starts above the fin for as long as it is up, so the back view has
// four slots for clouds instead of seven - the entries that lose are the
// farthest, which is the stack's ordinary overflow rule.

static const uint8_t kSpriteFinCount = 3;
static const uint8_t kSpriteFinMask = (1 << kSpriteFinCount) - 1;

// A Y-expanded sprite is 42 raster lines tall, so the three tile the column
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
// was drawn over the panel. The fin is not that case. It is three sprites, and
// they are indices 0, 1 and 2, whose data for a line is fetched in the tail
// cycles of the line *before* it; the four slots the stack can still hand out
// in this view are culled against kSpritesOffLead as they always were, so they
// are finished by raster 159 and nothing of theirs is in flight here.
//
// Measured rather than argued, which is the rule this file inherited from
// mem.h: with the stack full and every entry of it starting on the lowest line
// kSpritesOffLead allows, the panel is byte-identical to the build with the fin
// two lines higher - over six frames in x64sc and three in xscpu64, whose
// handler has its own NOP count and its own window. The fin never moves, so
// unlike an object passing through the band there is no "sometimes" for it to
// hide in: it is the same three sprites on the same three lines every frame.
//
// None of that generalises. It is an exemption for one fixed object on the
// lowest indices, not a new limit - docs/sprite_objects.md 0 says what a cloud
// would have to re-derive before it could do the same.
//
// The other two sprites follow upwards, which puts the top one at 36 - in the
// top border, where the border clips it, and where the tip's empty rows are
// anyway.
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
// The shaft is a constant width, which is what lets one bitmap serve two of the
// three sprites: a taper repeated would step back to narrow at the seam. What
// tapers is the tip, and it ends on the shaft's width so that seam is invisible
// too.
static const uint8_t kSpriteFinBodyWidth = 12;

// The tip's first inked row. Everything above it is transparent, and that is
// what keeps the fin out of the message strip: the message occupies row 0 of
// the viewport and nothing else does, so a tip whose ink starts below it can
// never draw over the text. The orientation indicator clears the same strip the
// same way, by sitting below it rather than by testing for it - which is worth
// more than a test here, because the alternative for a fixed object is to blink
// the whole tail out whenever a message appears.
static const uint8_t kSpriteFinTipFirstRow = 13;
// Last entry spelled as the shaft's width rather than as 12, so the seam
// between the two bitmaps matches by construction. A `const` array is not a
// constant expression on either compiler, so this is the only way to say it at
// compile time; sprites_test.cc checks the rest of the shape.
static const uint8_t kSpriteFinTipWidths[] = {2,  4,  6, 6,
                                              8, 10, 10, kSpriteFinBodyWidth};
static const uint8_t kSpriteFinTipRows = sizeof(kSpriteFinTipWidths);

static const uint8_t kSpriteFinInkTopVY =
    kSpriteFinTopVY + 2 * kSpriteFinTipFirstRow;

static_assert(kSpriteFinTipFirstRow + kSpriteFinTipRows == kSpriteHeightPixels,
              "the fin tip's widths do not fill the rest of the sprite");
static_assert(kSpriteFinBodyWidth <= kSpriteWidthPixels,
              "the fin is wider than a sprite");
static_assert(kSpriteFinInkTopVY >= kSpriteOffsetY + kMsgHeightPixels,
              "the fin's tip reaches into the message row");

// One row of one bitmap: `width` pixels centred in the sprite's 24, and
// nothing else. Width 0 leaves the row blank, which is what the tip's upper
// rows ask for.
static void _sprites_draw_fin_row(uint8_t *dst, uint8_t width) {
  dst[0] = 0;
  dst[1] = 0;
  dst[2] = 0;
  uint8_t x = (kSpriteWidthPixels - width) >> 1;
  for (uint8_t n = width; n != 0; --n) {
    dst[x >> 3] |= 0x80 >> (x & 7);
    ++x;
  }
}

// Both blocks. Called from sprites_init(), which is every entry to the
// simulation and every return from the map - more often than the once this
// needs, and cheap enough at a thousand-odd loop iterations on a screen
// transition that pinning it to boot would buy nothing.
//
// **Two passes, and the second one is not a tidiness choice.** The obvious
// shape is one pass with the tip's width per row read as
//
//     row < kSpriteFinTipFirstRow
//         ? 0
//         : kSpriteFinTipWidths[row - kSpriteFinTipFirstRow]
//
// and oscar64 v1.32.272-117 compiles that to `LDA kSpriteFinTipWidths-13,x`
// with the guard discarded, so every row above the taper reads the thirteen
// bytes in front of the array and draws whatever width it finds there. It is
// the third face of the biased-index-into-a-const-array bug in bugs/ - the two
// written up there are fixed and neither is this - and it is silent: the rows
// above the taper came out as full-width bars, which on screen is a grey slab
// across the top of the fin. Clearing every row first and then painting the
// taper over the rows that have one indexes both arrays from zero, so there is
// no bias for the compiler to fold. See bugs/const-array-ternary-guard/.
static void _sprites_draw_fin(void) {
  uint8_t *tip = kFinSpriteData;
  uint8_t *shaft = kFinSpriteData + 64;
  for (uint8_t row = 0; row < kSpriteHeightPixels; ++row) {
    _sprites_draw_fin_row(tip, 0);
    _sprites_draw_fin_row(shaft, kSpriteFinBodyWidth);
    tip += 3;
    shaft += 3;
  }
  uint8_t *ink = kFinSpriteData + kSpriteFinTipFirstRow * 3;
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
  uint8_t back = _sprites_frame_shown ^ 1;
  volatile sprite_frame_t *f = back ? &_sprites_frame_b : &_sprites_frame_a;

  // Above the fin when it is up, so that it keeps the lowest indices and with
  // them the priority - see the tail fin section. The shift is by a constant
  // either way, which is what the two assignments are for.
  uint8_t idx = 0;
  uint8_t bit = 1;
  if (_sprites_fin_on) {
    idx = kSpriteFinCount;
    bit = 1 << kSpriteFinCount;
  }
  uint8_t msbx = 0;
  uint8_t expand = 0;
  uint8_t enable = 0;

  for (uint8_t i = 0; i < _sprites_cand_count; ++i) {
    const sprite_cand_t *c = &_sprites_cand[i];
    uint8_t slots = (c->bitmap2 == kSpriteNoBitmap) ? 1 : 2;
    // kSpriteTerrainSlots, not kSpriteStackSize: index 7 belongs to the
    // vertical-speed needle (sprites.h).
    if (idx + slots > kSpriteTerrainSlots) {
      // The list is sorted, so everything after this is farther still.
      break;
    }
    uint8_t ptr = c->bitmap;
    uint8_t sy = c->y;
    for (uint8_t s = 0; s < slots; ++s) {
      f->pos[idx << 1] = (uint8_t)c->x;
      f->pos[(idx << 1) + 1] = sy;
      f->ptr[idx] = ptr;
      f->color[idx] = c->color;
      enable |= bit;
      if (c->x & 0x100) {
        msbx |= bit;
      }
      if (c->flags & kSpriteFlagExpandX) {
        expand |= bit;
      }
      ptr = c->bitmap2;
      sy += kSpriteHeightPixels;
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
  // Sprite 7, after the fill above rather than inside the loop: it is the one
  // index the stack never hands out, so the orientation indicator takes it
  // whether the stack overflowed or was empty. A frame that set no marker
  // leaves it as the fill wrote it - parked, disabled, and the needle's colour
  // untouched.
  if (_sprites_orient_on) {
    f->pos[kSpriteIdxOrient << 1] = kSpriteOrientVX;
    f->pos[(kSpriteIdxOrient << 1) + 1] = kSpriteOrientVY;
    f->ptr[kSpriteIdxOrient] = kSpriteDefOrient.bitmap_idx;
    f->color[kSpriteIdxOrient] = kColorOrientation;
    enable |= kSpriteOrientBit;
  }

  // The fin, into the three indices the loop above skipped rather than into
  // ones the fill has just parked - so it is written last for the same reason
  // the mark is, and reaches the same frame whether the stack overflowed or was
  // empty. One pointer variable rather than a table: the top sprite is the tip
  // and everything below it is the shaft, which is the whole of the art.
  uint8_t expand_y = 0;
  if (_sprites_fin_on) {
    uint8_t sy = kSpriteFinTopVY;
    uint8_t ptr = kFinSpriteBlock;
    for (uint8_t i = 0; i < kSpriteFinCount; ++i) {
      f->pos[i << 1] = kSpriteFinVX;
      f->pos[(i << 1) + 1] = sy;
      f->ptr[i] = ptr;
      f->color[i] = kColorAircraft;
      sy += kSpriteFinPitch;
      ptr = kFinSpriteBlock + 1;
    }
    enable |= kSpriteFinMask;
    expand_y = kSpriteFinMask;
  }

  f->msbx = msbx;
  f->expand = expand;
  f->expand_y = expand_y;
  f->enable = enable;

  _sprites_frame_shown = back;
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

inline void sprites_show_terrain_sprites() {
  if (_sprites_frame_shown) {
    _sprites_program_frame(_sprites_frame_b);
  } else {
    _sprites_program_frame(_sprites_frame_a);
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
  // - Sprite 7's colour. It is the vertical speed needle, the one instrument
  //   drawn in this band, and the terrain handler now writes all eight colours.
  // - $D017, for the same reason as $D01D: the back view's tail fin leaves the
  //   three lowest sprites Y-expanded, and three of the instruments are those
  //   three. Nothing shows it in the back view itself, which draws no
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
  for (uint8_t i = 0; i < 7; i++) {
    vic.spr_pos[i].x = 0;
  }
  vic.spr_msbx = 0;
  if (view_state == VIEW_CENTER) {
    vic.spr_pos[kSpriteIdxVSpeed].x =
        _sprites_instrument_xy[kSpriteIdxVSpeed].x;
    vic.spr_pos[kSpriteIdxVSpeed].y =
        _sprites_instrument_xy[kSpriteIdxVSpeed].y;
  } else {
    vic.spr_pos[7].x = 0;
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
#ifdef __OSCAR64__
#pragma unroll(full)
#endif
  for (uint8_t i = 0; i < 8; i++) {
    vic.spr_color[i] = kColorInstrument;
  }
  if (view_state == VIEW_CENTER) {
#ifdef __OSCAR64__
#pragma unroll(full)
#endif
    for (uint8_t i = 0; i < 7; i++) {
      vic.spr_pos[i].x = _sprites_instrument_xy[i].x;
      vic.spr_pos[i].y = _sprites_instrument_xy[i].y;
      *(kScreenRamMain + 1016 + i) = _sprites_instrument_idx[i];
      *(kScreenRamAlt + 1016 + i) = _sprites_instrument_idx[i];
    }
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
