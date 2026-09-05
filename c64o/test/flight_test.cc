#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../flight.h"
#include "../fmath.h"
#include "../msg.h"
#include "../vec.h"


// Stubs for msg.cc dependencies when compiling host test
uint8_t *mem_screen_ram = nullptr;
uint8_t *mem_screen_row_ptrs[25];
uint8_t color_buffer_dummy[560];
extern uint8_t *const mem_color_buffer = color_buffer_dummy;
static uint8_t test_screen_row[40];

static void assert_msg_rendered(const char *expected) {
  memset(test_screen_row, ' ', sizeof(test_screen_row));
  msg_render();
  if (expected == nullptr || strlen(expected) == 0) {
    for (int i = 0; i < 40; ++i) {
      assert(test_screen_row[i] == ' ');
    }
  } else {
    uint8_t len = (uint8_t)strlen(expected);
    if (len > 40) len = 40;
    uint8_t col = (40 - len) >> 1;
    assert(memcmp(test_screen_row + col, expected, len) == 0);
  }
}

// Mirrors of the constants inside flight.cc. They are static there, so the
// tests restate them; if one of these drifts the tests below are wrong rather
// than merely failing, so keep them in step.
#ifdef __FLIGHT_AOA__
// The stall angle, and the camber that pairs with it. These are the wing.
static const int16_t kAlphaStall = 56;
static const int16_t kCamberCl = 128;
#else
// The attitude the arcade model's rotation drives to, mirroring flight.cc.
static const int16_t kRotatePitchZ = 47;
#endif
// The speeds that angle works out at, clean and with flaps. They are no longer
// constants in the model - nothing in flight.cc is told either number - but
// the tests still read against them, and that they come out at the values the
// old model had to be given is the headline result. Upright C_L max is the
// peak plus the camber, and 56 + 128 is what holds this at 0x0400 exactly.
static const uint16_t kStallSpeedWithoutFlaps = 0x0400;
static const uint16_t kStallSpeedWithFlaps = 0x0340;
// Airspeed at which lift reaches kTrimLift upright at sea level, i.e. where the
// lift deficit and its sink penalty vanish. See flight.md 2.4.
static const int16_t kTrimSpeed = 0x0800;
// Landing envelope.
static const int32_t kGroundZ = 0x2000;
static const int16_t kMaxLandingRoll = 32;
static const int16_t kMinLandingUpZ = 0;
static const int16_t kMinLandingPitch = -32;
static const int16_t kMaxLandingPitch = 64;
static const int16_t kMaxLandingVSpeed = -0x0120;
static const uint16_t kMaxLandingSpeed = 0x0A00;
static const uint16_t kMaxGroundSpeed = 0x0D00;

// Holds an attitude for `frames` steps at a fixed throttle, ignoring fuel burn,
// and returns with flight_speed / flight_vspeed at the steady state. Used by
// the equilibrium tests below, which are about the trim the model settles into
// rather than about any single frame.
// Mean vertical speed over the tail of the last _settle, times 256, and
// whether it finished stalled.
//
// The mean, rather than the last frame, is what "is it holding altitude"
// has to ask now. One unit of the flight path is eight units of vertical
// speed at cruise, so an aircraft that is exactly level still reads as
// descending on some frames and climbing on others; a single-frame test picks
// up that quantization and calls it a trend.
static int32_t _settle_vmean256;
static bool _settle_stalled;

static void _settle(uint8_t throttle, int16_t pitch, int16_t up_z, int frames,
                    uint8_t flap = 0, int16_t start_speed = 0) {
  flight_init();
  flight_eye_z = 0x040000; // Well clear of both the ground and the ceiling
  flight_throttle = throttle;
  flight_fuel = 0x0FFFFFFF;
  flight_flap = flap;
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, up_z);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);
  if (start_speed > 0) {
    flight_speed = start_speed;
  }
  int16_t held_pitch = flight_cam.front.z;
  int16_t held_up_z = flight_cam.up.z;
  int32_t vsum = 0;
  const int watch = frames < 200 ? frames : 200;
  for (int i = 0; i < frames; ++i) {
    // The pilot holds the attitude; only the scalar state is under test.
    flight_cam.front.z = held_pitch;
    flight_cam.up.z = held_up_z;
    // And the altitude is held too, which is new and is the difference
    // between a steady state and a crash site. A settle that descends is now
    // ordinary - level flight needs a positive angle of attack, so any run
    // that holds the wrong pitch sinks - and at the rates the model reaches, a
    // few hundred frames is enough to arrive at the ground with the gear up.
    // The runs that did were reporting flight_status and a vertical speed of
    // zero, which reads as "settled, level" and is neither.
    //
    // Pinning it also pins the air density, which is what the comment above
    // has always meant by holding everything but the scalar state.
    flight_eye_z = 0x040000;
    flight_advance();
    flight_throttle = throttle;
    if (i >= frames - watch) {
      vsum += flight_vspeed;
    }
  }
  _settle_vmean256 = vsum * 256 / watch;
  _settle_stalled = flight_stall != 0;
}

#ifdef __FLIGHT_AOA__

// Both of these are asked only by the angle-of-attack tests, and the reason is
// the model rather than the plumbing: the arcade aeroplane trims level at
// front.z = 0 above its trim speed, so "sweep the attitudes and find the
// lowest one that holds altitude" is not a question its tests have to ask.
// Compiled into that build they are dead code, and -Wall says so.

// Two passes, the second seeded from the first's airspeed.
//
// The model has two attractors at some held attitudes: the aeroplane flying,
// and the aeroplane mushing down stalled, with the post-stall droop and the
// induced drag balancing gravity. Both are real - the stalled one is what a
// stall *is* - but which one a bench run lands in depends on the swing it
// takes getting there, and a run that pins the attitude has taken away the
// pitching break that would end the stalled one in flight.
//
// Seeding a second pass at the airspeed the first found starts it near the
// fixed point instead of hundreds of units above it, so the swing is small
// enough to stay on the flying branch where there is one. Where there is not,
// both passes stall and _settle_stalled says so.
static void _settle2(uint8_t throttle, int16_t pitch, int16_t up_z, int frames,
                     uint8_t flap = 0) {
  _settle(throttle, pitch, up_z, frames, flap);
  const int16_t seed = flight_speed;
  const bool first_stalled = _settle_stalled;
  const int32_t first_vmean = _settle_vmean256;
  _settle(throttle, pitch, up_z, frames, flap, seed);
  if (_settle_stalled && !first_stalled) {
    // The seeded pass found the worse branch; the first one is the answer.
    _settle(throttle, pitch, up_z, frames, flap);
    (void)first_vmean;
  }
}

// The lowest held pitch that holds altitude at this throttle, which is the
// question flight.md 2.1 asks of every throttle setting. False if no attitude
// in the sweep holds it.
static bool _level_trim(uint8_t throttle, int16_t up_z, uint8_t flap,
                        int16_t *out_pitch) {
  for (int16_t p = -64; p <= 200; ++p) {
    _settle2(throttle, p, up_z, 600, flap);
    if (!flight_status && !_settle_stalled && _settle_vmean256 >= 0) {
      *out_pitch = p;
      return true;
    }
  }
  return false;
}

#endif // __FLIGHT_AOA__


// Compass heading, 0..kHeadingMax-1, using the game's own routine. For
// printing; assertions compare the forward vector directly, which is both
// exact and finer grained than a 48 step compass.
static uint8_t _heading() {
  return _get_heading(flight_cam.front.x, flight_cam.front.y);
}

// True if the forward vector is bit for bit unchanged.
static bool _same_heading(int16_t fx, int16_t fy) {
  return flight_cam.front.x == fx && flight_cam.front.y == fy;
}

// Banks the aircraft the way the pilot does, by applying roll inputs. Use this
// where the point is that the attitude is one the game can actually reach;
// use the `roll` argument of _arm_touchdown where an exact left.z is needed.
// Steps from wings level, one kVecRollRight each:
//    1 -> left.z  31 (just inside kMaxLandingRoll)
//    4 -> left.z 119 (well past it)
//   26 -> up.z -256, left.z -11 (inverted, and inside the bank limit,
//         which is exactly the blind spot trigger 6 exists to cover)
static void _roll_by(int steps) {
  for (int i = 0; i < steps; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    vec_orthonormalize(&flight_cam);
  }
  for (int i = 0; i > steps; --i) {
    flight_input(FLIGHT_INPUT_ROLL_LEFT);
    vec_orthonormalize(&flight_cam);
  }
}

// Sets up a touchdown frame: places the aircraft one step above the ground
// with the given attitude and speed, so that a single flight_advance() crosses
// the ground plane and runs the envelope check. Returns the vertical speed the
// check will see.
//
// The predicted descent has to account for the sink penalty as well as the
// pitch term, otherwise the aircraft is simply parked below the ground plane
// and the clamp - not the descent - is what gets tested.
// `roll` sets an exact bank: it cannot be written straight into left.z, since
// vec_orthonormalize derives left from up x front and would discard it, so it
// is seeded through up = (0, -roll, sqrt(256^2 - roll^2)), which comes back
// out as left.z == roll. `roll_steps` additionally banks with real roll
// inputs, for cases where reaching the attitude the way the pilot does is the
// point - see _roll_by.
#ifdef __FLIGHT_AOA__
static int16_t _arm_touchdown(int16_t pitch, int16_t roll, int16_t speed,
                              uint8_t gear, int roll_steps = 0) {
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = 0x040000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = speed;
  int16_t up_z = (int16_t)sqrt(65536.0 - (double)roll * roll);
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, -roll, up_z);
  _roll_by(roll_steps);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);

  // Let the flight path catch up with the attitude before reading the sink.
  //
  // This used to be a single frame at altitude, which was enough when vertical
  // speed was front.z * speed and therefore a function of the attitude alone.
  // It is not any more: the flight path is its own state, flight_init() starts
  // it level, and one frame of a nose-down attitude produces an aeroplane
  // pointing down while still travelling horizontally - a large negative angle
  // of attack and hardly any sink. Arming a touchdown from that measures a
  // moment no approach ever passes through.
  //
  // So the attitude and the airspeed are both held while only the flight path
  // is allowed to move, which converges in a few tens of frames and leaves the
  // aircraft descending the way one that had been holding this attitude at
  // this speed really would be. The caller's airspeed is preserved exactly,
  // which is what the speed triggers need.
  mat3_t attitude = flight_cam;
  int16_t saved_speed = flight_speed;
  for (int i = 0; i < 60; ++i) {
    flight_cam = attitude;
    flight_speed = saved_speed;
    flight_eye_z = 0x040000;
    flight_advance();
  }
  int16_t saved_gamma = flight_gamma;
  flight_cam = attitude;
  flight_speed = saved_speed;
  int16_t vs = flight_vspeed;

  // Now re-arm the same state exactly one frame's descent above the ground.
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = saved_speed;
  flight_gamma = saved_gamma;
  flight_cam = attitude;
  flight_eye_z = (int32_t)kGroundZ - vs;
  return vs;
}
#else // !__FLIGHT_AOA__
static int16_t _arm_touchdown(int16_t pitch, int16_t roll, int16_t speed,
                              uint8_t gear, int roll_steps = 0) {
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = 0x040000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = speed;
  int16_t up_z = (int16_t)sqrt(65536.0 - (double)roll * roll);
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, -roll, up_z);
  _roll_by(roll_steps);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);

  // One free-flight frame at altitude tells us the vertical speed this state
  // actually produces, including the sink penalty.
  mat3_t attitude = flight_cam;
  int16_t saved_speed = flight_speed;
  flight_advance();
  int16_t vs = flight_vspeed;

  // Now re-arm the same state exactly one frame's descent above the ground.
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = saved_speed;
  flight_cam = attitude;
  flight_eye_z = (int32_t)kGroundZ - vs;
  return vs;
}
#endif // __FLIGHT_AOA__

// Puts the model genuinely into ground mode. model_on_ground is a static
// inside flight.cc that flight_init() clears, so setting flight_eye_z alone
// leaves the model airborne and every flight_input() takes the airborne
// branch. One advance at ground level is what actually sets the flag.
static void _put_on_ground(int16_t speed) {
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_gear = 1;
  flight_throttle = 0;
  // The contact frame is a touchdown as far as flight_advance is concerned, so
  // it has to pass the landing envelope. Fly it at kTrimSpeed: there the lift
  // deficit is exactly zero, so with wings level and no pitch the vertical
  // speed is zero and no trigger fires, whatever speed the caller actually
  // wants once the aircraft is on the runway.
  flight_speed = kTrimSpeed;
  flight_eye_z = kGroundZ;
  flight_advance(); // Trips the ground contact check -> model_on_ground = true
  assert(!flight_status);
  flight_speed = speed;
}

// 1. Level cruise equilibrium test.
// "Equilibrium" means the vertical speed settles to zero and the airspeed
// settles to a value that is stable frame over frame - not merely that the
// aircraft is still flying.

// 0. The stall speeds are consequences, not constants.
//
// This is the headline result of the change and the cheapest thing in the
// suite to check, so it goes first. Nothing in flight.cc is told 0x0400 or
// 0x0340 any more; they fall out of an angle and a lift slope, and they fall
// out on the numbers the old model had to be handed.
#ifdef __FLIGHT_AOA__
static void test_stall_speeds_are_derived() {
  printf("Running test_stall_speeds_are_derived...\n");

  // The lift slope is one unit of C_L per unit of alpha16, so C_L max upright
  // is the stall angle in those units plus the camber.
  const int32_t cl_max_clean = (kAlphaStall << 4) + kCamberCl;
  const int32_t cl_max_flap = cl_max_clean + 512; // kFlightFlapDeltaCl
  // Inverted the camber subtracts instead of adding, which is the whole of the
  // inverted penalty.
  const int32_t cl_max_inverted = (kAlphaStall << 4) - kCamberCl;

  // lift = V^2 * C_L / 262144 and the weight is 0x1000, so the speed at which
  // a C_L carries the aeroplane is sqrt(2^30 / C_L).
  const double clean = sqrt((double)(1 << 30) / (double)cl_max_clean);
  const double flap = sqrt((double)(1 << 30) / (double)cl_max_flap);
  const double inverted = sqrt((double)(1 << 30) / (double)cl_max_inverted);
  printf("  C_L max %d -> V_stall %.0f (clean, was told %d)\n", cl_max_clean,
         clean, kStallSpeedWithoutFlaps);
  printf("  C_L max %d -> V_stall %.0f (flaps, was told %d)\n", cl_max_flap,
         flap, kStallSpeedWithFlaps);
  printf("  C_L max %d -> V_stall %.0f (inverted)\n", cl_max_inverted,
         inverted);

  assert(clean > kStallSpeedWithoutFlaps - 1 &&
         clean < kStallSpeedWithoutFlaps + 1);
  assert(flap > kStallSpeedWithFlaps - 8 && flap < kStallSpeedWithFlaps + 8);
  // Inverted is strictly harder, which is what the camber is there for.
  assert(inverted > clean);

  // And the model agrees with the arithmetic: just under the derived clean
  // stall speed the wing runs out of angle, and just over it does not.
  for (int i = 0; i < 2; ++i) {
    const int16_t speed =
        (int16_t)(i ? kStallSpeedWithoutFlaps + 300 : kStallSpeedWithoutFlaps - 100);
    flight_init();
    flight_throttle = 0;
    bool stalled = false;
    for (int f = 0; f < 200 && !stalled; ++f) {
      flight_cam.front.z = 0;
      flight_speed = speed;
      flight_eye_z = 0x040000;
      flight_advance();
      stalled = flight_stall != 0;
    }
    printf("  held level at %d: stalled %d\n", speed, stalled);
    assert(stalled == (i == 0));
  }

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

#ifdef __FLIGHT_AOA__
static void test_level_cruise_equilibrium() {
  printf("Running test_level_cruise_equilibrium...\n");

  // Level flight is an attitude the pilot has to find, not front.z = 0. The
  // wing needs a positive angle of attack to carry the weight, so the level
  // attitude is that angle plus the flight path, and the flight path in level
  // flight is zero.
  int16_t pitch = 0;
  assert(_level_trim(0x14, 256, 0, &pitch));
  printf("  level trim at throttle 0x14: pitch %d, alpha %d, speed %d\n", pitch,
         flight_alpha(), flight_speed);
  assert(pitch > 0);
  assert(flight_alpha() > 0);
  assert(!flight_status);

  // "Equilibrium" here means bit-stable, not "small". Every state variable the
  // model carries stops moving and stays stopped - which is the property the
  // sixteen-to-one resolution of flight_alpha16 bought, and which the model
  // did not have at one unit per unit of front.z: there it could only hunt
  // across a lift error a sixteenth of a g wide.
  const int16_t held_pitch = flight_cam.front.z;
  const int16_t held_up_z = flight_cam.up.z;
  const int16_t speed = flight_speed;
  const int16_t gamma = flight_gamma;
  const int16_t alpha = flight_alpha16;
  const int16_t vspeed = flight_vspeed;
  for (int i = 0; i < 200; ++i) {
    flight_cam.front.z = held_pitch;
    flight_cam.up.z = held_up_z;
    flight_eye_z = 0x040000;
    flight_advance();
    flight_throttle = 0x14;
    assert(flight_speed == speed);
    assert(flight_gamma == gamma);
    assert(flight_alpha16 == alpha);
    assert(flight_vspeed == vspeed);
  }

  // And with the altitude left free, it is genuinely held.
  const int32_t z0 = flight_eye_z;
  for (int i = 0; i < 100; ++i) {
    flight_cam.front.z = held_pitch;
    flight_cam.up.z = held_up_z;
    flight_advance();
    flight_throttle = 0x14;
  }
  assert(flight_eye_z == z0);

  printf("  settled speed: %d (0x%04X), vspeed: %d\n", speed, speed, vspeed);
  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 1. Level cruise equilibrium test.
// "Equilibrium" means the vertical speed settles to zero and the airspeed
// settles to a value that is stable frame over frame - not merely that the
// aircraft is still flying.
static void test_level_cruise_equilibrium() {
  printf("Running test_level_cruise_equilibrium...\n");

  _settle(0x14, 0, 256, 400); // Cruise throttle, wings level, zero pitch
  int16_t settled_speed = flight_speed;
  int32_t settled_z = flight_eye_z;

  assert(!flight_status);
  assert(flight_vspeed == 0); // Actually level, not just airborne

  // Airspeed has stopped changing.
  for (int i = 0; i < 50; ++i) {
    flight_cam.front.z = 0;
    flight_advance();
    flight_throttle = 0x14;
  }
  assert(flight_speed == settled_speed);
  assert(flight_eye_z == settled_z); // Altitude held, not drifting
  assert(flight_speed > kTrimSpeed); // Above trim, so the deficit is zero

  printf("  settled speed: %d (0x%04X), vspeed: %d\n", settled_speed,
         settled_speed, flight_vspeed);
  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 1b. Trim speed boundary test.
// Below the trim speed the lift deficit produces sink; at or above it the
// deficit is clamped away and level pitch means genuinely level flight.

#ifdef __FLIGHT_AOA__
// 1b. The level attitude falls as the aircraft goes faster.
//
// This replaces a test of the old model's central mechanism, the one-sided
// lift deficit: there, below a trim speed the aircraft sank and at or above it
// front.z = 0 was level and no faster level trim existed. There is no such
// speed now. Lift is C_L(alpha) * V^2, so holding the weight at any speed is a
// question of angle, and the faster you go the less of it you need - which is
// the trim behaviour every real aeroplane has, and the one the attitude
// indicator has been implying all along.
static void test_level_trim_falls_with_speed() {
  printf("Running test_level_trim_falls_with_speed...\n");

  int16_t p_slow = 0, p_mid = 0, p_fast = 0;
  assert(_level_trim(0x10, 256, 0, &p_slow));
  const int16_t a_slow = flight_alpha(), v_slow = flight_speed;
  assert(_level_trim(0x14, 256, 0, &p_mid));
  const int16_t a_mid = flight_alpha(), v_mid = flight_speed;
  assert(_level_trim(0x18, 256, 0, &p_fast));
  const int16_t a_fast = flight_alpha(), v_fast = flight_speed;

  printf("  0x10: pitch %3d alpha %3d speed %5d\n", p_slow, a_slow, v_slow);
  printf("  0x14: pitch %3d alpha %3d speed %5d\n", p_mid, a_mid, v_mid);
  printf("  0x18: pitch %3d alpha %3d speed %5d\n", p_fast, a_fast, v_fast);

  // More throttle, more speed, less angle of attack, lower nose.
  assert(v_slow < v_mid && v_mid < v_fast);
  assert(a_slow > a_mid && a_mid > a_fast);
  assert(p_slow > p_mid && p_mid > p_fast);
  // And it is always some angle: there is no speed at which the wing carries
  // the aeroplane for nothing.
  assert(a_fast > 0);

  // The corollary, which is what a pilot notices: front.z = 0 is a descent at
  // every throttle, however fast.
  _settle2(0x18, 0, 256, 600);
  printf("  zero pitch at full throttle: speed %d, mean vspeed %.1f\n",
         flight_speed, _settle_vmean256 / 256.0);
  assert(_settle_vmean256 < 0);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// Same case as the AoA build's `test_level_trim_falls_with_speed`, under that name so
// main() needs no branch, but asserting the arcade model's behaviour -
// which for several of these is the opposite behaviour.
// 1b. Trim speed boundary test.
// Below the trim speed the lift deficit produces sink; at or above it the
// deficit is clamped away and level pitch means genuinely level flight.
static void test_level_trim_falls_with_speed() {
  printf("Running test_level_trim_falls_with_speed...\n");

  // A throttle that settles above trim speed -> no sink at zero pitch.
  _settle(0x18, 0, 256, 400);
  assert(flight_speed > kTrimSpeed);
  assert(flight_vspeed == 0);

  // A throttle that settles below trim speed -> sink at zero pitch.
  _settle(0x0A, 0, 256, 400);
  printf("  low throttle settled speed: %d, vspeed: %d\n", flight_speed,
         flight_vspeed);
  assert(flight_speed < kTrimSpeed);
  assert(flight_vspeed < 0); // Lift deficit sinks the aircraft

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 2. Power-off stall recovery test
static void test_power_off_stall_recovery() {
  printf("Running test_power_off_stall_recovery...\n");
  flight_init();
  flight_throttle = 0;

  for (int i = 0; i < 150; ++i) {
    flight_advance();
  }

  // Speed should recover above stall speed after nose drop
  assert(flight_speed >= 0x0300);
  assert(!flight_status);
  printf("  PASS\n\n");
}

// 3. No backward flight test
static void test_no_backward_flight() {
  printf("Running test_no_backward_flight...\n");
  flight_init();
  flight_throttle = 0;

  // Straight up, as a genuine orthonormal frame. Assigning front.z alone
  // leaves front.x at 256, so the vector is not unit length and normalizes to
  // a 45 degree climb - which is not the case this test is meant to cover,
  // and which hides the dead spot at the true vertical entirely.
  flight_cam.front = make_vector(0, 0, 256);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(-256, 0, 0);

  for (int i = 0; i < 150; ++i) {
    flight_advance();
    assert(flight_speed >= 0); // Must never be negative
  }

  // The nose must actually fall away from the vertical, not merely change.
  printf("  front.z after 150 frames: %d\n", flight_cam.front.z);
  assert(flight_cam.front.z < 128); // past 30 degrees of nose drop
  printf("  PASS\n\n");
}

// 4. Climb at different throttles test
static void test_climb_at_different_throttles() {
  printf("Running test_climb_at_different_throttles...\n");

  // 100% throttle climb
  flight_init();
  flight_throttle = 0x18; // Max throttle
  flight_cam.front.z = 30;
  int32_t start_z_100 = flight_eye_z;

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int32_t climb_100 = flight_eye_z - start_z_100;

  // 25% throttle climb
  flight_init();
  flight_throttle = 0x06; // ~25% throttle
  flight_cam.front.z = 30;
  int32_t start_z_25 = flight_eye_z;

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int32_t climb_25 = flight_eye_z - start_z_25;

  assert(climb_100 > climb_25);
  printf("  100%% climb: %d, 25%% climb: %d\n", climb_100, climb_25);
  printf("  PASS\n\n");
}

// 5. Banked turns drag and descent test

#ifdef __FLIGHT_AOA__
static void test_banked_turns_drag_and_descent() {
  printf("Running test_banked_turns_drag_and_descent...\n");

  // A level turn needs 1/cos(bank) times the weight in lift, and the wing can
  // only make it by working harder. Both halves of that are measurable: the
  // angle of attack the pilot has to hold, and the induced drag it costs.
  //
  // The old model had neither - it charged a flat left.z^2 term for the bank
  // and left the wing out of it - so this used to assert only that a banked
  // aircraft ends up slower.
  int16_t straight_pitch = 0;
  assert(_level_trim(0x18, 256, 0, &straight_pitch));
  const int16_t straight_speed = flight_speed;
  const int16_t straight_alpha = flight_alpha();

  // ~36 degrees of bank, held while the pitch is swept for a level trim.
  int16_t banked_pitch = -1, banked_speed = 0, banked_alpha = 0;
  for (int16_t p = -64; p <= 200; ++p) {
    flight_init();
    flight_eye_z = 0x040000;
    flight_throttle = 0x18;
    flight_fuel = 0x0FFFFFFF;
    flight_cam.front = make_vector(256, 0, 0);
    flight_cam.left = make_vector(0, 256, 0);
    flight_cam.up = make_vector(0, -150, (int16_t)sqrt(65536.0 - 150.0 * 150));
    flight_cam.front.z = p;
    vec_orthonormalize(&flight_cam);
    const int16_t hp = flight_cam.front.z, hu = flight_cam.up.z,
                  hl = flight_cam.left.z;
    int32_t vsum = 0;
    for (int i = 0; i < 600; ++i) {
      flight_cam.front.z = hp;
      flight_cam.up.z = hu;
      flight_cam.left.z = hl;
      vec_orthonormalize(&flight_cam);
      flight_eye_z = 0x040000;
      flight_advance();
      flight_throttle = 0x18;
      if (i >= 400) vsum += flight_vspeed;
    }
    if (!flight_status && !flight_stall && vsum >= 0) {
      banked_pitch = p;
      banked_speed = flight_speed;
      banked_alpha = flight_alpha();
      break;
    }
  }

  printf("  wings level: pitch %d alpha %d speed %d\n", straight_pitch,
         straight_alpha, straight_speed);
  printf("  36 deg bank: pitch %d alpha %d speed %d\n", banked_pitch,
         banked_alpha, banked_speed);

  assert(banked_pitch >= 0);              // A level turn is possible at 36 deg
  assert(banked_alpha > straight_alpha);  // and it costs angle of attack
  assert(banked_speed < straight_speed);  // which costs induced drag
  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 5. Banked turns drag and descent test
static void test_banked_turns_drag_and_descent() {
  printf("Running test_banked_turns_drag_and_descent...\n");

  // Straight flight baseline
  flight_init();
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_cam.front.z = 0;
    flight_advance();
  }
  int16_t straight_speed = flight_speed;

  // 40% banked flight
  flight_init();
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_cam.left.z = 150; // ~40% bank
    flight_cam.front.z = 0;
    flight_advance();
  }
  int16_t banked_speed = flight_speed;

  printf("  straight speed: %d, banked speed: %d\n", straight_speed,
         banked_speed);
  assert(banked_speed < straight_speed); // Induced turn drag penalty
  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 6. Inverted flight drag and pitch test.
// Inverted, lift acts downward, so the deficit is kTrimLift + |lift| rather
// than kTrimLift - lift. That has two measurable consequences, and this test
// asserts both against an upright baseline at the same throttle: the implicit
// deficit drag settles the aircraft at a lower speed, and the sink penalty
// means nose-up pitch is needed just to stay level.

#ifdef __FLIGHT_AOA__
static void test_inverted_flight_drag_and_pitch() {
  printf("Running test_inverted_flight_drag_and_pitch...\n");

  int16_t up_pitch = 0, inv_pitch = 0;
  assert(_level_trim(0x14, 256, 0, &up_pitch));
  const int16_t up_speed = flight_speed, up_alpha = flight_alpha();
  assert(_level_trim(0x14, -256, 0, &inv_pitch));
  const int16_t inv_speed = flight_speed, inv_alpha = flight_alpha();

  printf("  upright:  pitch %3d alpha %4d speed %5d\n", up_pitch, up_alpha,
         up_speed);
  printf("  inverted: pitch %3d alpha %4d speed %5d\n", inv_pitch, inv_alpha,
         inv_speed);

  // Angle of attack is a body angle, so inverted it has the opposite sign:
  // the air arrives on the canopy side, and that is what makes the lift point
  // the right way once the aircraft is upside down.
  assert(up_alpha > 0);
  assert(inv_alpha < 0);

  // Nose up relative to the horizon, and further up than the upright trim -
  // flight.md 3.2. It is the camber that costs this: a symmetric wing would
  // fly inverted on exactly the mirror of the upright attitude.
  assert(inv_pitch > 0);
  assert(inv_pitch > up_pitch);

  // And the same camber raises the inverted stall speed, which is the price
  // that stops inverted flight being free. The most negative C_L the wing can
  // reach is short of the peak by twice the camber offset.
  flight_init();
  flight_eye_z = 0x040000;
  flight_throttle = 0;
  flight_speed = 0x0480; // Comfortably above the upright stall speed
  flight_cam.up = make_vector(0, 0, -256);
  vec_orthonormalize(&flight_cam);
  const int16_t hu = flight_cam.up.z;
  bool stalled = false;
  for (int i = 0; i < 200 && !stalled; ++i) {
    flight_cam.front.z = 0;
    flight_cam.up.z = hu;
    flight_eye_z = 0x040000;
    flight_advance();
    stalled = flight_stall != 0;
  }
  printf("  inverted at 0x0480 (above the upright stall speed): stalled %d\n",
         stalled);
  assert(stalled);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 6. Inverted flight drag and pitch test.
// Inverted, lift acts downward, so the deficit is kTrimLift + |lift| rather
// than kTrimLift - lift. That has two measurable consequences, and this test
// asserts both against an upright baseline at the same throttle: the implicit
// deficit drag settles the aircraft at a lower speed, and the sink penalty
// means nose-up pitch is needed just to stay level.
static void test_inverted_flight_drag_and_pitch() {
  printf("Running test_inverted_flight_drag_and_pitch...\n");

  _settle(0x14, 0, 256, 400); // Upright baseline
  int16_t upright_speed = flight_speed;
  int16_t upright_vspeed = flight_vspeed;

  _settle(0x14, 0, -256, 400); // Same throttle, inverted
  int16_t inverted_speed = flight_speed;
  int16_t inverted_vspeed = flight_vspeed;

  printf("  upright:  speed=%d vspeed=%d\n", upright_speed, upright_vspeed);
  printf("  inverted: speed=%d vspeed=%d\n", inverted_speed, inverted_vspeed);

  // Deficit drag: the inverted trim speed is strictly lower.
  assert(inverted_speed < upright_speed);
  // Deficit sink: level pitch is not level flight when inverted.
  assert(upright_vspeed == 0);
  assert(inverted_vspeed < 0);

  // Nose-up pitch relative to the horizon is what recovers it. Find the
  // shallowest pitch that stops the descent and check it is a real climb-out
  // angle, not a rounding artefact.
  int16_t level_pitch = -1;
  for (int16_t p = 0; p <= 140; ++p) {
    _settle(0x14, p, -256, 400);
    if (!flight_status && flight_vspeed >= 0) {
      level_pitch = p;
      break;
    }
  }
  printf("  inverted level pitch at throttle 0x14: %d\n", level_pitch);
  assert(level_pitch > 0); // Nose up vs. the horizon, per flight.md 3.2

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 7. Gear drag penalty test
static void test_gear_drag_penalty() {
  printf("Running test_gear_drag_penalty...\n");

  flight_init();
  flight_gear = 0;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t clean_speed = flight_speed;

  flight_init();
  flight_gear = 1;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t gear_speed = flight_speed;

  assert(gear_speed < clean_speed);
  printf("  clean speed: %d, gear speed: %d\n", clean_speed, gear_speed);
  printf("  PASS\n\n");
}

// 8. Flap drag, lift and stall reduction test.
// Flaps do three separate things (flight.md 4.2) and this asserts each of them
// rather than just that the flag is set: more parasite drag, 50% more lift,
// and a lower stall speed.

#ifdef __FLIGHT_AOA__
static void test_flap_drag_lift_and_stall_reduction() {
  printf("Running test_flap_drag_lift_and_stall_reduction...\n");

  // (a) Drag: at the same throttle and the same level trim, flaps settle the
  // aircraft slower.
  int16_t clean_pitch = 0, flap_pitch = 0;
  assert(_level_trim(0x18, 256, 0, &clean_pitch));
  const int16_t clean_speed = flight_speed;
  assert(_level_trim(0x18, 256, 1, &flap_pitch));
  const int16_t flap_speed = flight_speed;
  printf("  clean speed: %d, flap speed: %d\n", clean_speed, flap_speed);
  assert(flap_speed < clean_speed);

  // (b) Lift: flaps are a camber shift, so at the same attitude and speed the
  // wing makes more of it. That shows up as a lower angle of attack for the
  // same job - the flapped aircraft trims level with the nose lower, and here
  // it trims below the horizon outright.
  printf("  level trim pitch: clean %d, flaps %d\n", clean_pitch, flap_pitch);
  assert(flap_pitch < clean_pitch);

  // (c) Stall speed. The stall is an angle, so the speeds are consequences:
  // between the two the clean wing runs out of angle and the flapped one does
  // not. Both are flown, not asserted from a single frame - the flight path
  // has to develop before the angle of attack means anything.
  const int16_t between =
      (int16_t)((kStallSpeedWithFlaps + kStallSpeedWithoutFlaps) / 2);
  assert(between > kStallSpeedWithFlaps && between < kStallSpeedWithoutFlaps);

  for (int flap = 0; flap < 2; ++flap) {
    flight_init();
    flight_eye_z = 0x040000;
    flight_throttle = 0;
    flight_flap = (uint8_t)flap;
    bool stalled = false;
    for (int i = 0; i < 200 && !stalled; ++i) {
      flight_cam.front.z = 0;
      flight_speed = between; // The speed is the independent variable here
      flight_eye_z = 0x040000;
      flight_advance();
      stalled = flight_stall != 0;
    }
    printf("  held at %d, flap %d: stalled %d\n", between, flap, stalled);
    assert(stalled == (flap == 0));
  }

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 8. Flap drag, lift and stall reduction test.
// Flaps do three separate things (flight.md 4.2) and this asserts each of them
// rather than just that the flag is set: more parasite drag, 50% more lift,
// and a lower stall speed.
static void test_flap_drag_lift_and_stall_reduction() {
  printf("Running test_flap_drag_lift_and_stall_reduction...\n");

  // (a) Drag: at the same throttle, flaps settle the aircraft slower.
  _settle(0x14, 0, 256, 400, 0);
  int16_t clean_speed = flight_speed;
  _settle(0x14, 0, 256, 400, 1);
  int16_t flap_speed = flight_speed;
  printf("  clean speed: %d, flap speed: %d\n", clean_speed, flap_speed);
  assert(flap_speed < clean_speed);

  // (b) Lift: below the trim speed, flaps reduce the deficit, so the sink rate
  // at the same airspeed and attitude is smaller.
  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = 0x0600; // Below kTrimSpeed, so a deficit exists
  flight_throttle = 0;
  flight_advance();
  int16_t clean_sink = flight_vspeed;

  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = 0x0600;
  flight_throttle = 0;
  flight_flap = 1;
  flight_advance();
  int16_t flap_sink = flight_vspeed;

  printf("  sink at 0x0600: clean %d, flaps %d\n", clean_sink, flap_sink);
  assert(clean_sink < 0);
  assert(flap_sink > clean_sink); // Less sink with flaps -> more lift

  // (c) Stall speed: a speed between the two stall constants stalls clean but
  // not with flaps.
  int16_t between =
      (int16_t)((kStallSpeedWithFlaps + kStallSpeedWithoutFlaps) / 2);
  assert(between > kStallSpeedWithFlaps && between < kStallSpeedWithoutFlaps);

  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = between;
  flight_throttle = 0;
  flight_advance();
  assert(flight_cam.front.z < 0); // Clean: stall pitch-down fired

  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = between;
  flight_throttle = 0;
  flight_flap = 1;
  flight_advance();
  assert(flight_cam.front.z == 0); // Flaps: still flying

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 9. Touchdown flare and crash envelope test

#ifdef __FLIGHT_AOA__
static void test_touchdown_flare_and_crash_envelope() {
  printf("Running test_touchdown_flare_and_crash_envelope...\n");

  // Gear up landing -> crash
  flight_init();
  flight_eye_z = 0x2000; // Ground altitude
  flight_gear = 0;
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_GEAR);

  // A flare with speed in hand is a landing.
  int16_t vs = _arm_touchdown(45, 0, 1100, 1);
  printf("  flare 45 at 1100 -> vspeed %d\n", vs);
  assert(vs < 0);
  assert(vs >= kMaxLandingVSpeed);
  flight_advance();
  assert(!flight_status);

  // Holding it off too long is now a stall, not a pitch violation, and the
  // sink is what kills it. This is a change of verdict rather than of outcome,
  // and it is the better one: an aeroplane that arrives nose-high and slow has
  // stalled onto the runway, which is what the classic accident is.
  //
  // Trigger 7 cannot own this any more, because it cannot be reached from the
  // air at all. A *descending* aircraft with the nose above 64 is by
  // construction past the stall angle - the flight path is below the horizon
  // and the nose is well above it - so the break has already fired, and at
  // that excess it trims the nose down by more in one step than the envelope
  // limit is wide.
  vs = _arm_touchdown(62, 0, 1030, 1);
  printf("  flare 62 at 1030 -> vspeed %d\n", vs);
  assert(vs < kMaxLandingVSpeed);
  flight_advance();
  printf("  front.z at the check: %d, status %d\n", flight_cam.front.z,
         flight_status);
  assert(flight_status == FLIGHT_CRASH_VSPEED);
  assert(flight_cam.front.z < kMaxLandingPitch); // The break got there first

  // Trigger 7 is not dead code, though: on the ground the flight path is the
  // runway, so there is no stall and nothing trims the nose. Over-rotating on
  // the roll is what it polices now, and it is why kFlightMaxGroundPitch has
  // to sit below kMaxLandingPitch.
  flight_init();
  flight_eye_z = kGroundZ;
  flight_gear = 1;
  flight_speed = 0x0400;
  flight_advance();
  assert(!flight_status);
  flight_cam.front.z = 80;
  vec_orthonormalize(&flight_cam);
  flight_advance();
  printf("  over-rotated on the runway: status %d\n", flight_status);
  assert(flight_status == FLIGHT_CRASH_PITCH_HIGH);

  // And the pilot cannot get there with the stick: the rotation limit stops
  // short of it.
  _put_on_ground(0x0400);
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_PITCH_UP);
    flight_advance();
    assert(!flight_status);
  }
  assert(flight_cam.front.z < kMaxLandingPitch);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 9. Touchdown flare and crash envelope test
static void test_touchdown_flare_and_crash_envelope() {
  printf("Running test_touchdown_flare_and_crash_envelope...\n");

  // Gear up landing -> crash
  flight_init();
  flight_eye_z = 0x2000; // Ground altitude
  flight_gear = 0;
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_GEAR);

  // Both flares below are flown as genuine descents through the ground plane.
  // That constrains the speed: a nose-up attitude only descends when the lift
  // deficit outweighs the pitch term, so just above stall the steepest
  // descending flare is front.z = 51, and a steeper one needs to be below
  // stall speed.

  // Safe landing flare (front.z = 45, gear down), descending at 1030.
  int16_t vs = _arm_touchdown(45, 0, 1030, 1);
  printf("  flare 45 at 1030 -> vspeed %d\n", vs);
  assert(vs < 0); // Really descending onto the runway
  assert(vs >= kMaxLandingVSpeed);
  flight_advance();
  assert(!flight_status);

  // Excessive landing flare (front.z = 80 > 64) -> crash. Held off until the
  // speed has decayed below stall, which is the only way this attitude can
  // arrive at the ground - the classic "hold it off too long" stall onto the
  // runway. The stall break trims the nose down a little on the way in, so
  // check what the envelope actually saw rather than what was set.
  vs = _arm_touchdown(80, 0, 700, 1);
  printf("  flare 80 at 700 -> vspeed %d\n", vs);
  assert(vs < 0);
  flight_advance();
  printf("  front.z at the check: %d (limit %d)\n", flight_cam.front.z,
         kMaxLandingPitch);
  assert(flight_cam.front.z > kMaxLandingPitch); // Still over the limit
  assert(flight_status == FLIGHT_CRASH_PITCH_HIGH);

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 10. Takeoff stall speed gate test.
// The gate lives in the on-ground branch of flight_input, so the model has to
// actually be in ground mode for this to test anything - see _put_on_ground.

#ifdef __FLIGHT_AOA__
// 10. Takeoff: the wing decides, not a gate.
//
// This used to test kFlightRotatePitchZ and the speed gate in front of it -
// pitch up was refused outright below the stall speed, and accepted above it
// by jumping the nose to a fixed attitude and declaring the aircraft airborne.
// Both are gone. Rotating is now always allowed, it makes lift like any other
// angle of attack, and flight_advance() unsticks the aircraft on the step the
// wing carries it.
static void test_takeoff_rotation_is_not_a_gate() {
  printf("Running test_takeoff_rotation_is_not_a_gate...\n");

  // Well below flying speed: the rotation is accepted, and nothing flies.
  _put_on_ground(0x0200);
  const int16_t before = flight_cam.front.z;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z > before); // Accepted, not refused
  for (int i = 0; i < 20; ++i) {
    flight_speed = 0x0200; // Hold it there; the wing is what is under test
    flight_advance();
  }
  assert(flight_eye_z == kGroundZ); // Still on the runway
  assert(flight_vspeed == 0);
  assert(!flight_status);

  // Rotated and given flying speed, it leaves the ground on its own.
  _put_on_ground(0x0500);
  for (int i = 0; i < 4; ++i) {
    flight_input(FLIGHT_INPUT_PITCH_UP);
  }
  const int16_t rotate_attitude = flight_cam.front.z;
  int liftoff_step = -1;
  int16_t liftoff_speed = 0;
  for (int i = 0; i < 400 && liftoff_step < 0; ++i) {
    flight_input(FLIGHT_INPUT_PITCH_UP); // Held back, as a pilot would
    flight_advance();
    flight_throttle = kMaxThrottle;
    if (flight_eye_z > kGroundZ) {
      liftoff_step = i;
      liftoff_speed = flight_speed;
    }
  }
  printf("  rotated to %d, airborne at step %d, speed %d\n", rotate_attitude,
         liftoff_step, liftoff_speed);
  assert(liftoff_step >= 0);
  assert(!flight_status);
  // The liftoff speed is a consequence of the lift equation, so it lands just
  // above the speed at which the wing can carry the weight at all. Nothing in
  // the model was told either number.
  assert(liftoff_speed > (int16_t)kStallSpeedWithoutFlaps);
  assert(liftoff_speed < (int16_t)kStallSpeedWithoutFlaps + 400);

  // Flaps make more lift at the same angle, so the same rotation flies sooner.
  _put_on_ground(0x0500);
  flight_flap = 1;
  for (int i = 0; i < 4; ++i) {
    flight_input(FLIGHT_INPUT_PITCH_UP);
  }
  int16_t flap_liftoff = 0;
  for (int i = 0; i < 400 && flap_liftoff == 0; ++i) {
    flight_input(FLIGHT_INPUT_PITCH_UP);
    flight_advance();
    flight_throttle = kMaxThrottle;
    if (flight_eye_z > kGroundZ) {
      flap_liftoff = flight_speed;
    }
  }
  printf("  with flaps: airborne at speed %d\n", flap_liftoff);
  assert(flap_liftoff > 0);
  assert(flap_liftoff < liftoff_speed);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// Same case as the AoA build's `test_takeoff_rotation_is_not_a_gate`, under that name so
// main() needs no branch, but asserting the arcade model's behaviour -
// which for several of these is the opposite behaviour.
// 10. Takeoff stall speed gate test.
// The gate lives in the on-ground branch of flight_input, so the model has to
// actually be in ground mode for this to test anything - see _put_on_ground.
static void test_takeoff_rotation_is_not_a_gate() {
  printf("Running test_takeoff_rotation_is_not_a_gate...\n");

  // Below stall speed: pitch up is refused outright.
  _put_on_ground(0x0200);
  int16_t pitch_before = flight_cam.front.z;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z ==
         pitch_before); // Rotation refused, not just clamped
  flight_advance();
  assert(flight_eye_z == kGroundZ); // Still on the runway
  assert(flight_vspeed == 0);

  // Just below the gate: still refused.
  _put_on_ground((int16_t)kStallSpeedWithoutFlaps);
  pitch_before = flight_cam.front.z;
  flight_input(FLIGHT_INPUT_PITCH_UP); // Gate is strictly greater than
  assert(flight_cam.front.z == pitch_before);
  flight_advance();
  assert(flight_eye_z == kGroundZ);

  // Above stall speed: rotates and becomes airborne.
  _put_on_ground(0x0800);
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z > 0); // Rotation accepted
  flight_advance();
  printf("  after rotation: z=%d (ground %d), front.z=%d\n", flight_eye_z,
         (int)kGroundZ, flight_cam.front.z);
  assert(flight_eye_z > kGroundZ); // Airborne
  assert(!flight_status);

  // Flaps lower the gate: a speed between the two stall constants is enough
  // with flaps down and not enough clean.
  int16_t between =
      (int16_t)((kStallSpeedWithFlaps + kStallSpeedWithoutFlaps) / 2);

  _put_on_ground(between);
  pitch_before = flight_cam.front.z;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z == pitch_before); // Clean: refused

  _put_on_ground(between);
  flight_flap = 1;
  flight_input(FLIGHT_INPUT_PITCH_UP);
  assert(flight_cam.front.z > 0); // Flaps: accepted

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 10b. Takeoff rotation attitude.
// kRotatePitchZ used to be mirrored here: the attitude the old model jumped
// the nose to once the aircraft passed a stall speed, because lift had no
// pitch term and one 3.6 degree step could not out-climb the sink penalty
// until airspeed 1608 - two thirds of the way up the green arc, with the
// wheels skipping off the runway once per frame the whole way. Rotating makes
// lift now, so the constant is gone and the liftoff speed is a consequence of
// the attitude the pilot chose.

// Rolls at a pinned airspeed with the stick back and reports whether the
// aircraft actually left the ground. Pinned because this is about the attitude
// the rotation reaches, not about the acceleration that gets it there.
static bool _rotates_at(int16_t speed, uint8_t flap, int *touchdowns) {
  _put_on_ground(speed);
  flight_flap = flap;
  bool climbed = false;
  *touchdowns = 0;
  // The budget scales with the step shift, because every rate in the model
  // does (flight.md 8). A fixed count of frames is a different amount of
  // flying on a SuperCPU than on a stock C64, and at shift 2 forty frames is
  // not enough to rotate and climb clear of the runway.
  const int frames = 40 << flight_step_shift;
  for (int f = 0; f < frames; ++f) {
    flight_speed = speed;
    flight_input(FLIGHT_INPUT_PITCH_UP);
    flight_speed = speed;
    flight_advance();
    if (flight_events & FLIGHT_EV_TOUCHDOWN) {
      ++*touchdowns;
    }
    if (flight_eye_z > kGroundZ) {
      climbed = true;
    }
  }
  return climbed;
}


#ifdef __FLIGHT_AOA__
static void test_takeoff_rotation_attitude() {
  printf("Running test_takeoff_rotation_attitude...\n");

  // The rotation limit, and the two things bounding it.
  //
  // It has to stay under kMaxLandingPitch, because the landing envelope runs
  // on every frame at ground level (flight.md 5.3) and trigger 7 does not care
  // that the aircraft is taking off rather than arriving. Set equal to it, the
  // takeoff roll crashed: vec_orthonormalize puts a unit back and 65 > 64.
  _put_on_ground(0x0800);
  for (int i = 0; i < 8; ++i) {
    flight_input(FLIGHT_INPUT_PITCH_UP);
  }
  const int16_t limit = flight_cam.front.z;
  printf("  rotation limit: front.z=%d (landing limit %d)\n", limit,
         (int)kMaxLandingPitch);
  assert(limit > 0);
  assert(limit < kMaxLandingPitch);

  // Holding it there does not crash, however long the roll lasts.
  for (int i = 0; i < 60; ++i) {
    flight_speed = 0x0200; // Below flying speed, so it stays on the runway
    flight_input(FLIGHT_INPUT_PITCH_UP);
    flight_advance();
    assert(!flight_status);
  }

  // Liftoff sits just above the speed at which the wing carries the weight,
  // and it flies away cleanly - no skipping, so no touchdown event per frame.
  // That skipping was the reason the old model needed a rotation constant at
  // all: it lifted the wheels and put them straight back down once a frame.
  int touchdowns = 0;
  assert(!_rotates_at((int16_t)kStallSpeedWithoutFlaps - 200, 0, &touchdowns));
  assert(_rotates_at(1500, 0, &touchdowns));
  assert(touchdowns == 0);

  // Flaps make more lift at the same angle, so they fly at a lower speed.
  assert(_rotates_at(0x03C0, 1, &touchdowns) && touchdowns == 0); // 960

  // Machine independence. The pitch step is scaled by the host's speed
  // (vec_set_rotation_shift) and so is every rate in the model, so a stock C64
  // and a SuperCPU take a different number of steps to the same attitude and
  // reach flying speed at the same airspeed. kCpuMaxStepShift is 2, so those
  // are the three cases that exist.
  for (uint8_t shift = 0; shift <= 2; ++shift) {
    flight_set_step_shift(shift);
    _put_on_ground(0x0800);
    for (int i = 0; i < 8 << shift; ++i) {
      flight_input(FLIGHT_INPUT_PITCH_UP);
    }
    printf("  step shift %d: front.z=%d\n", shift, flight_cam.front.z);
    assert(flight_cam.front.z == limit);
    assert(flight_cam.front.z < kMaxLandingPitch);
    assert(_rotates_at(1500, 0, &touchdowns));
  }
  flight_set_step_shift(0);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
static void test_takeoff_rotation_attitude() {
  printf("Running test_takeoff_rotation_attitude...\n");

  // One input reaches the takeoff attitude, rather than one pitch step of it.
  _put_on_ground(0x0800);
  flight_input(FLIGHT_INPUT_PITCH_UP);
  printf("  rotation attitude: front.z=%d (target %d)\n", flight_cam.front.z,
         (int)kRotatePitchZ);
  assert(flight_cam.front.z >= kRotatePitchZ - 2);
  // Comfortably inside kMaxLandingPitch, so a rotation that does not fly is
  // still a legal touchdown rather than a nose-high crash.
  assert(flight_cam.front.z < kMaxLandingPitch);

  // Liftoff is now just above the stall gate - the bottom of the green arc on
  // the airspeed dial - rather than half way along it.
  int touchdowns;
  assert(!_rotates_at((int16_t)kStallSpeedWithoutFlaps, 0, &touchdowns));
  assert(_rotates_at(0x0460, 0, &touchdowns)); // 1120, one needle notch up
  // And it flies away cleanly: no skipping, so no touchdown event per frame.
  assert(touchdowns == 0);

  // The old threshold is well clear of the new one, which is the whole point.
  assert(_rotates_at(1500, 0, &touchdowns) && touchdowns == 0);

  // Flaps lower it further, as they lower the gate.
  assert(_rotates_at(0x03C0, 1, &touchdowns) && touchdowns == 0); // 960

  // Machine independence. The pitch step is scaled by the host's speed
  // (vec_set_rotation_shift), so a rotation counted in steps would reach a
  // different attitude - and therefore a different liftoff speed - on a
  // SuperCPU than on a stock C64. Driving to an attitude is what makes these
  // agree. kCpuMaxStepShift is 2, so those are the three cases that exist.
  for (uint8_t shift = 0; shift <= 2; ++shift) {
    flight_set_step_shift(shift);
    _put_on_ground(0x0800);
    flight_input(FLIGHT_INPUT_PITCH_UP);
    printf("  step shift %d: front.z=%d\n", shift, flight_cam.front.z);
    assert(flight_cam.front.z >= kRotatePitchZ - 2);
    assert(flight_cam.front.z < kMaxLandingPitch);
    assert(_rotates_at(0x0460, 0, &touchdowns));
  }
  flight_set_step_shift(0);

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 11. Ground deceleration friction test
static void test_ground_deceleration_friction() {
  printf("Running test_ground_deceleration_friction...\n");

  _put_on_ground(0x0200);
  flight_throttle = 0;

  for (int i = 0; i < 300; ++i) {
    flight_advance();
  }

  printf("  ground decel end speed: %d, status: %d\n", flight_speed,
         flight_status);
  assert(flight_speed == 0); // Came to a full stop
  assert(!flight_status);
  printf("  PASS\n\n");
}

static void test_ground_braking() {
  printf("Running test_ground_braking...\n");

  // On ground: FLIGHT_INPUT_BRAKE reduces speed
  _put_on_ground(0x0200);
  int16_t speed_before = flight_speed;
  flight_input(FLIGHT_INPUT_BRAKE);
  assert(flight_speed < speed_before);
  assert(flight_speed == speed_before - 32);

  // Continue braking until full stop
  while (flight_speed > 0) {
    flight_input(FLIGHT_INPUT_BRAKE);
  }
  assert(flight_speed == 0);
  assert(!flight_status);

  // Airborne: FLIGHT_INPUT_BRAKE does nothing
  flight_init();
  flight_eye_z = 0x040000;
  int16_t air_speed_before = flight_speed;
  flight_input(FLIGHT_INPUT_BRAKE);
  assert(flight_speed == air_speed_before);

  printf("  PASS\n\n");
}

// 12. Zero fuel flameout transition test
static void test_zero_fuel_flameout_transition() {
  printf("Running test_zero_fuel_flameout_transition...\n");

  flight_init();
  msg_clear();
  flight_fuel = 50; // Low fuel
  flight_throttle = 0x14;

  // Still burning: nothing to say yet.
  flight_advance();
  assert(flight_fuel > 0);
  assert_msg_rendered(nullptr);

  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  assert(flight_fuel == 0);
  assert(flight_throttle == 0);

  // Replay the flameout frame on a fresh aircraft to catch the announcement.
  flight_init();
  msg_clear();
  flight_fuel = 10;
  flight_throttle = 0x14;
  flight_advance();
  assert(flight_fuel == 0);
  assert_msg_rendered("OUT OF FUEL");

  // Dry from here on, so the empty tank does not re-arm the message.
  msg_clear();
  flight_advance();
  assert_msg_rendered(nullptr);

  printf("  PASS\n\n");
}

// 13. Vertical dive terminal velocity clamping test

#ifdef __FLIGHT_AOA__
static void test_vertical_dive_terminal_velocity_clamping() {
  printf("Running test_vertical_dive_terminal_velocity_clamping...\n");

  // Nose truly straight down. Building the frame by writing front = (256, 0,
  // -256) and normalizing gives a *45 degree* dive, not a vertical one, which
  // is worth stating because it is the obvious way to do it and it is wrong.
  flight_init();
  flight_eye_z = 0x040000;
  flight_throttle = kMaxThrottle;
  flight_fuel = 0x0FFFFFFF;
  flight_cam.front = make_vector(0, 0, -256);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(256, 0, 0);
  const mat3_t dive = flight_cam;
  for (int i = 0; i < 1500; ++i) {
    flight_cam = dive;
    flight_eye_z = 0x040000;
    flight_advance();
    flight_throttle = kMaxThrottle;
  }
  const int16_t terminal = flight_speed;
  printf("  vertical dive terminal velocity: %d (clamp %d)\n", terminal,
         (int)kMaxSpeed);

  // Terminal velocity is where drag balances gravity, and it settles under the
  // clamp - so in normal flight the clamp is not what limits the aircraft.
  assert(terminal < (int16_t)kMaxSpeed);
  assert(terminal > 3000); // But it is a real dive, not a mush
  // It is a genuine steady state, not a snapshot of something still moving.
  for (int i = 0; i < 200; ++i) {
    flight_cam = dive;
    flight_eye_z = 0x040000;
    flight_advance();
    flight_throttle = kMaxThrottle;
    assert(flight_speed == terminal);
  }

  // The clamp still bounds flight_speed for everything downstream, whatever
  // the attitude - sound.cc sizes its wind table by kMaxSpeed.
  flight_speed = (int16_t)kMaxSpeed;
  flight_advance();
  assert(flight_speed <= (int16_t)kMaxSpeed);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 13. Vertical dive terminal velocity clamping test
static void test_vertical_dive_terminal_velocity_clamping() {
  printf("Running test_vertical_dive_terminal_velocity_clamping...\n");

  flight_init();
  flight_eye_z = 0x100000;   // High altitude
  flight_cam.front.z = -256; // Vertical dive
  flight_throttle = 0x18;

  for (int i = 0; i < 500; ++i) {
    flight_advance();
    assert(flight_speed >= 0);
    assert(flight_speed <= 0x0F00); // Clamped to kMaxSpeed
  }

  printf("  vertical dive speed: %d (0x%X)\n", flight_speed, flight_speed);
  assert(flight_speed >= 3800);
  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 14. Inverted stall and nose recovery test
static void test_inverted_stall_and_nose_recovery() {
  printf("Running test_inverted_stall_and_nose_recovery...\n");

  flight_init();
  flight_eye_z = 0x100000;
  flight_cam.up.z = -256;
  flight_speed = 0x0200; // Below stall speed

  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  // Nose should pitch down toward ground
  assert(flight_cam.front.z < 0);
  printf("  PASS\n\n");
}

// 15. Ground roll takeoff abort test
static void test_ground_roll_takeoff_abort() {
  printf("Running test_ground_roll_takeoff_abort...\n");

  _put_on_ground(0x0300);
  flight_throttle = 0;

  for (int i = 0; i < 450; ++i) {
    flight_advance();
  }

  assert(flight_speed == 0);
  assert(!flight_status);
  printf("  PASS\n\n");
}

// Squared length, so the orthonormality check needs no square root.
static int32_t _vec_length_sqr(const vec3_t *v) {
  int32_t x = v->x, y = v->y, z = v->z;
  return x * x + y * y + z * z;
}

// 16. Matrix orthonormality under continuous roll test
static void test_matrix_orthonormality_under_continuous_roll() {
  printf("Running test_matrix_orthonormality_under_continuous_roll...\n");

  flight_init();

  for (int i = 0; i < 300; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_LEFT);
    flight_advance();
  }

  // Vector lengths should remain near 256. Compared squared, so no sqrt.
  const int32_t kMinLenSqr = 250 * 250, kMaxLenSqr = 262 * 262;
  int32_t front_l2 = _vec_length_sqr(&flight_cam.front);
  int32_t left_l2 = _vec_length_sqr(&flight_cam.left);
  int32_t up_l2 = _vec_length_sqr(&flight_cam.up);

  assert(front_l2 >= kMinLenSqr && front_l2 <= kMaxLenSqr);
  assert(left_l2 >= kMinLenSqr && left_l2 <= kMaxLenSqr);
  assert(up_l2 >= kMinLenSqr && up_l2 <= kMaxLenSqr);

  printf("  length^2: front %d, left %d, up %d (256^2 = %d)\n", front_l2,
         left_l2, up_l2, 256 * 256);
  printf("  PASS\n\n");
}

// 17. Low altitude stall ground impact test
static void test_low_altitude_stall_ground_impact() {
  printf("Running test_low_altitude_stall_ground_impact...\n");

  flight_init();
  flight_eye_z = 0x2100; // Just above ground
  flight_speed = 0x0200; // Stall speed
  flight_gear = 0;       // Gear up

  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  assert(
      flight_status); // Altitude loss during stall should hit ground and crash
  printf("  PASS\n\n");
}

// 18. Abrupt climb throttle cut test
static void test_abrupt_climb_throttle_cut() {
  printf("Running test_abrupt_climb_throttle_cut...\n");

  flight_init();
  flight_eye_z = 0x100000;
  flight_cam.front.z = 180; // +45 deg climb
  flight_throttle = 0;      // Cut throttle

  for (int i = 0; i < 150; ++i) {
    flight_advance();
    assert(flight_speed >= 0); // No backward flight
  }

  assert(flight_cam.front.z < 180); // Nose pitches down
  printf("  PASS\n\n");
}

// 19. Touchdown exact boundary limits test.
//
// The nose-up pair is set up by parking the aircraft just below the ground
// plane so the clamp puts it exactly on it and the envelope predicate runs.
// That is deliberate here: a front.z = 64 attitude cannot produce a descent at
// any speed above stall (the steepest descending flare just above stall is
// 51), and below stall the stall break trims the nose, which would move the
// very value this test is pinning. Evaluating the predicate directly is the
// only way to isolate the exact boundary. The reachable version of this crash
// - held off until it stalls onto the runway - is covered as a real descent in
// test_touchdown_flare_and_crash_envelope.
// Puts the aircraft at the ground plane with an exact attitude, for the
// boundary tests, which are about one trigger at a time and need the attitude
// the envelope sees to be the attitude they asked for.
//
// It arms just *under* the plane rather than one frame's descent above it. The
// old version placed it above and let it fall, using front.z * speed as the
// descent - which is not the vertical speed any more. Worse, at the pitch
// limit there is no descending arrival with the nose that high that is not
// also stalled: 64 units of pitch against a stall angle of 56 means alpha is
// past the break the moment the flight path is level, and the break would trim
// the nose before the envelope ever saw it. Setting the flight path to keep
// alpha inside the stall is what holds the attitude still, and arming below
// the plane is what guarantees the contact happens anyway.
#ifdef __FLIGHT_AOA__
static void _arm_at_ground(int16_t pitch, int16_t speed, uint8_t gear) {
  flight_init();
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_gear = gear;
  flight_throttle = 0;
  flight_speed = speed;
  // Normalize first and set the attitude second, so front.z is exactly what
  // the caller asked for - the envelope reads it directly, and these tests are
  // about one unit either side of a limit.
  vec_orthonormalize(&flight_cam);
  flight_cam.front.z = pitch;
  // A gentle descent, well inside the sink limit, so that the arrival is a
  // real one and the trigger under test is the only one armed.
  flight_gamma = (int16_t)(-(100 * 4096) / speed);
  flight_eye_z = (int32_t)kGroundZ;
}
#endif // __FLIGHT_AOA__



#ifdef __FLIGHT_AOA__
static void test_touchdown_exact_boundary_limits() {
  printf("Running test_touchdown_exact_boundary_limits...\n");

  // The nose-down boundary, flown as an arrival.
  _arm_at_ground(kMinLandingPitch, kTrimSpeed, 1);
  const int16_t low_ok = flight_cam.front.z;
  flight_advance();
  printf("  arrival pitch %d -> status %d (vspeed %d, limit %d)\n", low_ok,
         flight_status, flight_vspeed, kMaxLandingVSpeed);
  assert(low_ok == kMinLandingPitch);
  assert(flight_vspeed >= kMaxLandingVSpeed); // Sink is not the binding check
  assert(!flight_status);

  _arm_at_ground(kMinLandingPitch - 1, kTrimSpeed, 1);
  const int16_t low_bad = flight_cam.front.z;
  flight_advance();
  printf("  arrival pitch %d -> status %d\n", low_bad, flight_status);
  assert(low_bad < kMinLandingPitch);
  assert(flight_status == FLIGHT_CRASH_PITCH_LOW);

  // The nose-up boundary is a *ground* limit now, and this is where it has to
  // be tested from, because there is no arrival that reaches it.
  //
  // The reason is the model rather than the fixture. kMaxLandingPitch is 64
  // and the wing breaks at 56, so an aircraft descending with the nose above
  // the limit has its flight path below the horizon and its angle of attack
  // past the stall by construction - and the break trims the nose before the
  // envelope is asked. One that is *not* stalled at that attitude is climbing,
  // and never arrives. On the ground neither applies: the runway is the flight
  // path, there is no stall, and the nose stays where it is put.
  for (int16_t pitch = kMaxLandingPitch; pitch <= kMaxLandingPitch + 1;
       ++pitch) {
    flight_init();
    flight_eye_x = 0x200000;
    flight_eye_y = 0x400000;
    flight_eye_z = kGroundZ;
    flight_gear = 1;
    flight_throttle = 0;
    flight_speed = 0x0400;
    flight_advance(); // Into ground mode
    assert(!flight_status);
    vec_orthonormalize(&flight_cam);
    flight_cam.front.z = pitch;
    flight_advance();
    printf("  ground roll at pitch %d -> status %d\n", pitch, flight_status);
    if (pitch <= kMaxLandingPitch) {
      assert(!flight_status);
    } else {
      assert(flight_status == FLIGHT_CRASH_PITCH_HIGH);
    }
  }

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 19. Touchdown exact boundary limits test.
//
// The nose-up pair is set up by parking the aircraft just below the ground
// plane so the clamp puts it exactly on it and the envelope predicate runs.
// That is deliberate here: a front.z = 64 attitude cannot produce a descent at
// any speed above stall (the steepest descending flare just above stall is
// 51), and below stall the stall break trims the nose, which would move the
// very value this test is pinning. Evaluating the predicate directly is the
// only way to isolate the exact boundary. The reachable version of this crash
// - held off until it stalls onto the runway - is covered as a real descent in
// test_touchdown_flare_and_crash_envelope.
static void test_touchdown_exact_boundary_limits() {
  printf("Running test_touchdown_exact_boundary_limits...\n");

  // Pitch = 64 (+15 deg flare) -> PASS
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 64;
  flight_speed = 0x0500;
  int16_t vs = vec_fastmul8p8(64, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(!flight_status);

  // Pitch = 65 -> CRASH
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 65;
  flight_speed = 0x0500;
  vs = vec_fastmul8p8(65, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_status);

  // Nose-down boundary, flown at kTrimSpeed rather than 0x0500. At 0x0500 a
  // front.z = -16 arrival sinks at -234, past kMaxLandingVSpeed, so this pair
  // would be testing trigger 2 instead of the pitch boundary it is named for.
  // At kTrimSpeed the lift deficit is zero, so vertical speed is just the
  // pitch term and stays well inside the sink limit.

  // Pitch = -27 (-6.0 deg pitch down) -> PASS
  flight_init();
  flight_gear = 1;
  flight_cam.front.z = -27;
  flight_speed = kTrimSpeed;
  vs = vec_fastmul8p8(-27, kTrimSpeed);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_vspeed >= kMaxLandingVSpeed); // Sink is not the binding check
  assert(!flight_status);

  // Pitch = -33 -> CRASH
  // Flown with flaps down. _landing_fault() checks sink rate before pitch, and
  // a clean-wing arrival this far nose-down sinks past kMaxLandingVSpeed at
  // every speed - the pitch term alone is -33 * speed / 256 - so sink would
  // always be the binding check and the pitch bound would never be reached.
  // The extra lift from the flaps holds the sink inside the limit; the
  // PITCH_LOW verdict below is what proves it, since a sink violation would
  // have been reported first.
  flight_init();
  flight_gear = 1;
  flight_flap = 1;
  flight_cam.front.z = -33;
  flight_speed = 0x0680;
  vs = vec_fastmul8p8(-33, 0x0680);
  flight_eye_z = 0x2000 - vs - 1;
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_PITCH_LOW);

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 20. Pause / unpause state freeze test
static void test_pause_unpause_state_freeze() {
  printf("Running test_pause_unpause_state_freeze...\n");

  flight_init();
  int32_t orig_x = flight_eye_x;
  int32_t orig_z = flight_eye_z;
  int16_t orig_speed = flight_speed;

  flight_paused = true;
  for (int i = 0; i < 50; ++i) {
    flight_advance();
  }

  assert(flight_eye_x == orig_x);
  assert(flight_eye_z == orig_z);
  assert(flight_speed == orig_speed);

  flight_paused = false;
  flight_advance();
  assert(flight_eye_x != orig_x); // Resumes motion

  printf("  PASS\n\n");
}

// 21. High altitude thrust and lift decay test
static void test_high_altitude_thrust_lift_decay() {
  printf("Running test_high_altitude_thrust_lift_decay...\n");

  // Sea level baseline
  flight_init();
  flight_eye_z = 0x020000;
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t low_alt_speed = flight_speed;

  // High altitude (above 0x080000)
  flight_init();
  flight_eye_z = 0x0A0000;
  flight_throttle = 0x14;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }
  int16_t high_alt_speed = flight_speed;

  assert(high_alt_speed < low_alt_speed); // Thrust & lift decay at altitude
  printf("  low alt speed: %d, high alt speed: %d\n", low_alt_speed,
         high_alt_speed);
  printf("  PASS\n\n");
}

// 22. High altitude stall speed increase test

#ifdef __FLIGHT_AOA__
// 22. Altitude and the stall.
//
// The old model raised its stall *speed* constant with altitude by hand. The
// stall is an angle now, so nothing has to: thinner air makes less lift at the
// same angle and speed, so the wing needs more angle to carry the weight, and
// it reaches the break sooner. The effect is the same and there is no
// constant behind it.
static void test_high_altitude_stall_increases_with_altitude() {
  printf("Running test_high_altitude_stall_increases_with_altitude...\n");

  // Same airspeed and the same held attitude, at sea level and high up.
  const int16_t speed = 0x0480; // Above the sea-level stall speed
  int steps[2] = {-1, -1};
  int16_t alpha[2] = {0, 0};
  const int32_t alt[2] = {0x040000, 0x300000};
  for (int i = 0; i < 2; ++i) {
    flight_init();
    flight_throttle = 0;
    for (int f = 0; f < 200; ++f) {
      flight_cam.front.z = 0;
      flight_speed = speed;
      flight_eye_z = alt[i];
      flight_advance();
      if (flight_stall && steps[i] < 0) {
        steps[i] = f;
        alpha[i] = flight_alpha();
      }
    }
  }
  printf("  sea level: stalls at step %d; at altitude: step %d\n", steps[0],
         steps[1]);
  assert(steps[1] >= 0);              // It stalls up high
  assert(steps[0] < 0 || steps[1] < steps[0]); // and sooner than at sea level

  // The mechanism, stated directly: at the same speed and attitude the thin
  // air makes less lift, so the flight path falls away faster and the angle of
  // attack builds quicker. Nothing here is a stall speed.
  flight_init();
  flight_eye_z = 0x300000;
  flight_speed = speed;
  flight_advance();
  const int16_t high_gamma = flight_gamma;
  flight_init();
  flight_eye_z = 0x040000;
  flight_speed = speed;
  flight_advance();
  printf("  first-step flight path: sea level %d, at altitude %d\n",
         flight_gamma, high_gamma);
  assert(high_gamma < flight_gamma);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// Same case as the AoA build's `test_high_altitude_stall_increases_with_altitude`, under that name so
// main() needs no branch, but asserting the arcade model's behaviour -
// which for several of these is the opposite behaviour.
// 22. High altitude stall speed increase test
static void test_high_altitude_stall_increases_with_altitude() {
  printf("Running test_high_altitude_stall_increases_with_altitude...\n");

  flight_init();
  flight_eye_z = 0x0C0000; // Very high altitude
  flight_speed = 0x0420;   // Above normal stall speed (0x0400), but below
                           // high-alt stall speed
  flight_throttle = 0;

  flight_advance();

  // Should trigger stall pitch-down because stall speed is higher at altitude
  assert(flight_cam.front.z < 0);
  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 23. Inverted flaps stall speed increase test

#ifdef __FLIGHT_AOA__
// 23. Inverted, flaps make it worse (flight.md 4.2).
//
// Flaps are a camber shift, added to C_L rather than multiplied into it, and
// the permanent camber is another. Upright both help; inverted the attitude
// needs a negative C_L and both fight it, so the wing runs out of angle
// sooner. There is no `up.z < 0` case anywhere in the model - the sign of the
// addition does all of it.
static void test_inverted_flaps_stall_speed_increase() {
  printf("Running test_inverted_flaps_stall_speed_increase...\n");

  const int16_t speed = 0x0600; // Well above the upright stall speed
  int steps[2] = {-1, -1};
  for (int flap = 0; flap < 2; ++flap) {
    flight_init();
    flight_throttle = 0;
    flight_flap = (uint8_t)flap;
    flight_cam.up = make_vector(0, 0, -256);
    vec_orthonormalize(&flight_cam);
    const int16_t hu = flight_cam.up.z;
    for (int f = 0; f < 200; ++f) {
      flight_cam.front.z = 0;
      flight_cam.up.z = hu;
      flight_speed = speed;
      flight_eye_z = 0x040000;
      flight_advance();
      if (flight_stall && steps[flap] < 0) {
        steps[flap] = f;
      }
    }
  }
  printf("  inverted at %d: clean stalls at step %d, flaps at step %d\n", speed,
         steps[0], steps[1]);
  assert(steps[1] >= 0);                        // Flaps stall it
  assert(steps[0] < 0 || steps[1] < steps[0]);  // and sooner than clean

  // Upright, the same flaps do the opposite, which is the point of asserting
  // both: one addition, two signs.
  int up_steps[2] = {-1, -1};
  for (int flap = 0; flap < 2; ++flap) {
    flight_init();
    flight_throttle = 0;
    flight_flap = (uint8_t)flap;
    for (int f = 0; f < 200; ++f) {
      flight_cam.front.z = 0;
      flight_speed = (int16_t)kStallSpeedWithoutFlaps - 40;
      flight_eye_z = 0x040000;
      flight_advance();
      if (flight_stall && up_steps[flap] < 0) {
        up_steps[flap] = f;
      }
    }
  }
  printf("  upright just under the clean stall speed: clean %d, flaps %d\n",
         up_steps[0], up_steps[1]);
  assert(up_steps[0] >= 0);  // Clean stalls
  assert(up_steps[1] < 0);   // Flaps do not

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 23. Inverted flaps stall speed increase test
static void test_inverted_flaps_stall_speed_increase() {
  printf("Running test_inverted_flaps_stall_speed_increase...\n");

  // Upright with flaps: stall speed is 0x0340
  flight_init();
  flight_cam.up.z = 256;
  flight_flap = 1;
  flight_speed = 0x03B0; // > 0x0340 (no stall)
  flight_throttle = 0;
  flight_advance();
  assert(flight_cam.front.z == 0); // No stall pitch down

  // Inverted with flaps: stall speed increases to 0x0480 (adverse camber)
  flight_init();
  flight_cam.up.z = -256;
  flight_flap = 1;
  flight_speed = 0x0420; // < 0x0480 (triggers stall!)
  flight_throttle = 0;
  flight_advance();
  assert(flight_cam.front.z < 0); // Triggers stall pitch down

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 24. Idle throttle glide slope speed decay test (Mission 2 fix)
static void test_idle_throttle_glide_slope_speed_decay() {
  printf("Running test_idle_throttle_glide_slope_speed_decay...\n");

  flight_init();
  flight_eye_z = 0x020000;
  flight_speed = 0x0600;    // Initial Mission 2 speed
  flight_throttle = 0;      // Cut throttle to 0%
  flight_cam.front.z = -16; // Gentle glide slope (~ -3.5 deg)

  int16_t start_speed = flight_speed;
  for (int i = 0; i < 100; ++i) {
    flight_advance();
  }

  printf("  glide slope 0%% throttle speed: %d -> %d\n", start_speed,
         flight_speed);
  assert(flight_speed < start_speed); // Speed MUST NOT increase on gentle glide
                                      // slope at 0% throttle
  printf("  PASS\n\n");
}

// 25. Rollout after touchdown test.
// A landing arrives with the flare pitch still set, and ground mode only
// clamps front.z to >= 0. Before the vspeed lock this fed a positive vertical
// speed frame after frame and the aircraft climbed back off the runway while
// still reporting on-ground.

#ifdef __FLIGHT_AOA__
static void test_rollout_stays_on_ground() {
  printf("Running test_rollout_stays_on_ground...\n");

  // Arrives with the flare pitch still set. Armed under the ground plane
  // rather than a frame above it, because front.z * speed is not the descent
  // any more - see _arm_at_ground.
  _arm_at_ground(45, 0x0500, 1);

  // Landing straight ahead, so the whole rollout must stay on front.y == 0.
  assert(flight_cam.front.y == 0);

  flight_advance();
  assert(!flight_status);
  assert(flight_eye_z == 0x2000);
  assert(flight_vspeed == 0);      // Vertical speed zeroed on touchdown
  assert(flight_gamma == 0);       // and so is the flight path: the runway is
                                   // the flight path once the wheels are down
  assert(flight_cam.front.z == 0); // Nose wheel down, once, at touchdown
  // The one normalize the nose drop costs must not swing the heading.
  assert(flight_cam.front.y == 0);

  // The rollout must stay pinned to the ground plane, nose down, and must not
  // wander off the heading it landed on.
  for (int i = 0; i < 600; ++i) {
    flight_advance();
    assert(flight_eye_z == 0x2000);
    assert(flight_vspeed == 0);
    assert(flight_cam.front.z == 0);
    assert(flight_cam.front.y == 0); // Does not wander off the runway heading
    assert(!flight_status);
  }
  printf("  rolled out to speed %d, still on the ground\n", flight_speed);
  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 25. Rollout after touchdown test.
// A landing arrives with the flare pitch still set, and ground mode only
// clamps front.z to >= 0. Before the vspeed lock this fed a positive vertical
// speed frame after frame and the aircraft climbed back off the runway while
// still reporting on-ground.
static void test_rollout_stays_on_ground() {
  printf("Running test_rollout_stays_on_ground...\n");

  flight_init();
  flight_gear = 1;
  flight_cam.front.z = 45; // Flare pitch, inside the landing envelope
  flight_speed = 0x0500;
  flight_throttle = 0;
  int16_t vs = vec_fastmul8p8(45, 0x0500);
  flight_eye_z = 0x2000 - vs - 1;

  // Landing straight ahead, so the whole rollout must stay on front.y == 0.
  assert(flight_cam.front.y == 0);

  flight_advance();
  assert(!flight_status);
  assert(flight_eye_z == 0x2000);
  assert(flight_vspeed == 0);      // Vertical speed zeroed on touchdown
  assert(flight_cam.front.z == 0); // Nose wheel down, once, at touchdown
  // The one normalize the nose drop costs must not swing the heading.
  assert(flight_cam.front.y == 0);

  // The rollout must stay pinned to the ground plane, nose down, and must not
  // wander off the heading it landed on.
  for (int i = 0; i < 600; ++i) {
    flight_advance();
    assert(flight_eye_z == 0x2000);
    assert(flight_vspeed == 0);
    assert(flight_cam.front.z == 0);
    assert(flight_cam.front.y == 0); // Does not wander off the runway heading
  }

  assert(!flight_status);
  assert(flight_speed == 0); // Wheel friction brings it to a stop
  printf("  rollout end: z=%d speed=%d front.z=%d\n", flight_eye_z,
         flight_speed, flight_cam.front.z);
  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 26. Landing envelope: excess sink rate (crash trigger 2).
//
// Note what this test documents about the envelope. Vertical speed is
// front.z * V / 256 - sink_penalty, and both terms are bounded hard for an
// upright arrival: pitch cannot go below kMinLandingPitch without trigger 4
// firing first, and the sink penalty is bounded by the stall speed floor. A
// sweep of every orthonormal attitude that passes the other four checks gives
// a worst upright sink of -301, inside the -0x0180 limit. So trigger 2 is
// only reachable inverted - where lift acts downward and the deficit doubles -
// and it is reachable there precisely because the bank check looks at left.z,
// which is ~0 after a full 180 degree roll. See docs/flight_review.md C5.

#ifdef __FLIGHT_AOA__
static void test_landing_envelope_sink_rate() {
  printf("Running test_landing_envelope_sink_rate...\n");

  // Nose down but with plenty of speed: sink stays inside the limit.
  int16_t fast = _arm_touchdown(-24, 0, 0x0800, 1);
  printf("  nose down at speed 0x0800 -> vspeed %d (limit %d)\n", fast,
         kMaxLandingVSpeed);
  assert(fast < 0);
  assert(fast >= kMaxLandingVSpeed);
  flight_advance();
  assert(!flight_status);

  // Steeper nose down: the sink drives past the limit while pitch, roll,
  // speed, gear and up.z are all still legal.
  int16_t steep = _arm_touchdown(-32, 0, 0x0800, 1);
  printf("  steeper nose down at pitch -32 speed 0x0800 -> vspeed %d\n", steep);
  assert(steep < kMaxLandingVSpeed);                    // Trigger 2 armed
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll); // 3 clear
  assert(flight_cam.up.z >= kMinLandingUpZ);            // 6 clear
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);    // 5 clear
  assert(flight_gear);                                  // 1 clear
  flight_advance();
  assert(flight_cam.front.z >= kMinLandingPitch); // 4 clear at the check
  assert(flight_status == FLIGHT_CRASH_VSPEED);

  // Sweep every arrival above stall speed that passes the other five checks.
  //
  // The property this used to assert - that a level-or-nose-up flare never
  // trips the sink limit - is no longer true, and losing it is the point of
  // the model rather than a regression. Vertical speed used to be front.z
  // times airspeed, so nose-up simply could not descend fast. It is the flight
  // path now, and an aeroplane held nose-high near the stall is descending
  // steeply whatever the nose is doing. What survives is the same rule with
  // the speed named: a flare *with speed in hand* is safe.
  bool reachable = false;
  int16_t worst = 0, worst_pitch = 0, worst_speed = 0;
  int16_t worst_flared_fast = 0;
  // 1.2 times the speed at which the wing runs out of angle - a normal
  // approach speed, and the one the green arc is drawn around.
  const int16_t kApproach = (int16_t)(kStallSpeedWithoutFlaps * 6 / 5);
  for (int16_t roll = -kMaxLandingRoll; roll <= kMaxLandingRoll; roll += 8) {
    for (int16_t p = kMinLandingPitch; p <= kMaxLandingPitch; p += 2) {
      for (int16_t sp = (int16_t)kStallSpeedWithoutFlaps + 6;
           sp <= (int16_t)kMaxLandingSpeed; sp += 40) {
        _arm_touchdown(p, roll, sp, 1);
        int16_t pitch = flight_cam.front.z;
        if (pitch < kMinLandingPitch || pitch > kMaxLandingPitch ||
            _abs16(flight_cam.left.z) > kMaxLandingRoll ||
            flight_cam.up.z < kMinLandingUpZ || flight_vspeed >= 0) {
          continue; // Another trigger owns it, or it is not descending
        }
        if (flight_vspeed < kMaxLandingVSpeed) {
          reachable = true;
        }
        if (flight_vspeed < worst) {
          worst = flight_vspeed;
          worst_pitch = pitch;
          worst_speed = flight_speed;
        }
        if (pitch >= 0 && sp >= kApproach && flight_vspeed < worst_flared_fast) {
          worst_flared_fast = flight_vspeed;
        }
      }
    }
  }
  printf("  above stall: worst sink %d at pitch=%d speed=%d;"
         " worst flare at or above %d: %d; limit %d\n",
         worst, worst_pitch, worst_speed, kApproach, worst_flared_fast,
         kMaxLandingVSpeed);
  assert(reachable); // The limit is live, not dead code
  // A flare flown at an approach speed always survives it.
  assert(worst_flared_fast >= kMaxLandingVSpeed);
  // And the worst arrival in the whole sweep is a slow one, not a fast one -
  // which is the shape of the rule the pilot has to learn now.
  assert(worst_speed < kApproach);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 26. Landing envelope: excess sink rate (crash trigger 2).
//
// Note what this test documents about the envelope. Vertical speed is
// front.z * V / 256 - sink_penalty, and both terms are bounded hard for an
// upright arrival: pitch cannot go below kMinLandingPitch without trigger 4
// firing first, and the sink penalty is bounded by the stall speed floor. A
// sweep of every orthonormal attitude that passes the other four checks gives
// a worst upright sink of -301, inside the -0x0180 limit. So trigger 2 is
// only reachable inverted - where lift acts downward and the deficit doubles -
// and it is reachable there precisely because the bank check looks at left.z,
// which is ~0 after a full 180 degree roll. See docs/flight_review.md C5.
static void test_landing_envelope_sink_rate() {
  printf("Running test_landing_envelope_sink_rate...\n");

  // Nose down but with plenty of speed: sink stays inside the limit.
  int16_t fast = _arm_touchdown(-24, 0, 0x0800, 1);
  printf("  nose down at speed 0x0800 -> vspeed %d (limit %d)\n", fast,
         kMaxLandingVSpeed);
  assert(fast < 0);
  assert(fast >= kMaxLandingVSpeed);
  flight_advance();
  assert(!flight_status);

  // Steep nose down: the sink drives past the limit while
  // pitch, roll, speed, gear and up.z are all still legal.
  int16_t slow = _arm_touchdown(-30, 0, 0x0400, 1);
  printf("  steep nose down at pitch -30 speed 0x0400 -> vspeed %d\n", slow);
  assert(slow < kMaxLandingVSpeed);                     // Trigger 2 armed
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll); // 3 clear
  assert(flight_cam.up.z >= kMinLandingUpZ);            // 6 clear
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);    // 5 clear
  assert(flight_gear);                                  // 1 clear
  flight_advance();
  assert(flight_cam.front.z >= kMinLandingPitch); // 4 clear at the check
  assert(flight_status == FLIGHT_CRASH_VSPEED);

  // Sweep every arrival above stall speed that passes the other five checks.
  // Two properties matter, and flight.md 5.3 states both:
  //   - the limit is reachable at all (otherwise trigger 2 is dead code), and
  //   - a level-or-nose-up flare never trips it.
  // Arrivals below stall speed are excluded because the stall break has
  // already driven the nose past kMinLandingPitch, so trigger 4 owns them.
  bool reachable = false;
  int16_t worst_flared = 0;
  int16_t worst = 0, worst_pitch = 0, worst_speed = 0;
  for (int16_t roll = -kMaxLandingRoll; roll <= kMaxLandingRoll; roll += 4) {
    for (int16_t p = kMinLandingPitch; p <= kMaxLandingPitch; ++p) {
      for (int16_t s = (int16_t)kStallSpeedWithoutFlaps + 6;
           s <= (int16_t)kMaxLandingSpeed; s += 10) {
        _arm_touchdown(p, roll, s, 1);
        int16_t pitch = flight_cam.front.z;
        if (pitch < kMinLandingPitch || pitch > kMaxLandingPitch ||
            _abs16(flight_cam.left.z) > kMaxLandingRoll ||
            flight_cam.up.z < kMinLandingUpZ || flight_vspeed >= 0) {
          continue; // Another trigger owns it, or it is not descending
        }
        if (flight_vspeed < kMaxLandingVSpeed) {
          reachable = true;
        }
        if (flight_vspeed < worst) {
          worst = flight_vspeed;
          worst_pitch = pitch;
          worst_speed = flight_speed;
        }
        if (pitch >= 0 && flight_vspeed < worst_flared) {
          worst_flared = flight_vspeed;
        }
      }
    }
  }
  printf("  above stall: worst sink %d at pitch=%d speed=%d;"
         " worst flared (front.z >= 0) %d; limit %d\n",
         worst, worst_pitch, worst_speed, worst_flared, kMaxLandingVSpeed);
  assert(reachable);                         // The limit is live, not dead code
  assert(worst_flared >= kMaxLandingVSpeed); // A proper flare always survives

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 27. Landing envelope: excess bank angle (crash trigger 3).
static void test_landing_envelope_bank_angle() {
  printf("Running test_landing_envelope_bank_angle...\n");

  // Exactly at kMaxLandingRoll -> lands.
  _arm_touchdown(0, kMaxLandingRoll, 0x0500, 1);
  printf("  touchdown left.z: %d (limit %d)\n", flight_cam.left.z,
         kMaxLandingRoll);
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll);
  flight_advance();
  assert(!flight_status);

  // One unit past the limit -> crash. The boundary is exact.
  _arm_touchdown(0, kMaxLandingRoll + 1, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_ROLL);

  // A wingtip-down arrival -> crash.
  _arm_touchdown(0, 120, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_status);

  // Symmetric: the other wing down crashes too.
  _arm_touchdown(0, -120, 0x0500, 1);
  assert(_abs16(flight_cam.left.z) > kMaxLandingRoll);
  flight_advance();
  assert(flight_status);

  printf("  PASS\n\n");
}

// 28. Landing envelope: excess touchdown speed (crash trigger 5).
static void test_landing_envelope_touchdown_speed() {
  printf("Running test_landing_envelope_touchdown_speed...\n");

  // At the limit -> lands.
  _arm_touchdown(0, 0, (int16_t)kMaxLandingSpeed, 1);
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);
  flight_advance();
  printf("  at limit: speed %d, status %d\n", flight_speed, flight_status);
  assert(!flight_status);

  // Over the limit -> crash. Drag bleeds a little speed during the frame, so
  // arrive with enough margin that the check still sees an overspeed.
  _arm_touchdown(0, 0, (int16_t)kMaxLandingSpeed + 0x0100, 1);
  flight_advance();
  printf("  over limit: speed %d, status %d\n", flight_speed, flight_status);
  assert(flight_status == FLIGHT_CRASH_SPEED);

  printf("  PASS\n\n");
}

// 29. Landing envelope: belly-up arrival (crash trigger 6).
// left.z returns to ~0 after a full 180 degree roll, so the bank check does
// not see an inverted arrival. up.z is the attitude the roll limit is really
// trying to express.
static void test_landing_envelope_inverted() {
  printf("Running test_landing_envelope_inverted...\n");

  // Upright reference at the same pitch and speed: lands.
  _arm_touchdown(0, 0, 0x0500, 1);
  flight_advance();
  assert(!flight_status);

  // Same arrival, belly up: crash, and specifically not because of any of the
  // other five triggers. 26 roll steps is a full roll to inverted, which is
  // also how left.z gets back inside the bank limit.
  _arm_touchdown(0, 0, 0x0500, 1, /*roll_steps=*/26);
  printf(
      "  inverted arrival: up.z=%d left.z=%d front.z=%d vspeed=%d speed=%d\n",
      flight_cam.up.z, flight_cam.left.z, flight_cam.front.z, flight_vspeed,
      flight_speed);
  assert(flight_cam.up.z < kMinLandingUpZ);
  assert(_abs16(flight_cam.left.z) <= kMaxLandingRoll); // Trigger 3 blind here
  assert(flight_cam.front.z >= kMinLandingPitch);
  assert(flight_cam.front.z <= kMaxLandingPitch);
  assert(flight_speed <= (int16_t)kMaxLandingSpeed);
  assert(flight_gear);
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_INVERTED);

  // A legal nose-up flare must not trip the new check: up.z falls with pitch,
  // so the threshold has to stay at 0 rather than a tight cos(roll) bound.
  _arm_touchdown(kMaxLandingPitch, 0, 0x0500, 1);
  printf("  max flare: front.z=%d up.z=%d\n", flight_cam.front.z,
         flight_cam.up.z);
  assert(flight_cam.up.z < 256); // Pitch really does reduce up.z
  assert(flight_cam.up.z >= kMinLandingUpZ);
  flight_advance();
  assert(!flight_status);

  printf("  PASS\n\n");
}

// 30. Inverted near-vertical stall break test.
// Above kMaxStallPitchZ the break is a body-axis rotation. A body "pitch down"
// moves front.z by -up.z/16, so when inverted it drives the nose further UP.
// The rotation has to be chosen by the sign of up.z for the nose to fall
// toward the ground at every attitude, as flight.md 2.2 requires.
static void test_inverted_high_nose_stall_breaks_downward() {
  printf("Running test_inverted_high_nose_stall_breaks_downward...\n");

  for (int inverted = 0; inverted < 2; ++inverted) {
    flight_init();
    flight_eye_z = 0x0400000;
    flight_throttle = 0;
    flight_fuel = 0;
    flight_speed = 0x0100; // Well below stall -> break fires every frame

    // Nose high, inside the dead spot, with up.z of the requested sign. With
    // front near vertical up is nearly horizontal, so |up.z| is small - but
    // its sign is what selects the rotation. Only front and up need seeding;
    // vec_orthonormalize derives left from up x front.
    flight_cam.front = make_vector(70, 0, 246);
    flight_cam.up =
        inverted ? make_vector(246, 0, -70) : make_vector(-246, 0, 70);
    flight_cam.left = make_vector(0, 256, 0);
    vec_orthonormalize(&flight_cam);

    assert(flight_cam.front.z > 224); // In the dead spot
    assert(inverted ? (flight_cam.up.z < 0) : (flight_cam.up.z > 0));
    int16_t start_z = flight_cam.front.z;

    for (int i = 0; i < 200; ++i) {
      flight_advance();
    }

    printf("  %-8s front.z %d -> %d\n", inverted ? "inverted" : "upright",
           start_z, flight_cam.front.z);
    // The nose must fall away from the vertical, whichever way up we started.
    assert(flight_cam.front.z < start_z);
    assert(flight_cam.front.z < 128); // Past 30 degrees of nose drop
  }

  printf("  PASS\n\n");
}

// Flies a settled glide at the given pitch with the engine out and returns the
// glide ratio (horizontal distance travelled / altitude lost) scaled by 1000.
// Returns 0 if the aircraft could not hold the glide.
#ifdef __FLIGHT_AOA__
static int32_t _glide_ratio_x1000(int16_t pitch) {
  flight_init();
  flight_fuel = 0; // Engine out
  flight_throttle = 0;
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, 256);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);
  int16_t hp = flight_cam.front.z, hu = flight_cam.up.z;

  // Flown at a pinned altitude with the descent accumulated by hand, rather
  // than by letting the aircraft actually fall from a great height. Two
  // reasons, and the first is a bug this replaces: the old version started at
  // 0x0400000, which is far above the 0x080000 density knee, so it measured
  // the glide in half-density air. That barely troubled a model whose only
  // drag was parasite; it wrecks one with an induced term, because thin air
  // means more C_L for the same weight and induced drag goes as its square.
  // Second, a glide steep enough to be interesting now reaches the ground
  // inside the measuring run.
  const int32_t kZ = 0x040000;
  int32_t dh = 0, dz = 0;
  for (int i = 0; i < 900; ++i) { // Settle
    flight_cam.front.z = hp;
    flight_cam.up.z = hu;
    flight_eye_z = kZ;
    flight_advance();
  }
  if (flight_status) {
    return 0;
  }
  for (int i = 0; i < 300; ++i) { // Measure
    flight_cam.front.z = hp;
    flight_cam.up.z = hu;
    const int32_t x0 = flight_eye_x;
    flight_eye_z = kZ;
    flight_advance();
    // The glide is flown straight ahead, so the ground track is along x alone
    // and no square root is needed for the horizontal distance.
    dh += flight_eye_x - x0;
    dz += kZ - flight_eye_z;
  }
  if (dz <= 0 || dh <= 0) {
    return 0;
  }
  return (int32_t)(((int64_t)dh * 1000) / dz);
}
#else // !__FLIGHT_AOA__
static int32_t _glide_ratio_x1000(int16_t pitch) {
  flight_init();
  flight_eye_z = 0x0400000; // High enough for a long glide
  flight_fuel = 0;          // Engine out
  flight_throttle = 0;
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, 256);
  flight_cam.front.z = pitch;
  vec_orthonormalize(&flight_cam);
  int16_t hp = flight_cam.front.z, hu = flight_cam.up.z;

  for (int i = 0; i < 600; ++i) { // Settle
    flight_cam.front.z = hp;
    flight_cam.up.z = hu;
    flight_advance();
  }
  if (flight_status) {
    return 0;
  }
  int32_t x0 = flight_eye_x, y0 = flight_eye_y, z0 = flight_eye_z;
  for (int i = 0; i < 200; ++i) { // Measure
    flight_cam.front.z = hp;
    flight_cam.up.z = hu;
    flight_advance();
  }
  // The glide is flown straight ahead, so the ground track is along x alone
  // and no square root is needed for the horizontal distance.
  assert(flight_eye_y == y0);
  int32_t dh = flight_eye_x - x0;
  int32_t dz = z0 - flight_eye_z;
  if (dz <= 0 || dh <= 0) {
    return 0;
  }
  return (int32_t)(((int64_t)dh * 1000) / dz);
}
#endif // __FLIGHT_AOA__

// 31. Optimal glide angle test (flight.md 6.2).

#ifdef __FLIGHT_AOA__
static void test_optimal_glide_angle() {
  printf("Running test_optimal_glide_angle...\n");

  int32_t best = 0;
  int16_t best_pitch = 0;
  for (int16_t p = -2; p >= -120; --p) {
    int32_t r = _glide_ratio_x1000(p);
    if (r > best) {
      best = r;
      best_pitch = p;
    }
  }
  printf("  best glide ratio %d.%03d:1 at front.z=%d\n", best / 1000,
         best % 1000, best_pitch);

  // Nose down, and a real glide rather than a mush.
  assert(best_pitch < 0);
  assert(best > 5000); // Better than 5:1

  // It is a peak: much shallower and much steeper are both worse. The band is
  // wide because the peak sits on a shelf - settled glide speed moves in
  // steps, and either side of the shelf the aircraft trades airspeed for
  // angle of attack at nearly the same cost.
  const int32_t shallow = _glide_ratio_x1000(-4);
  const int32_t steep = _glide_ratio_x1000(-110);
  printf("  shallow(-4) %d, steep(-110) %d\n", shallow, steep);
  assert(shallow < best);
  assert(steep < best);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 31. Optimal glide angle test (flight.md 6.2).
static void test_optimal_glide_angle() {
  printf("Running test_optimal_glide_angle...\n");

  int32_t best = 0;
  int16_t best_pitch = 0;
  for (int16_t p = -2; p >= -120; --p) {
    int32_t r = _glide_ratio_x1000(p);
    if (r > best) {
      best = r;
      best_pitch = p;
    }
  }
  printf("  best glide ratio %d.%03d:1 at front.z=%d\n", best / 1000,
         best % 1000, best_pitch);

  // The documented optimum is front.z = -49 (~ -11 deg). Allow a band rather
  // than an exact value: settled glide speed moves in steps, so the peak sits
  // on a shelf.
  assert(best_pitch <= -40 && best_pitch >= -60);
  assert(best > 4500); // Better than 4.5:1

  // And it really is a peak: both a shallower and a steeper glide are worse.
  int32_t shallow = _glide_ratio_x1000(-25);
  int32_t steep = _glide_ratio_x1000(-100);
  printf("  shallow(-25) %d, steep(-100) %d\n", shallow, steep);
  assert(shallow < best);
  assert(steep < best);

  // Flatter than about -8 degrees the aircraft cannot hold glide speed.
  assert(_glide_ratio_x1000(-5) < 1000);

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// Holds a bank for `frames` frames starting from a due-x heading, and leaves
// the resulting forward vector in flight_cam. Turn magnitude is read off
// front.y: the runs below stay inside a quarter turn, where |front.y| grows
// monotonically with the angle, so no inverse trig is needed.
static void _turn_over(int roll_steps, int16_t speed, int frames) {
  flight_init();
  flight_eye_z = 0x040000;
  flight_throttle = 0x14;
  flight_fuel = 0x0FFFFFFF;
  flight_speed = speed;
  flight_cam.front = make_vector(256, 0, 0);
  flight_cam.left = make_vector(0, 256, 0);
  flight_cam.up = make_vector(0, 0, 256);
  _roll_by(roll_steps);
  for (int i = 0; i < frames; ++i) {
    flight_advance();
    flight_throttle = 0x14;
    flight_speed = speed; // Hold the speed so only bank varies
  }
}

// 32. Turn rate test (flight.md 3.1).
// Yaw rate is rot = left.z >> 5 - a function of bank angle only. It is
// explicitly NOT proportional to airspeed; the spec used to claim left.z * V.

#ifdef __FLIGHT_AOA__
// 32. Turn rate is the horizontal half of lift (flight.md 3.1).
//
// This test used to assert the opposite of its own name's replacement: that
// the turn rate did *not* depend on airspeed, because the old model turned at
// `left.z >> 5` whatever the wing was doing (flight_review.md B4). The turn is
// now L sin(bank) over momentum, the same equation and the same 1/V as the
// flight path, so it depends on both - and a level turn's rate is g tan(bank)
// / V, which falls as the aircraft goes faster.
static void test_turn_rate_scales_with_lift_over_speed() {
  printf("Running test_turn_rate_scales_with_lift_over_speed...\n");

  // Wings level: no turn at all - the forward vector is untouched.
  _turn_over(0, 1800, 60);
  printf("  level      -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(_same_heading(256, 0));

  // Banked: the aircraft turns, and steeper bank turns faster. Both runs stay
  // inside a quarter turn, so |front.y| orders them.
  _turn_over(4, 1800, 60);
  const int16_t medium_y = _abs16(flight_cam.front.y);
  printf("  4 steps    -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(medium_y != 0);
  assert(flight_cam.front.x > 0); // Still inside the first quarter turn

  _turn_over(8, 1800, 60);
  const int16_t steep_y = _abs16(flight_cam.front.y);
  printf("  8 steps    -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(flight_cam.front.x > 0);
  assert(steep_y > medium_y);

  // Same bank, flown level at two airspeeds: the slower turn is the tighter
  // one. Each is flown at the attitude that trims that speed level in the
  // turn, because comparing rates across speeds means comparing them at the
  // same lift - a fixed attitude would compare a 1 g turn with a 2 g one.
  int16_t deg[2] = {0, 0};
  const int16_t entry[2] = {1400, 2400};
  for (int i = 0; i < 2; ++i) {
    int16_t best_pitch = 0;
    int32_t best_climb = 0x7FFFFFFF;
    for (int16_t p = -16; p <= 120; p += 2) {
      flight_init();
      flight_eye_z = 0x040000;
      flight_throttle = 0x18;
      flight_fuel = 0x0FFFFFFF;
      flight_speed = entry[i];
      _roll_by(8);
      const int16_t hu = flight_cam.up.z, hl = flight_cam.left.z;
      int32_t vsum = 0;
      for (int f = 0; f < 60; ++f) {
        flight_cam.front.z = p;
        flight_cam.up.z = hu;
        flight_cam.left.z = hl;
        vec_orthonormalize(&flight_cam);
        flight_speed = entry[i];
        flight_eye_z = 0x040000;
        flight_advance();
        flight_throttle = 0x18;
        vsum += flight_vspeed;
      }
      const int32_t off = vsum < 0 ? -vsum : vsum;
      if (!flight_status && off < best_climb) {
        best_climb = off;
        best_pitch = p;
      }
    }
    // Fly the level turn and count the heading change.
    flight_init();
    flight_eye_z = 0x040000;
    flight_throttle = 0x18;
    flight_fuel = 0x0FFFFFFF;
    flight_speed = entry[i];
    _roll_by(8);
    const int16_t hu = flight_cam.up.z, hl = flight_cam.left.z;
    double turned = 0.0;
    double hprev = atan2((double)flight_cam.front.y, (double)flight_cam.front.x);
    for (int f = 0; f < 60; ++f) {
      flight_cam.front.z = best_pitch;
      flight_cam.up.z = hu;
      flight_cam.left.z = hl;
      vec_orthonormalize(&flight_cam);
      flight_speed = entry[i];
      flight_eye_z = 0x040000;
      flight_advance();
      flight_throttle = 0x18;
      const double h =
          atan2((double)flight_cam.front.y, (double)flight_cam.front.x);
      double d = h - hprev;
      while (d > M_PI) d -= 2 * M_PI;
      while (d < -M_PI) d += 2 * M_PI;
      turned += d;
      hprev = h;
    }
    deg[i] = (int16_t)(_abs16((int16_t)(turned * 180.0 / M_PI)));
    printf("  level turn at %d: pitch %d, %d degrees in 60 steps\n", entry[i],
           best_pitch, deg[i]);
  }
  assert(deg[0] > 0 && deg[1] > 0);
  assert(deg[0] > deg[1]); // Slower is tighter

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// Same case as the AoA build's `test_turn_rate_scales_with_lift_over_speed`, under that name so
// main() needs no branch, but asserting the arcade model's behaviour -
// which for several of these is the opposite behaviour.
// 32. Turn rate test (flight.md 3.1).
// Yaw rate is rot = left.z >> 5 - a function of bank angle only. It is
// explicitly NOT proportional to airspeed; the spec used to claim left.z * V.
static void test_turn_rate_scales_with_lift_over_speed() {
  printf("Running test_turn_rate_scales_with_lift_over_speed...\n");

  // Wings level: no turn at all - the forward vector is untouched.
  _turn_over(0, 1800, 60);
  printf("  level      -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(_same_heading(256, 0));

  // Banked: the aircraft turns, and steeper bank turns faster. Both runs stay
  // inside a quarter turn, so |front.y| orders them.
  _turn_over(4, 1800, 60);
  int16_t medium_y = _abs16(flight_cam.front.y);
  int16_t medium_x = flight_cam.front.x;
  printf("  4 steps    -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(medium_y != 0);
  assert(medium_x > 0); // Still inside the first quarter turn

  _turn_over(10, 1800, 60);
  int16_t steep_y = _abs16(flight_cam.front.y);
  printf("  10 steps   -> front=(%4d,%4d) heading %2d/%d\n", flight_cam.front.x,
         flight_cam.front.y, _heading(), kHeadingMax);
  assert(flight_cam.front.x > 0);
  assert(steep_y > medium_y);

  // Same bank at three airspeeds: bit for bit identical forward vector.
  _turn_over(8, 1200, 60);
  int16_t fx = flight_cam.front.x, fy = flight_cam.front.y;
  printf("  8 steps at 1200/1800/2400 -> (%d,%d)", fx, fy);
  assert(fy != 0); // It did turn
  _turn_over(8, 1800, 60);
  printf(" (%d,%d)", flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(fx, fy));
  _turn_over(8, 2400, 60);
  printf(" (%d,%d)\n", flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(fx, fy));

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 33. Altitude loss in a banked turn (flight.md 3.1).
// Banking tilts the lift vector, so up.z falls and the vertical component of
// lift no longer carries the weight. Without added pitch or throttle the
// aircraft turns and descends.

#ifdef __FLIGHT_AOA__
static void test_banked_turn_loses_altitude() {
  printf("Running test_banked_turn_loses_altitude...\n");

  // Held at the wings-level trim attitude, so the bank is the only variable.
  // Altitude is accumulated rather than flown, because a 70 degree bank with
  // no pitch compensation now reaches the ground inside the run.
  int16_t trim_pitch = 0;
  assert(_level_trim(0x18, 256, 0, &trim_pitch));

  int32_t dz[3];
  const int roll_steps[3] = {0, 6, 10}; // Level, ~left.z 169, ~left.z 239
  for (int i = 0; i < 3; ++i) {
    flight_init();
    flight_eye_z = 0x040000;
    flight_throttle = 0x18; // Full throttle, so thrust is not the variable
    flight_fuel = 0x0FFFFFFF;
    flight_speed = 0x0900;
    flight_cam.front = make_vector(256, 0, 0);
    flight_cam.left = make_vector(0, 256, 0);
    flight_cam.up = make_vector(0, 0, 256);
    _roll_by(roll_steps[i]);
    const int16_t held_roll = flight_cam.left.z;
    dz[i] = 0;
    for (int f = 0; f < 200; ++f) {
      flight_cam.left.z = held_roll;
      flight_cam.front.z = trim_pitch; // Level trim, no extra pull
      const int32_t z0 = 0x040000;
      flight_eye_z = z0;
      flight_advance();
      flight_throttle = 0x18;
      dz[i] += flight_eye_z - z0;
    }
    printf("  left.z=%3d -> dz=%8d, speed=%d, alpha=%d\n", held_roll, dz[i],
           flight_speed, flight_alpha());
  }

  assert(dz[0] >= 0);    // Wings level at its own trim: holds altitude
  assert(dz[1] < 0);     // 45 deg bank: descends
  assert(dz[2] < dz[1]); // 70 deg bank: descends faster

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// 33. Altitude loss in a banked turn (flight.md 3.1).
// Banking tilts the lift vector, so up.z falls and the vertical component of
// lift no longer carries the weight. Without added pitch or throttle the
// aircraft turns and descends.
static void test_banked_turn_loses_altitude() {
  printf("Running test_banked_turn_loses_altitude...\n");

  int32_t dz[3];
  const int roll_steps[3] = {0, 6, 10}; // Level, ~left.z 169, ~left.z 239
  for (int i = 0; i < 3; ++i) {
    flight_init();
    flight_eye_z = 0x040000;
    flight_throttle = 0x18; // Full throttle, so thrust is not the variable
    flight_fuel = 0x0FFFFFFF;
    flight_speed = 0x0900;
    flight_cam.front = make_vector(256, 0, 0);
    flight_cam.left = make_vector(0, 256, 0);
    flight_cam.up = make_vector(0, 0, 256);
    _roll_by(roll_steps[i]);
    int16_t held_roll = flight_cam.left.z;
    int32_t z0 = flight_eye_z;
    for (int f = 0; f < 200; ++f) {
      flight_cam.left.z = held_roll;
      flight_cam.front.z = 0; // No pitch input to compensate
      flight_advance();
      flight_throttle = 0x18;
    }
    dz[i] = flight_eye_z - z0;
    printf("  left.z=%3d -> dz=%8d, speed=%d\n", held_roll, dz[i],
           flight_speed);
  }

  assert(dz[0] == 0);    // Wings level at full throttle: holds altitude
  assert(dz[1] < 0);     // 45 deg bank: descends
  assert(dz[2] < dz[1]); // 70 deg bank: descends faster

  printf("  PASS\n\n");
}
#endif // __FLIGHT_AOA__

// 34. Ground steering test (flight.md 5.1).
// On the ground the roll inputs are remapped to nose-wheel steering, and the
// wings stay locked level.
static void test_ground_steering() {
  printf("Running test_ground_steering...\n");

  _put_on_ground(0x0300);
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_LEFT);
    flight_advance();
  }
  int16_t left_y = flight_cam.front.y;
  printf("  20x ROLL_LEFT  -> front=(%4d,%4d) heading %2d/%d, left.z=%d\n",
         flight_cam.front.x, left_y, _heading(), kHeadingMax,
         flight_cam.left.z);
  assert(left_y != 0);            // It steered
  assert(flight_cam.left.z == 0); // ...without banking
  assert(flight_eye_z == kGroundZ);
  assert(!flight_status);

  _put_on_ground(0x0300);
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    flight_advance();
  }
  int16_t right_y = flight_cam.front.y;
  printf("  20x ROLL_RIGHT -> front=(%4d,%4d) heading %2d/%d, left.z=%d\n",
         flight_cam.front.x, right_y, _heading(), kHeadingMax,
         flight_cam.left.z);
  assert(right_y != 0);
  assert(flight_cam.left.z == 0);
  assert(!flight_status);

  // Symmetric: same magnitude, opposite sign.
  assert((left_y > 0) != (right_y > 0));
  assert(_abs16(left_y) == _abs16(right_y));

  // Roll input and the dedicated yaw input do exactly the same thing.
  _put_on_ground(0x0300);
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_YAW_LEFT);
    flight_advance();
  }
  assert(flight_cam.front.y == left_y);

  // A steered heading must hold, bit for bit. Rebuilding left/up from front
  // every frame used to ratchet it back toward the nearest axis:
  // vec_normalize truncates when it scales the vector back to length 256, so
  // the dominant component gains a unit first. 29 degrees decayed to 0 in
  // ~300 frames.
  int16_t held_x = flight_cam.front.x, held_y = flight_cam.front.y;
  for (int i = 0; i < 400; ++i) {
    flight_advance(); // Coasting, no further input
  }
  printf("  held 400 frames: (%d,%d) -> (%d,%d)\n", held_x, held_y,
         flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(held_x, held_y));
  assert(flight_cam.left.z == 0); // Still level while holding heading

  // Steering is nose wheel steering, so it needs the wheels turning.
  _put_on_ground(0);
  assert(flight_speed == 0);
  held_x = flight_cam.front.x;
  held_y = flight_cam.front.y;
  for (int i = 0; i < 20; ++i) {
    flight_input(FLIGHT_INPUT_YAW_LEFT);
    flight_input(FLIGHT_INPUT_ROLL_RIGHT);
    flight_advance();
  }
  printf("  stationary: (%d,%d) -> (%d,%d)\n", held_x, held_y,
         flight_cam.front.x, flight_cam.front.y);
  assert(_same_heading(held_x, held_y)); // Parked aircraft does not pivot

  // ...and works again as soon as it rolls.
  _put_on_ground(0x0300);
  held_x = flight_cam.front.x;
  held_y = flight_cam.front.y;
  flight_input(FLIGHT_INPUT_YAW_LEFT);
  flight_advance();
  assert(!_same_heading(held_x, held_y));

  printf("  PASS\n\n");
}

// 35. Takeoff roll speed margin test.
// The envelope check runs every frame at ground level, so the touchdown speed
// limit would also police the takeoff roll. kMaxGroundSpeed exists to keep a
// full-throttle ground roll clear of it.
static void test_takeoff_roll_speed_margin() {
  printf("Running test_takeoff_roll_speed_margin...\n");

  _put_on_ground(0x0100);
  flight_throttle = 0x18; // Firewall it
  flight_fuel = 0x0FFFFFFF;
  int16_t top = 0;
  for (int i = 0; i < 2000; ++i) {
    flight_advance();
    flight_throttle = 0x18;
    flight_fuel = 0x0FFFFFFF;
    if (flight_speed > top) {
      top = flight_speed;
    }
    assert(!flight_status); // A takeoff roll must never crash by itself
    assert(flight_eye_z == kGroundZ);
  }
  printf("  full throttle ground top speed: %d; landing limit %d,"
         " ground limit %d\n",
         top, (int)kMaxLandingSpeed, (int)kMaxGroundSpeed);

  // The roll really does run past the touchdown limit - that is the trap this
  // guards - and stays well inside the ground limit.
  assert(top > (int16_t)kMaxLandingSpeed - 400); // Close to it, at least
  assert(top < (int16_t)kMaxGroundSpeed);
  assert((int16_t)kMaxGroundSpeed - top > 800); // Real headroom, not 270

  printf("  PASS\n\n");
}

// Declared in host_vec.cc.
int host_vec_selfcheck();

// 0. The host multiply must match the 6502 assembly, or nothing below this
// line says anything about the real target.
static void test_host_multiply_matches_c64() {
  printf("Running test_host_multiply_matches_c64...\n");
  int mismatches = host_vec_selfcheck();
  if (mismatches) {
    printf("  %d mismatches against the vec_asm.cc contract\n", mismatches);
  }
  assert(mismatches == 0);
  printf("  PASS\n\n");
}

static void test_mission_waypoint_constraints() {
  printf("Running test_mission_waypoint_constraints...\n");

  for (int i = 0; i < kMissionCount; ++i) {
    mission_completed[i] = false;
  }

  // Test 1: Mission 01 Takeoff (WP_MIN_1000FT)
  flight_init_from_mission(0);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Below 1000ft (e.g. 0x01F000)
  flight_speed = 0x60;
  flight_eye_z = 0x01F000;
  flight_advance();
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Reach 1000ft (0x025000)
  flight_vspeed = 0;
  flight_eye_z = 0x025000;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[0]);

  // Test 2: Mission 02 Landing (WP_LANDED)
  flight_init_from_mission(1);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[1]);

  // On ground with gear down and stopped on Runway 1
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Contact frame: sets model_on_ground = true at trim speed
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[1]);

  // Test 3: Multi-waypoint mission 03 (Solo Flight: WP_MIN_1000FT then WP_LANDED)
  flight_init_from_mission(2);
  assert(flight_current_wp == 0);
  assert(flight_nav == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Below 1000ft (0x01F000)
  flight_speed = 0x60;
  flight_eye_z = 0x01F000;
  flight_advance();
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Reach 1000ft (0x021000) - waypoint 0 met
  flight_speed = 0x60;
  flight_eye_z = 0x021000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_nav == 0); // flight_nav does NOT auto-advance now
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[2]);

  // Land on Runway 1 with gear down
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Contact frame: sets model_on_ground = true at trim speed
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[2]);

  // Test 4: Manual NAV toggle with N key
  flight_init(); // Has 2 nav points
  assert(flight_nav == 0);
  flight_input(FLIGHT_INPUT_TOGGLE_NAV);
  assert(flight_nav == 1);
  flight_input(FLIGHT_INPUT_TOGGLE_NAV);
  assert(flight_nav == 0);

  printf("  PASS\n\n");
}

static void test_mission_01_takeoff_completion() {
  printf("Running test_mission_01_takeoff_completion...\n");

  msg_clear();
  mission_completed[0] = false;
  flight_init_from_mission(0);
  msg_show(kMissionTitles[0]);

  assert_msg_rendered(kMissionTitles[0]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Below 1000ft (e.g. 0x01F000)
  flight_speed = 0x60;
  flight_eye_z = 0x01F000;
  flight_advance();
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[0]);

  // Reach 1000ft (0x025000)
  flight_vspeed = 0;
  flight_eye_z = 0x025000;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[0]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

// Completing a mission is a goal reached, not the end of the flight. The
// physics and the controls stay live afterwards, the announcement is
// temporary, and only a crash freezes anything.
static void test_flight_continues_after_mission_completion() {
  printf("Running test_flight_continues_after_mission_completion...\n");

  msg_clear();
  mission_completed[0] = false;
  flight_init_from_mission(0);

  // Mission 01 completes on passing 1000ft.
  flight_speed = 0x60;
  flight_vspeed = 0;
  flight_eye_z = 0x025000;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[0]);
  assert(!flight_crashed());
  // Announced by flight.cc itself, not by the caller.
  assert_msg_rendered("MISSION COMPLETE!");

  // The aircraft is still flying: the next frame still moves it.
  int32_t x_before = flight_eye_x;
  int32_t y_before = flight_eye_y;
  flight_advance();
  assert(flight_eye_x != x_before || flight_eye_y != y_before);

  // And the controls still answer.
  uint8_t throttle_before = flight_throttle;
  flight_input(FLIGHT_INPUT_THROTTLE_UP);
  assert(flight_throttle == throttle_before + 1);

  // The waypoint is behind us, so the message fires once and is not repeated
  // frame after frame.
  msg_clear();
  flight_advance();
  assert_msg_rendered(nullptr);

  // It was a timed message, so the row frees itself for later warnings
  // instead of sitting there like a crash report.
  msg_show("MISSION COMPLETE!");
  uint16_t frames = 0;
  while (msg_active() && frames < 1000) {
    msg_update();
    ++frames;
  }
  assert(!msg_active());

  // A crash after the fact still ends the flight.
  flight_status = FLIGHT_CRASH_SPEED;
  assert(flight_crashed());
  x_before = flight_eye_x;
  y_before = flight_eye_y;
  throttle_before = flight_throttle;
  flight_advance();
  flight_input(FLIGHT_INPUT_THROTTLE_UP);
  assert(flight_eye_x == x_before && flight_eye_y == y_before);
  assert(flight_throttle == throttle_before);

  printf("  PASS\n\n");
}

static void test_mission_02_landing_completion() {
  printf("Running test_mission_02_landing_completion...\n");

  msg_clear();
  mission_completed[1] = false;
  flight_init_from_mission(1);
  msg_show(kMissionTitles[1]);

  assert_msg_rendered(kMissionTitles[1]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[1]);

  // On ground with gear down on Runway 1
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Touchdown frame
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[1]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_mission_03_solo_flight_completion() {
  printf("Running test_mission_03_solo_flight_completion...\n");

  msg_clear();
  mission_completed[2] = false;
  flight_init_from_mission(2);
  msg_show(kMissionTitles[2]);

  assert_msg_rendered(kMissionTitles[2]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[2]);

  // Below 1000ft (0x01F000)
  flight_speed = 0x60;
  flight_eye_z = 0x01F000;
  flight_advance();
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Reach 1000ft (0x020000) - Waypoint 0 met
  flight_vspeed = 0;
  flight_eye_z = 0x020000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[2]);
  assert_msg_rendered("NEXT GOAL COMPLETED");

  // Waypoint 1: Land on Runway 1 with gear down
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Touchdown frame
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[2]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_intermediate_navpoint_reached_message() {
  printf("Running test_intermediate_navpoint_reached_message...\n");

  msg_clear();
  flight_init_from_mission(5);

  // Fly to Lake 1 to complete intermediate waypoint 0 (navpoint 1)
  flight_eye_x = 0x100000;
  flight_eye_y = 0xD08000;
  flight_eye_z = 0x020000;
  flight_advance();

  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);
  assert_msg_rendered("WAYPOINT 1 REACHED");

  printf("  PASS\n\n");
}

static void test_mission_04_find_the_runway_completion() {
  printf("Running test_mission_04_find_the_runway_completion...\n");

  msg_clear();
  mission_completed[3] = false;
  flight_init_from_mission(3);
  msg_show(kMissionTitles[3]);

  assert_msg_rendered(kMissionTitles[3]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[3]);

  // Land on Runway 1 (0x200000, 0x400000) and land safely
  flight_gear = true;
  flight_eye_x = 0x200000;
  flight_eye_y = 0x400000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance(); // Touchdown frame
  assert(!flight_status);

  // Stopped on runway
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[3]);

  // Verify completion status message rendering (as sim.cc handles)
  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_mission_05_ferry_flight_completion() {
  printf("Running test_mission_05_ferry_flight_completion...\n");

  msg_clear();
  mission_completed[4] = false;
  flight_init_from_mission(4);
  msg_show(kMissionTitles[4]);

  assert_msg_rendered(kMissionTitles[4]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[4]);

  // Land on Runway 2 (0x600000, 0xBF8000) and land safely
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[4]);

  if (flight_status == FLIGHT_MISSION_COMPLETED) {
    msg_show("MISSION COMPLETE!", MSG_FOREVER, true);
  }
  assert_msg_rendered("MISSION COMPLETE!");

  printf("  PASS\n\n");
}

static void test_mission_06_area_patrol_completion() {
  printf("Running test_mission_06_area_patrol_completion...\n");

  msg_clear();
  mission_completed[5] = false;
  flight_init_from_mission(5);
  msg_show(kMissionTitles[5]);

  assert_msg_rendered(kMissionTitles[5]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[5]);

  // Waypoint 0: Lake 1 (0x100000, 0xD08000)
  flight_eye_x = 0x100000;
  flight_eye_y = 0xD08000;
  flight_eye_z = 0x020000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 1: Lake 2 (0x400000, 0xE08000)
  flight_eye_x = 0x400000;
  flight_eye_y = 0xE08000;
  flight_advance();
  assert(flight_current_wp == 2);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 2: Lake 3 (0x580000, 0x108000)
  flight_eye_x = 0x580000;
  flight_eye_y = 0x108000;
  flight_advance();
  assert(flight_current_wp == 3);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 3: Land on Runway 2 (0x600000, 0xBF8000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[5]);

  printf("  PASS\n\n");
}

static void test_mission_07_airshow_completion() {
  printf("Running test_mission_07_airshow_completion...\n");

  msg_clear();
  mission_completed[6] = false;
  flight_init_from_mission(6);
  msg_show(kMissionTitles[6]);

  assert_msg_rendered(kMissionTitles[6]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);
  assert(!mission_completed[6]);

  // Waypoint 0: a low pass, upside down, above Runway 2 (0x600000, 0x608000).
  // 0x006000 is inside the 250 ft ceiling the constraint now carries; the
  // same roll at 0x010000 is a roll in the cruise, not a pass, and is checked
  // below.
  flight_cam.up.z = -256;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = 0x010000;
  flight_advance();
  assert(flight_current_wp == 0); // inverted, but too high to be a pass
  flight_eye_z = 0x006000;
  flight_advance();
  assert(flight_current_wp == 1);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 1: Land on Runway 2 (0x600000, 0x608000)
  flight_cam.up.z = 256;
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[6]);

  printf("  PASS\n\n");
}

static void test_mission_08_aerial_recon_completion() {
  printf("Running test_mission_08_aerial_recon_completion...\n");

  msg_clear();
  mission_completed[7] = false;
  flight_init_from_mission(7);
  msg_show(kMissionTitles[7]);

  assert_msg_rendered(kMissionTitles[7]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 0: City 1 at 3000ft (0x100000, 0x688000, z >= 0x060000)
  flight_eye_x = 0x100000;
  flight_eye_y = 0x688000;
  flight_eye_z = 0x065000;
  flight_advance();
  assert(flight_current_wp == 1);

  // Waypoint 1: City 2 at 3000ft (0x680000, 0x988000, z >= 0x060000)
  flight_eye_x = 0x680000;
  flight_eye_y = 0x988000;
  flight_eye_z = 0x065000;
  flight_advance();
  assert(flight_current_wp == 2);

  // Waypoint 2: City 3 at 3000ft (0x700000, 0xE88000, z >= 0x060000)
  flight_eye_x = 0x700000;
  flight_eye_y = 0xE88000;
  flight_eye_z = 0x065000;
  flight_advance();
  assert(flight_current_wp == 3);

  // Waypoint 3: Land on Runway 2 (0x600000, 0x608000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[7]);

  printf("  PASS\n\n");
}

static void test_mission_09_crop_duster_completion() {
  printf("Running test_mission_09_crop_duster_completion...\n");

  msg_clear();
  mission_completed[8] = false;
  flight_init_from_mission(8);
  msg_show(kMissionTitles[8]);

  assert_msg_rendered(kMissionTitles[8]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 0: Field 1 below 100ft (0x300000, 0x888000, z <= 0x004000)
  flight_eye_x = 0x300000;
  flight_eye_y = 0x888000;
  flight_eye_z = 0x003000;
  flight_advance();
  assert(flight_current_wp == 1);

  // Waypoint 1: Field 2 below 100ft (0x080000, 0xA08000, z <= 0x004000)
  flight_eye_x = 0x080000;
  flight_eye_y = 0xA08000;
  flight_eye_z = 0x003000;
  flight_advance();
  assert(flight_current_wp == 2);

  // Waypoint 2: Field 3 below 100ft (0x380000, 0xB08000, z <= 0x004000)
  flight_eye_x = 0x380000;
  flight_eye_y = 0xB08000;
  flight_eye_z = 0x003000;
  flight_advance();
  assert(flight_current_wp == 3);

  // Waypoint 3: Land on Runway 2 (0x600000, 0x608000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[8]);

  printf("  PASS\n\n");
}

static void test_mission_10_fuel_challenge_completion() {
  printf("Running test_mission_10_fuel_challenge_completion...\n");

  msg_clear();
  mission_completed[9] = false;
  flight_init_from_mission(9);
  msg_show(kMissionTitles[9]);

  assert_msg_rendered(kMissionTitles[9]);
  assert(flight_current_wp == 0);
  assert(flight_status == FLIGHT_ONGOING);

  // Waypoint 0: Land on Runway 2 (0x600000, 0x608000)
  flight_gear = true;
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  flight_speed = 0;
  flight_advance();
  assert(flight_status == FLIGHT_MISSION_COMPLETED);
  assert(mission_completed[9]);

  printf("  PASS\n\n");
}

static void test_landing_off_runway_crash() {
  printf("Running test_landing_off_runway_crash...\n");
  flight_init();
  flight_gear = true;
  flight_eye_x = 0x000000; // Not on runway
  flight_eye_y = 0x000000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  assert(flight_status == FLIGHT_CRASH_NOT_ON_RUNWAY);
  printf("  PASS\n\n");
}

static void test_runway_1_bounds_alignment() {
  printf("Running test_runway_1_bounds_alignment...\n");
  // Test Runway 1 detection for EX: 0x001EF217, EY: 0x003F8000
  flight_init();
  flight_gear = true;
  flight_eye_x = 0x001EF217;
  flight_eye_y = 0x003F8000;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  assert(flight_status == FLIGHT_ONGOING || flight_status == FLIGHT_MISSION_COMPLETED);

  // Test edge of range 0x1C0001..0x23FFFF x 0x3C0001..0x43FFFF
  flight_init();
  flight_gear = true;
  flight_eye_x = 0x001C0001;
  flight_eye_y = 0x003C0001;
  flight_eye_z = kGroundZ;
  flight_throttle = 0;
  flight_speed = kTrimSpeed;
  flight_advance();
  assert(flight_status == FLIGHT_ONGOING || flight_status == FLIGHT_MISSION_COMPLETED);

  printf("  PASS\n\n");
}

// Distance between two map-pixel coordinates on one axis, the short way
// round: the world wraps, so 127 and 0 are neighbours.
static uint8_t _path_axis_delta(uint8_t a, uint8_t b) {
  uint8_t d = (uint8_t)(a - b) & 0x7F;
  return d <= 64 ? d : (uint8_t)(128 - d);
}

// The map view addresses cells by (row, col) and pixels by (px, py), and
// map.md section 4 claims the two agree by construction: py >> 3 is the
// screen row and px >> 2 the screen column. Everything the map draws depends
// on that, so check it exhaustively rather than by example.
static void test_flight_path_pixel_cell_agreement() {
  printf("Running test_flight_path_pixel_cell_agreement...\n");

  for (int32_t unit = 0; unit < 256; ++unit) {
    // flight_init() is what resets the ring; count alone is not enough, since
    // the write position is private to flight.cc. Paused, so flight_advance()
    // samples the position set here rather than one step past it.
    flight_init();
    flight_paused = true;
    // x selects the row.
    flight_eye_x = unit << 16;
    flight_eye_y = 0;
    flight_advance();
    const uint8_t xb = (uint8_t)(unit + 0x04);
    const uint8_t map_row = (xb >> 3) & 0x0F;
    const uint8_t screen_row = 15 - map_row;
    assert((flight_path_py[flight_path_count - 1] >> 3) == screen_row);

    // y selects the column.
    flight_init();
    flight_paused = true;
    flight_eye_x = 0;
    flight_eye_y = unit << 16;
    flight_advance();
    const uint8_t yb = (uint8_t)(unit + 0x04);
    const uint8_t map_col = (yb >> 3) & 0x1F;
    const uint8_t screen_col = 31 - map_col;
    assert((flight_path_px[flight_path_count - 1] >> 2) == screen_col);
  }
  flight_paused = false;

  printf("  PASS\n\n");
}

// The path is stored as bare points with no line drawing between them, which
// is only correct if consecutive samples are always neighbours -- including
// diagonally, since one step passing near a cell corner can cross a row and a
// column boundary at once. That rests on the aircraft covering at most 256 m
// per step, so fly it at kMaxSpeed -- the worst case -- in a variety of
// directions and check every append.
static void test_flight_path_samples_are_connected() {
  printf("Running test_flight_path_samples_are_connected...\n");

  static const int16_t kMaxSpeed = 0x0F00;
  int checked = 0;

  // Several segments, each restarted so the ring stays in chronological
  // order, and each steered differently to sweep the compass.
  for (int seg = 0; seg < 8; ++seg) {
    flight_init_from_mission(3);
    flight_eye_z = 0x040000;
    // Seeded with the start position, and nothing else yet.
    assert(flight_path_count == 1);

    for (int i = 0; i < seg; ++i) {
      flight_input(FLIGHT_INPUT_YAW_LEFT);
    }
    for (int frame = 0; frame < 400 && flight_path_count < kFlightPathLen;
         ++frame) {
      flight_speed = kMaxSpeed;  // hold the worst case against drag
      flight_eye_z = 0x040000;   // and stay airborne
      flight_advance();
    }
    assert(flight_path_count > 8);  // the segment actually moved

    for (uint8_t i = 1; i < flight_path_count; ++i) {
      const uint8_t dx =
          _path_axis_delta(flight_path_px[i], flight_path_px[i - 1]);
      const uint8_t dy =
          _path_axis_delta(flight_path_py[i], flight_path_py[i - 1]);
      // At most one pixel on each axis, and at least one somewhere: the
      // sampler drops repeats, and no step can skip a pixel.
      assert(dx <= 1 && dy <= 1);
      assert(dx + dy >= 1);
      ++checked;
    }
  }
  assert(checked > 300);

  printf("  PASS (%d transitions)\n\n", checked);
}

// The crash event fires exactly once, on the step that wrecks the aircraft.
//
// It carries no flag of its own: flight_advance() returns early on every frame
// after the crash, so simply reaching the end of a step while crashed means it
// happened during that step. That is only true as long as the early return
// stays at the top, which is what this pins down.

#ifdef __FLIGHT_AOA__
static void test_crash_event_fires_once() {
  printf("Running test_crash_event_fires_once...\n");

  flight_init_from_mission(3);
  flight_eye_z = 0x040000;
  flight_advance();
  assert(!flight_crashed());
  assert((flight_events & FLIGHT_EV_CRASH) == 0);

  // Fly it into the ground by holding a steep nose-down attitude.
  //
  // This used to hold the pitch-down *input*, which drove the old model
  // straight in because its flight path was its attitude. Held now it flies an
  // outside loop - the nose keeps rotating, the flight path follows it round,
  // and the aircraft comes over the top and climbs away. Holding an attitude
  // rather than an input is what a dive is in a model that has both.
  int steps = 0;
  while (!flight_crashed() && steps < 4000) {
    flight_cam.front.z = -200;
    vec_orthonormalize(&flight_cam);
    flight_advance();
    ++steps;
  }
  printf("  crashed after %d steps, status %d\n", steps, flight_status);
  assert(flight_crashed());

  // The wrecking step published it, alongside a bumped generation.
  assert((flight_events & FLIGHT_EV_CRASH) != 0);
  const uint8_t gen_at_crash = flight_gen;

  // Every later frame publishes nothing at all - not the crash again, and not
  // a new generation. Without that, a consumer keyed on the generation would
  // retrigger the crash sound on every frame for the rest of the flight.
  for (int i = 0; i < 20; ++i) {
    flight_advance();
    assert(flight_gen == gen_at_crash);
  }

  // A restart clears it, so the next attempt does not begin wrecked.
  flight_init_from_mission(3);
  assert(!flight_crashed());
  assert((flight_events & FLIGHT_EV_CRASH) == 0);

  printf("  PASS\n\n");
}
#else // !__FLIGHT_AOA__
// The crash event fires exactly once, on the step that wrecks the aircraft.
//
// It carries no flag of its own: flight_advance() returns early on every frame
// after the crash, so simply reaching the end of a step while crashed means it
// happened during that step. That is only true as long as the early return
// stays at the top, which is what this pins down.
static void test_crash_event_fires_once() {
  printf("Running test_crash_event_fires_once...\n");

  flight_init_from_mission(3);
  flight_eye_z = 0x040000;
  flight_advance();
  assert(!flight_crashed());
  assert((flight_events & FLIGHT_EV_CRASH) == 0);

  // Drive it into the ground hard enough to fail the landing envelope.
  int steps = 0;
  while (!flight_crashed() && steps < 4000) {
    flight_input(FLIGHT_INPUT_PITCH_DOWN);
    flight_advance();
    ++steps;
  }
  assert(flight_crashed());

  // The wrecking step published it, alongside a bumped generation.
  assert((flight_events & FLIGHT_EV_CRASH) != 0);
  const uint8_t gen_at_crash = flight_gen;

  // Every later frame publishes nothing at all - not the crash again, and not
  // a new generation. Without that, a consumer keyed on the generation would
  // retrigger the crash sound on every frame for the rest of the flight.
  for (int i = 0; i < 20; ++i) {
    flight_advance();
    assert(flight_gen == gen_at_crash);
  }

  // A restart clears it, so the next attempt does not begin wrecked.
  flight_init_from_mission(3);
  assert(!flight_crashed());
  assert((flight_events & FLIGHT_EV_CRASH) == 0);

  printf("  PASS (crashed after %d steps)\n\n", steps);
}
#endif // __FLIGHT_AOA__

// Pause freezes the controls as well as the physics.
//
// Without this, pause was a way to fly in stopped time: roll, retrim, change
// the throttle and drop the gear with nothing moving, then resume already set
// up. Z and X are the deliberate exception - repositioning the aircraft while
// frozen is what they are for.
//
// Asserted against flight_input() rather than against sim.cc's key handling,
// because that is where the rule lives and it is the layer a future call site
// would have to go through.
// Everything the pilot can press that is not Z or X. Each is applied
// repeatedly, because a single step of some of these is small enough that one
// application could be lost in rounding even if the guard were missing.
//
// The count has to be ODD. Flaps and gear are toggles, so an even number of
// presses returns them to where they started and the assertions pass whether
// the guard exists or not. An earlier version of this test used 8 and did not
// notice a guard that let the gear through.
static const int kPauseRepeats = 7;
static const enum flight_input_t kPauseBlocked[] = {
    FLIGHT_INPUT_ROLL_LEFT,   FLIGHT_INPUT_ROLL_RIGHT,
    FLIGHT_INPUT_PITCH_UP,    FLIGHT_INPUT_PITCH_DOWN,
    FLIGHT_INPUT_YAW_LEFT,    FLIGHT_INPUT_YAW_RIGHT,
    FLIGHT_INPUT_THROTTLE_UP, FLIGHT_INPUT_THROTTLE_DOWN,
    FLIGHT_INPUT_TOGGLE_FLAP, FLIGHT_INPUT_TOGGLE_GEAR,
    FLIGHT_INPUT_BRAKE,
};
static_assert(kPauseRepeats % 2 == 1, "toggles need an odd number of presses");

// Presses every blocked control while paused and requires nothing to move.
// Called in both flight regimes, because flight_input() dispatches through two
// entirely separate switch statements and they do not carry the same cases -
// FLIGHT_INPUT_BRAKE, for one, exists only on the ground.
static void _assert_pause_blocks_here(void) {
  assert(flight_paused);

  const mat3_t cam0 = flight_cam;
  const uint8_t throttle0 = flight_throttle;
  const uint8_t flap0 = flight_flap;
  const uint8_t gear0 = flight_gear;
  const int16_t speed0 = flight_speed;
  const int32_t eye_x0 = flight_eye_x;
  const int32_t eye_y0 = flight_eye_y;
  const int32_t eye_z0 = flight_eye_z;

  for (size_t i = 0; i < sizeof(kPauseBlocked) / sizeof(kPauseBlocked[0]);
       ++i) {
    for (int rep = 0; rep < kPauseRepeats; ++rep) {
      flight_input(kPauseBlocked[i]);
    }
  }

  assert(memcmp(&flight_cam, &cam0, sizeof(cam0)) == 0);
  assert(flight_throttle == throttle0);
  assert(flight_flap == flap0);
  assert(flight_gear == gear0);
  assert(flight_speed == speed0);

  // Nothing moved the aircraft either: with the pause held, position is frozen
  // along with the attitude and the instruments.
  assert(flight_eye_x == eye_x0);
  assert(flight_eye_y == eye_y0);
  assert(flight_eye_z == eye_z0);
}

static void test_paused_blocks_controls() {
  printf("Running test_paused_blocks_controls...\n");

  // Airborne, so the attitude and throttle controls all have something they
  // could visibly do.
  flight_init_from_mission(3);
  flight_eye_z = 0x040000;
  flight_advance();
  flight_paused = true;
  _assert_pause_blocks_here();

  const mat3_t cam0 = flight_cam;
  const uint8_t throttle0 = flight_throttle;
  const uint8_t gear0 = flight_gear;

  // The same controls work again the moment the pause lifts, so this is a
  // pause rule and not an accidental lockout.
  flight_paused = false;
  flight_input(FLIGHT_INPUT_THROTTLE_UP);
  assert(flight_throttle == throttle0 + 1);
  flight_input(FLIGHT_INPUT_TOGGLE_GEAR);
  assert(flight_gear != gear0);
  flight_input(FLIGHT_INPUT_ROLL_LEFT);
  assert(memcmp(&flight_cam, &cam0, sizeof(cam0)) != 0);

  // Now on the ground, rolling. This is the branch that owns the brake and the
  // nose wheel steering, and neither of them exists in the airborne switch at
  // all - so a guard that let the brake through would be invisible to the
  // checks above.
  flight_init_from_mission(0);
  flight_speed = 0x0400;
  flight_paused = true;
  _assert_pause_blocks_here();

  // Prove the regime was live rather than inert: unpaused, the brake really
  // does bite here. Without this the ground half could be asserting that
  // nothing happens in a state where nothing was going to happen anyway.
  flight_paused = false;
  {
    const int16_t before = flight_speed;
    flight_input(FLIGHT_INPUT_BRAKE);
    assert(flight_speed < before);
  }

  printf("  PASS\n\n");
}

// Ring behaviour: repeats are dropped, the buffer saturates rather than
// overflowing, and restarting the mission wipes the trail.
static void test_flight_path_ring_buffer() {
  printf("Running test_flight_path_ring_buffer...\n");

  flight_init_from_mission(3);
  assert(flight_path_count == 1);

  // Frozen position appends nothing, however long it is left running.
  flight_paused = true;
  const uint8_t px0 = flight_path_px[0];
  const uint8_t py0 = flight_path_py[0];
  for (int i = 0; i < 200; ++i) {
    flight_advance();
  }
  assert(flight_path_count == 1);
  assert(flight_path_px[0] == px0 && flight_path_py[0] == py0);
  flight_paused = false;

  // Fly long enough to wrap the ring several times over.
  flight_eye_z = 0x040000;
  for (int frame = 0; frame < 6000; ++frame) {
    flight_eye_z = 0x040000;
    flight_advance();
    assert(flight_path_count <= kFlightPathLen);
  }
  assert(flight_path_count == kFlightPathLen);

  // The oldest entries are being overwritten, not just appended past. Steer
  // while doing it, so the trail moves on both axes rather than running
  // straight down a column and rewriting the same px values.
  uint8_t before_px[kFlightPathLen], before_py[kFlightPathLen];
  memcpy(before_px, flight_path_px, sizeof(before_px));
  memcpy(before_py, flight_path_py, sizeof(before_py));
  for (int frame = 0; frame < 2000; ++frame) {
    flight_eye_z = 0x040000;
    flight_input(FLIGHT_INPUT_YAW_LEFT);
    flight_advance();
  }
  assert(memcmp(before_px, flight_path_px, sizeof(before_px)) != 0 ||
         memcmp(before_py, flight_path_py, sizeof(before_py)) != 0);

  // R restarts the mission, which starts a fresh trail at the start point.
  flight_init_from_mission(3);
  assert(flight_path_count == 1);
  const uint8_t xb = (uint8_t)((flight_eye_x >> 16) + 0x04);
  const uint8_t yb = (uint8_t)((flight_eye_y >> 16) + 0x04);
  assert(flight_path_py[0] == (uint8_t)(127 - (xb & 0x7F)));
  assert(flight_path_px[0] == (uint8_t)(127 - ((yb >> 1) & 0x7F)));

  printf("  PASS\n\n");
}

static void test_low_altitude_approach_warnings() {
  printf("Running test_low_altitude_approach_warnings...\n");
  flight_init();
  flight_current_wp = 5;
  flight_gear = false;
  flight_cam.front.z = -10;
  flight_eye_x = 0x200000; // Runway 1
  flight_eye_y = 0x400000;
  flight_eye_z = 0x003000; // <= 0x4000
  msg_clear();
  flight_advance();
  assert_msg_rendered("WARNING: GEAR RETRACTED");

  // Off a runway and descending through 125 ft with nothing else wrong: no
  // advisory at all. The check fires everywhere in the world, so warning here
  // meant warning on every low pass, the ones the missions ask for included.
  // Landing off a runway is still a crash and still names itself.
  flight_init();
  flight_current_wp = 5;
  flight_gear = true;
  flight_cam.front.z = -10;
  flight_eye_x = 0x000000; // Off runway
  flight_eye_y = 0x000000;
  flight_eye_z = 0x003000;
  msg_clear();
  flight_advance();
  assert_msg_rendered("");

  // ... and a fault that is not the runway still reports itself out there,
  // which the runway check used to mask by being first in the fault order.
  flight_init();
  flight_current_wp = 5;
  flight_gear = false;
  flight_cam.front.z = -10;
  flight_eye_x = 0x000000; // Off runway, gear up
  flight_eye_y = 0x000000;
  flight_eye_z = 0x003000;
  msg_clear();
  flight_advance();
  assert_msg_rendered("WARNING: GEAR RETRACTED");

  // Mission 7: Min 3000ft constraint warning over City 1 (wx=0x10, wy=0x68)
  flight_init_from_mission(7); // Waypoint 0 constraint is WP_MIN_3000FT at City 1
  flight_eye_x = 0x100000;
  flight_eye_y = 0x688000;
  flight_eye_z = 0x010000;     // Below 3000ft (0x060000)
  msg_clear();
  flight_advance();
  assert_msg_rendered("CLIMB ABOVE 3000FT FOR MISSION");

  // Mission 6: Fly inverted constraint warning
  flight_init_from_mission(6); // Waypoint 0 constraint is WP_UPSIDE_DOWN
  flight_eye_x = 0x600000;
  flight_eye_y = 0xC00000;
  flight_eye_z = 0x010000;
  flight_cam.up.z = 256;       // Upright (not inverted)
  msg_clear();
  flight_advance();
  assert_msg_rendered("FLY LOW INVERTED FOR MISSION");

  // Mission 9: Max 250ft constraint warning
  flight_init_from_mission(8); // Waypoint 0 (Field 1) constraint is WP_MAX_250FT
  flight_eye_x = 0x300000;
  flight_eye_y = 0x888000;
  flight_eye_z = 0x010000;     // Above 250ft (0x008000)
  msg_clear();
  flight_advance();
  assert_msg_rendered("GO BELOW 250FT FOR MISSION");

  printf("  PASS\n\n");
}

static void test_ground_gear_retraction_blocked() {
  printf("Running test_ground_gear_retraction_blocked...\n");

  // On ground with gear down: toggle gear should NOT retract gear
  _put_on_ground(0x0300);
  assert(flight_gear == 1);
  flight_input(FLIGHT_INPUT_TOGGLE_GEAR);
  assert(flight_gear == 1);

  // On ground with gear up (manually forced): toggle gear SHOULD extend gear
  flight_gear = 0;
  flight_input(FLIGHT_INPUT_TOGGLE_GEAR);
  assert(flight_gear == 1);

  // In air with gear down: toggle gear SHOULD retract gear
  flight_init_from_mission(3); // Airborne
  flight_gear = 1;
  flight_input(FLIGHT_INPUT_TOGGLE_GEAR);
  assert(flight_gear == 0);

  printf("  PASS\n\n");
}

// The world repeats, and the waypoint test has to repeat with it: x every 128
// world units (kWorldMapHeight rows of 8, which is also the period the map
// view draws and _flight_on_runway matches on), y every 256. Two things used
// to go wrong here, and this covers both.
static void test_waypoint_matching_across_world_periods() {
  printf("Running test_waypoint_matching_across_world_periods...\n");

  // Mission 05 lands on runway 2, waypoint (0x60, 0xBF).
  // Any x that is the same place modulo 128 is the same runway - same terrain,
  // same map pixel - so it completes the mission from any of them.
  for (int copy = 0; copy < 3; ++copy) {
    mission_completed[4] = false;
    flight_init_from_mission(4);
    flight_gear = true;
    flight_eye_x = (int32_t)((0x60 + 128 * copy) & 0xFF) << 16;
    flight_eye_y = 0xC00000;
    flight_eye_z = kGroundZ;
    flight_throttle = 0;
    flight_speed = kTrimSpeed;
    flight_advance();
    flight_speed = 0;
    flight_advance();
    assert(!flight_crashed());
    assert(flight_status == FLIGHT_MISSION_COMPLETED);
    assert(mission_completed[4]);
  }

  // The whole width of runway 2 works on the far copy, not just its centre.
  // Only the exact +128 offset used to pass, and only because negating an
  // int8_t -128 leaves it negative.
  for (int x = 0x5C; x <= 0x63; ++x) {
    mission_completed[4] = false;
    flight_init_from_mission(4);
    flight_gear = true;
    flight_eye_x = (int32_t)((x + 128) & 0xFF) << 16;
    flight_eye_y = 0xC00000;
    flight_eye_z = kGroundZ;
    flight_throttle = 0;
    flight_speed = kTrimSpeed;
    flight_advance();
    flight_speed = 0;
    flight_advance();
    assert(mission_completed[4]);
  }

  // y does not repeat until 256, so a point 128 units away in y is half the
  // map away and must not count. Mission 06 waypoint 0 is Lake 1 at
  // (0x10, 0xD0); (0x10, 0x50) is open ground.
  mission_completed[5] = false;
  flight_init_from_mission(5);
  flight_eye_x = 0x100000;
  flight_eye_y = 0x508000; // 0xD0 - 128
  flight_eye_z = 0x030000;
  flight_advance();
  assert(flight_current_wp == 0);

  // The real one still counts.
  flight_eye_y = 0xD08000;
  flight_advance();
  assert(flight_current_wp == 1);

  // And the edges of the tolerance are unchanged: +/-16 in x, +/-16 in y for
  // a plain waypoint.
  flight_init_from_mission(5);
  flight_eye_x = 0x100000;
  flight_eye_y = 0xE08000; // 0xD0 + 16, just inside
  flight_eye_z = 0x030000;
  flight_advance();
  assert(flight_current_wp == 1);

  flight_init_from_mission(5);
  flight_eye_x = 0x100000;
  flight_eye_y = 0xE18000; // 0xD0 + 17, just outside
  flight_eye_z = 0x030000;
  flight_advance();
  assert(flight_current_wp == 0);

  printf("  PASS\n\n");
}

// The inverted pass needs the aeroplane on its back AND low. up.z is
// 256 * cos(roll), so -128 is 120 degrees: a steep bank no longer counts.
static void test_upside_down_waypoint_needs_roll_and_altitude() {
  printf("Running test_upside_down_waypoint_needs_roll_and_altitude...\n");

  const int32_t kLow = 0x006000;  // inside the 250 ft ceiling
  const int32_t kHigh = 0x010000; // outside it
  struct { int16_t up_z; int32_t z; bool expect; } cases[] = {
      {-256, kLow, true},   // fully inverted, low: the pass
      {-256, kHigh, false}, // inverted but high: a roll in the cruise
      {-129, kLow, true},   // just past 120 degrees
      {-128, kLow, false},  // exactly 120 degrees is not enough
      {-64, kLow, false},   // steeply banked, not inverted
      {256, kLow, false},   // upright
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    mission_completed[6] = false;
    flight_init_from_mission(6);
    flight_eye_x = 0x600000;
    flight_eye_y = 0xC00000;
    flight_eye_z = cases[i].z;
    flight_cam.up.z = cases[i].up_z;
    flight_advance();
    assert((flight_current_wp == 1) == cases[i].expect);
  }

  printf("  PASS\n\n");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  mem_screen_row_ptrs[0] = test_screen_row;
  #ifdef __FLIGHT_AOA__
  const char *model = "angle of attack";
  const int count = 64;
#else
  // test_stall_speeds_are_derived has nothing to say about a model that is
  // told its stall speeds, so it is not compiled into the arcade build.
  const char *model = "arcade";
  const int count = 63;
#endif
  printf("=== FLIGHT MODEL COMPREHENSIVE SUITE (%d TESTS, %s) ===\n\n", count,
         model);
  test_host_multiply_matches_c64();
#ifdef __FLIGHT_AOA__
  test_stall_speeds_are_derived();
#endif
  test_level_cruise_equilibrium();
  test_level_trim_falls_with_speed();
  test_power_off_stall_recovery();
  test_no_backward_flight();
  test_climb_at_different_throttles();
  test_banked_turns_drag_and_descent();
  test_inverted_flight_drag_and_pitch();
  test_gear_drag_penalty();
  test_flap_drag_lift_and_stall_reduction();
  test_touchdown_flare_and_crash_envelope();
  test_takeoff_rotation_is_not_a_gate();
  test_takeoff_rotation_attitude();
  test_ground_deceleration_friction();
  test_ground_braking();
  test_zero_fuel_flameout_transition();
  test_vertical_dive_terminal_velocity_clamping();
  test_inverted_stall_and_nose_recovery();
  test_ground_roll_takeoff_abort();
  test_matrix_orthonormality_under_continuous_roll();
  test_low_altitude_stall_ground_impact();
  test_abrupt_climb_throttle_cut();
  test_touchdown_exact_boundary_limits();
  test_pause_unpause_state_freeze();
  test_high_altitude_thrust_lift_decay();
  test_high_altitude_stall_increases_with_altitude();
  test_inverted_flaps_stall_speed_increase();
  test_idle_throttle_glide_slope_speed_decay();
  test_rollout_stays_on_ground();
  test_landing_envelope_sink_rate();
  test_landing_envelope_bank_angle();
  test_landing_envelope_touchdown_speed();
  test_landing_envelope_inverted();
  test_inverted_high_nose_stall_breaks_downward();
  test_optimal_glide_angle();
  test_turn_rate_scales_with_lift_over_speed();
  test_banked_turn_loses_altitude();
  test_ground_steering();
  test_takeoff_roll_speed_margin();
  test_mission_waypoint_constraints();
  test_mission_01_takeoff_completion();
  test_flight_continues_after_mission_completion();
  test_mission_02_landing_completion();
  test_mission_03_solo_flight_completion();
  test_intermediate_navpoint_reached_message();
  test_mission_04_find_the_runway_completion();
  test_mission_05_ferry_flight_completion();
  test_mission_06_area_patrol_completion();
  test_mission_07_airshow_completion();
  test_mission_08_aerial_recon_completion();
  test_mission_09_crop_duster_completion();
  test_mission_10_fuel_challenge_completion();
  test_runway_1_bounds_alignment();
  test_landing_off_runway_crash();
  test_low_altitude_approach_warnings();
  test_waypoint_matching_across_world_periods();
  test_upside_down_waypoint_needs_roll_and_altitude();
  test_crash_event_fires_once();
  test_paused_blocks_controls();
  test_flight_path_pixel_cell_agreement();
  test_flight_path_samples_are_connected();
  test_flight_path_ring_buffer();
  test_ground_gear_retraction_blocked();
  printf("ALL %d TESTS PASSED SUCCESSFULLY (%s model)!\n", count, model);
  return 0;
}
