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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../vec.h"

#include "../color.h"
#include "../damage.h"
#include "../flight.h"
#include "../mission.h"
#include "../opponent.h"
#include "../vic.h"

// The border damage.cc flashes.
static uint8_t _vic[0x40];
struct VIC *vic_host = (struct VIC *)_vic;

static bool _down = false;
bool opponent_targets_down(void) { return _down; }

static int _rearms = 0;
void gun_rearm(void) { ++_rearms; }

// damage.cc's pick, set by hand.
static uint8_t _random = 1;
uint8_t opponent_random(void) { return _random; }

static const char *_msg = "";
static int _msgs = 0;
void msg_show(const char *text, uint8_t duration, bool permanent) {
  (void)duration;
  (void)permanent;
  _msg = text;
  ++_msgs;
}

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
  // message, the border's flash, and what it does.
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
    CHECK(damage_bits == kFailures[k].bit && !strcmp(_msg, kFailures[k].text),
          "damage %d: bits %02x, \"%s\"", k, damage_bits, _msg);
  }

  // A hit flashes the border red three times, a frame each, and leaves it
  // black; the fifth, which shoots the player down, as well.
  for (int hits = 1; hits <= kDamageHitsToDown; hits += kDamageHitsToDown - 1) {
    flight_init_from_mission(4);
    damage_reset();
    damage_hit((uint8_t)hits);
    char seen[16] = "";
    for (int n = 0; n < 8; ++n) {
      damage_frame();
      seen[n] = vic.color_border == kColorRed ? 'R' : vic.color_border == kColorBlack ? '.' : '?';
    }
    CHECK(!strcmp(seen, "R.R.R..."), "hit %d: border %s", hits, seen);
  }
  // A restart in the middle of one leaves it black.
  damage_hit(1);
  damage_frame();
  damage_reset();
  damage_frame();
  CHECK(vic.color_border == kColorBlack, "restart: border %d", vic.color_border);

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
  CHECK(seized == 200 && flight_throttle == 0 && !strcmp(_msg, "ENGINE SEIZED"),
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
  }

  // The fifth hit in the air: shot down. The controls are gone and the
  // aircraft falls - with an even random byte diving, with an odd one
  // spinning as well, rolling on round - until it hits the ground, and that
  // is the crash, SHOT DOWN. Mission 05 starts at 3,000 ft.
  for (int spin = 0; spin < 2; ++spin) {
    flight_init_from_mission(4);
    damage_reset();
    _random = (uint8_t)spin;
    damage_hit(5);
    CHECK(damage_down() && !!(damage_bits & kDamageSpin) == !!spin && !flight_crashed() &&
              !strcmp(_msg, "SHOT DOWN!"),
          "fall %d: the fifth hit left status %d, bits %02x, \"%s\"", spin, flight_status,
          damage_bits, _msg);
    // Nothing the pilot does takes, and no more hits count.
    mat3_t cam = flight_cam;
    uint8_t throttle = flight_throttle;
    flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    flight_input(FLIGHT_INPUT_THROTTLE_UP);
    flight_input(FLIGHT_INPUT_TOGGLE_GEAR);
    CHECK(!memcmp(&cam, &flight_cam, sizeof(mat3_t)) && flight_throttle == throttle,
          "fall %d: the controls still work", spin);
    uint8_t bits = damage_bits;
    damage_hit(6);
    CHECK(damage_bits == bits, "fall %d: a hit while falling counted", spin);
    int n = 0, steep = -1;
    bool rolled_over = false;
    while (!flight_crashed() && n < 1000) {
      flight_advance();
      ++n;
      if (steep < 0 && flight_cam.front.z <= -200) steep = n;
      rolled_over |= flight_cam.up.z < 0;
    }
    CHECK(flight_status == FLIGHT_CRASH_SHOT_DOWN &&
              !strcmp(flight_status_text(flight_status, true), "CRASHED: SHOT DOWN"),
          "fall %d: hit the ground with status %d after %d steps", spin, flight_status, n);
    CHECK(steep >= 0 && steep < 40 && flight_cam.front.z <= -200 && rolled_over == !!spin,
          "fall %d: steep after %d steps, nose %d at the ground, rolled over %d", spin, steep,
          flight_cam.front.z, rolled_over);
    printf("  shot down, %s: steep after %.1f s, the ground after %.1f s\n",
           spin ? "spinning" : "diving", steep / 6.25, n / 6.25);
  }

  // On the ground the fifth hit is the crash at once: 02 starts parked.
  flight_init_from_mission(1);
  damage_reset();
  damage_hit(5);
  CHECK(flight_status == FLIGHT_CRASH_SHOT_DOWN && !damage_down(),
        "on the ground: the fifth hit left status %d", flight_status);

  // --- The stall warning (flight.h flight_stall_warning()) -----------------
  //
  // fpilot flies the angle-of-attack model, where a turn held on the pull
  // key sits at the stall's angle without losing height: it must not warn.
  // A stall that sinks must, and as one warning, not one an onset. Mission
  // 05 starts at 3,000 ft; a minute of each, `bank` degrees, the pull key
  // held whenever the wings are within 4 degrees of it.
  {
    struct {
      int bank;
      uint8_t throttle;
      bool warns;
      const char *what;
    } const kCases[] = {
        {60, kMaxThrottle, false, "a 60 degree turn at full power"},
        {30, kMaxThrottle, false, "a 30 degree turn at full power"},
        {45, 18, false, "a 45 degree turn at cruise"},
        {75, kMaxThrottle, true, "a 75 degree turn, sinking"},
        {0, 0, true, "wings level at idle"},
    };
    for (const auto &c : kCases) {
      flight_init_from_mission(4);
      damage_reset();
      flight_throttle = c.throttle;
      int stalled = 0, warned = 0, onsets = 0;
      bool was = false;
      for (int n = 0; n < 375; ++n) {
        double roll = atan2(-flight_cam.left.z, flight_cam.up.z) * 180 / M_PI;
        if (roll < c.bank - 4) {
          flight_input(FLIGHT_INPUT_ROLL_LEFT);
        } else if (roll > c.bank + 4) {
          flight_input(FLIGHT_INPUT_ROLL_RIGHT);
        } else {
          flight_input(FLIGHT_INPUT_PITCH_UP);
        }
        flight_advance();
        stalled += flight_stall != 0;
        bool warning = flight_stall_warning() != 0;
        warned += warning;
        onsets += warning && !was;
        was = warning;
      }
      CHECK(stalled > 150 && (c.warns ? warned > 50 && onsets <= 2 : warned == 0),
            "stall warning, %s: stalled %d of 375 steps, warned %d, %d onsets", c.what, stalled,
            warned, onsets);
      printf("  stall warning, %s: stalled %d of 375 steps, warned %d in %d\n", c.what, stalled,
             warned, onsets);
    }
    // Back on the ground it is off at once.
    flight_init_from_mission(1);
    flight_stall_warn = 4;
    flight_advance();
    CHECK(!flight_stall_warning(), "stall warning: on the runway");
  }

  if (_fails) {
    printf("fpilot_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("fpilot_test: passed\n");
  return 0;
}
