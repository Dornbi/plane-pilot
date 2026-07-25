#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../flight.h"
#include "../fmath.h"
#include "../vec.h"

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
static const int16_t kMinLandingPitch = -16;
static const int16_t kMaxLandingVSpeed = -0x0180;
static const uint16_t kMaxLandingSpeed = 0x0A00;

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

// 1. Level cruise equilibrium test.
// "Equilibrium" means the vertical speed settles to zero and the airspeed
// settles to a value that is stable frame over frame - not merely that the
// aircraft is still flying.
static void test_level_cruise_equilibrium() {
  printf("Running test_level_cruise_equilibrium...\n");

  _settle(0x14, 0, 256, 400); // Cruise throttle, wings level, zero pitch
  int16_t settled_speed = flight_speed;
  int32_t settled_z = flight_eye_z;

  assert(!flight_crashed);
  assert(flight_vspeed == 0); // Actually level, not just airborne

  // Airspeed has stopped changing.
  for (int i = 0; i < 50; ++i) {
    flight_cam.front.z = 0;
    flight_advance();
    flight_throttle = 0x14;
  }
  assert(flight_speed == settled_speed);
  assert(flight_eye_z == settled_z); // Altitude held, not drifting
  assert(flight_speed > kTrimSpeed);  // Above trim, so the deficit is zero

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
  assert(!flight_crashed);
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
    if (!flight_crashed && flight_vspeed >= 0) {
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
  int16_t between = (int16_t)((kStallSpeedWithFlaps + kStallSpeedWithoutFlaps) / 2);
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
  assert(flight_crashed);

  // Safe landing flare (front.z = 45, gear down)
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 45;
  flight_speed = 0x0500;
  int16_t vs = vec_fastmul8p8(45, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(!flight_crashed);

  // Excessive landing flare (front.z = 80 > 64) -> crash
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 80;
  flight_speed = 0x0500;
  vs = vec_fastmul8p8(80, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_crashed);

  printf("  PASS\n\n");
}

// 10. Takeoff stall speed gate test
static void test_takeoff_stall_speed_gate() {
  printf("Running test_takeoff_stall_speed_gate...\n");

  flight_init();
  flight_eye_z = 0x2000; // On ground
  flight_gear = 1;
  flight_speed = 0x0200; // Below stall speed

  flight_input(FLIGHT_INPUT_PITCH_UP);
  flight_advance();
  // Should remain on ground
  assert(flight_eye_z == 0x2000);

  // Above stall speed
  flight_speed = 0x0800;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  flight_input(FLIGHT_INPUT_PITCH_UP);
  flight_advance();
  assert(flight_eye_z > 0x2000); // Airborne
  assert(!flight_crashed);

  printf("  PASS\n\n");
}

// 11. Ground deceleration friction test
static void test_ground_deceleration_friction() {
  printf("Running test_ground_deceleration_friction...\n");

  flight_init();
  flight_eye_z = 0x2000; // On ground
  flight_gear = 1;
  flight_speed = 0x0200;
  flight_throttle = 0;

  for (int i = 0; i < 300; ++i) {
    flight_advance();
  }

  printf("  ground decel end speed: %d, crashed: %d\n", flight_speed, flight_crashed);
  assert(flight_speed == 0); // Came to a full stop
  assert(!flight_crashed);
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
  flight_eye_z = 0x100000; // High altitude
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

  flight_init();
  flight_eye_z = 0x2000; // On ground
  flight_gear = 1;
  flight_speed = 0x0300;
  flight_throttle = 0;

  for (int i = 0; i < 450; ++i) {
    flight_advance();
  }

  assert(flight_speed == 0);
  assert(!flight_crashed);
  printf("  PASS\n\n");
}

static int32_t _vec_length(const vec3_t *v) {
  int32_t x = v->x, y = v->y, z = v->z;
  return sqrt(x * x + y * y + z * z);
}

// 16. Matrix orthonormality under continuous roll test
static void test_matrix_orthonormality_under_continuous_roll() {
  printf("Running test_matrix_orthonormality_under_continuous_roll...\n");

  flight_init();

  for (int i = 0; i < 300; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_LEFT);
    flight_advance();
  }

  // Vector lengths should remain near 256
  int32_t front_len = _vec_length(&flight_cam.front);
  int32_t left_len = _vec_length(&flight_cam.left);
  int32_t up_len = _vec_length(&flight_cam.up);

  assert(front_len >= 250 && front_len <= 262);
  assert(left_len >= 250 && left_len <= 262);
  assert(up_len >= 250 && up_len <= 262);

  printf("  front_len: %d, left_len: %d, up_len: %d\n", front_len, left_len,
         up_len);
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

  assert(flight_crashed); // Altitude loss during stall should hit ground and crash
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

// 19. Touchdown exact boundary limits test
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
  assert(!flight_crashed);

  // Pitch = 65 -> CRASH
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 65;
  flight_speed = 0x0500;
  vs = vec_fastmul8p8(65, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_crashed);

  // Pitch = -16 (-3.5 deg pitch down) -> PASS
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = -16;
  flight_speed = 0x0500;
  vs = vec_fastmul8p8(-16, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(!flight_crashed);

  // Pitch = -17 -> CRASH
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = -17;
  flight_speed = 0x0500;
  vs = vec_fastmul8p8(-17, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_crashed);

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
  flight_speed = 0x0420;   // Above normal stall speed (0x0400), but below high-alt stall speed
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
  flight_speed = 0x0600;  // Initial Mission 2 speed
  flight_throttle = 0;    // Cut throttle to 0%
  flight_cam.front.z = -16; // Gentle glide slope (~ -3.5 deg)

  int16_t start_speed = flight_speed;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  printf("  glide slope 0%% throttle speed: %d -> %d\n", start_speed,
         flight_speed);
  assert(flight_speed < start_speed); // Speed MUST NOT increase on gentle glide slope at 0% throttle
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

  flight_advance();
  assert(!flight_crashed);
  assert(flight_eye_z == 0x2000);
  assert(flight_vspeed == 0); // Vertical speed zeroed on touchdown

  // The rollout must stay pinned to the ground plane.
  for (int i = 0; i < 600; ++i) {
    flight_advance();
    assert(flight_eye_z == 0x2000);
    assert(flight_vspeed == 0);
  }

  assert(!flight_crashed);
  assert(flight_speed == 0); // Wheel friction brings it to a stop
  printf("  rollout end: z=%d speed=%d front.z=%d\n", flight_eye_z,
         flight_speed, flight_cam.front.z);
  printf("  PASS\n\n");
}

// Sets up a touchdown frame: places the aircraft one step above the ground
// with the given attitude and speed, so that a single flight_advance() crosses
// the ground plane and runs the envelope check. Returns the vertical speed the
// check will see.
//
// The predicted descent has to account for the sink penalty as well as the
// pitch term, otherwise the aircraft is simply parked below the ground plane
// and the clamp - not the descent - is what gets tested.
static int16_t _arm_touchdown(int16_t pitch, int16_t roll, int16_t speed,
                              uint8_t gear, uint8_t inverted = 0) {
  flight_init();
  flight_eye_z = 0x040000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = speed;
  // Bank has to be seeded through `up`, not `left`: vec_orthonormalize derives
  // left from up x front, so anything written straight into left.z is
  // discarded. With front along +x, up = (0, -roll, sqrt(256^2 - roll^2))
  // comes back out as left.z == roll.
  int16_t up_z = (int16_t)sqrt(65536.0 - (double)roll * roll);
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, -roll, inverted ? -up_z : up_z);
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
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = saved_speed;
  flight_cam = attitude;
  flight_eye_z = (int32_t)kGroundZ - vs;
  return vs;
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

  // Upright, at the steepest pitch the envelope allows: inside the limit.
  int16_t gentle = _arm_touchdown(kMinLandingPitch, 0, 0x0500, 1);
  printf("  upright at min pitch -> vspeed %d (limit %d)\n", gentle,
         kMaxLandingVSpeed);
  assert(gentle < 0);
  assert(gentle >= kMaxLandingVSpeed);
  flight_advance();
  assert(!flight_crashed);

  // Inverted arrival: the doubled lift deficit drives the sink rate past the
  // limit while pitch, roll, speed and gear are all still legal.
  int16_t steep = _arm_touchdown(kMinLandingPitch, 0, 0x0900, 1, /*inverted=*/1);
  printf("  inverted at min pitch -> vspeed %d, left.z %d, speed %d\n", steep,
         flight_cam.left.z, flight_speed);
  assert(steep < kMaxLandingVSpeed);                       // Trigger 2 armed
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll);    // Trigger 3 clear
  assert(flight_cam.front.z >= kMinLandingPitch);          // Trigger 4 clear
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);       // Trigger 5 clear
  flight_advance();
  assert(flight_crashed);

  printf("  PASS\n\n");
}

// 27. Landing envelope: excess bank angle (crash trigger 3).
static void test_landing_envelope_bank_angle() {
  printf("Running test_landing_envelope_bank_angle...\n");

  // Wings within kMaxLandingRoll -> lands.
  _arm_touchdown(0, kMaxLandingRoll, 0x0500, 1);
  printf("  touchdown left.z: %d (limit %d)\n", flight_cam.left.z,
         kMaxLandingRoll);
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll);
  flight_advance();
  assert(!flight_crashed);

  // A wingtip-down arrival -> crash.
  _arm_touchdown(0, 120, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_crashed);

  // Symmetric: the other wing down crashes too.
  _arm_touchdown(0, -120, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_crashed);

  printf("  PASS\n\n");
}

// 28. Landing envelope: excess touchdown speed (crash trigger 5).
static void test_landing_envelope_touchdown_speed() {
  printf("Running test_landing_envelope_touchdown_speed...\n");

  // At the limit -> lands.
  _arm_touchdown(0, 0, (int16_t)kMaxLandingSpeed, 1);
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);
  flight_advance();
  printf("  at limit: speed %d, crashed %d\n", flight_speed, flight_crashed);
  assert(!flight_crashed);

  // Over the limit -> crash. Drag bleeds a little speed during the frame, so
  // arrive with enough margin that the check still sees an overspeed.
  _arm_touchdown(0, 0, (int16_t)kMaxLandingSpeed + 0x0100, 1);
  flight_advance();
  printf("  over limit: speed %d, crashed %d\n", flight_speed, flight_crashed);
  assert(flight_crashed);

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

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("=== FLIGHT MODEL COMPREHENSIVE SUITE (30 TESTS) ===\n\n");
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
  printf("ALL 30 TESTS PASSED SUCCESSFULLY!\n");
  return 0;
}
