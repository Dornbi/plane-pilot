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

const uint8_t kWorldObjectX[kWorldObjectNumRows] = {2, 2, 2, 2};
const WorldObjectType kWorldObjectTypes[kWorldObjectNumRows] = {
    WORLD_OBJECT_NOTHING, WORLD_OBJECT_RUNWAY, WORLD_OBJECT_NOTHING,
    WORLD_OBJECT_NOTHING, WORLD_OBJECT_NOTHING};

// Objects represent grid positions where there is something else
// than the default dots. One object per row is allowed.
enum WorldDotType { DOT_NOTHING = 0, DOT_GROUND = 1, DOT_WATER = 2 };
static const uint8_t kDotStartX[kWorldObjectNumRows] = {1, 1, 1, 1};
static const uint8_t kDotEndX[kWorldObjectNumRows] = {5, 5, 5, 5};
static const WorldDotType kDotTypes[kWorldObjectNumRows] = {
    DOT_NOTHING, DOT_NOTHING, DOT_NOTHING, DOT_WATER, DOT_WATER};

// PERF: optimize(3) -> bytes: negligible cycles: -1000
#pragma optimize(3)
static inline int16_t _down_shift(uint32_t val) { return (int16_t)(val >> 9); }

static void _split_vec(vec3_t *v, vec3_t d4[4]) {
  d4[0] = make_vector(-v->x, -v->y, -v->z);
  d4[1] = make_vector(0, 0, 0);
  d4[2] = make_vector(v->x, v->y, v->z);
  d4[3] = make_vector(v->x << 1, v->y << 1, v->z << 1);
  v->x <<= 2;
  v->y <<= 2;
  v->z <<= 2;
}

__zeropage uint8_t _world_grid_radius;
__zeropage uint8_t _world_start_cx;
__zeropage uint8_t _world_start_cy;
__zeropage vec3_t _world_p_start;
__zeropage vec3_t _world_dx_vec;
__zeropage vec3_t _world_dy_vec;
__zeropage int8_t _world_step_x;
__zeropage int8_t _world_step_y;
__zeropage vec3_t _world_vec_v;
static int16_t _mitch_x[16];
static int16_t _mitch_y[16];
static int16_t _mitch_z[16];
static vec3_t _dx4[4], _dy4[4];

// Mitchell's Best-Candidate algorithm to maximize distance between points
// while maintaining an organic, non-linear distribution.
// https://gemini.google.com/share/c1bafe545f1f
static const uint8_t kMitchellPointsX[16] = {0, 3, 0, 1, 3, 1, 2, 0,
                                             3, 2, 0, 1, 2, 2, 1, 2};
static const uint8_t kMitchellPointsY[16] = {0, 0, 3, 1, 2, 3, 0, 1,
                                             3, 2, 2, 0, 1, 1, 2, 3};

static inline void _draw_box_points(uint8_t start_idx, uint8_t num_points,
                                    bool is_ground) {
  uint8_t idx = start_idx & 0x0F;
  for (uint8_t i = num_points;;) {
    vec_v = _world_vec_v;
    vec_v.x += _mitch_x[idx];
    vec_v.y += _mitch_y[idx];
    vec_v.z += _mitch_z[idx];
    gfx_project_and_draw(is_ground);
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
  _split_vec(&_world_dx_vec, _dx4);
  if (world_cam.front.x > 0) {
    vec_negate(&_world_dx_vec);
    p_start_world.x = grid_spacing;
  } else {
    p_start_world.x = -grid_spacing;
  }
  _world_dy_vec.x = world_cam.front.y;
  _world_dy_vec.y = world_cam.left.y;
  _world_dy_vec.z = world_cam.up.y;
  _split_vec(&_world_dy_vec, _dy4);
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
    _mitch_x[i] = _dx4[mx].x + _dy4[my].x;
    _mitch_y[i] = _dx4[mx].y + _dy4[my].y;
    _mitch_z[i] = _dx4[mx].z + _dy4[my].z;
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

void _world_render_object(WorldObjectType object_type) {
  if (object_type == WORLD_OBJECT_RUNWAY) {
    static vec3_t poly_verts[4];
    poly_verts[0] = _world_vec_v;
    vec_add(&poly_verts[0], &_dx4[0]);
    vec_add(&poly_verts[0], &_dy4[1]);

    poly_verts[1] = _world_vec_v;
    vec_add(&poly_verts[1], &_dx4[3]);
    vec_add(&poly_verts[1], &_dy4[1]);

    poly_verts[2] = _world_vec_v;
    vec_add(&poly_verts[2], &_dx4[3]);
    vec_add(&poly_verts[2], &_dy4[2]);

    poly_verts[3] = _world_vec_v;
    vec_add(&poly_verts[3], &_dx4[0]);
    vec_add(&poly_verts[3], &_dy4[2]);

    poly_draw_3d(poly_verts, 4, kQuadCharStart);
  }
}

void world_render_grid() {
  bm_model_start();
  _world_init_start_dx_dy();

  uint8_t cx = _world_start_cx;
  for (int8_t x = -_world_grid_radius;;) {
    _world_vec_v = _world_p_start;
    uint8_t abs_x = _abs16(x);
    uint8_t cx2 = cx << 1;
    uint8_t cy = _world_start_cy;
    for (int8_t y = -_world_grid_radius;;) {
      WorldDotType dot_type = DOT_GROUND;
      if (cy < kWorldObjectNumRows && cx >= kDotStartX[cy] &&
          cx <= kDotEndX[cy]) {
        dot_type = kDotTypes[cy];
      }
      if (dot_type == DOT_NOTHING) {
        if (kWorldObjectX[cy] == cx) {
          _world_render_object(kWorldObjectTypes[cy]);
        }
      } else {
        uint8_t start_idx = cx2 + cy;
        uint8_t num_points = _num_points_per_radius[_max16(abs_x, _abs16(y))];
        _draw_box_points(start_idx, num_points,
                         /* is_ground= */ dot_type == DOT_GROUND);
      }
      if (++y > _world_grid_radius) {
        break;
      }
      cy += _world_step_y;
      //  Step along Y axis
      vec_add(&_world_vec_v, &_world_dy_vec);
      if (_world_vec_v.x < 0) {
        break;
      }
    }
    if (++x > _world_grid_radius) {
      break;
    }
    cx += _world_step_x;
    // Step along X axis
    vec_add(&_world_p_start, &_world_dx_vec);
    if (_world_p_start.x < 0) {
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