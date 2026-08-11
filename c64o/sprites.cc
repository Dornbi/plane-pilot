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
#embed 2112 lzo "spritedef.bin"
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

static const uint8_t kSpriteIdxSun = 4;

inline void sprites_init(void) {
  vic.spr_color[kSpriteIdxSpeed] = kColorInstrument;
  vic.spr_color[kSpriteIdxRoll] = kColorInstrument;
  vic.spr_color[kSpriteIdxPitch] = kColorInstrument;
  vic.spr_color[kSpriteIdxVSpeed] = kColorInstrument;
  vic.spr_color[kSpriteIdxAlt1] = kColorInstrument;
  vic.spr_color[kSpriteIdxAlt2] = kColorInstrument;
  vic.spr_color[kSpriteIdxFuel] = kColorInstrument;
  vic.spr_color[kSpriteIdxThrottle] = kColorInstrument;
  vic.spr_enable = 0xFF;
}

#pragma bss(bss2)

struct sprite_xy_t {
  uint8_t x;
  uint8_t y;
};

static sprite_xy_t _sprites_terrain_xy[8];
static uint8_t _sprites_terrain_idx[8];

static volatile sprite_xy_t _sprites_instrument_xy[8];
static volatile uint8_t _sprites_instrument_idx[8];

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

static uint8_t _sun_x = 0;
static uint8_t _sun_y = 0;
static bool _sun_msbx = false;

// A terrain sprite that would land on the message text hides for as long as
// the message is up, rather than drawing over it. The test is a box overlap
// against the message span, so a message narrow enough — or a sprite far
// enough to the side — leaves the sprite alone. Coordinates are VIC sprite
// coordinates, i.e. already shifted by kSpriteOffsetX / kSpriteOffsetY.
static const int16_t kMsgBoxTop = kSpriteOffsetY;
static const int16_t kMsgBoxBottom = kSpriteOffsetY + kMsgHeightPixels;

static bool _hidden_by_msg(int16_t x, int16_t y, uint8_t width,
                           uint8_t height) {
  if (!msg_active()) {
    return false;
  }
  if (y >= kMsgBoxBottom || y + (int16_t)height <= kMsgBoxTop) {
    return false;
  }
  int16_t x0 = kSpriteOffsetX + (int16_t)msg_span_x0;
  int16_t x1 = kSpriteOffsetX + (int16_t)msg_span_x1;
  return x < x1 && x + (int16_t)width > x0;
}

inline void sprites_set_sun_position(int16_t x, int16_t y) {
  if (x < -12 || x > (int16_t)kScreenWidthPixels + 12 || y < -10 ||
      y > (int16_t)kScreenHeightPixels + 11) {
    x = 0;
    y = 0;
  } else {
    x += kSpriteOffsetX - kSpriteDefSun.pivot_x;
    y += kSpriteOffsetY - kSpriteDefSun.pivot_y;
    if (y >= kRasterScreenYStart + kViewportEndYPixels ||
        _hidden_by_msg(x, y, kSpriteWidthPixels, kSpriteHeightPixels)) {
      x = 0;
      y = 0;
    }
  }
  _sun_x = (uint8_t)x;
  _sun_y = (uint8_t)y;
  _sun_msbx = (x & 0x100);
}

inline void sprites_show_terrain_sprites() {
  *(kScreenRamMain + 1016 + kSpriteIdxSun) = kSpriteDefSun.bitmap_idx;
  *(kScreenRamAlt + 1016 + kSpriteIdxSun) = kSpriteDefSun.bitmap_idx;
  vic.spr_pos[kSpriteIdxSun].x = _sun_x;
  vic.spr_pos[kSpriteIdxSun].y = _sun_y;
  if (_sun_msbx) {
    vic.spr_msbx |= (1 << kSpriteIdxSun);
  } else {
    vic.spr_msbx &= ~(1 << kSpriteIdxSun);
  }
  vic.spr_color[kSpriteIdxSun] = kColorSun;
}

inline void sprites_show_no_sprites() {
#pragma unroll(full)
  for (uint8_t i = 0; i < 8; i++) {
    vic.spr_pos[i].x = 0;
  }
  vic.spr_msbx = 0;
}

inline void sprites_show_panel_top_sprites() {
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
#pragma unroll(full)
  if (view_state == VIEW_CENTER) {
    for (uint8_t i = 0; i < 7; i++) {
      vic.spr_pos[i].x = _sprites_instrument_xy[i].x;
      vic.spr_pos[i].y = _sprites_instrument_xy[i].y;
      *(kScreenRamMain + 1016 + i) = _sprites_instrument_idx[i];
      *(kScreenRamAlt + 1016 + i) = _sprites_instrument_idx[i];
    }
    vic.spr_color[kSpriteIdxSun] = kColorInstrument;
    vic.spr_msbx = (1 << kSpriteIdxThrottle);
  } else if (view_state == VIEW_LEFT) {
    vic.spr_pos[kSpriteIdxFuel].x = _sprites_instrument_xy[kSpriteIdxFuel].x;
    vic.spr_pos[kSpriteIdxFuel].y = _sprites_instrument_xy[kSpriteIdxFuel].y;
    *(kScreenRamMain + 1016 + kSpriteIdxFuel) =
        _sprites_instrument_idx[kSpriteIdxFuel];
    *(kScreenRamAlt + 1016 + kSpriteIdxFuel) =
        _sprites_instrument_idx[kSpriteIdxFuel];
    vic.spr_msbx = (1 << kSpriteIdxFuel);
    if (kSpriteIdxFuel == kSpriteIdxSun) {
      vic.spr_color[kSpriteIdxFuel] = kColorInstrument;
    }
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
    if (kSpriteIdxThrottle == kSpriteIdxSun) {
      vic.spr_color[kSpriteIdxThrottle] = kColorInstrument;
    }
  }
}
