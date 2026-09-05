#ifndef PROTO_AOA_H
#define PROTO_AOA_H

// Prototype: a flight model with an angle of attack.
//
// This is not in the game build. It exists to answer the open item at the top
// of docs/flight_review.md ("Decide the direction for A - either add an AoA
// term to lift, or keep the speed-and-bank model"), by being the AoA model in
// enough detail to measure. docs/flight_aoa.md is the write-up; aoa_proto.cc
// drives both this and the shipping flight.cc through the same sweeps and
// prints the two answers side by side.
//
// What is different from flight.cc, in one paragraph. The shipping model has
// one attitude and no flight path: airspeed points along the nose, so
// `vspeed = front.z * speed` and lift is f(V^2, bank) with `front.z` nowhere
// in it. Here the flight path is its own state (`aoa_gamma`), lift is
// f(C_L(alpha), V^2, bank) with alpha the difference between them, and the
// pilot's stick sets the nose while the wing sets the path. What follows -
// two-sided lift, a stall that is an angle and not a speed, induced drag that
// covers banked turns without a bank term, a takeoff that needs no
// kFlightRotatePitchZ fudge - falls out of that one change.
//
// Same arithmetic rules as the real model: int16 8.8 fixed point,
// vec_fastmul8p8, shifts, and one sixteen-entry lookup table (the lift curve
// needs none - see aoa_cl16). No floating point and
// no runtime division (vec_div8p8 is not called). Written against
// flight_step_shift == 0, the stock C64 rate of one step per 8 raster frames;
// the substep scaling of flight.md 8 is left out because it multiplies
// through unchanged and would only be noise here.

#include <stdint.h>

#include "../bool.h"
#include "../vec.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

extern mat3_t aoa_cam;

// Airspeed, 8.8, 0 .. kAoaMaxSpeed. Same scale and same clamp as flight_speed,
// so every number this model prints can be read against the shipping one.
static const uint16_t kAoaMaxSpeed = 0x0F00;
extern int16_t aoa_speed;
extern int16_t aoa_vspeed;

// sin(flight path angle), but at 4096 = 1.0 rather than the 256 the direction
// cosines use. The extra four bits are not decoration: the flight path is
// integrated from a force, so a lift imbalance too small to move one unit of
// it is an imbalance the aircraft never feels. At 256 the dead band was 6% of
// weight - a third of a g of permanent, silent error. At 4096 it is 1.5%.
//
// aoa_gamma_z() is the same angle in the 256 scale the vectors use.
extern int16_t aoa_gamma;
inline int16_t aoa_gamma_z(void) { return aoa_gamma >> 4; }

extern uint8_t aoa_throttle;
extern uint8_t aoa_flap;
extern uint8_t aoa_gear;
extern int32_t aoa_eye_x;
extern int32_t aoa_eye_y;
extern int32_t aoa_eye_z;

// Published for the same reason flight_stall is: the panel lamp and the
// warning tone must not be able to disagree with the physics. Unlike
// flight_stall this is an *angle* test, so it fires in the cases the speed
// test cannot see - see aoa_alpha below.
extern uint8_t aoa_stall;
extern bool aoa_on_ground;

// The current angle of attack, at sixteen units to one of front.z: the angle
// between where the nose points and where the aircraft is actually going,
// positive nose-above-path. This is the quantity the shipping model does not
// have, and every other difference follows from it.
//
// The fine scale is not a luxury. At one unit per unit of front.z, one step of
// alpha is 16 of C_L, which at cruise is 6% of the aircraft's weight - so the
// flight path could never settle, it could only hunt across a lift error a
// sixteenth of a g wide. That hunt was visible as a limit cycle in the settled
// vertical speed. Sixteen units to one puts the smallest lift step under 0.4%
// of weight, well inside the dead band of the integrator below, and the
// steady states really are steady.
//
// It also costs nothing, because front.z and aoa_gamma are both already
// carried at this scale or finer: alpha16 is (front.z << 4) - aoa_gamma, one
// shift and one subtraction.
extern int16_t aoa_alpha16;
// The same angle in the sine units the vectors and flight.md use, for display.
inline int16_t aoa_alpha(void) { return aoa_alpha16 >> 4; }

// Last step's lift, in the units of kAoaTrimLift, and its vertical component.
// Exported for the harness only.
extern int16_t aoa_lift;
extern int16_t aoa_lift_z;

// ---------------------------------------------------------------------------
// Tunables
//
// Not const, so aoa_proto.cc can sweep them. On target they would be constants
// and the shifts would fold.
// ---------------------------------------------------------------------------

// Lift needed to hold altitude - weight. Same value flight.cc uses, which is
// what keeps the two models' airspeeds comparable.
static const int16_t kAoaTrimLift = 0x1000;

// Angle of attack at which C_L peaks, in sine units: 64/256 = ~14.5 degrees.
// This is the stall, and it is the whole point of the model. In flight.cc the
// stall is a speed; here it is an angle, and the speed at which the wing
// happens to need that angle is a consequence.
extern int16_t kAoaAlphaStall;

// Peak C_L, 8.8, where 256 means "the coefficient at which flight.cc's lift
// equation trims at 0x0800". It is not a free parameter: the lift slope works
// out at exactly one unit of C_L per unit of alpha16, so the peak is the stall
// angle in those units and nothing else.
//
//   C_L = alpha16 = (front.z << 4) - gamma      (below the stall)
//
// That identity is why there is no lift-curve lookup table in this model, and
// why the stall speed is never written down anywhere in it:
//
//   lift = C_L * V^2 / 4,  so V_stall = 0x0800 / sqrt(1024/256) = 0x0400,
//
// the same 0x0400 flight.cc has to be told outright. Raise kAoaAlphaStall and
// the peak rises with it and the stall speed falls, which is the relationship
// a real wing has and the one two independent constants could not hold.
inline int16_t aoa_cl_peak(void) { return (int16_t)(kAoaAlphaStall << 4); }

// Flaps are a camber shift, so they *add* to C_L rather than scaling it. That
// asymmetry is the whole of the inverted flap penalty in flight.md 4.2, and
// here it needs no inverted special case: upright the offset adds to a
// positive C_L and the stall speed drops, inverted it fights the negative C_L
// the attitude needs and the stall speed rises.
//
// 512 against a 1024 peak is +50% of C_Lmax, which is what a single-slotted
// flap is worth, and puts the flapped stall speed at 0x0344 against the
// 0x0340 flight.cc carries.
extern int16_t kAoaFlapDeltaCl;

// A permanent camber offset, in the same units and by the same mechanism as
// the flaps: C_L at zero alpha. It is the knob that makes inverted flight
// expensive.
//
// At 0 the section is symmetric, and the measured consequence is that inverted
// level flight is *exactly* as cheap as upright - same throttle floor, same
// attitude, same settled speed, alpha simply negated. That is correct for the
// wing it describes and wrong for the aeroplane flight.md 3.2 wants, where
// sustained inverted flight is meant to sit on the edge of the stall. A real
// cambered section carries some of this, and any of it costs the inverted
// pilot twice: less lift where the attitude needs it, and a higher inverted
// stall speed. Default 0 so the tables below isolate one thing at a time.
extern int16_t kAoaCamberCl;

// Induced drag: (C_L^2 * V^2) >> this. The one number that decides the shape
// of the drag curve, and therefore the throttle needed to hold level flight,
// the best glide speed, and where the back side of the power curve starts.
// 4 is a textbook drag polar (best glide at 1.41 V_stall); 5 is a cleaner
// wing, and the number that reproduces flight.md's 46% level-flight floor.
// aoa_proto.cc prints both.
extern uint8_t kAoaInducedShift;

// Thrust and drag are divided by 8 << this; gravity is not. It is therefore
// the aircraft's weight measured against its engine and its airframe, and it
// is the one thing the AoA model cannot inherit from flight.cc unexamined.
//
// Why it has to exist. In flight.cc pitch attitude *is* flight path, so its
// "glide ratio" is read off the nose and never has to be paid for by a force
// balance. Here it is one: the glide angle is drag over weight, full stop. Run
// the numbers on the shipping scale and the aeroplane's thrust is 24 against a
// weight that shows up as 32 units of speed per radian - a thrust-to-weight of
// 0.75, three times a real light aircraft's - and a best glide of about 2:1.
// Both are the same fact, and neither is visible until the model has a real
// flight path to charge them to.
//
// Raising this divides thrust and drag together, so the top speed and the
// throttle *fractions* stay where they are while the glide ratio improves:
// measured, shift 1 takes best glide from 7.7:1 to 12.8:1 at an unchanged
// cruise of 2347.
//
// It does *not* fix TODO.md's "climb rate is too fast", and the measurement is
// worth keeping because the prediction was the opposite. Halving thrust and
// drag halves the climb *angle*, but the aeroplane also settles faster for any
// given angle, and climb rate is the product of the two: shift 1 measured
// +717 against shift 0's +610. Climb rate is owned by the gravity term, which
// this knob deliberately does not touch.
//
// 0 is flight.cc's own scale, and the default so that the tables compare like
// with like; aoa_proto.cc sweeps it.
extern uint8_t kAoaForceShift;

// Nose-up attitude the pilot can reach on the runway before the tail hits.
// It is a *limit*, not a target: unlike kFlightRotatePitchZ the model does not
// drive the nose to a magic attitude, because with a C_L(alpha) curve the
// rotation makes lift on its own.
extern int16_t kAoaMaxGroundPitch;

// Nose attitude past which the stall break has to be a body rotation instead
// of a direct write to front.z. Same reason and same value as flight.cc's
// kFlightMaxStallPitchZ: renormalization puts back nearly all of a direct
// change once the horizontal part of `front` is small.
static const int16_t kAoaMaxStallPitchZ = 224;

// Ground plane, matching kFlightMinEyeZ.
static const int32_t kAoaMinEyeZ = 0x2000;

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

// C_L for an angle of attack, signed, 8.8. Symmetric section: C_L(-a) is
// -C_L(a), which is what makes inverted flight fall out rather than being
// special-cased. Below the stall it is the identity; past it, the droop.
int16_t aoa_cl16(int16_t alpha16);
// The same, for an alpha in front.z's units. Harness convenience.
inline int16_t aoa_cl(int16_t alpha) {
  return aoa_cl16((int16_t)(alpha << 4));
}

// Airborne, level, wings level: the airspeed at which C_L(alpha) trims. Used
// by the harness to state a stall speed the model was never told.
uint16_t aoa_speed_for_cl(int16_t cl);

void aoa_init(void);
void aoa_advance(void);

// Same input set as flight_input, minus everything that is not flying.
enum aoa_input_t {
  AOA_INPUT_ROLL_LEFT,
  AOA_INPUT_ROLL_RIGHT,
  AOA_INPUT_PITCH_UP,
  AOA_INPUT_PITCH_DOWN,
  AOA_INPUT_YAW_LEFT,
  AOA_INPUT_YAW_RIGHT,
};
void aoa_input(enum aoa_input_t input);

// The repo's convention for oscar64: naming the header pulls in the source.
// Here it is what lets size_probe.cc build the model for the 6510, so the byte
// cost of landing it is a measurement and not a guess.
#pragma compile("aoa.cc")

#endif // PROTO_AOA_H
