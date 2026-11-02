#ifndef DAMAGE_H
#define DAMAGE_H

#include <stdint.h>

#include "bool.h"
#include "flight.h"
#include "opponent.h"

// The player's damage (docs/opponent_ai.md §8): each hit from an opponent's
// round causes a new failure, picked at random from those the player does
// not have yet, and a message says which, while the border flashes red. The fifth hit shoots the
// player down. Nothing repairs a failure, and nothing on the panel shows
// one.
//
// fpilot only, with the opponents that do the shooting. Elsewhere the hooks
// below are the undamaged values and flight.cc compiles to what it was.
#ifdef __ENABLE_OPPONENT__

// The failures, a bit each in damage_bits.
static const uint8_t kDamageEngine = 0x01;     // the throttle's top halved
static const uint8_t kDamageFuel = 0x02;       // fuel burns at twice the throttle
static const uint8_t kDamageOil = 0x04;        // the engine seizes ~30 s later
static const uint8_t kDamageHydraulics = 0x08; // flaps and gear stay as they are
static const uint8_t kDamageControls = 0x10;   // roll, pitch and yaw at half rate
// Not a hit's: the oil leak's end. The throttle's top is 0.
static const uint8_t kDamageSeized = 0x20;
// Hits that shoot the player down.
static const uint8_t kDamageHitsToDown = 5;
// Shot down in the air: the controls gone, the aircraft falls - in a steep
// dive, and with kDamageSpin rolling as well - until it hits the ground,
// which is the crash, FLIGHT_CRASH_SHOT_DOWN (flight.cc).
static const uint8_t kDamageDown = 0x40;
static const uint8_t kDamageSpin = 0x80;

extern uint8_t damage_bits;
// Flips on every rotation input while the controls are damaged.
extern uint8_t damage_toggle;

// No failures. With gun_reset().
void damage_reset(void);

// The player's `hits`th hit, as gun.cc counts them: a new failure, or shot
// down - falling (kDamageDown), or on the ground the crash at once,
// FLIGHT_CRASH_SHOT_DOWN. None counts once falling.
void damage_hit(uint8_t hits);

// A whole model step: the oil leak's countdown.
void damage_step(void);

// Once a frame, from the simulation loop: the border's flash after a hit.
void damage_frame(void);

// The hooks in flight.cc, one line each: the throttle's top...
inline uint8_t damage_throttle_max(void) {
  return (damage_bits & kDamageSeized)   ? 0
         : (damage_bits & kDamageEngine) ? kMaxThrottle / 2
                                         : kMaxThrottle;
}
// ...the fuel a whole step burns at throttle t...
inline uint8_t damage_burn(uint8_t t) {
  return (damage_bits & kDamageFuel) ? (uint8_t)(t << 1) : t;
}
// ...whether flaps and gear are stuck...
inline bool damage_hydraulics(void) { return (damage_bits & kDamageHydraulics) != 0; }
// ...whether the player is shot down and falling...
inline bool damage_down(void) { return (damage_bits & kDamageDown) != 0; }
// ...and whether this rotation input is the one in two that is lost.
inline bool damage_skip_input(void) {
  return (damage_bits & kDamageControls) && (damage_toggle ^= 1) != 0;
}

#pragma compile("damage.cc")

#else

inline void damage_reset(void) {}
inline void damage_hit(uint8_t hits) {}
inline void damage_frame(void) {}
inline void damage_step(void) {}
inline uint8_t damage_throttle_max(void) { return kMaxThrottle; }
inline uint8_t damage_burn(uint8_t t) { return t; }
inline bool damage_hydraulics(void) { return false; }
inline bool damage_down(void) { return false; }
inline bool damage_skip_input(void) { return false; }

#endif

#endif
