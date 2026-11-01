#ifndef GUN_H
#define GUN_H

#include <stdint.h>

#include "bool.h"
#include "opponent.h"

// The guns: rounds from the player's wings, flown in world space and hit
// against the opponents every model step, and one of them drawn as a tracer
// on a single sprite. docs/projectiles.md is the plan this follows.
//
// fpilot only, with the opponents it shoots at: without them everything
// below compiles to nothing and the call sites need no guards.
#ifdef __ENABLE_OPPONENT__

// No rounds in flight, the guns ready. With opponent_reset().
void gun_reset(void);

// One model step, after opponent_step(): moves every round and hits what it
// meets, then fires the next round if `trigger` is held.
void gun_step(bool trigger);

// Per frame, after opponent_add_sprites(): the smoke behind the last aircraft
// hit, and the round worth seeing, as a tracer, offered to the sprite stack.
void gun_add_sprites(void);

// Whether gun_add_sprites() will want a sprite this frame - a round in flight
// worth drawing, if it is in view - for opponent.cc's count of what the
// aircraft must share the stack with.
bool gun_tracer_up(void);

// Hits the player has taken. Only opponents' rounds can land them, and
// nothing fires those yet.
extern uint8_t gun_player_hits;

// The player's rounds left, as the decimal digits the panel's ammo counter
// shows, most significant first. gun_reset() fills them and every round fired
// takes one; at zero the guns are silent.
static const uint8_t kGunAmmoDigits = 4;
extern uint8_t gun_ammo[kGunAmmoDigits];

// False once gun_ammo has changed since gfx_update_ammo() last drew it, or the
// panel bitmap under the counter has been redrawn (gfx_invalidate_panel_bitmap).
extern bool gun_ammo_shown;

#pragma compile("gun.cc")

#else

inline void gun_reset(void) {}
inline void gun_step(bool trigger) {}
inline void gun_add_sprites(void) {}
inline bool gun_tracer_up(void) { return false; }

#endif

#endif
