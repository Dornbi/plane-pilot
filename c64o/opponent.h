#ifndef OPPONENT_H
#define OPPONENT_H

#include <stdint.h>

#include "bool.h"

// Other aircraft: where they are, how they move, and the per-frame call that
// draws them with planes.cc into sprite blocks under I/O and offers them to
// the sprite stack. docs/planes.md §13 is the plan this follows.
//
// fpilot only. Everything below compiles to nothing elsewhere, so the call
// sites need no guards of their own; build fpilot with -D__NO_OPPONENT__ to
// leave it out and measure what it costs.
#if defined(__FPILOT__) && !defined(__NO_OPPONENT__)
#define __ENABLE_OPPONENT__
#endif

#ifdef __ENABLE_OPPONENT__

// The most aircraft a mission sends.
static const uint8_t kOpponentMax = 2;

// Hits that down an aircraft (opponent_strike()).
static const uint8_t kOpponentHitsToDown = 5;

// Puts the mission's first pair at their start, and forgets what the last
// attempt shot down. Also invalidates, as below. The rest of its pairs follow
// from opponent_step(), each once the last is gone.
void opponent_reset(uint8_t mission);

// Forgets every bitmap drawn so far and rewrites the dot block. For after
// anything that wrote over $D000-$D3FF - the map, and the title on the way
// into the simulation.
void opponent_invalidate(void);

// One model step, next to flight_advance(): the aircraft move as far as the
// player's model would at their airspeed.
void opponent_step(void);

// Per frame, between sprites_stack_reset() and sprites_stack_commit(): draw
// each aircraft in view and offer it to the stack.
void opponent_add_sprites(void);

// The mission's WP_TARGETS_DOWN: the mission has not failed, and as many of
// its aircraft are down as it asks - or, where it does not say, every pair it
// sends is down or gone (09's give up).
bool opponent_targets_down(void);

// For gun.cc: slot i's position, x, y and z in flight_eye_*'s 24.8 half
// metres, into p, and whether it is there to be hit - flying or parked, and
// not down.
bool opponent_target(uint8_t i, int32_t *p);

// Slot i is hit. It trails smoke for ~2 s, and the fifth hit downs it: it
// stops, is no longer drawn or a target, and counts towards WP_TARGETS_DOWN,
// and its smoke hangs where it fell for ~5 s. A hit provokes it too.
void opponent_strike(uint8_t i);

// For gun.cc, after opponent_step(): whether slot i fires a round this step
// and, if it does, the round's velocity into v - x, y and z in sixteenths of
// a metre a step, from the muzzle along the nose plus the aircraft's own.
// Once: a second call the same step says no.
bool opponent_fires(uint8_t i, int16_t *v);

// For gun.cc: slot i has flown into the player, or the player into it. The
// flight ends, FLIGHT_CRASH_COLLISION, and the slot is down.
void opponent_collide(uint8_t i);

// For gun.cc: whether slot i waits to be fired upon - Provoked, and not yet
// attacking - and so whether a near miss is worth testing for.
bool opponent_calm(uint8_t i);

// Slot i is fired upon: a hit, or a round past it within ~40 m. A Provoked
// slot attacks from now on - as it does once it sees the player; any other
// carries on as it was.
void opponent_provoke(uint8_t i);

// A random byte, never 0: the next of 255 in a fixed order.
uint8_t opponent_random(void);

// For gun.cc, which draws it: the smoking aircraft's position, as
// opponent_target() gives it, and its heading's x and y, 8.8 - the last one
// hit, for ~2 s after the hit or ~5 s after it went down. False if no smoke
// is up.
bool opponent_smoke(int32_t *p, int16_t *h);

// Aircraft the player has shot down since opponent_reset(), for the debrief
// (debrief.h): those the guns downed, not those that flew into the ground or
// into the player.
extern uint8_t opponent_shot_down;

#pragma compile("opponent.cc")

#else

inline void opponent_reset(uint8_t mission) {}
inline void opponent_invalidate(void) {}
inline void opponent_step(void) {}
inline void opponent_add_sprites(void) {}
inline bool opponent_targets_down(void) { return false; }
inline bool opponent_target(uint8_t i, int32_t *p) { return false; }
inline void opponent_strike(uint8_t i) {}
inline bool opponent_smoke(int32_t *p, int16_t *h) { return false; }
inline bool opponent_fires(uint8_t i, int16_t *v) { return false; }
inline bool opponent_calm(uint8_t i) { return false; }
inline void opponent_provoke(uint8_t i) {}
inline void opponent_collide(uint8_t i) {}
inline uint8_t opponent_random(void) { return 1; }

#endif

#endif
