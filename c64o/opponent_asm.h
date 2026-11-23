#ifndef OPPONENT_ASM_H
#define OPPONENT_ASM_H

#include <stdint.h>

#include "bool.h"
#include "planes.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "flight.h"
#include "mem.h"
#include "opponent.h"
#include "world.h"
static_assert(kOpponentMax == 2, "opponent_asm assumes 2 opponent slots");
#else
extern int32_t *const kFlightEye[3];
extern uint8_t flight_step_shift;
extern mat3_t world_cam;
extern bool model_on_ground;
extern mat3_t flight_cam;
#endif

// The assembly half of opponent.cc, in opponent_asm.cc: shared opponent world,
// silhouette, and scratch state in bss2 / tscratch, plus 6502 routines for
// eye-to-opponent world wrapping/scaling, waypoint/eye half-metre deltas,
// 3-axis position reads, layout/camera-space struct copies, 6-bit steering
// reduction + 2D heading cross/dot products, position integration
// (_opponent_move / _opponent_ahead), camera-space body axes (_opponent_axes),
// staged frustum culling (_opponent_cull_slot), per-slot AI/flight/fall step
// (_opponent_ai_step_asm / _opponent_fly_and_move_asm / _opponent_fall_asm),
// and 2-opponent sprite-budget depth hysteresis/capping (_opponent_cap_asm)
// calling vec_asm.cc's shared vec_mul_ab core. opponent.cc keeps the C
// counterparts under #ifndef __OSCAR64__ for test/opponent_test.cc and
// test/mission_test.cc, and test/target_test.cc holds the 6502 assembly to
// 16-bit C references on the emulated 6510.

enum opponent_range_t { kOpponentNear, kOpponentFar, kOpponentOut };
static_assert(kOpponentNear == 0 && kOpponentFar == 1 && kOpponentOut == 2,
              "world delta range constants");

// What a pair starts from at 0, together, so that _opponent_send() clears it
// in one loop rather than a store an array.
struct opponent_fresh_t {
  // The heading: the front, a unit vector in 8.8. Always level, so z is 0,
  // the left is (-y, x, 0) and the up is the world's.
  vec3_t head[2];
  // The waypoint it is flying to.
  uint8_t next[2];
  // Which way it turned on the last step: +1 left, -1 right, 0 straight. And
  // the bank drawn: the same, once a turn has lasted two steps, so that the
  // one-step corrections on a leg do not flick the wings.
  int8_t turn[2];
  int8_t bank[2];
  // The climb level, -3 to 3: below 0 a descent.
  int8_t climb[2];
  // The climb level drawn: the climb, once it has lasted two whole steps, as
  // the bank holds a turn, so that a one-step correction does not flick the
  // nose. Held, not levelled, in between.
  int8_t pitch[2];
  uint8_t hits[2];
  // Whole steps left of the gun's cycle, 0 when it is ready for a burst.
  uint8_t gun[2];
};

extern opponent_fresh_t _opponent_fresh;
#define _opponent_head (_opponent_fresh.head)
#define _opponent_next (_opponent_fresh.next)
#define _opponent_turn (_opponent_fresh.turn)
#define _opponent_bank (_opponent_fresh.bank)
#define _opponent_climb (_opponent_fresh.climb)
#define _opponent_pitch (_opponent_fresh.pitch)
#define _opponent_hits (_opponent_fresh.hits)
#define _opponent_gun (_opponent_fresh.gun)

// Position in flight_eye_*'s 24.8 half metres: x, y, then z, packed for
// _opponent_pos() and _opponent_move().
extern int32_t _opponent_xyz[6];
#define _opponent_x (_opponent_xyz)
#define _opponent_y (_opponent_xyz + 2)
#define _opponent_z (_opponent_xyz + 4)

extern planes_state_t _opponent_planes[2];
extern planes_layout_t _opponent_shown[2];
extern __striped int16_t _opponent_speed[2];

extern uint8_t _opponent_down;
extern uint8_t _opponent_pullup;
extern uint8_t _opponent_firing;
extern int8_t _opponent_step_want;
extern uint8_t _opponent_pending;

struct opponent_vis_t {
  uint8_t front;
  uint8_t first;
  uint8_t near_m;
  uint8_t nearer;
  uint8_t capped;
};
extern opponent_vis_t _opponent_vis;
#define _opponent_front (_opponent_vis.front)
#define _opponent_first (_opponent_vis.first)
#define _opponent_near (_opponent_vis.near_m)
#define _opponent_nearer (_opponent_vis.nearer)
#define _opponent_capped (_opponent_vis.capped)

extern vec3_t _opponent_c;
extern vec3_t _opponent_cs[2];
extern mat3_t _opponent_axes_m;
extern bool _opponent_aim;
extern bool _opponent_break;

// Flags packed into the `flags` byte for _opponent_ai_step_asm() and
// _opponent_fly_and_move_asm().
static const uint8_t kOppFlagHunt = 0x01;
static const uint8_t kOppFlagTurns = 0x02;
static const uint8_t kOppFlagBomber = 0x04;

// Pitch sine table (8.8), shared between _opponent_axes() and opponent_fires().
extern const int16_t kOpponentPitchSin[8];

#ifdef __OSCAR64__
uint8_t _opponent_world_delta(uint8_t i);
int16_t _opponent_toward(uint16_t w, uint8_t idx);
void _opponent_pos(uint8_t i, int32_t *p);
void _opponent_latch_shown(uint8_t i);
void _opponent_copy_cs(uint8_t i);
uint8_t _opponent_steer(uint8_t i);
void _opponent_move(uint8_t i, uint8_t idx, int16_t h);
void _opponent_ahead(uint8_t i);
void _opponent_axes(uint8_t i);
uint8_t _opponent_cull_slot(uint8_t i);
int16_t _opponent_ai_step_asm(uint8_t i, uint8_t flags);
uint8_t _opponent_fly_and_move_asm(uint8_t i, uint8_t flags, int16_t cruise);
uint8_t _opponent_fall_asm(uint8_t i, uint8_t whole);
void _opponent_cap_asm(uint8_t far, uint8_t cap_all);

#pragma compile("opponent_asm.cc")
#endif

#endif
