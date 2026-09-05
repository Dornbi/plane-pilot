// Prototype AoA flight model. See aoa.h for what it is and why, and
// docs/flight_aoa.md for the measurements.
//
// Deliberately *not* a copy of flight.cc with a term added. Everything that
// only exists because the shipping model has no flight path is left out, so
// that what remains is the aerodynamics and nothing else:
//
//   - no kFlightRotatePitchZ. The rotation makes lift here, so the takeoff is
//     an ordinary consequence of C_L(alpha) and the gate can go.
//   - no `speed -= left.z^2 >> 5` bank drag term. Induced drag is C_L^2, and a
//     banked turn needs more C_L, so the turn pays for itself.
//   - no one-sided lift deficit. Lift above weight pushes the flight path up,
//     the same way lift below it lets the path fall.
//   - no stall *speed*. The stall is an angle; the speeds fall out of it.
//
// What it does keep from flight.cc, so the numbers stay comparable: the same
// airspeed scale and clamp, the same weight (kAoaTrimLift), the same parasite
// drag and gear/flap drag shifts, the same thrust-per-throttle-unit, the same
// altitude density decay, the same ground plane and the same wing-levelling
// rule on the runway.

#include "aoa.h"

#include "../fmath.h"

mat3_t aoa_cam;
int16_t aoa_speed;
int16_t aoa_vspeed;
int16_t aoa_gamma;
uint8_t aoa_throttle;
uint8_t aoa_flap;
uint8_t aoa_gear;
int32_t aoa_eye_x;
int32_t aoa_eye_y;
int32_t aoa_eye_z;
uint8_t aoa_stall;
bool aoa_on_ground;
int16_t aoa_alpha16;
int16_t aoa_lift;
int16_t aoa_lift_z;

int16_t kAoaAlphaStall = 64;
int16_t kAoaFlapDeltaCl = 512;
int16_t kAoaCamberCl = 0;
uint8_t kAoaInducedShift = 4;
uint8_t kAoaForceShift = 0;
int16_t kAoaMaxGroundPitch = 64;

static bool aoa_need_normalize;

// The part of a step's speed change that was finer than one unit, carried to
// the next step. Every longitudinal force below is summed at eight times the
// resolution of flight_speed and divided once at the end, so a drag that is
// worth two thirds of a unit a step really does cost two units every three
// steps instead of being truncated to nothing. flight.cc truncates each of its
// five terms separately and keeps no remainder, which is affordable there
// because none of its terms is ever small; here the induced term at high speed
// and every term at kAoaForceShift > 0 are.
static int16_t aoa_dv_rem;
// The same trick for the turn. Without it a gentle bank does not turn at all:
// the rate at 15 degrees and cruise works out under one unit a step, and one
// unit a step is the smallest turn vec_turn3_xy can be asked for.
static int16_t aoa_turn_rem;

// Saturation on the lift product, before the two bits are shifted back on.
// 4096 << 2 is four times kAoaTrimLift, so the wing tops out at 4 g.
//
// This is a range guard first and a load limit second. The 6510's int is 16
// bits (test/int16.h), and C_L * V^2 does not fit it at the top of the speed
// range: at kAoaMaxSpeed with the flaps out the honest product is 57,600, which
// wraps. Saturating is both the cheap fix and the physical one - a wing that
// could pull twelve g would be a wing that tears off.
static const int16_t kAoaLiftSat = 4096;

// 65536 / V, indexed by V >> 8. The 1/V in "flight path curvature is force
// over momentum" and in "turn rate is horizontal lift over momentum". A table
// because flight.md forbids runtime division, and sixteen entries because that
// is the whole speed range at a resolution the model cannot feel.
//
// Entry 0 is a clamp, not a value: 65536/128 is 512, which would overflow the
// products below, and under V = 256 the aircraft is falling rather than flying
// so the exact rate is not something anyone can see.
static const int16_t kAoaRecipV[16] = {
    256, 170, 102, 73, 56, 46, 39, 34, 30, 26, 24, 22, 20, 18, 17, 16,
};

static inline int16_t _aoa_recip_v(int16_t speed) {
  uint8_t i = (uint8_t)((uint16_t)speed >> 8);
  if (i > 15) {
    i = 15;
  }
  return kAoaRecipV[i];
}

// The lift curve. Below the stall it is not a curve at all but the identity:
// C_L in 8.8 and alpha16 are the same number, because the lift slope was
// chosen to make them so. There is no table, no index arithmetic, no
// interpolation, and - the point of the exercise - no quantization step
// between one attainable C_L and the next beyond the one alpha16 already has.
//
// Past the peak the wing droops rather than holding: pull harder there and you
// get *less* lift, so the flight path falls away faster than the nose does and
// alpha runs away. That is what makes a stall a stall rather than a ceiling,
// and the break in aoa_advance() is the pitching moment that ends it. Five
// eighths is the droop measured off a real C_L curve either side of the break,
// and 2048 (alpha 128, 30 degrees) is where it is called flat.
static const int16_t kAoaMaxAlpha16 = 2048;

int16_t aoa_cl16(int16_t alpha16) {
  int16_t a = alpha16 < 0 ? -alpha16 : alpha16;
  const int16_t peak = aoa_cl_peak();
  int16_t cl;
  if (a <= peak) {
    cl = a;
  } else {
    if (a > kAoaMaxAlpha16) {
      a = kAoaMaxAlpha16;
    }
    cl = peak - (((a - peak) * 5) >> 3);
  }
  return alpha16 < 0 ? -cl : cl;
}

uint16_t aoa_speed_for_cl(int16_t cl) {
  // The lift chain above is lift = V^2 * C_L / 262144, so the airspeed at
  // which a given C_L carries the aeroplane is sqrt(kAoaTrimLift * 262144 /
  // C_L) - which for kAoaTrimLift = 0x1000 is sqrt(2^30 / C_L).
  //
  // Host side only: this is the harness asking the model what its stall speed
  // works out at, not something the model needs to know about itself. That is
  // the point of the exercise - flight.cc has to be *told* 0x0400.
  if (cl <= 0) {
    return 0;
  }
  uint32_t n = (1u << 30) / (uint32_t)cl;
  uint32_t root = 0;
  uint32_t bit = 1u << 30;
  while (bit > n) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (n >= root + bit) {
      n -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return (uint16_t)root;
}

static const mat3_t _aoa_init_cam = {
    {256, 0, 0},
    {0, 256, 0},
    {0, 0, 256},
};

void aoa_init(void) {
  aoa_cam = _aoa_init_cam;
  aoa_speed = 0x0860;
  aoa_vspeed = 0;
  aoa_gamma = 0;
  aoa_throttle = 0x14;
  aoa_flap = 0;
  aoa_gear = 0;
  aoa_eye_x = 0x200000;
  aoa_eye_y = 0x400000;
  aoa_eye_z = 0x010000;
  aoa_stall = 0;
  aoa_on_ground = false;
  aoa_alpha16 = 0;
  aoa_lift = 0;
  aoa_lift_z = 0;
  aoa_need_normalize = false;
  aoa_dv_rem = 0;
  aoa_turn_rem = 0;
}

// Same as flight.cc's _flight_move_forward: the horizontal step is along the
// nose's ground track at full airspeed. Strictly it should be cos(gamma) of
// it, but that is a change to the world, not to the aerodynamics, and leaving
// it alone keeps the ground speeds of the two models comparable.
static void _aoa_move_forward(int16_t fspeed, int16_t vspeed) {
  aoa_eye_x += (int32_t)vec_fastmul8p8(aoa_cam.front.x, fspeed);
  aoa_eye_y += (int32_t)vec_fastmul8p8(aoa_cam.front.y, fspeed);
  aoa_eye_z += (int32_t)vspeed;
}

void aoa_advance(void) {
  uint8_t alt_penalty = 0;
  if (aoa_eye_z > 0x080000) {
    uint32_t alt_diff = (uint32_t)(aoa_eye_z - 0x080000) >> 12;
    alt_penalty = (alt_diff > 128) ? 128 : (uint8_t)alt_diff;
  }
  int16_t density = 256 - alt_penalty;

  uint16_t speed_sqr = vec_fastsqr8p8(aoa_speed);

  // --- The wing ------------------------------------------------------------
  //
  // alpha is the angle between where the nose points and where the aircraft is
  // going: front.z is sin(pitch), aoa_gamma_z() is sin(flight path), and their
  // difference is sin(alpha) to first order. On the ground the flight path is
  // the runway, so alpha is simply the nose attitude - which is why rotating
  // is what gets the aircraft off it.
  //
  // The sign flip is not a fudge. Angle of attack is a *body* angle - which
  // side of the wing the air arrives from - and rolling inverted swaps those
  // sides. Upside down, a nose held above the flight path in world terms is
  // air arriving on the canopy side, which is negative alpha and negative
  // lift. Everything flight.md 3.2 says about inverted flight ("the nose must
  // be pitched UP relative to the horizon") is this line and nothing else.
  //
  // Both sides are carried at aoa_gamma's scale rather than front.z's, so the
  // difference has sixteen times the resolution of a direction cosine. See
  // aoa_alpha16 in aoa.h for why that matters: at the coarse scale the
  // smallest change in alpha is a sixteenth of a g of lift, and a steady state
  // finer than that cannot exist.
  int16_t d = ((int16_t)(aoa_cam.front.z << 4)) - aoa_gamma;
  aoa_alpha16 = aoa_cam.up.z >= 0 ? d : -d;

  int16_t cl = aoa_cl16(aoa_alpha16) + kAoaCamberCl;
  if (aoa_flap) {
    // A camber shift, not a multiplier. Upright this adds to a positive C_L
    // and buys a lower stall speed; inverted the attitude needs a negative
    // C_L and the same offset fights it, so the inverted stall speed rises.
    // flight.cc needs an explicit `up.z < 0` stall constant to get this; here
    // it is one addition with no sign test in it.
    cl += kAoaFlapDeltaCl;
  }

  int16_t t = vec_fastmul8p8((int16_t)(speed_sqr >> 4), cl);
  if (t > kAoaLiftSat) {
    t = kAoaLiftSat;
  } else if (t < -kAoaLiftSat) {
    t = -kAoaLiftSat;
  }
  aoa_lift = vec_fastmul8p8((int16_t)(t << 2), density);
  aoa_lift_z = vec_fastmul8p8(aoa_lift, aoa_cam.up.z);

  // Liftoff, and this is the whole of it: the wing carries the aeroplane, so
  // it flies. No speed gate, no rotation target, no kFlightRotatePitchZ - the
  // pilot holds an attitude and the aircraft leaves the ground at whatever
  // speed that attitude needs.
  //
  // It has to happen here, ahead of the flight path integration below, and
  // that is not a tidiness point. Decided after it, the first airborne step
  // would be one with a flight path still pinned flat by the ground branch: no
  // climb, no rise in altitude, and the ground contact check at the bottom
  // would put the wheels straight back down. That is exactly the once-a-frame
  // touchdown cycle flight.md 5.2 describes, and it is not a thing an AoA
  // model has to live with - deciding here means the step that lifts off is
  // also the step that integrates a positive net force into a climb.
  if (aoa_on_ground && aoa_lift_z > kAoaTrimLift) {
    aoa_on_ground = false;
  }

  // --- Drag, thrust and gravity ---------------------------------------------
  //
  // Summed at eight times flight_speed's resolution and divided once, with the
  // remainder carried (aoa_dv_rem). Parasite is flight.cc's own coefficient and
  // so are the gear and flap additions.
  //
  // Induced is C_L^2 * V^2, and it is the term flight.cc spends three separate
  // hacks standing in for: the lift deficit's `deficit >> 10`, the bank term
  // `left.z^2 >> 5`, and the implicit inverted penalty. One coefficient covers
  // all three, because all three are the same thing - the wing being asked for
  // lift it has to work for.
  int16_t dv8 = 0;
  dv8 -= (int16_t)(speed_sqr >> 7);
  if (aoa_gear) {
    dv8 -= (int16_t)(speed_sqr >> 9);
  }
  if (aoa_flap) {
    dv8 -= (int16_t)(speed_sqr >> 9);
  }
  if (!aoa_on_ground) {
    // C_L past the peak is capped for the drag term. Induced drag goes as the
    // square of the lift actually made, and past the stall the wing is not
    // making more of it - what rises there is separation drag, which this
    // stands in for rather than pretends to model. It also keeps the product
    // inside sixteen bits, which the honest one does not at the top of the
    // speed range.
    int16_t cl_d = cl;
    const int16_t peak = aoa_cl_peak();
    if (cl_d > peak) {
      cl_d = peak;
    } else if (cl_d < -peak) {
      cl_d = -peak;
    }
    int16_t cl_sqr = vec_fastmul8p8(cl_d, cl_d);
    dv8 -= vec_fastmul8p8((int16_t)(speed_sqr >> 5), cl_sqr) >>
           kAoaInducedShift;
  } else if (aoa_throttle == 0 && aoa_speed > 0) {
    // Wheel friction, flight.cc's flat 2 units a step at eight times the
    // resolution. A force like the others, so it scales with them.
    dv8 -= 16;
  }
  // No fuel state in the prototype: this is a model of the aerodynamics, and
  // running the tank dry is flight.cc's `throttle = 0` either way.
  dv8 += vec_fastmul8p8((int16_t)(aoa_throttle << 3), density);

  // Gravity along the *flight path*, not along the nose. flight.cc uses
  // front.z here because it has nothing else, and the difference is exactly
  // the aeroplane that is pointing up while sinking - which is the attitude
  // every approach and every stall entry is flown at.
  //
  // Pre-scaled by kAoaForceShift so that the divide below leaves it at
  // flight.cc's `>> 3` whatever that shift is: weight is the fixed thing that
  // thrust and drag are being measured against, so it is the one force that
  // must not move with them.
  dv8 -= (int16_t)(aoa_gamma_z() << kAoaForceShift);

  const uint8_t dv_shift = 3 + kAoaForceShift;
  dv8 += aoa_dv_rem;
  const int16_t dv = dv8 >> dv_shift;
  aoa_dv_rem = (int16_t)(dv8 - (int16_t)(dv << dv_shift));
  aoa_speed += dv;

  if (aoa_speed < 0) {
    aoa_speed = 0;
  } else if (aoa_speed > (int16_t)kAoaMaxSpeed) {
    aoa_speed = (int16_t)kAoaMaxSpeed;
  }

  if (!aoa_on_ground) {
    // --- The flight path ---------------------------------------------------
    //
    // Net vertical force over momentum. This is the whole of the model's
    // vertical behaviour: sink, climb, the altitude a banked turn loses, and
    // the balloon when the flaps come out are all this one line seeing a
    // different `net`.
    int16_t net = aoa_lift_z - kAoaTrimLift;
    aoa_gamma += vec_fastmul8p8(net, _aoa_recip_v(aoa_speed)) >> 3;
    if (aoa_gamma > 4096) {
      aoa_gamma = 4096;
    } else if (aoa_gamma < -4096) {
      aoa_gamma = -4096;
    }
  }

  // --- The stall -----------------------------------------------------------
  //
  // An angle, not a speed. It therefore also fires in the two cases the
  // shipping model cannot see at all: the accelerated stall (pulling hard well
  // above V_stall) and the inverted stall (pushing hard the other way).
  aoa_stall = 0;
  if (!aoa_on_ground) {
    int16_t excess = (int16_t)((aoa_alpha16 < 0 ? -aoa_alpha16 : aoa_alpha16) -
                               aoa_cl_peak());
    if (excess > 0) {
      aoa_stall = 1;
      int16_t s = excess >> 4;
      if (s == 0) {
        s = 1;
      }
      // The break drives the nose back toward the flight path, which is what a
      // stalled wing's pitching moment does. It needs no "toward the ground"
      // special case: at low speed the flight path is already steeply down, so
      // chasing it *is* the nose drop, at any attitude and either way up.
      //
      // Which way front.z has to move is the sign of `d`, not of alpha - the
      // two differ when inverted, for the reason the alpha line above gives.
      // Which body rotation to use in the dead spot is the sign of alpha and
      // does not: a pitch-down step moves front.z by -up.z/16, so the up.z in
      // it cancels the up.z in `d`.
      const bool nose_down = d > 0;
      if (nose_down ? aoa_cam.front.z > kAoaMaxStallPitchZ
                    : aoa_cam.front.z < -kAoaMaxStallPitchZ) {
        // Dead spot, same as flight.cc: with the nose this high (or this low)
        // a direct change to front.z is undone by the renormalization below,
        // so the break has to be a body rotation.
        vec_transform3(aoa_alpha16 > 0 ? &kVecPitchDown : &kVecPitchUp,
                       &aoa_cam);
      } else if (nose_down) {
        aoa_cam.front.z -= s;
      } else {
        aoa_cam.front.z += s;
      }
      if (aoa_cam.front.z > 256) {
        aoa_cam.front.z = 256;
      } else if (aoa_cam.front.z < -256) {
        aoa_cam.front.z = -256;
      }
      aoa_need_normalize = true;
    }
  }

  // --- Ground --------------------------------------------------------------
  if (aoa_on_ground) {
    aoa_stall = 0;
    aoa_gamma = 0;
    aoa_vspeed = 0;

    if (aoa_cam.front.z < 0) {
      aoa_cam.front.z = 0;
      aoa_need_normalize = true;
    }
    if (aoa_cam.left.z != 0) {
      aoa_cam.left.x = -aoa_cam.front.y;
      aoa_cam.left.y = aoa_cam.front.x;
      aoa_cam.left.z = 0;
      vec_cross(&aoa_cam.front, &aoa_cam.left, &aoa_cam.up);
      aoa_need_normalize = true;
    }
  } else {
    aoa_vspeed = vec_fastmul8p8(aoa_gamma_z(), aoa_speed);
  }

  _aoa_move_forward((int16_t)(aoa_speed << 1), aoa_vspeed);

  if (aoa_eye_z <= kAoaMinEyeZ) {
    aoa_eye_z = kAoaMinEyeZ;
    // Descending, not merely low. An aircraft that has just unstuck sits at
    // exactly ground level for the step or two its flight path takes to build,
    // and a position-only test would call each of those steps a touchdown.
    if (!aoa_on_ground && aoa_vspeed < 0) {
      aoa_on_ground = true;
      aoa_gamma = 0;
      aoa_vspeed = 0;
      if (aoa_cam.front.z != 0) {
        aoa_cam.front.z = 0;
        aoa_need_normalize = true;
      }
    }
  }

  // --- The turn ------------------------------------------------------------
  //
  // The horizontal component of lift, over momentum: pull harder and the turn
  // tightens, fly slower at the same bank and it tightens too. flight.cc turns
  // at `left.z >> 5` whatever the wing is doing, which is why its turn rate
  // does not depend on airspeed (flight_review B4). Calibrated so that a 72
  // degree bank at trim lift and airspeed 0x0800 gives the same 7 units a step
  // that the shipping model gives - the everyday turn is unchanged, and what
  // moves is what happens either side of it.
  if (!aoa_on_ground) {
    // Horizontal lift over momentum, in the same units and with the same
    // constant as the flight path integration above - it is the same equation
    // with the same 1/V, applied to the other component of the same force.
    //
    // The scale is not free. A level turn's rate is g tan(bank) / V, and
    // matching that fixes the shift at seven: one unit of `rot` is 1/256 of a
    // radian a step, weight is 4096, and gravity is 32 speed units a step per
    // radian, so rot = 2 * L_horizontal / V. flight.cc's left.z >> 5 is about
    // 1.6 times that at cruise and does not fall off with speed at all.
    int16_t h = vec_fastmul8p8(aoa_lift, aoa_cam.left.z);
    aoa_turn_rem += vec_fastmul8p8(h, _aoa_recip_v(aoa_speed));
    int16_t rot = aoa_turn_rem >> 7;
    // A guard on the small-angle step, not a flight limit. flight.cc's turn
    // cannot exceed 8 by construction (left.z >> 5), but this one is a real
    // rate and a slow steep turn genuinely exceeds it - clamping there would
    // cap exactly the case the model exists to get right. 24 is 5.4 degrees a
    // step, where vec_turn3_xy's linearization is still worth 0.4%.
    if (rot > 24) {
      rot = 24;
    } else if (rot < -24) {
      rot = -24;
    }
    aoa_turn_rem -= (int16_t)(rot << 7);
    if (rot != 0) {
      vec_turn3_xy(rot, &aoa_cam);
      aoa_need_normalize = true;
    }
  } else {
    aoa_turn_rem = 0;
  }

  if (aoa_need_normalize) {
    vec_orthonormalize(&aoa_cam);
    aoa_need_normalize = false;
  }
}

static mat3_t *const kAoaRotations[] = {
    &kVecRollLeft, &kVecRollRight, &kVecPitchUp,
    &kVecPitchDown, &kVecYawLeft, &kVecYawRight,
};

void aoa_input(enum aoa_input_t input) {
  if (aoa_on_ground) {
    switch (input) {
    case AOA_INPUT_ROLL_LEFT:
    case AOA_INPUT_YAW_LEFT:
      if (aoa_speed > 0) {
        vec_transform3(&kVecYawLeft, &aoa_cam);
        aoa_need_normalize = true;
      }
      return;
    case AOA_INPUT_ROLL_RIGHT:
    case AOA_INPUT_YAW_RIGHT:
      if (aoa_speed > 0) {
        vec_transform3(&kVecYawRight, &aoa_cam);
        aoa_need_normalize = true;
      }
      return;
    case AOA_INPUT_PITCH_UP:
      // One step, and only up to the tail strike limit. The gate that used to
      // be here - "above stall speed, jump to kFlightRotatePitchZ" - is gone:
      // rotating below flying speed now simply holds the nose up until the
      // wing catches up, which is what it does on a real runway.
      if (aoa_cam.front.z < kAoaMaxGroundPitch) {
        vec_transform3(&kVecPitchUp, &aoa_cam);
        if (aoa_cam.front.z > kAoaMaxGroundPitch) {
          aoa_cam.front.z = kAoaMaxGroundPitch;
        }
        aoa_need_normalize = true;
      }
      return;
    case AOA_INPUT_PITCH_DOWN:
      if (aoa_cam.front.z > 0) {
        vec_transform3(&kVecPitchDown, &aoa_cam);
        if (aoa_cam.front.z < 0) {
          aoa_cam.front.z = 0;
        }
        aoa_need_normalize = true;
      }
      return;
    default:
      return;
    }
  }

  vec_transform3(kAoaRotations[input], &aoa_cam);
  aoa_need_normalize = true;
}
