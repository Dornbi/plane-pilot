#include "flight.h"

#include <stdint.h>
#include <stdlib.h>

#include "fmath.h"
#include "vec.h"

bool flight_paused = false;
enum FlightCrashReason flight_crashed = FLIGHT_CRASH_NONE;

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
static uint16_t flight_nav_point_x[6];
static uint16_t flight_nav_point_y[6];
static uint8_t flight_num_nav_points = 0;

void flight_update_nav() {
  flight_true_heading = _get_heading(flight_cam.front.x, flight_cam.front.y);
  if (flight_num_nav_points > 0) {
    flight_nav_x = flight_nav_point_x[flight_nav] - (flight_eye_x >> 8);
    flight_nav_y = flight_nav_point_y[flight_nav] - (flight_eye_y >> 8);
    flight_nav_heading =
        _get_heading(flight_nav_x, flight_nav_y) - flight_true_heading;
    if (flight_nav_heading > kHeadingMax) {
      flight_nav_heading += kHeadingMax;
    }
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
// Wings-up check. kMaxLandingRoll alone does not catch an inverted arrival:
// left.z is back to ~0 after a full 180 degree roll, so a belly-up landing
// used to pass the bank check. up.z is the attitude the roll limit is really
// trying to express. Zero rather than a tight cos(roll) bound because up.z
// also drops with nose-up pitch, and a legal flare must not trip this.
static const int16_t kMinLandingUpZ = 0;
static const int16_t kMinLandingPitch = -16;
static const int16_t kMaxLandingPitch = 64;
// Sink rate limit. Has to sit inside the range reachable ABOVE stall speed to
// mean anything: a below-stall arrival has already had its nose pushed past
// kMinLandingPitch by the stall break, so trigger 4 owns it. Above stall the
// worst sink any legal attitude produces is -251, so the old -0x0180 could
// never fire. At -0x00E0 a level-or-nose-up flare is always survivable and a
// nose-down arrival needs speed to survive it.
static const int16_t kMaxLandingVSpeed = -0x00E0;
static const uint16_t kMaxLandingSpeed = 0x0A00;
// The envelope check runs every frame the aircraft is at ground level, not
// just on the touchdown frame, so it also polices taxi and takeoff roll. That
// is wanted for the gear check - rolling on a retracted gear should fail
// immediately - but kMaxLandingSpeed is an impact limit and is far too close
// to the speeds a ground roll legitimately reaches. Full throttle with the
// gear down settles at 2290, only 270 under it, so any future drag or thrust
// tweak could turn a normal takeoff run into a crash. Once already rolling the
// limit is this looser one, which still catches nonsense start states from
// mission data but leaves the takeoff roll ~45% of headroom.
static const uint16_t kMaxGroundSpeed = 0x0D00;

void flight_init() {
  flight_paused = false;
  flight_crashed = FLIGHT_CRASH_NONE;
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
  flight_nav_point_x[0] = 0x2000;
  flight_nav_point_y[0] = 0x3F80;
  flight_nav_point_x[1] = 0x6000;
  flight_nav_point_y[1] = 0xBF80;
  flight_num_nav_points = 2;
  flight_nav = 0;
  flight_update_nav();
}

void flight_init_alt() {
  flight_paused = false;
  flight_crashed = FLIGHT_CRASH_NONE;
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
  flight_nav_point_x[0] = 0x2000;
  flight_nav_point_y[0] = 0x3F80;
  flight_nav_point_x[1] = 0x6000;
  flight_nav_point_y[1] = 0xBF80;
  flight_num_nav_points = 2;
  flight_nav = 1;
  flight_update_nav();
}

void flight_init_from_mission(const mission_t *mission) {
  flight_paused = false;
  flight_crashed = FLIGHT_CRASH_NONE;
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
  flight_num_nav_points = 0;
  for (uint8_t i = 0; i < mission->num_waypoints; ++i) {
    uint8_t wp_idx = mission->waypoints[i];
    const mission_waypoint_t *wp = &kMissionWaypoints[wp_idx];
    if (wp->x != 0 || wp->y != 0) {
      flight_nav_point_x[flight_num_nav_points] = (uint16_t)wp->x << 8;
      flight_nav_point_y[flight_num_nav_points] = ((uint16_t)wp->y << 8) + 0x80;
      flight_num_nav_points++;
    }
  }
  if (flight_num_nav_points == 0) {
    const mission_waypoint_t *def_wp = &kMissionWaypoints[kWaypointDefault];
    flight_nav_point_x[0] = (uint16_t)def_wp->x << 8;
    flight_nav_point_y[0] = ((uint16_t)def_wp->y << 8) + 0x80;
    flight_num_nav_points = 1;
  }
  flight_nav = 0;
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
      int16_t raw_lift =
          vec_fastmul8p8((int16_t)(speed_sqr >> 2), flight_cam.up.z);
      int16_t lift = vec_fastmul8p8(raw_lift, density);
      if (flight_flap) {
        // Flaps raise |C_L| by half. Upright that is what puts the stall speed
        // at kStallSpeedWithFlaps: stall speed scales as 1/sqrt(C_L), and
        // 0x0400 / sqrt(1.5) = 0x0343, so the constant and this multiplier
        // describe the same wing. Inverted, lift is already negative and the
        // shift deepens it - that is the adverse camber penalty, and it is why
        // the inverted stall speed goes up rather than down.
        lift += lift >> 1;
      }
      int16_t deficit = kTrimLift - lift;
      if (deficit > 0) {
        sink_penalty = deficit >> 4;
        flight_speed -= deficit >> 10;
      }

      uint16_t base_stall_speed;
      if (flight_flap) {
        base_stall_speed =
            (flight_cam.up.z >= 0) ? kStallSpeedWithFlaps : 0x0480;
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
          //
          // Which body rotation drops the nose depends on which way up the
          // aircraft is. To first order a body pitch step moves the nose by
          // -/+ up/16, so front.z changes by -/+ up.z/16: pitching "down"
          // raises the nose whenever up.z is negative. Picking the rotation by
          // the sign of up.z keeps the stall break pointed at the ground at
          // any attitude, which is what flight.md 2.2 requires.
          vec_transform3(flight_cam.up.z < 0 ? &kVecPitchUp : &kVecPitchDown,
                         &flight_cam);
        } else {
          uint8_t s = (stall_speed - flight_speed) >> 5;
          if (s == 0)
            s = 1;
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

      // Ground mode: level wings (roll = 0).
      //
      // Only rebuild when the wings are actually off level. Doing it every
      // frame slowly turned the aircraft back to whichever axis it was
      // nearest: rebuilding left/up from front costs a little length in the
      // 8.8 cross product, vec_orthonormalize then scales front back up, and
      // that scaling truncates - so the larger component gains a unit before
      // the smaller one does and the heading ratchets toward it. Held on the
      // runway it walked a 29 degree heading back to 0 in ~300 frames.
      // Skipping the no-op case also saves a full orthonormalize on every
      // frame of taxi and takeoff roll.
      if (flight_cam.left.z != 0) {
        flight_cam.left.x = -flight_cam.front.y;
        flight_cam.left.y = flight_cam.front.x;
        flight_cam.left.z = 0;
        vec_cross(&flight_cam.front, &flight_cam.left, &flight_cam.up);
        model_need_normalize = true;
      }
    }

    if (model_on_ground) {
      // The wheels are on the runway, so altitude is locked to the ground
      // plane whatever the nose is doing. Without this the flare pitch a
      // landing arrives with (front.z is not reset on touchdown, and ground
      // mode only clamps it to >= 0) keeps feeding a positive vertical speed
      // and the aircraft balloons back off the runway while still in ground
      // mode.
      flight_vspeed = 0;
    } else {
      flight_vspeed =
          vec_fastmul8p8(flight_cam.front.z, flight_speed) - sink_penalty;
    }

    // Motion
    flight_move_forward(flight_speed << 1, flight_vspeed);

    if (flight_eye_z <= kMinEyeZ) {
      // model_on_ground still holds last frame's value here, so it says
      // whether this is a touchdown or another frame of an existing ground
      // roll.
      bool was_on_ground = model_on_ground;
      uint16_t speed_limit = was_on_ground ? kMaxGroundSpeed : kMaxLandingSpeed;
      if (_abs16(flight_cam.left.z) > kMaxLandingRoll) {
        flight_crashed = FLIGHT_CRASH_ROLL;
      } else if (flight_cam.up.z < kMinLandingUpZ) {
        flight_crashed = FLIGHT_CRASH_INVERTED;
      } else if (flight_cam.front.z < kMinLandingPitch) {
        flight_crashed = FLIGHT_CRASH_PITCH_LOW;
      } else if (flight_cam.front.z > kMaxLandingPitch) {
        flight_crashed = FLIGHT_CRASH_PITCH_HIGH;
      } else if (flight_vspeed < kMaxLandingVSpeed) {
        flight_crashed = FLIGHT_CRASH_VSPEED;
      } else if (flight_speed > speed_limit) {
        flight_crashed = FLIGHT_CRASH_SPEED;
      } else if (!flight_gear) {
        flight_crashed = FLIGHT_CRASH_GEAR;
      }
      model_on_ground = true;
      // Touched down: the descent is over. Zeroed after the envelope check
      // above, which needs the sink rate the aircraft arrived with.
      flight_vspeed = 0;

      if (!was_on_ground && !flight_crashed && flight_cam.front.z != 0) {
        // Nose wheel comes down. Done once, on the touchdown transition,
        // rather than eased in over the rollout: easing means touching the
        // attitude every frame, and vec_normalize truncates when it rescales,
        // so a per frame nudge would ratchet the heading toward the nearest
        // axis - the same effect described at the wing levelling above.
        flight_cam.front.z = 0;
        model_need_normalize = true;
      }
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
    if (flight_num_nav_points > 0) {
      flight_nav++;
      if (flight_nav >= flight_num_nav_points) {
        flight_nav = 0;
      }
      flight_update_nav();
    }
    return;
  }

  if (flight_crashed) {
    return;
  }

  if (model_on_ground) {
    switch (input) {
    case FLIGHT_INPUT_ROLL_LEFT:
    case FLIGHT_INPUT_YAW_LEFT:
      // Nose wheel steering, so it needs the wheels to be turning. A parked
      // aircraft cannot pivot on the spot.
      if (flight_speed > 0) {
        vec_transform3(&kVecYawLeft, &flight_cam);
        model_need_normalize = true;
      }
      break;
    case FLIGHT_INPUT_ROLL_RIGHT:
    case FLIGHT_INPUT_YAW_RIGHT:
      if (flight_speed > 0) {
        vec_transform3(&kVecYawRight, &flight_cam);
        model_need_normalize = true;
      }
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
    case FLIGHT_INPUT_BRAKE:
      if (flight_speed > 32) {
        flight_speed -= 32;
      } else {
        flight_speed = 0;
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
