#include "world.h"

#include "benchmark.h"
#include "fmath.h"
#include "gfx.h"
#include "mem.h"
#include "poly.h"
#include "render.h"
#include "roll.h"
#include "sprites.h"
#include "vec.h"

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

#pragma bss(bss2)
mat3_t world_cam;

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

static inline int16_t _down_shift(uint32_t val) { return (int16_t)(val >> 9); }

// __noinline keeps this in one copy instead of being expanded into both
// call sites in _world_init_start_dx_dy. Verified byte-identical codegen
// with and without optimize(3), so no pragma region is needed here.
static __noinline void _split_vec(vec3_t *v, vec3_t d9[9]) {
  // This version needs half vectors -> more additions and subtractions and
  // more code. However, this means that dots and objects are closer to the
  // center of the grid cell which means fewer grid cells are needed.
  // vec3_t is three contiguous int16_t, so loop over the components instead
  // of expanding the split inline three times (saves code size; only called
  // twice per frame).
  int16_t *vp = (int16_t *)v;
  for (uint8_t i = 0; i < 3; ++i) {
    int16_t val = vp[i];
    int16_t hlf = val >> 1;
    int16_t dbl = val << 1;
    ((int16_t *)&d9[4])[i] = 0;
    ((int16_t *)&d9[6])[i] = val;
    ((int16_t *)&d9[2])[i] = -val;
    ((int16_t *)&d9[5])[i] = hlf;
    ((int16_t *)&d9[3])[i] = -hlf;
    ((int16_t *)&d9[1])[i] = -val - hlf;
    ((int16_t *)&d9[7])[i] = val + hlf;
    ((int16_t *)&d9[8])[i] = dbl;
    ((int16_t *)&d9[0])[i] = -dbl;
  }
  *v = make_vector(d9[8].x << 1, d9[8].y << 1, d9[8].z << 1);
}

static inline void _draw_box_points(uint8_t start_idx, uint8_t num_points,
                                    WorldMapType map_type) {
  uint8_t idx = start_idx & 0x0F;
  uint8_t color = KWorldDotColors[map_type];
  for (uint8_t i = num_points;;) {
    vec_v = _world_vec_v;
    vec_v.x += _mitch_x[idx];
    vec_v.y += _mitch_y[idx];
    vec_v.z += _mitch_z[idx];
    gfx_project_and_draw(color);
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
  _world_grid_radius = _get_msb(model_eye_z >> 9) >> 1;
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

  uint16_t mx = _down_shift(model_eye_x);
  uint16_t my = _down_shift(model_eye_y);
  uint8_t grid_x = (uint8_t)((mx + 512) >> 10);
  uint8_t grid_y = (uint8_t)((my + 512) >> 10);
  int16_t offset_x = (int16_t)(mx - ((uint16_t)grid_x << 10));
  int16_t offset_y = (int16_t)(my - ((uint16_t)grid_y << 10));
  p_start_world.x -= offset_x;
  p_start_world.y -= offset_y;
  p_start_world.z = -_down_shift(model_eye_z);
  // The radius-4 start coords reach ~4600, which used to need a halving
  // workaround around the old quarter-square multiply (it overflowed once
  // |a| + |b| exceeded 4095). The assembly vec_fastmul8p8 is exact for all
  // int16 inputs, so the transform can run at full precision.
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

  bm_model_end(910, "GRD:");

#ifdef __DEBUG_VIEW__
  if (mem_debug_enabled) {
    print_labeled_bcd(770, "GRD:", _world_grid_radius, 3);
  }
#endif
}

void world_update_roll_state() {
  bool updated = false;
  // Vector pointing to the distance.
  vec3_t v = {world_cam.front.x, world_cam.front.y, 0};
  if (v.x != 0 || v.y != 0) {
    // Furthest possible point on the horizon.
    // With <<7 we could already overflow int16_t.
    v.x <<= 6;
    v.y <<= 6;
    vec_transform_inv(&world_cam, &v, &vec_v);
    if (vec_project()) {
      render_cx_pixels = (int16_t)kScreenWidthPixels / 2 - vec_sx;
      render_cy_pixels = (int16_t)kViewportEndYPixels / 2 - vec_sy;
      roll_angle = _get_roll_angle(world_cam.up.z, world_cam.left.z);
      updated = true;
    }
  }
  if (!updated) {
    if (world_cam.front.z > 0) {
      render_cx_pixels = kScreenWidthPixels / 2;
      render_cy_pixels = kViewportEndYPixels + 100;
      roll_angle = 0;
    } else {
      render_cx_pixels = kScreenWidthPixels / 2;
      render_cy_pixels = kViewportEndYPixels;
      roll_angle = kRollMax / 2;
    }
  }
  roll_update_state();
}

static const vec3_t kSunDirWorld = {0, 256, 64};

void world_update_sun_pos() {
  vec_transform_inv(&world_cam, &kSunDirWorld, &vec_v);
  int16_t sx;
  int16_t sy;
  if (vec_project()) {
    sx = kScreenWidthPixels / 2 - vec_sx;
    sy = kViewportEndYPixels / 2 - vec_sy;
  } else {
    sx = -100;
    sy = 0;
  }
  sprites_set_sun_position(sx, sy);
  bm_end(670, "UPD:");

#ifdef __DEBUG_VIEW__
  if (mem_debug_enabled) {
    print_labeled_signed_bcd(970, "SXP:", sx, 4);
    print_labeled_signed_bcd(980, "SYP:", sy, 4);
  }
#endif
}
