#include "title.h"

#ifdef __OSCAR64__
#include <oscar.h>
#else
static const char *oscar_expand_lzo(char *dp, const char *sp) { return sp; }
#endif

#include "mem.h"
#include "titledef.h"
#include "vic.h"

// Runs on a screen transition and once per menu frame, neither of which is
// cycle critical - the same trade menu.cc, help.cc and screen.cc make.
#pragma optimize(push, outline)

#ifdef __OSCAR64__
#pragma data(data_compr)
static const char kTitleDataCompressed[] = {
#embed 256 lzo "titledef.bin"
};
#pragma data(data)
#else
static const char kTitleDataCompressed[256] = {0};
#endif

// mem.h picks the address; the generator picks the block number the sprite
// pointers are written from. They describe the same 256 bytes, and nothing but
// this line connects them.
static_assert(kTitleSpriteBlock == kTitleDefBitmapBase,
              "kTitleSpriteData is not where kTitleDefBitmapBase points");
static_assert(kTitleDefBitmapCount == 4, "the flyby programs four sprites");

// Hardware sprites 0..3, in the generator's reading order: 0 top left, 1 top
// right, 2 bottom left, 3 bottom right.
static const uint8_t kTitleSpriteMask = 0x0F;
// The two left-hand sprites and the two right-hand ones, for $D010.
static const uint8_t kTitleMaskLeft = 0x05;
static const uint8_t kTitleMaskRight = 0x0A;

// An unexpanded sprite. Multicolour halves the horizontal *resolution*, not
// the width, so the block is still 48 x 42 screen pixels.
static const uint8_t kTitleSpriteWidth = 24;
static const uint8_t kTitleSpriteHeight = 21;

// --- The trajectory ---------------------------------------------------------
//
// **Every flyby is the same aeroplane at the same angle at the same speed.**
// Two pixels left and two lines down per frame, always: the art is a fixed
// silhouette, so the direction of travel has to match the direction it is
// pointing or it reads as a skid, and a second speed would need a second
// silhouette to be honest about it. What varies between flybys is only where
// the aeroplane comes in.
static const uint8_t kTitleStepX = 2;
static const uint8_t kTitleStepY = 2;

// The display window in VIC sprite coordinates - the picture, not the border.
static const int16_t kTitleWindowLeft = 24;
static const int16_t kTitleWindowRight = 343;
static const uint8_t kTitleWindowTop = 51;
static const uint8_t kTitleWindowBottom = 250;

// The middle of the 2 x 2 block, which is what "where it enters" is measured
// from. A corner would put the entry point half an aeroplane away from where
// the eye puts it. Half of two columns is one column, and half of two rows is
// one row, which is why these read as a single sprite's dimensions.
static const uint8_t kTitleCenterX = kTitleDefCols * kTitleSpriteWidth / 2;
static const uint8_t kTitleCenterY = kTitleDefRows * kTitleSpriteHeight / 2;

// The two ends of the flyby: one line past the bottom border, and far enough
// left that even the trailing column has gone behind the left border.
//
// The lower half's Y is 21 more than the upper's and wraps past 255 on the way
// down, which is harmless twice over. It lands in 0..16, which is the top
// border; and on PAL, where the raster runs to 311 and the VIC compares only
// the low byte, it starts a second copy at raster 256..272 - which is the
// bottom border. Both are behind a border, and a border hides a sprite. The
// same aliasing covers the *upper* half at the start of a flyby, where Y is as
// low as 8.
static const uint8_t kTitleEndY = 252;
// One column, not two: the block's *right* column sits 24 pixels along, so it
// is the one that reaches zero last, and it is hidden as soon as its own
// position does.
static const int16_t kTitleEndX = -(int16_t)kTitleSpriteWidth;

// --- Lanes -------------------------------------------------------------------
//
// Since the angle never changes, all the paths an aeroplane can take are
// **parallel lines**, and a 45 degree line down and to the left is the set of
// points where X + Y is some constant. So one number picks a flyby out of that
// family, and it is called the lane here to keep it apart from the 45 degrees
// itself, which is not a choice anyone makes.
//
// Everything else about a flyby is that number read off a different side of the
// picture: where it crosses the top edge, where it crosses the right edge,
// where it leaves and how long it is on screen. Three lanes name the ones that
// matter, each written as the entry point it puts the block's centre on:
//
//   TopMid    the middle of the top edge
//   Corner    the top right corner, where the two edges meet
//   RightMid  half way down the right edge
//
// Subtracting the centre offset converts a centre position into the lane of the
// block's top left corner, which is what is actually stepped.
static const int16_t kTitleLaneTopMid = (kTitleWindowLeft + kTitleWindowRight) / 2 +
                                        (int16_t)kTitleWindowTop -
                                        (int16_t)(kTitleCenterX + kTitleCenterY);
static const int16_t kTitleLaneCorner = kTitleWindowRight +
                                        (int16_t)kTitleWindowTop -
                                        (int16_t)(kTitleCenterX + kTitleCenterY);
static const int16_t kTitleLaneRightMid =
    kTitleWindowRight +
    (int16_t)((kTitleWindowTop + kTitleWindowBottom) / 2) -
    (int16_t)(kTitleCenterX + kTitleCenterY);

// Flybys **alternate** between the two bands those three lanes bound: the first
// comes in over the right half of the top edge, the second over the top half of
// the right edge, and so on. Alternating rather than drawing the side at random
// is what makes the variety visible - the two bands are 160 lanes and 99, so a
// single sweep across both would come in over the top edge nearly twice as
// often as over the side, and a run of four the same way would not be unusual.
//
// The random byte then picks the lane within the band, and has to be scaled to
// fit it. `(r >> a) + (r >> b)` is r times (2^-a + 2^-b): 1/2 + 1/8 = 5/8 of a
// byte lands in the 160-lane top band, 1/4 + 1/8 = 3/8 in the 99-lane right
// one. Two shifts and an add. A remainder would use the range exactly and cost
// the 139-byte divmod routine that tools/check_mul_div.py fails the build over,
// to buy the last two lanes of one band and the last five of the other - which
// is to say nothing, since both bands were named by an edge's midpoint and the
// midpoint is not a hard line.
//
// A macro rather than a function because the static_asserts below need the same
// expression at compile time, and that is the only thing this file uses one for.
#define _title_spread(r, a, b) ((uint8_t)(((r) >> (a)) + ((r) >> (b))))

static const uint8_t kTitleSpreadTopMax = _title_spread(255, 1, 3);
static const uint8_t kTitleSpreadRightMax = _title_spread(255, 2, 3);

// oscar64 does not join adjacent string literals, so these are one line each.
static_assert(kTitleLaneTopMid + (int16_t)kTitleSpreadTopMax <= kTitleLaneCorner,
              "the top edge band now reaches past the corner into the right edge one");
static_assert(kTitleLaneCorner + (int16_t)kTitleSpreadRightMax <= kTitleLaneRightMid,
              "the right edge band now reaches past half way down the right edge");

// Where a flyby is spawned from, which is not where it is seen from. The
// aircraft has to start off the picture on the lane it was given, and the two
// ends of the range need different sides to start off: a low lane starts above
// the top edge, a high one out beyond the right. Taking the higher Y of the
// two - which is the same as clamping X - picks the right one without a branch
// on which case it is.
//
// The top border ends at raster 51, so a block whose lower half starts at
// Y + 21 = 29 is still completely hidden; 8 is that. The display window ends
// at X = 343 and the block is 48 wide, so at X = 352 none of it is on screen.
static const uint8_t kTitleStartY = 8;
static const int16_t kTitleStartXMax = 352;

// Frames at 50 Hz: three seconds before the first flyby, then three to five and
// a half between them. A flyby itself is between 1.6 and 2.4 seconds, so one
// comes round about every six. The jitter is what stops it beating against the
// tune.
static const uint16_t kTitleFirstGapFrames = 150;
static const uint16_t kTitleGapFrames = 150;
static const uint8_t kTitleGapJitter = 0x7F;

// Where the aircraft is, and whether it is anywhere at all. _title_x is signed
// and wider than the register because the block starts off the right-hand edge
// at 352, which needs the ninth bit in $D010, and because the arithmetic that
// puts it there works in the same width.
static bool _title_flying;
static uint16_t _title_gap;
static int16_t _title_x;
static uint8_t _title_y;

// Which band the next flyby comes from, toggled as each one starts. False is
// the top edge, so the first flyby after the menu page is painted comes in over
// the top and the second over the right - and the count restarts with the page,
// which is what makes "the first one" mean anything.
static bool _title_from_right;

// A Galois LFSR, the same shape as sound.cc's and for the same reason: eight
// bits of unpredictability for four bytes of state and no multiply. It is
// stepped every frame rather than only when a flyby starts, so the position of
// the next one depends on how long the player spent in the menu and in the
// help screen - without that, every run would fly the same path in the same
// order from a cold boot.
static const uint8_t kTitleRngTaps = 0xB4;
static uint8_t _title_rng = 0xA5;

static uint8_t _title_next_rand(void) {
  const bool lsb = _title_rng & 1;
  _title_rng >>= 1;
  if (lsb) {
    _title_rng ^= kTitleRngTaps;
  }
  return _title_rng;
}

// The eight position registers and $D010, from _title_x and _title_y.
//
// _title_x runs negative as the aircraft leaves to the left, and the register
// does not. Nor would a wider one help: hiding a 24-pixel column behind a left
// border that ends at X = 24 would want X = -24, and one step below zero wraps
// the sprite round to the right-hand side of the same raster line.
//
// **Parking a column at X = 0 is the exact answer, not an approximation.** A
// sprite at zero spans 0..23 and the picture starts at 24, so it is completely
// hidden - the property sprites.cc already leans on to park the instrument
// needles. Every negative position is equally hidden, so clamping them all to
// zero renders exactly what the arithmetic asks for, and the column that is
// still partly on screen keeps its true position and clips against the border
// a pixel at a time. The aeroplane goes off the left edge column by column
// with nothing snapping.
static void _title_program(void) {
  int16_t xl = _title_x;
  int16_t xr = _title_x + kTitleSpriteWidth;
  if (xl < 0) {
    xl = 0;
  }
  if (xr < 0) {
    xr = 0;
  }
  const uint8_t yb = _title_y + kTitleSpriteHeight;

  uint8_t msbx = 0;
  if (xl & 0x100) {
    msbx |= kTitleMaskLeft;
  }
  if (xr & 0x100) {
    msbx |= kTitleMaskRight;
  }

  vic.spr_pos[0].x = (uint8_t)xl;
  vic.spr_pos[0].y = _title_y;
  vic.spr_pos[1].x = (uint8_t)xr;
  vic.spr_pos[1].y = _title_y;
  vic.spr_pos[2].x = (uint8_t)xl;
  vic.spr_pos[2].y = yb;
  vic.spr_pos[3].x = (uint8_t)xr;
  vic.spr_pos[3].y = yb;
  vic.spr_msbx = msbx;
  // Last, so the first frame of a flyby cannot show the block at the position
  // the previous one ended on.
  vic.spr_enable = kTitleSpriteMask;
}

void title_arm(void) {
  // $CF00 is plain RAM under nothing at all, so unlike the $D400 blob this
  // needs no banking and no sei - see mem.h.
  oscar_expand_lzo((char *)kTitleSpriteData, kTitleDataCompressed);

  // The menu page is always the main buffer (screen_enter_static_mccm calls
  // mem_use_main_buffer), but going through mem_screen_ram keeps that a fact
  // about screen.cc rather than one this file has to know.
  for (uint8_t i = 0; i < kTitleDefBitmapCount; ++i) {
    mem_screen_ram[1016 + i] = kTitleSpriteBlock + i;
    vic.spr_color[i] = kTitleDefColorMain;
  }

  vic.spr_mcolor0 = kTitleDefColorMc0;
  vic.spr_mcolor1 = kTitleDefColorMc1;
  vic.spr_multi = kTitleSpriteMask;
  // In front of the text, not behind it. Everything else here is inherited
  // state that has to be cleared rather than assumed: the simulation leaves
  // $D01D holding whatever the last viewport frame expanded, and sprites_init()
  // is not reached again until a mission starts.
  vic.spr_priority = 0x00;
  vic.spr_expand_x = 0x00;
  vic.spr_expand_y = 0x00;
  vic.spr_msbx = 0x00;
  vic.spr_enable = 0x00;

  _title_flying = false;
  _title_from_right = false;
  _title_gap = kTitleFirstGapFrames;
}

void title_tick(void) {
  const uint8_t r = _title_next_rand();

  if (!_title_flying) {
    if (--_title_gap != 0) {
      return;
    }
    // One lane out of this flyby's band, and then the point on it the
    // aircraft is spawned from.
    int16_t lane;
    if (_title_from_right) {
      lane = kTitleLaneCorner + (int16_t)_title_spread(r, 2, 3);
    } else {
      lane = kTitleLaneTopMid + (int16_t)_title_spread(r, 1, 3);
    }
    _title_from_right = !_title_from_right;

    int16_t x = lane - (int16_t)kTitleStartY;
    if (x > kTitleStartXMax) {
      x = kTitleStartXMax;
    }
    _title_x = x;
    _title_y = (uint8_t)(lane - x);
    _title_flying = true;
  } else {
    _title_x -= kTitleStepX;
    _title_y += kTitleStepY;
    // Out of the bottom on the higher lanes, out of the left on the lower
    // ones. Which of the two happens first is the lane all over again, and
    // neither end needs to know which one it was.
    if (_title_y >= kTitleEndY || _title_x <= kTitleEndX) {
      _title_flying = false;
      _title_gap = kTitleGapFrames + (uint16_t)(r & kTitleGapJitter);
      vic.spr_enable = 0x00;
      return;
    }
  }

  _title_program();
}

#pragma optimize(pop)
