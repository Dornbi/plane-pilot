#include <stdint.h>
#include <string.h>

#include "benchmark.h"
#include "cia.h"
#include "keys.h"
#include "mem.h"
#include "print.h"
#include "vec.h"

// Grid parameters
const uint8_t kGridSize = 9;
const uint8_t kGridShift = 8;
const uint16_t kGridSpacing = 1 << kGridShift;

// Camera parameters
const int16_t kCamAltitude = 128;

mat3_t cam;

void init_camera() {
  cam.front = make_vector(256, 0, 0);
  cam.left = make_vector(0, 256, 0);
  cam.up = make_vector(0, 0, 256);
}

void draw_grid() {
  // 1. Initial point P0 = (X_start, Y_start, Z_start) in camera space
  // Base grid is on Z = -CAM_ALTITUDE plane in world space (relative to cam)
  bm_start();
  const vec3_t p_start =
      make_vector(kGridSpacing * 4, kGridSpacing * 4, -kCamAltitude);
  vec3_t p_start_transformed;
  vec_transform_inv(&cam, &p_start, &p_start_transformed);

  // 2. Pre-calculate delta vectors for step in X and step in Y
  // These are the columns of the orientation matrix (negated for backwards
  // step)
  vec3_t dx_vec;
  dx_vec.x = -cam.front.x;
  dx_vec.y = -cam.left.x;
  dx_vec.z = -cam.up.x;

  vec3_t dy_vec;
  dy_vec.x = -cam.front.y;
  dy_vec.y = -cam.left.y;
  dy_vec.z = -cam.up.y;
  bm_end(840, SCREEN_STR("grid0:  "));

  bm_start();
  uint8_t c1 = 0;
  uint8_t c2 = 0;
  for (int x = 0; x < kGridSize; ++x) {
    vec_v = p_start_transformed;
    for (int y = 0; y < kGridSize; ++y) {
      // 3. Project and draw
      if (vec_project()) {
        uint8_t px = 20 - (vec_sx >> 3);
        uint16_t py = 12 - (vec_sy >> 3);
        if (px >= 0 && px < 40 && py >= 0 && py < 25) {
          mem_screen_ram[py * 40 + px] = 0;
        }
        ++c1;
      }

      // Step along Y axis
      vec_v.x += dy_vec.x;
      vec_v.y += dy_vec.y;
      vec_v.z += dy_vec.z;
      ++c2;
    }
    // Step along X axis
    p_start_transformed.x += dx_vec.x;
    p_start_transformed.y += dx_vec.y;
    p_start_transformed.z += dx_vec.z;
  }

  bm_end(880, SCREEN_STR("grid2:  "));
  print_labeled_bcd(920, SCREEN_STR("c1"), (int32_t)c1);
  print_labeled_bcd(960, SCREEN_STR("c2"), (int32_t)c2);
}

void print_vectors() {
  print_labeled_signed_bcd(0, SCREEN_STR("fx"), cam.front.x);
  print_labeled_signed_bcd(10, SCREEN_STR("fy"), cam.front.y);
  print_labeled_signed_bcd(20, SCREEN_STR("fz"), cam.front.z);
  print_labeled_signed_bcd(40, SCREEN_STR("lx"), cam.left.x);
  print_labeled_signed_bcd(50, SCREEN_STR("ly"), cam.left.y);
  print_labeled_signed_bcd(60, SCREEN_STR("lz"), cam.left.z);
  print_labeled_signed_bcd(80, SCREEN_STR("ux"), cam.up.x);
  print_labeled_signed_bcd(90, SCREEN_STR("uy"), cam.up.y);
  print_labeled_signed_bcd(100, SCREEN_STR("uz"), cam.up.z);
}

int main() {
  cia_init();
  init_camera();
  mem_screen_ram = (uint8_t *)0x0400;
  memset(mem_screen_ram, ' ', 1000);
  bm_init();
  mem_debug_enabled = true;

  while (1) {
    print_vectors();
    draw_grid();

    keyb_poll();
    if (key_pressed(KSCAN_A)) {
      vec_transform3(&kVecYawLeft, &cam);
      memset(mem_screen_ram, ' ', 1000);
    }
    if (key_pressed(KSCAN_S)) {
      vec_transform3(&kVecYawRight, &cam);
      memset(mem_screen_ram, ' ', 1000);
    }
    if (key_pressed(KSCAN_I)) {
      vec_transform3(&kVecPitchDown, &cam);
      memset(mem_screen_ram, ' ', 1000);
    }
    if (key_pressed(KSCAN_K)) {
      vec_transform3(&kVecPitchUp, &cam);
      memset(mem_screen_ram, ' ', 1000);
    }
    if (key_pressed(KSCAN_J)) {
      vec_transform3(&kVecRollLeft, &cam);
      memset(mem_screen_ram, ' ', 1000);
    }
    if (key_pressed(KSCAN_L)) {
      vec_transform3(&kVecRollRight, &cam);
      memset(mem_screen_ram, ' ', 1000);
    }
    if (key_pressed(KSCAN_R)) {
      init_camera();
      memset(mem_screen_ram, ' ', 1000);
    }
  }

  return 0;
}
