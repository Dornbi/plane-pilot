#include "flight.h"

#include <stdint.h>
#include <stdlib.h>

#include "fmath.h"
#include "msg.h"
#include "vec.h"
#include "world.h"

bool flight_paused = false;
enum FlightStatus flight_status = FLIGHT_ONGOING;

uint8_t flight_current_wp = 0;
uint8_t flight_active_mission_idx = 0;

uint8_t flight_path_count = 0;
uint8_t flight_map_px = 0;
uint8_t flight_map_py = 0;
// Next slot to write. Wraps at kFlightPathLen, which is a power of two, so
// the wrap is a mask.
static uint8_t flight_path_head = 0;
static const uint8_t kFlightPathMask = kFlightPathLen - 1;

#ifdef __OSCAR64__
#pragma bss(bss2)
#endif

// The map view's flight path. Parallel arrays rather than one array of
// 2-byte entries, so an index needs no shift, matching how every other table
// here is stored.
//
// 256 bytes, which is most of what was left of bss2 ($0280..$0800): the
// polygon scratch buffers named in poly.cc moved out to main's bss to make
// room, and between them the region is now full to the byte. bss2 is RAM that
// exists whether or not anything is put there — the screen moved to $E800 —
// so leaving it half empty while main's bss grows is the one arrangement with
// nothing to recommend it.
uint8_t flight_path_px[kFlightPathLen];
uint8_t flight_path_py[kFlightPathLen];

mat3_t flight_cam;

int16_t flight_speed;
int16_t flight_vspeed;
uint8_t flight_throttle;
uint32_t flight_fuel;
uint8_t flight_flap;
uint8_t flight_gear;
uint8_t flight_stall;

uint8_t flight_events;
uint8_t flight_gen;

// Events accumulated so far in the current frame, before they are published.
//
// A separate byte is needed because the frame does not collect them all in one
// place. sim_run() calls flight_input() for the keys first and flight_advance()
// afterwards, so gear and flap are recorded before the step that publishes
// them even begins - clearing flight_events at the top of flight_advance(),
// which is the obvious reading of docs/sound.md section 5, would throw them
// away every time.
//
// Both writers are on the main line and run in a fixed order within the frame,
// so this needs no synchronisation of its own.
static uint8_t model_pending_events;

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

// Location of navigation waypoints. All three are kMaxNavPoints long, but
// in two different index spaces: flight_waypoint_nav is indexed by
// waypoint-within-mission and flight_nav_point_* by navpoint. Four is the
// cap on both -- see kMaxNavPoints in flight.h.
uint16_t flight_nav_point_x[kMaxNavPoints];
uint16_t flight_nav_point_y[kMaxNavPoints];
static uint8_t flight_waypoint_nav[kMaxNavPoints];
uint8_t flight_num_nav_points = 0;

// Appends the current position to the flight path, unless it is the pixel
// already at the head.
//
// The conversion is map.md section 4's: flight_eye_* is 24.8 fixed point
// metres, so >> 16 gives 256-metre world units; + 4 centres cells on
// multiples of 8; and the complement applies the map's 180 degree rotation.
// x selects the row and y the column, and y is halved because a horizontal
// map pixel spans two world units -- which is exactly what makes the map
// square on screen at 256 m per pixel on both axes.
//
// Comparing against the single most recent entry is enough to keep the path
// connected; see kFlightPathLen in flight.h.
static void _flight_path_sample() {
  const uint8_t py = 127 - ((uint8_t)((flight_eye_x >> 16) + 0x04) & 0x7F);
  const uint8_t px =
      127 - (((uint8_t)((flight_eye_y >> 16) + 0x04) >> 1) & 0x7F);
  flight_map_px = px;
  flight_map_py = py;
  if (flight_path_count != 0) {
    const uint8_t last = (flight_path_head - 1) & kFlightPathMask;
    if (flight_path_px[last] == px && flight_path_py[last] == py) {
      return;
    }
  }
  flight_path_px[flight_path_head] = px;
  flight_path_py[flight_path_head] = py;
  flight_path_head = (flight_path_head + 1) & kFlightPathMask;
  if (flight_path_count < kFlightPathLen) {
    flight_path_count++;
  }
}

static void _flight_update_nav() {
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
// kMaxSpeed lives in flight.h: sound.cc sizes its wind table by it.
static const int16_t kTrimLift = 0x1000;
static const uint8_t kMinThrottle = 0x00;
// kMaxThrottle lives in flight.h: sound.cc sizes its engine pitch table by it.
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
static const int16_t kMinLandingPitch = -32;
static const int16_t kMaxLandingPitch = 64;
// Sink rate limit. Has to sit inside the range reachable ABOVE stall speed to
// mean anything: a below-stall arrival has already had its nose pushed past
// kMinLandingPitch by the stall break, so trigger 4 owns it. Above stall the
// worst sink any legal attitude produces is -251, so the old -0x0180 could
// never fire. At -0x00E0 a level-or-nose-up flare is always survivable and a
// At -0x0120 a level-or-nose-up flare is always survivable and moderate
// nose-down landings survive, while steep nose-down dives still trigger sink limits.
static const int16_t kMaxLandingVSpeed = -0x0120;
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

// Host test fixture only; see flight.h.
#ifndef __OSCAR64__
void flight_init() {
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  flight_active_mission_idx = 0xFF;
  flight_current_wp = 0;
  flight_cam = _m_init;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = 0x010000;
  flight_speed = 0x860;
  flight_throttle = 0x14;
  model_need_normalize = false;
  flight_flap = false;
  flight_gear = false;
  flight_stall = false;
  // flight_gen is deliberately NOT reset. It is a free-running counter whose
  // only job is to differ from the last value a consumer saw, and restarting
  // it at zero could land on exactly the value already recorded - dropping one
  // event set on the first frame of a mission.
  flight_events = 0;
  model_pending_events = 0;
  flight_fuel = 0x21FFF;
  model_on_ground = false;
  flight_nav_point_x[0] = 0x2000;
  flight_nav_point_y[0] = 0x3F80;
  flight_nav_point_x[1] = 0x6000;
  flight_nav_point_y[1] = 0xBF80;
  flight_num_nav_points = 2;
  flight_nav = 0;
  _flight_update_nav();
  flight_path_head = 0;
  flight_path_count = 0;
  _flight_path_sample();
}
#endif

void flight_init_from_mission(uint8_t mission_idx) {
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  flight_active_mission_idx = mission_idx;
  flight_current_wp = 0;
  flight_cam = _m_init;
  flight_eye_x = (int32_t)kMissionStartX[mission_idx] << 16;
  flight_eye_y = ((int32_t)kMissionStartY[mission_idx] << 16) + 0x8000;
  flight_eye_z = (int32_t)kMissionStartZ[mission_idx] << 16;
  if (flight_eye_z <= kMinEyeZ) {
    flight_eye_z = kMinEyeZ;
    model_on_ground = true;
  } else {
    model_on_ground = false;
  }
  flight_speed = (int16_t)kMissionStartSpeed[mission_idx] << 4;
  flight_throttle = kMissionStartThrottle[mission_idx];
  flight_fuel = kMissionStartFuel[mission_idx]
                    ? (((uint32_t)kMissionStartFuel[mission_idx] << 12) - 1)
                    : 0;
  model_need_normalize = false;
  flight_flap = false;
  flight_gear = model_on_ground;
  flight_stall = false;
  // flight_gen is deliberately NOT reset. It is a free-running counter whose
  // only job is to differ from the last value a consumer saw, and restarting
  // it at zero could land on exactly the value already recorded - dropping one
  // event set on the first frame of a mission.
  flight_events = 0;
  model_pending_events = 0;
  flight_num_nav_points = 0;
  uint8_t wp_begin = kMissionWpBegin[mission_idx];
  uint8_t wp_end = kMissionWpEnd[mission_idx];
  uint8_t num_wp = wp_end - wp_begin;
  // No mission slice is longer than kMaxNavPoints today. Clamping the loop
  // rather than trusting that bounds both arrays at once -- flight_waypoint_nav
  // by i, flight_nav_point_* by a counter that advances at most once per
  // iteration -- so a future longer mission quietly loses its extra
  // waypoints instead of writing past the end of either.
  if (num_wp > kMaxNavPoints) {
    num_wp = kMaxNavPoints;
  }
  for (uint8_t i = 0; i < num_wp; ++i) {
    uint8_t wp_idx = wp_begin + i;
    uint8_t wx = kMissionWpX[wp_idx];
    uint8_t wy = kMissionWpY[wp_idx];
    if (wx != 0 || wy != 0) {
      flight_nav_point_x[flight_num_nav_points] = (uint16_t)wx << 8;
      flight_nav_point_y[flight_num_nav_points] = ((uint16_t)wy << 8) + 0x80;
      flight_num_nav_points++;
      flight_waypoint_nav[i] = flight_num_nav_points;
    } else {
      flight_waypoint_nav[i] = 0;
    }
  }
  if (flight_num_nav_points == 0) {
    uint8_t wx = kMissionWpX[kWaypointDefault];
    uint8_t wy = kMissionWpY[kWaypointDefault];
    flight_nav_point_x[0] = (uint16_t)wx << 8;
    flight_nav_point_y[0] = ((uint16_t)wy << 8) + 0x80;
    flight_num_nav_points = 1;
  }
  flight_nav = 0;
  _flight_update_nav();
  // The trail is per attempt, so R starts a fresh one. Seeding it with the
  // start position means the path is already anchored on the runway before
  // the aircraft has moved.
  flight_path_head = 0;
  flight_path_count = 0;
  _flight_path_sample();
}

static void _flight_move_forward(int16_t fspeed, int16_t vspeed) {
  flight_eye_x += vec_fastmul8p8(flight_cam.front.x, fspeed);
  flight_eye_y += vec_fastmul8p8(flight_cam.front.y, fspeed);
  flight_eye_z += vspeed;
  if (flight_eye_z < kMinEyeZ) {
    flight_eye_z = kMinEyeZ;
  }
}

// True when the aircraft is over a runway tile.
static bool _on_runway() {
  uint8_t row =
      ((uint8_t)((flight_eye_x >> 16) + 0x04) >> 3) & kWorldMapHeightMask;
  uint8_t col =
      ((uint8_t)((flight_eye_y >> 16) + 0x04) >> 3) & kWorldMapWidthMask;
  return kWorldMap[row][col] == MAP_OBJ_RUNWAY;
}

// One landing envelope test, shared by the approach warnings and the
// touchdown verdict. Returns FLIGHT_ONGOING while inside the envelope,
// otherwise the status describing the first violation.
//
// The order is what the pilot is told to fix first, so it runs from the
// things that have to be settled early on the approach (be over the runway,
// upright, gear down) to the ones that are trimmed on short final. It also
// decides which crash is reported when a touchdown breaks several rules at
// once.
static enum FlightStatus _landing_fault(uint16_t speed_limit,
                                        bool check_runway) {
  if (check_runway && !_on_runway()) {
    return FLIGHT_CRASH_NOT_ON_RUNWAY;
  }
  if (flight_cam.up.z < kMinLandingUpZ) {
    return FLIGHT_CRASH_INVERTED;
  }
  if (!flight_gear) {
    return FLIGHT_CRASH_GEAR;
  }
  if (flight_vspeed < kMaxLandingVSpeed) {
    return FLIGHT_CRASH_VSPEED;
  }
  if (_abs16(flight_cam.left.z) > kMaxLandingRoll) {
    return FLIGHT_CRASH_ROLL;
  }
  if (flight_cam.front.z < kMinLandingPitch) {
    return FLIGHT_CRASH_PITCH_LOW;
  }
  if (flight_cam.front.z > kMaxLandingPitch) {
    return FLIGHT_CRASH_PITCH_HIGH;
  }
  if (flight_speed > speed_limit) {
    return FLIGHT_CRASH_SPEED;
  }
  return FLIGHT_ONGOING;
}

// One text per FlightStatus, with the prefix supplied by the caller, so the
// approach warnings and the crash report share one set of strings.
// Keep in sync with the enum in flight.h.
static const char *const kFaultText[] = {
    "",               // FLIGHT_ONGOING
    "",               // FLIGHT_MISSION_COMPLETED (handled below)
    "BANK ANGLE",     // FLIGHT_CRASH_ROLL
    "INVERTED",       // FLIGHT_CRASH_INVERTED
    "PITCH TOO LOW",  // FLIGHT_CRASH_PITCH_LOW
    "PITCH TOO HIGH", // FLIGHT_CRASH_PITCH_HIGH
    "SINK RATE",      // FLIGHT_CRASH_VSPEED
    "TOO FAST",       // FLIGHT_CRASH_SPEED
    "GEAR RETRACTED", // FLIGHT_CRASH_GEAR
    "NOT ON RUNWAY",  // FLIGHT_CRASH_NOT_ON_RUNWAY
};

// Why the current waypoint is not satisfied while the aircraft is in the
// right place. Indexed by MissionWaypointConstraint; keep in sync with
// mission.h. WP_NOTHING has nothing to complain about: being there is the
// whole constraint.
static const char *const kWaypointFault[] = {
    "",                   // 0 WP_NOTHING
    "",                   // 1 WP_LANDED
    "CLIMB ABOVE 1000FT", // 2 WP_MIN_1000FT
    "CLIMB ABOVE 2000FT", // 3 WP_MIN_2000FT
    "CLIMB ABOVE 3000FT", // 4 WP_MIN_3000FT
    "GO BELOW 125FT",     // 5 WP_MAX_125FT
    "FLY INVERTED"        // 6 WP_UPSIDE_DOWN
    ,
};

// Both prefixes are nine characters and the longest fault text is fourteen,
// so 24 is the exact fit and this leaves a little room. Built here rather
// than stored per message because msg_show() keeps only the pointer. Every
// caller below bails out once flight_status is set, so no warning can rewrite
// the buffer while a crash message is still on screen.
static char _status_text[40];

static const char *_join(const char *prefix, const char *suffix) {
  char *dst = _status_text;
  while (*prefix) {
    *dst++ = *prefix++;
  }
  while (*suffix) {
    *dst++ = *suffix++;
  }
  *dst = 0;
  return _status_text;
}

const char *flight_status_text(enum FlightStatus status, bool crashed) {
  if (status == FLIGHT_MISSION_COMPLETED) {
    return "MISSION COMPLETE!";
  }
  return _join(crashed ? "CRASHED: " : "WARNING: ", kFaultText[status]);
}

// Longer than a waypoint message, because it is the one the pilot flew the
// whole mission for, but still temporary: the flight continues underneath it
// and the message row belongs to the warnings again once it expires.
static const uint16_t kMissionCompleteDuration = 3 * MSG_DEFAULT_DURATION;

static void _flight_check_mission_waypoints() {
  if (flight_active_mission_idx >= kMissionCount || flight_crashed() ||
      flight_paused) {
    return;
  }
  uint8_t wp_begin = kMissionWpBegin[flight_active_mission_idx];
  uint8_t wp_end = kMissionWpEnd[flight_active_mission_idx];
  uint8_t num_wp = wp_end - wp_begin;
  if (flight_current_wp >= num_wp) {
    return;
  }

  uint8_t wp_idx = wp_begin + flight_current_wp;
  MissionWaypointConstraint constraint = kMissionWpConstraint[wp_idx];
  uint8_t eye_x_high = (uint8_t)(flight_eye_x >> 16);
  uint8_t eye_y_high = (uint8_t)(flight_eye_y >> 16);
  uint8_t wx = kMissionWpX[wp_idx];
  uint8_t wy = kMissionWpY[wp_idx];

  bool pos_ok = true;
  if (wx != 0 || wy != 0) {
    int8_t dx = (int8_t)(eye_x_high - wx);
    int8_t dy = (int8_t)(eye_y_high - wy);
    if (dx < 0) {
      dx = -dx;
    }
    if (dy < 0) {
      dy = -dy;
    }
    uint8_t max_dy = (constraint == WP_LANDED) ? 0x04 : 0x10;
    pos_ok = (dx <= 0x10 && dy <= max_dy);
  }

  // Altitude limits live in a table so the three MIN_*FT cases share one
  // comparison instead of open-coding three 32-bit ones.
  // Indexed by MissionWaypointConstraint; keep in sync with mission.h.
  static const uint8_t kWpMinAltHi[] = {
      0, // 0 WP_NOTHING
      0, // 1 WP_LANDED       (handled below)
      2, // 2 WP_MIN_1000FT   (0x020000 >> 16)
      4, // 3 WP_MIN_2000FT
      6, // 4 WP_MIN_3000FT
      0, // 5 WP_MAX_125FT    (handled below)
      0, // 6 WP_UPSIDE_DOWN  (handled below)
  };
  bool met = pos_ok;
  if (met) {
    switch (constraint) {
    case WP_MAX_125FT:
      met = flight_eye_z <= 0x004000;
      break;
    case WP_UPSIDE_DOWN:
      met = flight_cam.up.z < 0;
      break;
    case WP_LANDED:
      met = model_on_ground && (flight_speed <= 0x0010);
      break;
    default:
      met = (uint8_t)(flight_eye_z >> 16) >= kWpMinAltHi[constraint];
      break;
    }
  }

  if (met) {
    if (flight_current_wp + 1 < num_wp) {
      uint8_t nav_n = flight_waypoint_nav[flight_current_wp];
      if (nav_n != 0) {
        static char nav_reached_buf[] = "WAYPOINT 1 REACHED";
        nav_reached_buf[9] = '0' + nav_n;
        msg_show(nav_reached_buf);
      } else {
        msg_show("NEXT GOAL COMPLETED");
      }
      flight_current_wp++;
    } else {
      // The last waypoint. The flight is not over: the pilot keeps the
      // aircraft and can fly on until they quit, restart or crash it. Past
      // the end of the list, so this cannot fire a second time - the message
      // is a moment, not a state, and nothing here re-arms it.
      flight_current_wp = num_wp;
      flight_status = FLIGHT_MISSION_COMPLETED;
      msg_show(flight_status_text(FLIGHT_MISSION_COMPLETED, false),
               kMissionCompleteDuration);
      if (flight_active_mission_idx < kMissionCount) {
        mission_completed[flight_active_mission_idx] = true;
      }
    }
  } else if (pos_ok && (wx != 0 || wy != 0)) {
    // Over the waypoint but the constraint is not satisfied: say which one,
    // so the player knows they are in the right place and only the altitude
    // or the attitude is wrong.
    const char *fault = kWaypointFault[constraint];
    if (*fault) {
      msg_show(_join(fault, " FOR MISSION"));
    }
  }
}

void flight_advance() {
  if (flight_crashed()) {
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
      flight_stall = flight_speed < stall_speed;
      if (flight_stall) {
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
      flight_stall = false;
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
    _flight_move_forward(flight_speed << 1, flight_vspeed);

    if (!model_on_ground && flight_vspeed < 0 && flight_eye_z <= 0x4000) {
      enum FlightStatus fault = _landing_fault(kMaxLandingSpeed, true);
      if (fault) {
        msg_show(flight_status_text(fault, false));
      }
    }

    if (flight_eye_z <= kMinEyeZ) {
      // model_on_ground still holds last frame's value here, so it says
      // whether this is a touchdown or another frame of an existing ground
      // roll.
      bool was_on_ground = model_on_ground;
      uint16_t speed_limit = was_on_ground ? kMaxGroundSpeed : kMaxLandingSpeed;
      // Only a fault is written back. A clean touchdown leaves the status
      // alone rather than clearing it to FLIGHT_ONGOING, which would erase a
      // FLIGHT_MISSION_COMPLETED the pilot has already earned.
      enum FlightStatus touchdown_fault =
          _landing_fault(speed_limit, !was_on_ground);
      if (touchdown_fault) {
        flight_status = touchdown_fault;
      }

      if (!was_on_ground) {
        // The arrival, not the rollout: this block runs on every frame at
        // ground level, so the transition is what the sound wants.
        model_pending_events |= FLIGHT_EV_TOUCHDOWN;
      }

      model_on_ground = true;
      // Touched down: the descent is over. Zeroed after the envelope check
      // above, which needs the sink rate the aircraft arrived with.
      flight_vspeed = 0;

      if (!was_on_ground && !flight_crashed() && flight_cam.front.z != 0) {
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
      // The tank runs dry once. Every later frame arrives here too, with
      // nothing left to burn, so the announcement hangs off the last of the
      // fuel rather than off the empty tank.
      if (flight_fuel != 0) {
        msg_show("OUT OF FUEL");
      }
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
    // Paused: the physics is frozen, so recompute the vertical speed
    // instrument from the attitude rather than leaving it at whatever the last
    // running step produced.
    //
    // This used to matter because the controls still worked while paused and
    // the pilot could pitch the nose. They no longer do (see flight_input),
    // and neither input that survives a pause moves front.z or flight_speed,
    // so in practice this now recomputes the same value every frame. It stays
    // as the one place vspeed is derived: it costs a multiply and it means the
    // instrument cannot disagree with the state it is displaying.
    flight_vspeed = vec_fastmul8p8(flight_cam.front.z, flight_speed);
  }

  if (model_need_normalize) {
    vec_orthonormalize(&flight_cam);
    model_need_normalize = false;
  }

  _flight_update_nav();
  _flight_path_sample();
  _flight_check_mission_waypoints();

  // Reaching here while wrecked means the crash happened during *this* step:
  // the guard at the top of this function returns early on every later frame.
  // So the transition needs no remembered flag of its own, and there is no
  // path by which the event can fire twice.
  if (flight_crashed()) {
    model_pending_events |= FLIGHT_EV_CRASH;
  }

  // Publish the frame's events, then bump the generation - in that order, and
  // last of all. An observer that sees a new generation is then guaranteed to
  // see the complete set that belongs to it.
  //
  // This is past the crash guard at the top, so a wrecked aircraft publishes
  // nothing further and its last generation stands. That is what stops the
  // final event of a flight from being retriggered on every frame afterwards.
  flight_events = model_pending_events;
  model_pending_events = 0;
  ++flight_gen;
}

void flight_input(enum flight_input_t input) {
  if (input == FLIGHT_INPUT_TOGGLE_NAV) {
    if (flight_num_nav_points > 0) {
      flight_nav++;
      if (flight_nav >= flight_num_nav_points) {
        flight_nav = 0;
      }
      _flight_update_nav();

      static char nav_msg_buf[] = "NAVPOINT 1 SELECTED";
      nav_msg_buf[9] = '0' + (flight_nav + 1);
      msg_show(nav_msg_buf);
    }
    return;
  }

  // Paused freezes the aircraft, and that has to include the controls. Without
  // this, pause was a way to reconfigure in stopped time - roll, retrim, drop
  // the gear and change the throttle with the physics not running - and then
  // resume already set up. The instruments moved while the world did not.
  //
  // Z and X are the exception, and the only one. They exist precisely to
  // reposition the aircraft while frozen, and each already tests
  // flight_paused for itself before doing anything.
  //
  // N is not listed here because it never reaches this point: the navpoint
  // toggle returns above, ahead of even the crash guard. It selects which
  // waypoint the compass points at, which is map-reading rather than flying.
  //
  // The rule lives here rather than in sim.cc's key handling so that it cannot
  // be bypassed by a future call site, and so flight_test can assert it.
  if (flight_paused && input != FLIGHT_INPUT_MOVE_FORWARD &&
      input != FLIGHT_INPUT_MOVE_BACKWARD) {
    return;
  }

  if (flight_crashed()) {
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
      model_pending_events |= FLIGHT_EV_FLAP;
      break;
    case FLIGHT_INPUT_TOGGLE_GEAR:
      if (!flight_gear) {
        flight_gear = 1;
        model_pending_events |= FLIGHT_EV_GEAR;
      }
      break;
    case FLIGHT_INPUT_MOVE_BACKWARD:
    case FLIGHT_INPUT_MOVE_FORWARD:
      if (flight_paused) {
        int16_t speed = input == FLIGHT_INPUT_MOVE_FORWARD
                            ? kMoveForwardBackwardSpeed
                            : -kMoveForwardBackwardSpeed;
        int16_t vspeed = vec_fastmul8p8(flight_cam.front.z, speed);
        _flight_move_forward(speed, vspeed);
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
    model_pending_events |= FLIGHT_EV_FLAP;
    break;
  case FLIGHT_INPUT_TOGGLE_GEAR:
    flight_gear = 1 - flight_gear;
    model_pending_events |= FLIGHT_EV_GEAR;
    break;
  case FLIGHT_INPUT_MOVE_BACKWARD:
  case FLIGHT_INPUT_MOVE_FORWARD:
    if (flight_paused) {
      int16_t speed = input == FLIGHT_INPUT_MOVE_FORWARD
                          ? kMoveForwardBackwardSpeed
                          : -kMoveForwardBackwardSpeed;
      int16_t vspeed = vec_fastmul8p8(flight_cam.front.z, speed);
      _flight_move_forward(speed, vspeed);
    }
    break;
  default:
    break;
  }
}
