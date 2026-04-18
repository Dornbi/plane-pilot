#include "sprites.h"

#include "color.h"
#include "mem.h"
#include "print.h"
#include "roll.h"
#include "spritedef.h"
#include "vic.h"
#include <string.h>

const char kSpriteDataCompressed[] = {
#embed 2112 lzo "spritedef.bin"
};

static const uint8_t kSpriteOffsetX = 24;
static const uint8_t kSpriteOffsetY = 50;

static const uint8_t kSpeedPivotX = 104;
static const uint8_t kSpeedPivotY = 136;
static const uint8_t kAltPivotX = 104;
static const uint8_t kAltPivotY = 178;
static const uint8_t kVSpeedPivotX = 216;
static const uint8_t kVSpeedPivotY = 136;
static const uint8_t kRollPivotX = 160;
static const uint8_t kRollPivotY = 136;
static const uint8_t kThrottlePivotX = 254;
static const uint8_t kThrottlePivotY = 166;
static const uint8_t kFuelPivotX = 64;
static const uint8_t kFuelPivotY = 166;

inline void sprites_init(void) {
  vic.spr_color[0] = kColorInstrument;
  vic.spr_color[1] = kColorInstrument;
  vic.spr_color[2] = kColorInstrument;
  vic.spr_color[3] = kColorInstrument;
  vic.spr_color[4] = kColorInstrument;
  vic.spr_color[5] = kColorInstrument;
  vic.spr_color[6] = kColorInstrument;
  vic.spr_color[7] = kColorSun;
  vic.spr_msbx = 0x20; // Throttle is at 256 + 24

  *(kScreenRamMain + 1016 + 7) = kSpriteDefSun.bitmap_idx;
  *(kScreenRamAlt + 1016 + 7) = kSpriteDefSun.bitmap_idx;

  sprites_show_all();
}

inline void sprites_show_sun_only() { vic.spr_enable = 0x80; }

inline void sprites_show_all() { vic.spr_enable = 0xFF; }

static void _set_sprite(uint8_t idx, const sprite_meta_t *meta_array,
                        uint8_t dir, uint8_t pivot_x, uint8_t pivot_y) {
  const sprite_meta_t *meta = &meta_array[dir];
  vic.spr_pos[idx].x = pivot_x - meta->pivot_x;
  vic.spr_pos[idx].y = pivot_y - meta->pivot_y;
  *(kScreenRamMain + 1016 + idx) = meta->bitmap_idx;
  *(kScreenRamAlt + 1016 + idx) = meta->bitmap_idx;
}

static void _set_sprite_centered(uint8_t idx, const sprite_meta_t *meta_array,
                                 uint8_t dir, uint8_t pivot_x,
                                 uint8_t pivot_y) {
  const sprite_meta_t *meta = &meta_array[dir];
  vic.spr_pos[idx].x = pivot_x - 12;
  vic.spr_pos[idx].y = pivot_y - 10;
  *(kScreenRamMain + 1016 + idx) = meta->bitmap_idx;
  *(kScreenRamAlt + 1016 + idx) = meta->bitmap_idx;
}

inline void sprites_set_speed(uint8_t speed) {
  _set_sprite(0, kSpriteDefMetaLongArm, (speed >> 1) & 0x1f,
              kSpriteOffsetX + kSpeedPivotX, kSpriteOffsetY + kSpeedPivotY);
}

inline void sprites_set_alt(uint16_t alt) {
  _set_sprite(1, kSpriteDefMetaLongArm, (alt >> 4) & 0x1f,
              kSpriteOffsetX + kAltPivotX, kSpriteOffsetY + kAltPivotY);
  _set_sprite(2, kSpriteDefMetaShortArm, (((alt >> 4) * 205) >> 11) & 0x1f,
              kSpriteOffsetX + kAltPivotX, kSpriteOffsetY + kAltPivotY);
}

inline void sprites_set_vspeed(int16_t vspeed) {
  if (vspeed > 0x300) {
    vspeed = 0x300;
  } else if (vspeed < -0x300) {
    vspeed = -0x300;
  }
  uint8_t dir = (0x18 + (vspeed >> 6)) & 0x1f;
  _set_sprite(3, kSpriteDefMetaLongArm, dir, kSpriteOffsetX + kVSpeedPivotX,
              kSpriteOffsetY + kVSpeedPivotY);
}

static const uint8_t _roll_to_dir[kRollMax] = {
    8,  7,  7,  6,  6,  5,  5,  4,  4,  3,  3,  2,  2,  1,  1,
    0,  31, 31, 30, 30, 29, 29, 28, 28, 27, 27, 26, 26, 25, 25,
    24, 23, 23, 22, 22, 21, 21, 20, 20, 19, 19, 18, 18, 17, 17,
    16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9,  9};

inline void sprites_set_roll(uint8_t roll_angle) {
  const uint8_t idx = 4;
  const sprite_meta_t *meta = &kSpriteDefMetaLongArm[_roll_to_dir[roll_angle]];
  vic.spr_pos[idx].x = kSpriteOffsetX + kRollPivotX - 12;
  vic.spr_pos[idx].y = kSpriteOffsetY + kRollPivotY - 10;
  *(kScreenRamMain + 1016 + idx) = meta->bitmap_idx;
  *(kScreenRamAlt + 1016 + idx) = meta->bitmap_idx;
}

inline void sprites_set_throttle(uint8_t throttle) {
  _set_sprite(5, kSpriteDefMetaShortArm, (0x14 + throttle) & 0x1f,
              (kSpriteOffsetX + kThrottlePivotX) & 0xff,
              kSpriteOffsetY + kThrottlePivotY);
}

inline void sprites_set_fuel(uint32_t fuel) {
  _set_sprite(6, kSpriteDefMetaShortArm, (0x18 + (uint8_t)(fuel >> 13)) & 0x1f,
              (kSpriteOffsetX + kFuelPivotX) & 0xff,
              kSpriteOffsetY + kFuelPivotY);
}

static const char *const kHeadingBitmaps[] = {
    (const char *const)0xF120,
    (const char *const)0xF0C0,
    (const char *const)0xF060,
    (const char *const)0xF000,
};

static const char *kHeadingDest = (const char *)0xFC08;

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

static uint8_t _sun_x = 0;
static uint8_t _sun_y = 0;
static bool _sun_msbx = false;

inline void sprites_set_sun(int16_t x, int16_t y) {
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

inline void sprites_update_sun_hw() {
  vic.spr_pos[7].x = _sun_x;
  vic.spr_pos[7].y = _sun_y;
  if (_sun_msbx) {
    vic.spr_msbx |= 0x80;
  } else {
    vic.spr_msbx &= ~0x80;
  }
}

inline void sprites_hide_sun_hw() {
  vic.spr_pos[7].x = 0;
  vic.spr_msbx &= 0x7F;
}