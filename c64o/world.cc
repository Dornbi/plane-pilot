#include "world.h"

#include "benchmark.h"
#include "fmath.h"
#include "gfx.h"
#include "mem.h"
#include "model.h"
#include "vec.h"

// Objects represent grid positions where there is something else
// than the default dots. One object per row is allowed.
enum DotType { DOT_NOTHING = 0, DOT_GROUND = 1, DOT_WATER = 2 };

static const uint8_t kObjectNumRows = 4;
static const uint8_t kDotStartX[kObjectNumRows] = {2, 2, 2, 2};
static const uint8_t kDotEndX[kObjectNumRows] = {6, 6, 6, 6};
static const DotType kDotTypes[kObjectNumRows] = {DOT_WATER, DOT_WATER,
                                                  DOT_WATER, DOT_GROUND};
enum ObjectType { OBJECT_NOTHING = 0, OBJECT_RUNWAY = 1 };
static const uint8_t kObjectX[kObjectNumRows] = {2, 2, 2, 2};
static const ObjectType kObjectTypes[kObjectNumRows] = {
    OBJECT_NOTHING, OBJECT_NOTHING, OBJECT_NOTHING, OBJECT_NOTHING};

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

static __zeropage vec3_t _p_start;
static __zeropage uint8_t _grid_radius;
static __zeropage vec3_t _dx_vec;
static __zeropage vec3_t _dy_vec;
static int16_t _mitch_x[16];
static int16_t _mitch_y[16];
static int16_t _mitch_z[16];
static __zeropage vec3_t _vec_v;
static __zeropage int8_t _step_x;
static __zeropage int8_t _step_y;
static __zeropage uint8_t _start_cx;
static __zeropage uint8_t _start_cy;

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
    vec_v = _vec_v;
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

static void _init_start_dx_dy() {
  static const uint16_t kGridSpacing = 0x400;
  static const uint16_t kGridMask = 0x3FF;
  // kMinHeight (  ~32m) = 0x002000  5 --> 2
  // default    ( ~128m) = 0x008000  7 --> 3
  //            ( ~512m) = 0x020000  9 --> 4
  _grid_radius = _get_msb(model_eye_z >> 9) >> 1;
  if (_grid_radius < 2) {
    _grid_radius = 2;
  }
  if (_grid_radius > 4) {
    _grid_radius = 4;
  }
  if (_grid_radius == 2) {
    _num_points_per_radius = kNumPoints2;
  } else if (_grid_radius == 3) {
    _num_points_per_radius = kNumPoints3;
  } else if (_grid_radius == 4) {
    _num_points_per_radius = kNumPoints4;
  }
  int16_t grid_spacing = kGridSpacing * _grid_radius;
  vec3_t p_start_world;

  // Pre-calculate delta vectors for step in X and step in Y
  // The order is such that when _vec_v.x becomes < 0, we can break the loop
  // along both axes.
  vec3_t dx4[4], dy4[4];
  _dx_vec.x = model_cam.front.x;
  _dx_vec.y = model_cam.left.x;
  _dx_vec.z = model_cam.up.x;
  _split_vec(&_dx_vec, dx4);
  if (model_cam.front.x > 0) {
    _dx_vec.x = -_dx_vec.x;
    _dx_vec.y = -_dx_vec.y;
    _dx_vec.z = -_dx_vec.z;
    p_start_world.x = grid_spacing;
  } else {
    p_start_world.x = -grid_spacing;
  }
  _dy_vec.x = model_cam.front.y;
  _dy_vec.y = model_cam.left.y;
  _dy_vec.z = model_cam.up.y;
  _split_vec(&_dy_vec, dy4);
  if (model_cam.front.y > 0) {
    _dy_vec.x = -_dy_vec.x;
    _dy_vec.y = -_dy_vec.y;
    _dy_vec.z = -_dy_vec.z;
    p_start_world.y = grid_spacing;
  } else {
    p_start_world.y = -grid_spacing;
  }

  // Precompute Mitchell combinations
  for (uint8_t i = 0; i < 16; ++i) {
    uint8_t mx = kMitchellPointsX[i];
    uint8_t my = kMitchellPointsY[i];
    _mitch_x[i] = dx4[mx].x + dy4[my].x;
    _mitch_y[i] = dx4[mx].y + dy4[my].y;
    _mitch_z[i] = dx4[mx].z + dy4[my].z;
  }

  uint16_t mx = _down_shift(model_eye_x);
  uint16_t my = _down_shift(model_eye_y);
  p_start_world.x -= (mx & kGridMask);
  p_start_world.y -= (my & kGridMask);
  p_start_world.z = -_down_shift(model_eye_z);
  uint8_t grid_x = mx >> 10;
  uint8_t grid_y = my >> 10;
  vec_transform_inv(&model_cam, &p_start_world, &_p_start);

  _step_x = 1;
  _start_cx = grid_x - _grid_radius;
  if (model_cam.front.x > 0) {
    _step_x = -1;
    _start_cx = grid_x + _grid_radius;
  }

  _step_y = 1;
  _start_cy = grid_y - _grid_radius;
  if (model_cam.front.y > 0) {
    _step_y = -1;
    _start_cy = grid_y + _grid_radius;
  }
}

static inline void _render_object(ObjectType object_type) {}

void world_render_grid() {
  // 1. Initial point P0 = (X_start, Y_start, Z_start) in _model_camera space
  // Base grid is on Z = -_model_cam_ALTITUDE plane in world space (relative
  // to _model_cam)
  bm_model_start();

  _init_start_dx_dy();

  uint8_t cx = _start_cx;
  for (int8_t x = -_grid_radius;;) {
    _vec_v = _p_start;
    uint8_t abs_x = _abs16(x);
    uint8_t cx2 = cx << 1;
    uint8_t cy = _start_cy;
    for (int8_t y = -_grid_radius;;) {
      DotType dot_type = DOT_GROUND;
      if (cy < kObjectNumRows && cx >= kDotStartX[cy] && cx <= kDotEndX[cy]) {
        dot_type = kDotTypes[cy];
      }
      if (dot_type == DOT_NOTHING) {
        if (kObjectX[cy] == cx) {
          _render_object(kObjectTypes[cy]);
        }
      } else {
        uint8_t start_idx = cx2 + cy;
        uint8_t num_points = _num_points_per_radius[_max16(abs_x, _abs16(y))];
        _draw_box_points(start_idx, num_points,
                         /* is_ground= */ dot_type == DOT_GROUND);
      }
      if (++y > _grid_radius) {
        break;
      }
      cy += _step_y;
      //  Step along Y axis
      _vec_v.x += _dy_vec.x;
      _vec_v.y += _dy_vec.y;
      _vec_v.z += _dy_vec.z;
      if (_vec_v.x < 0) {
        break;
      }
    }
    if (++x > _grid_radius) {
      break;
    }
    cx += _step_x;
    // Step along X axis
    _p_start.x += _dx_vec.x;
    _p_start.y += _dx_vec.y;
    _p_start.z += _dx_vec.z;
    if (_p_start.x < 0) {
      break;
    }
  }

  bm_model_end(900, SCREEN_STR("GRD:"));
#ifdef __DEBUG_MODEL__
  if (mem_debug_enabled) {
    print_labeled_signed_bcd(840, SCREEN_STR("GRD:"), _grid_radius, 4);
  }
#endif
}