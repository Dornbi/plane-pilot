#include "sprites.h"

#include "color.h"
#include "mem.h"
#include "roll.h"
#include "spritedef.h"
#include "vic.h"
#include <string.h>

const char kSpriteDataCompressed[] = {
#embed 2112 lzo "spritedef.bin"
};

static const uint8_t kSpriteOffsetX = 24;
static const uint8_t kSpriteOffsetY = 50;

static const uint8_t kSpeedPivotX = 112;
static const uint8_t kSpeedPivotY = 176;
static const uint8_t kAltPivotX = 208;
static const uint8_t kAltPivotY = 176;
static const uint8_t kVSpeedPivotX = 224;
static const uint8_t kVSpeedPivotY = 138;
static const uint8_t kRollPivotX = 160;
static const uint8_t kRollPivotY = 168;
static const uint8_t kThrottlePivotX = 8; // 264 & 0xff
static const uint8_t kThrottlePivotY = 166;
static const uint8_t kFuelPivotX = 56;
static const uint8_t kFuelPivotY = 166;

static const uint8_t kIdxFuel = 0;
static const uint8_t kIdxSpeed = 1;
static const uint8_t kIdxRoll = 2;
static const uint8_t kIdxPitch = 3;
static const uint8_t kIdxAlt1 = 4;
static const uint8_t kIdxAlt2 = 5;
static const uint8_t kIdxThrottle = 6;
static const uint8_t kIdxVSpeed = 7;

static const uint8_t kIdxSun = 4;

inline void sprites_init(void) {
  vic.spr_color[kIdxSpeed] = kColorInstrument;
  vic.spr_color[kIdxRoll] = kColorInstrument;
  vic.spr_color[kIdxPitch] = kColorInstrument;
  vic.spr_color[kIdxVSpeed] = kColorInstrument;
  vic.spr_color[kIdxAlt1] = kColorInstrument;
  vic.spr_color[kIdxAlt2] = kColorInstrument;
  vic.spr_color[kIdxFuel] = kColorInstrument;
  vic.spr_color[kIdxThrottle] = kColorInstrument;
  vic.spr_enable = 0xFF;
}

struct sprite_xy_t {
  uint8_t x;
  uint8_t y;
};

static sprite_xy_t _sprite_terrain_xy[8];
static uint8_t _sprite_terrain_idx[8];

static sprite_xy_t _sprite_instrument_xy[8];
static uint8_t _sprite_instrument_idx[8];

static void _set_instrument_sprite(uint8_t idx, const sprite_meta_t *meta_array,
                                   uint8_t dir, uint8_t pivot_x,
                                   uint8_t pivot_y) {
  const sprite_meta_t *meta = &meta_array[dir];
  _sprite_instrument_xy[idx].x = pivot_x - meta->pivot_x;
  _sprite_instrument_xy[idx].y = pivot_y - meta->pivot_y;
  _sprite_instrument_idx[idx] = meta->bitmap_idx;
}

inline void sprites_set_speed(uint8_t speed) {
  _set_instrument_sprite(kIdxSpeed, kSpriteDefMetaLongArm, (speed >> 1) & 0x1f,
                         kSpriteOffsetX + kSpeedPivotX,
                         kSpriteOffsetY + kSpeedPivotY);
}

inline void sprites_set_alt(uint16_t alt) {
  _set_instrument_sprite(kIdxAlt1, kSpriteDefMetaLongArm, (alt >> 4) & 0x1f,
                         kSpriteOffsetX + kAltPivotX,
                         kSpriteOffsetY + kAltPivotY);
  _set_instrument_sprite(
      kIdxAlt2, kSpriteDefMetaShortArm, (((alt >> 4) * 205) >> 11) & 0x1f,
      kSpriteOffsetX + kAltPivotX, kSpriteOffsetY + kAltPivotY);
}

inline void sprites_set_vspeed(int16_t vspeed) {
  if (vspeed > 0x300) {
    vspeed = 0x300;
  } else if (vspeed < -0x300) {
    vspeed = -0x300;
  }
  uint8_t dir = (0x18 + (vspeed >> 6)) & 0x1f;
  _set_instrument_sprite(kIdxVSpeed, kSpriteDefMetaLongArm, dir,
                         kSpriteOffsetX + kVSpeedPivotX,
                         kSpriteOffsetY + kVSpeedPivotY);
}

static const uint8_t _roll_to_dir[kRollMax] = {
    8,  7,  7,  6,  6,  5,  5,  4,  4,  3,  3,  2,  2,  1,  1,
    0,  31, 31, 30, 30, 29, 29, 28, 28, 27, 27, 26, 26, 25, 25,
    24, 23, 23, 22, 22, 21, 21, 20, 20, 19, 19, 18, 18, 17, 17,
    16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9,  9};

inline void sprites_set_pitch(int8_t pitch_angle) {
  const sprite_meta_t *meta = &kSpriteDefMetaLongArm[8];
  _sprite_instrument_xy[kIdxPitch].x = kSpriteOffsetX + kRollPivotX - 12;
  _sprite_instrument_xy[kIdxPitch].y =
      kSpriteOffsetY + kRollPivotY - 10 - (pitch_angle >> 2);
  _sprite_instrument_idx[kIdxPitch] = meta->bitmap_idx;
}

inline void sprites_set_roll(uint8_t roll_angle) {
  const sprite_meta_t *meta = &kSpriteDefMetaLongArm[_roll_to_dir[roll_angle]];
  _sprite_instrument_xy[kIdxRoll].x = kSpriteOffsetX + kRollPivotX - 12;
  _sprite_instrument_xy[kIdxRoll].y = kSpriteOffsetY + kRollPivotY - 10;
  _sprite_instrument_idx[kIdxRoll] = meta->bitmap_idx;
}

inline void sprites_set_throttle(uint8_t throttle) {
  _set_instrument_sprite(kIdxThrottle, kSpriteDefMetaShortArm,
                         (0x14 + throttle) & 0x1f,
                         (kSpriteOffsetX + kThrottlePivotX) & 0xff,
                         kSpriteOffsetY + kThrottlePivotY);
}

inline void sprites_set_fuel(uint32_t fuel) {
  _set_instrument_sprite(
      kIdxFuel, kSpriteDefMetaShortArm, (0x18 + (uint8_t)(fuel >> 13)) & 0x1f,
      (kSpriteOffsetX + kFuelPivotX) & 0xff, kSpriteOffsetY + kFuelPivotY);
}

static uint8_t _sun_x = 0;
static uint8_t _sun_y = 0;
static bool _sun_msbx = false;

inline void sprites_set_sun_position(int16_t x, int16_t y) {
  if (x < -12 || x > (int16_t)kScreenWidthPixels + 12 || y < -10 ||
      y > (int16_t)kScreenHeightPixels + 11) {
    x = 0;
    y = 0;
  } else {
    x += kSpriteOffsetX - kSpriteDefSun.pivot_x;
    y += kSpriteOffsetY - kSpriteDefSun.pivot_y;
  }
  _sun_x = (uint8_t)x;
  _sun_y = (uint8_t)y;
  _sun_msbx = (x & 0x100);
}

inline void sprites_show_terrain_sprites() {
  *(kScreenRamMain + 1016 + kIdxSun) = kSpriteDefSun.bitmap_idx;
  *(kScreenRamAlt + 1016 + kIdxSun) = kSpriteDefSun.bitmap_idx;
  vic.spr_pos[kIdxSun].x = _sun_x;
  vic.spr_pos[kIdxSun].y = _sun_y;
  if (_sun_msbx) {
    vic.spr_msbx |= (1 << kIdxSun);
  } else {
    vic.spr_msbx &= ~(1 << kIdxSun);
  }
  vic.spr_color[kIdxSun] = kColorSun;
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
  vic.spr_pos[kIdxVSpeed].x = _sprite_instrument_xy[kIdxVSpeed].x;
  vic.spr_pos[kIdxVSpeed].y = _sprite_instrument_xy[kIdxVSpeed].y;
  *(kScreenRamMain + 1016 + kIdxVSpeed) = _sprite_instrument_idx[kIdxVSpeed];
  *(kScreenRamAlt + 1016 + kIdxVSpeed) = _sprite_instrument_idx[kIdxVSpeed];
}

inline void sprites_show_panel_bottom_sprites() {
#pragma unroll(full)
  for (uint8_t i = 0; i < 7; i++) {
    vic.spr_pos[i].x = _sprite_instrument_xy[i].x;
    vic.spr_pos[i].y = _sprite_instrument_xy[i].y;
    *(kScreenRamMain + 1016 + i) = _sprite_instrument_idx[i];
    *(kScreenRamAlt + 1016 + i) = _sprite_instrument_idx[i];
  }
  vic.spr_color[kIdxSun] = kColorInstrument;
  vic.spr_msbx = (1 << kIdxThrottle);
}

static const char *const kHeadingBitmaps[] = {
    (const char *const)0xF120,
    (const char *const)0xF0C0,
    (const char *const)0xF060,
    (const char *const)0xF000,
};

static const char *kHeadingDest = (const char *)0xF488;

inline void sprites_set_heading_bitmap(uint8_t heading) {
  static const uint8_t kHeadingCharMax = kHeadingMax / 4;
  uint8_t heading_ch = (heading >> 2) + 3;
  if (heading_ch >= kHeadingCharMax) {
    heading_ch -= kHeadingCharMax;
  }
  const char *src_start = kHeadingBitmaps[heading & 0x03];
  char *dst = (char *)kHeadingDest;
  const char *src = src_start + (heading_ch * 8);
  for (uint8_t i = 6;;) {
    memcpy(dst, src, 8);
    ++heading_ch;
    if (heading_ch >= kHeadingCharMax) {
      heading_ch = 0;
      src = src_start;
    } else {
      src += 8;
    }
    dst += 8;
    if (--i == 0) {
      break;
    }
  }
}