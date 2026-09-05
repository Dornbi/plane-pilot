// Drives the prototype AoA model (aoa.cc) and the shipping model (flight.cc)
// through the same sweeps and prints the two answers side by side.
//
// Everything printed here is measured, in the sense flight.md uses the word:
// the model is run to a steady state and read, never predicted. The tables in
// docs/flight_aoa.md are this program's output.
//
//   make -C c64o/proto run
//
// The shipping model is linked, not reimplemented, so its column cannot drift
// from what the game does. Both are run at flight_step_shift 0 - the stock C64
// rate of one model step per eight raster frames, 6.25 steps a second.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aoa.h"

#include "../flight.h"
#include "../fmath.h"
#include "../msg.h"
#include "../vec.h"

// Stubs for the msg.cc dependencies flight.cc pulls in, same as flight_test.
uint8_t *mem_screen_ram = nullptr;
uint8_t *mem_screen_row_ptrs[25];
static uint8_t color_buffer_dummy[560];
extern uint8_t *const mem_color_buffer = color_buffer_dummy;

// Well clear of the ground and below the 0x080000 density knee, so every
// steady state below is a sea-level one.
static const int32_t kTestZ = 0x040000;
static const int32_t kGroundZ = 0x2000;

// Long enough for the slowest speed/path pair to settle. The AoA model has two
// coupled states rather than one, so it takes longer to converge than the
// shipping model does; 4000 steps is ~11 minutes of flight at 6.25 Hz.
static const int kSettleSteps = 4000;
// The tail of the run, used to report whether a "steady state" really is one.
static const int kWatchSteps = 200;

struct trim_t {
  int16_t speed;
  int16_t vspeed;
  int16_t alpha;   // AoA model only; 0 for the shipping model, which has none
  int16_t gamma_z; // AoA model only
  int16_t vspeed_lo;
  int16_t vspeed_hi;
  // Mean vertical speed over the tail of the run, x256. Everything below
  // judges "does it hold altitude" on this rather than on the last step or on
  // the worst step: the models settle into a limit cycle a few units wide -
  // one unit of the flight path is 8 units of vertical speed at cruise - so an
  // aircraft that is exactly level still reads as descending on some steps.
  int32_t vspeed_mean256;
  bool stalled;
};

// --------------------------------------------------------------------------
// Holding an attitude
//
// Both helpers do what flight_test's _settle does: the pilot holds the stick
// where it is, the altitude is pinned so the air density cannot drift, and
// only the scalar state is left to converge.
// --------------------------------------------------------------------------

// Builds the attitude a settle starts from. `up_z` of 0 means the vertical
// dive, and it needs its own frame: writing front = (256, 0, -256) and up =
// (0, 0, 0) and calling vec_orthonormalize - which is what the obvious version
// does, and what flight_test's own helper does - leaves a *45 degree* dive,
// because normalizing (256, 0, -256) is (181, 0, -181). flight.md's terminal
// velocities are quoted for a nose "truly straight down"; this is the frame
// that actually is.
static void set_attitude(mat3_t *m, int16_t pitch, int16_t up_z) {
  if (up_z == 0) {
    m->front = make_vector(0, 0, pitch < 0 ? -256 : 256);
    m->left = make_vector(0, 256, 0);
    m->up = make_vector(pitch < 0 ? 256 : -256, 0, 0);
    return;
  }
  m->front = make_vector(256, 0, 0);
  m->left = make_vector(0, 256, 0);
  m->up = make_vector(0, 0, up_z);
  m->front.z = pitch;
  vec_orthonormalize(m);
}

static trim_t settle_ship(uint8_t throttle, int16_t pitch, int16_t up_z,
                          uint8_t flap = 0, uint8_t gear = 0) {
  flight_init();
  flight_eye_z = kTestZ;
  flight_throttle = throttle;
  flight_fuel = 0x0FFFFFFF;
  flight_flap = flap;
  flight_gear = gear;
  set_attitude(&flight_cam, pitch, up_z);
  const int16_t held_pitch = flight_cam.front.z;
  const int16_t held_up_z = flight_cam.up.z;

  trim_t t;
  t.vspeed_lo = 32767;
  t.vspeed_hi = -32768;
  int32_t vsum = 0;
  for (int i = 0; i < kSettleSteps; ++i) {
    flight_cam.front.z = held_pitch;
    flight_cam.up.z = held_up_z;
    flight_eye_z = kTestZ;
    flight_advance();
    flight_throttle = throttle;
    if (i >= kSettleSteps - kWatchSteps) {
      if (flight_vspeed < t.vspeed_lo) t.vspeed_lo = flight_vspeed;
      if (flight_vspeed > t.vspeed_hi) t.vspeed_hi = flight_vspeed;
      vsum += flight_vspeed;
    }
  }
  t.vspeed_mean256 = vsum * 256 / kWatchSteps;
  t.speed = flight_speed;
  t.vspeed = flight_vspeed;
  t.stalled = flight_stall != 0;
  t.alpha = 0;
  t.gamma_z = flight_cam.front.z;
  return t;
}

// The flight path a trimmed aeroplane would already be on at this attitude and
// airspeed: the one where lift carries the weight, so the vertical force is
// zero on the first step.
//
// Starting anywhere else is not a harmless transient. The model is only
// stable while the wing is below its stall angle, and a settle that starts
// with the flight path flat and the nose 25 degrees down starts with a large
// *negative* alpha - the path then swings down past the trim angle and out the
// far side into a stall, where the droop makes it run away. In flight the
// pitching break ends that, but a bench run that pins the attitude has taken
// the break away, and the run settles into a mush that is an artefact of the
// harness rather than a property of the model. Trimming the start removes the
// swing rather than the break.
static int16_t trim_gamma(int16_t pitch, int16_t up_z, int16_t speed) {
  if (up_z == 0 || speed <= 0) {
    return (int16_t)(pitch << 4);
  }
  // lift_z = lift * up.z / 256 = kAoaTrimLift, and lift = V^2 * C_L / 262144.
  double need_lift = (double)kAoaTrimLift * 256.0 / (double)up_z;
  double cl = need_lift * 262144.0 / ((double)speed * speed);
  double peak = aoa_cl_peak();
  if (cl > peak) cl = peak;
  if (cl < -peak) cl = -peak;
  int32_t alpha16 = (int32_t)(cl + (cl < 0 ? -0.5 : 0.5));
  int32_t d = up_z >= 0 ? alpha16 : -alpha16;
  int32_t g = ((int32_t)pitch << 4) - d;
  if (g > 4096) g = 4096;
  if (g < -4096) g = -4096;
  return (int16_t)g;
}

static trim_t _settle_aoa_once(uint8_t throttle, int16_t pitch, int16_t up_z,
                               uint8_t flap, uint8_t gear, int16_t start_speed) {
  aoa_init();
  aoa_eye_z = kTestZ;
  aoa_throttle = throttle;
  aoa_flap = flap;
  aoa_gear = gear;
  if (start_speed > 0) {
    aoa_speed = start_speed;
  }
  set_attitude(&aoa_cam, pitch, up_z);
  const int16_t held_pitch = aoa_cam.front.z;
  const int16_t held_up_z = aoa_cam.up.z;
  aoa_gamma = trim_gamma(held_pitch, held_up_z, aoa_speed);

  trim_t t;
  t.vspeed_lo = 32767;
  t.vspeed_hi = -32768;
  int32_t vsum = 0;
  for (int i = 0; i < kSettleSteps; ++i) {
    aoa_cam.front.z = held_pitch;
    aoa_cam.up.z = held_up_z;
    aoa_eye_z = kTestZ;
    aoa_advance();
    if (i >= kSettleSteps - kWatchSteps) {
      if (aoa_vspeed < t.vspeed_lo) t.vspeed_lo = aoa_vspeed;
      if (aoa_vspeed > t.vspeed_hi) t.vspeed_hi = aoa_vspeed;
      vsum += aoa_vspeed;
    }
  }
  t.vspeed_mean256 = vsum * 256 / kWatchSteps;
  t.speed = aoa_speed;
  t.vspeed = aoa_vspeed;
  t.stalled = aoa_stall != 0;
  t.alpha = aoa_alpha();
  t.gamma_z = aoa_gamma_z();
  return t;
}

// Two passes, the second seeded from the first's airspeed.
//
// The model has two attractors at some held attitudes: the aeroplane flying,
// and the aeroplane mushing down stalled with the droop and the induced drag
// balancing gravity. Both are real - the stalled one is what a stall *is* -
// but which of them a bench run lands in depends on the swing it takes on the
// way, and a run that pins the attitude has taken away the pitching break that
// would end the stalled one in flight. That showed up as a glide table where
// holding 64 units nose-down mushed while 50 and 80 either side of it flew.
//
// Seeding a second pass at the airspeed the first one found puts the start
// near the fixed point instead of a thousand units above it, so the swing is
// small enough to stay on the flying branch where one exists. Where none does,
// both passes stall and the row is marked, which is the honest answer.
static trim_t settle_aoa(uint8_t throttle, int16_t pitch, int16_t up_z,
                         uint8_t flap = 0, uint8_t gear = 0) {
  trim_t first = _settle_aoa_once(throttle, pitch, up_z, flap, gear, 0);
  trim_t second =
      _settle_aoa_once(throttle, pitch, up_z, flap, gear, first.speed);
  if (second.stalled && !first.stalled) {
    return first;
  }
  return second;
}

// The lowest held pitch attitude that still holds altitude, which is the
// question flight.md 2.1 asks of every throttle setting. Returns false if no
// attitude in the sweep holds it.
static bool lowest_level_pitch(bool aoa, uint8_t throttle, int16_t up_z,
                               uint8_t flap, int16_t *out_pitch, trim_t *out) {
  trim_t best;
  int32_t best_vspeed = -0x7FFFFFFF;
  int16_t best_pitch = 0;
  bool found = false;
  for (int16_t pitch = -64; pitch <= 200; ++pitch) {
    trim_t t = aoa ? settle_aoa(throttle, pitch, up_z, flap)
                   : settle_ship(throttle, pitch, up_z, flap);
    if (t.vspeed_mean256 >= 0) {
      *out_pitch = pitch;
      *out = t;
      return true;
    }
    if (t.vspeed_mean256 > best_vspeed) {
      best_vspeed = t.vspeed_mean256;
      best_pitch = pitch;
      best = t;
      found = true;
    }
  }
  if (found) {
    *out_pitch = best_pitch;
    *out = best;
  }
  return false;
}

// --------------------------------------------------------------------------

static void hdr(const char *title) {
  printf("\n%s\n", title);
  for (const char *p = title; *p; ++p) putchar('=');
  putchar('\n');
}

// --------------------------------------------------------------------------
// 1. What the wing is
// --------------------------------------------------------------------------

static void section_wing() {
  hdr("1. The lift curve, and the speeds that fall out of it");

  printf("\n  alpha  deg    C_L   trims at V\n");
  for (int16_t a = 0; a <= 128; a += 8) {
    int16_t cl = aoa_cl(a);
    printf("  %5d %5.1f  %5d   %8u\n", a, asin(a / 256.0) * 180.0 / M_PI, cl,
           aoa_speed_for_cl(cl));
  }

  // Upright C_L max is the peak plus the camber, and inverted it is the peak
  // minus it - the camber adds to what the attitude needs one way up and
  // fights it the other. Leaving it out of this table reported a clean stall
  // speed of 1094 for a wing that stalls at 1024.
  const int16_t peak = aoa_cl(kAoaAlphaStall);
  const int16_t up_max = peak + kAoaCamberCl;
  const int16_t inv_max = peak - kAoaCamberCl;
  printf("\n  Stall speed is not a constant in this model - it is the speed at\n"
         "  which the wing needs its peak C_L, and it is never written down.\n"
         "  C_L max is the lift slope's peak (%d) plus the camber (%d) upright,\n"
         "  and minus it inverted:\n", peak, kAoaCamberCl);
  printf("    clean         C_L max %5d -> V_stall %5u   (flight.cc: %u)\n",
         up_max, aoa_speed_for_cl(up_max), 0x0400);
  printf("    flaps down    C_L max %5d -> V_stall %5u   (flight.cc: %u)\n",
         (int)(up_max + kAoaFlapDeltaCl),
         aoa_speed_for_cl(up_max + kAoaFlapDeltaCl), 0x0340);
  printf("    inverted      C_L max %5d -> V_stall %5u\n", (int)-inv_max,
         aoa_speed_for_cl(inv_max));
  printf("    inverted+flap C_L max %5d -> V_stall %5u\n",
         (int)-(inv_max - kAoaFlapDeltaCl),
         aoa_speed_for_cl(inv_max - kAoaFlapDeltaCl));
  printf("\n  The inverted flap penalty needs no `up.z < 0` case here: flaps are\n"
         "  an addition to C_L, so inverted they subtract from what the\n"
         "  attitude needs.\n");
}

// --------------------------------------------------------------------------
// 2. Level flight
// --------------------------------------------------------------------------

static void level_table(int16_t up_z, uint8_t flap, const char *what) {
  printf("\n  %s\n", what);
  printf("  throttle |            flight.cc            |              AoA\n");
  printf("           | level?  pitch   speed  |  level?  pitch  alpha  speed\n");
  printf("  ---------+------------------------+-----------------------------\n");
  static const uint8_t kThrottles[] = {6, 8, 10, 11, 12, 14, 16, 17, 18, 20, 24};
  for (unsigned i = 0; i < sizeof(kThrottles) / sizeof(kThrottles[0]); ++i) {
    uint8_t thr = kThrottles[i];
    int16_t sp = 0, ap = 0;
    trim_t st, at;
    bool sok = lowest_level_pitch(false, thr, up_z, flap, &sp, &st);
    bool aok = lowest_level_pitch(true, thr, up_z, flap, &ap, &at);
    printf("   %2u (%3u%%)|", thr, (unsigned)(thr * 100 / kMaxThrottle));
    if (sok) {
      printf("  yes   %5d  %6d |", sp, st.speed);
    } else {
      printf("   no  (%+5.0f)      - |", st.vspeed_mean256 / 256.0);
    }
    if (aok) {
      printf("   yes   %5d  %5d  %6d\n", ap, at.alpha, at.speed);
    } else {
      printf("    no  (%+5.0f)      -      -\n", at.vspeed_mean256 / 256.0);
    }
  }
}

static void section_level() {
  hdr("2. Level flight: the lowest pitch that holds altitude");
  level_table(256, 0, "Upright, clean, sea level");
  level_table(256, 1, "Upright, flaps down");
  level_table(-256, 0, "Inverted, clean");
  printf("\n  The inverted table is the upright one mirrored: same attitude,\n"
         "  same speed, alpha negated. That is exactly right for the wing as\n"
         "  tuned, because kAoaCamberCl is 0 and a symmetric section does not\n"
         "  care which way up it is - and exactly wrong for the aeroplane\n"
         "  flight.md 3.2 describes, where inverted flight is meant to be flown\n"
         "  on the edge of the stall. Camber is the knob, and it is the flap\n"
         "  mechanism with the flaps welded down:\n");
  {
    const int16_t saved = kAoaCamberCl;
    kAoaCamberCl = 96;
    level_table(-256, 0, "Inverted, clean, kAoaCamberCl = 96");
    kAoaCamberCl = saved;
  }
  printf("\n  Note what moved. flight.cc has no pitch term in lift, so above\n"
         "  its trim speed the level attitude is front.z = 0 exactly and there\n"
         "  is no faster level trim. Here level flight always needs a positive\n"
         "  alpha, and the faster you go the less of it - which is the trim\n"
         "  behaviour every real aeroplane has and the one the panel's attitude\n"
         "  indicator has been implying all along.\n");
}

// --------------------------------------------------------------------------
// 3. Banked turns
//
// The old version of this section held the pitch at front.z = 0 for both
// models, which looks even-handed and is not: in the AoA model front.z = 0 is
// not level flight, it is a descent at the trim angle of attack. Every run here
// therefore holds a pitch attitude that means the same thing in both models -
// either the one that trims level wings-level, or the one that trims level in
// the turn.
// --------------------------------------------------------------------------

struct turn_t {
  int32_t dz;
  double deg;    // heading change over the run
  double bank;   // measured off left.z, not assumed from the roll count
  int16_t speed;
  int16_t alpha;
  bool stalled;
};

static const int kTurnSteps = 300;

// Roll to a bank, hold it and the pitch, and fly.
//
// `pin_speed` non-zero turns the run into a turn *rate* measurement with speed
// as the independent variable, and to be one it has to pin more than the
// speed. Turn rate is horizontal lift over momentum, so comparing rates across
// speeds means comparing them at the same *lift* - which for a level turn is
// the load factor the bank asks for, 1/cos(bank), and not the same alpha. Held
// at a fixed alpha instead, lift goes as V^2 and the fast run turns faster,
// which measures the harness rather than the model. So under pin_speed the AoA
// run also pins the flight path level and sets the nose to the alpha that
// trims there - the attitude a pilot flying a level turn would be holding.
static turn_t turn_run(bool aoa, uint8_t throttle, int16_t pitch,
                       int roll_steps, int steps, int16_t pin_speed) {
  // Heading is accumulated step by step, not read off the two ends: a steep
  // turn covers several revolutions in a 300 step run, and the difference of
  // two atan2 calls cannot tell three turns from four.
  double turned = 0.0, hprev;
  turn_t r;
  if (aoa) {
    aoa_init();
    aoa_eye_z = kTestZ;
    aoa_throttle = throttle;
    aoa_speed = pin_speed ? pin_speed : 0x0800;
    for (int i = 0; i < roll_steps; ++i) {
      aoa_input(AOA_INPUT_ROLL_RIGHT);
      vec_orthonormalize(&aoa_cam);
    }
    aoa_cam.front.z = pitch;
    vec_orthonormalize(&aoa_cam);
    int16_t hp = aoa_cam.front.z;
    const int16_t hu = aoa_cam.up.z, hl = aoa_cam.left.z;
    if (pin_speed) {
      // front.z for a level turn: gamma is zero, so alpha16 is the C_L that
      // carries 1/cos(bank) times the weight at this speed.
      // trim_gamma with a level nose returns -alpha16, so this is the alpha
      // that trims, in front.z's units. It saturates at the stall angle, and
      // where it does the turn below is alpha limited rather than bank
      // limited - which is a real thing and is why the table has a corner in
      // it, not a harness artefact.
      hp = (int16_t)(-trim_gamma(0, hu, pin_speed) >> 4);
      aoa_cam.front.z = hp;
      vec_orthonormalize(&aoa_cam);
      hp = aoa_cam.front.z;
    }
    aoa_gamma = pin_speed ? 0 : trim_gamma(hp, hu, aoa_speed);
    hprev = atan2((double)aoa_cam.front.y, (double)aoa_cam.front.x);
    for (int i = 0; i < steps; ++i) {
      aoa_cam.front.z = hp;
      aoa_cam.up.z = hu;
      aoa_cam.left.z = hl;
      vec_orthonormalize(&aoa_cam);
      if (pin_speed) {
        aoa_speed = pin_speed;
        aoa_gamma = 0;
      }
      aoa_advance();
      double h = atan2((double)aoa_cam.front.y, (double)aoa_cam.front.x);
      double d = h - hprev;
      while (d > M_PI) d -= 2 * M_PI;
      while (d < -M_PI) d += 2 * M_PI;
      turned += d;
      hprev = h;
    }
    r.dz = aoa_eye_z - kTestZ;
    r.speed = aoa_speed;
    r.alpha = aoa_alpha();
    r.stalled = aoa_stall != 0;
    r.bank = asin(hl / 256.0) * 180.0 / M_PI;
  } else {
    flight_init();
    flight_eye_z = kTestZ;
    flight_throttle = throttle;
    flight_fuel = 0x0FFFFFFF;
    flight_speed = pin_speed ? pin_speed : 0x0800;
    for (int i = 0; i < roll_steps; ++i) {
      flight_input(FLIGHT_INPUT_ROLL_RIGHT);
      vec_orthonormalize(&flight_cam);
    }
    flight_cam.front.z = pitch;
    vec_orthonormalize(&flight_cam);
    const int16_t hp = flight_cam.front.z, hu = flight_cam.up.z,
                  hl = flight_cam.left.z;
    hprev = atan2((double)flight_cam.front.y, (double)flight_cam.front.x);
    for (int i = 0; i < steps; ++i) {
      flight_cam.front.z = hp;
      flight_cam.up.z = hu;
      flight_cam.left.z = hl;
      vec_orthonormalize(&flight_cam);
      if (pin_speed) flight_speed = pin_speed;
      flight_advance();
      flight_throttle = throttle;
      double h = atan2((double)flight_cam.front.y, (double)flight_cam.front.x);
      double d = h - hprev;
      while (d > M_PI) d -= 2 * M_PI;
      while (d < -M_PI) d += 2 * M_PI;
      turned += d;
      hprev = h;
    }
    r.dz = flight_eye_z - kTestZ;
    r.speed = flight_speed;
    r.alpha = 0;
    r.stalled = flight_stall != 0;
    r.bank = asin(hl / 256.0) * 180.0 / M_PI;
  }
  // Degrees per second at the stock C64's 6.25 model steps a second, which is
  // a rate a pilot can recognise rather than a count that depends on the run.
  r.deg = turned * 180.0 / M_PI * 6.25 / steps;
  return r;
}

// The lowest held pitch that keeps a banked turn level, by sweep.
static bool level_turn_pitch(bool aoa, uint8_t throttle, int roll_steps,
                             int16_t *out_pitch, turn_t *out) {
  for (int16_t pitch = -32; pitch <= 200; ++pitch) {
    turn_t t = turn_run(aoa, throttle, pitch, roll_steps, kTurnSteps, 0);
    if (t.dz >= 0 && !t.stalled) {
      *out_pitch = pitch;
      *out = t;
      return true;
    }
  }
  return false;
}

// up.z after n roll inputs, for the harness's own arithmetic.
static int16_t a_up_z(int roll_steps) {
  aoa_init();
  for (int i = 0; i < roll_steps; ++i) {
    aoa_input(AOA_INPUT_ROLL_RIGHT);
    vec_orthonormalize(&aoa_cam);
  }
  return aoa_cam.up.z;
}

static void section_turns() {
  hdr("3. Banked turns");

  static const int kRolls[] = {0, 5, 10};

  printf("\n  a) Full throttle, pitch held at each model's own wings-level\n"
         "     trim (flight.cc 0, AoA 14), %d steps.\n\n", kTurnSteps);
  printf("  roll steps |          flight.cc          |            AoA\n");
  printf("   (bank)    |    dAlt     deg/s   speed  |    dAlt     deg/s  alpha  speed\n");
  printf("  -----------+-----------------------------+-------------------------------\n");
  for (unsigned i = 0; i < sizeof(kRolls) / sizeof(kRolls[0]); ++i) {
    turn_t s = turn_run(false, 24, 0, kRolls[i], kTurnSteps, 0);
    turn_t a = turn_run(true, 24, 14, kRolls[i], kTurnSteps, 0);
    printf("   %2d (%2.0f deg)| %8d  %6.1f  %5d | %8d  %6.1f %5d  %5d%s\n",
           kRolls[i], s.bank, s.dz, s.deg, s.speed, a.dz, a.deg, a.alpha,
           a.speed, a.stalled ? "  stall" : "");
  }

  printf("\n  b) What it takes to hold the turn level: the lowest pitch that\n"
         "     does not lose altitude over the run, full throttle.\n\n");
  printf("  roll steps |     flight.cc      |            AoA\n");
  printf("   (bank)    |  pitch     speed   |  pitch  alpha    speed\n");
  printf("  -----------+--------------------+-----------------------\n");
  for (unsigned i = 0; i < sizeof(kRolls) / sizeof(kRolls[0]); ++i) {
    int16_t sp = 0, ap = 0;
    turn_t st, at;
    bool sok = level_turn_pitch(false, 24, kRolls[i], &sp, &st);
    bool aok = level_turn_pitch(true, 24, kRolls[i], &ap, &at);
    printf("   %2d (%2.0f deg)|", kRolls[i], st.bank);
    if (sok) printf("  %5d    %5d   |", sp, st.speed);
    else printf("   none - cannot     |");
    if (aok) printf("  %5d  %5d    %5d\n", ap, at.alpha, at.speed);
    else printf("   none - cannot hold it level\n");
  }
  printf("\n     The AoA column is the load factor a level turn needs, arrived\n"
         "     at by the wing rather than asserted: steeper bank, more alpha,\n"
         "     more induced drag, lower settled speed - and a bank steep enough\n"
         "     runs the wing out of alpha before it runs out of throttle, which\n"
         "     is the accelerated stall of section 4b arriving in a turn.\n");

  printf("\n  c) Turn rate against airspeed, %d roll steps, speed pinned.\n"
         "     flight.cc turns at left.z >> 5 whatever the wing is doing, so\n"
         "     its rate is the same at every speed (flight_review B4). The AoA\n"
         "     model turns on the horizontal part of lift over momentum, so a\n"
         "     slow turn is a tight one.\n\n", kRolls[2]);
  printf("  entry speed |  flight.cc deg/s |  AoA deg/s  alpha\n");
  printf("  ------------+------------------+------------------\n");
  static const int16_t kEntry[] = {0x0500, 0x0800, 0x0B00};
  for (unsigned i = 0; i < sizeof(kEntry) / sizeof(kEntry[0]); ++i) {
    turn_t s = turn_run(false, 24, 0, kRolls[2], 100, kEntry[i]);
    turn_t a = turn_run(true, 24, 14, kRolls[2], 100, kEntry[i]);
    // Whether the wing could make the lift a level turn at this bank asks
    // for, asked of the wing rather than inferred from the alpha the run
    // finished on - trim_gamma saturates at the stall angle, so the run holds
    // C_L max and reports an alpha a unit or two under it.
    const double need = (double)kAoaTrimLift * 256.0 / (double)a_up_z(kRolls[2]) *
                        262144.0 / ((double)kEntry[i] * kEntry[i]);
    printf("      %5d   |      %6.1f       |   %6.1f   %4d%s\n", kEntry[i],
           s.deg, a.deg, a.alpha,
           need > aoa_cl_peak() ? "  (at C_L max)" : "");
  }
  printf("\n     The 1/V is only half the story, and the low row is the other\n"
         "     half: a level 71 degree turn asks for three times the weight in\n"
         "     lift, and at 1280 the wing has run out of alpha before it can\n"
         "     make it. Below that corner the turn is limited by the wing and\n"
         "     not by the bank, which is a thing this model can express and a\n"
         "     fixed left.z >> 5 cannot.\n");
}

// --------------------------------------------------------------------------
// 4. Stalls
// --------------------------------------------------------------------------

static void section_stall() {
  hdr("4. The stall");

  printf("\n  a) Power-off stall from level flight. Throttle 0, pitch held at\n"
         "     16 (~3.6 deg nose up), run until the model breaks the stall.\n\n");
  {
    flight_init();
    flight_eye_z = kTestZ;
    flight_throttle = 0;
    flight_fuel = 0x0FFFFFFF;
    flight_speed = 0x0800;
    flight_cam.front.z = 16;
    vec_orthonormalize(&flight_cam);
    int at = -1;
    for (int i = 0; i < 400; ++i) {
      flight_eye_z = kTestZ;
      flight_advance();
      if (flight_stall && at < 0) {
        at = i;
        break;
      }
    }
    printf("     flight.cc: stalls at step %3d, speed %5d, pitch %4d\n", at,
           flight_speed, flight_cam.front.z);
  }
  {
    aoa_init();
    aoa_eye_z = kTestZ;
    aoa_throttle = 0;
    aoa_speed = 0x0800;
    aoa_cam.front.z = 16;
    vec_orthonormalize(&aoa_cam);
    int at = -1;
    for (int i = 0; i < 400; ++i) {
      aoa_eye_z = kTestZ;
      aoa_advance();
      if (aoa_stall && at < 0) {
        at = i;
        break;
      }
    }
    printf("     AoA:       stalls at step %3d, speed %5d, pitch %4d, alpha %d\n",
           at, aoa_speed, aoa_cam.front.z, aoa_alpha());
  }

  printf("\n  b) Accelerated stall. Level at high speed, then the stick comes\n"
         "     all the way back: one pitch-up input per step. This is the case\n"
         "     a speed-gated stall cannot see at all.\n\n");
  {
    flight_init();
    flight_eye_z = kTestZ;
    flight_throttle = 24;
    flight_fuel = 0x0FFFFFFF;
    flight_speed = 0x0B00;
    int at = -1;
    for (int i = 0; i < 40; ++i) {
      flight_input(FLIGHT_INPUT_PITCH_UP);
      flight_advance();
      if (flight_stall) {
        at = i;
        break;
      }
    }
    if (at < 0) {
      printf("     flight.cc: never stalls. Speed after 40 steps of full\n"
             "                back stick: %d, pitch %d.\n",
             flight_speed, flight_cam.front.z);
    } else {
      printf("     flight.cc: stalls at step %d, speed %d\n", at, flight_speed);
    }
  }
  {
    aoa_init();
    aoa_eye_z = kTestZ;
    aoa_throttle = 24;
    aoa_speed = 0x0B00;
    int at = -1;
    for (int i = 0; i < 40; ++i) {
      aoa_input(AOA_INPUT_PITCH_UP);
      aoa_advance();
      if (aoa_stall) {
        at = i;
        break;
      }
    }
    if (at < 0) {
      printf("     AoA:       never stalls in 40 steps, speed %d\n", aoa_speed);
    } else {
      printf("     AoA:       stalls at step %d, speed %5d, alpha %d - %.1fx\n"
             "                the 1 g stall speed, which is what a %d deg\n"
             "                pull is worth.\n",
             at, aoa_speed, aoa_alpha(),
             aoa_speed / (double)aoa_speed_for_cl(aoa_cl_peak()),
             (int)(asin(aoa_cam.front.z / 256.0) * 180.0 / M_PI));
    }
  }

  printf("\n  c) Recovery. Engine off, nose held 14 degrees up until it stalls,\n     then hands off. 160 steps is 26 seconds.\n\n");
  {
    aoa_init();
    aoa_eye_z = 0x060000;
    aoa_throttle = 0;
    aoa_speed = 0x0500;
    aoa_cam.front.z = 64;
    vec_orthonormalize(&aoa_cam);
    printf("     step  speed  pitch  path  alpha  stall\n");
    bool recovered = false;
    for (int i = 0; i <= 160; ++i) {
      if (i % 10 == 0 && (i <= 60 || !recovered)) {
        printf("     %4d  %5d  %5d %5d  %5d  %s\n", i, aoa_speed,
               aoa_cam.front.z, aoa_gamma_z(), aoa_alpha(),
               aoa_stall ? "yes" : "");
      }
      aoa_advance();
      if (!recovered && i > 20 && !aoa_stall) {
        printf("     %4d  %5d  %5d %5d  %5d  <- flying again\n", i, aoa_speed,
               aoa_cam.front.z, aoa_gamma_z(), aoa_alpha());
        recovered = true;
      }
    }
  }
}

// --------------------------------------------------------------------------
// 5. Takeoff
// --------------------------------------------------------------------------

static void section_takeoff() {
  hdr("5. Takeoff, and the death of kFlightRotatePitchZ");

  printf("\n  Full throttle from a standing start on mission 0's runway. The\n"
         "  pilot holds the stick back from the beginning; the aircraft leaves\n"
         "  the ground when it is ready to.\n\n");

  {
    // flight.cc: PITCH_UP is a gate, not a stick. Below the stall speed it is
    // ignored; above it the nose jumps to kFlightRotatePitchZ and the aircraft
    // is handed to the airborne branch.
    flight_init_from_mission(0);
    flight_throttle = kMaxThrottle;
    flight_fuel = 0x0FFFFFFF;
    int16_t liftoff = -1;
    for (int i = 0; i < 600; ++i) {
      flight_input(FLIGHT_INPUT_PITCH_UP);
      flight_advance();
      flight_throttle = kMaxThrottle;
      if (flight_eye_z > kGroundZ && liftoff < 0) {
        liftoff = flight_speed;
        printf("  flight.cc: airborne at step %3d, speed %5d, pitch %3d\n", i,
               liftoff, flight_cam.front.z);
        break;
      }
    }
    if (liftoff < 0) printf("  flight.cc: never left the ground\n");
  }

  printf("\n  The AoA model has no rotation constant, so the rotation attitude\n"
         "  is the pilot's to choose and the liftoff speed follows from it:\n\n");
  printf("  hold pitch |  deg  | clean liftoff | flaps liftoff\n");
  printf("  -----------+-------+---------------+--------------\n");
  static const int16_t kHold[] = {16, 31, 47, 64};
  for (unsigned i = 0; i < sizeof(kHold) / sizeof(kHold[0]); ++i) {
    int16_t v[2] = {-1, -1};
    for (int flap = 0; flap < 2; ++flap) {
      aoa_init();
      aoa_eye_z = kGroundZ;
      aoa_on_ground = true;
      aoa_speed = 0;
      aoa_throttle = kMaxThrottle;
      aoa_gear = 1;
      aoa_flap = (uint8_t)flap;
      for (int s = 0; s < 600; ++s) {
        if (aoa_on_ground && aoa_cam.front.z < kHold[i]) {
          aoa_input(AOA_INPUT_PITCH_UP);
          if (aoa_cam.front.z > kHold[i]) {
            aoa_cam.front.z = kHold[i];
            vec_orthonormalize(&aoa_cam);
          }
        }
        bool was = aoa_on_ground;
        aoa_advance();
        if (was && !aoa_on_ground) {
          v[flap] = aoa_speed;
          break;
        }
      }
    }
    printf("      %4d   | %4.1f  |     %5d     |     %5d\n", kHold[i],
           asin(kHold[i] / 256.0) * 180.0 / M_PI, v[0], v[1]);
  }
  printf("\n  flight.cc's own table (flight.md 5.2) is 1608 clean at front.z 16\n"
         "  and 1047 at front.z 47, against a stall speed of 1024 it was told.\n"
         "  Here the liftoff speed is the speed at which C_L(alpha) carries the\n"
         "  weight, so it is right by construction at every attitude and the\n"
         "  constant that was papering over it is not needed.\n");
}

// --------------------------------------------------------------------------
// 6. Glide, and the drag curve
// --------------------------------------------------------------------------

// Glide ratio from a settled state: the horizontal step over the vertical one.
static double glide_ratio(int16_t front_z, int16_t speed, int16_t vspeed) {
  if (vspeed >= 0) return 0.0;
  double fx = sqrt(256.0 * 256.0 - (double)front_z * front_z);
  double horiz = fx * (speed * 2) / 256.0;
  return horiz / -(double)vspeed;
}

static void section_glide() {
  hdr("6. Glide");

  printf("\n  Engine off, pitch held, run to a steady glide.\n\n");
  printf("  pitch |         flight.cc            |            AoA\n");
  printf("        |  speed   vspd   ratio        |  speed   vspd  alpha  ratio\n");
  printf("  ------+------------------------------+---------------------------\n");
  static const int16_t kPitch[] = {-8, -16, -25, -40, -50, -64, -80, -100};
  double best_s = 0, best_a = 0;
  int16_t best_sp = 0, best_ap = 0;
  for (unsigned i = 0; i < sizeof(kPitch) / sizeof(kPitch[0]); ++i) {
    trim_t s = settle_ship(0, kPitch[i], 256);
    trim_t a = settle_aoa(0, kPitch[i], 256);
    double rs = glide_ratio(kPitch[i], s.speed, s.vspeed);
    double ra = glide_ratio(kPitch[i], a.speed, a.vspeed);
    if (rs > best_s && !s.stalled) { best_s = rs; best_sp = kPitch[i]; }
    if (ra > best_a && !a.stalled) { best_a = ra; best_ap = kPitch[i]; }
    printf("  %5d | %6d %6d  %5.2f%s | %6d %6d %6d  %5.2f%s\n", kPitch[i],
           s.speed, s.vspeed, rs, s.stalled ? " stall" : "      ", a.speed,
           a.vspeed, a.alpha, ra, a.stalled ? " stall" : "");
  }
  printf("\n  best: flight.cc %.2f:1 at %d, AoA %.2f:1 at %d\n", best_s, best_sp,
         best_a, best_ap);
}

// --------------------------------------------------------------------------
// 7. The dive
// --------------------------------------------------------------------------

static void section_dive() {
  hdr("7. Terminal velocity");

  printf("\n  Nose straight down, held, run to a steady speed.\n\n");
  printf("  configuration          | flight.cc |   AoA\n");
  printf("  -----------------------+-----------+-------\n");
  struct { const char *name; uint8_t thr, flap, gear; } cases[] = {
      {"full throttle, clean", 24, 0, 0},
      {"full throttle, gear", 24, 0, 1},
      {"idle, clean", 0, 0, 0},
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    trim_t s = settle_ship(cases[i].thr, -256, 0, cases[i].flap, cases[i].gear);
    trim_t a = settle_aoa(cases[i].thr, -256, 0, cases[i].flap, cases[i].gear);
    printf("  %-22s |   %5d   | %5d\n", cases[i].name, s.speed, a.speed);
  }
  printf("\n  Both are under the 0x0F00 = 3840 clamp, so the clamp is still not\n"
         "  what limits the aircraft.\n");
}

// --------------------------------------------------------------------------
// 8. The one tunable that decides the feel
// --------------------------------------------------------------------------

// Everything the two scale tunables move, in four numbers.
struct polar_t {
  int throttle_floor;   // lowest throttle with a level trim, 0 if none
  int16_t cruise_speed; // settled level speed at full throttle
  double best_glide;
  int16_t best_climb;   // best vertical speed at full throttle
};

static polar_t measure_polar() {
  polar_t p;
  p.throttle_floor = 0;
  p.cruise_speed = 0;
  for (uint8_t thr = 4; thr <= kMaxThrottle; ++thr) {
    int16_t pitch = 0;
    trim_t t;
    if (lowest_level_pitch(true, thr, 256, 0, &pitch, &t)) {
      p.throttle_floor = thr;
      break;
    }
  }
  {
    int16_t pitch = 0;
    trim_t t;
    if (lowest_level_pitch(true, kMaxThrottle, 256, 0, &pitch, &t)) {
      p.cruise_speed = t.speed;
    }
  }
  p.best_glide = 0;
  for (int16_t pitch = -8; pitch >= -120; pitch -= 2) {
    trim_t g = settle_aoa(0, pitch, 256);
    if (g.stalled) continue;
    double r = glide_ratio(pitch, g.speed, g.vspeed);
    if (r > p.best_glide) p.best_glide = r;
  }
  p.best_climb = -32768;
  for (int16_t pitch = 0; pitch <= 200; pitch += 2) {
    trim_t c = settle_aoa(kMaxThrottle, pitch, 256);
    if (c.stalled) continue;
    if (c.vspeed > p.best_climb) p.best_climb = c.vspeed;
  }
  return p;
}

static void section_induced() {
  hdr("8. The two scale knobs, and what each of them is for");

  printf("\n  kAoaInducedShift sets the shape of the drag curve - where it\n"
         "  bottoms out, and therefore the throttle needed to hold level\n"
         "  flight (flight.md 2.1 puts that at 46%%).\n\n"
         "  kAoaForceShift divides thrust and drag together and leaves gravity\n"
         "  alone, so it is the aeroplane's weight against its engine and its\n"
         "  airframe. It moves the glide ratio without moving the top speed or\n"
         "  the throttle fractions.\n\n");

  printf("  induced  force |  level floor  cruise  best glide  best climb\n");
  printf("  ---------------+---------------------------------------------\n");
  const uint8_t si = kAoaInducedShift, sf = kAoaForceShift;
  struct { uint8_t ind, force; } cases[] = {
      {3, 0}, {4, 0}, {5, 0}, {4, 1}, {5, 1}, {4, 2},
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    kAoaInducedShift = cases[i].ind;
    kAoaForceShift = cases[i].force;
    polar_t p = measure_polar();
    printf("     %u       %u   |", cases[i].ind, cases[i].force);
    if (p.throttle_floor) {
      printf("   %2d (%3d%%)", p.throttle_floor,
             p.throttle_floor * 100 / kMaxThrottle);
    } else {
      printf("     none   ");
    }
    printf("  %5d     %5.2f:1     %+6d\n", p.cruise_speed, p.best_glide,
           p.best_climb);
  }
  kAoaInducedShift = si;
  kAoaForceShift = sf;

  printf("\n  For reference, the shipping model on the same measurements:\n");
  {
    int floor_thr = 0;
    for (uint8_t thr = 4; thr <= kMaxThrottle; ++thr) {
      int16_t pitch = 0;
      trim_t t;
      if (lowest_level_pitch(false, thr, 256, 0, &pitch, &t)) {
        floor_thr = thr;
        break;
      }
    }
    int16_t pitch = 0;
    trim_t t;
    lowest_level_pitch(false, kMaxThrottle, 256, 0, &pitch, &t);
    double best = 0;
    for (int16_t p2 = -8; p2 >= -120; p2 -= 2) {
      trim_t g = settle_ship(0, p2, 256);
      if (g.stalled) continue;
      double r = glide_ratio(p2, g.speed, g.vspeed);
      if (r > best) best = r;
    }
    int16_t climb = -32768;
    for (int16_t p2 = 0; p2 <= 200; p2 += 2) {
      trim_t c = settle_ship(kMaxThrottle, p2, 256);
      if (c.stalled) continue;
      if (c.vspeed > climb) climb = c.vspeed;
    }
    printf("  flight.cc      |   %2d (%3d%%)  %5d     %5.2f:1     %+6d\n",
           floor_thr, floor_thr * 100 / kMaxThrottle, t.speed, best, climb);
  }
  printf("\n  The last column is worth reading against TODO.md's \"climb rate is\n"
         "  too fast\", because it says the obvious fix is not one. Halving\n"
         "  thrust and drag halves the climb *angle*, but the aeroplane also\n"
         "  settles faster at any given angle, and the rate is the product of\n"
         "  the two: force shift 1 measures +717 against shift 0's +610. Climb\n"
         "  rate belongs to the gravity term, which this knob leaves alone on\n"
         "  purpose.\n\n"
         "  Closest to the aeroplane the game flies today is induced 4, force 0:\n"
         "  a slightly slower cruise, a slightly better glide, a slower climb,\n"
         "  and a level-flight floor that has moved from 45%% to 58%%. Induced 5\n"
         "  puts the floor back at 41%% and buys a 13:1 glide, at the cost of a\n"
         "  climb rate that is already the thing TODO.md complains about.\n");
}

// --------------------------------------------------------------------------
// 9. Camber
// --------------------------------------------------------------------------

static int floor_throttle(int16_t up_z, uint8_t flap) {
  for (uint8_t thr = 4; thr <= kMaxThrottle; ++thr) {
    int16_t pitch = 0;
    trim_t t;
    if (lowest_level_pitch(true, thr, up_z, flap, &pitch, &t)) {
      return thr;
    }
  }
  return 0;
}

static void section_camber() {
  hdr("9. kAoaCamberCl: how much inverted flight should cost");

  printf("\n  A permanent C_L offset, by the flap mechanism with the flaps\n"
         "  welded down. At 0 the section is symmetric and inverted flight is\n"
         "  exactly as cheap as upright, which flight.md 3.2 says it must not\n"
         "  be. It cuts both ways by construction: what it gives the upright\n"
         "  wing it takes from the inverted one.\n\n");
  printf("  camber | upright floor | inverted floor | upright cruise | inv pitch\n");
  printf("  -------+---------------+----------------+----------------+----------\n");
  const int16_t saved = kAoaCamberCl;
  static const int16_t kCam[] = {0, 64, 128, 192, 256, 384};
  for (unsigned i = 0; i < sizeof(kCam) / sizeof(kCam[0]); ++i) {
    kAoaCamberCl = kCam[i];
    int fu = floor_throttle(256, 0);
    int fi = floor_throttle(-256, 0);
    int16_t pu = 0, pi = 0;
    trim_t tu, ti;
    bool oku = lowest_level_pitch(true, kMaxThrottle, 256, 0, &pu, &tu);
    bool oki = lowest_level_pitch(true, kMaxThrottle, -256, 0, &pi, &ti);
    printf("   %4d  |", kCam[i]);
    if (fu) printf("   %2d (%3d%%)   |", fu, fu * 100 / kMaxThrottle);
    else printf("     none      |");
    if (fi) printf("    %2d (%3d%%)    |", fi, fi * 100 / kMaxThrottle);
    else printf("     none       |");
    printf("     %5d      |", oku ? tu.speed : 0);
    printf("   %5d\n", oki ? pi : 0);
  }
  kAoaCamberCl = saved;

  printf("\n  The floors barely move, and that is the finding: at trim the total\n"
         "  C_L is fixed by the weight, so camber changes the *attitude* that\n"
         "  carries it and not the induced drag it costs. What it does buy is\n"
         "  the inverted stall speed - the most negative C_L the wing can reach\n"
         "  is -(peak) + camber - and a nose-high inverted attitude to fly.\n\n");
  printf("  camber | inverted V_stall | upright V_stall | glide | best climb\n");
  printf("  -------+------------------+-----------------+-------+-----------\n");
  for (unsigned i = 0; i < sizeof(kCam) / sizeof(kCam[0]); ++i) {
    kAoaCamberCl = kCam[i];
    polar_t p = measure_polar();
    printf("   %4d  |       %5u      |      %5u      | %5.2f |     %+5d\n",
           kCam[i], aoa_speed_for_cl(aoa_cl_peak() - kCam[i]),
           aoa_speed_for_cl(aoa_cl_peak() + kCam[i]), p.best_glide,
           p.best_climb);
  }
  kAoaCamberCl = saved;

  printf("\n  Camber on its own moves the upright stall speed too, and 0x0400 is\n"
         "  a number the airspeed dial's green arc and the whole of flight.md\n"
         "  5.3 are built on. Lowering the stall *angle* by the same amount\n"
         "  puts it back: upright C_L max is peak + camber, so holding that sum\n"
         "  at 1024 holds the clean stall speed at 1024 exactly, and the camber\n"
         "  then comes entirely out of the inverted side.\n\n");
  printf("  stall angle / camber |  clean  flap  inverted  inv+flap | floor  glide  climb\n");
  printf("  ---------------------+----------------------------------+--------------------\n");
  const int16_t sa = kAoaAlphaStall;
  struct { int16_t alpha, cam; } kPair[] = {
      {64, 0}, {60, 64}, {56, 128}, {52, 192}, {48, 256},
  };
  for (unsigned i = 0; i < sizeof(kPair) / sizeof(kPair[0]); ++i) {
    kAoaAlphaStall = kPair[i].alpha;
    kAoaCamberCl = kPair[i].cam;
    const int16_t pk = aoa_cl_peak();
    polar_t p = measure_polar();
    int fu = p.throttle_floor;
    printf("     %2d / %3d          | %6u %5u %9u %9u | %2d%%  %6.2f %+6d\n",
           kPair[i].alpha, kPair[i].cam, aoa_speed_for_cl(pk + kAoaCamberCl),
           aoa_speed_for_cl(pk + kAoaCamberCl + kAoaFlapDeltaCl),
           aoa_speed_for_cl(pk - kAoaCamberCl),
           aoa_speed_for_cl(pk - kAoaCamberCl - kAoaFlapDeltaCl),
           fu * 100 / kMaxThrottle, p.best_glide, p.best_climb);
  }
  kAoaAlphaStall = sa;
  kAoaCamberCl = saved;
}

// --------------------------------------------------------------------------

int main(int argc, char **argv) {
  const char *only = argc > 1 ? argv[1] : nullptr;
  flight_set_step_shift(0);
  // The prototype's tunables set to what flight.cc now ships, so the two
  // columns are the same aeroplane and any difference between them is a
  // porting bug rather than a tuning choice.
  kAoaAlphaStall = 56;
  kAoaCamberCl = 128;
  aoa_init();

  printf("Prototype: an angle-of-attack flight model, against the shipping one\n");
  printf("Both at flight_step_shift 0 (stock C64, 6.25 model steps a second).\n");

  if (!only || !strcmp(only, "wing")) section_wing();
  if (!only || !strcmp(only, "level")) section_level();
  if (!only || !strcmp(only, "turns")) section_turns();
  if (!only || !strcmp(only, "stall")) section_stall();
  if (!only || !strcmp(only, "takeoff")) section_takeoff();
  if (!only || !strcmp(only, "glide")) section_glide();
  if (!only || !strcmp(only, "dive")) section_dive();
  if (!only || !strcmp(only, "induced")) section_induced();
  if (!only || !strcmp(only, "camber")) section_camber();
  printf("\n");
  return 0;
}
