#ifndef VEC_H
#define VEC_H

#include "bool.h"
#include <stdint.h>

#ifndef __OSCAR64__
#define __zeropage
// Multiplies vec_mul_a and b (16-bit signed integers). Interprets b as 8.8
// fixed point, and returns the middle 16-bits of the 32-bit result. Note:
// optimized for |b| <= 256.
//
// Host stand-in for the 6502 routine in vec_asm.cc. That one forms the
// product from the magnitudes and applies the sign at the end, so it computes
// trunc(a * b / 256), rounding toward zero. A plain arithmetic shift would
// floor toward -infinity instead and differ by one on every negative product
// with a remainder - about half of all products - which would make host test
// results meaningless for the C64.
inline int16_t vec_fastmul8p8(int16_t a, int16_t b) {
  int32_t p = (int32_t)a * b;
  int32_t magnitude = (p < 0 ? -p : p) >> 8;
  return (int16_t)(p < 0 ? -magnitude : magnitude);
}
#else
int16_t vec_fastmul8p8(int16_t a, int16_t b);
#endif

// Squares a 16-bit signed integer and returns a 16-bit unsigned integer.
// Both input and output use 8.8 fixed-point semantics.
uint16_t vec_fastsqr8p8(int16_t a);

// Divides a by b using 8.8 fixed point semantics. Equivalent to (a << 8) / b.
int16_t vec_div8p8(int16_t a, int16_t b);

// 16-bit signed 3D vector.
struct vec3_t {
  int16_t x;
  int16_t y;
  int16_t z;
};

inline vec3_t make_vector(int16_t x, int16_t y, int16_t z) {
  vec3_t v;
  v.x = x;
  v.y = y;
  v.z = z;
  return v;
}

// Rotation matrix represented by 3 orthogonal unit vectors.
// Unit length is 256 (8.8 fixed point).
struct mat3_t {
  vec3_t front;
  vec3_t left;
  vec3_t up;
};

extern vec3_t vec_v;
extern int16_t vec_sx;
extern int16_t vec_sy;

// Addition and subtraction in place.
void vec_add(vec3_t *dst, const vec3_t *src);
void vec_sub(vec3_t *dst, const vec3_t *src);
void vec_negate(vec3_t *v);

// Projects 3d vector to screen coordinates and populates vec_sx and vec_sy.
// x is forward, y is left, z is up.
// @param vec_v: vector to project
// @result vec_sx, vec_sy: screen coordinates
// Returns true on success, false on failure.
bool vec_project();

// Similar to vec_project, but does not cull based on the screen boundaries.
// It will only return false if the point is behind the camera (x <= 8).
bool vec_project_nocull();

// Normalizes vector with 8.8 fixed point arithmetic.
void vec_normalize(vec3_t *v);

// Cross product of u and v, result in vec_res.
// Uses 8.8 fixed point arithmetic.
void vec_cross(const vec3_t *u, const vec3_t *v, vec3_t *res);

// Orthonormalizes mat using 8.8 fixed point arithmetic.
void vec_orthonormalize(mat3_t *mat);

extern const mat3_t kVecRollLeft;
extern const mat3_t kVecRollRight;
extern const mat3_t kVecPitchUp;
extern const mat3_t kVecPitchDown;
extern const mat3_t kVecYawLeft;
extern const mat3_t kVecYawRight;

// Transforms u by vec_mat and stores the result in res.
// Column-major counterpart of vec_transform_inv. ppilot no longer calls it
// (see vec_transform3), so it only ends up in vectest and the host tests.
void vec_transform(const mat3_t *mat, const vec3_t *u, vec3_t *res);
// (Inverted) transforms u by vec_mat and stores the result in res.
void vec_transform_inv(const mat3_t *mat, const vec3_t *u, vec3_t *res);

// Transforms m by t.
void vec_transform3(const mat3_t *t, mat3_t *m);
// (Inverted) transforms m by t.
void vec_transform3_inv(const mat3_t *t, mat3_t *m);

#pragma compile("vec.cc")
#pragma compile("vec_asm.cc")
#pragma compile("vec_lut.cc")

#endif // VEC_H
