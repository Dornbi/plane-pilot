// Host test for fpilot's missions as they fly: flight.cc and opponent.cc
// together, from each mission's start.
//
// The rules are that every mission puts its targets on screen - in the
// front view, if only as the dot - within 10 s of the start in the air, or of
// the lift-off from the runway within 20 s, but for 04's, which come from
// behind; and brings the player within 1 km of them inside 90 s. A plain
// pilot flies here: full throttle, straight on the heading the mission
// starts on; holding the start's height if it starts in the air, taking off
// and climbing if it starts on the runway.
// Nothing it does is clever, so a real pilot gets there no later.
//
// And 01's recon, met head-on and 2,500 ft above, has to be catchable after
// the climb: a pilot who turns after it at the pass and only then climbs to
// its height has to reach it with a minute in hand before it leaves the map.
// That one does have to steer - banked turns towards it - and hold the
// recon's height once there.
//
// What opponent.cc draws with and tells the message row is stubbed; nothing
// here draws.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../flight.h"
#include "../mission.h"
#include "../opponent.h"
#include "../sprites.h"
#include "../vec.h"
#include "../view.h"
#include "../world.h"

// What opponent.cc reads from modules not linked here.
mat3_t world_cam;
view_state_t view_state = VIEW_CENTER;
static uint8_t _bank[0x4000];
uint8_t *kOpponentBank = _bank;

bool sprites_stack_add(int16_t, int16_t, int16_t, int8_t, int8_t, uint8_t, uint8_t, uint8_t,
                       uint8_t) {
  return true;
}
void msg_show(const char *, uint8_t, bool) {}

// opponent.cc, for where the aircraft are, and gun.cc, to shoot them.
#include "../opponent.cc"
#include "../gun.cc"

static int _fails = 0;

#define CHECK(cond, ...)             \
  do {                               \
    if (!(cond)) {                   \
      ++_fails;                      \
      printf("mission_test: ");      \
      printf(__VA_ARGS__);           \
      printf("\n");                  \
    }                                \
  } while (0)

static const double kStepsPerSecond = 6.25;

// Horizontal distance from the player to slot i, in flight_eye_*'s half
// metres, the short way round the world's 128 units of x.
static double distance(int i) {
  double dx = (double)(_opponent_x[i] - flight_eye_x) / 256.0;
  double dy = (double)(_opponent_y[i] - flight_eye_y) / 256.0;
  dx = fmod(dx + 16384.0 + 4 * 32768.0, 32768.0) - 16384.0;
  return sqrt(dx * dx + dy * dy);
}

static double degrees(double y, double x) { return atan2(y, x) * 180 / M_PI; }

// The point (dx, dy, dz), world half metres from the eye, along the
// aircraft's front, left and up.
static double along(const vec3_t &a, double dx, double dy, double dz) {
  return (a.x * dx + a.y * dy + a.z * dz) / 256.0;
}

// 01: holds the start's height until the recon is past overhead, then turns
// after it in a 45 degree bank and climbs to its height, and chases it there.
// When it first gets within 250 m of it, in steps, and when the recon left.
//
// With `guns`, it also shoots: inside 400 m it puts its nose on the recon in
// height as well, and holds Space while the recon is within 500 m and about
// 3.5 degrees of the nose. When the recon went down, in steps.
static void chase(bool guns, int *caught_at, int *gone_at, int *down_at) {
  flight_init_from_mission(0);
  opponent_reset(0);
  gun_reset();
  const int32_t hold_z = flight_eye_z;
  bool passed = false;
  *caught_at = *gone_at = *down_at = -1;
  for (int n = 0; n < (int)(kStepsPerSecond * 600) && *gone_at < 0 && *down_at < 0; ++n) {
    flight_throttle = 24;
    double dx = (double)(_opponent_x[0] - flight_eye_x) / 256.0;
    double dy = (double)(_opponent_y[0] - flight_eye_y) / 256.0;
    double dz = (double)(_opponent_z[0] - flight_eye_z) / 256.0;
    passed = passed || dx < 0;
    double ahead = along(flight_cam.front, dx, dy, dz);
    double side = along(flight_cam.left, dx, dy, dz);
    double above = along(flight_cam.up, dx, dy, dz);
    bool aiming = guns && passed && ahead > 0 && ahead < 800;
    // Bank towards the recon's bearing once it is past - left for a target
    // to the left, a left roll being positive here - and wings level when
    // it is near dead ahead.
    double err = degrees(dy, dx) - degrees(flight_cam.front.y, flight_cam.front.x);
    while (err > 180) err -= 360;
    while (err < -180) err += 360;
    double want_roll = !passed ? 0 : err > 10 ? 45 : err < -10 ? -45 : err * 2;
    double roll = degrees(-flight_cam.left.z, flight_cam.up.z);
    if (roll < want_roll - 4) {
      flight_input(FLIGHT_INPUT_ROLL_LEFT);
    } else if (roll > want_roll + 4) {
      flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    }
    // Height: the start's until the pass, then climbing at pitch 80 to the
    // recon's, then holding that.
    if (aiming) {
      if (above > ahead / 64) {
        flight_input(FLIGHT_INPUT_PITCH_UP);
      } else if (above < -ahead / 64) {
        flight_input(FLIGHT_INPUT_PITCH_DOWN);
      }
    } else if (passed && dz > 0) {
      if (flight_cam.front.z < 80) {
        flight_input(FLIGHT_INPUT_PITCH_UP);
      } else if (flight_cam.front.z > 88) {
        flight_input(FLIGHT_INPUT_PITCH_DOWN);
      }
    } else {
      int32_t z = passed ? _opponent_z[0] : hold_z;
      if (flight_eye_z < z && flight_vspeed <= 0) {
        flight_input(FLIGHT_INPUT_PITCH_UP);
      } else if (flight_eye_z > z && flight_vspeed >= 0) {
        flight_input(FLIGHT_INPUT_PITCH_DOWN);
      }
    }
    flight_advance();
    opponent_step();
    gun_step(aiming && ahead > 80 && ahead < 1000 && fabs(side) < ahead / 16 &&
             fabs(above) < ahead / 16);
    CHECK(!flight_crashed(), "chase: crashed at %.0f s", n / kStepsPerSecond);
    if (flight_crashed()) return;
    if (passed && *caught_at < 0 && sqrt(dx * dx + dy * dy + dz * dz) < 500) *caught_at = n;
    if (!_opponent_on[0]) *gone_at = n;
    if (_opponent_down & 1) *down_at = n;
  }
}

int main() {
  static const char *const kNames[] = {"recon", "bombers", "ambush", "scramble"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == kMissionCount, "a name a mission");
  // Seconds to the targets on screen, from the start in the air or the
  // lift-off; 0 for none. 04's come from behind: the pair from 200 m and
  // 384 m back down the runway, overtaking only a pilot slower off it than
  // this one.
  static const int kInView[] = {10, 20, 10, 0};
  for (int m = 0; m < kMissionCount; ++m) {
    flight_init_from_mission(m);
    opponent_reset(m);
    const bool on_ground = kMissionStartZ[m] == 0;
    const int32_t hold_z = flight_eye_z;
    int close_at = -1, gone_at = -1, failed_at = -1, seen_at = -1, up_at = on_ground ? -1 : 0;
    double nearest = 1e9;
    for (int n = 0; n < (int)(kStepsPerSecond * 600); ++n) {
      flight_throttle = 24;
      if (on_ground) {
        // Rotate once fast enough, then hold a steady climb.
        if (flight_speed > 1300 || flight_eye_z > 0x2800) {
          if (flight_cam.front.z < 80) {
            flight_input(FLIGHT_INPUT_PITCH_UP);
          } else if (flight_cam.front.z > 88) {
            flight_input(FLIGHT_INPUT_PITCH_DOWN);
          }
        }
      } else if (flight_eye_z < hold_z && flight_vspeed <= 0) {
        flight_input(FLIGHT_INPUT_PITCH_UP);
      } else if (flight_eye_z > hold_z && flight_vspeed >= 0) {
        flight_input(FLIGHT_INPUT_PITCH_DOWN);
      }
      flight_advance();
      opponent_step();
      CHECK(!flight_crashed(), "%s: crashed at %.0f s", kNames[m], n / kStepsPerSecond);
      if (flight_crashed()) break;
      if (up_at < 0 && flight_eye_z > 0x2000) up_at = n;
      // On screen as opponent.cc decides it, in the front view.
      world_cam = flight_cam;
      if (seen_at < 0 && _opponent_cull()) seen_at = n;
      bool any = false;
      for (int i = 0; i < kOpponentMax; ++i) {
        if (!_opponent_on[i]) continue;
        any = true;
        double d = distance(i);
        if (d < nearest) nearest = d;
        if (d <= 2000 && close_at < 0) close_at = n;
      }
      if (_opponent_failed && failed_at < 0) failed_at = n;
      if (!any && gone_at < 0) gone_at = n;
    }
    const int in_view = kInView[m];
    CHECK(!in_view || (up_at >= 0 && seen_at >= 0 && seen_at - up_at <= in_view * kStepsPerSecond),
          "%s: on screen %.1f s after %s, not %d", kNames[m], (seen_at - up_at) / kStepsPerSecond,
          on_ground ? "the lift-off" : "the start", in_view);
    CHECK(close_at >= 0 && close_at <= 90 * kStepsPerSecond,
          "%s: within 1 km after %.0f s, not 90", kNames[m], close_at / kStepsPerSecond);
    printf("  %s: ", kNames[m]);
    if (on_ground) printf("off at %.0f s, ", up_at / kStepsPerSecond);
    if (seen_at >= 0) printf("on screen at %.1f s, ", seen_at / kStepsPerSecond);
    printf("within 1 km at %.0f s", close_at / kStepsPerSecond);
    if (failed_at >= 0) printf(", failed at %.1f min", failed_at / kStepsPerSecond / 60);
    if (gone_at >= 0) printf(", all gone at %.1f min", gone_at / kStepsPerSecond / 60);
    printf("\n");
  }
  int caught_at, gone_at, down_at;
  chase(false, &caught_at, &gone_at, &down_at);
  CHECK(caught_at >= 0 && gone_at - caught_at >= 60 * kStepsPerSecond,
        "chase: within 250 m of the recon at %.1f min, and it left at %.1f",
        caught_at / kStepsPerSecond / 60, gone_at / kStepsPerSecond / 60);
  printf("  recon, climbing only after the pass: within 250 m at %.1f min, %.1f min before it "
         "leaves\n",
         caught_at / kStepsPerSecond / 60, (gone_at - caught_at) / kStepsPerSecond / 60);

  // End to end, with the guns: the same chase shoots the recon down before
  // it leaves, and the mission takes it as its goals completed - three hits
  // through gun.cc, opponent_strike() and flight.cc's WP_TARGETS_DOWN.
  mission_completed[0] = 0;
  chase(true, &caught_at, &gone_at, &down_at);
  // flight.cc meets the waypoint on its next step.
  for (int n = 0; n < 4; ++n) flight_advance();
  CHECK(down_at >= 0 && !_opponent_failed && mission_completed[0] == kMissionGoals,
        "guns: recon down at %.1f min, failed %d, level %d", down_at / kStepsPerSecond / 60,
        _opponent_failed, mission_completed[0]);
  printf("  recon, shot down by the same pilot with guns: at %.1f min\n",
         down_at / kStepsPerSecond / 60);

  if (_fails) {
    printf("mission_test: %d checks FAILED\n", _fails);
    return 1;
  }
  printf("mission_test: passed\n");
  return 0;
}
