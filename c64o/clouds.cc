#include "clouds.h"

#include "clouddef.h"
#include "clouds_asm.h"
#include "color.h"
#include "flight.h"
#include "mem.h"
#include "spritedef.h"
#include "sprites.h"
#include "vec.h"
#include "world.h"

// Per-frame path, like world.cc: the outliner would trade cycles for bytes.
#pragma optimize(push, nooutline)

#ifndef __OSCAR64__
// Host C counterparts of clouds_asm.cc's _clouds_build_basis(), _clouds_dot(),
// _clouds_apply_offset(), _clouds_scan_init(), and _clouds_scan_next().

// world.cc's conversion, repeated rather than shared because it is one shift
// and exporting it would tie two per-frame files together for nothing.
// flight_eye_* is 24.8 fixed point in half metres; the terrain grid - and
// therefore everything here - works in 1 m units (docs/clouds.md §2.1).
static inline int16_t _clouds_down_shift(uint32_t val) {
  return (int16_t)(val >> 9);
}

static void _clouds_build_basis(void) {
  // vec_transform_inv() is linear and carries no translation, so the image of
  // the world displacement (k, 0, 0) is just the matrix's first column times
  // k. With k = kCloudOffsetU >> 1 = 16, vec_fastmul8p8(c, 16) is trunc(c/16),
  // stored in camera-row order in [-16, +16].
  const int16_t k = (int16_t)kCloudOffsetU >> 1;
  const int16_t *cam = &world_cam.front.x;
  for (uint8_t i = 0; i < 9; ++i) {
    _clouds_half_basis[i] = (int8_t)vec_fastmul8p8(cam[i], k);
  }
}

// One camera axis of the group at (vec_v.x, vec_v.y, _clouds_dz) from the eye:
// 0 front, 1 left, 2 up, the rows of world_cam.
static __noinline int16_t _clouds_dot(uint8_t axis) {
  const vec3_t *row = &world_cam.front + axis;
  if (!_clouds_z_valid[axis]) {
    _clouds_z_valid[axis] = 1;
    _clouds_z[axis] = vec_fastmul8p8(row->z, _clouds_dz);
  }
  return _clouds_z[axis] + vec_fastmul8p8(row->x, vec_v.x) +
         vec_fastmul8p8(row->y, vec_v.y);
}

// v outside [-r, r], as one unsigned compare instead of two signed ones: v + r
// maps the range onto [0, 2r] and everything else above it. Exact for any
// int16_t v while 0 <= r <= 32767, so 2r still fits.
static inline bool _clouds_outside(int16_t v, int16_t r) {
  return (uint16_t)(v + r) > (uint16_t)(r << 1);
}

// vec_v = _clouds_centre + pat[axis] * _clouds_half_basis[axis + {0,3,6}]
// across all 3 world axes, for the only coefficients the table contains
// (+-1, +-2, 0), returning the updated vec_v.x.
static int16_t _clouds_apply_offset(void) {
  vec_v = _clouds_centre;
  const int8_t *pat = &kCloudGroupOffset[0][0][0] + _clouds_pat_off;
  _clouds_pat_off += 3;
  for (uint8_t axis = 0; axis < 3; ++axis) {
    int8_t coeff = pat[axis];
    if (coeff == 0) {
      continue;
    }
    int16_t x = _clouds_half_basis[axis + 0];
    int16_t y = _clouds_half_basis[axis + 3];
    int16_t z = _clouds_half_basis[axis + 6];
    if (coeff < 0) {
      x = -x;
      y = -y;
      z = -z;
      coeff = -coeff;
    }
    if (coeff > 1) {
      x <<= 1;
      y <<= 1;
      z <<= 1;
    }
    vec_v.x += x;
    vec_v.y += y;
    vec_v.z += z;
  }
  return vec_v.x;
}

static uint16_t _cld_frac_x;
static uint16_t _cld_frac_y;
static uint8_t _cld_cx;
static uint8_t _cld_cy;
static uint8_t _cld_start_cy;
static uint8_t _cld_ux_hi;
static uint8_t _cld_uy_hi;
static uint8_t _cld_hx;

static void _clouds_scan_init(void) {
  const uint16_t mx = (uint16_t)_clouds_down_shift(flight_eye_x);
  const uint16_t my = (uint16_t)_clouds_down_shift(flight_eye_y);
  _clouds_dz = kCloudDeckU - _clouds_down_shift(flight_eye_z);
  _cld_frac_x = (uint16_t)-(int16_t)(mx & (kCloudCellU - 1));
  _cld_frac_y = (uint16_t)-(int16_t)(my & (kCloudCellU - 1));
  _cld_cx = (uint8_t)(mx >> kCloudCellShift) - kCloudScanRadius;
  _cld_start_cy = (uint8_t)(my >> kCloudCellShift) - kCloudScanRadius;
  _cld_cy = _cld_start_cy;
  _cld_ux_hi = 0;
  _cld_uy_hi = 0;
  _cld_hx = kCloudHashX[_cld_cx & kCloudCellMaskX];
  _clouds_basis_valid = 0;
  _clouds_z_valid[0] = 0;
  _clouds_z_valid[1] = 0;
  _clouds_z_valid[2] = 0;
}

static uint8_t _clouds_scan_next(void) {
  const uint8_t kScanSpanHi = (2 * kCloudScanRadius + 1)
                              << (kCloudCellShift - kCloudJitterShift);
  const uint8_t kScanStepHi = 1 << (kCloudCellShift - kCloudJitterShift);

  while (_cld_ux_hi < kScanSpanHi) {
    const uint8_t idx =
        (_cld_hx ^ kCloudHashY[_cld_cy & kCloudCellMaskY]) & 31;
    const uint8_t ha = kCloudHashA[idx];
    const uint8_t ux_hi = _cld_ux_hi;
    const uint8_t uy_hi = _cld_uy_hi;

    // Advance cursor before any continue or return.
    ++_cld_cy;
    _cld_uy_hi += kScanStepHi;
    if (_cld_uy_hi >= kScanSpanHi) {
      ++_cld_cx;
      _cld_ux_hi += kScanStepHi;
      _cld_hx = kCloudHashX[_cld_cx & kCloudCellMaskX];
      _cld_cy = _cld_start_cy;
      _cld_uy_hi = 0;
    }

    if ((ha & kCloudGateBits) >= kCloudGateLimit) {
      continue;
    }
    const uint8_t hb = kCloudHashB[idx];
    const uint8_t jx = ux_hi | (hb & 0x0F);
    const uint16_t off_x = _cld_frac_x + ((uint16_t)jx << kCloudJitterShift);
    if (off_x > (uint16_t)(kCloudCullU << 1)) {
      continue;
    }
    const uint8_t jy = uy_hi | (hb >> 4);
    const uint16_t off_y = _cld_frac_y + ((uint16_t)jy << kCloudJitterShift);
    if (off_y > (uint16_t)(kCloudCullU << 1)) {
      continue;
    }

    vec_v.x = (int16_t)(off_x - kCloudCullU);
    vec_v.y = (int16_t)(off_y - kCloudCullU);

    _clouds_centre.x = _clouds_dot(0);
    if (_clouds_centre.x <= 8 || _clouds_centre.x > kCloudRungDepth[0]) {
      continue;
    }

    const int16_t vert = (_clouds_centre.x >> 2) + 128;
    _clouds_centre.z = _clouds_dot(2);
    if (_clouds_outside(_clouds_centre.z, vert)) {
      continue;
    }
    const int16_t lat = (_clouds_centre.x >> 1) + (vert >> 1) + 64;
    _clouds_centre.y = _clouds_dot(1);
    if (_clouds_outside(_clouds_centre.y, lat)) {
      continue;
    }

    uint8_t rung = 0;
    while (rung < kCloudRungCount - 1 &&
           _clouds_centre.x <= kCloudRungDepth[rung + 1]) {
      ++rung;
    }
    if (rung < kCloudRungCollapsed) {
      vec_v = _clouds_centre;
    } else {
      _clouds_pat_off = (uint8_t)((ha >> 6) * (kCloudBlobsPerGroup * 3));
      if (!_clouds_basis_valid) {
        _clouds_build_basis();
        _clouds_basis_valid = 1;
      }
    }
    return rung;
  }
  return 0xFF;
}
#endif // !__OSCAR64__

void clouds_add_candidates(void) {
  _clouds_scan_init();

  for (;;) {
    const uint8_t rung = _clouds_scan_next();
    if (rung == 0xFF) {
      break;
    }

    // Far groups draw as a single blob (§3.5). One rung up is about what the
    // whole group covered, and it is still a single-sprite entry because
    // kCloudRungCollapsed is at most kCloudRungStacked.
    const uint8_t collapsed = rung < kCloudRungCollapsed;
    uint8_t blobs = collapsed ? 1 : kCloudBlobsPerGroup;

    // One flat row per rung: bitmap2 is kSpriteNoBitmap below
    // kCloudRungStacked and the lower block's index above it (§3.4, §4.4).
    const sprite_cloud_rung_t *meta = &kSpriteDefCloudRung[rung + collapsed];

    do {
      mem_try_flip();
      int16_t depth = _clouds_centre.x;
      if (!collapsed) {
        depth = _clouds_apply_offset();
        // The group centre is in front of the camera, but a blob is offset by
        // up to 54 units, so at the shortest ranges one can still land behind
        // it.
        if (depth <= 8) {
          continue;
        }
      }
      // The shared out-of-line copy: an inlined one is ~260 bytes, for about
      // 15 cycles a blob.
      if (!vec_project_once()) {
        continue;
      }

      // Depth is per blob, not per group: the stack sorts on it, and inside a
      // group the near blob really should occlude the far one.
      sprites_stack_add(depth, (int16_t)kScreenWidthPixels / 2 - vec_sx,
                        (int16_t)kViewportEndYPixels / 2 - vec_sy,
                        meta->pivot_x, meta->pivot_y, meta->bitmap,
                        meta->bitmap2, COLOR_CLOUD,
                        kSpriteFlagExpandX | kSpriteFlagAlignDither);
    } while (--blobs != 0);
  }
}

#pragma optimize(pop)
