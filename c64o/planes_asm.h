#ifndef PLANES_ASM_H
#define PLANES_ASM_H

#include <stdint.h>

#include "planes.h"
#include "vec.h"

// The half of planes.cc that the C64 runs as assembly, in planes_asm.cc: the
// axis products and the polygon fill. planes.cc has the same functions in C
// for the host, where test/planes_test.cc holds them to lib/planes.py;
// test/target_test.cc holds the assembly to the same reference. The two
// follow each other step for step. Nothing outside the renderer uses this.

// Each pair's magnitude times k, for p = 1 .. 15.
extern uint8_t _planes_pv[16];

// Each pair's screen offset, x from its axis's y and y from its z: negated at
// p, as is at 16 + p, and zero at 0 -- the terms planedef.h's kPlaneVert*
// index.
extern int8_t _planes_tx[32], _planes_ty[32];

// This frame's vertices in buffer coordinates, for the fill.
extern uint8_t _planes_lx[kPlaneVertMax], _planes_ly[kPlaneVertMax];

// The products of axis a's pairs, first .. end - 1, each trunc(component * v
// / 256) as vec_fastmul8p8 truncates: _planes_tx from a's y, _planes_ty from
// its z.
void _planes_axis_products(const vec3_t *a, uint8_t first, uint8_t end);

// Where the fill draws: four sprite blocks 64 bytes apart from `back`, cols
// of them across, and rows at or below `height` skipped.
void _planes_fill_begin(uint8_t *back, uint8_t cols, uint8_t height);

// Edge-inclusive convex fill with vertices at pixel centres (lib/planes.py
// fill_poly()): vertices first .. first + count - 1 of _planes_lx, _planes_ly.
// x is inside the buffer; y may be below it.
void _planes_fill_poly(uint8_t first, uint8_t count);

// A row's edge masks, and where its byte j is: 0-2 in the left block, 3-5 in
// the one 64 bytes on.
static const uint8_t kPlaneLeftMask[8] = {0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01};
static const uint8_t kPlaneRightMask[8] = {0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF};
static const uint8_t kPlaneByteOffset[6] = {0, 1, 2, 64, 65, 66};

#endif
