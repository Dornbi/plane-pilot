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

// Each of the model's magnitudes times k, rounded, and each pair's, for
// p = 1 .. 15: what _planes_mags() fills.
extern uint8_t _planes_px[16];
extern uint8_t _planes_pv[16];

// magnitude * k / 256 rounded, as trunc(2 * magnitude * k / 256), for every
// magnitude, then each pair's from its magnitude's. k is at most the size
// cap, so a byte.
void _planes_mags(uint8_t k);

// Each pair's screen offset, x from its axis's y and y from its z: negated at
// p, as is at 16 + p, and zero at 0 -- the terms planedef.h's kPlaneVert*
// index.
extern int8_t _planes_tx[32], _planes_ty[32];

// This frame's vertices relative to the centre, in screen pixels offset by
// 128; and the fill's, in buffer coordinates, which planes_fill() copies from
// the vertex cache's key.
extern uint8_t _planes_vx[kPlaneVertMax], _planes_vy[kPlaneVertMax];
// The fill's are the same arrays: nothing projects while a fill runs, and
// the fill copies its vertices from the key first, so the projection's are
// dead by then.
#define _planes_lx _planes_vx
#define _planes_ly _planes_vy

// Step 9's flat surfaces: _planes_vx and _planes_vy 0 .. 15, each the sum
// of its fore, left and up terms (planedef.h) plus 128.
void _planes_flat(void);

// The bounding box of vertices 0 .. count - 1, count at least 1.
extern uint8_t _planes_box_x0, _planes_box_x1, _planes_box_y0, _planes_box_y1;
void _planes_box(uint8_t count);

// Step 12: the vertices in buffer coordinates, 0 .. count - 1, into the
// vertex cache's key_x and key_y at `key`, 32 bytes apart. x is _planes_vx
// less bx, y is _planes_vy less by plus s9, the slide modulo 512, and each is
// halved when expanded. True if any differed from the key.
bool _planes_key(uint8_t *key, uint8_t count, uint8_t bx, uint8_t by, uint16_t s9,
                 uint8_t xs, uint8_t ys);

// The fuselage's three stations, centre (offset by 128) and radius, and its
// outline: each station offset by r * u along the unit normal u = (ux, uy)
// (8.8, |u| <= 256), as lib/planes.py smul() rounds it - stations 0 .. 2 on
// one side, then 2 .. 0 on the other - into _planes_vx and _planes_vy from
// index `at`.
extern uint8_t _planes_sx[3], _planes_sy[3], _planes_sr[3];
void _planes_tube(int16_t ux, int16_t uy, uint8_t at);

// The products of each axis's pairs (1 .. 15), each trunc(component * v / 256)
// as vec_fastmul8p8 truncates: _planes_tx from the axis's y, _planes_ty from
// its z, and 0 at index 0.
void _planes_axis_products(const mat3_t *axes);

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
