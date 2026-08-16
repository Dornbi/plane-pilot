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
// flight_eye_* is 24.8 fixed point in metres; the terrain grid - and therefore
// everything here - works in 2 m units (docs/clouds.md §2.1).
static inline int16_t _clouds_down_shift(uint32_t val) {
  return (int16_t)(val >> 9);
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

  for (int8_t dx = -kCloudScanRadius; dx <= kCloudScanRadius; ++dx) {
    const int8_t cx = centre_cx + dx;
    const uint8_t hx = kCloudHashX[cx & kCloudCellMaskX];
    const int16_t cell_x = frac_x + ((int16_t)dx << kCloudCellShift);

    for (int8_t dy = -kCloudScanRadius; dy <= kCloudScanRadius; ++dy) {
      const int8_t cy = centre_cy + dy;

      // The gate, and the reason the scan is affordable: seven cells in eight
      // stop here, at two table reads and a mask (§2.3).
      const uint8_t idx = (hx ^ kCloudHashY[cy & kCloudCellMaskY]) & 31;
      const uint8_t ha = kCloudHashA[idx];
      if ((ha & kCloudGateMask) != 0) {
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
      vec_transform_inv(&world_cam, &delta, &vec_v);

      // Behind the camera, or past the ladder's reach. vec_project() would
      // reject the first anyway; testing it here skips the rung walk.
      if (vec_v.x <= 8 || vec_v.x > kCloudRungDepth[0]) {
        continue;
      }

      // The size ladder (§3.2). A walk down a table of constants rather than a
      // divide: the blob diameter is fixed, so the rung is a pure function of
      // depth and the thresholds are known at build time.
      uint8_t rung = 0;
      while (rung < kCloudRungCount - 1 &&
             vec_v.x <= kCloudRungDepth[rung + 1]) {
        ++rung;
      }
      // Phase 5 draws single sprites only; the 1 x 2 stacks arrive with the
      // rest of the ladder. Clamping rather than culling keeps a near cloud
      // visible at the largest size it can currently draw.
      if (rung >= kCloudRungStacked) {
        rung = kCloudRungStacked - 1;
      }

      if (!vec_project()) {
        continue;
      }

      const sprite_cloud1_meta_t *meta = &kSpriteDefCloud1Sprite[rung];
      sprites_stack_add(vec_v.x, (int16_t)kScreenWidthPixels / 2 - vec_sx,
                        (int16_t)kViewportEndYPixels / 2 - vec_sy,
                        meta->pivot_x, meta->pivot_y, meta->bitmap_idx,
                        kSpriteNoBitmap, kColorCloud,
                        kSpriteFlagExpandX | kSpriteFlagAlignDither);
    }
  }
}

#pragma optimize(pop)
