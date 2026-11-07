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

// No rounds in flight, the guns ready, and no damage. With opponent_reset().
void gun_reset(void);

// Fills gun_ammo back up to what gun_reset() gives, marking it for a redraw
// only if that changes it. flight.cc calls it when the aircraft comes to a
// halt on the ground.
void gun_rearm(void);

// One model step, after opponent_step(): fires the opponents' rounds it
// asked for, moves every round and hits what it meets, then fires the
// player's next round if `trigger` is held.
void gun_step(bool trigger);

// A round's speed from the muzzle, 48 m a step in sixteenths of a metre.
static const int16_t kGunMuzzle = 48 * 16;

// Per frame, after opponent_add_sprites(): the smoke behind the last aircraft
// hit, and the round worth seeing, as a tracer, offered to the sprite stack.
void gun_add_sprites(void);

// Whether gun_add_sprites() will want a sprite this frame - a round in flight
// worth drawing, if it is in view - for opponent.cc's count of what the
// aircraft must share the stack with.
bool gun_tracer_up(void);

// Whether there is smoke where a round hit the ground, which takes the smoke
// sprite when no aircraft is smoking: for the same count.
bool gun_puff_up(void);

// Hits the player has taken. Only opponents' rounds can land them, and each
// is damage (damage.h).
extern uint8_t gun_player_hits;

// The player's rounds left, as the decimal digits the panel's ammo counter
// shows, most significant first. gun_reset() fills them and every round fired
// - one tracer, which stands for ten - takes ten; at zero the guns are silent.
static const uint8_t kGunAmmoDigits = 3;
extern uint8_t gun_ammo[kGunAmmoDigits];

// False once gun_ammo has changed since gfx_update_ammo() last drew it, or the
// panel bitmap under the counter has been redrawn (gfx_invalidate_panel_bitmap).
extern bool gun_ammo_shown;

// The attempt's tallies for the debrief (debrief.h), as decimal digits like
// gun_ammo, most significant first, kGunTallyDigits a tally: the player's
// rounds fired, reloads included, from kGunTallyFired; those that hit an
// opponent, from kGunTallyHits; and from kGunTallyDown the aircraft shot down,
// which debrief.cc fills in from opponent_shot_down. The rounds are counted
// in tens like gun_ammo, so their last digit stays 0. gun_reset() zeroes them.
static const uint8_t kGunTallyDigits = 5;
static const uint8_t kGunTallyFired = 0;
static const uint8_t kGunTallyHits = kGunTallyDigits;
static const uint8_t kGunTallyDown = 2 * kGunTallyDigits;
static const uint8_t kGunTallies = 3;
extern uint8_t gun_tally[kGunTallies * kGunTallyDigits];

#pragma compile("gun.cc")

#else

inline void gun_reset(void) {}
inline void gun_rearm(void) {}
inline void gun_step(bool trigger) {}
inline void gun_add_sprites(void) {}
inline bool gun_tracer_up(void) { return false; }
inline bool gun_puff_up(void) { return false; }

#endif

#endif
