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
#include "vec.h"

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
#elif __SHOT__ == 5
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
// Scene 9 with the first opponent shot down as it overtakes: three hits
// spread over its pass, a round's worth of smoke after each of the first two
// and the long trail of a downed aircraft after the third, so that the
// smoke, the hit and downing logic and the stack's room for the smoke are in
// the frames tools/bench.sh times. The hits are scored directly
// (opponent_strike()), as the parked aeroplane's rounds fly level and pass
// under opponents 48 m up; the guns still fire throughout, released for the
// poll of each hit. Frames 77, 101 and 125 of the mission, give or take one.
static const shot_step_t kShotScript[] = {
    {100, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_K},      {6, SHOT_IDLE},
    {1, KSCAN_RETURN},
    {4, SHOT_IDLE},
    {72, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {23, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {23, KSCAN_SPACE},
    {1, SHOT_STRIKE},
    {4000, KSCAN_SPACE},
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
    keyb_matrix[key >> 3] &= ~(1 << (key & 7));
  } else if (key == SHOT_FREEZE) {
    flight_paused = true;
#ifdef SHOT_POSED
  } else if (key == SHOT_POSE) {
    _shot_apply_pose();
#endif
#if __SHOT__ == 10
  } else if (key == SHOT_STRIKE) {
    opponent_strike(0);
#endif
  }

  if (--shot_left == 0) {
    ++shot_idx;
  }
}
