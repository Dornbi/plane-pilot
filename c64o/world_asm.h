#ifndef WORLD_ASM_H
#define WORLD_ASM_H

#include <stdint.h>

#include "vec.h"
#ifndef __TARGET_TEST__
#include "gfx.h"
#include "mem.h"
#else
static const uint8_t kGfxDotGround = 0x08;
#ifndef _ASM_TEST_MEM_DEFS
#define _ASM_TEST_MEM_DEFS
static const uint8_t kScreenHeight = 25;
static const uint8_t kViewportHeight = 14;
extern __striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
extern __striped uint8_t *const mem_color_row_ptrs[kViewportHeight];
#endif
#endif

// The assembly half of world.cc, in world_asm.cc: Pass 138's inner Mitchell
// terrain-dot loop (_world_draw_box_points). world.cc holds the C version
// (#ifndef __OSCAR64__) for host builds and tests.

extern __striped int16_t _world_mitch_x[16];
extern __striped int16_t _world_mitch_y[16];
extern __striped int16_t _world_mitch_z[16];
extern vec3_t _world_vec_v;

// Projects and plots `num_points` Mitchell dots starting at `start_idx & 0x0F`
// around `_world_vec_v`. `color` is a palette index (< 8) or kGfxDotGround.
void _world_draw_box_points(uint8_t start_idx, uint8_t num_points,
                            uint8_t color);

#ifdef __TARGET_TEST__
#pragma compile("world_asm.cc")
#endif

#endif
