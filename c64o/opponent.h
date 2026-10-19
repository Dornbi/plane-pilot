#ifndef OPPONENT_H
#define OPPONENT_H

#include <stdint.h>

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

#pragma compile("opponent.cc")

#else

inline void opponent_reset(uint8_t mission) {}
inline void opponent_invalidate(void) {}
inline void opponent_step(void) {}
inline void opponent_add_sprites(void) {}

#endif

#endif
