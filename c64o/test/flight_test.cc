#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../flight.h"
#include "../fmath.h"
#include "../msg.h"
#include "../vec.h"

// Stubs for msg.cc dependencies when compiling host test
uint8_t *mem_screen_ram = nullptr;
uint8_t *mem_screen_row_ptrs[25];
uint8_t color_buffer_dummy[560];
extern uint8_t *const mem_color_buffer = color_buffer_dummy;
static uint8_t test_screen_row[40];

static void assert_msg_rendered(const char *expected) {
  memset(test_screen_row, ' ', sizeof(test_screen_row));
  msg_render();
  if (expected == nullptr || strlen(expected) == 0) {
    for (int i = 0; i < 40; ++i) {
      assert(test_screen_row[i] == ' ');
    }
  } else {
    uint8_t len = (uint8_t)strlen(expected);
    if (len > 40) len = 40;
    uint8_t col = (40 - len) >> 1;
    assert(memcmp(test_screen_row + col, expected, len) == 0);
  }
}

// Mirrors of the constants inside flight.cc. They are static there, so the
// tests restate them; if one of these drifts the tests below are wrong rather
// than merely failing, so keep them in step.
static const uint16_t kStallSpeedWithoutFlaps = 0x0400;
static const uint16_t kStallSpeedWithFlaps = 0x0340;
// Airspeed at which lift reaches kTrimLift upright at sea level, i.e. where the
// lift deficit and its sink penalty vanish. See flight.md 2.4.
static const int16_t kTrimSpeed = 0x0800;
// Landing envelope.
static const int32_t kGroundZ = 0x2000;
static const int16_t kMaxLandingRoll = 32;
static const int16_t kMinLandingUpZ = 0;
static const int16_t kMinLandingPitch = -32;
static const int16_t kMaxLandingPitch = 64;
static const int16_t kMaxLandingVSpeed = -0x00E0;
static const uint16_t kMaxLandingSpeed = 0x0A00;
static const uint16_t kMaxGroundSpeed = 0x0D00;

// Holds an attitude for `frames` steps at a fixed throttle, ignoring fuel burn,
// and returns with flight_speed / flight_vspeed at the steady state. Used by
// the equilibrium tests below, which are about the trim the model settles into
// rather than about any single frame.
static void _settle(uint8_t throttle, int16_t pitch, int16_t up_z, int frames,
                    uint8_t flap = 0) {
  flight_init();
  flight_eye_z = 0x040000; // Well clear of both the ground and the ceiling
  flight_throttle = throttle;
  flight_fuel = 0x0FFFFFFF;
  flight_flap = flap;
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, up_z);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);
  int16_t held_pitch = flight_cam.front.z;
  int16_t held_up_z = flight_cam.up.z;
  for (int i = 0; i < frames; ++i) {
    // The pilot holds the attitude; only the scalar state is under test.
    flight_cam.front.z = held_pitch;
    flight_cam.up.z = held_up_z;
    flight_advance();
    flight_throttle = throttle;
  }
}

// Compass heading, 0..kHeadingMax-1, using the game's own routine. For
// printing; assertions compare the forward vector directly, which is both
// exact and finer grained than a 48 step compass.
static uint8_t _heading() {
  return _get_heading(flight_cam.front.x, flight_cam.front.y);
}

// True if the forward vector is bit for bit unchanged.
static bool _same_heading(int16_t fx, int16_t fy) {
  return flight_cam.front.x == fx && flight_cam.front.y == fy;
}

// Banks the aircraft the way the pilot does, by applying roll inputs. Use this
// where the point is that the attitude is one the game can actually reach;
// use the `roll` argument of _arm_touchdown where an exact left.z is needed.
// Steps from wings level, one kVecRollRight each:
//    1 -> left.z  31 (just inside kMaxLandingRoll)
//    4 -> left.z 119 (well past it)
//   26 -> up.z -256, left.z -11 (inverted, and inside the bank limit,
//         which is exactly the blind spot trigger 6 exists to cover)
static void _roll_by(int steps) {
  for (int i = 0; i < steps; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    vec_orthonormalize(&flight_cam);
  }
  for (int i = 0; i > steps; --i) {
    flight_input(FLIGHT_INPUT_ROLL_LEFT);
    vec_orthonormalize(&flight_cam);
  }
}

// Sets up a touchdown frame: places the aircraft one step above the ground
// with the given attitude and speed, so that a single flight_advance() crosses
// the ground plane and runs the envelope check. Returns the vertical speed the
// check will see.
//
// The predicted descent has to account for the sink penalty as well as the
// pitch term, otherwise the aircraft is simply parked below the ground plane
// and the clamp - not the descent - is what gets tested.
// `roll` sets an exact bank: it cannot be written straight into left.z, since
// vec_orthonormalize derives left from up x front and would discard it, so it
// is seeded through up = (0, -roll, sqrt(256^2 - roll^2)), which comes back
// out as left.z == roll. `roll_steps` additionally banks with real roll
// inputs, for cases where reaching the attitude the way the pilot does is the
// point - see _roll_by.
static int16_t _arm_touchdown(int16_t pitch, int16_t roll, int16_t speed,
                              uint8_t gear, int roll_steps = 0) {
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = 0x040000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = speed;
  int16_t up_z = (int16_t)sqrt(65536.0 - (double)roll * roll);
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, -roll, up_z);
  _roll_by(roll_steps);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);

  // One free-flight frame at altitude tells us the vertical speed this state
  // actually produces, including the sink penalty.
  mat3_t attitude = flight_cam;
  int16_t saved_speed = flight_speed;
  flight_advance();
  int16_t vs = flight_vspeed;

  // Now re-arm the same state exactly one frame's descent above the ground.
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = saved_speed;
  flight_cam = attitude;
  flight_eye_z = (int32_t)kGroundZ - vs;
  return vs;
}

// Puts the model genuinely into ground mode. model_on_ground is a static
// inside flight.cc that flight_init() clears, so setting flight_eye_z alone
// leaves the model airborne and every flight_input() takes the airborne
// branch. One advance at ground level is what actually sets the flag.
static void _put_on_ground(int16_t speed) {
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_gear = 1;
  flight_throttle = 0;
  // The contact frame is a touchdown as far as flight_advance is concerned, so
  // it has to pass the landing envelope. Fly it at kTrimSpeed: there the lift
  // deficit is exactly zero, so with wings level and no pitch the vertical
  // speed is zero and no trigger fires, whatever speed the caller actually
  // wants once the aircraft is on the runway.
  flight_speed = kTrimSpeed;
  flight_eye_z = kGroundZ;
  flight_advance(); // Trips the ground contact check -> model_on_ground = true
  assert(!flight_status);
  flight_speed = speed;
}

// 1. Level cruise equilibrium test.
// "Equilibrium" means the vertical speed settles to zero and the airspeed
// settles to a value that is stable frame over frame - not merely that the
// aircraft is still flying.
static void test_level_cruise_equilibrium() {
  printf("Running test_level_cruise_equilibrium...\n");

  _settle(0x14, 0, 256, 400); // Cruise throttle, wings level, zero pitch
  int16_t settled_speed = flight_speed;
  int32_t settled_z = flight_eye_z;

  assert(!flight_status);
  assert(flight_vspeed == 0); // Actually level, not just airborne

  // Airspeed has stopped changing.
  for (int i = 0; i < 50; ++i) {
    flight_cam.front.z = 0;
    flight_advance();
    flight_throttle = 0x14;
  }
  assert(flight_speed == settled_speed);
  assert(flight_eye_z == settled_z); // Altitude held, not drifting
  assert(flight_speed > kTrimSpeed); // Above trim, so the deficit is zero

  printf("  settled speed: %d (0x%04X), vspeed: %d\n", settled_speed,
         settled_speed, flight_vspeed);
  printf("  PASS\n\n");
}

// 1b. Trim speed boundary test.
// Below the trim speed the lift deficit produces sink; at or above it the
// deficit is clamped away and level pitch means genuinely level flight.
static void test_trim_speed_boundary() {
  printf("Running test_trim_speed_boundary...\n");

  // A throttle that settles above trim speed -> no sink at zero pitch.
  _settle(0x18, 0, 256, 400);
  assert(flight_speed > kTrimSpeed);
  assert(flight_vspeed == 0);

  // A throttle that settles below trim speed -> sink at zero pitch.
  _settle(0x0A, 0, 256, 400);
  printf("  low throttle settled speed: %d, vspeed: %d\n", flight_speed,
         flight_vspeed);
  assert(flight_speed < kTrimSpeed);
  assert(flight_vspeed < 0); // Lift deficit sinks the aircraft

  printf("  PASS\n\n");
}

// 2. Power-off stall recovery test
static void test_power_off_stall_recovery() {
  printf("Running test_power_off_stall_recovery...\n");
  flight_init();
  flight_throttle = 0;

  for (int i = 0; i < 150; ++i) {
    flight_advance();
  }

  // Speed should recover above stall speed after nose drop
  assert(flight_speed >= 0x0300);
  assert(!flight_status);
  printf("  PASS\n\n");
}

// 3. No backward flight test
static void test_no_backward_flight() {
  printf("Running test_no_backward_flight...\n");
  flight_init();
  flight_throttle = 0;

  // Straight up, as a genuine orthonormal frame. Assigning front.z alone
  // leaves front.x at 256, so the vector is not unit length and normalizes to
  // a 45 degree climb - which is not the case this test is meant to cover,
  // and which hides the dead spot at the true vertical entirely.
  flight_cam.front = make_vector(0, 0, 256);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(-256, 0, 0);

  for (int i = 0; i < 150; ++i) {
    flight_advance();
    assert(flight_speed >= 0); // Must never be negative
  }

  // The nose must actually fall away from the vertical, not merely change.
  printf("  front.z after 150 frames: %d\n", flight_cam.front.z);
  assert(flight_cam.front.z < 128); // past 30 degrees of nose drop
  printf("  PASS\n\n");
}

// 4. Climb at different throttles test
static void test_climb_at_different_throttles() {
  printf("Running test_climb_at_different_throttles...\n");

  // 100% throttle climb
  flight_init();
  flight_throttle = 0x18; // Max throttle
  flight_cam.front.z = 30;
  int32_t start_z_100 = flight_eye_z;

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int32_t climb_100 = flight_eye_z - start_z_100;

  // 25% throttle climb
  flight_init();
  flight_throttle = 0x06; // ~25% throttle
  flight_cam.front.z = 30;
  int32_t start_z_25 = flight_eye_z;

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int32_t climb_25 = flight_eye_z - start_z_25;

  assert(climb_100 > climb_25);
  printf("  100%% climb: %d, 25%% climb: %d\n", climb_100, climb_25);
  printf("  PASS\n\n");
}

// 5. Banked turns drag and descent test
static void test_banked_turns_drag_and_descent() {
  printf("Running test_banked_turns_drag_and_descent...\n");

  // Straight flight baseline
  flight_init();
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_cam.front.z = 0;
    flight_advance();
  }
  int16_t straight_speed = flight_speed;

  // 40% banked flight
  flight_init();
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_cam.left.z = 150; // ~40% bank
    flight_cam.front.z = 0;
    flight_advance();
  }
  int16_t banked_speed = flight_speed;

  printf("  straight speed: %d, banked speed: %d\n", straight_speed,
         banked_speed);
  assert(banked_speed < straight_speed); // Induced turn drag penalty
  printf("  PASS\n\n");
}

// 6. Inverted flight drag and pitch test.
// Inverted, lift acts downward, so the deficit is kTrimLift + |lift| rather
// than kTrimLift - lift. That has two measurable consequences, and this test
// asserts both against an upright baseline at the same throttle: the implicit
// deficit drag settles the aircraft at a lower speed, and the sink penalty
// means nose-up pitch is needed just to stay level.
static void test_inverted_flight_drag_and_pitch() {
  printf("Running test_inverted_flight_drag_and_pitch...\n");

  _settle(0x14, 0, 256, 400); // Upright baseline
  int16_t upright_speed = flight_speed;
  int16_t upright_vspeed = flight_vspeed;

  _settle(0x14, 0, -256, 400); // Same throttle, inverted
  int16_t inverted_speed = flight_speed;
  int16_t inverted_vspeed = flight_vspeed;

  printf("  upright:  speed=%d vspeed=%d\n", upright_speed, upright_vspeed);
  printf("  inverted: speed=%d vspeed=%d\n", inverted_speed, inverted_vspeed);

  // Deficit drag: the inverted trim speed is strictly lower.
  assert(inverted_speed < upright_speed);
  // Deficit sink: level pitch is not level flight when inverted.
  assert(upright_vspeed == 0);
  assert(inverted_vspeed < 0);

  // Nose-up pitch relative to the horizon is what recovers it. Find the
  // shallowest pitch that stops the descent and check it is a real climb-out
  // angle, not a rounding artefact.
  int16_t level_pitch = -1;
  for (int16_t p = 0; p <= 140; ++p) {
    _settle(0x14, p, -256, 400);
    if (!flight_status && flight_vspeed >= 0) {
      level_pitch = p;
      break;
    }
  }
  printf("  inverted level pitch at throttle 0x14: %d\n", level_pitch);
  assert(level_pitch > 0); // Nose up vs. the horizon, per flight.md 3.2

  printf("  PASS\n\n");
}

// 7. Gear drag penalty test
static void test_gear_drag_penalty() {
  printf("Running test_gear_drag_penalty...\n");

  flight_init();
  flight_gear = 0;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t clean_speed = flight_speed;

  flight_init();
  flight_gear = 1;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t gear_speed = flight_speed;

  assert(gear_speed < clean_speed);
  printf("  clean speed: %d, gear speed: %d\n", clean_speed, gear_speed);
  printf("  PASS\n\n");
}

// 8. Flap drag, lift and stall reduction test.
// Flaps do three separate things (flight.md 4.2) and this asserts each of them
// rather than just that the flag is set: more parasite drag, 50% more lift,
// and a lower stall speed.
static void test_flap_drag_lift_and_stall_reduction() {
  printf("Running test_flap_drag_lift_and_stall_reduction...\n");

  // (a) Drag: at the same throttle, flaps settle the aircraft slower.
  _settle(0x14, 0, 256, 400, 0);
  int16_t clean_speed = flight_speed;
  _settle(0x14, 0, 256, 400, 1);
  int16_t flap_speed = flight_speed;
  printf("  clean speed: %d, flap speed: %d\n", clean_speed, flap_speed);
  assert(flap_speed < clean_speed);

  // (b) Lift: below the trim speed, flaps reduce the deficit, so the sink rate
  // at the same airspeed and attitude is smaller.
  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = 0x0600; // Below kTrimSpeed, so a deficit exists
  flight_throttle = 0;
  flight_advance();
  int16_t clean_sink = flight_vspeed;

  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = 0x0600;
  flight_throttle = 0;
  flight_flap = 1;
  flight_advance();
  int16_t flap_sink = flight_vspeed;

  printf("  sink at 0x0600: clean %d, flaps %d\n", clean_sink, flap_sink);
  assert(clean_sink < 0);
  assert(flap_sink > clean_sink); // Less sink with flaps -> more lift

  // (c) Stall speed: a speed between the two stall constants stalls clean but
  // not with flaps.
  int16_t between =
      (int16_t)((kStallSpeedWithFlaps + kStallSpeedWithoutFlaps) / 2);
  assert(between > kStallSpeedWithFlaps && between < kStallSpeedWithoutFlaps);

  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = between;
  flight_throttle = 0;
  flight_advance();
  assert(flight_cam.front.z < 0); // Clean: stall pitch-down fired

  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = between;
  flight_throttle = 0;
  flight_flap = 1;
  flight_advance();
  assert(flight_cam.front.z == 0); // Flaps: still flying

  printf("  PASS\n\n");
}

// 9. Touchdown flare and crash envelope test
static void test_touchdown_flare_and_crash_envelope() {
  printf("Running test_touchdown_flare_and_crash_envelope...\n");

  // Gear up landing -> crash
  flight_init();
  flight_eye_z = 0x2000; // Ground altitude
  flight_gear = 0;
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_GEAR);

  // Both flares below are flown as genuine descents through the ground plane.
  // That constrains the speed: a nose-up attitude only descends when the lift
  // deficit outweighs the pitch term, so just above stall the steepest
  // descending flare is front.z = 51, and a steeper one needs to be below
  // stall speed.

  // Safe landing flare (front.z = 45, gear down), descending at 1030.
  int16_t vs = _arm_touchdown(45, 0, 1030, 1);
  printf("  flare 45 at 1030 -> vspeed %d\n", vs);
  assert(vs < 0); // Really descending onto the runway
  assert(vs >= kMaxLandingVSpeed);
  flight_advance();
  assert(!flight_status);

  // Excessive landing flare (front.z = 80 > 64) -> crash. Held off until the
  // speed has decayed below stall, which is the only way this attitude can
  // arrive at the ground - the classic "hold it off too long" stall onto the
  // runway. The stall break trims the nose down a little on the way in, so
  // check what the envelope actually saw rather than what was set.
  vs = _arm_touchdown(80, 0, 700, 1);
  printf("  flare 80 at 700 -> vspeed %d\n", vs);
  assert(vs < 0);
  flight_advance();
  printf("  front.z at the check: %d (limit %d)\n", flight_cam.front.z,
         kMaxLandingPitch);
  assert(flight_cam.front.z > kMaxLandingPitch); // Still over the limit
  assert(flight_status == FLIGHT_CRASH_PITCH_HIGH);

  printf("  PASS\n\n");
}

// 10. Takeoff stall speed gate test.
// The gate lives in the on-ground branch of flight_input, so the model has to
// actually be in ground mode for this to test anything - see _put_on_ground.
static void test_takeoff_stall_speed_gate() {
  printf("Running test_takeoff_stall_speed_gate...\n");

  // Below stall speed: pitch up is refused outright.
  _put_on_ground(0x0200);
  int16_t pitch_before = flight_cam.front.z;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z ==
         pitch_before); // Rotation refused, not just clamped
  flight_advance();
  assert(flight_eye_z == kGroundZ); // Still on the runway
  assert(flight_vspeed == 0);

  // Just below the gate: still refused.
  _put_on_ground((int16_t)kStallSpeedWithoutFlaps);
  pitch_before = flight_cam.front.z;
  flight_input(FLIGHT_INPUT_PITCH_UP); // Gate is strictly greater than
  assert(flight_cam.front.z == pitch_before);
  flight_advance();
  assert(flight_eye_z == kGroundZ);

  // Above stall speed: rotates and becomes airborne.
  _put_on_ground(0x0800);
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z > 0); // Rotation accepted
  flight_advance();
  printf("  after rotation: z=%d (ground %d), front.z=%d\n", flight_eye_z,
         (int)kGroundZ, flight_cam.front.z);
  assert(flight_eye_z > kGroundZ); // Airborne
  assert(!flight_status);

  // Flaps lower the gate: a speed between the two stall constants is enough
  // with flaps down and not enough clean.
  int16_t between =
      (int16_t)((kStallSpeedWithFlaps + kStallSpeedWithoutFlaps) / 2);

  _put_on_ground(between);
  pitch_before = flight_cam.front.z;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z == pitch_before); // Clean: refused

  _put_on_ground(between);
  flight_flap = 1;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z > 0); // Flaps: accepted

  printf("  PASS\n\n");
}

// 11. Ground deceleration friction test
static void test_ground_deceleration_friction() {
  printf("Running test_ground_deceleration_friction...\n");

  _put_on_ground(0x0200);
  flight_throttle = 0;

  for (int i = 0; i < 300; ++i) {
    flight_advance();
  }

  printf("  ground decel end speed: %d, status: %d\n", flight_speed,
         flight_status);
  assert(flight_speed == 0); // Came to a full stop
  assert(!flight_status);
  printf("  PASS\n\n");
}

static void test_ground_braking() {
  printf("Running test_ground_braking...\n");

  // On ground: FLIGHT_INPUT_BRAKE reduces speed
  _put_on_ground(0x0200);
  int16_t speed_before = flight_speed;
  flight_input(FLIGHT_INPUT_BRAKE);
  assert(flight_speed < speed_before);
  assert(flight_speed == speed_before - 32);

  // Continue braking until full stop
  while (flight_speed > 0) {
    flight_input(FLIGHT_INPUT_BRAKE);
  }
  assert(flight_speed == 0);
  assert(!flight_status);

  // Airborne: FLIGHT_INPUT_BRAKE does nothing
  flight_init();
  flight_eye_z = 0x040000;
  int16_t air_speed_before = flight_speed;
  flight_input(FLIGHT_INPUT_BRAKE);
  assert(flight_speed == air_speed_before);

  printf("  PASS\n\n");
}

// 12. Zero fuel flameout transition test
static void test_zero_fuel_flameout_transition() {
  printf("Running test_zero_fuel_flameout_transition...\n");

  flight_init();
  flight_fuel = 50; // Low fuel
  flight_throttle = 0x14;

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  assert(flight_fuel == 0);
  assert(flight_throttle == 0);
  printf("  PASS\n\n");
}

// 13. Vertical dive terminal velocity clamping test
static void test_vertical_dive_terminal_velocity_clamping() {
  printf("Running test_vertical_dive_terminal_velocity_clamping...\n");

  flight_init();
  flight_eye_z = 0x100000;   // High altitude
  flight_cam.front.z = -256; // Vertical dive
  flight_throttle = 0x18;

  for (int i = 0; i < 500; ++i) {
    flight_advance();
    assert(flight_speed >= 0);
    assert(flight_speed <= 0x0F00); // Clamped to kMaxSpeed
  }

  printf("  vertical dive speed: %d (0x%X)\n", flight_speed, flight_speed);
  assert(flight_speed >= 3800);
  printf("  PASS\n\n");
}

// 14. Inverted stall and nose recovery test
static void test_inverted_stall_and_nose_recovery() {
  printf("Running test_inverted_stall_and_nose_recovery...\n");

  flight_init();
  flight_eye_z = 0x100000;
  flight_cam.up.z = -256;
  flight_speed = 0x0200; // Below stall speed

  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  // Nose should pitch down toward ground
  assert(flight_cam.front.z < 0);
  printf("  PASS\n\n");
}

// 15. Ground roll takeoff abort test
static void test_ground_roll_takeoff_abort() {
  printf("Running test_ground_roll_takeoff_abort...\n");

  _put_on_ground(0x0300);
  flight_throttle = 0;

  for (int i = 0; i < 450; ++i) {
    flight_advance();
  }

  assert(flight_speed == 0);
  assert(!flight_status);
  printf("  PASS\n\n");
}

// Squared length, so the orthonormality check needs no square root.
static int32_t _vec_length_sqr(const vec3_t *v) {
  int32_t x = v->x, y = v->y, z = v->z;
  return x * x + y * y + z * z;
}

// 16. Matrix orthonormality under continuous roll test
static void test_matrix_orthonormality_under_continuous_roll() {
  printf("Running test_matrix_orthonormality_under_continuous_roll...\n");

  flight_init();

  for (int i = 0; i < 300; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_LEFT);
    flight_advance();
  }

  // Vector lengths should remain near 256. Compared squared, so no sqrt.
  const int32_t kMinLenSqr = 250 * 250, kMaxLenSqr = 262 * 262;
  int32_t front_l2 = _vec_length_sqr(&flight_cam.front);
  int32_t left_l2 = _vec_length_sqr(&flight_cam.left);
  int32_t up_l2 = _vec_length_sqr(&flight_cam.up);

  assert(front_l2 >= kMinLenSqr && front_l2 <= kMaxLenSqr);
  assert(left_l2 >= kMinLenSqr && left_l2 <= kMaxLenSqr);
  assert(up_l2 >= kMinLenSqr && up_l2 <= kMaxLenSqr);

  printf("  length^2: front %d, left %d, up %d (256^2 = %d)\n", front_l2,
         left_l2, up_l2, 256 * 256);
  printf("  PASS\n\n");
}

// 17. Low altitude stall ground impact test
static void test_low_altitude_stall_ground_impact() {
  printf("Running test_low_altitude_stall_ground_impact...\n");

  flight_init();
  flight_eye_z = 0x2100; // Just above ground
  flight_speed = 0x0200; // Stall speed
  flight_gear = 0;       // Gear up

  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  assert(
      flight_status); // Altitude loss during stall should hit ground and crash
  printf("  PASS\n\n");
}

// 18. Abrupt climb throttle cut test
static void test_abrupt_climb_throttle_cut() {
  printf("Running test_abrupt_climb_throttle_cut...\n");

  flight_init();
  flight_eye_z = 0x100000;
  flight_cam.front.z = 180; // +45 deg climb
  flight_throttle = 0;      // Cut throttle

  for (int i = 0; i < 150; ++i) {
    flight_advance();
    assert(flight_speed >= 0); // No backward flight
  }

  assert(flight_cam.front.z < 180); // Nose pitches down
  printf("  PASS\n\n");
}

// 19. Touchdown exact boundary limits test.
//
// The nose-up pair is set up by parking the aircraft just below the ground
// plane so the clamp puts it exactly on it and the envelope predicate runs.
// That is deliberate here: a front.z = 64 attitude cannot produce a descent at
// any speed above stall (the steepest descending flare just above stall is
// 51), and below stall the stall break trims the nose, which would move the
// very value this test is pinning. Evaluating the predicate directly is the
// only way to isolate the exact boundary. The reachable version of this crash
// - held off until it stalls onto the runway - is covered as a real descent in
// test_touchdown_flare_and_crash_envelope.
static void test_touchdown_exact_boundary_limits() {
  printf("Running test_touchdown_exact_boundary_limits...\n");

  // Pitch = 64 (+15 deg flare) -> PASS
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 64;
  flight_speed = 0x0500;
  int16_t vs = vec_fastmul8p8(64, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(!flight_status);

  // Pitch = 65 -> CRASH
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 65;
  flight_speed = 0x0500;
  vs = vec_fastmul8p8(65, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_status);

  // Nose-down boundary, flown at kTrimSpeed rather than 0x0500. At 0x0500 a
  // front.z = -16 arrival sinks at -234, past kMaxLandingVSpeed, so this pair
  // would be testing trigger 2 instead of the pitch boundary it is named for.
  // At kTrimSpeed the lift deficit is zero, so vertical speed is just the
  // pitch term and stays well inside the sink limit.

  // Pitch = -27 (-6.0 deg pitch down) -> PASS
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = -27;
  flight_speed = kTrimSpeed;
  vs = vec_fastmul8p8(-27, kTrimSpeed);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_vspeed >= kMaxLandingVSpeed); // Sink is not the binding check
  assert(!flight_status);

  // Pitch = -33 -> CRASH
  // Flown with flaps down. _landing_fault() checks sink rate before pitch, and
  // a clean-wing arrival this far nose-down sinks past kMaxLandingVSpeed at
  // every speed - the pitch term alone is -33 * speed / 256 - so sink would
  // always be the binding check and the pitch bound would never be reached.
  // The extra lift from the flaps holds the sink inside the limit; the
  // PITCH_LOW verdict below is what proves it, since a sink violation would
  // have been reported first.
  flight_init();
  flight_gear = 1;
  flight_flap = 1;
  flight_cam.front.z = -33;
  flight_speed = 0x0680;
  vs = vec_fastmul8p8(-33, 0x0680);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_PITCH_LOW);

  printf("  PASS\n\n");
}

// 20. Pause / unpause state freeze test
static void test_pause_unpause_state_freeze() {
  printf("Running test_pause_unpause_state_freeze...\n");

  flight_init();
  int32_t orig_x = flight_eye_x;
  int32_t orig_z = flight_eye_z;
  int16_t orig_speed = flight_speed;

  flight_paused = true;
  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  assert(flight_eye_x == orig_x);
  assert(flight_eye_z == orig_z);
  assert(flight_speed == orig_speed);

  flight_paused = false;
  flight_advance();
  assert(flight_eye_x != orig_x); // Resumes motion

  printf("  PASS\n\n");
}

// 21. High altitude thrust and lift decay test
static void test_high_altitude_thrust_lift_decay() {
  printf("Running test_high_altitude_thrust_lift_decay...\n");

  // Sea level baseline
  flight_init();
  flight_eye_z = 0x020000;
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t low_alt_speed = flight_speed;

  // High altitude (above 0x080000)
  flight_init();
  flight_eye_z = 0x0A0000;
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t high_alt_speed = flight_speed;

  assert(high_alt_speed < low_alt_speed); // Thrust & lift decay at altitude
  printf("  low alt speed: %d, high alt speed: %d\n", low_alt_speed,
         high_alt_speed);
  printf("  PASS\n\n");
}

// 22. High altitude stall speed increase test
static void test_high_altitude_stall_speed_increase() {
  printf("Running test_high_altitude_stall_speed_increase...\n");

  flight_init();
  flight_eye_z = 0x0C0000; // Very high altitude
  flight_speed = 0x0420;   // Above normal stall speed (0x0400), but below
                           // high-alt stall speed
  flight_throttle = 0;

  flight_advance();

  // Should trigger stall pitch-down because stall speed is higher at altitude
  assert(flight_cam.front.z < 0);
  printf("  PASS\n\n");
}

// 23. Inverted flaps stall speed increase test
static void test_inverted_flaps_stall_speed_increase() {
  printf("Running test_inverted_flaps_stall_speed_increase...\n");

  // Upright with flaps: stall speed is 0x0340
  flight_init();
  flight_cam.up.z = 256;
  flight_flap = 1;
  flight_speed = 0x03B0; // > 0x0340 (no stall)
  flight_throttle = 0;
  flight_advance();
  assert(flight_cam.front.z == 0); // No stall pitch down

  // Inverted with flaps: stall speed increases to 0x0480 (adverse camber)
  flight_init();
  flight_cam.up.z = -256;
  flight_flap = 1;
  flight_speed = 0x0420; // < 0x0480 (triggers stall!)
  flight_throttle = 0;
  flight_advance();
  assert(flight_cam.front.z < 0); // Triggers stall pitch down

  printf("  PASS\n\n");
}

// 24. Idle throttle glide slope speed decay test (Mission 2 fix)
static void test_idle_throttle_glide_slope_speed_decay() {
  printf("Running test_idle_throttle_glide_slope_speed_decay...\n");

  flight_init();
  flight_eye_z = 0x020000;
  flight_speed = 0x0600;    // Initial Mission 2 speed
  flight_throttle = 0;      // Cut throttle to 0%
  flight_cam.front.z = -16; // Gentle glide slope (~ -3.5 deg)

  int16_t start_speed = flight_speed;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  printf("  glide slope 0%% throttle speed: %d -> %d\n", start_speed,
         flight_speed);
  assert(flight_speed < start_speed); // Speed MUST NOT increase on gentle glide
                                      // slope at 0% throttle
  printf("  PASS\n\n");
}

// 25. Rollout after touchdown test.
// A landing arrives with the flare pitch still set, and ground mode only
// clamps front.z to >= 0. Before the vspeed lock this fed a positive vertical
// speed frame after frame and the aircraft climbed back off the runway while
// still reporting on-ground.
static void test_rollout_stays_on_ground() {
  printf("Running test_rollout_stays_on_ground...\n");

  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 45; // Flare pitch, inside the landing envelope
  flight_speed = 0x0500;
  flight_throttle = 0;
  int16_t vs = vec_fastmul8p8(45, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;

  // Landing straight ahead, so the whole rollout must stay on front.y == 0.
  assert(flight_cam.front.y == 0);

  flight_advance();
  assert(!flight_status);
  assert(flight_eye_z == 0x2000);
  assert(flight_vspeed == 0);      // Vertical speed zeroed on touchdown
  assert(flight_cam.front.z == 0); // Nose wheel down, once, at touchdown
  // The one normalize the nose drop costs must not swing the heading.
  assert(flight_cam.front.y == 0);

  // The rollout must stay pinned to the ground plane, nose down, and must not
  // wander off the heading it landed on.
  for (int i = 0; i < 600; ++i) {
    flight_advance();
    assert(flight_eye_z == 0x2000);
    assert(flight_vspeed == 0);
    assert(flight_cam.front.z == 0);
    assert(flight_cam.front.y == 0); // Does not wander off the runway heading
  }

  assert(!flight_status);
  assert(flight_speed == 0); // Wheel friction brings it to a stop
  printf("  rollout end: z=%d speed=%d front.z=%d\n", flight_eye_z,
         flight_speed, flight_cam.front.z);
  printf("  PASS\n\n");
}

// 26. Landing envelope: excess sink rate (crash trigger 2).
//
// Note what this test documents about the envelope. Vertical speed is
// front.z * V / 256 - sink_penalty, and both terms are bounded hard for an
// upright arrival: pitch cannot go below kMinLandingPitch without trigger 4
// firing first, and the sink penalty is bounded by the stall speed floor. A
// sweep of every orthonormal attitude that passes the other four checks gives
// a worst upright sink of -301, inside the -0x0180 limit. So trigger 2 is
// only reachable inverted - where lift acts downward and the deficit doubles -
// and it is reachable there precisely because the bank check looks at left.z,
// which is ~0 after a full 180 degree roll. See flight_review.md C5.
static void test_landing_envelope_sink_rate() {
  printf("Running test_landing_envelope_sink_rate...\n");

  // Nose down but with plenty of speed: sink stays inside the limit.
  int16_t fast = _arm_touchdown(-24, 0, 0x0800, 1);
  printf("  nose down at speed 0x0800 -> vspeed %d (limit %d)\n", fast,
         kMaxLandingVSpeed);
  assert(fast < 0);
  assert(fast >= kMaxLandingVSpeed);
  flight_advance();
  assert(!flight_status);

  // Same attitude, slow: the lift deficit drives the sink past the limit while
  // pitch, roll, speed, gear and up.z are all still legal.
  int16_t slow = _arm_touchdown(-24, 0, 0x0450, 1);
  printf("  nose down at speed 0x0450 -> vspeed %d\n", slow);
  assert(slow < kMaxLandingVSpeed);                     // Trigger 2 armed
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll); // 3 clear
  assert(flight_cam.up.z >= kMinLandingUpZ);            // 6 clear
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);    // 5 clear
  assert(flight_gear);                                  // 1 clear
  flight_advance();
  assert(flight_cam.front.z >= kMinLandingPitch); // 4 clear at the check
  assert(flight_status == FLIGHT_CRASH_VSPEED);

  // Sweep every arrival above stall speed that passes the other five checks.
  // Two properties matter, and flight.md 5.3 states both:
  //   - the limit is reachable at all (otherwise trigger 2 is dead code), and
  //   - a level-or-nose-up flare never trips it.
  // Arrivals below stall speed are excluded because the stall break has
  // already driven the nose past kMinLandingPitch, so trigger 4 owns them.
  bool reachable = false;
  int16_t worst_flared = 0;
  int16_t worst = 0, worst_pitch = 0, worst_speed = 0;
  for (int16_t roll = -kMaxLandingRoll; roll <= kMaxLandingRoll; roll += 4) {
    for (int16_t p = kMinLandingPitch; p <= kMaxLandingPitch; ++p) {
      for (int16_t s = (int16_t)kStallSpeedWithoutFlaps + 6;
           s <= (int16_t)kMaxLandingSpeed; s += 10) {
        _arm_touchdown(p, roll, s, 1);
        int16_t pitch = flight_cam.front.z;
        if (pitch < kMinLandingPitch || pitch > kMaxLandingPitch ||
            _abs16(flight_cam.left.z) > kMaxLandingRoll ||
            flight_cam.up.z < kMinLandingUpZ || flight_vspeed >= 0) {
          continue; // Another trigger owns it, or it is not descending
        }
        if (flight_vspeed < kMaxLandingVSpeed) {
          reachable = true;
        }
        if (flight_vspeed < worst) {
          worst = flight_vspeed;
          worst_pitch = pitch;
          worst_speed = flight_speed;
        }
        if (pitch >= 0 && flight_vspeed < worst_flared) {
          worst_flared = flight_vspeed;
        }
      }
    }
  }
  printf("  above stall: worst sink %d at pitch=%d speed=%d;"
         " worst flared (front.z >= 0) %d; limit %d\n",
         worst, worst_pitch, worst_speed, worst_flared, kMaxLandingVSpeed);
  assert(reachable);                         // The limit is live, not dead code
  assert(worst_flared >= kMaxLandingVSpeed); // A proper flare always survives

  printf("  PASS\n\n");
}

// 27. Landing envelope: excess bank angle (crash trigger 3).
static void test_landing_envelope_bank_angle() {
  printf("Running test_landing_envelope_bank_angle...\n");

  // Exactly at kMaxLandingRoll -> lands.
  _arm_touchdown(0, kMaxLandingRoll, 0x0500, 1);
  printf("  touchdown left.z: %d (limit %d)\n", flight_cam.left.z,
         kMaxLandingRoll);
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll);
  flight_advance();
  assert(!flight_status);

  // One unit past the limit -> crash. The boundary is exact.
  _arm_touchdown(0, kMaxLandingRoll + 1, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_ROLL);

  // A wingtip-down arrival -> crash.
  _arm_touchdown(0, 120, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_status);

  // Symmetric: the other wing down crashes too.
  _arm_touchdown(0, -120, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_status);

  printf("  PASS\n\n");
}

// 28. Landing envelope: excess touchdown speed (crash trigger 5).
static void test_landing_envelope_touchdown_speed() {
  printf("Running test_landing_envelope_touchdown_speed...\n");

  // At the limit -> lands.
  _arm_touchdown(0, 0, (int16_t)kMaxLandingSpeed, 1);
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);
  flight_advance();
  printf("  at limit: speed %d, status %d\n", flight_speed, flight_status);
  assert(!flight_status);

  // Over the limit -> crash. Drag bleeds a little speed during the frame, so
  // arrive with enough margin that the check still sees an overspeed.
  _arm_touchdown(0, 0, (int16_t)kMaxLandingSpeed + 0x0100, 1);
  flight_advance();
  printf("  over limit: speed %d, status %d\n", flight_speed, flight_status);
  assert(flight_status == FLIGHT_CRASH_SPEED);

  printf("  PASS\n\n");
}

// 29. Landing envelope: belly-up arrival (crash trigger 6).
// left.z returns to ~0 after a full 180 degree roll, so the bank check does
// not see an inverted arrival. up.z is the attitude the roll limit is really
// trying to express.
static void test_landing_envelope_inverted() {
  printf("Running test_landing_envelope_inverted...\n");

  // Upright reference at the same pitch and speed: lands.
  _arm_touchdown(0, 0, 0x0500, 1);
  flight_advance();
  assert(!flight_status);

  // Same arrival, belly up: crash, and specifically not because of any of the
  // other five triggers. 26 roll steps is a full roll to inverted, which is
  // also how left.z gets back inside the bank limit.
  _arm_touchdown(0, 0, 0x0500, 1, /*roll_steps=*/26);
  printf(
      "  inverted arrival: up.z=%d left.z=%d front.z=%d vspeed=%d speed=%d\n",
      flight_cam.up.z, flight_cam.left.z, flight_cam.front.z, flight_vspeed,
      flight_speed);
  assert(flight_cam.up.z < kMinLandingUpZ);
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll); // Trigger 3 blind here
  assert(flight_cam.front.z >= kMinLandingPitch);
  assert(flight_cam.front.z <= kMaxLandingPitch);
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);
  assert(flight_gear);
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_INVERTED);

  // A legal nose-up flare must not trip the new check: up.z falls with pitch,
  // so the threshold has to stay at 0 rather than a tight cos(roll) bound.
  _arm_touchdown(kMaxLandingPitch, 0, 0x0500, 1);
  printf("  max flare: front.z=%d up.z=%d\n", flight_cam.front.z,
         flight_cam.up.z);
  assert(flight_cam.up.z < 256); // Pitch really does reduce up.z
  assert(flight_cam.up.z >= kMinLandingUpZ);
  flight_advance();
  assert(!flight_status);

  printf("  PASS\n\n");
}

// 30. Inverted near-vertical stall break test.
// Above kMaxStallPitchZ the break is a body-axis rotation. A body "pitch down"
// moves front.z by -up.z/16, so when inverted it drives the nose further UP.
// The rotation has to be chosen by the sign of up.z for the nose to fall
// toward the ground at every attitude, as flight.md 2.2 requires.
static void test_inverted_high_nose_stall_breaks_downward() {
  printf("Running test_inverted_high_nose_stall_breaks_downward...\n");

  for (int inverted = 0; inverted < 2; ++inverted) {
    flight_init();
    flight_eye_z = 0x0400000;
    flight_throttle = 0;
    flight_fuel = 0;
    flight_speed = 0x0100; // Well below stall -> break fires every frame

    // Nose high, inside the dead spot, with up.z of the requested sign. With
    // front near vertical up is nearly horizontal, so |up.z| is small - but
    // its sign is what selects the rotation. Only front and up need seeding;
    // vec_orthonormalize derives left from up x front.
    flight_cam.front = make_vector(70, 0, 246);
    flight_cam.up =
        inverted ? make_vector(246, 0, -70) : make_vector(-246, 0, 70);
    flight_cam.left = make_vector(0, 256, 0);
    vec_orthonormalize(&flight_cam);

    assert(flight_cam.front.z > 224); // In the dead spot
    assert(inverted ? (flight_cam.up.z < 0) : (flight_cam.up.z > 0));
    int16_t start_z = flight_cam.front.z;

    for (int i = 0; i < 200; ++i) {
      flight_advance();
    }

    printf("  %-8s front.z %d -> %d\n", inverted ? "inverted" : "upright",
           start_z, flight_cam.front.z);
    // The nose must fall away from the vertical, whichever way up we started.
    assert(flight_cam.front.z < start_z);
    assert(flight_cam.front.z < 128); // Past 30 degrees of nose drop
  }

  printf("  PASS\n\n");
}

// Flies a settled glide at the given pitch with the engine out and returns the
// glide ratio (horizontal distance travelled / altitude lost) scaled by 1000.
// Returns 0 if the aircraft could not hold the glide.
static int32_t _glide_ratio_x1000(int16_t pitch) {
  flight_init();
  flight_eye_z = 0x0400000; // High enough for a long glide
  flight_fuel = 0;          // Engine out
  flight_throttle = 0;
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, 256);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);
  int16_t hp = flight_cam.front.z, hu = flight_cam.up.z;

  for (int i = 0; i < 600; ++i) { // Settle
    flight_cam.front.z = hp;
    flight_cam.up.z = hu;
    flight_advance();
  }
  if (flight_status) {
    return 0;
  }
  int32_t x0 = flight_eye_x, y0 = flight_eye_y, z0 = flight_eye_z;
  for (int i = 0; i < 200; ++i) { // Measure
    flight_cam.front.z = hp;
    flight_cam.up.z = hu;
    flight_advance();
  }
  // The glide is flown straight ahead, so the ground track is along x alone
  // and no square root is needed for the horizontal distance.
  assert(flight_eye_y == y0);
  int32_t dh = flight_eye_x - x0;
  int32_t dz = z0 - flight_eye_z;
  if (dz <= 0 || dh <= 0) {
    return 0;
  }
  return (int32_t)(((int64_t)dh * 1000) / dz);
}

// 31. Optimal glide angle test (flight.md 6.2).
static void test_optimal_glide_angle() {
  printf("Running test_optimal_glide_angle...\n");

  int32_t best = 0;
  int16_t best_pitch = 0;
  for (int16_t p = -2; p >= -120; --p) {
    int32_t r = _glide_ratio_x1000(p);
    if (r > best) {
      best = r;
      best_pitch = p;
    }
  }
  printf("  best glide ratio %d.%03d:1 at front.z=%d\n", best / 1000,
         best % 1000, best_pitch);

  // The documented optimum is front.z = -49 (~ -11 deg). Allow a band rather
  // than an exact value: settled glide speed moves in steps, so the peak sits
  // on a shelf.
  assert(best_pitch <= -40 && best_pitch >= -60);
  assert(best > 4500); // Better than 4.5:1

  // And it really is a peak: both a shallower and a steeper glide are worse.
  int32_t shallow = _glide_ratio_x1000(-25);
  int32_t steep = _glide_ratio_x1000(-100);
  printf("  shallow(-25) %d, steep(-100) %d\n", shallow, steep);
  assert(shallow < best);
  assert(steep < best);

  // Flatter than about -8 degrees the aircraft cannot hold glide speed.
  assert(_glide_ratio_x1000(-5) < 1000);

  printf("  PASS\n\n");
}

// Holds a bank for `frames` frames starting from a due-x heading, and leaves
// the resulting forward vector in flight_cam. Turn magnitude is read off
// front.y: the runs below stay inside a quarter turn, where |front.y| grows
// monotonically with the angle, so no inverse trig is needed.
static void _turn_over(int roll_steps, int16_t speed, int frames) {
  flight_init();
  flight_eye_z = 0x040000;
  flight_throttle = 0x14;
  flight_fuel = 0x0FFFFFFF;
  flight_speed = speed;
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, 256);
  _roll_by(roll_steps);
  for (int i = 0; i < frames; ++i) {
    flight_advance();
    flight_throttle = 0x14;
    flight_speed = speed; // Hold the speed so only bank varies
  }
}

// 32. Turn rate test (flight.md 3.1).
// Yaw rate is rot = left.z >> 5 - a function of bank angle only. It is
// explicitly NOT proportional to airspeed; the spec used to claim left.z * V.
static void test_turn_rate_depends_on_bank_not_speed() {
  printf("Running test_turn_rate_depends_on_bank_not_speed...\n");

  // Wings level: no turn at all - the forward vector is untouched.
  _turn_over(0, 1800, 60);
  printf("  level      -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(_same_heading(256, 0));

  // Banked: the aircraft turns, and steeper bank turns faster. Both runs stay
  // inside a quarter turn, so |front.y| orders them.
  _turn_over(4, 1800, 60);
  int16_t medium_y = _abs16(flight_cam.front.y);
  int16_t medium_x = flight_cam.front.x;
  printf("  4 steps    -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(medium_y != 0);
  assert(medium_x > 0); // Still inside the first quarter turn

  _turn_over(10, 1800, 60);
  int16_t steep_y = _abs16(flight_cam.front.y);
  printf("  10 steps   -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(flight_cam.front.x > 0);
  assert(steep_y > medium_y);

  // Same bank at three airspeeds: bit for bit identical forward vector.
  _turn_over(8, 1200, 60);
  int16_t fx = flight_cam.front.x, fy = flight_cam.front.y;
  printf("  8 steps at 1200/1800/2400 -> (%d,%d)", fx, fy);
  assert(fy != 0); // It did turn
  _turn_over(8, 1800, 60);
  printf(" (%d,%d)", flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(fx, fy));
  _turn_over(8, 2400, 60);
  printf(" (%d,%d)\n", flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(fx, fy));

  printf("  PASS\n\n");
}

// 33. Altitude loss in a banked turn (flight.md 3.1).
// Banking tilts the lift vector, so up.z falls and the vertical component of
// lift no longer carries the weight. Without added pitch or throttle the
// aircraft turns and descends.
static void test_banked_turn_loses_altitude() {
  printf("Running test_banked_turn_loses_altitude...\n");

  int32_t dz[3];
  const int roll_steps[3] = {0, 6, 10}; // Level, ~left.z 169, ~left.z 239
  for (int i = 0; i < 3; ++i) {
    flight_init();
    flight_eye_z = 0x040000;
    flight_throttle = 0x18; // Full throttle, so thrust is not the variable
    flight_fuel = 0x0FFFFFFF;
    flight_speed = 0x0900;
    flight_cam.front = make_vector(256, 0, 0);
    flight_cam.left = make_vector(0, 256, 0);
    flight_cam.up = make_vector(0, 0, 256);
    _roll_by(roll_steps[i]);
    int16_t held_roll = flight_cam.left.z;
    int32_t z0 = flight_eye_z;
    for (int f = 0; f < 200; ++f) {
      flight_cam.left.z = held_roll;
      flight_cam.front.z = 0; // No pitch input to compensate
      flight_advance();
      flight_throttle = 0x18;
    }
    dz[i] = flight_eye_z - z0;
    printf("  left.z=%3d -> dz=%8d, speed=%d\n", held_roll, dz[i],
           flight_speed);
  }

  assert(dz[0] == 0);    // Wings level at full throttle: holds altitude
  assert(dz[1] < 0);     // 45 deg bank: descends
  assert(dz[2] < dz[1]); // 70 deg bank: descends faster

  printf("  PASS\n\n");
}

// 34. Ground steering test (flight.md 5.1).
// On the ground the roll inputs are remapped to nose-wheel steering, and the
// wings stay locked level.
static void test_ground_steering() {
  printf("Running test_ground_steering...\n");

  _put_on_ground(0x0300);
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_LEFT);
    flight_advance();
  }
  int16_t left_y = flight_cam.front.y;
  printf("  20x ROLL_LEFT  -> front=(%4d,%4d) heading %2d/%d, left.z=%d\n",
         flight_cam.front.x, left_y, _heading(), kHeadingMax,
         flight_cam.left.z);
  assert(left_y != 0);            // It steered
  assert(flight_cam.left.z == 0); // ...without banking
  assert(flight_eye_z == kGroundZ);
  assert(!flight_status);

  _put_on_ground(0x0300);
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    flight_advance();
  }
  int16_t right_y = flight_cam.front.y;
  printf("  20x ROLL_RIGHT -> front=(%4d,%4d) heading %2d/%d, left.z=%d\n",
         flight_cam.front.x, right_y, _heading(), kHeadingMax,
         flight_cam.left.z);
  assert(right_y != 0);
  assert(flight_cam.left.z == 0);
  assert(!flight_status);

  // Symmetric: same magnitude, opposite sign.
  assert((left_y > 0) != (right_y > 0));
  assert(_abs16(left_y) == _abs16(right_y));

  // Roll input and the dedicated yaw input do exactly the same thing.
  _put_on_ground(0x0300);
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_YAW_LEFT);
    flight_advance();
  }
  assert(flight_cam.front.y == left_y);

  // A steered heading must hold, bit for bit. Rebuilding left/up from front
  // every frame used to ratchet it back toward the nearest axis:
  // vec_normalize truncates when it scales the vector back to length 256, so
  // the dominant component gains a unit first. 29 degrees decayed to 0 in
  // ~300 frames.
  int16_t held_x = flight_cam.front.x, held_y = flight_cam.front.y;
  for (int i = 0; i < 400; ++i) {
    flight_advance(); // Coasting, no further input
  }
  printf("  held 400 frames: (%d,%d) -> (%d,%d)\n", held_x, held_y,
         flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(held_x, held_y));
  assert(flight_cam.left.z == 0); // Still level while holding heading

  // Steering is nose wheel steering, so it needs the wheels turning.
  _put_on_ground(0);
  assert(flight_speed == 0);
  held_x = flight_cam.front.x;
  held_y = flight_cam.front.y;
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_YAW_LEFT);
    flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    flight_advance();
  }
  printf("  stationary: (%d,%d) -> (%d,%d)\n", held_x, held_y,
         flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(held_x, held_y)); // Parked aircraft does not pivot

  // ...and works again as soon as it rolls.
  _put_on_ground(0x0300);
  held_x = flight_cam.front.x;
  held_y = flight_cam.front.y;
  flight_input(FLIGHT_INPUT_YAW_LEFT);
  flight_advance();
  assert(!_same_heading(held_x, held_y));

  printf("  PASS\n\n");
}

// 35. Takeoff roll speed margin test.
// The envelope check runs every frame at ground level, so the touchdown speed
// limit would also police the takeoff roll. kMaxGroundSpeed exists to keep a
// full-throttle ground roll clear of it.
static void test_takeoff_roll_speed_margin() {
  printf("Running test_takeoff_roll_speed_margin...\n");

  _put_on_ground(0x0100);
  flight_throttle = 0x18; // Firewall it
  flight_fuel = 0x0FFFFFFF;
  int16_t top = 0;
  for (int i = 0; i < 2000; ++i) {
    flight_advance();
    flight_throttle = 0x18;
    flight_fuel = 0x0FFFFFFF;
    if (flight_speed > top) {
      top = flight_speed;
    }
    assert(!flight_status); // A takeoff roll must never crash by itself
    assert(flight_eye_z == kGroundZ);
  }
  printf("  full throttle ground top speed: %d; landing limit %d,"
         " ground limit %d\n",
         top, (int)kMaxLandingSpeed, (int)kMaxGroundSpeed);

  // The roll really does run past the touchdown limit - that is the trap this
  // guards - and stays well inside the ground limit.
  assert(top > (int16_t)kMaxLandingSpeed - 400); // Close to it, at least
  assert(top < (int16_t)kMaxGroundSpeed);
  assert((int16_t)kMaxGroundSpeed - top > 800); // Real headroom, not 270

  printf("  PASS\n\n");
}

// Declared in host_vec.cc.
int host_vec_selfcheck();

// 0. The host multiply must match the 6502 assembly, or nothing below this
// line says anything about the real target.
static void test_host_multiply_matches_c64() {
  printf("Running test_host_multiply_matches_c64...\n");
  int mismatches = host_vec_selfcheck();
  if (mismatches) {
    printf("  %d mismatches against the vec_asm.cc contract\n", mismatches);
  }
  assert(mismatches == 0);
  printf("  PASS\n\n");
}

static void test_mission_waypoint_constraints() {
  printf("Running test_mission_waypoint_constraints...\n");

  for (int i = 0; i < kMissionCount; ++i) {
    mission_completed[i] = false;
  }

  // Test 1: Mission 01 Takeoff (WP_MIN_1000FT)
  flight_init_from_mission(0);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Below 1000ft (e.g. 0x01F000)
  flight_speed = 0x60;
  flight_eye_z = 0x01F000;
  flight_advance();
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Reach 1000ft (0x025000)
  flight_vspeed = 0;
  flight_eye_z = 0x025000;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[0]);

  // Test 2: Mission 02 Landing (WP_LANDED)
  flight_init_from_mission(1);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[1]);

  // On ground with gear down and stopped on Runway 1
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Contact frame: sets model_on_ground = true at trim speed
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[1]);

  // Test 3: Multi-waypoint mission 03 (Solo Flight: WP_MIN_2000FT then WP_LANDED)
  flight_init_from_mission(2);
  assert(flight_current_wp == 0);
  assert(flight_nav == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Below 2000ft (0x03F000)
  flight_speed = 0x60;
  flight_eye_z = 0x03F000;
  flight_advance();
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Reach 2000ft (0x041000) - waypoint 1 met
  flight_speed = 0x60;
  flight_eye_z = 0x041000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_nav == 0); // flight_nav does NOT auto-advance now
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[2]);

  // Land on Runway 1 with gear down
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Contact frame: sets model_on_ground = true at trim speed
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[2]);

  // Test 4: Manual NAV toggle with N key
  flight_init(); // Has 2 nav points
  assert(flight_nav == 0);
  flight_input(FLIGHT_INPUT_TOGGLE_NAV);
  assert(flight_nav == 1);
  flight_input(FLIGHT_INPUT_TOGGLE_NAV);
  assert(flight_nav == 0);

  printf("  PASS\n\n");
}

static void test_mission_01_takeoff_completion() {
  printf("Running test_mission_01_takeoff_completion...\n");

  msg_clear();
  mission_completed[0] = false;
  flight_init_from_mission(0);
  msg_show(kMissionTitles[0]);

  assert_msg_rendered(kMissionTitles[0]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Below 1000ft (e.g. 0x01F000)
  flight_speed = 0x60;
  flight_eye_z = 0x01F000;
  flight_advance();
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Reach 1000ft (0x025000)
  flight_vspeed = 0;
  flight_eye_z = 0x025000;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[0]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_mission_02_landing_completion() {
  printf("Running test_mission_02_landing_completion...\n");

  msg_clear();
  mission_completed[1] = false;
  flight_init_from_mission(1);
  msg_show(kMissionTitles[1]);

  assert_msg_rendered(kMissionTitles[1]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[1]);

  // On ground with gear down on Runway 1
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Touchdown frame
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[1]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_mission_03_solo_flight_completion() {
  printf("Running test_mission_03_solo_flight_completion...\n");

  msg_clear();
  mission_completed[2] = false;
  flight_init_from_mission(2);
  msg_show(kMissionTitles[2]);

  assert_msg_rendered(kMissionTitles[2]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[2]);

  // Below 2000ft (0x03F000)
  flight_speed = 0x60;
  flight_eye_z = 0x03F000;
  flight_advance();
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Reach 2000ft (0x050000) - Waypoint 0 met
  flight_vspeed = 0;
  flight_eye_z = 0x050000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[2]);
  assert_msg_rendered("NEXT GOAL REACHED");

  // Waypoint 1: Land on Runway 1 with gear down
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Touchdown frame
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[2]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_intermediate_navpoint_reached_message() {
  printf("Running test_intermediate_navpoint_reached_message...\n");

  msg_clear();
  flight_init_from_mission(5);

  // Fly to Lake 1 to complete intermediate waypoint 0 (navpoint 1)
  flight_eye_x = 0x580000;
  flight_eye_y = 0x108000;
  flight_eye_z = 0x020000;
  flight_advance();

  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);
  assert_msg_rendered("NAVPOINT 1 REACHED");

  printf("  PASS\n\n");
}

static void test_mission_04_find_the_runway_completion() {
  printf("Running test_mission_04_find_the_runway_completion...\n");

  msg_clear();
  mission_completed[3] = false;
  flight_init_from_mission(3);
  msg_show(kMissionTitles[3]);

  assert_msg_rendered(kMissionTitles[3]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[3]);

  // Land on Runway 1 (0x200000, 0x400000) and land safely
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Touchdown frame
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[3]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_mission_05_ferry_flight_completion() {
  printf("Running test_mission_05_ferry_flight_completion...\n");

  msg_clear();
  mission_completed[4] = false;
  flight_init_from_mission(4);
  msg_show(kMissionTitles[4]);

  assert_msg_rendered(kMissionTitles[4]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[4]);

  // Land on Runway 2 (0x600000, 0xBF8000) and land safely
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[4]);

  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_mission_06_area_patrol_completion() {
  printf("Running test_mission_06_area_patrol_completion...\n");

  msg_clear();
  mission_completed[5] = false;
  flight_init_from_mission(5);
  msg_show(kMissionTitles[5]);

  assert_msg_rendered(kMissionTitles[5]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[5]);

  // Waypoint 0: Lake 1 (0x580000, 0x108000)
  flight_eye_x = 0x580000;
  flight_eye_y = 0x108000;
  flight_eye_z = 0x020000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 1: Lake 2 (0x400000, 0xE08000)
  flight_eye_x = 0x400000;
  flight_eye_y = 0xE08000;
  flight_advance();
  assert(flight_current_wp == 2);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 2: Lake 3 (0x100000, 0xD08000)
  flight_eye_x = 0x100000;
  flight_eye_y = 0xD08000;
  flight_advance();
  assert(flight_current_wp == 3);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 3: Land on Runway 2 (0x600000, 0xBF8000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[5]);

  printf("  PASS\n\n");
}

static void test_mission_07_airshow_completion() {
  printf("Running test_mission_07_airshow_completion...\n");

  msg_clear();
  mission_completed[6] = false;
  flight_init_from_mission(6);
  msg_show(kMissionTitles[6]);

  assert_msg_rendered(kMissionTitles[6]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[6]);

  // Waypoint 0: Fly upside down above Runway 2 (0x600000, 0x608000)
  flight_cam.up.z = -256;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = 0x010000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 1: Land on Runway 2 (0x600000, 0x608000)
  flight_cam.up.z = 256;
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[6]);

  printf("  PASS\n\n");
}

static void test_mission_08_aerial_recon_completion() {
  printf("Running test_mission_08_aerial_recon_completion...\n");

  msg_clear();
  mission_completed[7] = false;
  flight_init_from_mission(7);
  msg_show(kMissionTitles[7]);

  assert_msg_rendered(kMissionTitles[7]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 0: City 1 at 3000ft (0x580000, 0x108000, z >= 0x060000)
  flight_eye_x = 0x580000;
  flight_eye_y = 0x108000;
  flight_eye_z = 0x065000;
  flight_advance();
  assert(flight_current_wp == 1);

  // Waypoint 1: City 2 at 3000ft (0x580000, 0x688000, z >= 0x060000)
  flight_eye_x = 0x580000;
  flight_eye_y = 0x688000;
  flight_eye_z = 0x065000;
  flight_advance();
  assert(flight_current_wp == 2);

  // Waypoint 2: City 3 at 3000ft (0x580000, 0x708000, z >= 0x060000)
  flight_eye_x = 0x580000;
  flight_eye_y = 0x708000;
  flight_eye_z = 0x065000;
  flight_advance();
  assert(flight_current_wp == 3);

  // Waypoint 3: Land on Runway 2 (0x600000, 0x608000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[7]);

  printf("  PASS\n\n");
}

static void test_mission_09_crop_duster_completion() {
  printf("Running test_mission_09_crop_duster_completion...\n");

  msg_clear();
  mission_completed[8] = false;
  flight_init_from_mission(8);
  msg_show(kMissionTitles[8]);

  assert_msg_rendered(kMissionTitles[8]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 0: Field 1 below 100ft (0x300000, 0x888000, z <= 0x004000)
  flight_eye_x = 0x300000;
  flight_eye_y = 0x888000;
  flight_eye_z = 0x003000;
  flight_advance();
  assert(flight_current_wp == 1);

  // Waypoint 1: Field 2 below 100ft (0x080000, 0xA08000, z <= 0x004000)
  flight_eye_x = 0x080000;
  flight_eye_y = 0xA08000;
  flight_eye_z = 0x003000;
  flight_advance();
  assert(flight_current_wp == 2);

  // Waypoint 2: Field 3 below 100ft (0x380000, 0xB08000, z <= 0x004000)
  flight_eye_x = 0x380000;
  flight_eye_y = 0xB08000;
  flight_eye_z = 0x003000;
  flight_advance();
  assert(flight_current_wp == 3);

  // Waypoint 3: Land on Runway 2 (0x600000, 0x608000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[8]);

  printf("  PASS\n\n");
}

static void test_mission_10_fuel_challenge_completion() {
  printf("Running test_mission_10_fuel_challenge_completion...\n");

  msg_clear();
  mission_completed[9] = false;
  flight_init_from_mission(9);
  msg_show(kMissionTitles[9]);

  assert_msg_rendered(kMissionTitles[9]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 0: Land on Runway 2 (0x600000, 0x608000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[9]);

  printf("  PASS\n\n");
}

static void test_landing_off_runway_crash() {
  printf("Running test_landing_off_runway_crash...\n");
  flight_init();
  flight_gear = true;
  flight_eye_x = 0x000000; // Not on runway
  flight_eye_y = 0x000000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_NOT_ON_RUNWAY);
  printf("  PASS\n\n");
}

static void test_low_altitude_approach_warnings() {
  printf("Running test_low_altitude_approach_warnings...\n");
  flight_init();
  flight_current_wp = 5;
  flight_gear = false;
  flight_cam.front.z = -10;
  flight_eye_x = 0x200000; // Runway 1
  flight_eye_y = 0x400000;
  flight_eye_z = 0x003000; // <= 0x4000
  msg_clear();
  flight_advance();
  assert_msg_rendered("WARNING: GEAR RETRACTED");

  flight_init();
  flight_current_wp = 5;
  flight_gear = true;
  flight_cam.front.z = -10;
  flight_eye_x = 0x000000; // Off runway
  flight_eye_y = 0x000000;
  flight_eye_z = 0x003000;
  msg_clear();
  flight_advance();
  assert_msg_rendered("WARNING: NOT ON RUNWAY");

  printf("  PASS\n\n");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  mem_screen_row_ptrs[0] = test_screen_row;
  printf("=== FLIGHT MODEL COMPREHENSIVE SUITE (52 TESTS) ===\n\n");
  test_host_multiply_matches_c64();
  test_level_cruise_equilibrium();
  test_trim_speed_boundary();
  test_power_off_stall_recovery();
  test_no_backward_flight();
  test_climb_at_different_throttles();
  test_banked_turns_drag_and_descent();
  test_inverted_flight_drag_and_pitch();
  test_gear_drag_penalty();
  test_flap_drag_lift_and_stall_reduction();
  test_touchdown_flare_and_crash_envelope();
  test_takeoff_stall_speed_gate();
  test_ground_deceleration_friction();
  test_ground_braking();
  test_zero_fuel_flameout_transition();
  test_vertical_dive_terminal_velocity_clamping();
  test_inverted_stall_and_nose_recovery();
  test_ground_roll_takeoff_abort();
  test_matrix_orthonormality_under_continuous_roll();
  test_low_altitude_stall_ground_impact();
  test_abrupt_climb_throttle_cut();
  test_touchdown_exact_boundary_limits();
  test_pause_unpause_state_freeze();
  test_high_altitude_thrust_lift_decay();
  test_high_altitude_stall_speed_increase();
  test_inverted_flaps_stall_speed_increase();
  test_idle_throttle_glide_slope_speed_decay();
  test_rollout_stays_on_ground();
  test_landing_envelope_sink_rate();
  test_landing_envelope_bank_angle();
  test_landing_envelope_touchdown_speed();
  test_landing_envelope_inverted();
  test_inverted_high_nose_stall_breaks_downward();
  test_optimal_glide_angle();
  test_turn_rate_depends_on_bank_not_speed();
  test_banked_turn_loses_altitude();
  test_ground_steering();
  test_takeoff_roll_speed_margin();
  test_mission_waypoint_constraints();
  test_mission_01_takeoff_completion();
  test_mission_02_landing_completion();
  test_mission_03_solo_flight_completion();
  test_intermediate_navpoint_reached_message();
  test_mission_04_find_the_runway_completion();
  test_mission_05_ferry_flight_completion();
  test_mission_06_area_patrol_completion();
  test_mission_07_airshow_completion();
  test_mission_08_aerial_recon_completion();
  test_mission_09_crop_duster_completion();
  test_mission_10_fuel_challenge_completion();
  test_landing_off_runway_crash();
  test_low_altitude_approach_warnings();
  printf("ALL 52 TESTS PASSED SUCCESSFULLY!\n");
  return 0;
}
