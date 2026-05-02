#include "model.h"

#include <stdint.h>
#include <stdlib.h>

#include "benchmark.h"
#include "fmath.h"
#include "gfx.h"
#include "mem.h"
#include "render.h"
#include "roll.h"
#include "sprites.h"
#include "vec.h"

// Simple mode disables all the 3D logic for debugging.
const bool kSimpleMode = false;

static mat3_t _model_cam;
// Roughly 24.8 fixed point in meters
static int32_t _model_eye_x;
static int32_t _model_eye_y;
static int32_t _model_eye_z;
// 0x0800 =~ 50 m/s
static int16_t _model_speed;
static uint8_t _model_throttle;
static uint32_t _model_fuel;
static bool _model_need_normalize;
static const vec3_t kSunDirWorld = {0, 256, 64};

static const int32_t kMinEyeZ = 0x2000;
static const int32_t kStallSpeed = 0x0400;
static const int32_t kMaxSpeed = 0x0F00;
static const uint8_t kMinThrottle = 0x00;
static const uint8_t kMaxThrottle = 0x18;

static const mat3_t _m_init = {
    {256, 0, 0},
    {0, 256, 0},
    {0, 0, 256},
};

static const mat3_t _m_init_alt = {
    {256, 0, 0},
    {0, 210, -147},
    {0, 147, 210},
};

void model_init() {
  if (kSimpleMode) {
    render_cx_pixels = 129;
    render_cy_pixels = -12;
    roll_angle = 52;
  } else {
    _model_cam = _m_init;
    _model_eye_x = 0;
    _model_eye_y = 0;
    _model_eye_z = 0x8800;
    _model_speed = 0x860;
    _model_throttle = 0x14;
    _model_need_normalize = false;
    model_reset_fuel();
  }
}

void model_init_alt() {
  _model_cam = _m_init_alt;
  _model_eye_x = 0;
  _model_eye_y = 0;
  _model_eye_z = 0x10800;
  _model_speed = 0x860;
  _model_throttle = 0x14;
  _model_need_normalize = false;
  model_reset_fuel();
}

void model_reset_fuel() { _model_fuel = 0x21FFF; }

static const uint8_t kRollAngleLut[65] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,
    4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,
    8,  8,  9,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12,
    12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 15};

static uint8_t _get_roll_angle() {
  int16_t y = _model_cam.left.z;
  int16_t x = _model_cam.up.z;
  uint8_t ratio = _get_ratio(x, y);
  uint8_t angle = kRollAngleLut[ratio];
  if (x >= 0) {
    if (y >= 0) {
      // Q0: 0-15
      return angle;
    } else {
      // Q3: 45-60 (wraps to 0)
      return angle > 0 ? kRollMax - angle : 0;
    }
  } else {
    if (y >= 0) {
      // Q1: 15-30
      return kRollMax / 2 - angle;
    } else {
      // Q2: 30-45
      return kRollMax / 2 + angle;
    }
  }
}

static const uint8_t kHeadingLut[65] = {
    0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  2, 2, 3, 3,
    3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  6, 6, 6, 6,
    6,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  9,  9,  9, 9, 9, 10,
    10, 10, 10, 10, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12};

static uint8_t _get_heading() {
  int16_t y = _model_cam.front.y;
  int16_t x = _model_cam.front.x;
  uint8_t ratio = _get_ratio(x, y);
  uint8_t angle = kHeadingLut[ratio];
  if (x >= 0) {
    if (y < 0) {
      // Q0: 0-12
      return angle;
    } else {
      // Q3: 36-48 (wraps to 0)
      return angle > 0 ? kHeadingMax - angle : 0;
    }
  } else {
    if (y < 0) {
      // Q1: 12-24
      return kHeadingMax / 2 - angle;
    } else {
      // Q2: 24-36
      return kHeadingMax / 2 + angle;
    }
  }
}

static void _update_roll_render_state() {
  bm_model_start();
  bool updated = false;
  // Vector pointing to the distance.
  vec3_t v = {_model_cam.front.x, _model_cam.front.y, 0};
  static vec3_t t;
  if (v.x != 0 || v.y != 0) {
    // Furthest possible point on the horizon.
    // With <<7 we could already overflow int16_t.
    v.x <<= 6;
    v.y <<= 6;
    vec_transform_inv(&_model_cam, &v, &vec_v);
    if (vec_project()) {
      render_cx_pixels = (int16_t)kScreenWidthPixels / 2 - vec_sx;
      render_cy_pixels = (int16_t)kViewportEndYPixels / 2 - vec_sy;
      roll_angle = _get_roll_angle();
      updated = true;
    }
  }
  if (!updated) {
    if (_model_cam.front.z > 0) {
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
  bm_model_end(860, SCREEN_STR("MDL:"));
#ifdef __DEBUG_MODEL__
  if (mem_debug_enabled) {
    print_labeled_signed_bcd(680, SCREEN_STR("FX: "), _model_cam.front.x, 4);
    print_labeled_signed_bcd(690, SCREEN_STR("FY: "), _model_cam.front.y, 4);
    print_labeled_signed_bcd(700, SCREEN_STR("FZ: "), _model_cam.front.z, 4);
    print_labeled_signed_bcd(720, SCREEN_STR("LX: "), _model_cam.left.x, 4);
    print_labeled_signed_bcd(730, SCREEN_STR("LY: "), _model_cam.left.y, 4);
    print_labeled_signed_bcd(740, SCREEN_STR("LZ: "), _model_cam.left.z, 4);
    print_labeled_signed_bcd(760, SCREEN_STR("UX: "), _model_cam.up.x, 4);
    print_labeled_signed_bcd(770, SCREEN_STR("UY: "), _model_cam.up.y, 4);
    print_labeled_signed_bcd(780, SCREEN_STR("UZ: "), _model_cam.up.z, 4);
    print_labeled_bcd(800, SCREEN_STR("EX: "), (_model_eye_x >> 8) & 0xffff);
    print_labeled_bcd(810, SCREEN_STR("EY: "), (_model_eye_y >> 8) & 0xffff);
    print_labeled_bcd(820, SCREEN_STR("EZ: "), (_model_eye_z >> 8) & 0xffff);
  }
#endif
}

static void _update_sun_render_state() {
  vec_transform_inv(&_model_cam, &kSunDirWorld, &vec_v);
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
#ifdef __DEBUG_MODEL__
  if (mem_debug_enabled) {
    print_labeled_signed_bcd(920, SCREEN_STR("SXP:"), sx, 4);
    print_labeled_signed_bcd(930, SCREEN_STR("SYP:"), sy, 4);
  }
#endif
}

static void _model_stall() {
  static mat3_t mat_stall = {{256, 0, 0}, {0, 256, 0}, {0, 0, 256}};
  mat_stall.up = make_vector(0, 0, 256);
  vec_cross(&mat_stall.up, &_model_cam.front, &mat_stall.left);
  vec_normalize(&mat_stall.left);
  vec_cross(&mat_stall.left, &mat_stall.up, &mat_stall.front);
  mat_stall.up.x -= mat_stall.front.x >> 5;
  mat_stall.up.y -= mat_stall.front.y >> 5;
  mat_stall.front.z += 8;
  vec_transform3_inv(&mat_stall, &_model_cam);
  _model_need_normalize = true;
}

void model_update() {
  if (kSimpleMode) {
    roll_update_state();
  } else {
    // Speed: Air resistance, gravity, trhottle
    _model_speed -= vec_fastsqr8p8(_model_speed) >> 10;
    _model_speed -= _model_cam.front.z >> 3;
    _model_speed += _model_throttle;
    if (_model_speed < 0) {
      _model_speed = 0;
    } else if (_model_speed < kStallSpeed) {
      _model_stall();
    } else if (_model_speed > kMaxSpeed) {
      _model_speed = kMaxSpeed;
    }

    // Vspeed
    int16_t vspeed = vec_fastmul8p8(_model_cam.front.z, _model_speed);

    // Motion
    _model_eye_x += vec_fastmul8p8(_model_cam.front.x, _model_speed << 1);
    _model_eye_y += vec_fastmul8p8(_model_cam.front.y, _model_speed << 1);
    _model_eye_z += vspeed;
    if (_model_eye_z < kMinEyeZ) {
      _model_eye_z = kMinEyeZ;
    }

    // Fuel
    uint8_t fuel_consumption = _model_throttle;
    if (_model_fuel > fuel_consumption) {
      _model_fuel -= fuel_consumption;
    } else {
      _model_fuel = 0;
    }

    // Rotation
    int8_t rot = _model_cam.left.z >> 5;
    if (rot != 0) {
      static mat3_t mat3_rot = {{256, 0, 0}, {0, 256, 0}, {0, 0, 256}};
      mat3_rot.front.y = rot;
      mat3_rot.left.x = -rot;
      vec_transform3_inv(&mat3_rot, &_model_cam);
      _model_need_normalize = true;
    }

    if (_model_need_normalize) {
      vec_orthonormalize(&_model_cam);
      _model_need_normalize = false;
    }

    _update_roll_render_state();
    sprites_set_speed(_model_speed >> 6);
    sprites_set_alt(_model_eye_z >> 8);
    sprites_set_vspeed(vspeed);
    sprites_set_roll(roll_angle);
    sprites_set_pitch(_model_cam.front.z >> 2);
    sprites_set_throttle(_model_throttle);
    sprites_set_fuel(_model_fuel);
    uint8_t heading = _get_heading();
    sprites_set_heading_bitmap(heading);
    _update_sun_render_state();
#ifdef __DEBUG_MODEL__
    if (mem_debug_enabled) {
      print_labeled_signed_bcd(960, SCREEN_STR("SPD:"), _model_speed, 4);
      print_labeled_signed_bcd(970, SCREEN_STR("VSP:"), vspeed, 4);
      print_labeled_signed_bcd(980, SCREEN_STR("HDG:"), heading, 4);
    }
#endif
  }
#ifdef __DEBUG_MODEL__
  if (mem_debug_enabled) {
    print_labeled_signed_bcd(850, SCREEN_STR("ROL:"), roll_angle, 4);
    print_labeled_signed_bcd(880, SCREEN_STR("CXP:"), render_cx_pixels, 4);
    print_labeled_signed_bcd(890, SCREEN_STR("CYP:"), render_cy_pixels, 4);
  }
#endif
}

void model_input(enum model_input_t input) {
  if (kSimpleMode) {
    switch (input) {
    case MODEL_INPUT_ROLL_LEFT:
      if (roll_angle > 0) {
        --roll_angle;
      } else {
        roll_angle = kRollMax - 1;
      }
      break;
    case MODEL_INPUT_ROLL_RIGHT:
      if (roll_angle < kRollMax - 1) {
        ++roll_angle;
      } else {
        roll_angle = 0;
      }
      break;
    case MODEL_INPUT_PITCH_UP:
      --render_cy_pixels;
      break;
    case MODEL_INPUT_PITCH_DOWN:
      ++render_cy_pixels;
      break;
    case MODEL_INPUT_YAW_LEFT:
      --render_cx_pixels;
      break;
    case MODEL_INPUT_YAW_RIGHT:
      ++render_cx_pixels;
      break;
    default:
      break;
    }
  } else {
    switch (input) {
    case MODEL_INPUT_ROLL_LEFT:
      vec_transform3(&kVecRollLeft, &_model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_ROLL_RIGHT:
      vec_transform3(&kVecRollRight, &_model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_PITCH_UP:
      vec_transform3(&kVecPitchUp, &_model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_PITCH_DOWN:
      vec_transform3(&kVecPitchDown, &_model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_YAW_LEFT:
      vec_transform3(&kVecYawLeft, &_model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_YAW_RIGHT:
      vec_transform3(&kVecYawRight, &_model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_THROTTLE_UP:
      if (_model_throttle < kMaxThrottle) {
        _model_throttle += 1;
      }
      break;
    case MODEL_INPUT_THROTTLE_DOWN:
      if (_model_throttle > kMinThrottle) {
        _model_throttle -= 1;
      }
      break;
    default:
      break;
    }
  }
}

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
static __zeropage uint8_t _grid_x;
static __zeropage uint8_t _grid_y;
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

static inline void _draw_box_points(uint8_t start_idx, uint8_t num_points) {
  uint8_t idx = start_idx & 0x0F;
  for (uint8_t i = num_points;;) {
    vec_v = _vec_v;
    vec_v.x += _mitch_x[idx];
    vec_v.y += _mitch_y[idx];
    vec_v.z += _mitch_z[idx];
    gfx_project_and_draw();
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
  _grid_radius = _get_msb(_model_eye_z >> 9) >> 1;
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
  _dx_vec.x = _model_cam.front.x;
  _dx_vec.y = _model_cam.left.x;
  _dx_vec.z = _model_cam.up.x;
  _split_vec(&_dx_vec, dx4);
  if (_model_cam.front.x > 0) {
    _dx_vec.x = -_dx_vec.x;
    _dx_vec.y = -_dx_vec.y;
    _dx_vec.z = -_dx_vec.z;
    p_start_world.x = grid_spacing;
  } else {
    p_start_world.x = -grid_spacing;
  }
  _dy_vec.x = _model_cam.front.y;
  _dy_vec.y = _model_cam.left.y;
  _dy_vec.z = _model_cam.up.y;
  _split_vec(&_dy_vec, dy4);
  if (_model_cam.front.y > 0) {
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

  uint16_t mx = _down_shift(_model_eye_x);
  uint16_t my = _down_shift(_model_eye_y);
  p_start_world.x -= (mx & kGridMask);
  p_start_world.y -= (my & kGridMask);
  p_start_world.z = -_down_shift(_model_eye_z);
  _grid_x = mx >> 10;
  _grid_y = my >> 10;
  vec_transform_inv(&_model_cam, &p_start_world, &_p_start);

  _step_x = 1;
  _start_cx = _grid_x - _grid_radius;
  if (_model_cam.front.x > 0) {
    _step_x = -1;
    _start_cx = _grid_x + _grid_radius;
  }

  _step_y = 1;
  _start_cy = _grid_y - _grid_radius;
  if (_model_cam.front.y > 0) {
    _step_y = -1;
    _start_cy = _grid_y + _grid_radius;
  }
}

void model_render_grid() {
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
      uint8_t start_idx = cx2 + cy;
      uint8_t num_points = _num_points_per_radius[_max16(abs_x, _abs16(y))];
      _draw_box_points(start_idx, num_points);
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