#include "opponent.h"

#include <c64/memmap.h>

#include "color.h"
#include "flight.h"
#include "mem.h"
#include "mission.h"
#include "planedef.h"
#include "planes.h"
#include "sprites.h"
#include "vec.h"
#include "world.h"

// docs/planes.md §13: the Scramble pass.

// One slot: the Scramble pass flies one aircraft. A second needs §5's block
// sets by rank - eight blocks and a four-sprite cap for the nearer, four and
// a two-sprite cap for the other - since $D000-$D3FF holds 16 blocks, not the
// 17 that two four-sprite slots and the dot would take.
static const uint8_t kOpponentMax = 1;

// --- Sprite blocks ----------------------------------------------------------
//
// $D000-$D33F, RAM under I/O that the VIC in bank 3 reads, time-shared with
// the map view's screen RAM and the title aircraft (mem.h, docs/planes.md §5):
// the dot's block, then the aircraft's two sets of four, front and back: up
// to 2 x 2 sprites (kView below).
static const uint8_t kOpponentBlockDot = (0xD000 - 0xC000) / 64;
static_assert(kOpponentBlockDot == kTitleSpriteBlock,
              "the dot is the first block at $D000, written as such below");
static const uint8_t kOpponentBlockFirst[kOpponentMax] = {kOpponentBlockDot + 1};
static const uint8_t kOpponentSetBlocks[kOpponentMax] = {4};
static_assert(kOpponentBlockDot + 1 + 2 * 4 <= (0xD400 - 0xC000) / 64,
              "opponent blocks run past $D3FF into the sprite bitmaps");

static inline uint8_t *_opponent_block(uint8_t block) {
  return (uint8_t *)(0xC000 + ((uint16_t)block << 6));
}

// The viewport's centre, the last line a sprite may start on, plain and
// Y-expanded (mem.h), and the model's own size cap: the aircraft grows to
// 2 x 2 sprites and holds its size from ~54 m. One view for every slot: a
// view that varies costs ~150 bytes, since oscar64 folds a constant one into
// the renderer.
static const planes_view_t kView = {
    kScreenWidthPixels / 2, kViewportEndYPixels / 2,
    kSpriteVisibleEndYPixels - 1,
    kViewportEndYPixels - kSpritesOffLeadExpandY - 1, kPlaneKMax};
static_assert(kSpriteVisibleEndYPixels - 1 == 89 &&
                  kViewportEndYPixels - kSpritesOffLeadExpandY - 1 == 68,
              "the DMA cuts moved: check planes.h's description of them");

// --- The Scramble pass ------------------------------------------------------
//
// Behind the player parked on runway 1 and off to its left, low and level and
// heading north, so it overtakes alongside the runway and flies on north in a
// straight line, slowly enough to catch after take-off (docs/planes.md §13,
// "The first enemy").
//
// Airspeed in the model's units: the player's half-throttle level trim, the
// slowest it can fly level, so that a player who has caught it can sit behind
// it. Below the 1,024 stall it would not be an aeroplane; above the player's
// 2,054 cruise it could not be caught.
static const int16_t kOpponentSpeed = 1500;
// How far behind the player it starts, how far to the left, and how high:
// 24.8 metres. The runway is one unit, 256 m, wide and the player starts on
// its centreline, so its left edge is 128 m away; the pass is 64 m beyond
// it. +y is west, which is left facing north. The height is 80 m above the
// eye of an aircraft on the ground (flight.cc kFlightMinEyeZ, 32 m).
static const int32_t kOpponentBehind = 400L << 8;
static const int32_t kOpponentLeft = 96L << 8;
static const int32_t kOpponentPassZ = 96L << 8;

// --- State ------------------------------------------------------------------

#pragma bss(bss2)
static bool _opponent_on[kOpponentMax];
// Position in flight_eye_*'s 24.8 metres. No attitude yet: everything flies
// level and north (opponent_add_sprites()).
static int32_t _opponent_x[kOpponentMax];
static int32_t _opponent_y[kOpponentMax];
static int32_t _opponent_z[kOpponentMax];
static int16_t _opponent_speed[kOpponentMax];
static planes_state_t _opponent_planes[kOpponentMax];
// Bit i: which of slot i's two block sets is on screen.
static uint8_t _opponent_front;
// Bit i: slot i was projected anew last frame and is filled this one.
static uint8_t _opponent_pending;
// The layout of what is in slot i's front set.
static planes_layout_t _opponent_shown[kOpponentMax];
// The aircraft being drawn, in camera space and quarter metres.
static vec3_t _opponent_c;
#pragma bss(bss)

void opponent_invalidate(void) {
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    planes_state_init(&_opponent_planes[i]);
    _opponent_shown[i] = _opponent_planes[i].lay;
  }
  _opponent_front = 0;
  _opponent_pending = 0;
  mmap_set(MMAP_RAM);
  planes_dot_bitmap((uint8_t *)0xD000);
  mmap_set(MMAP_NO_ROM);
}

void opponent_reset(uint8_t mission) {
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    _opponent_on[i] = false;
  }
  _opponent_on[0] = true;
  _opponent_x[0] = ((int32_t)kMissionStartX[mission] << 16) - kOpponentBehind;
  _opponent_y[0] =
      ((int32_t)kMissionStartY[mission] << 16) + 0x8000 + kOpponentLeft;
  _opponent_z[0] = kOpponentPassZ;
  _opponent_speed[0] = kOpponentSpeed;
  opponent_invalidate();
}

void opponent_step(void) {
  if (flight_paused || flight_crashed()) {
    return;
  }
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!_opponent_on[i]) {
      continue;
    }
    // What flight.cc's _flight_move_forward() adds for a level aircraft
    // heading north: front (256, 0, 0) leaves the product as it is, so it is
    // the airspeed doubled and shifted by the step size.
    int16_t step = _opponent_speed[i] << 1;
    for (uint8_t n = flight_step_shift; n != 0; --n) {
      step >>= 1;
    }
    _opponent_x[i] += step;
  }
}

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

// d, a difference of two 24.8 positions, wrapped to the world - mask 0x7F for
// x, which repeats every 128 units, 0xFF for y and z - in quarter metres
// (docs/planes.md §2). False if it is 16 units, 4 km, or more: the cull.
static bool _opponent_quarter(int32_t d, uint8_t mask, int16_t *q) {
  uint8_t u = (uint8_t)(d >> 16) & mask;
  // Sign-extend x's seven bits. For y and z this sets bit 7 only on values
  // from 64 units up, which the cull rejects either way.
  if (u & 0x40) {
    u |= 0x80;
  }
  // A signed range test, not (uint8_t)(u + 16) >= 32: oscar64 folds that to
  // u >= 16, wraparound dropped, and culled everything behind on x.
  int8_t s = (int8_t)u;
  if (s >= 16 || s < -15) {
    return false;
  }
  *q = (int16_t)(((uint16_t)u << 10) + ((uint16_t)d >> 6));
  return true;
}

// Whether the aircraft at _opponent_c is wholly outside the viewport.
//
// Every part of it is within r = kPlaneMaxRadius / 2 quarter metres of c, so
// no part is inside an edge at tangent t if |y| - r > t * (x + r), that is
// |y| - r * (1 + t) > t * x. The edges are at t = 160 / 256 = 1/2 + 1/8
// across and 56 / 256 = 1/4 - 1/32 up and down (vec_project()), and each
// gets another 4 pixels, x / 64, for expanded pixels' rounding.
//
// Unsigned: x is positive here, and oscar64's signed shifts cost twice the
// bytes. On _opponent_c rather than three parameters, for the same reason.
static const uint16_t kOpponentReachX = (kPlaneMaxRadius / 2) * 13 / 8 + 2;
static const uint16_t kOpponentReachZ = (kPlaneMaxRadius / 2) * 39 / 32 + 2;

static __noinline bool _opponent_off_screen(void) {
  uint16_t half = (uint16_t)_opponent_c.x >> 1;
  uint16_t eighth = half >> 2;
  uint16_t px4 = eighth >> 3;
  int16_t y = _opponent_c.y, z = _opponent_c.z;
  uint16_t ay = (uint16_t)(y < 0 ? -y : y);
  uint16_t az = (uint16_t)(z < 0 ? -z : z);
  return ay > half + eighth + px4 + kOpponentReachX ||
         az > (half >> 1) - (eighth >> 2) + px4 + kOpponentReachZ;
}

void opponent_add_sprites(void) {
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!_opponent_on[i]) {
      continue;
    }
    vec3_t rel;
    if (!_opponent_quarter(_opponent_x[i] - flight_eye_x, 0x7F, &rel.x) ||
        !_opponent_quarter(_opponent_y[i] - flight_eye_y, 0xFF, &rel.y) ||
        !_opponent_quarter(_opponent_z[i] - flight_eye_z, 0xFF, &rel.z)) {
      continue;
    }
    vec_transform_inv(&world_cam, &rel, &_opponent_c);
    // planes_render() says the same, but only after the axes below.
    if (_opponent_c.x <= 64) {
      continue;
    }
    // Wholly outside the viewport. planes_render() only gives up 45 degrees
    // off the view axis, so an aircraft just over the top on the pass, or
    // just off to one side, would be projected, and drawn or looked up in
    // the cache, for nothing: 7,000-38,000 cycles a frame.
    if (_opponent_off_screen()) {
      continue;
    }
    uint8_t bit = (uint8_t)(1 << i);
    uint8_t first = kOpponentBlockFirst[i];
    uint8_t set = kOpponentSetBlocks[i];
    uint8_t front = (_opponent_front & bit) ? (uint8_t)(first + set) : first;
    uint8_t back = (_opponent_front & bit) ? first : (uint8_t)(first + set);
    planes_state_t *state = &_opponent_planes[i];
    planes_layout_t *shown = &_opponent_shown[i];

    // A redraw takes two frames, the projection in one and the fill in the
    // next (docs/planes.md §13, "The silhouette at half rate"), so neither
    // frame pays for both. The position is this frame's either way: the
    // bitmap on screen is placed at this frame's centre.
    planes_frame_t f;
    if (_opponent_pending & bit) {
      // The fill the last frame projected. It writes under I/O; the raster
      // handlers bank I/O in for themselves (gfx.cc _gfx_isr), and nothing
      // in planes_fill() touches it. Then the new bitmap is the front one:
      // it goes on screen with this frame's characters, at
      // mem_switch_buffer(), and by the time a later frame fills the other
      // set the raster handler has stopped showing it - mem_switch_buffer()
      // runs past raster 250 in its colour copy before it returns.
      bool seen = planes_centre(&kView, &_opponent_c, &f);
      mmap_set(MMAP_RAM);
      planes_fill(state, _opponent_block(back));
      mmap_set(MMAP_NO_ROM);
      _opponent_pending &= (uint8_t)~bit;
      _opponent_front ^= bit;
      front = back;
      *shown = state->lay;
      if (!seen) {
        continue;
      }
      planes_place(shown, &kView, &f);
    } else {
      // The body axes in camera space. Everything flies level and north so
      // far, so the body frame is the world's and its axes in camera space
      // are world_cam's columns - and of those planes_project() reads only
      // the screen components, y and z. Three vec_transform_inv() calls did
      // the same for ~3,500 cycles a frame. An opponent that turns needs them
      // back, with an attitude per aircraft.
      mat3_t axes;
      axes.front.y = world_cam.left.x;
      axes.front.z = world_cam.up.x;
      axes.left.y = world_cam.left.y;
      axes.left.z = world_cam.up.y;
      axes.up.y = world_cam.left.z;
      axes.up.z = world_cam.up.z;
      planes_project(state, &kView, &_opponent_c, &axes, &f);
      if (f.hidden != kPlaneShown) {
        continue;
      }
      if (f.level != kPlaneLevelDot && !f.cached) {
        // A new silhouette, filled next frame; until then the old bitmap.
        _opponent_pending |= bit;
        planes_place(shown, &kView, &f);
      }
    }
    if (f.hidden != kPlaneShown) {
      continue;
    }

    uint8_t bitmap = kOpponentBlockDot;
    uint8_t bitmap2 = kSpriteNoBitmap;
    uint8_t flags = 0;
    if (f.level != kPlaneLevelDot) {
      bitmap = front;
      if (f.rows == 2) {
        bitmap2 = (uint8_t)(front + f.cols);
      }
      if (f.cols == 2) {
        flags |= kSpriteFlagWide;
      }
      if (f.xs == 2) {
        flags |= kSpriteFlagExpandX;
      }
      if (f.ys == 2) {
        flags |= kSpriteFlagExpandY;
      }
    }
    // Depth in the clouds' 2 m units, so that the two sort together.
    sprites_stack_add(_opponent_c.x >> 3, f.x, f.y, 0, 0, bitmap, bitmap2,
                      kColorOpponent, flags);
  }
}

#pragma optimize(pop)
