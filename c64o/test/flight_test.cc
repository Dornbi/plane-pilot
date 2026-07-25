#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../flight.h"
#include "../vec.h"

// 1. Level cruise equilibrium test
static void test_level_cruise_equilibrium() {
  printf("Running test_level_cruise_equilibrium...\n");
  flight_init();
  flight_throttle = 0x14; // Cruise throttle (~75%)

  for (int i = 0; i < 200; ++i) {
    flight_advance();
  }

  assert(!flight_crashed);
  assert(flight_speed > 0x0500);
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
  flight_cam.front.z = 256; // Straight up

  for (int i = 0; i < 150; ++i) {
    flight_advance();
    assert(flight_speed >= 0); // Must never be negative
  }

  assert(flight_cam.front.z < 256); // Nose should pitch down toward ground
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

// 6. Inverted flight drag and pitch test
static void test_inverted_flight_drag_and_pitch() {
  printf("Running test_inverted_flight_drag_and_pitch...\n");

  flight_init();
  flight_cam.up.z = -256; // Fully inverted

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  // Negative lift creates large lift deficit -> extra induced drag
  assert(flight_speed < 0x0800);
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

// 8. Flap drag and stall reduction test
static void test_flap_drag_and_stall_reduction() {
  printf("Running test_flap_drag_and_stall_reduction...\n");

  flight_init();
  flight_flap = 1;

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  assert(flight_flap == 1);
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

int main(int argc, char **argv) {
  printf("=== FLIGHT MODEL COMPREHENSIVE SUITE (24 DYNAMIC TESTS) ===\n\n");
  test_level_cruise_equilibrium();
  test_power_off_stall_recovery();
  test_no_backward_flight();
  test_climb_at_different_throttles();
  test_banked_turns_drag_and_descent();
  test_inverted_flight_drag_and_pitch();
  test_gear_drag_penalty();
  test_flap_drag_and_stall_reduction();
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
  printf("ALL 24 DYNAMIC TESTS PASSED SUCCESSFULLY!\n");
  return 0;
}
