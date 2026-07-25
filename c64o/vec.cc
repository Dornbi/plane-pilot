#include "vec.h"
#include <stdint.h>

#ifndef __OSCAR64__
vec3_t vec_v;
int16_t vec_sx;
int16_t vec_sy;
#endif

static inline uint8_t _lobyte(int16_t a) { return a & 0xff; }
static inline int8_t _hibyte(int16_t a) { return (a >> 8) & 0xff; }
static inline int16_t _optmul(int8_t a, int16_t b) {
  if (a == 0) {
    return 0;
  } else if (a == 1) {
    return b;
  } else if (a == -1) {
    return -b;
  } else {
    return a * b;
  }
}

// PERF: using this -> -100 bytes, +1700 cycles (when tilted).
int16_t _unused_vec_fastmul8p8_32(int16_t a, int16_t b) {
  // vec_asm.c is faster but also increases code size.
  if (!_lobyte(b)) {
    return _optmul(_hibyte(b), a);
  }
  if (!_lobyte(a)) {
    return _optmul(_hibyte(a), b);
  }
  return ((int32_t)a * b) >> 8;
}

uint16_t _unused_vec_fastsqr8p8(int16_t a) {
  // vec_asm.c is faster but also increases code size.
  if (!_lobyte(a)) {
    return _optmul(_hibyte(a), a);
  }
  return ((int32_t)a * a) >> 8;
}

extern const uint8_t vec_sqr_lo[];
extern const uint8_t vec_sqr_hi[];
extern const uint8_t vec_recip_lut[];

static inline uint16_t _get_sqr_div4(uint16_t x) {
  return ((uint16_t)vec_sqr_hi[x] << 8) | vec_sqr_lo[x];
}

inline void vec_add(vec3_t *dest, const vec3_t *src) {
  dest->x += src->x;
  dest->y += src->y;
  dest->z += src->z;
}

inline void vec_sub(vec3_t *dest, const vec3_t *src) {
  dest->x -= src->x;
  dest->y -= src->y;
  dest->z -= src->z;
}

inline void vec_negate(vec3_t *v) {
  v->x = -v->x;
  v->y = -v->y;
  v->z = -v->z;
}

uint16_t vec_fastsqr8p8u(uint16_t a) {
  uint8_t h = _hibyte(a);
  uint8_t l = _lobyte(a);

  // --- Special Case: High byte is 0 ---
  // Result is simply L^2
  if (h == 0) {
    return _get_sqr_div4(l) >> (8 - 2);
  }

  // --- Special Case: Low byte is 0 ---
  // Result is (H*256)^2 = H^2 * 65536
  if (l == 0) {
    return _get_sqr_div4((uint16_t)h << 1) << 8;
  }

  // --- General Case: Difference of Squares ---

  // 1. Compute L^2 and H^2
  uint16_t l2 = _get_sqr_div4(l) >> 6;
  uint16_t h2 = _get_sqr_div4((uint16_t)h << 1) << 8;

  // 2. Compute cross product 2*H*L using Difference of Squares
  // Since T(x) = x^2/4, then T(h+l) - T(h-l) = hl
  uint16_t sum = (uint16_t)h + l;
  uint16_t diff = (h > l) ? (h - l) : (l - h);
  uint16_t hl = _get_sqr_div4(sum) - _get_sqr_div4(diff);

  // 3. Combine parts
  // (H^2 << 16) + (2HL << 8) + L^2
  return h2 + (hl << 1) + l2;
}

uint16_t vec_fastsqr8p8(int16_t a) {
  if (a > 0) {
    return vec_fastsqr8p8u((uint16_t)a);
  } else {
    return vec_fastsqr8p8u(-a);
  }
}

// General case for multiplication.
// Note from Claude: this has up-to-a-few-LSB truncation error.
static inline int16_t _vec_fastmul8p8(int16_t a, int16_t b) {
  // --- 1. Sign Handling ---
  // Since vec_fastsqr8p8u is unsigned, we work with absolute values
  int8_t sign = 1;
  uint16_t ua, ub;

  if (a < 0) {
    ua = -a;
    sign = -sign;
  } else {
    ua = a;
  }
  if (b < 0) {
    ub = -b;
    sign = -sign;
  } else {
    ub = b;
  }

  // --- 2. Quarter-Square Identity ---
  // Identity: ab = ((a+b)^2 - (a-b)^2) / 4
  //
  // Note: In 8.8 fixed point:
  // (A/256 * B/256) = ( (A+B)^2/256^2 - (A-B)^2/256^2 ) / 4
  // Since vec_fastsqr8p8u(x) returns x^2/256, we just need to divide
  // the difference of the results by 4 to get the final 8.8 result.

  uint16_t sum = ua + ub;
  uint16_t diff = (ua > ub) ? (ua - ub) : (ub - ua);

  uint16_t sq_sum = vec_fastsqr8p8u(sum);
  uint16_t sq_diff = vec_fastsqr8p8u(diff);

  // The division by 4 (>> 2) aligns the result back to 8.8
  int16_t res = (int16_t)((sq_sum - sq_diff) >> 2);

  return (sign < 0) ? -res : res;
}

// Helper to perform 8x8 unsigned multiply using  the tables
// x * y = T(x+y) - T(x-y)
static inline uint16_t _mul8x8_u(uint8_t x, uint8_t y) {
  uint16_t sum = (uint16_t)x + y;
  uint16_t diff = (x > y) ? (x - y) : (y - x);
  return _get_sqr_div4(sum) - _get_sqr_div4(diff);
}

// Special case when a == int8_t (lobyte is zero)
// PERF: cycles: -700 (when tilted) bytes: +200
static int16_t _vec_fastmul8p0(int8_t h, int16_t b) {
  // --- 1. Sign Handling ---
  int sign = 1;
  uint8_t uh;
  uint16_t ub;

  if (h < 0) {
    uh = -h;
    sign = -sign;
  } else {
    uh = h;
  }
  if (b < 0) {
    ub = -b;
    sign = -sign;
  } else {
    ub = b;
  }

  // --- 2. Optimized Decomposition ---
  // Since a = H * 256, then a * b / 256 = H * b.
  // H * b = H * (b_hi << 8 + b_lo) = (H * b_hi << 8) + (H * b_lo)
  uint8_t b_hi = (uint8_t)(ub >> 8);
  uint8_t b_lo = (uint8_t)(ub & 0xFF);

  uint16_t term_hi = _mul8x8_u(uh, b_hi);
  uint16_t term_lo = _mul8x8_u(uh, b_lo);

  // Combine results: term_hi is shifted to the high byte position
  int16_t res = (int16_t)((term_hi << 8) + term_lo);

  return (sign < 0) ? -res : res;
}

int16_t _unused_vec_fastmul8p8(int16_t a, int16_t b) {
  // These early exits handle identity properties and zeros.
  if (!_lobyte(a)) {
    int8_t ah = _hibyte(a);
    if (ah == 0) {
      return 0;
    } else if (ah == 1) {
      return b;
    } else if (ah == -1) {
      return -b;
    } else {
      return _vec_fastmul8p0(ah, b);
    }
  }
  if (!_lobyte(b)) {
    int8_t bh = _hibyte(b);
    if (bh == 0) {
      return 0;
    } else if (bh == 1) {
      return a;
    } else if (bh == -1) {
      return -a;
    } else {
      return _vec_fastmul8p0(bh, a);
    }
  }
  return _vec_fastmul8p8(a, b);
}

int16_t vec_div8p8(int16_t a, int16_t b) {
  if (a == 0)
    return 0;

  int8_t sign = 1;
  uint16_t ua, ub;
  if (a < 0) {
    ua = -a;
    sign = -sign;
  } else {
    ua = a;
  }
  if (b < 0) {
    ub = -b;
    sign = -sign;
  } else {
    ub = b;
  }

  while (ub > 255) {
    ua >>= 1;
    ub >>= 1;
  }

  if (ub == 0)
    return sign > 0 ? 32767 : -32767;

  // Normalize ub to [128, 255], scaling ua by the same amount. If ua
  // overflows 16 bits here, the exact quotient is always > 32767 (ub is
  // still < 128 when it happens), so saturating is the exact result.
  while (ub < 128) {
    ub <<= 1;
    if (ua & 0x8000) {
      return sign > 0 ? 32767 : -32767;
    }
    ua <<= 1;
  }

  uint16_t b_recip = 0x100 + vec_recip_lut[ub - 128];

  // Scale ua down to prevent internal overflow in vec_fastmul8p8
  // Max safe value for ua to prevent sum overflow (ua + b_recip < 4096) is:
  // since b_recip is up to 512, ua must be < 3584. We use 3072 for headroom.
  uint8_t scale = 0;
  while (ua >= 3072) {
    ua >>= 1;
    scale++;
  }

  int16_t res = vec_fastmul8p8(ua, b_recip);

  uint32_t final_val = (uint32_t)res;
  if (scale > 0) {
    // Check for overflow before shifting left
    if (final_val > (32767 >> scale)) {
      return sign > 0 ? 32767 : -32767;
    }
    final_val <<= scale;
  }

  if (final_val > 32767) {
    return sign > 0 ? 32767 : -32767;
  }

  int16_t final_res = (int16_t)final_val;
  return sign > 0 ? final_res : -final_res;
}

static bool _unused_vec_project(const vec3_t *v) {
  if (v->x <= 8) {
    return false;
  }
  if (v->y < -v->x || v->y > v->x || v->z < -v->x || v->z > v->x) {
    return false;
  }
  vec_sx = (int16_t)(((int32_t)v->y * 256) / v->x);
  vec_sy = (int16_t)(((int32_t)v->z * 256) / v->x);
  return true;
}

// PERF: inline -> cycles: ~0 bytes: -140
static void _limit_vec(vec3_t *v) {
  if (v->x <= -255) {
    v->x = -256;
  } else if (v->x >= 255) {
    v->x = 256;
  }
  if (v->y <= -255) {
    v->y = -256;
  } else if (v->y >= 255) {
    v->y = 256;
  }
  if (v->z <= -255) {
    v->z = -256;
  } else if (v->z >= 255) {
    v->z = 256;
  }
}

void vec_normalize(vec3_t *v) {
  uint16_t L2 =
      vec_fastsqr8p8(v->x) + vec_fastsqr8p8(v->y) + vec_fastsqr8p8(v->z);

  if (L2 == 0) {
    return;
  }

  L2 <<= 6;
  uint16_t res = 0;
  uint16_t bit = (uint16_t)1 << 14;

  while (bit > L2) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (L2 >= res + bit) {
      L2 -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }

  if (res == 0) {
    return;
  }

  v->x = vec_div8p8(v->x, res << 1);
  v->y = vec_div8p8(v->y, res << 1);
  v->z = vec_div8p8(v->z, res << 1);

  _limit_vec(v);
}

void vec_cross(const vec3_t *u, const vec3_t *v, vec3_t *res) {
  res->x = vec_fastmul8p8(u->y, v->z) - vec_fastmul8p8(u->z, v->y);
  res->y = vec_fastmul8p8(u->z, v->x) - vec_fastmul8p8(u->x, v->z);
  res->z = vec_fastmul8p8(u->x, v->y) - vec_fastmul8p8(u->y, v->x);
}

void vec_orthonormalize(mat3_t *mat) {
  vec_normalize(&mat->front);
  vec_cross(&mat->up, &mat->front, &mat->left);
  vec_normalize(&mat->left);
  vec_cross(&mat->front, &mat->left, &mat->up);
  _limit_vec(&mat->up);
}

// clang-format off
const mat3_t kVecRollLeft = {
  { 256,    0,    0},
  {   0,  256,  -32},
  {   0,   32,  256}};
const mat3_t kVecRollRight = {
  { 256,    0,    0},
  {   0,  256,   32},
  {   0,  -32,  256}};
const mat3_t kVecPitchUp = {
  { 256,    0,   16},
  {   0,  256,    0},
  { -16,    0,  256}};
const mat3_t kVecPitchDown = {
  { 256,    0,  -16},
  {   0,  256,    0},
  {  16,    0,  256}};
const mat3_t kVecYawLeft = {
  { 256,    8,    0},
  {  -8,  256,    0},
  {   0,    0,  256}};
const mat3_t kVecYawRight = {
  { 256,   -8,    0},
  {   8,  256,    0},
  {   0,    0,  256}};
// clang-format on

void vec_transform(const mat3_t *mat, const vec3_t *u, vec3_t *res) {
  res->x = vec_fastmul8p8(mat->front.x, u->x);
  res->x += vec_fastmul8p8(mat->left.x, u->y);
  res->x += vec_fastmul8p8(mat->up.x, u->z);

  res->y = vec_fastmul8p8(mat->front.y, u->x);
  res->y += vec_fastmul8p8(mat->left.y, u->y);
  res->y += vec_fastmul8p8(mat->up.y, u->z);

  res->z = vec_fastmul8p8(mat->front.z, u->x);
  res->z += vec_fastmul8p8(mat->left.z, u->y);
  res->z += vec_fastmul8p8(mat->up.z, u->z);
}

void vec_transform_inv(const mat3_t *mat, const vec3_t *u, vec3_t *res) {
  res->x = vec_fastmul8p8(mat->front.x, u->x);
  res->x += vec_fastmul8p8(mat->front.y, u->y);
  res->x += vec_fastmul8p8(mat->front.z, u->z);

  res->y = vec_fastmul8p8(mat->left.x, u->x);
  res->y += vec_fastmul8p8(mat->left.y, u->y);
  res->y += vec_fastmul8p8(mat->left.z, u->z);

  res->z = vec_fastmul8p8(mat->up.x, u->x);
  res->z += vec_fastmul8p8(mat->up.y, u->y);
  res->z += vec_fastmul8p8(mat->up.z, u->z);
}

void vec_transform3(const mat3_t *t, mat3_t *m) {
  const mat3_t mc = *m;
  vec_transform(&mc, &t->front, &m->front);
  vec_transform(&mc, &t->left, &m->left);
  vec_transform(&mc, &t->up, &m->up);
}

void vec_transform3_inv(const mat3_t *t, mat3_t *m) {
  const mat3_t mc = *m;
  vec_transform_inv(t, &mc.front, &m->front);
  vec_transform_inv(t, &mc.left, &m->left);
  vec_transform_inv(t, &mc.up, &m->up);
}
