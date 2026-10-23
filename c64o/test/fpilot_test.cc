// Host test for fpilot's side of flight.cc: its missions (fmission.cc) and
// the two levels of completing one (mission.h).
//
// GOALS COMPLETED! when every opponent is down, which marks the mission once
// in the menu; MISSION COMPLETED! when the pilot then lands on runway 1,
// which marks it twice. A later attempt that gets only as far as the goals
// keeps the two. And every mission has its two navpoints, the targets' and
// runway 1's.
//
// flight.cc is linked as fpilot builds it, -D__FPILOT__, with fmission.cc.
// What it asks opponent.cc - whether the targets are down - and what it tells
// the message row are stubbed here.

#include <stdio.h>
#include <string.h>

#include "../flight.h"
#include "../mission.h"
#include "../opponent.h"

static bool _down = false;
bool opponent_targets_down(void) { return _down; }

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

    // Parked on runway 1, which is where it ends: nothing until the targets
    // are down.
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

    // Still on runway 1: landed, so the mission.
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

  // The goals alone, nowhere near runway 1: one mark.
  mission_completed[0] = 0;
  flight_init_from_mission(0);
  flight_eye_x = 0x500000;
  _down = true;
  flight_advance();
  flight_advance();
  CHECK(mission_completed[0] == kMissionGoals, "away from home: level %d", mission_completed[0]);

  if (_fails) {
    printf("fpilot_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("fpilot_test: passed\n");
  return 0;
}
