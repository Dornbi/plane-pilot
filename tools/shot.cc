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
//   - keys_wait_release() deliberately keeps calling the plain keyb_poll(), so
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

struct shot_step_t {
  uint16_t polls;
  uint8_t key;
};

// Where the aeroplane is put and which way it points.
//
// Positions are in world units, 8.8 fixed, and 0x80 is half a unit. x is
// north and y is west. c64o/world_map.cc is a table of 16 rows of x by 32
// columns of y at eight units to a cell, and world.cc rounds rather than
// truncates when it indexes that table - so the cell in row r, column c is
// *centred* on (8r, 8c) and reaches four units either side of it, and the
// world repeats every 16 cells of x and 32 of y. The runway in row 4,
// column 8 is therefore centred on (32, 64); its own vertices put it across
// x 28..36 at y 63..64, which is the strip the runway 1 waypoint sits in the
// middle of. To photograph a thing, stand a few units south of it at its own
// y and look north.
//
// Altitudes are on the same scale, where 0x0200 is 1000 ft and the ground is
// at 0x0020 - the 62 ft of ground clearance kFlightMinEyeZ describes.
//
// Angles are binary degrees: 256 to the turn, so 0x40 is a quarter turn.
// yaw 0 looks north along +x and 0x40 looks west along +y, pitch is nose up,
// roll is right wing down.
struct shot_pose_t {
  uint16_t x;
  uint16_t y;
  uint16_t z;
  uint8_t yaw;
  int8_t pitch;
  int8_t roll;
  int16_t speed;
  uint8_t throttle;
  bool gear;
  bool flap;
};

// Cruise and approach, the two the poses below are flown at. Speed is the
// model's own scale, where kMaxSpeed is 0x0F00 and the stall is around 0x0400.
#define SHOT_CRUISE 0x0900, 0x14, false, false
#define SHOT_APPROACH 0x0480, 0x06, true, true

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
#else

// --- The posed scenes ------------------------------------------------------
//
// All of them start mission 04 and then throw its start state away. It is the
// mission that begins airborne at cruise, which is the only thing about it
// that matters here: the aeroplane has to be off the ground before the pose
// lands, or the model still believes it is rolling down a runway.

#if __SHOT__ == 3
// Short final for runway 1: eight units south of the threshold, lined up on
// the middle of the strip at y 63.5, a few hundred feet up. Gear and flaps
// down, because that is what the lamps on the panel are for.
//
// Seven of bank, about ten degrees, to the left. A dead level horizon reads
// as a diagram rather than as an aeroplane, and ten degrees is a lineup
// correction on final rather than a manoeuvre - enough for the picture to
// tilt and for the artificial horizon to have something to say. The two
// scenes lean opposite ways on purpose: this one and the lake are the pair
// the README shows together, and a matched pair of identical tilts looks
// like a mistake.
static const shot_pose_t kShotPose = {0x1800, 0x3F80, 0x0080, 0,     -3,
                                      -7,     SHOT_APPROACH};
#elif __SHOT__ == 4
// The same country as the approach shot is flown over, but a different thing
// in it and from the other side: north-east of the city in row 2, looking
// west along the forest band that runs down the middle of the map, in about
// 55 degrees of bank.
//
// West is where the sun is, which is the whole reason this one looks that
// way - it is the only heading the sun is on. The cloud below it is not
// luck either: cloud groups are placed by a hash of the cell they sit in, so
// whether one is in shot is a property of *where the camera stands*, not of
// the altitude or the weather. At (24, 92) there is one to the west; four
// units south there is not. If this scene ever loses its cloud, move the
// camera a unit or two rather than reaching for the cloud constants.
static const shot_pose_t kShotPose = {0x1800, 0x5C00, 0x0380, 0x40, -2,
                                      40,     SHOT_CRUISE};
#elif __SHOT__ == 6
// The debug view wants a scene with something in it to account for, not an
// empty green plain: the counters are the subject and PLY, CLD and GRD only
// mean anything when there are polygons, clouds and terrain to draw. This
// looks north at the town in row 13, column 19, leaning left for the same
// reason the other two lean - a level horizon looks like a test pattern.
static const shot_pose_t kShotPose = {0x6000, 0x9800, 0x0140, 0,    -4,
                                      -6,     SHOT_CRUISE};
#elif __SHOT__ == 7
// The lake in row 11, column 2, with the water and reed dots that surround it
// spread across the near ground. The scenic one, banked right - the opposite
// way to the approach shot above.
static const shot_pose_t kShotPose = {0x5000, 0x1000, 0x0100, 0,    -4,
                                      9,      SHOT_CRUISE};
#else
#error "build with -D__SHOT__=<scene number>"
#endif

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

// cos(a) * 256 for the first quarter turn, a = 0..64 in binary degrees. The
// other three quarters are the same numbers with the sign and the direction of
// travel flipped, which _shot_cos() below does rather than storing them.
static const int16_t kShotCos[65] = {
    256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245,
    243, 241, 239, 237, 234, 231, 229, 226, 223, 220, 216, 213, 209,
    206, 202, 198, 194, 190, 185, 181, 177, 172, 167, 162, 157, 152,
    147, 142, 137, 132, 126, 121, 115, 109, 104, 98,  92,  86,  80,
    74,  68,  62,  56,  50,  44,  38,  31,  25,  19,  13,  6,   0,
};

static int16_t _shot_cos(uint8_t a) {
  if (a < 64) {
    return kShotCos[a];
  }
  if (a < 128) {
    return -kShotCos[128 - a];
  }
  if (a < 192) {
    return -kShotCos[a - 128];
  }
  return kShotCos[256 - (uint16_t)a];
}

static int16_t _shot_sin(uint8_t a) { return _shot_cos((uint8_t)(a - 64)); }

// 8.8 by 8.8. Runs once, off the render path, so the general 32-bit form is
// fine here where vec_asm.cc would not be.
static int16_t _shot_mul(int16_t a, int16_t b) {
  return (int16_t)(((int32_t)a * (int32_t)b) >> 8);
}

static void _shot_apply_pose(void) {
  flight_eye_x = (int32_t)kShotPose.x << 8;
  flight_eye_y = (int32_t)kShotPose.y << 8;
  flight_eye_z = (int32_t)kShotPose.z << 8;

  const int16_t cy = _shot_cos(kShotPose.yaw);
  const int16_t sy = _shot_sin(kShotPose.yaw);
  const int16_t cp = _shot_cos((uint8_t)kShotPose.pitch);
  const int16_t sp = _shot_sin((uint8_t)kShotPose.pitch);

  // Yaw about up, then pitch about left. Written out rather than composed from
  // vec.h's rotation steps, which are one control input each and would need a
  // loop and a rounding argument to reach a named angle.
  flight_cam.front.x = _shot_mul(cp, cy);
  flight_cam.front.y = _shot_mul(cp, sy);
  flight_cam.front.z = sp;
  flight_cam.left.x = -sy;
  flight_cam.left.y = cy;
  flight_cam.left.z = 0;
  flight_cam.up.x = -_shot_mul(sp, cy);
  flight_cam.up.y = -_shot_mul(sp, sy);
  flight_cam.up.z = cp;

  if (kShotPose.roll != 0) {
    const int16_t cr = _shot_cos((uint8_t)kShotPose.roll);
    const int16_t sr = _shot_sin((uint8_t)kShotPose.roll);
    const vec3_t l = flight_cam.left;
    const vec3_t u = flight_cam.up;
    flight_cam.left.x = _shot_mul(l.x, cr) - _shot_mul(u.x, sr);
    flight_cam.left.y = _shot_mul(l.y, cr) - _shot_mul(u.y, sr);
    flight_cam.left.z = _shot_mul(l.z, cr) - _shot_mul(u.z, sr);
    flight_cam.up.x = _shot_mul(u.x, cr) + _shot_mul(l.x, sr);
    flight_cam.up.y = _shot_mul(u.y, cr) + _shot_mul(l.y, sr);
    flight_cam.up.z = _shot_mul(u.z, cr) + _shot_mul(l.z, sr);
  }

  flight_speed = kShotPose.speed;
  flight_throttle = kShotPose.throttle;
  flight_gear = kShotPose.gear;
  flight_flap = kShotPose.flap;
  flight_paused = true;
}

#endif

static const uint8_t kShotSteps = sizeof(kShotScript) / sizeof(kShotScript[0]);

static uint8_t shot_idx = 0;
static uint16_t shot_left = 0;

void shot_poll(void) {
  keyb_poll();

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
#if __SHOT__ != 1 && __SHOT__ != 2 && __SHOT__ != 5
  } else if (key == SHOT_POSE) {
    _shot_apply_pose();
#endif
  }

  if (--shot_left == 0) {
    ++shot_idx;
  }
}
