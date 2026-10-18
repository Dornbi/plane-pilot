#include "traffic.h"

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

// docs/planes.md §13. Two aircraft slots; the Scramble pass flies the first.

static const uint8_t kTrafficMax = 2;

// --- Sprite blocks ----------------------------------------------------------
//
// $D000-$D33F, RAM under I/O that the VIC in bank 3 reads, time-shared with
// the map view's screen RAM and the title aircraft (mem.h, docs/planes.md §5):
// the dot's block, then each aircraft's two sets, front and back. Slot 0 has
// sets of four blocks so that it can be allowed 2 x 2 one day; for now both
// are held to two sprites by the size cap (kView below).
static const uint8_t kTrafficBlockDot = (0xD000 - 0xC000) / 64;
static_assert(kTrafficBlockDot == kTitleSpriteBlock,
              "the dot is the first block at $D000, written as such below");
static const uint8_t kTrafficBlockFirst[kTrafficMax] = {kTrafficBlockDot + 5,
                                                        kTrafficBlockDot + 1};
static const uint8_t kTrafficSetBlocks[kTrafficMax] = {4, 2};
static_assert(kTrafficBlockDot + 5 + 2 * 4 <= (0xD400 - 0xC000) / 64,
              "traffic blocks run past $D3FF into the sprite bitmaps");

static inline uint8_t *_traffic_block(uint8_t block) {
  return (uint8_t *)(0xC000 + ((uint16_t)block << 6));
}

// The viewport's centre, the last line a sprite may start on, plain and
// Y-expanded (mem.h), and the two-sprite size cap.
static const planes_view_t kView = {
    kScreenWidthPixels / 2, kViewportEndYPixels / 2,
    kSpriteVisibleEndYPixels - 1,
    kViewportEndYPixels - kSpritesOffLeadExpandY - 1, kPlaneKMaxTwoSprites};
static_assert(kSpriteVisibleEndYPixels - 1 == 89 &&
                  kViewportEndYPixels - kSpritesOffLeadExpandY - 1 == 68,
              "the DMA cuts moved: check planes.h's description of them");

// --- The Scramble pass ------------------------------------------------------
//
// Straight behind the player parked on runway 1, low and level and heading
// north, so it overtakes down the runway and flies on north in a straight
// line, slowly enough to catch after take-off (docs/planes.md §13, "The first
// enemy").
//
// Airspeed in the model's units: the player's half-throttle level trim, the
// slowest it can fly level, so that a player who has caught it can sit behind
// it. Below the 1,024 stall it would not be an aeroplane; above the player's
// 2,054 cruise it could not be caught.
static const int16_t kTrafficSpeed = 1500;
// How far behind the player it starts, and how high: 24.8 metres.
static const int32_t kTrafficBehind = 400L << 8;
static const int32_t kTrafficPassZ = 56L << 8;

// --- State ------------------------------------------------------------------

#pragma bss(bss2)
static bool _traffic_on[kTrafficMax];
// Position in flight_eye_*'s 24.8 metres. No attitude yet: everything flies
// level and north (traffic_add_sprites()).
static int32_t _traffic_x[kTrafficMax];
static int32_t _traffic_y[kTrafficMax];
static int32_t _traffic_z[kTrafficMax];
static int16_t _traffic_speed[kTrafficMax];
static planes_state_t _traffic_planes[kTrafficMax];
// Bit i: which of slot i's two block sets is on screen.
static uint8_t _traffic_front;
// The aircraft being drawn, in camera space and quarter metres.
static vec3_t _traffic_c;
#pragma bss(bss)

void traffic_invalidate(void) {
  for (uint8_t i = 0; i < kTrafficMax; ++i) {
    planes_state_init(&_traffic_planes[i]);
  }
  _traffic_front = 0;
  mmap_set(MMAP_RAM);
  planes_dot_bitmap((uint8_t *)0xD000);
  mmap_set(MMAP_NO_ROM);
}

void traffic_reset(uint8_t mission) {
  for (uint8_t i = 0; i < kTrafficMax; ++i) {
    _traffic_on[i] = false;
  }
  _traffic_on[0] = true;
  _traffic_x[0] = ((int32_t)kMissionStartX[mission] << 16) - kTrafficBehind;
  _traffic_y[0] = ((int32_t)kMissionStartY[mission] << 16) + 0x8000;
  _traffic_z[0] = kTrafficPassZ;
  _traffic_speed[0] = kTrafficSpeed;
  traffic_invalidate();
}

void traffic_step(void) {
  if (flight_paused || flight_crashed()) {
    return;
  }
  for (uint8_t i = 0; i < kTrafficMax; ++i) {
    if (!_traffic_on[i]) {
      continue;
    }
    // What flight.cc's _flight_move_forward() adds for a level aircraft
    // heading north: front (256, 0, 0) leaves the product as it is, so it is
    // the airspeed doubled and shifted by the step size.
    int16_t step = _traffic_speed[i] << 1;
    for (uint8_t n = flight_step_shift; n != 0; --n) {
      step >>= 1;
    }
    _traffic_x[i] += step;
  }
}

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

// d, a difference of two 24.8 positions, wrapped to the world - mask 0x7F for
// x, which repeats every 128 units, 0xFF for y and z - in quarter metres
// (docs/planes.md §2). False if it is 16 units, 4 km, or more: the cull.
static bool _traffic_quarter(int32_t d, uint8_t mask, int16_t *q) {
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

// Whether the aircraft at _traffic_c is wholly outside the viewport.
//
// Every part of it is within r = kPlaneMaxRadius / 2 quarter metres of c, so
// no part is inside an edge at tangent t if |y| - r > t * (x + r), that is
// |y| - r * (1 + t) > t * x. The edges are at t = 160 / 256 = 1/2 + 1/8
// across and 56 / 256 = 1/4 - 1/32 up and down (vec_project()), and each
// gets another 4 pixels, x / 64, for expanded pixels' rounding.
//
// Unsigned: x is positive here, and oscar64's signed shifts cost twice the
// bytes. On _traffic_c rather than three parameters, for the same reason.
static const uint16_t kTrafficReachX = (kPlaneMaxRadius / 2) * 13 / 8 + 2;
static const uint16_t kTrafficReachZ = (kPlaneMaxRadius / 2) * 39 / 32 + 2;

static __noinline bool _traffic_off_screen(void) {
  uint16_t half = (uint16_t)_traffic_c.x >> 1;
  uint16_t eighth = half >> 2;
  uint16_t px4 = eighth >> 3;
  int16_t y = _traffic_c.y, z = _traffic_c.z;
  uint16_t ay = (uint16_t)(y < 0 ? -y : y);
  uint16_t az = (uint16_t)(z < 0 ? -z : z);
  return ay > half + eighth + px4 + kTrafficReachX ||
         az > (half >> 1) - (eighth >> 2) + px4 + kTrafficReachZ;
}

void traffic_add_sprites(void) {
  for (uint8_t i = 0; i < kTrafficMax; ++i) {
    if (!_traffic_on[i]) {
      continue;
    }
    vec3_t rel;
    if (!_traffic_quarter(_traffic_x[i] - flight_eye_x, 0x7F, &rel.x) ||
        !_traffic_quarter(_traffic_y[i] - flight_eye_y, 0xFF, &rel.y) ||
        !_traffic_quarter(_traffic_z[i] - flight_eye_z, 0xFF, &rel.z)) {
      continue;
    }
    vec_transform_inv(&world_cam, &rel, &_traffic_c);
    // planes_render() says the same, but only after the axes below.
    if (_traffic_c.x <= 64) {
      continue;
    }
    // Wholly outside the viewport. planes_render() only gives up 45 degrees
    // off the view axis, so an aircraft just over the top on the pass, or
    // just off to one side, would be projected, and drawn or looked up in
    // the cache, for nothing: 7,000-38,000 cycles a frame.
    if (_traffic_off_screen()) {
      continue;
    }
    // The body axes in camera space. Everything flies level and north so
    // far, so the body frame is the world's and its axes in camera space
    // are world_cam's columns - and of those planes_render() reads only the
    // screen components, y and z. Three vec_transform_inv() calls did the
    // same for ~3,500 cycles a frame. Traffic that turns needs them back,
    // with an attitude per aircraft.
    mat3_t axes;
    axes.front.y = world_cam.left.x;
    axes.front.z = world_cam.up.x;
    axes.left.y = world_cam.left.y;
    axes.left.z = world_cam.up.y;
    axes.up.y = world_cam.left.z;
    axes.up.z = world_cam.up.z;

    uint8_t bit = (uint8_t)(1 << i);
    uint8_t first = kTrafficBlockFirst[i];
    uint8_t set = kTrafficSetBlocks[i];
    uint8_t front = (_traffic_front & bit) ? (uint8_t)(first + set) : first;
    uint8_t back = (_traffic_front & bit) ? first : (uint8_t)(first + set);

    planes_frame_t f;
    // The fill writes under I/O. The raster handlers bank I/O in for
    // themselves (gfx.cc _gfx_isr), and nothing in planes_render() touches it.
    mmap_set(MMAP_RAM);
    planes_render(&_traffic_planes[i], &kView, &_traffic_c, &axes,
                  _traffic_block(back), &f);
    mmap_set(MMAP_NO_ROM);
    if (f.hidden != kPlaneShown) {
      continue;
    }

    uint8_t bitmap = kTrafficBlockDot;
    uint8_t bitmap2 = kSpriteNoBitmap;
    uint8_t flags = 0;
    if (f.level != kPlaneLevelDot) {
      // A new bitmap is in the back set: it becomes the front one. It goes on
      // screen with the characters of this frame, at mem_switch_buffer(), and
      // by the time the next frame draws into the other set the raster
      // handler has stopped showing it - mem_switch_buffer() runs past raster
      // 250 in its colour copy before it returns.
      if (!f.cached) {
        _traffic_front ^= bit;
        front = back;
      }
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
    sprites_stack_add(_traffic_c.x >> 3, f.x, f.y, 0, 0, bitmap, bitmap2, kColorTraffic,
                      flags);
  }
}

#pragma optimize(pop)
