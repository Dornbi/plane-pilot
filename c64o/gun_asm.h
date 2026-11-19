#ifndef GUN_ASM_H
#define GUN_ASM_H

#include <stdint.h>

#include "bool.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "flight.h"
#include "mem.h"
#include "opponent.h"
#include "world.h"
#else
static const uint8_t kOpponentMax = 2;
extern uint8_t kFlightSubstepMask;
extern int32_t *const kFlightEye[3];
extern uint8_t flight_step_shift;
extern mat3_t world_cam;
#endif

// The assembly half of gun.cc, in gun_asm.cc: shared round and scratch state
// in bss2 / tscratch, plus 6502 routines for eye tracking, world-to-eye
// wrapping, swept-box hit and collision tests, per-step round motion, active
// tracer selection, staged camera-cone projection, and 16-angle needle
// quantisation. gun.cc keeps the C counterparts under #ifndef __OSCAR64__ for
// test/gun_test.cc and test/mission_test.cc, and test/target_test.cc holds the
// 6502 assembly to 16-bit C references on the emulated 6510.

static const uint8_t kRoundMax = 8;
static const uint8_t kRoundNone = 0xFF;
// A round's owner: the player, or 1 + the opponent slot that fired it.
static const uint8_t kRoundPlayer = 0;
// Model steps a round flies before it is gone: ~600 m at 48 m a step.
static const uint8_t kRoundLife = 12;
// Half the hit box's side: 10 m against the ~20 m span the opponents are
// drawn at (opponent.cc kOpponentKn). And a near miss's, which provokes an
// aircraft that waits to be fired upon (opponent_provoke()): 40 m.
static const int16_t kRoundHit = 10 * 16;
static const int16_t kRoundNear = 40 * 16;
// Half the side of the box an opponent and the player collide in, on every
// axis: 12 m, about the ~20 m span the opponents are drawn at and the
// player's own.
static const int16_t kGunCollide = 12 * 16;
// Frames the smoke where a round hits the ground lasts: ~1.5 s at the 6-8 a
// second a stock C64 renders.
static const uint8_t kPuffFrames = 10;
static const uint8_t kRoundAxes = 3 * kRoundMax;

// Tangents of the 11.25 degree needle steps' midpoints, x256.
static const uint8_t kTracerTan[4] = {25, 78, 137, 210};

// Model steps flown, from 1; 0 is a free slot. Grouped with the other scalar
// state that gun_reset() zeroes so one byte loop clears all 14 B.
struct gun_z_t {
  uint8_t age[kRoundMax];
  uint8_t near_m;
  uint8_t wait;
  uint8_t side;
  uint8_t substep;
  uint8_t puff_t;
  bool armed;
};

extern __striped int16_t _round_p[kRoundAxes];
extern __striped int16_t _round_v[kRoundAxes];
extern gun_z_t _gun_z;
#define _round_age _gun_z.age
#define _gun_near _gun_z.near_m
#define _gun_wait _gun_z.wait
#define _gun_side _gun_z.side
#define _gun_substep _gun_z.substep
#define _gun_puff_t _gun_z.puff_t
#define _gun_armed _gun_z.armed
extern uint8_t _round_owner[kRoundMax];
extern int32_t _gun_eye[3];
extern __striped int16_t _gun_prev[3 * kOpponentMax];
extern uint8_t _gun_last;
extern __striped int16_t _gun_puff[3];

extern int32_t _gun_p[3];
extern __striped int16_t _gun_t[3];
extern __striped int16_t _gun_d[3];
extern __striped int16_t _gun_q[3];
extern __striped int16_t _gun_moved[3];
extern int16_t _gun_v[3];
extern vec3_t _gun_c;
extern int16_t _gun_s[2];
extern int16_t _gun_h[2];

#ifdef __OSCAR64__
void _gun_track_all(void);
uint8_t _gun_rel_from(uint8_t c0);
static inline uint8_t _gun_rel3_to(void) { return _gun_rel_from(0); }
static inline uint8_t _gun_rel2(void) { return _gun_rel_from(2); }
uint8_t _gun_hits(uint8_t r, int16_t box);
bool _gun_collides(uint8_t k, bool prev);
void _gun_store_enemy_round(uint8_t r, uint8_t owner);
uint8_t _gun_step_rounds(void);
uint8_t _gun_shown(void);
uint8_t _gun_dir(int16_t dx, int16_t dy);
bool _gun_view(void);
bool _gun_view_round(uint8_t r);
void _gun_vel_round(uint8_t r);

#pragma compile("gun_asm.cc")
#endif

#endif
