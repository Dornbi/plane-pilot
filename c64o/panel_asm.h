#ifndef PANEL_ASM_H
#define PANEL_ASM_H

#include <stdint.h>

#include "bool.h"
#include "spritedef.h"
#include "sprites_asm.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "color.h"
#include "flight.h"
#include "fmath.h"
#include "gfx.h"
#include "gun.h"
#include "mem.h"
#include "roll.h"
#include "view.h"
#else
static const uint8_t kRollMax = 60;
static const uint8_t kHeadingMax = 48;
static const uint8_t kColorBlack = 0;
static const uint8_t kColorLightRed = 10;
static const uint8_t kScreenWidth = 40;
static const uint8_t kCopyWidthChars = 8;
static const uint8_t kFillWidthChars = kScreenWidth - kCopyWidthChars;
enum view_state_t {
  VIEW_UNKNOWN = 0,
  VIEW_LEFT = 1,
  VIEW_CENTER = 2,
  VIEW_RIGHT = 3,
  VIEW_BACK = 4,
};
static const uint8_t kGunAmmoDigits = 3;
extern mat3_t flight_cam;
extern int16_t flight_speed;
extern int16_t flight_vspeed;
extern uint8_t flight_throttle;
extern uint32_t flight_fuel;
extern uint8_t flight_flap;
extern uint8_t flight_gear;
extern uint8_t flight_stall;
extern int32_t flight_eye_z;
extern uint8_t flight_true_heading;
extern uint8_t flight_nav_heading;
extern uint8_t roll_angle;
extern view_state_t view_state;
extern uint8_t _gfx_heading_bitmap_cache;
extern uint8_t gun_ammo[kGunAmmoDigits];
extern bool gun_ammo_shown;
extern const char kGfxAmmoDigits[];
void panel_asm_reset_cache(void);
#endif

// The assembly half of panel.cc, in panel_asm.cc: per-frame instrument panel
// updates (all six dial arms with per-arm/altitude change caching, closed-form
// roll dial, pitch bar, heading bitmap strip, five color-RAM indicator lamps,
// and fpilot's ammo counter). panel.cc keeps the C counterpart under
// #ifndef __OSCAR64__ for host builds, and test/target_test.cc (in
// target_planes_test.prg) holds the 6502 assembly to a pure C reference on the
// emulated 6510.

#ifdef __OSCAR64__
void panel_update_instruments(void);

#pragma compile("panel_asm.cc")
#endif

#endif
