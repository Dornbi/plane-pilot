#include "damage.h"

#include "color.h"
#include "fmath.h"
#include "msg.h"
#include "vic.h"

// docs/opponent_ai.md §8.

uint8_t damage_bits;
uint8_t damage_toggle;
// Whole steps until the leaking engine seizes, 0 if it is not leaking.
static uint8_t _damage_oil;

// ~32 s at 6.25 whole steps a second.
static const uint8_t kDamageOilSteps = 200;
// Frames of the border's flash after a hit, red on the odd ones as they
// count down: three flashes in under a second at 6-8 frames a second, and
// black at the end.
static const uint8_t kDamageFlash = 6;
static uint8_t _damage_flash;

// By failure, in kDamage* bit order: failure k is bit k, so its mask is
// fmath.h's kBit[k] and needs no table of its own.
static_assert(kDamageEngine == 1 << 0 && kDamageFuel == 1 << 1 && kDamageOil == 1 << 2 &&
                  kDamageHydraulics == 1 << 3 && kDamageControls == 1 << 4,
              "failure k is bit k");
static const char *const kDamageText[5] = {"ENGINE DAMAGED", "FUEL LEAK", "OIL LEAK",
                                           "HYDRAULICS HIT", "CONTROLS DAMAGED"};

void damage_reset(void) {
  damage_bits = 0;
  damage_toggle = 0;
  _damage_oil = 0;
  // A restart mid-flash would leave the border red.
  _damage_flash = 0;
  vic.color_border = kColorBlack;
}

// A new failure's effects that are not the hooks' to apply: the throttle cut
// to its new top, and the message.
static void _damage_new(const char *text) {
  uint8_t top = damage_throttle_max();
  if (flight_throttle > top) {
    flight_throttle = top;
  }
  msg_show(text);
}

void damage_hit(uint8_t hits) {
  if (damage_down()) {
    return;
  }
  _damage_flash = kDamageFlash;
  if (hits >= kDamageHitsToDown) {
    if (model_on_ground) {
      flight_status = FLIGHT_CRASH_SHOT_DOWN;
    } else {
      damage_bits |= (opponent_random() & 1) ? kDamageDown | kDamageSpin : kDamageDown;
      msg_show("SHOT DOWN!");
    }
    return;
  }
  // One of the five, near enough evenly, and if the player has it already
  // the next one round that they do not: hits 1-4 always find one.
  uint8_t k = opponent_random() & 15;
  while (k >= 5) {
    k -= 5;
  }
  while (damage_bits & kBit[k]) {
    if (++k == 5) {
      k = 0;
    }
  }
  uint8_t bit = kBit[k];
  damage_bits |= bit;
  if (bit == kDamageOil) {
    _damage_oil = kDamageOilSteps;
  }
  _damage_new(kDamageText[k]);
}

void damage_frame(void) {
  if (_damage_flash != 0) {
    vic.color_border = (--_damage_flash & 1) ? kColorRed : kColorBlack;
  }
}

void damage_step(void) {
  if (_damage_oil != 0 && --_damage_oil == 0) {
    damage_bits |= kDamageSeized;
    _damage_new("ENGINE SEIZED");
  }
}
