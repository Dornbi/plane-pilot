#include "world.h"

#include "benchmark.h"
#ifdef __ENABLE_CLOUDS__
#include "clouds.h"
#endif
#include "color.h"
#include "flight.h"
#include "fmath.h"
#include "gfx.h"
#include "mem.h"
#include "poly.h"
#include "render.h"
#include "roll.h"
#include "spritedef.h"
#include "sprites.h"
#include "gun.h"
#include "opponent.h"
#include "vec.h"

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

// Not static so that polydemo.cc can access them for testing.
__zeropage uint8_t _world_grid_radius;
__zeropage uint8_t _world_start_cx;
__zeropage uint8_t _world_start_cy;
__zeropage vec3_t _world_p_start;
__zeropage vec3_t _world_dx_vec;
__zeropage vec3_t _world_dy_vec;
__zeropage int8_t _world_step_x;
__zeropage int8_t _world_step_y;
__zeropage vec3_t _world_vec_v;

#pragma bss(bss2)
mat3_t world_cam;

// Rebuilt at the start of every world_render_grid() and only read inside it,
// so fpilot keeps them, and _world_render_object()'s poly_verts, in the main
// screen's dead rows (mem.h scratch).
#ifdef __FPILOT__
#pragma bss(scratch)
#endif
static vec3_t _world_dx4[9];
static vec3_t _world_dy4[9];
#pragma bss(bss2)
__striped static int16_t _world_mitch_x[16];
__striped static int16_t _world_mitch_y[16];
__striped static int16_t _world_mitch_z[16];

// Mitchell's Best-Candidate algorithm to maximize distance between points
// while maintaining an organic, non-linear distribution.
// https://gemini.google.com/share/c1bafe545f1f
// Each entry is the byte offset (n * sizeof(vec3_t)) of _world_dx4[n] or
// _world_dy4[n], n in {0, 2, 4, 6}, as kWorldObjX/Y hold theirs, so the
// precompute below indexes the tables directly instead of multiplying by 6.
#define WORLD_MITCH(n) ((n) * sizeof(vec3_t))
static const uint8_t kMitchellPointsX[16] = {
    WORLD_MITCH(0), WORLD_MITCH(6), WORLD_MITCH(0), WORLD_MITCH(2),
    WORLD_MITCH(6), WORLD_MITCH(2), WORLD_MITCH(4), WORLD_MITCH(0),
    WORLD_MITCH(6), WORLD_MITCH(4), WORLD_MITCH(0), WORLD_MITCH(2),
    WORLD_MITCH(4), WORLD_MITCH(4), WORLD_MITCH(2), WORLD_MITCH(4)};
static const uint8_t kMitchellPointsY[16] = {
    WORLD_MITCH(0), WORLD_MITCH(0), WORLD_MITCH(6), WORLD_MITCH(2),
    WORLD_MITCH(4), WORLD_MITCH(6), WORLD_MITCH(0), WORLD_MITCH(2),
    WORLD_MITCH(6), WORLD_MITCH(4), WORLD_MITCH(4), WORLD_MITCH(0),
    WORLD_MITCH(2), WORLD_MITCH(2), WORLD_MITCH(4), WORLD_MITCH(6)};
#undef WORLD_MITCH

static inline int16_t _down_shift(uint32_t val) { return (int16_t)(val >> 9); }

// __noinline keeps this in one copy instead of being expanded into both
// call sites in _world_init_start_dx_dy. Verified byte-identical codegen
// with and without optimize(3), so no pragma region is needed here.
static __noinline void _world_split_vec(vec3_t *v, vec3_t d9[9]) {
  // This version needs half vectors -> more additions and subtractions and
  // more code. However, this means that dots and objects are closer to the
  // center of the grid cell which means fewer grid cells are needed.
  // vec3_t is three contiguous int16_t, so loop over the components instead
  // of expanding the split inline three times (saves code size; only called
  // twice per frame).
  int16_t *vp = (int16_t *)v;
  int16_t *dp = (int16_t *)d9;
  for (uint8_t i = 0; i < 3; ++i) {
    int16_t val = *vp;
    int16_t hlf = val >> 1;
    int16_t dbl = val << 1;
    dp[0 * 3] = -dbl;
    dp[1 * 3] = -val - hlf;
    dp[2 * 3] = -val;
    dp[3 * 3] = -hlf;
    dp[4 * 3] = 0;
    dp[5 * 3] = hlf;
    dp[6 * 3] = val;
    dp[7 * 3] = val + hlf;
    dp[8 * 3] = dbl;
    *vp = dbl << 1;
    ++vp;
    ++dp;
  }
}

#ifdef __OSCAR64__
static_assert(kViewportHeightPixels == 112 && kViewportWidthPixels == 320 &&
                  kViewportStartX == 0 && kScreenHeight == 25 &&
                  kViewportHeight == 14 && kGfxDotGround == 8 &&
                  kGfxGroundPoints == 224 && kGfxColorPoints == 240,
              "_world_draw_box_points asm constants mismatch");
#endif

static inline void _world_draw_box_points(uint8_t start_idx, uint8_t num_points,
                                          WorldMapType map_type) {
#ifdef __OSCAR64__
  uint8_t idx = start_idx & 0x0F;
  uint8_t cnt = num_points;
  uint8_t col = WORLD_DOT_COLORS[map_type];
  col = (col == kGfxDotGround) ? 0 : (uint8_t)(0x08 | col);
  __asm {
    ldx idx
  L_wd_loop:
    // Forward cone check: vec_v.x = _world_vec_v.x + _world_mitch_x[idx] >= 9.
    clc
    lda _world_vec_v + 0
    adc _world_mitch_x, x
    sta vec_v + 0
    lda _world_vec_v + 1
    adc _world_mitch_x + 16, x
    bmi L_wd_fail_mid
    sta vec_v + 1
    bne L_wd_x_ok
    lda vec_v + 0
    cmp #$09
    bcc L_wd_fail_mid
  L_wd_x_ok:

    // Lateral cone check: |y| <= x; store |y| in vec_proj_mul_a, sign in vec_v + 3.
    clc
    lda _world_vec_v + 2
    adc _world_mitch_y, x
    sta vec_proj_mul_a + 0
    lda _world_vec_v + 3
    adc _world_mitch_y + 16, x
    sta vec_v + 3
    bpl L_wd_y_pos
    sec
    lda #$00
    sbc vec_proj_mul_a + 0
    sta vec_proj_mul_a + 0
    lda #$00
    sbc vec_v + 3
  L_wd_y_pos:
    sta vec_proj_mul_a + 1
    cmp vec_v + 1
    bcc L_wd_y_ok
    bne L_wd_fail_mid
    lda vec_v + 0
    cmp vec_proj_mul_a + 0
    bcs L_wd_y_ok
  L_wd_fail_mid:
    jmp L_wd_next
  L_wd_y_ok:

    // Vertical cone check: |z| <= (x >> 2); store |z| in vec_proj_mul_b, sign in vec_v + 5.
    clc
    lda _world_vec_v + 4
    adc _world_mitch_z, x
    sta vec_proj_mul_b + 0
    lda _world_vec_v + 5
    adc _world_mitch_z + 16, x
    sta vec_v + 5
    bpl L_wd_z_pos
    sec
    lda #$00
    sbc vec_proj_mul_b + 0
    sta vec_proj_mul_b + 0
    lda #$00
    sbc vec_v + 5
  L_wd_z_pos:
    sta vec_proj_mul_b + 1
    lda vec_v + 1
    lsr
    sta vec_proj_recip
    lda vec_v + 0
    ror
    lsr vec_proj_recip
    ror
    sta vec_proj_tmp1
    lda vec_proj_recip
    cmp vec_proj_mul_b + 1
    bcc L_wd_fail_mid
    bne L_wd_z_ok
    lda vec_proj_tmp1
    cmp vec_proj_mul_b + 0
    bcc L_wd_fail_mid
  L_wd_z_ok:

    // Tightened lateral check for 320px viewport: |y| <= x - (x >> 2) (C=1 here).
    lda vec_v + 0
    sbc vec_proj_tmp1
    tay
    lda vec_v + 1
    sbc vec_proj_recip
    cmp vec_proj_mul_a + 1
    bcc L_wd_fail_mid
    bne L_wd_proj
    cpy vec_proj_mul_a + 0
    bcc L_wd_fail_mid
  L_wd_proj:

    // Normalize x into [128, 255] alongside |y| and |z|, then look up reciprocal.
    lda vec_v + 0
    sta vec_proj_tmp1
    lda vec_v + 1
    sta vec_proj_recip
    beq L_wd_shl_chk
  L_wd_shr:
    lsr vec_proj_recip
    ror vec_proj_tmp1
    lsr vec_proj_mul_a + 1
    ror vec_proj_mul_a + 0
    lsr vec_proj_mul_b + 1
    ror vec_proj_mul_b + 0
    lda vec_proj_recip
    bne L_wd_shr
    beq L_wd_lut
  L_wd_shl_chk:
    lda vec_proj_tmp1
    bmi L_wd_lut
  L_wd_shl:
    asl vec_proj_mul_a + 0
    asl vec_proj_mul_b + 0
    asl vec_proj_tmp1
    bpl L_wd_shl
  L_wd_lut:
    ldx vec_proj_tmp1
    lda vec_recip_lut - 128, x
    sta vec_proj_recip

    // Project horizontal coordinate -> vec_v+2 = hpx_lo = (160 - vec_sx) >> 1.
    // Note vec_v+0..3 are dead after vec_project_mul reads vec_v+3 for sign.
    lda vec_v + 3
    sta vec_mul8_lo
    lda vec_proj_mul_a + 0
    jsr vec_project_mul
    sta vec_proj_tmp1
    lda #$a0
    sec
    sbc vec_proj_tmp1
    sta vec_v + 2
    stx vec_proj_tmp1
    lda #$00
    sbc vec_proj_tmp1
    lsr
    ror vec_v + 2
    lda vec_v + 2
    cmp #$a0
    bcs L_wd_next

    // Project vertical coordinate -> vec_v+4 = py_lo = 56 - vec_sy_lo.
    lda vec_v + 5
    sta vec_mul8_lo
    lda vec_proj_mul_b + 0
    jsr vec_project_mul
    eor #$ff
    clc
    adc #$39
    cmp #$70
    bcs L_wd_next
    sta vec_v + 4

    // Plot into empty solid-ground cell only; vec_v+0/1 serves as scr_p.
    lsr
    lsr
    lsr
    tax
    lda mem_screen_row_ptrs, x
    sta vec_v + 0
    lda mem_screen_row_ptrs + 25, x
    sta vec_v + 1
    lda vec_v + 2
    lsr
    lsr
    tay
    lda (vec_v + 0), y
    cmp #$80
    bne L_wd_next
    lda vec_v + 2
    and #$03
    sta vec_proj_tmp1
    lda vec_v + 4
    and #$06
    asl
    ora vec_proj_tmp1
    sta vec_proj_tmp1
    lda col
    bne L_wd_color
    lda vec_proj_tmp1
    ora #$e0
    sta (vec_v + 0), y
    bne L_wd_next
  L_wd_color:
    lda vec_proj_tmp1
    ora #$f0
    sta (vec_v + 0), y
    lda mem_color_row_ptrs, x
    sta vec_v + 0
    lda mem_color_row_ptrs + 14, x
    sta vec_v + 1
    lda col
    sta (vec_v + 0), y
  L_wd_next:
    dec cnt
    beq L_wd_done
    ldx idx
    inx
    txa
    and #$0f
    sta idx
    tax
    jmp L_wd_loop
  L_wd_done:
  }
#else
  uint8_t idx = start_idx & 0x0F;
  uint8_t color = WORLD_DOT_COLORS[map_type];
  for (uint8_t i = num_points;;) {
    // A point behind the camera needs no y, and one off to the side no z.
    vec_v.x = _world_vec_v.x + _world_mitch_x[idx];
    if (vec_viewport_ahead()) {
      vec_v.y = _world_vec_v.y + _world_mitch_y[idx];
      if (vec_viewport_beside()) {
        vec_v.z = _world_vec_v.z + _world_mitch_z[idx];
        if (vec_viewport_project()) {
          gfx_draw_projected(color);
        }
      }
    }
    if (--i == 0) {
      break;
    }
    idx = (idx + 1) & 0x0F;
  }
#endif
}

// Radius 2 uses [0..2] (total 72 = 4 + 2*9 + 2*25); radius 3 uses [0..3]
// (total 96 = 4 + 2*9 + 25 + 49).
static const uint8_t kNumPoints3[] = {8, 4, 2, 1};
// Total = 108 = 2 + 25 (5*5) + 81 (9*9)
static const uint8_t kNumPoints4[] = {4, 2, 2, 1, 1};
static const uint8_t *_num_points_per_radius;

// One axis of the grid, 0 for x and 1 for y. Sets the axis' step vector, its
// split table, and which way and from which cell the walk goes, and returns
// the start point's coordinate along the axis, in the world frame. Shared
// rather than written out per axis, and it takes the axis rather than its
// operands, because passing those twice cost more than the code it shares.
static __noinline int16_t _world_init_axis(uint8_t axis, int16_t grid_spacing) {
  // The camera's front, left and up components along the axis.
  const int16_t *cam = &world_cam.front.x + axis;
  vec3_t *dvec = axis ? &_world_dy_vec : &_world_dx_vec;
  // The order is such that when _vec_v.x becomes < 0, we can break the loop
  // along both axes.
  dvec->x = cam[0];
  dvec->y = cam[3];
  dvec->z = cam[6];
  _world_split_vec(dvec, axis ? _world_dy4 : _world_dx4);
  uint16_t m = _down_shift(axis ? flight_eye_y : flight_eye_x);
  uint8_t grid = (uint8_t)((m + 512) >> 10);
  int16_t p = grid_spacing;
  int8_t step = -1;
  uint8_t start = grid + _world_grid_radius;
  if (cam[0] > 0) {
    vec_negate(dvec);
  } else {
    p = -p;
    step = 1;
    start = grid - _world_grid_radius;
  }
  if (axis) {
    _world_step_y = step;
    _world_start_cy = start;
  } else {
    _world_step_x = step;
    _world_start_cx = start;
  }
  return p - (int16_t)(m - ((uint16_t)grid << 10));
}

void _world_init_start_dx_dy() {
  static const uint16_t kGridSpacing = 0x400;
  // Half the top bit of eye_z >> 9, clamped to 2..4: 4 from 256 up, 3 from
  // 64, else 2. Two compares rather than finding the bit and clamping it.
  // kMinHeight (  ~16m) = 0x002000  5 --> 2
  // default    (  ~64m) = 0x008000  7 --> 3
  //            ( ~256m) = 0x020000  9 --> 4
  uint16_t n = (uint16_t)(flight_eye_z >> 9);
  uint8_t radius = 2;
  const uint8_t *num_points = kNumPoints3;
  if (n >= 64) {
    radius = 3;
    if (n >= 256) {
      radius = 4;
      num_points = kNumPoints4;
    }
  }
  _world_grid_radius = radius;
  _num_points_per_radius = num_points;
  int16_t grid_spacing = kGridSpacing * radius;

  // Pre-calculate delta vectors for step in X and step in Y, and the start
  // point relative to the eye.
  _world_p_start.x = _world_init_axis(0, grid_spacing);
  _world_p_start.y = _world_init_axis(1, grid_spacing);
  _world_p_start.z = -_down_shift(flight_eye_z);

  // Precompute Mitchell combinations
  for (uint8_t i = 0; i < 16; ++i) {
    const vec3_t *dx =
        (const vec3_t *)((const uint8_t *)_world_dx4 + kMitchellPointsX[i]);
    const vec3_t *dy =
        (const vec3_t *)((const uint8_t *)_world_dy4 + kMitchellPointsY[i]);
    _world_mitch_x[i] = dx->x + dy->x;
    _world_mitch_y[i] = dx->y + dy->y;
    _world_mitch_z[i] = dx->z + dy->z;
  }

  // The radius-4 start coords reach ~4600, which used to need a halving
  // workaround around the old quarter-square multiply (it overflowed once
  // |a| + |b| exceeded 4095). The assembly vec_fastmul8p8 is exact for all
  // int16 inputs, so the transform can run at full precision.
  vec_transform_inv(&world_cam, &_world_p_start, &_world_p_start);
}

void _world_render_object(WorldMapType object_type) {
#ifdef __FPILOT__
#pragma bss(scratch)
#endif
  static vec3_t poly_verts[kPolyMaxVertices];
#pragma bss(bss2)
  uint8_t obj_idx = object_type - kWorldMapObjStart;
  uint8_t num_verts = kWorldObjNumVerts[obj_idx];
  uint8_t k = kWorldObjFirstVert[obj_idx];
  for (uint8_t i = 0; i < num_verts; ++i, ++k) {
    // kWorldObjX/Y hold byte offsets into the tables (world_map.cc).
    const vec3_t *dx =
        (const vec3_t *)((const uint8_t *)_world_dx4 + kWorldObjX[k]);
    const vec3_t *dy =
        (const vec3_t *)((const uint8_t *)_world_dy4 + kWorldObjY[k]);
    poly_verts[i].x = _world_vec_v.x + dx->x + dy->x;
    poly_verts[i].y = _world_vec_v.y + dx->y + dy->y;
    poly_verts[i].z = _world_vec_v.z + dx->z + dy->z;
  }
  // Just the draw, not the vertex build above it, so this stays comparable
  // with the by-hand figure in docs/framerate.md.
  bm_sub_start();
  poly_draw_3d(poly_verts, num_verts, kWorldObjChars[obj_idx],
               WORLD_OBJ_COLORS[obj_idx]);
  bm_sub_end(BM_SUB_POLY);
}

__noinline void world_render_grid() {
  bm_model_start();
  _world_init_start_dx_dy();

  // If the current vec_v.x falls below this value, we can abort. The tests
  // below subtract and look at the sign rather than compare: x is at most the
  // grid's reach plus the altitude, far inside int16, and bailout is -512 at
  // the least, so the difference cannot overflow, and oscar64's signed
  // 16-bit compare is twice the code.
  int16_t bailout = -_max16(_abs16(_world_dx4[0].x), _abs16(_world_dy4[0].x));
  uint8_t cx = _world_start_cx;
  for (int8_t x = -(int8_t)_world_grid_radius;;) {
    _world_vec_v = _world_p_start;
    uint8_t abs_x = _abs8(x);
    // Note: cx is N and cy is W in this case.
    const uint8_t *map_row = world_map_row(cx & kWorldMapHeightMask);
    uint8_t cy = _world_start_cy;
    for (int8_t y = -(int8_t)_world_grid_radius;;) {
      WorldMapType map_type = world_map_cell(map_row, cy & kWorldMapWidthMask);
      if (map_type >= kWorldMapObjStart) {
        _world_render_object(map_type);
      } else if (map_type != MAP_NOTHING) {
        uint8_t start_idx = (cx << 1) + cy;
        uint8_t num_points = _num_points_per_radius[_max8(abs_x, _abs8(y))];
        _world_draw_box_points(start_idx, num_points, map_type);
      }
      if (y == (int8_t)_world_grid_radius) {
        break;
      }
      ++y;
      cy += _world_step_y;
      //  Step along Y axis
      vec_add(&_world_vec_v, &_world_dy_vec);
      if ((int16_t)(_world_vec_v.x - bailout) < 0) {
        break;
      }
    }
    if (x == (int8_t)_world_grid_radius) {
      break;
    }
    ++x;
    cx += _world_step_x;
    // Step along X axis
    vec_add(&_world_p_start, &_world_dx_vec);
    if ((int16_t)(_world_p_start.x - bailout) < 0) {
      break;
    }
  }

  bm_model_end(830, "GRD:");

#ifdef __ENABLE_DEBUG__
  if (mem_debug_enabled) {
    print_labeled_bcd(772, "GRD:", _world_grid_radius, 2);
  }
#endif
}

void world_update_roll_state() {
  bool updated = false;
  // Vector pointing to the distance.
  vec_v.x = world_cam.front.x;
  vec_v.y = world_cam.front.y;
  vec_v.z = 0;
  if (vec_v.x != 0 || vec_v.y != 0) {
    // Furthest possible point on the horizon.
    // With <<7 we could already overflow int16_t.
    vec_v.x <<= 6;
    vec_v.y <<= 6;
    vec_transform_inv(&world_cam, &vec_v, &vec_v);
    if (vec_project_once()) {
      render_cx_pixels = (int16_t)kScreenWidthPixels / 2 - vec_sx;
      render_cy_pixels = (int16_t)kViewportEndYPixels / 2 - vec_sy;
      roll_angle = _get_roll_angle(world_cam.up.z, world_cam.left.z);
      updated = true;
    }
  }
  if (!updated) {
    if (world_cam.front.z > 0) {
      render_cx_pixels = kScreenWidthPixels / 2;
      render_cy_pixels = kViewportEndYPixels + 100;
      roll_angle = 0;
    } else {
      render_cx_pixels = kScreenWidthPixels / 2;
      render_cy_pixels = kViewportEndYPixels;
      roll_angle = kRollMax / 2;
    }
  }
  roll_update_state();
}

static const vec3_t kSunDirWorld = {0, 256, 64};

// Rebuilds the viewport sprite frame. The sun is the only object today, and it
// goes through the stack as an ordinary entry rather than owning a fixed sprite
// index: an "infinite" depth sorts it behind everything, which is where it
// belongs. Clouds (clouds.md) and opponents (planes.md) add themselves here
// too.
__forceinline void world_update_objects() {
  sprites_stack_reset();
#ifdef __ENABLE_CLOUDS__
  bm_sub_start();
  clouds_add_candidates();
  bm_sub_end(BM_SUB_CLOUDS);
#endif
  opponent_add_sprites();
  gun_add_sprites();
  vec_transform_inv(&world_cam, &kSunDirWorld, &vec_v);
  if (vec_project_once()) {
    sprites_stack_add(0x7FFF, kScreenWidthPixels / 2 - vec_sx,
                      kViewportEndYPixels / 2 - vec_sy, kSpriteDefSun.pivot_x,
                      kSpriteDefSun.pivot_y, kSpriteDefSun.bitmap_idx,
                      kSpriteNoBitmap, COLOR_SUN, 0);
  }
  // The orientation indicator, on the sprite the stack never hands out
  // (sprites.h). Fixed in the middle of the viewport, so this offers no
  // position and takes no slot: it is the mark the moving horizon is read
  // against, not another object in the world.
  sprites_set_orientation();
  // The tail fin, on the two the stack gives up while it is there. Neither of
  // these offers a position and neither is an object: they are parts of the
  // aeroplane, and each is drawn in the one view it is in front of the camera
  // in. Both are cleared by sprites_stack_reset() above, so a frame that skips
  // them leaves their sprites disabled.
  sprites_set_fin();
  bm_sub_start();
  sprites_stack_commit();
  bm_sub_end(BM_SUB_SPRITES);
  bm_end(779, "UPD:");
}
#pragma optimize(pop)
