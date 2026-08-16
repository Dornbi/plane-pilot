#include "sprites.h"

#include "color.h"
#include "mem.h"
#include "msg.h"
#include "roll.h"
#include "spritedef.h"
#include "vec.h"
#include "vic.h"
#include "view.h"

#pragma data(data_compr)
char kSpriteDataCompressed[] = {
#embed 3072 lzo "spritedef.bin"
};
#pragma data(data)

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
  uint8_t enable;
};

// $D000, as a literal rather than through the vic struct, so `[i]` is plainly
// absolute-indexed.
#define kVicSpritePos ((volatile uint8_t *)0xD000)

static sprite_cand_t _sprites_cand[kSpriteStackSize];
static uint8_t _sprites_cand_count;

// Double buffered, and that is not optional. _gfx_switch_to_terrain() reads the
// frame from an interrupt at raster 250 while sprites_stack_commit() writes it
// from the main line at an unrelated point. A torn read of a single object -
// which is all the old sun path could produce - is one object a frame stale;
// a torn read across eight is one object's X against another's Y, which is a
// sprite in the wrong place. Commit fills the back frame and then stores the
// index, and a single byte store is atomic on a 6502, so no sei is needed.
// Two named objects rather than an array of two, because the raster handler
// must reach them by a *constant* address - see the macro below.
static sprite_frame_t _sprites_frame_a;
static sprite_frame_t _sprites_frame_b;
static volatile uint8_t _sprites_frame_shown;

#pragma bss(bss2)

static volatile sprite_xy_t _sprites_instrument_xy[8];
static volatile uint8_t _sprites_instrument_idx[8];

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

void sprites_stack_reset(void) { _sprites_cand_count = 0; }

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
  // Unlike the old sun path this does not cull an object that merely reaches
  // past the bottom of the viewport: the panel handler parks sprites at x = 0
  // at raster 161 and the VIC compares X per line, so an object straddling the
  // edge is clipped there rather than hidden whole.
  if (y >= (int16_t)kViewportEndYPixels || y + (int16_t)height <= 0) {
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

void sprites_stack_commit(void) {
  // Main line, so a pointer is fine here - the restriction above is on the
  // interrupt side only.
  uint8_t back = _sprites_frame_shown ^ 1;
  sprite_frame_t *f = back ? &_sprites_frame_b : &_sprites_frame_a;

  uint8_t idx = 0;
  uint8_t bit = 1;
  uint8_t msbx = 0;
  uint8_t expand = 0;
  uint8_t enable = 0;

  for (uint8_t i = 0; i < _sprites_cand_count; ++i) {
    const sprite_cand_t *c = &_sprites_cand[i];
    uint8_t slots = (c->bitmap2 == kSpriteNoBitmap) ? 1 : 2;
    if (idx + slots > kSpriteStackSize) {
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
  f->msbx = msbx;
  f->expand = expand;
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
  vic.spr_enable = 0xFF;
  vic.spr_expand_x = 0;
  vic.spr_color[kSpriteIdxVSpeed] = kColorInstrument;
#pragma unroll(full)
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
#pragma unroll(full)
  for (uint8_t i = 0; i < 8; i++) {
    vic.spr_color[i] = kColorInstrument;
  }
  if (view_state == VIEW_CENTER) {
#pragma unroll(full)
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
  } else {
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
}
