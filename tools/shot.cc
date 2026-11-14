// Scripted keyboard input and hardcoded poses, for capturing the screenshots
// in screens/.
//
// This file is never part of a real build. tools/make_shots.sh copies c64o/ to
// a scratch directory, drops this in beside it, and redirects the keyboard
// poll of the three screen loops (menu, help, flight) at shot_poll() below.
//
// Why it has to exist at all: the game scans the key matrix directly, so
// VICE's -keybuf - which fills the BASIC keyboard buffer - cannot drive it,
// and nothing else reaches the matrix headlessly. shot_poll() polls the real
// matrix and then clears the bits of whatever key the script says is held, so
// every key_pressed() call site sees a press without knowing anything about
// this file. See docs/emulator.md.
//
// Scripts are measured in polls of whichever loop is running, not in cycles:
// the menu and the help screen poll at 50 Hz, the flight loop at whatever it
// renders at (~7 Hz). Two consequences worth knowing before editing one:
//
//   - keys_wait_release() deliberately keeps calling the plain keys_poll(), so
//     a one-poll scripted press releases itself on the next iteration. That is
//     what lets {1, KSCAN_M} open the map instead of opening it and having the
//     still-held key close it again on the next pass.
//   - A key only fires once per press where the game edge-detects it, so
//     stepping the menu cursor down three rows is three separate one-poll
//     presses with a gap between them, not one press held for three polls.
//
// The scenes that photograph the world do not fly there. SHOT_POSE writes a
// position and an attitude straight into the model and freezes it, which is
// the only way to point the aeroplane at a particular thing on the map: the
// controls are the only steering there is, every mission starts pointing north
// from a fixed place, and flying somewhere by holding keys for a scripted
// number of polls puts the camera wherever the aerodynamics felt like by the
// time the script ran out. Scene numbers are chosen with -D__SHOT__=n and pair
// with the table in tools/make_shots.sh, which owns the capture point and the
// output name.

#include "shot.h"

#include "flight.h"
#ifdef SHOT_MSG
#include "msg.h"
#endif
#ifdef __ENABLE_COLOR_SCHEMES__
#include <string.h>

#include "box.h"
#include "mission.h"
#include "render.h"
#include "vic.h"
#endif
#include "vec.h"
#if __SHOT__ >= 18 && __SHOT__ <= 21
#include "gun.h"
#include "opponent.h"
#include "roll.h"
#include "world.h"
#endif

// Script entries: hold `key` for `polls` polls of whichever loop is running.
// `key` is a KeyScanCode, or one of the pseudo-keys below.
#define SHOT_IDLE 0xff
// Freezes the flight model directly rather than through the P key, so the
// frame holds still for the capture with no PAUSED banner over the viewport.
#define SHOT_FREEZE 0xfe
// Places the aeroplane at kShotPose and freezes it there.
#define SHOT_POSE 0xfd
// Scores a hit on fpilot's first opponent, as a round of the player's would.
#define SHOT_STRIKE 0xfc
// Puts SHOT_SCHEME's colours in place of the mission's; see below.
#define SHOT_RECOLOR 0xfb
#if __SHOT__ >= 18 && __SHOT__ <= 21
#define SHOT_TURN_WEST 0xfa
#define SHOT_TURN_BANK 0xf9
#define SHOT_WAIT_APPROACH 0xf8
#define SHOT_WAIT_TURN_COMBAT 0xf7
#define SHOT_WAIT_TRACER 0xf6
#define SHOT_PASSIVE_TURN_COMBAT 0xf5
#define SHOT_DEBRIEF_FIGHT 0xf4
#endif

#ifdef __ENABLE_COLOR_SCHEMES__
// A posed scene is a picture of a place, and the map one of the map, not of
// mission 04, so they are in Earth's colours - what the screens/ pictures
// have always shown - unless SHOT_SCHEME picks another
// (docs/color_schemes.md). $D021 is rewritten every frame from the scheme
// and the map sets its own; the two ground registers are set only at entry.
#ifndef SHOT_SCHEME
#if __SHOT__ == 14
#define SHOT_SCHEME 1
#elif __SHOT__ == 15
#define SHOT_SCHEME 2
#elif __SHOT__ == 16 || __SHOT__ == 17
#define SHOT_SCHEME 3
#else
#define SHOT_SCHEME 0
#endif
#endif
static void _shot_apply_scheme(void) {
  memcpy(&color_scheme, &kColorSchemes[SHOT_SCHEME], sizeof(ColorScheme));
  box_invalidate();
  // As _start_mission() does: the colour buffer's sky is the old scheme's.
  render_sky_rows = 0;
  vic.color_back1 = COLOR_GROUND;
  vic.color_back2 = COLOR_GRNDOBJ;
}
#endif

struct shot_step_t {
  uint16_t polls;
  uint8_t key;
};

#include "shot_pose.h"
#if __SHOT__ == 10
#include "opponent.h"
#endif
#ifdef __SHOT_BAKED__
// kShotCam, computed on the host by tools/shot_bake.cc; see shot_pose.h.
#include "shot_baked.h"
#endif

#if __SHOT__ >= 18 && __SHOT__ <= 21
static vec3_t _shot_v;
static int32_t _shot_p[3];
static bool _turn_pull_tick;
#endif
#if __SHOT__ == 19
static int16_t _prev_x[kOpponentMax];
static int16_t _prev_y[kOpponentMax];
static bool _prev_valid[kOpponentMax];
#endif

#if __SHOT__ == 18 || __SHOT__ == 20
// Maintain an upright bank using world roll_angle (VIEW_CENTER: 4..5 is ~27 deg
// right bank; VIEW_BACK: 55..56 is mirrored ~27 deg bank).
static void _shot_maintain_right_bank(uint8_t min_ra, uint8_t max_ra,
                                      int8_t target_fz) {
  if (flight_cam.up.z < 170) {
    if (flight_cam.left.z >= 0) {
      keyb_matrix[KSCAN_J >> 3] &= ~kBit[KSCAN_J & 7];
    } else {
      keyb_matrix[KSCAN_L >> 3] &= ~kBit[KSCAN_L & 7];
    }
  } else if (roll_angle >= 30) {
    if (min_ra >= 45) {
      if (roll_angle > max_ra) {
        keyb_matrix[KSCAN_J >> 3] &= ~kBit[KSCAN_J & 7];
      } else if (roll_angle < min_ra) {
        keyb_matrix[KSCAN_L >> 3] &= ~kBit[KSCAN_L & 7];
      }
    } else {
      keyb_matrix[KSCAN_L >> 3] &= ~kBit[KSCAN_L & 7];
    }
  } else {
    if (min_ra >= 45) {
      // Wrap directly 0 -> 59 -> ... instead of crossing through 15..45.
      keyb_matrix[KSCAN_J >> 3] &= ~kBit[KSCAN_J & 7];
    } else if (roll_angle < min_ra) {
      keyb_matrix[KSCAN_L >> 3] &= ~kBit[KSCAN_L & 7];
    } else if (roll_angle > max_ra) {
      keyb_matrix[KSCAN_J >> 3] &= ~kBit[KSCAN_J & 7];
    }
  }
  _turn_pull_tick = !_turn_pull_tick;
  if (_turn_pull_tick) {
    if (flight_cam.front.z < target_fz - 6) {
      keyb_matrix[KSCAN_K >> 3] &= ~kBit[KSCAN_K & 7];
    } else if (flight_cam.front.z > target_fz + 12) {
      keyb_matrix[KSCAN_I >> 3] &= ~kBit[KSCAN_I & 7];
    }
  }
}
#endif

#if __SHOT__ == 19
// In VIEW_RIGHT, world_cam.left == flight_cam.front, so pitch tilts the
// horizon diagonally (roll_angle 4..5 == ~24..30 deg) while slight left bank
// raises the view above the horizon.
static void _shot_maintain_side_tilt(uint8_t min_ra, uint8_t max_ra) {
  if (flight_cam.left.z > -25) {
    keyb_matrix[KSCAN_J >> 3] &= ~kBit[KSCAN_J & 7];
  } else if (flight_cam.left.z < -55) {
    keyb_matrix[KSCAN_L >> 3] &= ~kBit[KSCAN_L & 7];
  }
  if (roll_angle >= 30 || roll_angle < min_ra) {
    if (flight_cam.front.z < 140) {
      keyb_matrix[KSCAN_K >> 3] &= ~kBit[KSCAN_K & 7];
    }
  } else if (roll_angle > max_ra) {
    keyb_matrix[KSCAN_I >> 3] &= ~kBit[KSCAN_I & 7];
  }
}
#endif

#if __SHOT__ >= 18 && __SHOT__ <= 20
static bool _shot_enemy_in_view(int16_t min_dist, int16_t max_dist,
                                bool want_profile, bool tight) {
  bool found = false;
  for (uint8_t i = 0; i < kOpponentMax; ++i) {
    if (!opponent_target(i, _shot_p)) {
#if __SHOT__ == 19
      _prev_valid[i] = false;
#endif
      continue;
    }
#if __SHOT__ == 19
    int16_t wx = (int16_t)(_shot_p[0] >> 8);
    int16_t wy = (int16_t)(_shot_p[1] >> 8);
    int16_t vx = _prev_valid[i] ? (wx - _prev_x[i]) : 0;
    int16_t vy = _prev_valid[i] ? (wy - _prev_y[i]) : 0;
    _prev_x[i] = wx;
    _prev_y[i] = wy;
    bool had_prev = _prev_valid[i];
    _prev_valid[i] = true;
#endif

    int32_t dx = (_shot_p[0] - flight_eye_x) >> 8;
    int32_t dy = (_shot_p[1] - flight_eye_y) >> 8;
    int32_t dz = (_shot_p[2] - flight_eye_z) >> 8;
    if (dx > 1600 || dx < -1600 || dy > 1600 || dy < -1600 ||
        dz > 1600 || dz < -1600) {
      continue;
    }
    _shot_v.x = (int16_t)dx;
    _shot_v.y = (int16_t)dy;
    _shot_v.z = (int16_t)dz;
    vec_transform_inv(&world_cam, &_shot_v, &_shot_v);
    int16_t cx = _shot_v.x;
    if (cx < min_dist || cx > max_dist) {
      continue;
    }
    int16_t cy = _shot_v.y < 0 ? -_shot_v.y : _shot_v.y;
    int16_t cz = _shot_v.z < 0 ? -_shot_v.z : _shot_v.z;
    int16_t maxy = tight ? (cx >> 2) + (cx >> 4) : (cx >> 1);
    int16_t maxz = tight ? (cx >> 3) + (cx >> 4)
                         : ((__SHOT__ == 20) ? ((cx >> 3) + (cx >> 4))
                                             : (cx >> 2));
    if (cy >= maxy || cz >= maxz) {
      continue;
    }
#if __SHOT__ == 18
    if (_shot_v.z < -(cx >> 5) || _shot_v.z > (cx >> 3)) {
      continue;
    }
#elif __SHOT__ == 20
    if (cy < (cx >> 4)) {
      continue;
    }
#endif
#if __SHOT__ == 19
    if (want_profile && had_prev && (vx != 0 || vy != 0)) {
      _shot_v.x = vx;
      _shot_v.y = vy;
      _shot_v.z = 0;
      vec_transform_inv(&world_cam, &_shot_v, &_shot_v);
      int16_t vcx = _shot_v.x < 0 ? -_shot_v.x : _shot_v.x;
      int16_t vcy = _shot_v.y < 0 ? -_shot_v.y : _shot_v.y;
      if ((vcy << 1) <= vcx) {
        continue;
      }
    }
#else
    (void)want_profile;
#endif
    found = true;
  }
  return found;
}
#endif

#if __SHOT__ == 1
// The menu, with the title flyby running. Nothing to press.
static const shot_step_t kShotScript[] = {
    {1, SHOT_IDLE},
};
#elif __SHOT__ == 2
// The help screen, opened from the menu with H and then left up.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_H},
};
#elif __SHOT__ == 5 || __SHOT__ == 17
// The map. This one does fly: the breadcrumb trail is the point of the
// picture and there is no way to fake one, so it takes mission 04 - the only
// start that is already airborne at cruise - flies it unattended for a minute
// and a half with one brief roll partway so the trail has a bend in it, and
// then presses M. The map is modal and freezes the simulation under it, so
// there is nothing to hold still afterwards.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {200, SHOT_IDLE},
    {4, KSCAN_L},
    {300, SHOT_IDLE},
    {1, SHOT_RECOLOR},
    {1, KSCAN_M},
};
#elif __SHOT__ == 8
// The start of the first mission, frozen there. Not a screenshot:
// tools/bench.sh times it. In ppilot that is parked on runway 1, whose
// polygon filling the lower viewport is the single most expensive one in the
// game (docs/framerate.md); in fpilot it is 01 Recon, airborne.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {1, SHOT_FREEZE},
};
#elif __SHOT__ == 9
// Mission 04, Scramble, flown live rather than frozen, with the guns going:
// parked on runway 1 while both opponents overtake alongside it and fly on
// north, which brings them into view from behind, near enough to be drawn
// at size and redrawn as they go. Not a screenshot: tools/bench.sh times it,
// as the one scene where the opponents' model steps, their silhouettes'
// projection and fill, the rounds and their tracers all cost something.
// Space is held only after a few polls because gun.cc fires once the trigger
// has been released since the mission began.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {4, SHOT_IDLE},
    {4000, KSCAN_SPACE},
};
#elif __SHOT__ == 10
// Scene 9 with the first opponent shot down as it overtakes: five hits
// over the first half of its pass, a round's worth of smoke after each of
// the first four and the long trail of a downed aircraft after the fifth, so
// that the smoke, the hit and downing logic, the fall and the stack's room
// for the smoke are in the frames tools/bench.sh times. The hits are scored
// directly (opponent_strike()), as the parked aeroplane's rounds fly level
// and pass under opponents 48 m up; the guns still fire throughout, released
// for the poll of each hit. Frames 77, 89, 101, 113 and 125 of the mission,
// give or take one: 12 apart, so that it is down by the middle of the timed
// window, as it was with three hits 24 apart.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {4, SHOT_IDLE},
    {72, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {11, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {11, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {11, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {11, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {4000, KSCAN_SPACE},
};
#elif __SHOT__ == 11 || __SHOT__ == 12
// Mission 05, Head on, flown live and hands off: straight on at cruise while
// the two hunters come in from 4.6 km, meet the player head-on at about
// frame 280 and turn after it. Scene 11 looks ahead, at the approach; scene
// 12 holds the back view from frame 300, at the chase. tools/bench.sh times
// both: the hunters' manoeuvres and redraws against scene 9's pass.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
#if __SHOT__ == 12
    {300, SHOT_IDLE},
    {4000, KSCAN_4},
#endif
};
#elif __SHOT__ == 13
// Mission 04 live and parked on runway 1, hands off, looking ahead: the pair
// overtake, fly on north to their waypoints ~2.5 km past the runway, turn
// back at about frame 525 and come in to strafe from about frame 1000.
// tools/bench.sh times the runs.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
};
#elif __SHOT__ == 18
// fpilot 05 HEAD ON: bank right early as hunters close inside ~700m so we are
// in an upright ~27 deg right turn as they cross above the crosshair, then fire.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {180, SHOT_WAIT_APPROACH},
    {4, KSCAN_A},
    {10, SHOT_TURN_BANK},
    {220, SHOT_WAIT_TURN_COMBAT},
    {16, SHOT_WAIT_TRACER},
    {1, SHOT_FREEZE},
};
#elif __SHOT__ == 19
// fpilot 05 HEAD ON: break left, switch to VIEW_RIGHT (3) tilted 24..30 deg,
// capture an enemy fighter banking dynamically in profile.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {220, SHOT_WAIT_APPROACH},
    {34, SHOT_TURN_WEST},
    {1, KSCAN_3},
    {300, SHOT_PASSIVE_TURN_COMBAT},
    {1, SHOT_FREEZE},
};
#elif __SHOT__ == 20
// fpilot 10 COMBAT PATROL: drop flaps so hunters close behind VIEW_BACK (4),
// then roll into an upright ~27 deg turn (mirrored roll_angle 55..56).
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {1, KSCAN_F},
    {1, KSCAN_4},
    {280, SHOT_WAIT_APPROACH},
    {14, SHOT_TURN_BANK},
    {220, SHOT_PASSIVE_TURN_COMBAT},
    {1, SHOT_FREEZE},
};
#elif __SHOT__ == 21
// fpilot 03 AMBUSH: drop flaps for slow strafe pass, shoot both parked
// aircraft down until GOALS: COMPLETED, then Q to MISSION DEBRIEF.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_K}, {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {1, KSCAN_F},
    {1200, SHOT_DEBRIEF_FIGHT},
    {20, SHOT_IDLE},
    {1, KSCAN_Q},
    {100, SHOT_IDLE},
};
#elif defined(SHOT_POSED)

// --- The posed scenes ------------------------------------------------------
//
// All of them start mission 04 and then throw its start state away; the pose
// itself is in shot_pose.h. In ppilot that is the mission that begins
// airborne at cruise, which matters: the aeroplane has to be off the ground
// before the pose lands, or the model still believes it is rolling down a
// runway. fpilot's 04 starts parked, but the scenes it is posed for
// (tools/bench.sh) are frozen and never step the model.

static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
#if __SHOT__ == 6
    // The debug view is switched on before the pose, so that the frame the
    // counters describe is the frame in the picture.
    {1, KSCAN_D},
#endif
    {1, SHOT_POSE},
#ifdef SHOT_BACK
    // -DSHOT_BACK: held 4, the back view, with the tail fin in it, to the
    // capture and past it.
    {30000, KSCAN_4},
#endif
};

static void _shot_apply_pose(void) {
#ifdef __SHOT_BAKED__
  const shot_cam_t *c = &kShotCam;
#else
  // Static, as ppilot's stack segment has no room for it.
  static shot_cam_t computed;
  shot_pose_compute(&computed);
  const shot_cam_t *c = &computed;
#endif
  flight_eye_x = c->eye[0];
  flight_eye_y = c->eye[1];
  flight_eye_z = c->eye[2];
  flight_cam.front.x = c->cam[0];
  flight_cam.front.y = c->cam[1];
  flight_cam.front.z = c->cam[2];
  flight_cam.left.x = c->cam[3];
  flight_cam.left.y = c->cam[4];
  flight_cam.left.z = c->cam[5];
  flight_cam.up.x = c->cam[6];
  flight_cam.up.y = c->cam[7];
  flight_cam.up.z = c->cam[8];

  flight_speed = kShotPose.speed;
  flight_throttle = kShotPose.throttle;
  flight_gear = kShotPose.gear;
  flight_flap = kShotPose.flap;
  flight_paused = true;

#ifdef __ENABLE_COLOR_SCHEMES__
  _shot_apply_scheme();
#endif
#ifdef SHOT_MSG
  // -DSHOT_MSG: a message on the line, held, to see it in a scheme.
  msg_show(kMissionTitles[0], 0, true);
#endif
}

#else
#error "build with -D__SHOT__=<scene number>"
#endif

static const uint8_t kShotSteps = sizeof(kShotScript) / sizeof(kShotScript[0]);

static uint8_t shot_idx = 0;
static uint16_t shot_left = 0;

void shot_poll(void) {
  keys_poll();

  if (shot_idx >= kShotSteps) {
    return;
  }
  if (shot_left == 0) {
    shot_left = kShotScript[shot_idx].polls;
  }

  const uint8_t key = kShotScript[shot_idx].key;
  if (key < 64) {
    // The matrix is active low: a cleared bit is a key that is down.
    keyb_matrix[key >> 3] &= ~kBit[key & 7];
  } else if (key == SHOT_FREEZE) {
    flight_paused = true;
#ifdef SHOT_POSED
  } else if (key == SHOT_POSE) {
    _shot_apply_pose();
#endif
#ifdef __ENABLE_COLOR_SCHEMES__
  } else if (key == SHOT_RECOLOR) {
    _shot_apply_scheme();
#endif
#if __SHOT__ == 10
  } else if (key == SHOT_STRIKE) {
    opponent_strike(0);
#endif
#if __SHOT__ == 18 || __SHOT__ == 19 || __SHOT__ == 20
  } else if (key == SHOT_WAIT_APPROACH) {
#if __SHOT__ == 20
    if (_shot_enemy_in_view(120, 460, false, false)) {
      shot_left = 1;
    }
#else
    int16_t max_d = (__SHOT__ == 18) ? 1400 : 620;
    if (_shot_enemy_in_view(120, max_d, false, false)) {
      shot_left = 1;
    }
#endif
#endif
#if __SHOT__ == 19
  } else if (key == SHOT_TURN_WEST) {
    _shot_maintain_side_tilt(4, 5);
    if (flight_cam.front.y >= 175) {
      shot_left = 1;
    } else {
      keyb_matrix[KSCAN_A >> 3] &= ~kBit[KSCAN_A & 7];
    }
#endif
#if __SHOT__ == 18 || __SHOT__ == 20
  } else if (key == SHOT_TURN_BANK) {
#if __SHOT__ == 20
    _shot_maintain_right_bank(55, 56, -4);
#else
    _shot_maintain_right_bank(4, 5, -4);
#endif
#endif
#if __SHOT__ == 18
  } else if (key == SHOT_WAIT_TURN_COMBAT) {
    _shot_maintain_right_bank(4, 5, -4);
    bool fallback = (shot_left <= 40);
    if (roll_angle >= 3 && roll_angle <= 6 &&
        _shot_enemy_in_view(75, fallback ? 480 : 340, false, !fallback)) {
      shot_left = 1;
    }
  } else if (key == SHOT_WAIT_TRACER) {
    _shot_maintain_right_bank(4, 5, -4);
    keyb_matrix[KSCAN_SPACE >> 3] &= ~kBit[KSCAN_SPACE & 7];
    if (gun_tracer_up()) {
      flight_paused = true;
      shot_idx = kShotSteps;
      return;
    }
#endif
#if __SHOT__ == 19 || __SHOT__ == 20
  } else if (key == SHOT_PASSIVE_TURN_COMBAT) {
#if __SHOT__ == 19
    _shot_maintain_side_tilt(4, 5);
    bool ok_tilt =
        (roll_angle >= 3 && roll_angle <= 7 && flight_cam.up.z >= 170);
    if (ok_tilt &&
        ((gun_tracer_up() && _shot_enemy_in_view(75, 480, true, false)) ||
         (shot_left <= 180 && _shot_enemy_in_view(75, 480, true, false)) ||
         (shot_left <= 80 && _shot_enemy_in_view(65, 620, false, false)))) {
      flight_paused = true;
      shot_idx = kShotSteps;
      return;
    }
#else
    _shot_maintain_right_bank(55, 56, -4);
    bool ok_tilt = (roll_angle >= 53 && roll_angle <= 58);
    if (ok_tilt &&
        ((gun_tracer_up() && _shot_enemy_in_view(55, 220, false, false)) ||
         (shot_left <= 180 && _shot_enemy_in_view(55, 150, false, false)) ||
         (shot_left <= 60 && _shot_enemy_in_view(55, 340, false, false)))) {
      flight_paused = true;
      shot_idx = kShotSteps;
      return;
    }
#endif
#endif
#if __SHOT__ == 21
  } else if (key == SHOT_DEBRIEF_FIGHT) {
    if (flight_current_wp > 0) {
      shot_left = 1;
    } else {
      bool has_tgt = opponent_target(0, _shot_p);
      if (!has_tgt) {
        has_tgt = opponent_target(1, _shot_p);
      }
      if (has_tgt) {
        int32_t dx = (_shot_p[0] - flight_eye_x) >> 8;
        int32_t dy = (_shot_p[1] - flight_eye_y) >> 8;
        int32_t dz = (_shot_p[2] - flight_eye_z) >> 8;
        if (dx <= 1600 && dx >= -1600 && dy <= 1600 && dy >= -1600 &&
            dz <= 1600 && dz >= -1600) {
          _shot_v.x = (int16_t)dx;
          _shot_v.y = (int16_t)dy;
          _shot_v.z = (int16_t)dz;
          vec_transform_inv(&flight_cam, &_shot_v, &_shot_v);
          _turn_pull_tick = !_turn_pull_tick;
          if (_shot_v.x <= 60) {
            if (flight_eye_z < 0x030000L || flight_cam.front.z < 12) {
              keyb_matrix[KSCAN_K >> 3] &= ~kBit[KSCAN_K & 7];
            }
            keyb_matrix[KSCAN_A >> 3] &= ~kBit[KSCAN_A & 7];
            if (flight_cam.up.z < 170) {
              keyb_matrix[(flight_cam.left.z >= 0 ? KSCAN_J : KSCAN_L) >> 3] &=
                  ~kBit[(flight_cam.left.z >= 0 ? KSCAN_J : KSCAN_L) & 7];
            } else if (roll_angle < 4 || roll_angle >= 30) {
              keyb_matrix[KSCAN_L >> 3] &= ~kBit[KSCAN_L & 7];
            } else if (roll_angle > 5) {
              keyb_matrix[KSCAN_J >> 3] &= ~kBit[KSCAN_J & 7];
            }
          } else {
            if (flight_cam.left.z > 12) {
              keyb_matrix[KSCAN_J >> 3] &= ~kBit[KSCAN_J & 7];
            } else if (flight_cam.left.z < -12) {
              keyb_matrix[KSCAN_L >> 3] &= ~kBit[KSCAN_L & 7];
            }
            if (_shot_v.y > 10 || (_shot_v.y > 2 && _turn_pull_tick)) {
              keyb_matrix[KSCAN_A >> 3] &= ~kBit[KSCAN_A & 7];
            } else if (_shot_v.y < -10 || (_shot_v.y < -2 && _turn_pull_tick)) {
              keyb_matrix[KSCAN_S >> 3] &= ~kBit[KSCAN_S & 7];
            }
            if (flight_eye_z < 0x008000L || _shot_v.z > 10 ||
                (_shot_v.z > 2 && _turn_pull_tick)) {
              keyb_matrix[KSCAN_K >> 3] &= ~kBit[KSCAN_K & 7];
            } else if (_shot_v.z < -10 || (_shot_v.z < -2 && _turn_pull_tick)) {
              keyb_matrix[KSCAN_I >> 3] &= ~kBit[KSCAN_I & 7];
            }
            int16_t cy = _shot_v.y < 0 ? -_shot_v.y : _shot_v.y;
            int16_t cz = _shot_v.z < 0 ? -_shot_v.z : _shot_v.z;
            int16_t cone = (_shot_v.x >> 4) + 8;
            if (_shot_v.x < 950 && cy <= cone && cz <= cone) {
              keyb_matrix[KSCAN_SPACE >> 3] &= ~kBit[KSCAN_SPACE & 7];
            }
          }
        } else if (flight_eye_z < 0x018000L && flight_cam.front.z < 12) {
          keyb_matrix[KSCAN_K >> 3] &= ~kBit[KSCAN_K & 7];
        }
      } else if (flight_cam.front.z < 0) {
        keyb_matrix[KSCAN_K >> 3] &= ~kBit[KSCAN_K & 7];
      }
    }
#endif
  }

  if (--shot_left == 0) {
    ++shot_idx;
  }
}
