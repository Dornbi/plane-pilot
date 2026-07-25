#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../flight.h"
#include "../vec.h"

static void test_level_cruise() {
  printf("Running test_level_cruise...\n");
  flight_init();

  int32_t start_z = flight_eye_z;
  int32_t start_x = flight_eye_x;

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  printf("  x change: %d -> %d (delta: %d)\n", start_x, flight_eye_x,
         flight_eye_x - start_x);
  printf("  z change: %d -> %d (delta: %d)\n", start_z, flight_eye_z,
         flight_eye_z - start_z);
  printf("  speed: %d, vspeed: %d\n", flight_speed, flight_vspeed);

  // Position x should have advanced forward
  assert(flight_eye_x > start_x);
  // Altitude z should be stable/alive
  assert(!flight_crashed);
  printf("  PASS\n\n");
}

static void test_90_degree_roll() {
  printf("Running test_90_degree_roll...\n");
  flight_init();

  // Set 90 deg roll left: front=(256,0,0), left=(0,0,256), up=(0,-256,0)
  flight_cam.front.x = 256;
  flight_cam.front.y = 0;
  flight_cam.front.z = 0;
  flight_cam.left.x = 0;
  flight_cam.left.y = 0;
  flight_cam.left.z = 256;
  flight_cam.up.x = 0;
  flight_cam.up.y = -256;
  flight_cam.up.z = 0;

  // Throttle to 0
  flight_throttle = 0;

  int32_t start_z = flight_eye_z;

  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  int32_t delta_z = flight_eye_z - start_z;
  printf("  50 frames at 90 deg roll: start z=%d, end z=%d, delta z=%d\n",
         start_z, flight_eye_z, delta_z);
  printf("  end speed: %d, vspeed: %d\n", flight_speed, flight_vspeed);
  assert(delta_z < -5000);
  assert(flight_vspeed < 0);
  printf("  PASS\n\n");
}

static void test_low_speed_descent() {
  printf("Running test_low_speed_descent...\n");
  flight_init();

  // Throttle off, low speed (~0x0500)
  flight_throttle = 0;
  flight_speed = 0x0500;

  int32_t start_z = flight_eye_z;

  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  int32_t delta_z = flight_eye_z - start_z;
  printf("  50 frames at low speed: start z=%d, end z=%d, delta z=%d\n",
         start_z, flight_eye_z, delta_z);
  printf("  end speed: %d, vspeed: %d\n", flight_speed, flight_vspeed);
  printf("  PASS\n\n");
}

static void test_stall_pitch_down() {
  printf("Running test_stall_pitch_down...\n");
  flight_init();

  // Set speed below stall speed
  flight_speed = 0x0300; // < 0x0400 stall threshold

  flight_advance();

  printf("  stall advance: pitch (front.z) = %d\n", flight_cam.front.z);
  assert(flight_cam.front.z < 0);
  printf("  PASS\n\n");
}

int main(int argc, char **argv) {
  printf("=== FLIGHT MODEL HOST TESTS ===\n\n");
  test_level_cruise();
  test_90_degree_roll();
  test_low_speed_descent();
  test_stall_pitch_down();
  printf("ALL HOST TESTS PASSED!\n");
  return 0;
}
