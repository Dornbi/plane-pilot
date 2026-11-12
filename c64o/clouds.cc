#include "clouds.h"

#include "clouddef.h"
#include "color.h"
#include "flight.h"
#include "mem.h"
#include "spritedef.h"
#include "sprites.h"
#include "vec.h"
#include "world.h"

// Per-frame path, like world.cc: the outliner would trade cycles for bytes.
#pragma optimize(push, nooutline)

// world.cc's conversion, repeated rather than shared because it is one shift
// and exporting it would tie two per-frame files together for nothing.
// flight_eye_* is 24.8 fixed point in half metres; the terrain grid - and therefore
// everything here - works in 1 m units (docs/clouds.md §2.1).
static inline int16_t _clouds_down_shift(uint32_t val) {
  return (int16_t)(val >> 9);
}

// The camera-space image of a *half* step along each world axis, indexed by
// world axis (§2.5). kCloudGroupOffset counts in half steps, so this is the
// unit its coefficients multiply.
#ifdef __FPILOT__
#pragma bss(tscratch)
#else
#pragma bss(bss2)
#endif
static vec3_t _clouds_half_basis[3];
// Every cloud group shares the same _clouds_dz within a frame, so each camera
// axis's row->z * _clouds_dz contribution is computed at most once per frame on
// first _clouds_dot_row(axis) use.
static int16_t _clouds_z[3];
static bool _clouds_z_valid[3];
static int16_t _clouds_dz;

// The basis only depends on the camera, so it is the same for every group in
// a frame. It is also useless in the ~58% of frames where no group is in
// range, so rather than pay it unconditionally it is built by the first group
// that wants it, and this flag stops the second from rebuilding it.
static bool _clouds_basis_valid;
static const int8_t *_clouds_pat;
#pragma bss(bss)

// Evaluates one row of vec_transform_inv(&world_cam, (vec_v.x, vec_v.y, _clouds_dz)):
// axis 0 -> camera x (front), 1 -> y (left), 2 -> z (up).
static __noinline int16_t _clouds_dot_row(uint8_t axis) {
  const vec3_t *row = &world_cam.front + axis;
  if (!_clouds_z_valid[axis]) {
    _clouds_z_valid[axis] = true;
    _clouds_z[axis] = vec_fastmul8p8(row->z, _clouds_dz);
  }
  return _clouds_z[axis] + vec_fastmul8p8(row->x, vec_v.x) +
         vec_fastmul8p8(row->y, vec_v.y);
}

static void _clouds_build_basis(void) {
  // vec_transform_inv() is linear and carries no translation, so the image of
  // the world displacement (k, 0, 0) is just the matrix's first column times
  // k: three multiplies per axis, nine for the whole basis, rather than the
  // twenty-seven three full transforms would cost.
  const int16_t k = (int16_t)kCloudOffsetU >> 1;
  const int16_t *cam = &world_cam.front.x;
  vec3_t *dst = _clouds_half_basis;
  for (uint8_t axis = 0; axis < 3; ++axis) {
    dst->x = vec_fastmul8p8(cam[0], k);
    dst->y = vec_fastmul8p8(cam[3], k);
    dst->z = vec_fastmul8p8(cam[6], k);
    ++cam;
    ++dst;
  }
}

// v outside [-r, r], as one unsigned compare instead of two signed ones: v + r
// maps the range onto [0, 2r] and everything else above it. Exact for any
// int16_t v while 0 <= r <= 32767, so 2r still fits.
static inline bool _clouds_outside(int16_t v, int16_t r) {
  return (uint16_t)(v + r) > (uint16_t)(r << 1);
}

// vec_v += pat[axis] * _clouds_half_basis[axis] across all 3 world axes, for
// the only coefficients the table contains. generate_clouds.py's
// check_group_offsets() fails the build if any coefficient exceeds +-2, which
// is what lets the doubling be a shift and the whole thing stay free of
// multiplies.
static void _clouds_apply_offset(void) {
  const int8_t *pat = _clouds_pat;
  _clouds_pat = pat + 3;
  const vec3_t *h = _clouds_half_basis;
  for (uint8_t axis = 0; axis < 3; ++axis, ++h) {
    int8_t coeff = pat[axis];
    if (coeff == 0) {
      continue;
    }
    int16_t x = h->x;
    int16_t y = h->y;
    int16_t z = h->z;
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
}

void clouds_add_candidates(void) {
  const uint16_t mx = (uint16_t)_clouds_down_shift(flight_eye_x);
  const uint16_t my = (uint16_t)_clouds_down_shift(flight_eye_y);

  // The deck is flat, so every cloud in the world is at the same height and
  // the vertical offset is computed once (§2.6).
  _clouds_dz = kCloudDeckU - _clouds_down_shift(flight_eye_z);

  // Where the eye sits inside its own cell, negated. Every candidate's offset
  // from the eye is then this plus a whole number of cells plus the jitter -
  // all small, all int16, and no absolute world position is ever formed. That
  // matters: the world wraps inside a uint16 and absolute positions would have
  // to be built modulo that, while differences never do.
  const uint16_t frac_x = (uint16_t)-(int16_t)(mx & (kCloudCellU - 1));
  const uint16_t frac_y = (uint16_t)-(int16_t)(my & (kCloudCellU - 1));

  uint8_t cx = (uint8_t)(mx >> kCloudCellShift) - kCloudScanRadius;
  const uint8_t start_cy = (uint8_t)(my >> kCloudCellShift) - kCloudScanRadius;

  _clouds_basis_valid = false;
  _clouds_z_valid[0] = false;
  _clouds_z_valid[1] = false;
  _clouds_z_valid[2] = false;

  const uint8_t kScanSpanHi = (2 * kCloudScanRadius + 1)
                              << (kCloudCellShift - kCloudJitterShift);
  const uint8_t kScanStepHi = 1 << (kCloudCellShift - kCloudJitterShift);

  for (uint8_t ux_hi = 0; ux_hi < kScanSpanHi; ux_hi += kScanStepHi, ++cx) {
    const uint8_t hx = kCloudHashX[cx & kCloudCellMaskX];
    uint8_t cy = start_cy;

    for (uint8_t uy_hi = 0; uy_hi < kScanSpanHi; uy_hi += kScanStepHi, ++cy) {
      // The gate, and the reason the scan is affordable: most cells stop here,
      // at two table reads and a compare (§2.3). A limit rather than a mask,
      // so density is a dial rather than a power of two - the pattern still
      // comes from the top two bits of the same byte, which the five gate bits
      // leave alone.
      const uint8_t idx = (hx ^ kCloudHashY[cy & kCloudCellMaskY]) & 31;
      const uint8_t ha = kCloudHashA[idx];
      if ((ha & kCloudGateBits) >= kCloudGateLimit) {
        continue;
      }
      const uint8_t hb = kCloudHashB[idx];

      // Cull on the world axes, before the transform. This is not only the
      // cheap reject it looks like - it is what makes the draw distance
      // consistent (§2.2). The 5 x 5 scan is guaranteed to find every group
      // within kCloudCullU on *each axis*; its corners reach further, and a
      // group drawn out there would appear and disappear as the eye crossed a
      // cell boundary rather than as it moved. Culling on the same box the
      // guarantee is stated in removes that entirely.
      // Because ux_hi/uy_hi start at 0 rather than -kCloudScanRadius, off_x and
      // off_y are already biased by +kCloudCullU (= kCloudScanRadius << 11).
      const uint8_t jx = ux_hi | (hb & 0x0F);
      const uint16_t off_x = frac_x + ((uint16_t)jx << kCloudJitterShift);
      if (off_x > (uint16_t)(kCloudCullU << 1)) {
        continue;
      }
      const uint8_t jy = uy_hi | (hb >> 4);
      const uint16_t off_y = frac_y + ((uint16_t)jy << kCloudJitterShift);
      if (off_y > (uint16_t)(kCloudCullU << 1)) {
        continue;
      }

      // vec_v is idle until blob projection below, so keeping dx/dy in its
      // zeropage words avoids extra tscratch loads inside _clouds_dot_row().
      vec_v.x = (int16_t)(off_x - kCloudCullU);
      vec_v.y = (int16_t)(off_y - kCloudCullU);

      // Behind the camera, or past the ladder's reach. vec_project() would
      // reject the first anyway; testing it here skips the rung walk.
      const int16_t cx_cam = _clouds_dot_row(0);
      if (cx_cam <= 8 || cx_cam > kCloudRungDepth[0]) {
        continue;
      }

      // A conservative wedge reject (§5). vec_project() culls to the screen
      // exactly, but it costs ~350 cycles and a group now pays it three times,
      // so a group nowhere near the viewport is worth turning away with
      // compares. sx = 256 y / x against a half-width of 160 means a point is
      // on screen only while |y| <= 0.625 x, and |z| <= 0.219 x vertically.
      // The bounds here are looser than both - 0.625 x by two shifts, 0.25 x
      // vertically - plus 128 units of slack for the blob offsets (up to 54)
      // and a blob's own radius (48), so nothing that could draw is rejected.
      //
      // cx_cam is in (8, kCloudRungDepth[0]] here, so both bounds are
      // positive and well inside _clouds_outside()'s range.
      const int16_t vert = (cx_cam >> 2) + 128;
      const int16_t cz_cam = _clouds_dot_row(2);
      if (_clouds_outside(cz_cam, vert)) {
        continue;
      }
      const int16_t lat = (cx_cam >> 1) + (vert >> 1) + 64;
      const int16_t cy_cam = _clouds_dot_row(1);
      if (_clouds_outside(cy_cam, lat)) {
        continue;
      }

      // The size ladder (§3.2). A walk down a table of constants rather than a
      // divide: the blob diameter is fixed, so the rung is a pure function of
      // depth and the thresholds are known at build time. All three blobs
      // share it - they are within 50 m of each other at a range of
      // kilometres, so three rungs would be the same answer three times.
      uint8_t rung = 0;
      while (rung < kCloudRungCount - 1 &&
             cx_cam <= kCloudRungDepth[rung + 1]) {
        ++rung;
      }
      // Far groups draw as a single blob (§3.5). Out here the three of them
      // span less than one hardware sprite, so the other two are a projection
      // and a stack insertion each spent on pixels the first already covers -
      // and, because the group count grows as the square of the depth, nearly
      // every group in range is out here.
      //
      // One rung up is about what the whole group covered, and it is still a
      // single-sprite entry because kCloudRungCollapsed is at most
      // kCloudRungStacked. Adding to the index rather than choosing between two
      // pointers also keeps this well away from the shape of §3.4.
      const uint8_t collapsed = rung < kCloudRungCollapsed;
      uint8_t blobs = 1;
      if (!collapsed) {
        if (!_clouds_basis_valid) {
          _clouds_build_basis();
          _clouds_basis_valid = true;
        }
        blobs = kCloudBlobsPerGroup;
        // The pattern is the top two bits of the same hash byte the gate read,
        // so a cell's orientation costs nothing to fetch (§2.5).
        _clouds_pat = kCloudGroupOffset[ha >> 6][0];
      }

      // One flat row per rung, so nothing here has to know which half of the
      // ladder it is on: bitmap2 is kSpriteNoBitmap below kCloudRungStacked and
      // the lower block's index above it, and the pivot comes with it (20
      // rather than 10 for the stacked rungs - the pivot is the centre of the
      // taller pair). Above kCloudRungStacked the entry becomes two hardware
      // sprites at commit, the lower one 21 lines down; that 21 flips the
      // dither row parity and the block's own phase was built with the same 21
      // in it, so the seam is continuous (§4.4).
      //
      // The flat table is also load bearing, not tidiness: selecting between
      // the two per-half structs in C miscompiles under oscar64 (§3.4).
      const sprite_cloud_rung_t *meta = &kSpriteDefCloudRung[rung + collapsed];

      for (uint8_t b = 0; b < blobs; ++b) {
        vec_v.x = cx_cam;
        vec_v.y = cy_cam;
        vec_v.z = cz_cam;
        if (!collapsed) {
          _clouds_apply_offset();
          // The group centre is in front of the camera, but a blob is offset by
          // up to 54 units, so at the shortest ranges one can still land behind
          // it.
          if (vec_v.x <= 8) {
            continue;
          }
        }
        // The shared out-of-line copy: an inlined one is ~260 bytes, for about
        // 15 cycles a blob.
        if (!vec_project_once()) {
          continue;
        }

        // Depth is per blob, not per group: the stack sorts on it, and inside
        // a group the near blob really should occlude the far one.
        sprites_stack_add(vec_v.x, (int16_t)kScreenWidthPixels / 2 - vec_sx,
                          (int16_t)kViewportEndYPixels / 2 - vec_sy,
                          meta->pivot_x, meta->pivot_y, meta->bitmap,
                          meta->bitmap2, COLOR_CLOUD,
                          kSpriteFlagExpandX | kSpriteFlagAlignDither);
      }
    }
  }
}

#pragma optimize(pop)
