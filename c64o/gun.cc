#include "gun.h"

#include "color.h"
#include "flight.h"
#include "mem.h"
#include "spritedef.h"
#include "sprites.h"
#include "vec.h"
#include "world.h"

// docs/projectiles.md. The rounds are state and the sprite is a view of one
// of them: every round is flown and hit-tested each step whether or not it
// is drawn, and each frame at most one is offered to the stack.

// --- Rounds -----------------------------------------------------------------
//
// A round never gets more than a kilometre or so from the player, so it is
// kept relative to the eye, in sixteenths of a metre - int16 is good to
// 2 km - rather than in the world's 24.8 half metres. Every step the eye's
// own movement is taken off them all. That keeps everything below 16-bit:
// with world positions the module was twice the size.
//
// By axis and then by round, the axes x, y and z as everywhere, in one flat
// striped array each: round r's axis c is [c * kRoundMax + r], one index
// register and no multiply, where [3][kRoundMax] cost a third more code.

static const uint8_t kRoundMax = 8;
static const uint8_t kRoundNone = 0xFF;
// A round's owner: the player, or 1 + the opponent slot that fired it.
static const uint8_t kRoundPlayer = 0;
// Model steps a round flies before it is gone: ~600 m at 48 m a step.
static const uint8_t kRoundLife = 12;
// Half the hit box's side: 10 m against the ~20 m span the opponents are
// drawn at (opponent.cc kOpponentKn).
static const int16_t kRoundHit = 10 * 16;

// Muzzle velocity, 48 m a step: slower than a real gun, so a tracer is seen
// for a dozen frames rather than two (docs/projectiles.md §10).
static const int16_t kGunMuzzle = 48 * 16;
// Where a round starts, in the aircraft's frame: 20 m ahead of the eye -
// clear of the renderer's 8 m near cull - 4 m out on the wing that fires it
// and 1.5 m below, so it enters from the lower left or right.
static const int16_t kGunAhead = 20 * 16;
static const int16_t kGunWing = 4 * 16;
static const int16_t kGunDrop = 3 * 8;
// Inwards per step, so that the two streams cross the boresight 200 m ahead:
// 4 m in 200 is a fiftieth of the muzzle velocity.
static const int16_t kGunConverge = kGunMuzzle / 50;
// Model steps between the player's rounds while the trigger is held. Each
// is from the other wing.
static const uint8_t kGunInterval = 2;

#pragma bss(bss2)
static const uint8_t kRoundAxes = 3 * kRoundMax;
static __striped int16_t _round_p[kRoundAxes];
static __striped int16_t _round_v[kRoundAxes];
// Model steps flown, from 1; 0 is a free slot.
static uint8_t _round_age[kRoundMax];
static uint8_t _round_owner[kRoundMax];
// The eye the rounds are relative to, in flight_eye_*'s 24.8 half metres.
static int32_t _gun_eye[3];
// What a round is being tested against, relative to the eye like a round;
// and _gun_hits()' and gun_step()'s working, static rather than on oscar64's
// software stack.
static __striped int16_t _gun_t[3];
static __striped int16_t _gun_d[3];
static __striped int16_t _gun_q[3];
static __striped int16_t _gun_moved[3];
// The player's newest round, the one drawn while firing, or kRoundNone.
static uint8_t _gun_last;
// Steps until the next round may go, and which wing it goes from.
static uint8_t _gun_wait;
static uint8_t _gun_side;
static uint8_t _gun_substep;
// The trigger has been released since gun_reset().
static bool _gun_armed;
uint8_t gun_player_hits;
#pragma bss(bss)

static int16_t _gun_abs(int16_t v) { return v < 0 ? -v : v; }

void gun_reset(void) {
  for (uint8_t r = 0; r < kRoundMax; ++r) {
    _round_age[r] = 0;
  }
  _gun_eye[0] = flight_eye_x;
  _gun_eye[1] = flight_eye_y;
  _gun_eye[2] = flight_eye_z;
  _gun_last = kRoundNone;
  _gun_wait = 0;
  _gun_side = 0;
  _gun_substep = 0;
  _gun_armed = false;
  gun_player_hits = 0;
}

// Axis c of the eye's step, `eye` being flight_eye_* on that axis: what it
// moved, in sixteenths, into _gun_moved[c]. What the shift loses is kept in
// _gun_eye, so nothing drifts; the eye moves at most 15 m a step, so the
// difference fits int16 before the shift.
//
// Called once an axis with the eye spelled out, never from a loop over c
// that picks flight_eye_* by c: oscar64 compiled that loop to pass the axis
// in A and, from the second time round, left the loop's byte offset there
// instead - 4 and 8 for 1 and 2 - so the y axis read the eye's z, and every
// target came out half a unit, 64 m, to one side.
static __noinline void _gun_track(uint8_t c, int32_t eye) {
  int16_t d = (int16_t)(eye - _gun_eye[c]);
  _gun_moved[c] = d >> 5;
  _gun_eye[c] += (int16_t)(d & ~31);
  _gun_t[c] = 0;
}

// Axis c of a world position less the eye, in sixteenths of a metre the
// short way round - x repeats every 128 units, y every 256 - into _gun_t[t];
// false if it is farther than ~2 km, past what int16 holds.
static __noinline bool _gun_rel(int32_t d, uint8_t c, uint8_t t) {
  uint8_t u = (uint8_t)(d >> 16);
  if (c == 0) {
    u &= 0x7F;
    if (u & 0x40) {
      u |= 0x80;
    }
  }
  int8_t s = (int8_t)u;
  if (s < -15 || s > 14) {
    return false;
  }
  // A unit is 2,048 sixteenths; the 1/256 half metres below it, 65,536.
  _gun_t[t] = (int16_t)(((int16_t)s << 11) + (int16_t)((uint16_t)d >> 5));
  return true;
}

// A velocity's share of one step at flight_step_shift, like everything else
// that moves (opponent.cc _opponent_step_s()).
static __noinline int16_t _gun_step_s(int16_t v) {
  for (uint8_t n = flight_step_shift; n != 0; --n) {
    v >>= 1;
  }
  return v;
}

// A free slot, or failing one the oldest round's.
static uint8_t _gun_slot(void) {
  uint8_t slot = 0, oldest = 0;
  for (uint8_t r = 0; r < kRoundMax; ++r) {
    uint8_t age = _round_age[r];
    if (age == 0) {
      return r;
    }
    if (age > oldest) {
      oldest = age;
      slot = r;
    }
  }
  return slot;
}

// A round from the next wing: offset from the eye along the aircraft's front,
// left and up, and moving at the muzzle velocity along the nose, turned in
// towards the boresight, plus the aircraft's own motion - flight.cc's, along
// the nose in x and y and flight_vspeed in z, in 1/256 half metres a step.
static void _gun_fire(void) {
  uint8_t r = _gun_slot();
  // +left is the left wing; each turns in towards the other side.
  int16_t wing = _gun_side ? -kGunWing : kGunWing;
  int16_t in = _gun_side ? kGunConverge : -kGunConverge;
  _gun_side ^= 1;
  const int16_t *f = &flight_cam.front.x;
  const int16_t *l = &flight_cam.left.x;
  const int16_t *u = &flight_cam.up.x;
  int16_t speed2 = flight_speed << 1;
  uint8_t i = r;
  for (uint8_t c = 0; c < 3; ++c, i += kRoundMax) {
    _round_p[i] = vec_fastmul8p8(f[c], kGunAhead) +
                  vec_fastmul8p8(l[c], wing) - vec_fastmul8p8(u[c], kGunDrop);
    int16_t own = c == 2 ? flight_vspeed : vec_fastmul8p8(f[c], speed2);
    _round_v[i] = vec_fastmul8p8(f[c], kGunMuzzle) +
                  vec_fastmul8p8(l[c], in) + (own >> 5);
  }
  _round_age[r] = 1;
  _round_owner[r] = kRoundPlayer;
  _gun_last = r;
}

// Whether round r, moving one step from where it is, passes within the hit
// box of the point in _gun_t. Tested at four points along the step: a
// round moves up to 63 m a step with the aircraft's own speed in it, and
// would jump a 20 m aircraft, while the points are at most 16 m apart,
// inside the box's 20 m side.
static __noinline bool _gun_hits(uint8_t r) {
  uint8_t i = r;
  for (uint8_t c = 0; c < 3; ++c, i += kRoundMax) {
    int16_t d = _gun_t[c] - _round_p[i];
    int16_t v = _gun_step_s(_round_v[i]);
    // Out of reach of the whole step on any axis: nowhere near.
    if (_gun_abs(d) > _gun_abs(v) + kRoundHit) {
      return false;
    }
    _gun_d[c] = d;
    _gun_q[c] = v >> 2;
  }
  for (uint8_t n = 0; n < 4; ++n) {
    bool in = true;
    for (uint8_t c = 0; c < 3; ++c) {
      int16_t d = _gun_d[c] - _gun_q[c];
      _gun_d[c] = d;
      if (_gun_abs(d) > kRoundHit) {
        in = false;
      }
    }
    if (in) {
      return true;
    }
  }
  return false;
}

// The player's rounds into the opponents: each that hits frees its round
// and strikes the slot.
static void _gun_hit_opponents(void) {
  int32_t p[3];
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!opponent_target(i, p)) {
      continue;
    }
    uint8_t c = 0;
    while (c < 3 && _gun_rel(p[c] - _gun_eye[c], c, c)) {
      ++c;
    }
    if (c < 3) {
      continue;
    }
    for (uint8_t r = 0; r < kRoundMax; ++r) {
      if (_round_age[r] != 0 && _round_owner[r] == kRoundPlayer &&
          _gun_hits(r)) {
        opponent_strike(i);
        _round_age[r] = 0;
      }
    }
  }
}

void gun_step(bool trigger) {
  if (flight_paused || flight_crashed()) {
    return;
  }
  // The eye has moved since the last step: take it off every round.
  _gun_track(0, flight_eye_x);
  _gun_track(1, flight_eye_y);
  _gun_track(2, flight_eye_z);
  // Anyone else's rounds into the player, at the eye, which is 0 in the
  // rounds' frame; then the player's into the opponents; and every round
  // left on a step.
  for (uint8_t r = 0; r < kRoundMax; ++r) {
    if (_round_age[r] != 0 && _round_owner[r] != kRoundPlayer &&
        _gun_hits(r)) {
      ++gun_player_hits;
      _round_age[r] = 0;
    }
  }
  _gun_hit_opponents();
  // The ground, z 0, in the rounds' frame: a round below it is gone. Past
  // int16's reach the eye is too high for a round to get down there.
  int16_t ground = _gun_rel(-_gun_eye[2], 2, 0) ? _gun_t[0] : -32767;
  for (uint8_t r = 0; r < kRoundMax; ++r) {
    if (_round_age[r] == 0) {
      continue;
    }
    uint8_t i = r;
    for (uint8_t c = 0; c < 3; ++c, i += kRoundMax) {
      _round_p[i] += _gun_step_s(_round_v[i]) - _gun_moved[c];
    }
    if (_round_p[2 * kRoundMax + r] < ground) {
      _round_age[r] = 0;
    }
  }
  // Ages, and the trigger, once a whole step: at a larger flight_step_shift
  // the substeps above only share its distance out.
  if ((++_gun_substep & kFlightSubstepMask) != 0) {
    return;
  }
  for (uint8_t r = 0; r < kRoundMax; ++r) {
    if (_round_age[r] != 0 && ++_round_age[r] > kRoundLife) {
      _round_age[r] = 0;
    }
  }
  if (_gun_last != kRoundNone && _round_age[_gun_last] == 0) {
    _gun_last = kRoundNone;
  }
  // After the rounds have moved, so a new one is drawn where it leaves the
  // wing before its first step takes it 48 m on.
  // Not until the trigger has been let go once since the mission started:
  // Space also starts it from the menu, and is still held on its first
  // frames.
  if (!trigger) {
    _gun_armed = true;
  }
  if (_gun_wait != 0) {
    --_gun_wait;
  } else if (trigger && _gun_armed) {
    _gun_fire();
    _gun_wait = kGunInterval - 1;
  }
}

// --- The tracer ----------------------------------------------------------------
//
// A short line along the round's motion on screen, drawn with the instrument
// needles' bitmaps (spritedef.h kSpriteDefLongArm, kSpriteDefShortArm): 16
// angles over half a turn, and a line needs no more. Centred on the round.

// Depths, in camera space's eighth metres: X-expanded inside ~150 m, the long
// needle out to ~300 m, the short one beyond.
static const int16_t kTracerExpand = 1200;
static const int16_t kTracerLong = 2400;

// The needle bitmap for a line along (dx, dy) on screen, y down: 0..15,
// clockwise from straight up, the arms' order. The angle from the vertical
// is folded to an octant and compared against the tangents of the 11.25
// degree steps' midpoints, x256: no atan, and one byte multiply a compare.
static const uint8_t kTracerTan[4] = {25, 78, 137, 210};
static uint8_t _gun_dir(int16_t dx, int16_t dy) {
  // A line has no direction: fold onto the right half, or straight up.
  if (dx < 0 || (dx == 0 && dy > 0)) {
    dx = -dx;
    dy = -dy;
  }
  uint16_t a = (uint16_t)_gun_abs(dy), b = (uint16_t)dx;
  while (a >= 128 || b >= 128) {
    a >>= 1;
    b >>= 1;
  }
  // Within 45 degrees of the vertical, count the steps away from it; else
  // count those away from the horizontal, back from 8.
  bool steep = b <= a;
  uint16_t n = steep ? b : a, m = steep ? a : b;
  uint8_t k = 0;
  for (uint8_t t = 0; t < 4; ++t) {
    if ((n << 8) > vec_mul8x8((uint8_t)m, kTracerTan[t])) {
      ++k;
    }
  }
  if (!steep) {
    k = (uint8_t)(8 - k);
  }
  return dy > 0 ? (uint8_t)((16 - k) & 15) : k;
}

// The point in _gun_rel3 - relative to the eye like a round - in camera
// space and eighth metres, and where it is on screen in _gun_sx, _gun_sy;
// false if it is not in the view's cone.
static vec3_t _gun_rel3;
static int16_t _gun_sx, _gun_sy;
static __noinline bool _gun_view(vec3_t *cam) {
  vec3_t rel = make_vector(_gun_rel3.x >> 1, _gun_rel3.y >> 1, _gun_rel3.z >> 1);
  vec_transform_inv(&world_cam, &rel, cam);
  if (cam->x <= 64 || _gun_abs(cam->y) >= cam->x ||
      _gun_abs(cam->z) >= cam->x) {
    return false;
  }
  _gun_sx = kScreenWidthPixels / 2 - vec_div8p8(cam->y, cam->x);
  _gun_sy = kViewportEndYPixels / 2 - vec_div8p8(cam->z, cam->x);
  return true;
}

// Round r, as _gun_view().
static bool _gun_camera(uint8_t r, vec3_t *cam) {
  uint8_t i = r;
  for (uint8_t c = 0; c < 3; ++c, i += kRoundMax) {
    (&_gun_rel3.x)[c] = _round_p[i];
  }
  return _gun_view(cam);
}

// The round worth drawing: the player's newest while firing, else the
// youngest of anyone else's.
static uint8_t _gun_shown(void) {
  if (_gun_last != kRoundNone) {
    return _gun_last;
  }
  uint8_t shown = kRoundNone, youngest = 0xFF;
  for (uint8_t r = 0; r < kRoundMax; ++r) {
    uint8_t age = _round_age[r];
    if (age != 0 && _round_owner[r] != kRoundPlayer && age < youngest) {
      youngest = age;
      shown = r;
    }
  }
  return shown;
}

bool gun_tracer_up(void) { return _gun_shown() != kRoundNone; }

// --- Smoke --------------------------------------------------------------------
//
// One sprite, behind the aircraft opponent.cc says is smoking: a cloud blob
// drawn black, from the cloud ladder's single-sprite rungs, 1 to 4, picked
// for a ~16 m puff rather than the clouds' 96 m - rung 4 inside ~186 m, down
// to rung 1 past ~292 m, in metres.
static const int16_t kSmokeRungDepth[3] = {292, 228, 186};
// 25 m behind the aircraft along its heading, and 4 m above.
static const int16_t kSmokeBack = 25 * 16;
static const int16_t kSmokeUp = 4 * 16;

static void _gun_smoke(void) {
  int32_t p[3];
  int16_t h[2];
  if (!opponent_smoke(p, h)) {
    return;
  }
  for (uint8_t c = 0; c < 3; ++c) {
    if (!_gun_rel(p[c] - _gun_eye[c], c, c)) {
      return;
    }
  }
  _gun_rel3.x = _gun_t[0] - vec_fastmul8p8(h[0], kSmokeBack);
  _gun_rel3.y = _gun_t[1] - vec_fastmul8p8(h[1], kSmokeBack);
  _gun_rel3.z = _gun_t[2] + kSmokeUp;
  vec3_t c;
  if (!_gun_view(&c)) {
    return;
  }
  int16_t depth = c.x >> 3;
  uint8_t rung = 1;
  for (uint8_t k = 0; k < 3; ++k) {
    if (depth <= kSmokeRungDepth[k]) {
      ++rung;
    }
  }
  const sprite_cloud_rung_t *meta = &kSpriteDefCloudRung[rung];
  sprites_stack_add(depth, _gun_sx, _gun_sy, meta->pivot_x, meta->pivot_y,
                    meta->bitmap, kSpriteNoBitmap, kColorBlack,
                    kSpriteFlagExpandX | kSpriteFlagAlignDither);
}

void gun_add_sprites(void) {
  _gun_smoke();
  uint8_t r = _gun_shown();
  if (r == kRoundNone) {
    return;
  }
  vec3_t c;
  if (!_gun_camera(r, &c)) {
    return;
  }
  // Which way it is going on screen, exactly: the screen point is
  // (-y/x, -z/x) scaled, and its rate of change along the velocity v is
  // (y vx - vy x, z vx - vz x) over a positive x squared. Not the difference
  // to where the round will be a step on: a far round moves a pixel or less
  // a step, and whole pixels put its needle anywhere - mostly upright. The
  // velocity in eighth metres too, at most ~500, against a round within
  // ~1.3 km, ~10,000, keeps the products in int16 with room to spare.
  vec3_t v, w;
  uint8_t i = r;
  for (uint8_t a = 0; a < 3; ++a, i += kRoundMax) {
    (&v.x)[a] = _round_v[i] >> 1;
  }
  vec_transform_inv(&world_cam, &v, &w);
  int16_t dx = vec_fastmul8p8(c.y, w.x) - vec_fastmul8p8(w.y, c.x);
  int16_t dy = vec_fastmul8p8(c.z, w.x) - vec_fastmul8p8(w.z, c.x);
  uint8_t bitmap = kSpriteDefShortArmBitmap;
  uint8_t flags = 0;
  if (c.x < kTracerLong) {
    bitmap = kSpriteDefLongArmBitmap;
    if (c.x < kTracerExpand) {
      // Expansion doubles x, so the bitmap is the one for half the run: the
      // stretched line comes out along the true one.
      flags = kSpriteFlagExpandX;
      dx >>= 1;
    }
  }
  bitmap += _gun_dir(dx, dy);
  uint8_t color =
      _round_owner[r] == kRoundPlayer ? kColorYellow : kColorLightRed;
  // Depth in the clouds' 1 m units, like the opponents', so all sort together.
  sprites_stack_add(c.x >> 3, _gun_sx, _gun_sy, kSpriteDefArmCenterX,
                    kSpriteDefArmCenterY, bitmap, kSpriteNoBitmap, color, flags);
}
