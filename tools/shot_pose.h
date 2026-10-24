#ifndef SHOT_POSE_H
#define SHOT_POSE_H

// The posed scenes of tools/shot.cc: where the aeroplane is put, which way it
// points, and the arithmetic that turns that into the model's eye and camera
// matrix.
//
// Pure integer code with no target dependencies, so that it compiles both
// into shot.cc on the C64 and into tools/shot_bake.cc on the host. The host
// copy exists for tools/bench.sh: fpilot has no room left for this code and
// its 32-bit multiply, so the bench bakes the result into shot_baked.h and
// builds with -D__SHOT_BAKED__, which leaves out everything below that only
// computes it. Same integer arithmetic either way, so the same pose.

#include <stdint.h>

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

// The model state a pose comes to: the eye, and the camera's front, left and
// up rows in that order.
struct shot_cam_t {
  int32_t eye[3];
  int16_t cam[9];
};

// Cruise and approach, the two the poses below are flown at. Speed is the
// model's own scale, where kMaxSpeed is 0x0F00 and the stall is around 0x0400.
#define SHOT_CRUISE 0x0900, 0x14, false, false
#define SHOT_APPROACH 0x0480, 0x06, true, true

#if __SHOT__ == 3 || __SHOT__ == 4 || __SHOT__ == 6 || __SHOT__ == 7
#define SHOT_POSED
#endif

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
#endif

#if defined(SHOT_POSED) && !defined(__SHOT_BAKED__)

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

static void shot_pose_compute(shot_cam_t *c) {
  c->eye[0] = (int32_t)kShotPose.x << 8;
  c->eye[1] = (int32_t)kShotPose.y << 8;
  c->eye[2] = (int32_t)kShotPose.z << 8;

  const int16_t cy = _shot_cos(kShotPose.yaw);
  const int16_t sy = _shot_sin(kShotPose.yaw);
  const int16_t cp = _shot_cos((uint8_t)kShotPose.pitch);
  const int16_t sp = _shot_sin((uint8_t)kShotPose.pitch);

  // Yaw about up, then pitch about left. Written out rather than composed from
  // vec.h's rotation steps, which are one control input each and would need a
  // loop and a rounding argument to reach a named angle.
  int16_t *front = c->cam, *left = c->cam + 3, *up = c->cam + 6;
  front[0] = _shot_mul(cp, cy);
  front[1] = _shot_mul(cp, sy);
  front[2] = sp;
  left[0] = -sy;
  left[1] = cy;
  left[2] = 0;
  up[0] = -_shot_mul(sp, cy);
  up[1] = -_shot_mul(sp, sy);
  up[2] = cp;

  if (kShotPose.roll != 0) {
    const int16_t cr = _shot_cos((uint8_t)kShotPose.roll);
    const int16_t sr = _shot_sin((uint8_t)kShotPose.roll);
    // Static for the same reason as shot.cc's copy of the result: ppilot's
    // stack segment has no room for them.
    static int16_t l[3], u[3];
    for (uint8_t i = 0; i < 3; ++i) {
      l[i] = left[i];
      u[i] = up[i];
    }
    for (uint8_t i = 0; i < 3; ++i) {
      left[i] = _shot_mul(l[i], cr) - _shot_mul(u[i], sr);
      up[i] = _shot_mul(u[i], cr) + _shot_mul(l[i], sr);
    }
  }
}

#endif

#endif
