#include "clouds.h"

#include "bool.h"
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
// flight_eye_* is 24.8 fixed point in metres; the terrain grid - and therefore
// everything here - works in 2 m units (docs/clouds.md §2.1).
static inline int16_t _clouds_down_shift(uint32_t val) {
  return (int16_t)(val >> 9);
}

// The camera-space image of a *half* step along each world axis, indexed by
// world axis (§2.5). kCloudGroupOffset counts in half steps, so this is the
// unit its coefficients multiply.
#pragma bss(bss2)
static vec3_t _clouds_half_basis[3];
#pragma bss(bss)

// The basis only depends on the camera, so it is the same for every group in
// a frame. It is also useless in the ~58% of frames where no group is in
// range, so rather than pay it unconditionally it is built by the first group
// that wants it, and this flag stops the second from rebuilding it.
static bool _clouds_basis_valid;

static void _clouds_build_basis(void) {
  // vec_transform_inv() is linear and carries no translation, so the image of
  // the world displacement (k, 0, 0) is just the matrix's first column times
  // k: three multiplies per axis, nine for the whole basis, rather than the
  // twenty-seven three full transforms would cost.
  const int16_t k = (int16_t)kCloudOffsetU >> 1;

  _clouds_half_basis[0].x = vec_fastmul8p8(world_cam.front.x, k);
  _clouds_half_basis[0].y = vec_fastmul8p8(world_cam.left.x, k);
  _clouds_half_basis[0].z = vec_fastmul8p8(world_cam.up.x, k);

  _clouds_half_basis[1].x = vec_fastmul8p8(world_cam.front.y, k);
  _clouds_half_basis[1].y = vec_fastmul8p8(world_cam.left.y, k);
  _clouds_half_basis[1].z = vec_fastmul8p8(world_cam.up.y, k);

  _clouds_half_basis[2].x = vec_fastmul8p8(world_cam.front.z, k);
  _clouds_half_basis[2].y = vec_fastmul8p8(world_cam.left.z, k);
  _clouds_half_basis[2].z = vec_fastmul8p8(world_cam.up.z, k);
}

// The group pattern a collapsed group uses instead of its own (§3.5). One row,
// because a collapsed group draws one blob, and all zeroes so that the three
// _clouds_add_step() calls below stay unconditional: each returns on its first
// compare, which is cheaper in both cycles and bytes than a branch inside the
// loop - measured, the branch cost 137 bytes and bought nothing.
static const int8_t kCloudNoOffset[1][3] = {{0, 0, 0}};

// dst += coeff * _clouds_half_basis[axis], for the only coefficients the table
// contains. generate_clouds.py's check_group_offsets() fails the build if any
// coefficient exceeds +-2, which is what lets the doubling be a shift and the
// whole thing stay free of multiplies.
static void _clouds_add_step(vec3_t *dst, uint8_t axis, int8_t coeff) {
  if (coeff == 0) {
    return;
  }
  const vec3_t *h = &_clouds_half_basis[axis];
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
  dst->x += x;
  dst->y += y;
  dst->z += z;
}

void clouds_add_candidates(void) {
  const uint16_t mx = (uint16_t)_clouds_down_shift(flight_eye_x);
  const uint16_t my = (uint16_t)_clouds_down_shift(flight_eye_y);

  // The deck is flat, so every cloud in the world is at the same height and
  // the vertical offset is computed once (§2.6).
  const int16_t rel_z = kCloudDeckU - _clouds_down_shift(flight_eye_z);

  // Where the eye sits inside its own cell, negated. Every candidate's offset
  // from the eye is then this plus a whole number of cells plus the jitter -
  // all small, all int16, and no absolute world position is ever formed. That
  // matters: the world wraps inside a uint16 and absolute positions would have
  // to be built modulo that, while differences never do.
  const int16_t frac_x = -(int16_t)(mx & (kCloudCellU - 1));
  const int16_t frac_y = -(int16_t)(my & (kCloudCellU - 1));

  const int8_t centre_cx = (int8_t)(mx >> kCloudCellShift);
  const int8_t centre_cy = (int8_t)(my >> kCloudCellShift);

  _clouds_basis_valid = false;

  for (int8_t dx = -kCloudScanRadius; dx <= kCloudScanRadius; ++dx) {
    const int8_t cx = centre_cx + dx;
    const uint8_t hx = kCloudHashX[cx & kCloudCellMaskX];
    const int16_t cell_x = frac_x + ((int16_t)dx << kCloudCellShift);

    for (int8_t dy = -kCloudScanRadius; dy <= kCloudScanRadius; ++dy) {
      const int8_t cy = centre_cy + dy;

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

      const int16_t rel_x =
          cell_x + ((int16_t)(hb & 0x0F) << kCloudJitterShift);
      const int16_t rel_y = frac_y + ((int16_t)dy << kCloudCellShift) +
                            ((int16_t)(hb >> 4) << kCloudJitterShift);

      // Cull on the world axes, before the transform. This is not only the
      // cheap reject it looks like - it is what makes the draw distance
      // consistent (§2.2). The 5 x 5 scan is guaranteed to find every group
      // within kCloudCullU on *each axis*; its corners reach further, and a
      // group drawn out there would appear and disappear as the eye crossed a
      // cell boundary rather than as it moved. Culling on the same box the
      // guarantee is stated in removes that entirely.
      if (rel_x > kCloudCullU || rel_x < -kCloudCullU || rel_y > kCloudCullU ||
          rel_y < -kCloudCullU) {
        continue;
      }

      vec3_t delta;
      delta.x = rel_x;
      delta.y = rel_y;
      delta.z = rel_z;

      vec3_t centre;
      vec_transform_inv(&world_cam, &delta, &centre);

      // Behind the camera, or past the ladder's reach. vec_project() would
      // reject the first anyway; testing it here skips the rung walk.
      if (centre.x <= 8 || centre.x > kCloudRungDepth[0]) {
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
      const int16_t lat = (centre.x >> 1) + (centre.x >> 3) + 128;
      if (centre.y > lat || centre.y < -lat) {
        continue;
      }
      const int16_t vert = (centre.x >> 2) + 128;
      if (centre.z > vert || centre.z < -vert) {
        continue;
      }

      // The size ladder (§3.2). A walk down a table of constants rather than a
      // divide: the blob diameter is fixed, so the rung is a pure function of
      // depth and the thresholds are known at build time. All three blobs
      // share it - they are within 100 m of each other at a range of
      // kilometres, so three rungs would be the same answer three times.
      uint8_t rung = 0;
      while (rung < kCloudRungCount - 1 &&
             centre.x <= kCloudRungDepth[rung + 1]) {
        ++rung;
      }
      // Far groups draw as a single blob (§3.5). Out here the three of them
      // span less than one hardware sprite, so the other two are a projection
      // and a stack insertion each spent on pixels the first already covers -
      // and, because the group count grows as the square of the depth, nearly
      // every group in range is out here.
      //
      // The collapse is three things that cost nothing: a blob count, a zero
      // offset row, and a rung shift. One rung up is about what the whole
      // group covered, and it is still a single-sprite entry because
      // kCloudRungCollapsed is at most kCloudRungStacked. Adding to the index
      // rather than choosing between two pointers also keeps this well away
      // from the shape of §3.4.
      const uint8_t collapsed = rung < kCloudRungCollapsed;
      const uint8_t blobs = collapsed ? 1 : kCloudBlobsPerGroup;

      if (!_clouds_basis_valid) {
        _clouds_build_basis();
        _clouds_basis_valid = true;
      }

      // The pattern is the top two bits of the same hash byte the gate read,
      // so a cell's orientation costs nothing to fetch (§2.5).
      const int8_t(*pattern)[3] =
          collapsed ? kCloudNoOffset : kCloudGroupOffset[ha >> 6];

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
        vec_v = centre;
        _clouds_add_step(&vec_v, 0, pattern[b][0]);
        _clouds_add_step(&vec_v, 1, pattern[b][1]);
        _clouds_add_step(&vec_v, 2, pattern[b][2]);

        // The group centre is in front of the camera, but a blob is offset by
        // up to 54 units, so at the shortest ranges one can still land behind
        // it.
        if (vec_v.x <= 8) {
          continue;
        }
        if (!vec_project()) {
          continue;
        }

        // Depth is per blob, not per group: the stack sorts on it, and inside
        // a group the near blob really should occlude the far one.
        sprites_stack_add(vec_v.x, (int16_t)kScreenWidthPixels / 2 - vec_sx,
                          (int16_t)kViewportEndYPixels / 2 - vec_sy,
                          meta->pivot_x, meta->pivot_y, meta->bitmap,
                          meta->bitmap2, kColorCloud,
                          kSpriteFlagExpandX | kSpriteFlagAlignDither);
      }
    }
  }
}

#pragma optimize(pop)
