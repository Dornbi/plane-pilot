#include "world.h"

#include "benchmark.h"
#include "fmath.h"
#include "gfx.h"
#include "mem.h"
#include "poly.h"
#include "vec.h"

int32_t world_eye_x;
int32_t world_eye_y;
int32_t world_eye_z;
mat3_t world_cam;

// Not static so that polydemo.cc can access them for testing.
__zeropage uint8_t _world_grid_radius;
__zeropage uint8_t _world_start_cx;
__zeropage uint8_t _world_start_cy;
__zeropage vec3_t _world_p_start;
__zeropage vec3_t _world_dx_vec;
__zeropage vec3_t _world_dy_vec;
__zeropage int8_t _world_step_x;
__zeropage int8_t _world_step_y;
__zeropage vec3_t _world_vec_v;
static vec3_t _world_dx4[9];
static vec3_t _world_dy4[9];
static int16_t _mitch_x[16];
static int16_t _mitch_y[16];
static int16_t _mitch_z[16];

// Mitchell's Best-Candidate algorithm to maximize distance between points
// while maintaining an organic, non-linear distribution.
// https://gemini.google.com/share/c1bafe545f1f
static const uint8_t kMitchellPointsX[16] = {0, 6, 0, 2, 6, 2, 4, 0,
                                             6, 4, 0, 2, 4, 4, 2, 4};
static const uint8_t kMitchellPointsY[16] = {0, 0, 6, 2, 4, 6, 0, 2,
                                             6, 4, 4, 0, 2, 2, 4, 6};

// PERF: optimize(3) -> bytes: negligible cycles: -1000
#pragma optimize(push, 3)
static inline int16_t _down_shift(uint32_t val) { return (int16_t)(val >> 9); }

#define _SPLIT(val, comp, d9)                                                  \
  dbl = val << 1;                                                              \
  hlf = val >> 1;                                                              \
  d9[4].comp = 0;                                                              \
  d9[6].comp = val;                                                            \
  d9[2].comp = -val;                                                           \
  d9[5].comp = hlf;                                                            \
  d9[3].comp = -hlf;                                                           \
  d9[1].comp = -val - hlf;                                                     \
  d9[7].comp = val + hlf;                                                      \
  d9[8].comp = dbl;                                                            \
  d9[0].comp = -dbl;

static void _split_vec(vec3_t *v, vec3_t d9[9]) {
  // This version needs half vectors -> more additions and subtractions and
  // more code. However, this means that dots and objects are closer to the
  // center of the grid cell which means fewer grid cells are needed.
  int16_t hlf;
  int16_t dbl;
  _SPLIT(v->x, x, d9);
  _SPLIT(v->y, y, d9);
  _SPLIT(v->z, z, d9);
  *v = make_vector(d9[8].x << 1, d9[8].y << 1, d9[8].z << 1);
}

#pragma optimize(pop)
static inline void _draw_box_points(uint8_t start_idx, uint8_t num_points,
                                    WorldMapType map_type) {
  uint8_t idx = start_idx & 0x0F;
  for (uint8_t i = num_points;;) {
    vec_v = _world_vec_v;
    vec_v.x += _mitch_x[idx];
    vec_v.y += _mitch_y[idx];
    vec_v.z += _mitch_z[idx];
    gfx_project_and_draw(KWorldDotColors[map_type]);
    if (--i == 0) {
      break;
    }
    idx = (idx + 1) & 0x0F;
  }
}

// Total = 72 = 4 + 2*9 (3*3) + 2*25 (5x5)
static const uint8_t kNumPoints2[] = {8, 4, 2};
// Total = 96 = 4 + 2*9 (3*3) + 25 (5*5) + 49 (7*7)
static const uint8_t kNumPoints3[] = {8, 4, 2, 1};
// Total = 108 = 2 + 25 (5*5) + 81 (9*9)
static const uint8_t kNumPoints4[] = {4, 2, 2, 1, 1};
static const uint8_t *_num_points_per_radius;

void _world_init_start_dx_dy() {
  static const uint16_t kGridSpacing = 0x400;
  static const uint16_t kGridMask = 0x3FF;
  // kMinHeight (  ~32m) = 0x002000  5 --> 2
  // default    ( ~128m) = 0x008000  7 --> 3
  //            ( ~512m) = 0x020000  9 --> 4
  _world_grid_radius = _get_msb(world_eye_z >> 9) >> 1;
  if (_world_grid_radius < 2) {
    _world_grid_radius = 2;
  }
  if (_world_grid_radius > 4) {
    _world_grid_radius = 4;
  }
  if (_world_grid_radius == 2) {
    _num_points_per_radius = kNumPoints2;
  } else if (_world_grid_radius == 3) {
    _num_points_per_radius = kNumPoints3;
  } else if (_world_grid_radius == 4) {
    _num_points_per_radius = kNumPoints4;
  }
  int16_t grid_spacing = kGridSpacing * _world_grid_radius;
  vec3_t p_start_world;

  // Pre-calculate delta vectors for step in X and step in Y
  // The order is such that when _vec_v.x becomes < 0, we can break the loop
  // along both axes.
  _world_dx_vec.x = world_cam.front.x;
  _world_dx_vec.y = world_cam.left.x;
  _world_dx_vec.z = world_cam.up.x;
  _split_vec(&_world_dx_vec, _world_dx4);
  if (world_cam.front.x > 0) {
    vec_negate(&_world_dx_vec);
    p_start_world.x = grid_spacing;
  } else {
    p_start_world.x = -grid_spacing;
  }
  _world_dy_vec.x = world_cam.front.y;
  _world_dy_vec.y = world_cam.left.y;
  _world_dy_vec.z = world_cam.up.y;
  _split_vec(&_world_dy_vec, _world_dy4);
  if (world_cam.front.y > 0) {
    vec_negate(&_world_dy_vec);
    p_start_world.y = grid_spacing;
  } else {
    p_start_world.y = -grid_spacing;
  }

  // Precompute Mitchell combinations
  for (uint8_t i = 0; i < 16; ++i) {
    uint8_t mx = kMitchellPointsX[i];
    uint8_t my = kMitchellPointsY[i];
    _mitch_x[i] = _world_dx4[mx].x + _world_dy4[my].x;
    _mitch_y[i] = _world_dx4[mx].y + _world_dy4[my].y;
    _mitch_z[i] = _world_dx4[mx].z + _world_dy4[my].z;
  }

  uint16_t mx = _down_shift(world_eye_x);
  uint16_t my = _down_shift(world_eye_y);
  p_start_world.x -= (mx & kGridMask);
  p_start_world.y -= (my & kGridMask);
  p_start_world.z = -_down_shift(world_eye_z);
  uint8_t grid_x = mx >> 10;
  uint8_t grid_y = my >> 10;
  vec_transform_inv(&world_cam, &p_start_world, &_world_p_start);

  _world_step_x = 1;
  _world_start_cx = grid_x - _world_grid_radius;
  if (world_cam.front.x > 0) {
    _world_step_x = -1;
    _world_start_cx = grid_x + _world_grid_radius;
  }

  _world_step_y = 1;
  _world_start_cy = grid_y - _world_grid_radius;
  if (world_cam.front.y > 0) {
    _world_step_y = -1;
    _world_start_cy = grid_y + _world_grid_radius;
  }
}

void _world_render_object(WorldMapType object_type) {
  static vec3_t poly_verts[kPolyMaxVertices];
  uint8_t obj_idx = object_type - kWorldMapObjStart;
  uint8_t num_verts = kWorldObjNumVerts[obj_idx];
  for (uint8_t i = 0; i < num_verts; ++i) {
    poly_verts[i] = _world_vec_v;
    vec_add(&poly_verts[i], &_world_dx4[kWorldObjX[obj_idx][i]]);
    vec_add(&poly_verts[i], &_world_dy4[kWorldObjY[obj_idx][i]]);
  }
  poly_draw_3d(poly_verts, num_verts, kWorldObjChars[obj_idx],
               kWorldObjColors[obj_idx]);
}

__noinline void world_render_grid() {
  bm_model_start();
  _world_init_start_dx_dy();

  // If the current vec_v.x falls below this value, we can abort.
  int16_t bailout = -_max16(_abs16(_world_dx4[0].x), _abs16(_world_dy4[0].x));
  uint8_t cx = _world_start_cx;
  for (int8_t x = -_world_grid_radius;;) {
    _world_vec_v = _world_p_start;
    uint8_t abs_x = _abs16(x);
    uint8_t cx2 = cx << 1;
    uint8_t cy = _world_start_cy;
    for (int8_t y = -_world_grid_radius;;) {
      // Note: cx is N and cy is W in this case.
      WorldMapType map_type =
          kWorldMap[cx & kWorldMapHeightMask][cy & kWorldMapWidthMask];
      if (map_type >= kWorldMapObjStart) {
        _world_render_object(map_type);
      } else if (map_type != MAP_NOTHING) {
        uint8_t start_idx = cx2 + cy;
        uint8_t num_points = _num_points_per_radius[_max16(abs_x, _abs16(y))];
        _draw_box_points(start_idx, num_points, map_type);
      }
      if (++y > _world_grid_radius) {
        break;
      }
      cy += _world_step_y;
      //  Step along Y axis
      vec_add(&_world_vec_v, &_world_dy_vec);
      if (_world_vec_v.x < bailout) {
        break;
      }
    }
    if (++x > _world_grid_radius) {
      break;
    }
    cx += _world_step_x;
    // Step along X axis
    vec_add(&_world_p_start, &_world_dx_vec);
    if (_world_p_start.x < bailout) {
      break;
    }
  }

  bm_model_end(910, SCREEN_STR("GRD:"));
#ifdef __DEBUG_MODEL__
  if (mem_debug_enabled) {
    print_labeled_bcd(770, SCREEN_STR("GRD:"), _world_grid_radius, 3);
  }
#endif
}