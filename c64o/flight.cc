#include "flight.h"

#include <stdint.h>
#include <stdlib.h>

#include "fmath.h"
#include "msg.h"
// For kSpriteDataCompressed / kSpriteScratchPath below. Nothing else here needs
// it, and no other header in this file's chain pulls it in - without this the
// borrowed path buffer only compiles when some earlier translation unit
// happened to have included it first.
#include "sprites.h"
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
// 256 bytes, and they cost nothing: they sit in the tail of the compressed
// sprite blob, which is scrap from the moment mem_init() has expanded it to
// $D400. mem.cc already aliases the viewport colour buffer onto the front of
// it; sprites.h has the map and the size assert.
//
// Spelled without a cast because `(uint8_t *)kSpriteDataCompressed + n` is not
// a constant initializer to oscar64 while the implicit char* conversion is.
// The address is a link-time constant either way, so the generated code is the
// same absolute indexing an ordinary array would get.
//
// Unlike bss, these bytes start out holding compressed sprite data rather than
// zeroes. Nothing reads them before they are written: flight_path_count starts
// at 0, is reset by both flight_init paths, and bounds the only reader
// (_map_draw_path). Keep it that way -- an index not derived from
// flight_path_count would paint LZO onto the map.
#ifdef __OSCAR64__
uint8_t *const flight_path_px = kSpriteDataCompressed + kSpriteScratchPath;
uint8_t *const flight_path_py =
    kSpriteDataCompressed + kSpriteScratchPath + kFlightPathLen;
#else
// The host tests link flight.cc without sprites.cc and have no blob to borrow
// from. Ordinary storage; nothing off-target cares where it is.
static uint8_t _flight_path_host[2 * kFlightPathLen];
uint8_t *const flight_path_px = _flight_path_host;
uint8_t *const flight_path_py = _flight_path_host + kFlightPathLen;
#endif

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

#ifdef __FLIGHT_AOA__
int16_t flight_gamma;
int16_t flight_alpha16;
#endif

static bool model_on_ground = false;
static bool model_need_normalize;

// The part of a step's speed change that was finer than one unit, carried to
// the next step. Every longitudinal force is summed at eight times
// flight_speed's resolution and divided once at the end, so a drag worth two
// thirds of a unit a step really does cost two units every three steps instead
// of being truncated to nothing.
//
// The old model truncated each of its five terms separately and kept no
// remainder, which it could afford because none of its terms was ever small.
// The induced term at high speed is, and so is every term once
// flight_step_shift divides them - which is also why the flat `-= 2` of ground
// friction no longer needs its own model_substep gate: it is a force like the
// others now, and the remainder makes a quarter of it exact.
#ifdef __FLIGHT_AOA__
static int16_t model_dv_rem;
// The same, for the turn. Without it a gentle bank does not turn at all: the
// rate at 15 degrees and cruise works out under one unit a step, and one unit
// is the smallest turn vec_turn3_xy can be asked for.
static int16_t model_turn_rem;
#endif

// Where we are inside one of the model's old, larger steps (vec.h). Terms too
// small to divide - a flat subtraction, or one with a floor of 1 under it -
// are applied on the step where this is zero and skipped on the rest, so their
// total over a whole old step is unchanged. At shift 0 the mask is 0, this is
// always zero, and every one of them fires on every step exactly as before.
static uint8_t model_substep;

uint8_t flight_step_shift;
uint8_t kFlightFramesPerStep = 8;
uint8_t kFlightSubstepMask = 0;

void flight_set_step_shift(uint8_t shift) {
  flight_step_shift = shift;
  kFlightFramesPerStep = (8 >> shift) ? (8 >> shift) : 1;
  kFlightSubstepMask = (uint8_t)((1 << shift) - 1);
  vec_set_rotation_shift(shift);
  model_substep = 0;
}

// (v >> n) >> flight_step_shift, which is exactly v >> (n + shift) - a right
// shift truncates, so splitting it changes nothing. Written as a loop because
// the 6502 has no variable shift.
//
// __noinline deliberately, and measured: ten call sites inlined cost 217 bytes
// more than one copy each and saved 206 cycles a model step. At 6.25 steps a
// second that is 0.13% of the machine against a fifth of a kilobyte, and bytes
// are the scarcer thing here.
static __noinline uint16_t _flight_step_u(uint16_t v) {
  uint8_t n = flight_step_shift;
  while (n--) {
    v >>= 1;
  }
  return v;
}

static __noinline int16_t _flight_step_s(int16_t v) {
  uint8_t n = flight_step_shift;
  while (n--) {
    v >>= 1;
  }
  return v;
}

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

static const uint32_t kFlightMinEyeZ = 0x2000;
// Ceiling for the two "stay low" waypoint constraints, on the altitude scale
// where 1000 ft is 0x020000 and the ground is kFlightMinEyeZ - so 250 ft, of
// which the lowest 62 ft is the ground itself. It was 125 ft, which left the
// crop duster a 62 ft band to hold between the limit and the dirt: two ticks
// of the altimeter's fine needle, which moves one per 31 ft.
static const uint32_t kFlightWpMaxAlt = 0x008000;
// kMaxSpeed lives in flight.h: sound.cc sizes its wind table by it.
//
// Weight. The lift the wing has to make to hold altitude, and the number every
// other constant here is measured against.
static const int16_t kFlightTrimLift = 0x1000;
static const uint8_t kFlightMinThrottle = 0x00;
// kMaxThrottle lives in flight.h: sound.cc sizes its engine pitch table by it.

#ifdef __FLIGHT_AOA__

// --- The wing -------------------------------------------------------------
//
// There is no stall *speed* in this model and no lift curve table either. Both
// used to be here, and what replaced them is one identity: the lift slope is
// chosen so that C_L in 8.8 and flight_alpha16 are the same number.
//
//   C_L = alpha16 = (front.z << 4) - flight_gamma      (below the stall)
//
// So the stall is an angle, the speeds fall out of it, and the two constants
// this replaced - 0x0400 clean and 0x0340 with flaps - are now consequences
// rather than assertions. They come out at 1024 and 836.

// The angle at which C_L peaks, in front.z's units: 56/256, about 12.6
// degrees. This is the stall.
static const int16_t kFlightAlphaStall = 56;
// Peak C_L, which is that angle in alpha16's units and is not free of it.
// Raise the stall angle and the peak rises with it and the stall speed falls,
// which is the relationship a real wing has and the one two independent
// constants could not hold.
static const int16_t kFlightClPeak = kFlightAlphaStall << 4;

// Camber: C_L at zero alpha. A wing is not symmetric, and this is what makes
// inverted flight expensive - the whole of flight.md 3.2, in one addition and
// with no `up.z < 0` case anywhere. Upright it adds to the C_L the attitude
// needs; inverted the attitude needs a negative C_L and it fights that.
//
// It is paired with the stall angle deliberately. Upright C_L max is
// peak + camber, so holding that sum at 1024 holds the clean stall speed at
// exactly the 0x0400 the airspeed dial's green arc and the whole of flight.md
// 5.3 were built on, and the camber then comes entirely out of the inverted
// side: 56 + 128 gives 1024 upright and 1182 inverted.
static const int16_t kFlightCamberCl = 128;

// Flaps, by the same mechanism: a camber shift, added rather than multiplied.
// +512 against a 1024 upright C_L max is what a single slotted flap is worth,
// and it puts the flapped stall speed at 836 against the 0x0340 = 832 the old
// model was told. Inverted it stacks with the camber the wrong way, which is
// the adverse camber penalty, and the inverted flapped stall goes to 2048.
static const int16_t kFlightFlapDeltaCl = 512;

// Where the post-stall droop is called flat: alpha 128, about 30 degrees.
static const int16_t kFlightMaxAlpha16 = 2048;

// Saturation on the lift product, before the two bits are shifted back on.
// 4096 << 2 is four times kFlightTrimLift, so the wing tops out at 4 g.
//
// A range guard first and a load limit second. oscar64's int is 16 bits
// (test/int16.h) and C_L * V^2 does not fit it at the top of the speed range -
// at kMaxSpeed with the flaps out the honest product is 57,600, which wraps.
// Saturating is both the cheap fix and the physical one.
static const int16_t kFlightLiftSat = 4096;

// Induced drag: (C_L^2 * V^2) >> this. One term, replacing three separate
// stand-ins the old model needed - the lift deficit's `deficit >> 10`, the
// bank term `left.z^2 >> 5`, and the implicit inverted penalty - because all
// three were the same thing, the wing being asked for lift it has to work for.
//
// It sets where the drag curve bottoms out, and with it the throttle needed to
// hold level flight, the best glide ratio and the climb rate. Measured against
// the model this replaced, whose figures are in brackets:
//
//   shift 4:  floor 58%,  cruise 2340,  glide 3.84:1,  best climb +354
//   shift 5:  floor 33%,  cruise 2429,  glide 7.33:1,  best climb +526
//   (old model: floor 45%, cruise 2509, glide 6.13:1, best climb +663)
//
// 5 is the one that flies like the aeroplane the missions were built around,
// and it is a better aeroplane on the two counts that were open: the glide is
// longer and the climb is slower, which is what TODO.md asks for.
//
// 4 was chosen first, on prototype numbers that turned out to be measuring the
// harness - a glide read off a run that had never settled, in half density air
// a long way above the ceiling knee. See docs/flight_aoa.md 4.
static const uint8_t kFlightInducedShift = 5;

// 65536 / V, indexed by V >> 8. The 1/V in "flight path curvature is force
// over momentum", and the same 1/V in "turn rate is horizontal lift over
// momentum". A table because flight.md 1 forbids runtime division, and sixteen
// entries because that is the whole speed range at a resolution the aircraft
// cannot feel.
//
// Entry 0 is a clamp rather than a value: 65536/128 is 512, which would
// overflow the products it feeds, and under V = 256 the aircraft is falling
// rather than flying so the exact rate is not something a pilot can see.
static const int16_t kFlightRecipV[16] = {
    256, 170, 102, 73, 56, 46, 39, 34, 30, 26, 24, 22, 20, 18, 17, 16,
};

// Nose attitude above which the stall break has to be done as a rotation
// instead of by lowering front.z directly. sin(61 deg) * 256; past this the
// horizontal part of front is small enough that renormalization undoes most
// of a direct change.
static const int16_t kFlightMaxStallPitchZ = 224;

// How far the pilot can rotate on the runway before the tail hits. A limit,
// not a target.
//
// kFlightRotatePitchZ used to live here - the constant that drove the nose to
// a fixed 10.6 degrees the moment the aircraft passed a stall speed, and whose
// own comment said "if lift ever grows a pitch term this constant should be
// deleted rather than retuned". Lift has grown one, so it is deleted. Rotating
// now makes lift, the aircraft unsticks when the wing carries it, and the
// liftoff speed follows from the attitude the pilot chose instead of from a
// number written down here.
// 48 is sin(10.8 deg), and it is bounded from both sides. Above, by
// kFlightMaxLandingPitch = 64: the envelope check runs on *every* frame at
// ground level (flight.md 5.3), so a rotation limit at or near 64 crashes the
// takeoff roll on trigger 7 - at exactly 64 it did, because
// vec_orthonormalize puts back a unit and 65 > 64. Below, by the stall angle:
// rotating fully has to leave the wing short of the break, and 48 against
// kFlightAlphaStall = 56 does. The liftoff that falls out is airspeed 1094
// against a 1024 stall speed, which is the margin a real rotation has.
static const int16_t kFlightMaxGroundPitch = 48;

#else // !__FLIGHT_AOA__

// The arcade model's wing: no angle of attack, so the two stall speeds are
// constants it is told rather than consequences it derives. kFlightTrimLift,
// kFlightMinThrottle and kMaxSpeed are shared and are declared above.
static const uint16_t kFlightStallSpeedWithoutFlaps = 0x0400;
static const uint16_t kFlightStallSpeedWithFlaps = 0x0340;

// Nose attitude above which the stall pitch down has to be done as a rotation
// instead of by lowering front.z directly. sin(61 deg) * 256; past this the
// horizontal part of front is small enough that renormalization undoes most
// of a direct change.
static const int16_t kFlightMaxStallPitchZ = 224;

// Nose attitude the takeoff rotation drives to, sin(10.6 deg) * 256.
//
// This is a fudge, and it is here to compensate for a hole in the model rather
// than because a wing works this way: lift is f(V^2, up.z) and front.z never
// enters it (flight_review.md A), so rotating the nose cannot make the force
// that lifts the aircraft off. All pitch can do is aim the flight path up and
// out-climb the sink penalty, and with the one step the rotation used to apply
// (front.z = 16, ~3.6 deg) that crossover sits at airspeed 1608 - about 40% of
// the way along the green arc on the airspeed dial, whose bottom end is the
// 0x0400 stall speed. The aircraft was therefore legal to rotate long before it
// could fly, and every frame in between lifted the wheels and put them straight
// back down, which the sound driver hears as a fresh touchdown each time.
//
// 47 puts the crossover at 1093, just inside the green, which is what the dial
// has been promising all along. If lift ever grows a pitch term this constant
// should be deleted rather than retuned.
static const int16_t kFlightRotatePitchZ = 47;
// Bound on the loop below. The rotation is expressed as an attitude rather than
// a number of pitch steps because the steps are scaled by the machine's speed
// (vec_set_rotation_shift), so a fixed count would rotate a stock C64 and a
// SuperCPU to different attitudes and give them different liftoff speeds. At
// kCpuMaxStepShift the step is 16 >> 2 = 4, so twelve is the most it can need.
static const uint8_t kFlightMaxRotateSteps = 16;

#endif // __FLIGHT_AOA__

// Landing thresholds
static const int16_t kFlightMaxLandingRoll = 32;
// Wings-up check. kMaxLandingRoll alone does not catch an inverted arrival:
// left.z is back to ~0 after a full 180 degree roll, so a belly-up landing
// used to pass the bank check. up.z is the attitude the roll limit is really
// trying to express. Zero rather than a tight cos(roll) bound because up.z
// also drops with nose-up pitch, and a legal flare must not trip this.
static const int16_t kFlightMinLandingUpZ = 0;
static const int16_t kFlightMinLandingPitch = -32;
static const int16_t kFlightMaxLandingPitch = 64;
// Sink rate limit. Has to sit inside the range reachable ABOVE stall speed to
// mean anything: a below-stall arrival has already had its nose pushed past
// kMinLandingPitch by the stall break, so trigger 4 owns it. Above stall the
// worst sink any legal attitude produces is -251, so the old -0x0180 could
// never fire. At -0x00E0 a level-or-nose-up flare is always survivable and a
// At -0x0120 a level-or-nose-up flare is always survivable and moderate
// nose-down landings survive, while steep nose-down dives still trigger sink
// limits.
static const int16_t kFlightMaxLandingVSpeed = -0x0120;
static const uint16_t kFlightMaxLandingSpeed = 0x0A00;
// The envelope check runs every frame the aircraft is at ground level, not
// just on the touchdown frame, so it also polices taxi and takeoff roll. That
// is wanted for the gear check - rolling on a retracted gear should fail
// immediately - but kMaxLandingSpeed is an impact limit and is far too close
// to the speeds a ground roll legitimately reaches. Full throttle with the
// gear down settles at 2290, only 270 under it, so any future drag or thrust
// tweak could turn a normal takeoff run into a crash. Once already rolling the
// limit is this looser one, which still catches nonsense start states from
// mission data but leaves the takeoff roll ~45% of headroom.
static const uint16_t kFlightMaxGroundSpeed = 0x0D00;

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
#ifdef __FLIGHT_AOA__
  flight_gamma = 0;
  flight_alpha16 = 0;
  model_dv_rem = 0;
  model_turn_rem = 0;
#endif
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

void flight_init_from_mission(uint8_t mission_idx) {
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  flight_active_mission_idx = mission_idx;
  flight_current_wp = 0;
  flight_cam = _m_init;
  flight_eye_x = (int32_t)kMissionStartX[mission_idx] << 16;
  flight_eye_y = ((int32_t)kMissionStartY[mission_idx] << 16) + 0x8000;
  flight_eye_z = (int32_t)kMissionStartZ[mission_idx] << 16;
  if (flight_eye_z <= kFlightMinEyeZ) {
    flight_eye_z = kFlightMinEyeZ;
    model_on_ground = true;
  } else {
    model_on_ground = false;
  }
  flight_speed = (int16_t)kMissionStartSpeed[mission_idx] << 4;
  flight_throttle = kMissionStartThrottle[mission_idx];
  flight_fuel = kMissionStartFuel[mission_idx]
                    ? (((uint32_t)kMissionStartFuel[mission_idx] << 12) - 1)
                    : 0;
#ifdef __FLIGHT_AOA__
  flight_gamma = 0;
  flight_alpha16 = 0;
  model_dv_rem = 0;
  model_turn_rem = 0;
#endif
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

#ifdef __FLIGHT_AOA__

// The lift curve. Below the stall it is not a curve at all but the identity:
// C_L in 8.8 and alpha16 are the same number, because the lift slope was
// chosen to make them so. No table, no index arithmetic, no interpolation, and
// no quantization step between one attainable C_L and the next beyond the one
// alpha16 already has.
//
// Past the peak the wing droops rather than holding: pull harder there and you
// get *less* lift, so the flight path falls away faster than the nose does and
// alpha runs away. That is what makes a stall a stall rather than a ceiling,
// and the break in flight_advance() is the pitching moment that ends it. Five
// eighths is the droop either side of a real break.
static int16_t _flight_cl16(int16_t alpha16) {
  int16_t a = alpha16 < 0 ? -alpha16 : alpha16;
  int16_t cl;
  if (a <= kFlightClPeak) {
    cl = a;
  } else {
    if (a > kFlightMaxAlpha16) {
      a = kFlightMaxAlpha16;
    }
    cl = (int16_t)(kFlightClPeak - (((a - kFlightClPeak) * 5) >> 3));
  }
  return alpha16 < 0 ? -cl : cl;
}

static int16_t _flight_recip_v(int16_t speed) {
  uint8_t i = (uint8_t)((uint16_t)speed >> 8);
  if (i > 15) {
    i = 15;
  }
  return kFlightRecipV[i];
}

#endif // __FLIGHT_AOA__

#ifdef __FLIGHT_AOA__

// Returns true if the step ran into the ground plane, which is the only thing
// that can tell the contact check below the difference between an aircraft
// sitting *at* ground level - which is where one that has just unstuck sits
// while its flight path builds - and one that arrived there from underneath or
// through it. The clamp erases that difference, so it has to be reported.
static bool _flight_move_forward(int16_t fspeed, int16_t vspeed) {
  // The shift is after the multiply, not on fspeed: the product carries the
  // bits a quarter-sized step needs and fspeed on its own does not.
  flight_eye_x += _flight_step_s(vec_fastmul8p8(flight_cam.front.x, fspeed));
  flight_eye_y += _flight_step_s(vec_fastmul8p8(flight_cam.front.y, fspeed));
  flight_eye_z += _flight_step_s(vspeed);
  if (flight_eye_z < kFlightMinEyeZ) {
    flight_eye_z = kFlightMinEyeZ;
    return true;
  }
  return false;
}

#else // !__FLIGHT_AOA__

static void _flight_move_forward(int16_t fspeed, int16_t vspeed) {
  // The shift is after the multiply, not on fspeed: the product carries the
  // bits a quarter-sized step needs and fspeed on its own does not.
  flight_eye_x += _flight_step_s(vec_fastmul8p8(flight_cam.front.x, fspeed));
  flight_eye_y += _flight_step_s(vec_fastmul8p8(flight_cam.front.y, fspeed));
  flight_eye_z += _flight_step_s(vspeed);
  if (flight_eye_z < kFlightMinEyeZ) {
    flight_eye_z = kFlightMinEyeZ;
  }
}

#endif // __FLIGHT_AOA__

// True when the aircraft is over a runway tile.
static bool _flight_on_runway() {
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
static enum FlightStatus _flight_landing_fault(uint16_t speed_limit,
                                               bool check_runway) {
  if (check_runway && !_flight_on_runway()) {
    return FLIGHT_CRASH_NOT_ON_RUNWAY;
  }
  if (flight_cam.up.z < kFlightMinLandingUpZ) {
    return FLIGHT_CRASH_INVERTED;
  }
  if (!flight_gear) {
    return FLIGHT_CRASH_GEAR;
  }
  if (flight_vspeed < kFlightMaxLandingVSpeed) {
    return FLIGHT_CRASH_VSPEED;
  }
  if (_abs16(flight_cam.left.z) > kFlightMaxLandingRoll) {
    return FLIGHT_CRASH_ROLL;
  }
  if (flight_cam.front.z < kFlightMinLandingPitch) {
    return FLIGHT_CRASH_PITCH_LOW;
  }
  if (flight_cam.front.z > kFlightMaxLandingPitch) {
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
static const char *const kFlightFaultText[] = {
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
static const char *const kFlightWaypointFault[] = {
    "",                   // 0 WP_NOTHING
    "",                   // 1 WP_LANDED
    "CLIMB ABOVE 1000FT", // 2 WP_MIN_1000FT
    "CLIMB ABOVE 3000FT", // 3 WP_MIN_3000FT
    "GO BELOW 250FT",     // 4 WP_MAX_250FT
    "FLY LOW INVERTED"    // 5 WP_UPSIDE_DOWN
    ,
};

// Both prefixes are nine characters and the longest fault text is fourteen,
// so 24 is the exact fit and this leaves a little room. Built here rather
// than stored per message because msg_show() keeps only the pointer. Every
// caller below bails out once flight_status is set, so no warning can rewrite
// the buffer while a crash message is still on screen.
static char _flight_status_text[40];

static const char *_flight_join(const char *prefix, const char *suffix) {
  char *dst = _flight_status_text;
  while (*prefix) {
    *dst++ = *prefix++;
  }
  while (*suffix) {
    *dst++ = *suffix++;
  }
  *dst = 0;
  return _flight_status_text;
}

const char *flight_status_text(enum FlightStatus status, bool crashed) {
  if (status == FLIGHT_MISSION_COMPLETED) {
    return "MISSION COMPLETE!";
  }
  return _flight_join(crashed ? "CRASHED: " : "WARNING: ",
                      kFlightFaultText[status]);
}

// Longer than a waypoint message, because it is the one the pilot flew the
// whole mission for, but still temporary: the flight continues underneath it
// and the message row belongs to the warnings again once it expires.
static const uint16_t kFlightMissionCompleteDuration = 3 * MSG_DEFAULT_DURATION;

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
    // Distance to the waypoint the short way round each of the world's two
    // rings, rather than a signed difference.
    //
    // The world repeats, and the two axes do not repeat with the same period.
    // kWorldMap is kWorldMapHeight rows of 8 world units, so x comes back
    // round every 128 - which is also the period _flight_on_runway() matches
    // on and the one the map view draws (_flight_path_sample above). y is
    // kWorldMapWidth columns of 8, a whole 256. Folding each difference to
    // the shorter way round is what lets a waypoint match on whichever copy
    // of the world the aircraft happens to be flying over: without it the
    // aeroplane could sit on runway 2, at the map pixel the runway is drawn
    // at, and be told it was 128 units away.
    //
    // Unsigned all through. The signed form this replaces took its absolute
    // value by negating an int8_t, and -128 negates to itself, so a waypoint
    // exactly 128 units off in y - half the map - passed a +/-16 test.
    uint8_t adx = (uint8_t)(eye_x_high - wx) & 0x7F;
    if (adx > 64) {
      adx = 128 - adx;
    }
    uint8_t ady = (uint8_t)(eye_y_high - wy);
    if (ady > 128) {
      ady = (uint8_t)(0 - ady);
    }
    uint8_t max_dy = (constraint == WP_LANDED) ? 0x04 : 0x10;
    pos_ok = (adx <= 0x10 && ady <= max_dy);
  }

  // Altitude limits live in a table so the MIN_*FT cases share one
  // comparison instead of open-coding separate 32-bit ones.
  // Indexed by MissionWaypointConstraint; keep in sync with mission.h.
  static const uint8_t kWpMinAltHi[] = {
      0, // 0 WP_NOTHING
      0, // 1 WP_LANDED       (handled below)
      2, // 2 WP_MIN_1000FT   (0x020000 >> 16)
      6, // 3 WP_MIN_3000FT
      0, // 4 WP_MAX_250FT    (handled below)
      0, // 5 WP_UPSIDE_DOWN  (handled below)
  };
  bool met = pos_ok;
  if (met) {
    switch (constraint) {
    case WP_MAX_250FT:
      met = flight_eye_z <= kFlightWpMaxAlt;
      break;
    case WP_UPSIDE_DOWN:
      // A low pass, which is what the briefing asks for and what the crowd
      // came to see - so the altitude counts as much as the attitude. up.z is
      // 256 * cos(roll), so the bound is 120 degrees of roll: enough that the
      // aeroplane is unambiguously on its back rather than steeply banked,
      // which anything merely below zero would have allowed.
      met = flight_cam.up.z < -128 && flight_eye_z <= kFlightWpMaxAlt;
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
               kFlightMissionCompleteDuration);
      if (flight_active_mission_idx < kMissionCount) {
        mission_completed[flight_active_mission_idx] = true;
      }
    }
  } else if (pos_ok && (wx != 0 || wy != 0)) {
    // Over the waypoint but the constraint is not satisfied: say which one,
    // so the player knows they are in the right place and only the altitude
    // or the attitude is wrong.
    const char *fault = kFlightWaypointFault[constraint];
    if (*fault) {
      msg_show(_flight_join(fault, " FOR MISSION"));
    }
  }
}

void flight_advance() {
  if (flight_crashed()) {
    return;
  }

  // Whether this step is the one that carries the terms too small to divide
  // (see model_substep). Needed after the paused branch as well, so it is
  // declared out here.
  const bool whole_step = (model_substep == 0);

  if (!flight_paused) {
    // Altitude density decay (above Z = 0x080000)
    uint8_t alt_penalty = 0;
    if (flight_eye_z > 0x080000) {
      uint32_t alt_diff = (flight_eye_z - 0x080000) >> 12;
      alt_penalty = (alt_diff > 128) ? 128 : (uint8_t)alt_diff;
    }
    uint16_t density = 256 - alt_penalty;

#ifdef __FLIGHT_AOA__

    uint16_t speed_sqr = vec_fastsqr8p8(flight_speed);

    // --- The wing --------------------------------------------------------
    //
    // Angle of attack: the angle between where the nose points and where the
    // aircraft is going. front.z is sin(pitch), flight_gamma_z() is sin(flight
    // path), and their difference is sin(alpha) to first order. On the ground
    // the flight path is the runway, so alpha is simply the nose attitude -
    // which is why rotating is what gets the aircraft off it.
    //
    // Both sides are taken at flight_gamma's scale rather than front.z's, so
    // the difference has sixteen times the resolution of a direction cosine.
    // That is not a refinement: at the coarse scale one step of alpha is a
    // sixteenth of a g of lift, and a flight path integrated from a force that
    // quantized cannot settle, only hunt.
    //
    // The sign flip is not a fudge either. Angle of attack is a *body* angle -
    // which side of the wing the air arrives from - and rolling inverted swaps
    // those sides. Upside down, a nose held above the flight path in world
    // terms is air arriving on the canopy side, which is negative alpha and
    // negative lift. Everything flight.md 3.2 says about inverted flight is
    // this line.
    int16_t alpha_d = (int16_t)(flight_cam.front.z << 4) - flight_gamma;
    flight_alpha16 = flight_cam.up.z >= 0 ? alpha_d : -alpha_d;

    int16_t cl = _flight_cl16(flight_alpha16) + kFlightCamberCl;
    if (flight_flap) {
      cl += kFlightFlapDeltaCl;
    }

    int16_t lift_t = vec_fastmul8p8((int16_t)(speed_sqr >> 4), cl);
    if (lift_t > kFlightLiftSat) {
      lift_t = kFlightLiftSat;
    } else if (lift_t < -kFlightLiftSat) {
      lift_t = -kFlightLiftSat;
    }
    int16_t lift = vec_fastmul8p8((int16_t)(lift_t << 2), density);
    int16_t lift_z = vec_fastmul8p8(lift, flight_cam.up.z);

    // Liftoff, and this is the whole of it: the wing carries the aeroplane, so
    // it flies. No speed gate, no rotation target - the pilot holds an
    // attitude and the aircraft leaves the ground at whatever speed that
    // attitude needs.
    //
    // It has to happen here, ahead of the flight path integration below, and
    // that is not a tidiness point. Decided after it, the first airborne step
    // would be one with a flight path still pinned flat by the ground branch:
    // no climb, no rise in altitude, and the ground contact check at the
    // bottom would put the wheels straight back down. That is exactly the
    // once-a-frame touchdown cycle kFlightRotatePitchZ existed to paper over,
    // and an AoA model does not have to live with it.
    if (model_on_ground && lift_z > kFlightTrimLift) {
      model_on_ground = false;
    }

    // --- Drag, thrust and gravity ----------------------------------------
    //
    // Summed at eight times flight_speed's resolution and divided once, with
    // the remainder carried (model_dv_rem). Parasite, gear and flap keep the
    // coefficients they always had.
    int16_t dv8 = 0;
    dv8 -= (int16_t)(speed_sqr >> 7);
    if (flight_gear) {
      dv8 -= (int16_t)(speed_sqr >> 9);
    }
    if (flight_flap) {
      dv8 -= (int16_t)(speed_sqr >> 9);
    }
    if (!model_on_ground) {
      // C_L past the peak is capped for the drag term. Induced drag goes as
      // the square of the lift actually made, and past the stall the wing is
      // not making more of it - what rises there is separation drag, which
      // this stands in for rather than pretends to model. It also keeps the
      // product inside sixteen bits, which the honest one does not at the top
      // of the speed range.
      int16_t cl_d = cl;
      if (cl_d > kFlightClPeak) {
        cl_d = kFlightClPeak;
      } else if (cl_d < -kFlightClPeak) {
        cl_d = -kFlightClPeak;
      }
      int16_t cl_sqr = vec_fastmul8p8(cl_d, cl_d);
      dv8 -= vec_fastmul8p8((int16_t)(speed_sqr >> 5), cl_sqr) >>
             kFlightInducedShift;
    } else if (flight_throttle == 0 && flight_speed > 0) {
      // Wheel friction: the old flat 2 units a step, at eight times the
      // resolution. A force like the others now, so flight_step_shift divides
      // it along with them and it no longer needs a model_substep gate.
      dv8 -= 16;
    }
    if (flight_fuel > 0) {
      dv8 += vec_fastmul8p8((int16_t)(flight_throttle << 3), density);
    }

    // Gravity along the *flight path*, not along the nose. The old model used
    // front.z because it had nothing else, and the difference is exactly the
    // aeroplane that is pointing up while sinking - the attitude every
    // approach and every stall entry is flown at.
    dv8 -= flight_gamma_z();

    dv8 = _flight_step_s(dv8);
    dv8 += model_dv_rem;
    int16_t dv = dv8 >> 3;
    model_dv_rem = (int16_t)(dv8 - (int16_t)(dv << 3));
    flight_speed += dv;

    if (flight_speed < 0) {
      flight_speed = 0;
    } else if (flight_speed > (int16_t)kMaxSpeed) {
      flight_speed = (int16_t)kMaxSpeed;
    }

    if (!model_on_ground) {
      // --- The flight path ------------------------------------------------
      //
      // Net vertical force over momentum. This one line is the whole of the
      // model's vertical behaviour: the sink at low speed, the climb, the
      // altitude a banked turn loses and the balloon when the flaps come out
      // are all this seeing a different `net`.
      //
      // Unlike the lift deficit it replaced, it is two-sided. Lift above the
      // weight pushes the flight path up, where the old model discarded it.
      int16_t net = lift_z - kFlightTrimLift;
      flight_gamma +=
          _flight_step_s(vec_fastmul8p8(net, _flight_recip_v(flight_speed))) >>
          3;
      if (flight_gamma > 4096) {
        flight_gamma = 4096;
      } else if (flight_gamma < -4096) {
        flight_gamma = -4096;
      }
    }

    // --- The stall -------------------------------------------------------
    //
    // An angle, not a speed. It therefore also fires in the two cases a speed
    // gate cannot see at all: the accelerated stall - pulling hard well above
    // the 1 g stall speed - and the inverted stall.
    flight_stall = 0;
    if (!model_on_ground) {
      int16_t excess =
          (int16_t)((flight_alpha16 < 0 ? -flight_alpha16 : flight_alpha16) -
                    kFlightClPeak);
      if (excess > 0) {
        flight_stall = 1;
        uint8_t brk = (uint8_t)(_flight_step_u((uint16_t)excess) >> 5);
        if (brk == 0) {
          brk = whole_step ? 1 : 0;
        }
        // The break drives the nose back toward the flight path, which is what
        // a stalled wing's pitching moment does. It needs no "toward the
        // ground" special case: at low speed the flight path is already
        // steeply down, so chasing it *is* the nose drop, at any attitude and
        // either way up.
        //
        // Which way front.z has to move is the sign of alpha_d, not of alpha -
        // the two differ when inverted. Which body rotation to use in the dead
        // spot is the sign of alpha and does not: a pitch-down step moves
        // front.z by -up.z/16, so the up.z in it cancels the up.z in alpha_d.
        const bool nose_down = alpha_d > 0;
        if (nose_down ? flight_cam.front.z > kFlightMaxStallPitchZ
                      : flight_cam.front.z < -kFlightMaxStallPitchZ) {
          // Dead spot. front is a unit vector, so with the nose this high its
          // horizontal part is almost nothing and vec_orthonormalize scales
          // the whole vector back to length 256 at the end of the frame,
          // putting back nearly all of a direct change to front.z. A body axis
          // rotation is well defined at any attitude, and one step is enough
          // to tip the nose off the vertical.
          vec_transform3(flight_alpha16 > 0 ? &kVecPitchDown : &kVecPitchUp,
                         &flight_cam);
        } else if (nose_down) {
          flight_cam.front.z -= brk;
        } else {
          flight_cam.front.z += brk;
        }
        if (flight_cam.front.z > 256) {
          flight_cam.front.z = 256;
        } else if (flight_cam.front.z < -256) {
          flight_cam.front.z = -256;
        }
        model_need_normalize = true;
      }
    } else {
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
      flight_gamma = 0;
      flight_vspeed = 0;
    } else {
      flight_vspeed = vec_fastmul8p8(flight_gamma_z(), flight_speed);
    }

#else // !__FLIGHT_AOA__

    // Speed: Air resistance, gravity, throttle
    uint16_t speed_sqr = vec_fastsqr8p8(flight_speed);
    flight_speed -= _flight_step_u(speed_sqr) >> 10;
    if (flight_gear) {
      flight_speed -= _flight_step_u(speed_sqr) >> 12;
    }
    if (flight_flap) {
      flight_speed -= _flight_step_u(speed_sqr) >> 12;
    }
    if (!model_on_ground) {
      flight_speed -= _flight_step_u(vec_fastsqr8p8(flight_cam.left.z)) >> 5;
    }
    flight_speed -= _flight_step_s(flight_cam.front.z) >> 3;
    if (flight_fuel > 0) {
      // vec_fastmul8p8 rather than a general 16x16 multiply: density is
      // already 8.8 with 256 meaning "sea level", which is exactly the
      // convention this routine expects.
      flight_speed += _flight_step_s(vec_fastmul8p8(flight_throttle, density));
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
      int16_t deficit = kFlightTrimLift - lift;
      if (deficit > 0) {
        sink_penalty = deficit >> 4;
        flight_speed -= _flight_step_s(deficit) >> 10;
      }

      uint16_t base_stall_speed;
      if (flight_flap) {
        base_stall_speed =
            (flight_cam.up.z >= 0) ? kFlightStallSpeedWithFlaps : 0x0480;
      } else {
        base_stall_speed = kFlightStallSpeedWithoutFlaps;
      }
      uint16_t stall_speed = base_stall_speed + ((uint16_t)alt_penalty << 1);

      if (flight_speed < 0) {
        flight_speed = 0;
      }
      flight_stall = flight_speed < stall_speed;
      if (flight_stall) {
        if (flight_cam.front.z > kFlightMaxStallPitchZ) {
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
          uint8_t s = _flight_step_u(stall_speed - flight_speed) >> 5;
          if (s == 0)
            s = whole_step ? 1 : 0;
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
      if (whole_step && flight_throttle == 0 && flight_speed > 0) {
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

#endif // __FLIGHT_AOA__

    // Motion
#ifdef __FLIGHT_AOA__
    const bool hit_floor =
        _flight_move_forward(flight_speed << 1, flight_vspeed);
#else
    _flight_move_forward(flight_speed << 1, flight_vspeed);
#endif

    if (!model_on_ground && flight_vspeed < 0 && flight_eye_z <= 0x4000) {
      // The advisory does not test the runway, which is why check_runway is
      // false here while the touchdown verdict below still passes true.
      //
      // This block runs on any descent below 125 ft anywhere in the world, so
      // "NOT ON RUNWAY" fired on every low pass rather than on approaches
      // that had drifted off the strip - including the low passes the
      // missions themselves ask for, where it warned the pilot against the
      // altitude the briefing had just demanded. Being first in the fault
      // order it also masked the warnings that were worth having: a gear-up
      // approach away from the field reported the runway, not the gear.
      // Landing off a runway is still a crash, and still says so.
      enum FlightStatus fault =
          _flight_landing_fault(kFlightMaxLandingSpeed, false);
      if (fault) {
        msg_show(flight_status_text(fault, false));
      }
    }

#ifdef __FLIGHT_AOA__
    // Already rolling, descending, or stopped by the floor.
    //
    // A position-only test called every step of an unstick a touchdown: the
    // aircraft sits at exactly ground level for as long as its flight path
    // takes to build, and _flight_move_forward clamps it there. That is one
    // step on a stock C64 and four on a SuperCPU where every rate is
    // quartered, so exempting only the step the wheels left on worked at
    // flight_step_shift 0 and bounced the aeroplane down the runway at shift
    // 2 - fourteen touchdowns in forty frames.
    //
    // hit_floor is the third arm because the clamp hides the case it covers:
    // an aircraft that was below the plane and climbing gets moved back to it
    // and is descending by neither test, and without this would fly along
    // inside the ground for ever.
    if (flight_eye_z <= kFlightMinEyeZ &&
        (model_on_ground || flight_vspeed < 0 || hit_floor)) {
#else
    if (flight_eye_z <= kFlightMinEyeZ) {
#endif
      // model_on_ground still holds last frame's value here, so it says
      // whether this is a touchdown or another frame of an existing ground
      // roll.
      bool was_on_ground = model_on_ground;
      uint16_t speed_limit =
          was_on_ground ? kFlightMaxGroundSpeed : kFlightMaxLandingSpeed;
      // Only a fault is written back. A clean touchdown leaves the status
      // alone rather than clearing it to FLIGHT_ONGOING, which would erase a
      // FLIGHT_MISSION_COMPLETED the pilot has already earned.
      enum FlightStatus touchdown_fault =
          _flight_landing_fault(speed_limit, !was_on_ground);
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
      // above, which needs the sink rate the aircraft arrived with, and the
      // flight path with it - on the ground the runway *is* the flight path,
      // which is what makes alpha the nose attitude there.
      flight_vspeed = 0;
#ifdef __FLIGHT_AOA__
      flight_gamma = 0;
#endif

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
    uint8_t fuel_consumption = whole_step ? flight_throttle : 0;
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

#ifdef __FLIGHT_AOA__

    // Rotation (only when airborne)
    //
    // The horizontal component of lift over momentum: the same equation as the
    // flight path above, with the same 1/V, applied to the other component of
    // the same force. Pull harder and the turn tightens; fly the same bank
    // slower and it tightens too.
    //
    // The scale is not free. A level turn's rate is g tan(bank) / V, and
    // matching that fixes the shift at seven: one unit of `rot` is 1/256 of a
    // radian a step, the weight is kFlightTrimLift, and gravity is 32 speed
    // units a step per radian, so rot = 2 * L_horizontal / V.
    //
    // What this replaces was `rot = left.z >> 5` - about 1.6 times this at
    // cruise, and flat in airspeed, which is flight_review.md B4. It also
    // replaces the bank drag term that used to sit in the speed equation:
    // induced drag is C_L^2 and a banked turn needs more C_L, so the turn
    // pays for itself now.
    if (!model_on_ground) {
      int16_t h = vec_fastmul8p8(lift, flight_cam.left.z);
      model_turn_rem +=
          _flight_step_s(vec_fastmul8p8(h, _flight_recip_v(flight_speed)));
      int16_t rot = model_turn_rem >> 7;
      // A guard on the small angle step, not a flight limit. The old turn
      // could not exceed 8 by construction; this one is a real rate and a slow
      // steep turn genuinely exceeds it, so clamping there would cap exactly
      // the case worth getting right. 24 is 5.4 degrees a step, where
      // vec_turn3_xy's linearization is still worth 0.4%.
      if (rot > 24) {
        rot = 24;
      } else if (rot < -24) {
        rot = -24;
      }
      model_turn_rem -= (int16_t)(rot << 7);
      if (rot != 0) {
        // Was a general 3x3 against an identity carrying only front.y = rot
        // and left.x = -rot; vec_turn3_xy() is that matrix written out, bit
        // for bit the same answer for a fifth of the multiplies.
        vec_turn3_xy(rot, &flight_cam);
        model_need_normalize = true;
      }
    } else {
      model_turn_rem = 0;
    }
#else // !__FLIGHT_AOA__

    // Rotation (only when airborne)
    if (!model_on_ground) {
      int8_t rot = _flight_step_s(flight_cam.left.z) >> 5;
      if (rot != 0) {
        // Was a general 3x3 against an identity carrying only front.y = rot
        // and left.x = -rot; vec_turn3_xy() is that matrix written out, bit
        // for bit the same answer for a fifth of the multiplies. |rot| <= 8,
        // since left.z is a unit-vector component and the shift is by five.
        vec_turn3_xy(rot, &flight_cam);
        model_need_normalize = true;
      }
    }
#endif // __FLIGHT_AOA__

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
#ifdef __FLIGHT_AOA__
    flight_vspeed = vec_fastmul8p8(flight_gamma_z(), flight_speed);
#else
    flight_vspeed = vec_fastmul8p8(flight_cam.front.z, flight_speed);
#endif
  }

  if (model_need_normalize) {
    vec_orthonormalize(&flight_cam);
    model_need_normalize = false;
  }

  _flight_update_nav();
  if (whole_step) {
    _flight_path_sample();
  }
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
  model_substep = (model_substep + 1) & kFlightSubstepMask;
}

// The airborne attitude steps, indexed by input - FLIGHT_INPUT_ROLL_LEFT.
static mat3_t *const kFlightRotations[] = {
    &kVecRollLeft, &kVecRollRight, &kVecPitchUp,
    &kVecPitchDown, &kVecYawLeft, &kVecYawRight,
};
static_assert(FLIGHT_INPUT_YAW_RIGHT - FLIGHT_INPUT_ROLL_LEFT + 1 ==
                  sizeof(kFlightRotations) / sizeof(kFlightRotations[0]),
              "kFlightRotations must cover the contiguous rotation inputs");

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
  // N is not listed here because it never reaches this point: the navpoint
  // toggle returns above, ahead of even the crash guard. It selects which
  // waypoint the compass points at, which is map-reading rather than flying.
  //
  // The rule lives here rather than in sim.cc's key handling so that it cannot
  // be bypassed by a future call site, and so flight_test can assert it.
  if (flight_paused) {
    return;
  }

  if (flight_crashed()) {
    return;
  }

  // Actions that mean the same thing on the ground and in the air. They used
  // to be a copy each in the two switches below, and the copies were the bulk
  // of what those switches had in common.
  switch (input) {
  case FLIGHT_INPUT_THROTTLE_UP:
    if (flight_throttle < kMaxThrottle) {
      flight_throttle += 1;
    }
    return;
  case FLIGHT_INPUT_THROTTLE_DOWN:
    if (flight_throttle > kFlightMinThrottle) {
      flight_throttle -= 1;
    }
    return;
  case FLIGHT_INPUT_TOGGLE_FLAP:
    flight_flap = 1 - flight_flap;
    model_pending_events |= FLIGHT_EV_FLAP;
    return;
  default:
    break;
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
#ifdef __FLIGHT_AOA__
    case FLIGHT_INPUT_PITCH_UP:
      // One step of rotation, up to the tail strike limit. What used to be
      // here was a gate - "above the stall speed, jump the nose to
      // kFlightRotatePitchZ and declare the aircraft airborne" - and it is
      // gone along with the constant. Rotating below flying speed now simply
      // holds the nose up until the wing catches up, which is what it does on
      // a real runway, and flight_advance() decides the liftoff by asking
      // whether the wing carries the aeroplane.
      if (flight_cam.front.z < kFlightMaxGroundPitch) {
        vec_transform3(&kVecPitchUp, &flight_cam);
        if (flight_cam.front.z > kFlightMaxGroundPitch) {
          flight_cam.front.z = kFlightMaxGroundPitch;
        }
        model_need_normalize = true;
      }
      break;
#else // !__FLIGHT_AOA__
    case FLIGHT_INPUT_PITCH_UP: {
      uint16_t stall_speed = flight_flap ? kFlightStallSpeedWithFlaps
                                         : kFlightStallSpeedWithoutFlaps;
      if (flight_speed > stall_speed) {
        // Rotation: one action that sets the takeoff attitude, not a pitch step
        // the pilot has to repeat. Once the wheels are off, the airborne branch
        // below owns the pitch again and this cannot fire a second time.
        uint8_t steps = kFlightMaxRotateSteps;
        while (flight_cam.front.z < kFlightRotatePitchZ && steps--) {
          vec_transform3(&kVecPitchUp, &flight_cam);
        }
        model_need_normalize = true;
        model_on_ground = false;
      }
      break;
    }

#endif // __FLIGHT_AOA__
    case FLIGHT_INPUT_TOGGLE_GEAR:
      if (!flight_gear) {
        flight_gear = 1;
        model_pending_events |= FLIGHT_EV_GEAR;
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

#ifdef __FLIGHT_AOA__
  // The elevator will not drive the wing deeper into a stall.
  //
  // This is a control law and not physics, and it is here because the pilot
  // has no stick force to feel. One keypress is 3.6 degrees of pitch; the
  // stall break pushing back is a quarter of that at the angles just past the
  // break, so a held key wins and keeps winning, and the aircraft ends up in a
  // deep stall the pilot never felt themselves entering. Holding the stick
  // back through a rotation - the most ordinary thing a player does - stalled
  // the aeroplane at nought feet and crashed it.
  //
  // Note what it does *not* do. The input that carries the wing from just
  // under the break to just past it is allowed, so the stall is still
  // reachable and the accelerated stall of flight.md 2.2 still fires: what is
  // refused is only the next one after that. And nothing here stops the flight
  // path from stalling the wing on its own, which is how a stall arrives in a
  // banked turn or a pull-up. The pilot cannot bury it; the aeroplane can.
  if (input == FLIGHT_INPUT_PITCH_UP && flight_stall && flight_alpha16 > 0) {
    return;
  }
  if (input == FLIGHT_INPUT_PITCH_DOWN && flight_stall && flight_alpha16 < 0) {
    return;
  }

#endif // __FLIGHT_AOA__

  // Airborne, every axis is the same action with a different matrix, so the
  // six switch arms are one indexed call. kFlightRotations is in the enum's
  // order and the static_assert below is what keeps it that way.
  if (input >= FLIGHT_INPUT_ROLL_LEFT && input <= FLIGHT_INPUT_YAW_RIGHT) {
    vec_transform3(kFlightRotations[input - FLIGHT_INPUT_ROLL_LEFT],
                   &flight_cam);
    model_need_normalize = true;
    return;
  }
  if (input == FLIGHT_INPUT_TOGGLE_GEAR) {
    flight_gear = 1 - flight_gear;
    model_pending_events |= FLIGHT_EV_GEAR;
  }
}
