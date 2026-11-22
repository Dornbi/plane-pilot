#ifndef CLOUDS_ASM_H
#define CLOUDS_ASM_H

#include <stdint.h>

#include "clouddef.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "flight.h"
#include "mem.h"
#include "world.h"
#else
extern int32_t flight_eye_x;
extern int32_t flight_eye_y;
extern int32_t flight_eye_z;
extern mat3_t world_cam;
#endif

// The assembly half of clouds.cc, in clouds_asm.cc: shared per-frame cloud
// scratch state in tscratch (fpilot) / bss2 (ppilot), plus 6502 routines for
// the 5x5 hash-gated cloud scan + world/frustum cull + rung walk, staged
// camera-row dot products via vec_mul8, 8-bit half-basis generation
// (trunc(world_cam / 16)), and group-blob offset application. clouds.cc keeps
// the C counterparts under #ifndef __OSCAR64__ for host builds, and
// test/target_test.cc holds the 6502 assembly to 16-bit C references on the
// emulated 6510.

// Per-frame zeroed flags and scan origin (cleared together in _clouds_scan_init).
struct clouds_zero_t {
  uint8_t basis_valid;
  uint8_t z_valid[3];
  uint8_t ux_hi;
  uint8_t uy_hi;
};

// Camera-row order: [0..2] front.{x,y,z}, [3..5] left.{x,y,z},
// [6..8] up.{x,y,z}. Each entry is trunc(cam / 16) in [-16, 16].
extern int8_t _clouds_half_basis[9];
extern clouds_zero_t _clouds_zero;
#define _clouds_basis_valid (_clouds_zero.basis_valid)
#define _clouds_z_valid (_clouds_zero.z_valid)
extern int16_t _clouds_dz;
extern int16_t _clouds_z[3];
extern vec3_t _clouds_centre;
extern uint8_t _clouds_pat_off;

#ifdef __OSCAR64__
void _clouds_build_basis(void);
int16_t _clouds_dot(uint8_t axis);
int16_t _clouds_apply_offset(void);
void _clouds_scan_init(void);
uint8_t _clouds_scan_next(void);

#pragma compile("clouds_asm.cc")
#endif

#endif
