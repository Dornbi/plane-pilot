#include <string.h>

#include "benchmark.h"
#include "chardefs.h"
#include "cia.h"
#include "fmath.h"
#include "gfx.h"
#include "keys.h"
#include "mem.h"
#include "poly.h"
#include "print.h"
#include "vec.h"
#include "world.h"

static void (*kForceDummyDep)() = world_render_grid;

extern const uint8_t kWorldObjectX[kWorldObjectNumRows];
extern const WorldObjectType kWorldObjectTypes[kWorldObjectNumRows];

// Internal vars from world.cc
extern uint8_t _world_grid_radius;
extern uint8_t _world_start_cx;
extern uint8_t _world_start_cy;
extern vec3_t _world_p_start;
extern vec3_t _world_dx_vec;
extern vec3_t _world_dy_vec;
extern int8_t _world_step_x;
extern int8_t _world_step_y;
extern vec3_t _world_vec_v;

extern void _world_init_start_dx_dy();
extern void _world_render_object(WorldObjectType object_type);

static const vertex_t kPolys[][4] = {{{20, 2}, {24, 6}, {20, 10}, {16, 6}},
                                     {{1, 1}, {38, 1}, {38, 12}, {1, 12}},
                                     {{0, 0}, {39, 0}, {39, 13}, {0, 13}},
                                     {{18, 3}, {22, 3}, {30, 9}, {10, 9}},
                                     {{36, 3}, {44, 3}, {70, 17}, {10, 17}},
                                     {{36, 3}, {44, 3}, {70, 19}, {10, 15}},
                                     {{20, 2}, {35, 8}, {20, 12}, {5, 6}},
                                     {{10, 5}, {12, 5}, {12, 7}, {10, 7}}};

static const uint8_t kPolyCount = sizeof(kPolys) / sizeof(kPolys[0]);

struct viewpoint_t {
  mat3_t cam;
  int32_t eye_x;
  int32_t eye_y;
  int32_t eye_z;
};

static const viewpoint_t vkViewPoints[] = {
    {{{256, -8, -27}, {20, 215, 138}, {18, -140, 215}},
     0x10029C,
     0x088C84,
     0x00B053},
    {{{256, -8, -27}, {20, 215, 138}, {18, -140, 215}},
     0x10429C,
     0x088A84,
     0x00A993}};

static const uint8_t kViewpointCount =
    sizeof(vkViewPoints) / sizeof(vkViewPoints[0]);

static void _clear_screen() {
  memset(mem_screen_ram, kCharSolidGround, kViewportHeight * kScreenWidth);
  memset(mem_screen_ram + kViewportHeight * kScreenWidth, kCharSolid11,
         (kScreenHeight - kViewportHeight) * kScreenWidth);
}

// Stripped down version of world_render_grid().
static void _stripped_world_render_grid(const viewpoint_t *viewpoint) {
  world_cam = viewpoint->cam;
  world_eye_x = viewpoint->eye_x;
  world_eye_y = viewpoint->eye_y;
  world_eye_z = viewpoint->eye_z;

  _world_init_start_dx_dy();
  if (mem_debug_enabled && true) {
    print_labeled_signed_bcd(600, "XX:", _world_dx_vec.x);
    print_labeled_signed_bcd(640, "XY:", _world_dx_vec.y);
    print_labeled_signed_bcd(680, "XZ:", _world_dx_vec.z);
    print_labeled_signed_bcd(720, "YX:", _world_dy_vec.x);
    print_labeled_signed_bcd(760, "YY:", _world_dy_vec.y);
    print_labeled_signed_bcd(800, "YZ:", _world_dy_vec.z);
  }

  uint8_t cx = _world_start_cx;
  for (int8_t x = -_world_grid_radius;;) {
    _world_vec_v = _world_p_start;
    uint8_t abs_x = _abs16(x);
    uint8_t cx2 = cx << 1;
    uint8_t cy = _world_start_cy;
    for (int8_t y = -_world_grid_radius;;) {
      if (kWorldObjectX[cy] == cx) {
        _world_render_object(kWorldObjectTypes[cy]);
      }
      if (++y > _world_grid_radius) {
        break;
      }
      cy += _world_step_y;
      //  Step along Y axis
      vec_add(&_world_vec_v, &_world_dy_vec);
      if (_world_vec_v.x < _world_dy_vec.x) {
        break;
      }
    }
    if (++x > _world_grid_radius) {
      break;
    }
    cx += _world_step_x;
    // Step along X axis
    vec_add(&_world_p_start, &_world_dx_vec);
    if (_world_p_start.x < _world_dx_vec.x) {
      break;
    }
  }
}

int main() {
  cia_init();
  bm_init();

  mem_init();
  mem_switch_buffer();
  _clear_screen();
  mem_switch_buffer();
  _clear_screen();

  mem_init_mccm();
  gfx_init_chars();
  gfx_init_raster_irqs();

  uint8_t mode = 0;
  mem_switch_debug(true);

  while (1) {
    for (uint8_t i = 0; i < 2; ++i) {
      _clear_screen();
      if (mode < kPolyCount) {
        poly_fill(kPolys[mode], 4, kQuadCharGroundSparseStart);
      } else {
        _stripped_world_render_grid(&vkViewPoints[mode - kPolyCount]);
      }
      print_labeled_bcd(990, SCREEN_STR("MODE:"), mode, 2);
      mem_switch_buffer();
    }

    keyb_poll();
    if (key_pressed(KSCAN_M)) {
      if (mode < kPolyCount) {
        mode = kPolyCount;
      } else {
        mode = 0;
      }
    }
    if (key_pressed(KSCAN_SPACE)) {
      ++mode;
      if (mode == kPolyCount) {
        mode = 0;
      } else if (mode >= kPolyCount + kViewpointCount) {
        mode = kPolyCount;
      }
    }
  };

  return 0;
}