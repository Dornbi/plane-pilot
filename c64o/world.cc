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
#include "world_asm.h"

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
// so fpilot keeps them, and _world_render_object()'s _world_poly_verts, in the
// main screen's dead rows (mem.h scratch). _world_d4[0..8] is _world_dx4 and
// _world_d4[9..17] is _world_dy4 so 6502 asm can index both with X.
#ifdef __FPILOT__
#pragma bss(scratch)
#endif
vec3_t _world_d4[18];
vec3_t _world_poly_verts[kPolyMaxVertices];
#pragma bss(bss2)
__striped int16_t _world_mitch_x[16];
__striped int16_t _world_mitch_y[16];
__striped int16_t _world_mitch_z[16];

#ifndef __OSCAR64__
uint8_t _world_obj_idx;

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

static __noinline void _world_split_vec(vec3_t *v, vec3_t d9[9]) {
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

// Host C counterpart of world_asm.cc's _world_draw_box_points().
void _world_draw_box_points(uint8_t start_idx, uint8_t num_points,
                            uint8_t color) {
  uint8_t idx = start_idx & 0x0F;
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
}

// Radius 2 uses [0..2] (total 72 = 4 + 2*9 + 2*25); radius 3 uses [0..3]
// (total 96 = 4 + 2*9 + 25 + 49).
static const uint8_t kNumPoints3[] = {8, 4, 2, 1};
// Total = 108 = 2 + 25 (5*5) + 81 (9*9)
static const uint8_t kNumPoints4[] = {4, 2, 2, 1, 1};
static const uint8_t *_num_points_per_radius;

// One axis of the grid, 0 for x and 1 for y.
static __noinline int16_t _world_init_axis(uint8_t axis, int16_t grid_spacing) {
  const int16_t *cam = &world_cam.front.x + axis;
  vec3_t *dvec = axis ? &_world_dy_vec : &_world_dx_vec;
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

// Host C counterpart of world_asm.cc's _world_init_start_dx_dy().
void _world_init_start_dx_dy() {
  static const uint16_t kGridSpacing = 0x400;
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

  _world_p_start.x = _world_init_axis(0, grid_spacing);
  _world_p_start.y = _world_init_axis(1, grid_spacing);
  _world_p_start.z = -_down_shift(flight_eye_z);

  for (uint8_t i = 0; i < 16; ++i) {
    const vec3_t *dx =
        (const vec3_t *)((const uint8_t *)_world_dx4 + kMitchellPointsX[i]);
    const vec3_t *dy =
        (const vec3_t *)((const uint8_t *)_world_dy4 + kMitchellPointsY[i]);
    _world_mitch_x[i] = dx->x + dy->x;
    _world_mitch_y[i] = dx->y + dy->y;
    _world_mitch_z[i] = dx->z + dy->z;
  }

  vec_transform_inv(&world_cam, &_world_p_start, &_world_p_start);
}

// Host C counterpart of world_asm.cc's _world_build_obj_verts().
uint8_t _world_build_obj_verts(uint8_t obj_idx) {
  uint8_t num_verts = kWorldObjNumVerts[obj_idx];
  uint8_t k = kWorldObjFirstVert[obj_idx];
  bool any_in = false;
  for (uint8_t i = 0; i < num_verts; ++i, ++k) {
    const vec3_t *dx =
        (const vec3_t *)((const uint8_t *)_world_dx4 + kWorldObjX[k]);
    const vec3_t *dy =
        (const vec3_t *)((const uint8_t *)_world_dy4 + kWorldObjY[k]);
    int16_t vx = _world_vec_v.x + dx->x + dy->x;
    _world_poly_verts[i].x = vx;
    if (vx >= 8) {
      any_in = true;
    }
  }
  if (!any_in) {
    return 0;
  }
  k = kWorldObjFirstVert[obj_idx];
  for (uint8_t i = 0; i < num_verts; ++i, ++k) {
    const vec3_t *dx =
        (const vec3_t *)((const uint8_t *)_world_dx4 + kWorldObjX[k]);
    const vec3_t *dy =
        (const vec3_t *)((const uint8_t *)_world_dy4 + kWorldObjY[k]);
    _world_poly_verts[i].y = _world_vec_v.y + dx->y + dy->y;
    _world_poly_verts[i].z = _world_vec_v.z + dx->z + dy->z;
  }
  return num_verts;
}
#endif // !__OSCAR64__

void _world_render_object_cur(void) {
  uint8_t obj_idx = _world_obj_idx;
  uint8_t num_verts = _world_build_obj_verts(obj_idx);
  if (num_verts == 0) {
    return;
  }
  // Just the draw, not the vertex build above it, so this stays comparable
  // with the by-hand figure in docs/framerate.md.
  bm_sub_start();
  poly_draw_3d(_world_poly_verts, num_verts, kWorldObjChars[obj_idx],
               WORLD_OBJ_COLORS[obj_idx]);
  bm_sub_end(BM_SUB_POLY);
}

void _world_render_object(WorldMapType object_type) {
  _world_obj_idx = (uint8_t)(object_type - kWorldMapObjStart);
  _world_render_object_cur();
}

#ifndef __OSCAR64__
// Host C counterpart of world_asm.cc's _world_render_grid_walk().
void _world_render_grid_walk(void) {
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
        _world_draw_box_points(start_idx, num_points, WORLD_DOT_COLORS[map_type]);
      }
      if (y == (int8_t)_world_grid_radius) {
        break;
      }
      ++y;
      cy += _world_step_y;
      // Step along Y axis
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
}
#endif // !__OSCAR64__

__noinline void world_render_grid() {
  bm_model_start();
  _world_init_start_dx_dy();
  _world_render_grid_walk();
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
