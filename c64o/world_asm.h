#ifndef WORLD_ASM_H
#define WORLD_ASM_H

#include <stdint.h>

#include "vec.h"
#ifndef __TARGET_TEST__
#include "gfx.h"
#include "mem.h"
#include "poly.h"
#include "world.h"
#else
static const uint8_t kGfxDotGround = 0x08;
#ifndef _ASM_POLY_MAX_VERTS_DEF
#define _ASM_POLY_MAX_VERTS_DEF
static const uint8_t kPolyMaxVertices = 6;
#endif
static const uint8_t kWorldMapHeight = 16;
static const uint8_t kWorldMapWidth = 32;
static const uint8_t kWorldMapObjStart = 7;
static const uint8_t kWorldObjDim = 9;
static const uint8_t kWorldObjVerts = 23;
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
extern int32_t flight_eye_x;
extern int32_t flight_eye_y;
extern int32_t flight_eye_z;
extern mat3_t world_cam;
extern const uint8_t kWorldMap[kWorldMapHeight][kWorldMapWidth / 2];
extern const uint8_t KWorldDotColors[7];
extern const uint8_t kWorldObjX[kWorldObjVerts];
extern const uint8_t kWorldObjY[kWorldObjVerts];
extern const uint8_t kWorldObjFirstVert[kWorldObjDim];
extern const uint8_t kWorldObjNumVerts[kWorldObjDim];
#endif

// The assembly half of world.cc, in world_asm.cc:
//   - _world_init_start_dx_dy(): altitude radius selection, X/Y axis splits
//     directly into contiguous _world_d4[18] (_world_dx4[0..8],
//     _world_dy4[9..17]), grouped zero-skipping Mitchell 16-entry precompute,
//     and camera-inverse transform of _world_p_start.
//   - _world_build_obj_verts(): builds a ground polygon's 3D vertices into
//     _world_poly_verts[] with an X-first all-behind (< 8) early reject.
//   - _world_draw_box_points(): inner Mitchell terrain-dot frustum, projection,
//     and plot loop (shared with _world_render_grid_walk()).
//   - _world_render_grid_walk(): outer X/Y grid walk with 256-byte kWorldMap
//     indexing and X-first bailout step.
//
// world.cc keeps exact C counterparts under #ifndef __OSCAR64__ for host builds
// and tests; test/target_test.cc verifies all four routines on the emulated
// 6510.

extern __zeropage uint8_t _world_grid_radius;
extern __zeropage uint8_t _world_start_cx;
extern __zeropage uint8_t _world_start_cy;
extern __zeropage vec3_t _world_p_start;
extern __zeropage vec3_t _world_dx_vec;
extern __zeropage vec3_t _world_dy_vec;
extern __zeropage int8_t _world_step_x;
extern __zeropage int8_t _world_step_y;
extern __zeropage vec3_t _world_vec_v;

// Contiguous split tables: [0..8] = _world_dx4, [9..17] = _world_dy4.
extern vec3_t _world_d4[18];
#define _world_dx4 (_world_d4)
#define _world_dy4 (_world_d4 + 9)

extern vec3_t _world_poly_verts[kPolyMaxVertices];

extern __striped int16_t _world_mitch_x[16];
extern __striped int16_t _world_mitch_y[16];
extern __striped int16_t _world_mitch_z[16];

// 0-arg callback from _world_render_grid_walk() into world.cc to draw the
// object whose index (map_type - kWorldMapObjStart) sits in _world_obj_idx.
extern uint8_t _world_obj_idx;
void _world_render_object_cur(void);

void _world_init_start_dx_dy(void);
uint8_t _world_build_obj_verts(uint8_t obj_idx);
void _world_draw_box_points(uint8_t start_idx, uint8_t num_points,
                            uint8_t color);
void _world_render_grid_walk(void);

#ifdef __OSCAR64__
#pragma compile("world_asm.cc")
#endif

#endif
