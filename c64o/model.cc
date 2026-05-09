#include "model.h"

#include <stdint.h>
#include <stdlib.h>

#include "benchmark.h"
#include "fmath.h"
#include "mem.h"
#include "render.h"
#include "roll.h"
#include "sprites.h"
#include "vec.h"

// Simple mode disables all the 3D logic for debugging.
const bool kSimpleMode = false;

mat3_t model_cam;
int32_t model_eye_x;
int32_t model_eye_y;
int32_t model_eye_z;

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
    model_cam = _m_init;
    model_eye_x = 0;
    model_eye_y = 0;
    model_eye_z = 0x8800;
    _model_speed = 0x860;
    _model_throttle = 0x14;
    _model_need_normalize = false;
    model_reset_fuel();
  }
}

void model_init_alt() {
  model_cam = _m_init_alt;
  model_eye_x = 0;
  model_eye_y = 0;
  model_eye_z = 0x10800;
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
  int16_t y = model_cam.left.z;
  int16_t x = model_cam.up.z;
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
  int16_t y = model_cam.front.y;
  int16_t x = model_cam.front.x;
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
  vec3_t v = {model_cam.front.x, model_cam.front.y, 0};
  static vec3_t t;
  if (v.x != 0 || v.y != 0) {
    // Furthest possible point on the horizon.
    // With <<7 we could already overflow int16_t.
    v.x <<= 6;
    v.y <<= 6;
    vec_transform_inv(&model_cam, &v, &vec_v);
    if (vec_project()) {
      render_cx_pixels = (int16_t)kScreenWidthPixels / 2 - vec_sx;
      render_cy_pixels = (int16_t)kViewportEndYPixels / 2 - vec_sy;
      roll_angle = _get_roll_angle();
      updated = true;
    }
  }
  if (!updated) {
    if (model_cam.front.z > 0) {
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
    print_labeled_signed_bcd(680, SCREEN_STR("FX: "), model_cam.front.x, 4);
    print_labeled_signed_bcd(690, SCREEN_STR("FY: "), model_cam.front.y, 4);
    print_labeled_signed_bcd(700, SCREEN_STR("FZ: "), model_cam.front.z, 4);
    print_labeled_signed_bcd(720, SCREEN_STR("LX: "), model_cam.left.x, 4);
    print_labeled_signed_bcd(730, SCREEN_STR("LY: "), model_cam.left.y, 4);
    print_labeled_signed_bcd(740, SCREEN_STR("LZ: "), model_cam.left.z, 4);
    print_labeled_signed_bcd(760, SCREEN_STR("UX: "), model_cam.up.x, 4);
    print_labeled_signed_bcd(770, SCREEN_STR("UY: "), model_cam.up.y, 4);
    print_labeled_signed_bcd(780, SCREEN_STR("UZ: "), model_cam.up.z, 4);
    print_labeled_bcd(800, SCREEN_STR("EX: "), (model_eye_x >> 8) & 0xffff);
    print_labeled_bcd(810, SCREEN_STR("EY: "), (model_eye_y >> 8) & 0xffff);
    print_labeled_bcd(820, SCREEN_STR("EZ: "), (model_eye_z >> 8) & 0xffff);
  }
#endif
}

static void _update_sun_render_state() {
  vec_transform_inv(&model_cam, &kSunDirWorld, &vec_v);
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
  static mat3_t mat_stall;
  mat_stall.up = make_vector(0, 0, 256);
  vec_cross(&mat_stall.up, &model_cam.front, &mat_stall.left);
  vec_normalize(&mat_stall.left);
  vec_cross(&mat_stall.left, &mat_stall.up, &mat_stall.front);
  mat_stall.up.x -= mat_stall.front.x >> 5;
  mat_stall.up.y -= mat_stall.front.y >> 5;
  mat_stall.front.z += 8;
  vec_transform3_inv(&mat_stall, &model_cam);
  _model_need_normalize = true;
}

void model_update() {
  if (kSimpleMode) {
    roll_update_state();
  } else {
    // Speed: Air resistance, gravity, trhottle
    _model_speed -= vec_fastsqr8p8(_model_speed) >> 10;
    _model_speed -= model_cam.front.z >> 3;
    _model_speed += _model_throttle;
    if (_model_speed < 0) {
      _model_speed = 0;
    } else if (_model_speed < kStallSpeed) {
      _model_stall();
    } else if (_model_speed > kMaxSpeed) {
      _model_speed = kMaxSpeed;
    }

    // Vspeed
    int16_t vspeed = vec_fastmul8p8(model_cam.front.z, _model_speed);

    // Motion
    model_eye_x += vec_fastmul8p8(model_cam.front.x, _model_speed << 1);
    model_eye_y += vec_fastmul8p8(model_cam.front.y, _model_speed << 1);
    model_eye_z += vspeed;
    if (model_eye_z < kMinEyeZ) {
      model_eye_z = kMinEyeZ;
    }

    // Fuel
    uint8_t fuel_consumption = _model_throttle;
    if (_model_fuel > fuel_consumption) {
      _model_fuel -= fuel_consumption;
    } else {
      _model_fuel = 0;
    }

    // Rotation
    int8_t rot = model_cam.left.z >> 5;
    if (rot != 0) {
      static mat3_t mat3_rot = {{256, 0, 0}, {0, 256, 0}, {0, 0, 256}};
      mat3_rot.front.y = rot;
      mat3_rot.left.x = -rot;
      vec_transform3_inv(&mat3_rot, &model_cam);
      _model_need_normalize = true;
    }

    if (_model_need_normalize) {
      vec_orthonormalize(&model_cam);
      _model_need_normalize = false;
    }

    _update_roll_render_state();
    sprites_set_speed(_model_speed >> 6);
    sprites_set_alt(model_eye_z >> 8);
    sprites_set_vspeed(vspeed);
    sprites_set_roll(roll_angle);
    sprites_set_pitch(model_cam.front.z >> 2);
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
      vec_transform3(&kVecRollLeft, &model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_ROLL_RIGHT:
      vec_transform3(&kVecRollRight, &model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_PITCH_UP:
      vec_transform3(&kVecPitchUp, &model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_PITCH_DOWN:
      vec_transform3(&kVecPitchDown, &model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_YAW_LEFT:
      vec_transform3(&kVecYawLeft, &model_cam);
      _model_need_normalize = true;
      break;
    case MODEL_INPUT_YAW_RIGHT:
      vec_transform3(&kVecYawRight, &model_cam);
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
