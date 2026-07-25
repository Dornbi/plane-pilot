#include "flight.h"

#include <stdint.h>
#include <stdlib.h>

#include "fmath.h"
#include "vec.h"

bool flight_paused = false;
bool flight_crashed = false;

#ifdef __OSCAR64__
#pragma bss(bss2)
#endif

mat3_t flight_cam;

int16_t flight_speed;
int16_t flight_vspeed;
uint8_t flight_throttle;
uint32_t flight_fuel;
uint8_t flight_flap;
uint8_t flight_gear;

uint8_t flight_nav = 0;
int16_t flight_nav_x = 0;
int16_t flight_nav_y = 0;
uint8_t flight_true_heading = 0;
uint8_t flight_nav_heading = 0;

int32_t flight_eye_x;
int32_t flight_eye_y;
int32_t flight_eye_z;

static bool model_on_ground = false;
static bool model_need_normalize;

// Location of navigation waypoints.
// They match the eye_x and eye_y coordinates >> 8
// Corresponding to the runways in world_map.cc
static const uint16_t kNavPointX[2] = {
    0x6000,
    0x2000,
};
static const uint16_t kNavPointY[2] = {
    0xBF80,
    0x3F80,
};

void flight_update_nav() {
  flight_true_heading = _get_heading(flight_cam.front.x, flight_cam.front.y);
  flight_nav_x = kNavPointX[flight_nav] - (flight_eye_x >> 8);
  flight_nav_y = kNavPointY[flight_nav] - (flight_eye_y >> 8);
  flight_nav_heading =
      _get_heading(flight_nav_x, flight_nav_y) - flight_true_heading;
  if (flight_nav_heading > kHeadingMax) {
    flight_nav_heading += kHeadingMax;
  }
}


static const mat3_t _m_init = {
    {256, 0, 0},
    {0, 256, 0},
    {0, 0, 256},
};

static const uint32_t kMinEyeZ = 0x2000;
static const uint16_t kStallSpeedWithoutFlaps = 0x0400;
static const uint16_t kStallSpeedWithFlaps = 0x0340;
static const uint16_t kMaxSpeed = 0x0F00;
static const int16_t kTrimLift = 0x1000;
static const uint8_t kMinThrottle = 0x00;
static const uint8_t kMaxThrottle = 0x18;
static const int16_t kMoveForwardBackwardSpeed = 0x4000;

// Nose attitude above which the stall pitch down has to be done as a rotation
// instead of by lowering front.z directly. sin(61 deg) * 256; past this the
// horizontal part of front is small enough that renormalization undoes most
// of a direct change.
static const int16_t kMaxStallPitchZ = 224;

// Landing thresholds
static const int16_t kMaxLandingRoll = 32;
static const int16_t kMinLandingPitch = -16;
static const int16_t kMaxLandingPitch = 64;
static const int16_t kMaxLandingVSpeed = -0x0180;
static const uint16_t kMaxLandingSpeed = 0x0A00;

void flight_init() {
  flight_paused = false;
  flight_crashed = false;
  flight_cam = _m_init;
  flight_eye_x = 0x140000;
  flight_eye_y = 0x3F8000;
  flight_eye_z = 0x010000;
  flight_speed = 0x860;
  flight_throttle = 0x14;
  model_need_normalize = false;
  flight_flap = false;
  flight_gear = false;
  flight_fuel = 0x21FFF;
  model_on_ground = false;
  flight_nav = 0;
  flight_update_nav();
}

void flight_init_alt() {
  flight_paused = false;
  flight_crashed = false;
  flight_cam = _m_init;
  flight_eye_x = 0x400000;
  flight_eye_y = 0xBF8000;
  flight_eye_z = 0x040000;
  flight_speed = 0x860;
  flight_throttle = 0x14;
  model_need_normalize = false;
  flight_flap = false;
  flight_gear = false;
  flight_fuel = 0x21FFF;
  model_on_ground = false;
  flight_nav = 1;
  flight_update_nav();
}

void flight_init_from_mission(const mission_t *mission) {
  flight_paused = false;
  flight_crashed = false;
  flight_cam = _m_init;
  flight_eye_x = (int32_t)mission->start_x << 16;
  flight_eye_y = ((int32_t)mission->start_y << 16) + 0x8000;
  flight_eye_z = (int32_t)mission->start_z << 16;
  if (flight_eye_z <= kMinEyeZ) {
    flight_eye_z = kMinEyeZ;
    model_on_ground = true;
  } else {
    model_on_ground = false;
  }
  flight_speed = (int16_t)mission->start_speed << 4;
  flight_throttle = mission->start_throttle;
  flight_fuel =
      mission->start_fuel ? (((uint32_t)mission->start_fuel << 12) - 1) : 0;
  model_need_normalize = false;
  flight_flap = false;
  flight_gear = model_on_ground;
  flight_nav = (mission->start_y >= 0x80) ? 1 : 0;
  flight_update_nav();
}

static void flight_move_forward(int16_t fspeed, int16_t vspeed) {
  flight_eye_x += vec_fastmul8p8(flight_cam.front.x, fspeed);
  flight_eye_y += vec_fastmul8p8(flight_cam.front.y, fspeed);
  flight_eye_z += vspeed;
  if (flight_eye_z < kMinEyeZ) {
    flight_eye_z = kMinEyeZ;
  }
}

void flight_advance() {
  if (flight_crashed) {
    return;
  }

  if (!flight_paused) {
    // Altitude density decay (above Z = 0x080000)
    uint8_t alt_penalty = 0;
    if (flight_eye_z > 0x080000) {
      uint32_t alt_diff = (flight_eye_z - 0x080000) >> 12;
      alt_penalty = (alt_diff > 128) ? 128 : (uint8_t)alt_diff;
    }
    uint16_t density = 256 - alt_penalty;

    // Speed: Air resistance, gravity, throttle
    uint16_t speed_sqr = vec_fastsqr8p8(flight_speed);
    flight_speed -= speed_sqr >> 10;
    if (flight_gear) {
      flight_speed -= speed_sqr >> 12;
    }
    if (flight_flap) {
      flight_speed -= speed_sqr >> 12;
    }
    if (!model_on_ground) {
      flight_speed -= vec_fastsqr8p8(flight_cam.left.z) >> 5;
    }
    flight_speed -= flight_cam.front.z >> 3;
    if (flight_fuel > 0) {
      // vec_fastmul8p8 rather than a general 16x16 multiply: density is
      // already 8.8 with 256 meaning "sea level", which is exactly the
      // convention this routine expects.
      flight_speed += vec_fastmul8p8(flight_throttle, density);
    }

    int16_t sink_penalty = 0;
    if (!model_on_ground) {
      int16_t raw_lift = vec_fastmul8p8((int16_t)(speed_sqr >> 2), flight_cam.up.z);
      int16_t lift = vec_fastmul8p8(raw_lift, density);
      int16_t deficit = kTrimLift - lift;
      if (deficit > 0) {
        sink_penalty = deficit >> 4;
        flight_speed -= deficit >> 10;
      }

      uint16_t base_stall_speed;
      if (flight_flap) {
        base_stall_speed = (flight_cam.up.z >= 0) ? kStallSpeedWithFlaps : 0x0480;
      } else {
        base_stall_speed = kStallSpeedWithoutFlaps;
      }
      uint16_t stall_speed = base_stall_speed + ((uint16_t)alt_penalty << 1);

      if (flight_speed < 0) {
        flight_speed = 0;
      }
      if (flight_speed < stall_speed) {
        if (flight_cam.front.z > kMaxStallPitchZ) {
          // Dead spot. front is a unit vector, so with the nose this high its
          // horizontal part is almost nothing, and vec_orthonormalize scales
          // the whole vector back to length 256 at the end of the frame -
          // putting back nearly all of a direct change to front.z. Pointing
          // straight up it puts back all of it and the nose never drops.
          // A body axis rotation is well defined at any attitude, and one
          // step is enough to tip the nose off the vertical; from there the
          // cheap path below works again.
          vec_transform3(&kVecPitchDown, &flight_cam);
        } else {
          uint8_t s = (stall_speed - flight_speed) >> 5;
          if (s == 0) s = 1;
          flight_cam.front.z -= s;
          if (flight_cam.front.z < -256) {
            flight_cam.front.z = -256;
          }
        }
        model_need_normalize = true;
      } else if (flight_speed > kMaxSpeed) {
        flight_speed = kMaxSpeed;
      }
    } else {
      // In ground mode: no stall
      if (flight_throttle == 0 && flight_speed > 0) {
        flight_speed -= 2;
      }
      if (flight_speed < 0) {
        flight_speed = 0;
      } else if (flight_speed > kMaxSpeed) {
        flight_speed = kMaxSpeed;
      }

      // Ground mode: cannot pitch forward (front.z >= 0)
      if (flight_cam.front.z < 0) {
        flight_cam.front.z = 0;
        model_need_normalize = true;
      }

      // Ground mode: level wings (roll = 0)
      flight_cam.left.x = -flight_cam.front.y;
      flight_cam.left.y = flight_cam.front.x;
      flight_cam.left.z = 0;
      vec_cross(&flight_cam.front, &flight_cam.left, &flight_cam.up);
      model_need_normalize = true;
    }

    flight_vspeed =
        vec_fastmul8p8(flight_cam.front.z, flight_speed) - sink_penalty;

    // Motion
    flight_move_forward(flight_speed << 1, flight_vspeed);

    if (flight_eye_z <= kMinEyeZ) {
      if (_abs16(flight_cam.left.z) > kMaxLandingRoll ||
          flight_cam.front.z < kMinLandingPitch ||
          flight_cam.front.z > kMaxLandingPitch ||
          flight_vspeed < kMaxLandingVSpeed ||
          flight_speed > kMaxLandingSpeed || !flight_gear) {
        flight_crashed = true;
      }
      model_on_ground = true;
    }

    // Fuel
    uint8_t fuel_consumption = flight_throttle;
    if (flight_fuel > fuel_consumption) {
      flight_fuel -= fuel_consumption;
    } else {
      flight_fuel = 0;
      flight_throttle = 0;
    }

    // Rotation (only when airborne)
    if (!model_on_ground) {
      int8_t rot = flight_cam.left.z >> 5;
      if (rot != 0) {
        static mat3_t mat3_rot = {{256, 0, 0}, {0, 256, 0}, {0, 0, 256}};
        mat3_rot.front.y = rot;
        mat3_rot.left.x = -rot;
        vec_transform3_inv(&mat3_rot, &flight_cam);
        model_need_normalize = true;
      }
    }
  } else {
    // Paused: the physics is frozen, but keep the vertical speed instrument
    // live so it still reflects any attitude change the pilot makes.
    flight_vspeed = vec_fastmul8p8(flight_cam.front.z, flight_speed);
  }

  if (model_need_normalize) {
    vec_orthonormalize(&flight_cam);
    model_need_normalize = false;
  }

  flight_update_nav();
}

void flight_input(enum flight_input_t input) {
  if (input == FLIGHT_INPUT_TOGGLE_NAV) {
    flight_nav = 1 - flight_nav;
    flight_update_nav();
    return;
  }

  if (flight_crashed) {
    return;
  }

  if (model_on_ground) {
    switch (input) {
    case FLIGHT_INPUT_ROLL_LEFT:
    case FLIGHT_INPUT_YAW_LEFT:
      vec_transform3(&kVecYawLeft, &flight_cam);
      model_need_normalize = true;
      break;
    case FLIGHT_INPUT_ROLL_RIGHT:
    case FLIGHT_INPUT_YAW_RIGHT:
      vec_transform3(&kVecYawRight, &flight_cam);
      model_need_normalize = true;
      break;
    case FLIGHT_INPUT_PITCH_DOWN:
      if (flight_cam.front.z > 0) {
        vec_transform3(&kVecPitchDown, &flight_cam);
        if (flight_cam.front.z < 0) {
          flight_cam.front.z = 0;
        }
        model_need_normalize = true;
      }
      break;
    case FLIGHT_INPUT_PITCH_UP: {
      uint16_t stall_speed =
          flight_flap ? kStallSpeedWithFlaps : kStallSpeedWithoutFlaps;
      if (flight_speed > stall_speed) {
        vec_transform3(&kVecPitchUp, &flight_cam);
        model_need_normalize = true;
        model_on_ground = false;
      }
      break;
    }
    case FLIGHT_INPUT_THROTTLE_UP:
      if (flight_throttle < kMaxThrottle) {
        flight_throttle += 1;
      }
      break;
    case FLIGHT_INPUT_THROTTLE_DOWN:
      if (flight_throttle > kMinThrottle) {
        flight_throttle -= 1;
      }
      break;
    case FLIGHT_INPUT_TOGGLE_FLAP:
      flight_flap = 1 - flight_flap;
      break;
    case FLIGHT_INPUT_TOGGLE_GEAR:
      flight_gear = 1 - flight_gear;
      break;
    case FLIGHT_INPUT_MOVE_BACKWARD:
    case FLIGHT_INPUT_MOVE_FORWARD:
      if (flight_paused) {
        int16_t speed = input == FLIGHT_INPUT_MOVE_FORWARD
                            ? kMoveForwardBackwardSpeed
                            : -kMoveForwardBackwardSpeed;
        int16_t vspeed = vec_fastmul8p8(flight_cam.front.z, speed);
        flight_move_forward(speed, vspeed);
      }
      break;
    default:
      break;
    }
    return;
  }

  switch (input) {
  case FLIGHT_INPUT_ROLL_LEFT:
    vec_transform3(&kVecRollLeft, &flight_cam);
    model_need_normalize = true;
    break;
  case FLIGHT_INPUT_ROLL_RIGHT:
    vec_transform3(&kVecRollRight, &flight_cam);
    model_need_normalize = true;
    break;
  case FLIGHT_INPUT_PITCH_UP:
    vec_transform3(&kVecPitchUp, &flight_cam);
    model_need_normalize = true;
    break;
  case FLIGHT_INPUT_PITCH_DOWN:
    vec_transform3(&kVecPitchDown, &flight_cam);
    model_need_normalize = true;
    break;
  case FLIGHT_INPUT_YAW_LEFT:
    vec_transform3(&kVecYawLeft, &flight_cam);
    model_need_normalize = true;
    break;
  case FLIGHT_INPUT_YAW_RIGHT:
    vec_transform3(&kVecYawRight, &flight_cam);
    model_need_normalize = true;
    break;
  case FLIGHT_INPUT_THROTTLE_UP:
    if (flight_throttle < kMaxThrottle) {
      flight_throttle += 1;
    }
    break;
  case FLIGHT_INPUT_THROTTLE_DOWN:
    if (flight_throttle > kMinThrottle) {
      flight_throttle -= 1;
    }
    break;
  case FLIGHT_INPUT_TOGGLE_FLAP:
    flight_flap = 1 - flight_flap;
    break;
  case FLIGHT_INPUT_TOGGLE_GEAR:
    flight_gear = 1 - flight_gear;
    break;
  case FLIGHT_INPUT_MOVE_BACKWARD:
  case FLIGHT_INPUT_MOVE_FORWARD:
    if (flight_paused) {
      int16_t speed = input == FLIGHT_INPUT_MOVE_FORWARD
                          ? kMoveForwardBackwardSpeed
                          : -kMoveForwardBackwardSpeed;
      int16_t vspeed = vec_fastmul8p8(flight_cam.front.z, speed);
      flight_move_forward(speed, vspeed);
    }
    break;
  default:
    break;
  }
}
