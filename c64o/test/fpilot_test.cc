// Host test for fpilot's side of flight.cc: its missions (fmission.cc) and
// the two levels of completing one (mission.h).
//
// GOALS COMPLETED! when every opponent is down, which marks the mission once
// in the menu; MISSION COMPLETED! when the pilot then lands on runway 1,
// which marks it twice. A later attempt that gets only as far as the goals
// keeps the two. And every mission has its two navpoints, the targets' and
// runway 1's, in that order, selected as "TARGET" and "HOME BASE".
//
// flight.cc is linked as fpilot builds it, -D__FPILOT__, with fmission.cc.
// What it asks opponent.cc - whether the targets are down - what it tells
// the message row, and the guns it rearms are stubbed here.

#include <stdio.h>
#include <string.h>

#include "../vec.h"

#include "../damage.h"
#include "../flight.h"
#include "../mission.h"
#include "../opponent.h"

static bool _down = false;
bool opponent_targets_down(void) { return _down; }

static int _rearms = 0;
void gun_rearm(void) { ++_rearms; }

// damage.cc's pick, set by hand.
static uint8_t _random = 1;
uint8_t opponent_random(void) { return _random; }

static const char *_msg = "";
static int _msgs = 0;
static bool _blink = false;
void msg_show(const char *text, uint8_t duration, bool permanent) {
  (void)duration;
  (void)permanent;
  _msg = text;
  _blink = false;
  ++_msgs;
}
void msg_blink(void) { _blink = true; }

static int _fails = 0;

#define CHECK(cond, ...)             \
  do {                               \
    if (!(cond)) {                   \
      ++_fails;                      \
      printf("fpilot_test: ");       \
      printf(__VA_ARGS__);           \
      printf("\n");                  \
    }                                \
  } while (0)

int main() {
  for (int m = 0; m < kMissionCount; ++m) {
    mission_completed[m] = 0;
    _down = false;
    flight_init_from_mission(m);
    CHECK(flight_num_nav_points == 2 && flight_nav_point_x[1] == 0x2000 &&
              flight_nav_point_y[1] == 0x3F80,
          "mission %d: %d navpoints, 2 at %04x %04x", m + 1, flight_num_nav_points,
          flight_nav_point_x[1], flight_nav_point_y[1]);

    // Selecting them: the runway is a navpoint, the targets an enemy.
    flight_input(FLIGHT_INPUT_TOGGLE_NAV);
    CHECK(!strcmp(_msg, "HOME BASE SELECTED"), "mission %d: selecting 2 says \"%s\"", m + 1,
          _msg);
    flight_input(FLIGHT_INPUT_TOGGLE_NAV);
    CHECK(!strcmp(_msg, "TARGET SELECTED"), "mission %d: selecting 1 says \"%s\"", m + 1,
          _msg);
    // flight.cc names them by number, so the mission has to have them in
    // that order: the targets' waypoint first, then landing on runway 1.
    int w = kMissionWpBegin[m];
    CHECK(kMissionWpEnd[m] - w == 2 && kMissionWpConstraint[w] == WP_TARGETS_DOWN &&
              kMissionWpConstraint[w + 1] == WP_LANDED,
          "mission %d: navpoints not the targets', then home", m + 1);

    // Nothing until the targets are down - even parked on runway 1, where
    // 02 starts and every mission ends.
    for (int n = 0; n < 20; ++n) flight_advance();
    CHECK(mission_completed[m] == 0 && flight_current_wp == 0,
          "mission %d: completed with the targets up", m + 1);

    _down = true;
    _msgs = 0;
    flight_advance();
    CHECK(mission_completed[m] == kMissionGoals && !strcmp(_msg, "GOALS COMPLETED!") &&
              flight_status == FLIGHT_ONGOING,
          "mission %d: targets down: level %d, \"%s\", status %d", m + 1, mission_completed[m],
          _msg, flight_status);

    // Then parked on runway 1 - where 02 starts, so as it starts, but still
    // flying this mission with its goals met: landed, so the mission.
    uint8_t wp = flight_current_wp;
    flight_init_from_mission(1);
    flight_active_mission_idx = (uint8_t)m;
    flight_current_wp = wp;
    flight_advance();
    CHECK(mission_completed[m] == kMissionCompleted && kMissionCompleted == 2 &&
              !strcmp(_msg, "MISSION COMPLETED!") && flight_status == FLIGHT_MISSION_COMPLETED,
          "mission %d: home: level %d, \"%s\", status %d", m + 1, mission_completed[m], _msg,
          flight_status);
    CHECK(_msgs == 2, "mission %d: %d messages", m + 1, _msgs);

    // Again, only as far as the goals: the two marks stay.
    flight_init_from_mission(m);
    _down = false;
    flight_advance();
    _down = true;
    flight_advance();
    CHECK(mission_completed[m] == kMissionCompleted, "mission %d: level %d after a second try",
          m + 1, mission_completed[m]);
  }

  // Rolling to a halt, the guns are rearmed once; begun parked, as 02
  // starts, or rolling, they are not.
  flight_init_from_mission(1);
  _rearms = 0;
  flight_advance();
  CHECK(_rearms == 0, "begun parked: rearmed %d times", _rearms);
  flight_speed = 0x0010;
  flight_advance();
  CHECK(_rearms == 0, "rolling: rearmed %d times", _rearms);
  for (int n = 0; n < 50; ++n) flight_advance();
  CHECK(flight_speed == 0 && _rearms == 1, "halted: speed %d, rearmed %d times", flight_speed,
        _rearms);

  // The goals alone, nowhere near runway 1: one mark.
  mission_completed[0] = 0;
  flight_init_from_mission(0);
  flight_eye_x = 0x500000;
  _down = true;
  flight_advance();
  flight_advance();
  CHECK(mission_completed[0] == kMissionGoals, "away from home: level %d", mission_completed[0]);

  // --- Damage (damage.cc, docs/opponent_ai.md §8) -------------------------
  //
  // Mission 05 starts in the air. Each failure by itself, picked by hand
  // (damage.cc takes the random byte's low four bits, folded to 0-4): its
  // blinking message, and what it does.
  struct {
    uint8_t bit;
    const char *text;
  } const kFailures[5] = {{kDamageEngine, "ENGINE DAMAGED"},
                          {kDamageFuel, "FUEL LEAK"},
                          {kDamageOil, "OIL LEAK"},
                          {kDamageHydraulics, "HYDRAULICS HIT"},
                          {kDamageControls, "CONTROLS DAMAGED"}};
  for (int k = 0; k < 5; ++k) {
    flight_init_from_mission(4);
    damage_reset();
    flight_throttle = kMaxThrottle;
    _random = (uint8_t)k;
    damage_hit(1);
    CHECK(damage_bits == kFailures[k].bit && !strcmp(_msg, kFailures[k].text) && _blink,
          "damage %d: bits %02x, \"%s\", blink %d", k, damage_bits, _msg, _blink);
  }

  // Engine: the throttle cut to half, and no higher after.
  flight_init_from_mission(4);
  damage_reset();
  flight_throttle = kMaxThrottle;
  _random = 0;
  damage_hit(1);
  for (int n = 0; n < 30; ++n) flight_input(FLIGHT_INPUT_THROTTLE_UP);
  CHECK(flight_throttle == kMaxThrottle / 2, "engine: throttle %d", flight_throttle);

  // Fuel leak: twice the fuel a whole step.
  {
    uint32_t burnt[2];
    for (int leak = 0; leak < 2; ++leak) {
      flight_init_from_mission(4);
      damage_reset();
      if (leak) damage_bits = kDamageFuel;
      flight_throttle = 0x10;
      uint32_t f = flight_fuel;
      for (int n = 0; n < 20; ++n) flight_advance();
      burnt[leak] = f - flight_fuel;
    }
    CHECK(burnt[0] == 20 * 0x10 && burnt[1] == 2 * burnt[0], "fuel leak: %u, then %u",
          (unsigned)burnt[0], (unsigned)burnt[1]);
  }

  // Oil leak: the engine seizes, throttle and its top 0, after its count of
  // whole steps and not before.
  flight_init_from_mission(4);
  damage_reset();
  flight_throttle = 0x10;
  _random = 2;
  damage_hit(1);
  int seized = -1;
  for (int n = 0; n < 300 && seized < 0; ++n) {
    damage_step();
    if (damage_bits & kDamageSeized) seized = n + 1;
  }
  CHECK(seized == 200 && flight_throttle == 0 && !strcmp(_msg, "ENGINE SEIZED") && _blink,
        "oil: seized after %d steps, throttle %d, \"%s\"", seized, flight_throttle, _msg);
  flight_input(FLIGHT_INPUT_THROTTLE_UP);
  CHECK(flight_throttle == 0, "oil: the seized engine took throttle");

  // Hydraulics: flaps and gear stay where they are.
  flight_init_from_mission(4);
  damage_reset();
  uint8_t flap = flight_flap, gear = flight_gear;
  damage_bits = kDamageHydraulics;
  flight_input(FLIGHT_INPUT_TOGGLE_FLAP);
  flight_input(FLIGHT_INPUT_TOGGLE_GEAR);
  CHECK(flight_flap == flap && flight_gear == gear, "hydraulics: flaps %d gear %d moved",
        flight_flap, flight_gear);
  damage_bits = 0;
  flight_input(FLIGHT_INPUT_TOGGLE_FLAP);
  CHECK(flight_flap != flap, "hydraulics: undamaged flaps did not move");

  // Controls: every other rotation input, so ten rolls do what five do.
  {
    mat3_t after[2];
    for (int hurt = 0; hurt < 2; ++hurt) {
      flight_init_from_mission(4);
      damage_reset();
      if (hurt) damage_bits = kDamageControls;
      for (int n = 0; n < (hurt ? 10 : 5); ++n) flight_input(FLIGHT_INPUT_ROLL_LEFT);
      after[hurt] = flight_cam;
    }
    CHECK(!memcmp(&after[0], &after[1], sizeof(mat3_t)),
          "controls: ten rolls damaged are not five undamaged");
  }

  // Four hits, four different failures, whatever the random byte; the
  // fifth shoots the player down.
  {
    bool all = true;
    for (int r = 1; r < 256; ++r) {
      flight_init_from_mission(4);
      damage_reset();
      _random = (uint8_t)r;
      int n = 0;
      for (int h = 1; h <= 4; ++h) {
        uint8_t before = damage_bits;
        damage_hit((uint8_t)h);
        if (__builtin_popcount(damage_bits) == __builtin_popcount(before) + 1) ++n;
        _random = (uint8_t)(_random * 5 + 3);
      }
      all &= n == 4 && flight_status == FLIGHT_ONGOING;
    }
    CHECK(all, "damage: four hits did not always give four failures");
    damage_hit(5);
    CHECK(flight_status == FLIGHT_CRASH_SHOT_DOWN && flight_crashed() &&
              !strcmp(flight_status_text(flight_status, true), "CRASHED: SHOT DOWN"),
          "damage: the fifth hit left status %d, \"%s\"", flight_status,
          flight_status_text(flight_status, true));
  }

  if (_fails) {
    printf("fpilot_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("fpilot_test: passed\n");
  return 0;
}
