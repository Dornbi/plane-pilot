#ifndef FLIGHT_ASM_H
#define FLIGHT_ASM_H

#include <stdint.h>

#include "bool.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "flight.h"
#else
static const uint16_t kMaxSpeed = 0x0F00;
extern uint8_t flight_step_shift;
extern bool model_on_ground;
extern mat3_t flight_cam;
extern int16_t flight_speed;
extern int16_t flight_vspeed;
extern uint8_t flight_throttle;
extern uint32_t flight_fuel;
extern uint8_t flight_flap;
extern uint8_t flight_gear;
extern uint8_t flight_stall;
extern int32_t flight_eye_x;
extern int32_t flight_eye_y;
extern int32_t flight_eye_z;
#endif

// The assembly half of flight.cc's arcade flight model (!__FLIGHT_AOA__):
// per-frame speed/drag/lift/stall/ground-attitude/motion integration
// (_flight_physics_move_asm), 32-bit fuel burn (_flight_fuel_step_asm), and
// airborne turn rate (_flight_turn_rot_asm), all in flight_asm.cc under
// __OSCAR64__. flight.cc holds the pure C equivalents (#ifndef __OSCAR64__)
// for host builds and tests.

// Constants pinned by static_assert in flight.cc so flight_asm.cc cannot drift.
static const uint32_t kFlightAsmMinEyeZ = 0x2000;
static const int16_t kFlightAsmTrimLift = 0x1000;
static const uint16_t kFlightAsmStallWithoutFlaps = 0x0400;
static const uint16_t kFlightAsmStallWithFlaps = 0x0340;
static const int16_t kFlightAsmMaxStallPitchZ = 224;

// Defined in flight.cc (or target_test.cc under __TARGET_TEST__).
extern bool model_need_normalize;

// Runs the arcade model's altitude density, drag, gravity, throttle, lift,
// stall break, vertical speed / ground attitude constraints, and 24.8 eye
// integration with floor clamp. Returns non-zero iff flight_eye_z <=
// kFlightAsmMinEyeZ (clamped to kFlightAsmMinEyeZ).
uint8_t _flight_physics_move_asm(uint8_t whole_step);

// Subtracts fuel_consumption from 32-bit flight_fuel, clamping to 0 and
// cutting flight_throttle on exhaustion. Returns non-zero iff flight_fuel
// transitioned from non-zero to 0 on this step (caller shows "OUT OF FUEL").
uint8_t _flight_fuel_step_asm(uint8_t fuel_consumption);

// Returns the airborne turn step `_flight_step_s(flight_cam.left.z) >> 5`,
// or 0 while on the ground.
int8_t _flight_turn_rot_asm(void);

#ifdef __OSCAR64__
#pragma compile("flight_asm.cc")
#endif

#endif
