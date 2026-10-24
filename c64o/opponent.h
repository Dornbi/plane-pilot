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
static const uint8_t kOpponentHitsToDown = 3;

// Puts the mission's aircraft at their start. Also invalidates, as below.
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

// The mission's WP_TARGETS_DOWN: every aircraft it sent is down, and the
// mission has not failed.
bool opponent_targets_down(void);

// For gun.cc: slot i's position, x, y and z in flight_eye_*'s 24.8 half
// metres, into p, and whether it is there to be hit - flying or parked, and
// not down.
bool opponent_target(uint8_t i, int32_t *p);

// Slot i is hit. It trails smoke from now on, and the third hit downs it: it
// stops, is no longer drawn or a target, and counts towards WP_TARGETS_DOWN.
void opponent_strike(uint8_t i);

// For gun.cc, which draws it: the smoking aircraft's position, as
// opponent_target() gives it, and its heading's x and y, 8.8 - the last one
// hit, or for a few seconds after it went down, where it fell. False if no
// smoke is up.
bool opponent_smoke(int32_t *p, int16_t *h);

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

#endif

#endif
