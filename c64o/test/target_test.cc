// Arithmetic that only tells the truth on a 6510.
//
// Every other suite in this directory is compiled by g++, where `int` is 32
// bits. oscar64's is 16, and C promotes every narrower operand to `int` before
// it does anything, so an intermediate that wraps on the target does not wrap
// on the host. A host test then compares against arithmetic the C64 never
// performed, and passes.
//
// c64o/test/int16.h carries the measurements and the i16() helpers that let a
// host test model the target's width by hand. This file is the other half: the
// cases where modelling is not good enough and the answer has to come off the
// real chip. It is built by oscar64 and run in VICE - `make -C c64o test` does
// both when x64sc is on the PATH, and skips with a line when it is not.
//
// **There is no reference arithmetic in here.** That is the point. The expected
// values below are literals, each one a number a 6510 produces and an x86 does
// not, so nothing in this file can be quietly rewritten into 32-bit arithmetic
// by the compiler that reads it. Where a case does need a computed reference,
// it computes it in plain `int` - which *is* 16 bits here, so it wraps for free
// and needs no helper.
//
// Adding a case: give it the next id, add it to the table in the comment above
// main(), and keep the whole file inside a few million cycles so the VICE run
// stays under a second of wall clock in warp.
//
//   id  what
//   ---------------------------------------------------------------------
//    1  poly.cc's old rounding term, (sx + 2) >> 2 at the saturation point
//    2  poly.cc's current rounding term, same input
//    3  the int8_t negate-then-shift of mul_test.cc
//    4  vec_fastmul8p8 against a 16-bit product, at the edges
//    5  render.cc's _mul over the roll table's full int8_t range
//    6  planes.cc against lib/planes.py: frame fields and every bitmap byte,
//       reported as id 1000 + the case number
//    7  planes.cc against lib/planes.py over a few hundred more frames, by
//       checksum, reported as id 2000 + the case number
//    8  vec_div8p8's shift for a divisor of 64 against its own loop
//    9  vec_fastmul8p8 with an operand below 256 against the 32-bit product
//   10  poly_asm.cc _poly_frustum_all_out against 16-bit C reference
//   11  poly_asm.cc _poly_scan_lines2 against byte-for-byte C reference
//   12  world_asm.cc _world_draw_box_points against byte-for-byte C reference
//   13  render_asm.cc _render_snap_center_asm against C roll_get_dist reference
//   14  render_asm.cc _render_flush_span main/alt $E800/$EC00 sled & color RAM
//   15  poly_asm.h _poly_bounds_and_clear & _poly_trace_edges against C ref
//   16  planes_asm.cc split project/reslide/fill, place cuts & slid-out clear
//   17  opponent_asm.cc _opponent_world_delta, _opponent_toward, _opponent_pos,
//       _opponent_latch_shown, _opponent_copy_cs & _opponent_steer against C ref
//   18  gun_asm.cc _gun_track_all, _gun_rel_from, _gun_hits, _gun_collides,
//       _gun_store_enemy_round, _gun_step_rounds, _gun_shown, _gun_dir,
//       _gun_view, _gun_view_round & _gun_vel_round against C ref
//   19  vec_asm.cc vec_div8p8 / vec_frac16 / vec_fracn & planes_asm.cc planes_centre
//   20  sprites_asm.cc sprites_stack_add, sprites_stack_commit,
//       _sprites_set_instrument_sprite & sprites_set_alt against C ref
//   21  clouds_asm.cc _clouds_build_basis, _clouds_dot, _clouds_apply_offset,
//       _clouds_scan_init & _clouds_scan_next against C ref
//   22  world_asm.cc _world_init_start_dx_dy, _world_build_obj_verts &
//       _world_render_grid_walk against 16-bit C ref
//   23  poly_asm.cc _poly_project_vertices against 16-bit C ref
//   24  box_asm.cc _box_prepare_fill & _box_draw_asm against 16-bit C ref
//   25  vec_asm.cc fastmul fast-paths, fastsqr, dot, transform_inv, turn_xy,
//       cross, rotate_step, normalize & orthonormalize against C ref
//   26  opponent_asm.cc _opponent_move, _opponent_ahead, _opponent_axes &
//       _opponent_cull_slot against 16-bit C ref
//   27  render_asm.cc _render_pull_and_snap_asm & _render_fill_sky_ground_asm
//       against byte-for-byte C reference (level + tilted, main/alt, sky rows)
//   28  flight_asm.cc _flight_physics_move_asm, _flight_fuel_step_asm &
//       _flight_turn_rot_asm against pure-C arcade flight model reference
//       (in target_planes_test.prg alongside Tests 11-12)
//   29  panel_asm.cc panel_update_instruments (all 6 dial arms + caching,
//       closed-form roll, pitch bar, heading strip, 5 lamps, ammo counter &
//       view states) against pure-C reference (in target_planes_test.prg)
//   30  sound_asm.cc sound_update (LFSR roughness, PWM triangle sweep,
//       quarter-square engine jitter, wind speed clamp/gating, Voice 3
//       arbitration/priority/stall warble/crash sweep, volume clamp, and
//       sound_dirty) against pure-C reference (in target_planes_test.prg)
//   31  opponent_asm.cc _opponent_ai_step_asm, _opponent_fly_and_move_asm,
//       _opponent_fall_asm & _opponent_cap_asm against 16-bit C references
//       (in target_planes_test.prg)

#include <stdint.h>

// Test 7's cases + asm suites need more room than oscar64's default layout
// leaves: code and data up to $D000 (main() banks out BASIC + KERNAL ROM at
// entry), stack in $0200-$03FF, bss in $0400-$07FF, and test buffers in
// $E800-$FDB4.
#pragma heapsize(0)
#pragma stacksize(0x200)
#pragma region(main, 0x0854, 0xD000, , , {code, data})
#pragma region(stackregion, 0x0200, 0x0400, , , {heap, stack})
#pragma section(gun_bss, 0, , , bss)
#pragma section(opp_bss, 0, , , bss)
#pragma section(cld_bss, 0, , , bss)
#pragma region(bssregion, 0x0400, 0x0800, , , {bss})
#pragma region(oppbssreg, 0xE000, 0xE800, , , {opp_bss, cld_bss, gun_bss})
// And the zero page the programs under test get from -xz: vec_asm.cc's
// routines keep their operands there.
#pragma region(zeropage, 0x78, 0x100, , , {zeropage})

#include "../planedef.h"
#include "../planes.h"
#include "../planes_asm.h"
#include "../vec.h"
#ifdef __TARGET_TEST_PLANES__
#include "../flight_asm.h"
#include "../opponent_asm.h"
#include "../panel_asm.h"
#include "../sound_asm.h"
#include "planes_target_cases.h"
#else
#include "../poly_asm.h"
#include "../render_asm.h"
#include "../world_asm.h"
#include "../box_asm.h"
static const int16_t kTargetCx0 = 160, kTargetCy0 = 56;
static const int16_t kTargetCut1 = 89, kTargetCut2 = 68;
#endif

#pragma optimize(push, nooutline)

// If this ever fails the file is being built for something that is not the
// target, and every literal below is wrong.
static_assert(sizeof(int) == 2, "target_test must be built by oscar64");

// volatile, or oscar64 drops a global that is only ever written - symbol and
// all, so vice_dump.sh cannot even find it. docs/emulator.md.
volatile uint16_t g_failures;
volatile uint16_t g_first_fail;
volatile int16_t g_first_got;
volatile int16_t g_first_want;
volatile uint16_t g_cases;

static __noinline void expect(uint16_t id, int16_t got, int16_t want) {
  ++g_cases;
  if (got == want) {
    return;
  }
  if (g_failures == 0) {
    g_first_fail = id;
    g_first_got = got;
    g_first_want = want;
  }
  ++g_failures;
}

static uint16_t _cur_id;
static __noinline void expect_id(int16_t got, int16_t want) {
  expect(_cur_id, got, want);
}

// Opaque to the optimiser, so a case cannot be folded into its own answer.
#ifndef __TARGET_TEST_PLANES__
volatile int16_t v_sx = 32767;
volatile int8_t v_cy = -128;

// 1 and 2. vec_div8p8 saturates at 32767 and a vertex just past the near plane
// saturates it routinely, so this input is reachable rather than synthetic.
//
// The old form adds 2 first: on a 16-bit int that is 32769, which wraps to
// -32767, and the vertex lands on the far side of the screen. One polygon then
// covered the whole viewport. The host cannot see it - there the sum is 32769
// and the result 8192, which is also what the *fixed* form gives. So the two
// forms are indistinguishable under g++ and differ by 16,384 here.
static void test_poly_rounding(void) {
  const int16_t sx = v_sx;
  expect(1, (int16_t)((sx + 2) >> 2), -8192);
  expect(2, (int16_t)((sx >> 2) + ((sx >> 1) & 1)), 8192);
}

// 3. The conversion of render.cc's horizon term shipped broken because the
// int8_t operand was negated before the shift: -(-128) is 128, which does not
// fit an int8_t and wraps straight back to -128. Diagonal bands through the sky.
static void test_int8_negate_then_shift(void) {
  const int8_t cy = v_cy;
  expect(3, (int16_t)((int16_t)(-cy) << 8) >> 8, -128);
}

// 4. vec_fastmul8p8 builds its product from the magnitudes and applies the sign
// last, so it wraps sign-magnitude where a plain product wraps two's
// complement. The two agree everywhere the product fits; these are the corners
// where it does not, and the literals are what the routine actually returns.
static void test_fastmul_edges(void) {
  expect(4, vec_fastmul8p8(32000, 256), 32000);
  expect(4, vec_fastmul8p8(-32000, 256), -32000);
  expect(4, vec_fastmul8p8(256, 256), 256);
  expect(4, vec_fastmul8p8(-256, -256), 256);
  expect(4, vec_fastmul8p8(4096, -2048), -32768);
}

// 5. render.cc's _mul(a, b) is vec_fastmul8p8(a, b << 8), standing in for the
// 16-bit product a * b. mul_test.cc sweeps this on the host against an i16()
// reference; here the reference is just `int`, which is 16 bits, so it wraps by
// itself and there is nothing to get wrong.
//
// A subset of the roll table rather than all 60 rows, and a stride over dx
// rather than all 65,536 values: this has to run in an emulator, and the point
// is that the arithmetic agrees at the edges, not an exhaustive sweep. The
// exhaustive one lives on the host, where it is affordable.
static const int8_t kRollB[] = {16, 8, 6, 4, 2, 1, -1, -2, -4, -6, -8, -16};

static void test_mul_against_16bit_product(void) {
  for (uint8_t i = 0; i < sizeof(kRollB); ++i) {
    const int8_t b = kRollB[i];
    for (int32_t dx = -32768; dx <= 32767; dx += 257) {
      const int16_t a = (int16_t)dx;
      const int16_t got = vec_fastmul8p8(a, (int16_t)b << 8);
      // `int` is 16 bits here, so this product wraps exactly as the C64's did.
      const int16_t want = (int16_t)(a * b);
      expect(5, got, want);
    }
  }
}
#endif // !__TARGET_TEST_PLANES__
#pragma optimize(pop)
#pragma optimize(push, outline)

// 6. The host suite holds planes.cc to lib/planes.py over four thousand cases,
// and the first version passed it and drew only the left edge of every
// polygon on the C64: oscar64 mis-compiled the endpoint swap in _fill_poly
// (bugs/). So a few dozen cases run here too, with every expected byte from
// the reference, rendered through a front and a back buffer as a program
// would and compared byte for byte.
static uint8_t (*const _planes_sets)[4][kPlaneBlockStride] =
    (uint8_t (*)[4][kPlaneBlockStride])0xF000;
#ifdef __TARGET_TEST_PLANES__
static uint8_t _planes_dot[kPlaneBlockBytes];
static planes_state_t *const _tp_state = (planes_state_t *)0xFD00;
#pragma bss(opp_bss)
static planes_frame_t _tp_frame;
static mat3_t _tp_axes;
static vec3_t _tp_c;
#pragma bss(bss)

static void test_planes(void) {
  const planes_view_t view = {kTargetCx0, kTargetCy0, kTargetCut1, kTargetCut2,
                              kPlaneKMax, kPlaneKnModel};
  planes_state_init(_tp_state);
  planes_dot_bitmap(_planes_dot);
  uint8_t front = 0;
  for (uint8_t n = 0; n < kPlaneTargetCount; ++n) {
    const plane_target_case_t *pc = &kPlaneTargetCases[n];
    _cur_id = 1000 + n;
    if (pc->fresh) {
      planes_state_init(_tp_state);
    }
    _tp_c = make_vector(pc->c[0], pc->c[1], pc->c[2]);
    _tp_axes.front = make_vector(pc->axes[0][0], pc->axes[0][1], pc->axes[0][2]);
    _tp_axes.left = make_vector(pc->axes[1][0], pc->axes[1][1], pc->axes[1][2]);
    _tp_axes.up = make_vector(pc->axes[2][0], pc->axes[2][1], pc->axes[2][2]);
    uint8_t *back = _planes_sets[front ^ 1][0];
    planes_render(_tp_state, &view, &_tp_c, &_tp_axes, back, &_tp_frame);

    expect_id(_tp_frame.hidden, pc->hidden);
    if (pc->hidden) {
      continue;
    }
    expect_id(_tp_frame.level, pc->level);
    expect_id(_tp_frame.ys, pc->ys);
    expect_id(_tp_frame.cols, pc->cols);
    expect_id(_tp_frame.rows, pc->rows);
    expect_id(_tp_frame.x, pc->x);
    expect_id(_tp_frame.y, pc->y);
    expect_id(_tp_frame.slid, pc->slid);
    expect_id(_tp_frame.clamped ? 1 : 0, pc->clamped);
    expect_id(_tp_frame.cached ? 1 : 0, pc->cached);
    const uint8_t *want = kPlaneTargetBytes + pc->bytes;
    if (_tp_frame.level == kPlaneLevelDot) {
      for (uint8_t i = 0; i < kPlaneBlockBytes; ++i) {
        expect_id(_planes_dot[i], want[i]);
      }
      continue;
    }
    if (!_tp_frame.cached) {
      front ^= 1;
    }
    for (uint8_t b = 0; b < _tp_frame.cols * _tp_frame.rows; ++b) {
      for (uint8_t i = 0; i < kPlaneBlockBytes; ++i) {
        expect_id(_planes_sets[front][b][i], want[b * kPlaneBlockBytes + i]);
      }
    }
  }
}

// 7. The same through a few hundred frames, compared by checksum rather than
// byte by byte so that they fit: attitudes and distances, an approach through
// the hysteresis, and drifts through both caches and the slide. This is what
// holds planes.cc's assembly to the reference; the host test cannot run it.
static uint8_t _sum1, _sum2;

static __noinline void _sum(uint8_t b) {
  _sum1 = (uint8_t)(_sum1 + b);
  _sum2 = (uint8_t)(_sum2 + _sum1);
}

static void _sum_bytes(const uint8_t *p, uint8_t n) {
  for (uint8_t i = 0; i < n; ++i) {
    _sum(p[i]);
  }
}

static void test_planes_sweep(void) {
  const planes_view_t view = {kTargetCx0, kTargetCy0, kTargetCut1, kTargetCut2,
                              kPlaneKMax, kPlaneKnModel};
  planes_state_init(_tp_state);
  uint8_t front = 0;
  for (uint16_t n = 0; n < kPlaneSweepCount; ++n) {
    const plane_sweep_case_t *pc = &kPlaneSweep[n];
    if (pc->fresh) {
      planes_state_init(_tp_state);
    }
    _tp_c = make_vector(pc->c[0], pc->c[1], pc->c[2]);
    _tp_axes.front = make_vector(pc->axes[0][0], pc->axes[0][1], pc->axes[0][2]);
    _tp_axes.left = make_vector(pc->axes[1][0], pc->axes[1][1], pc->axes[1][2]);
    _tp_axes.up = make_vector(pc->axes[2][0], pc->axes[2][1], pc->axes[2][2]);
    uint8_t *back = _planes_sets[front ^ 1][0];
    planes_render(_tp_state, &view, &_tp_c, &_tp_axes, back, &_tp_frame);

    _sum1 = _sum2 = 0;
    _sum(_tp_frame.hidden);
    if (_tp_frame.hidden == kPlaneShown) {
      _sum(_tp_frame.level);
      _sum(_tp_frame.ys);
      _sum(_tp_frame.cols);
      _sum(_tp_frame.rows);
      _sum((uint8_t)_tp_frame.x);
      _sum((uint8_t)((uint16_t)_tp_frame.x >> 8));
      _sum((uint8_t)_tp_frame.y);
      _sum((uint8_t)((uint16_t)_tp_frame.y >> 8));
      _sum(_tp_frame.slid);
      _sum(_tp_frame.clamped ? 1 : 0);
      _sum(_tp_frame.cached ? 1 : 0);
      if (_tp_frame.level == kPlaneLevelDot) {
        _sum_bytes(_planes_dot, kPlaneBlockBytes);
      } else {
        if (!_tp_frame.cached) {
          front ^= 1;
        }
        for (uint8_t b = 0; b < _tp_frame.cols * _tp_frame.rows; ++b) {
          _sum_bytes(_planes_sets[front][b], kPlaneBlockBytes);
        }
      }
    }
    expect((uint16_t)(2000 + n), (int16_t)(((uint16_t)_sum2 << 8) | _sum1), (int16_t)pc->check);
  }
}
#else

// 8. vec_div8p8 answers a divisor of 64 or -64 with a shift instead of its
// loop (the near clip's lifted vertices, poly.cc). 2a / 128 is the same
// quotient and takes the loop, so the routine is its own reference; the
// literals are the ends of the range and both sides of where it saturates.
static void test_div_by_64(void) {
  _cur_id = 8;
  for (int16_t a = -16383; a <= 16383 - 109; a += 109) {
    expect_id(vec_div8p8(a, 64), vec_div8p8((int16_t)(a << 1), 128));
    expect_id(vec_div8p8(a, -64), vec_div8p8((int16_t)(a << 1), -128));
  }
  expect_id(vec_div8p8(0, 64), 0);
  expect_id(vec_div8p8(63, 64), 252);
  expect_id(vec_div8p8(-100, 64), -400);
  expect_id(vec_div8p8(8191, 64), 32764);
  expect_id(vec_div8p8(8192, 64), 32767);
  expect_id(vec_div8p8(-8191, -64), 32764);
  expect_id(vec_div8p8(-8192, 64), -32767);
  expect_id(vec_div8p8(32767, -64), -32767);
  expect_id(vec_div8p8(-32768, 64), -32767);
}

// 9. vec_fastmul8p8 skips the high byte's two partial products when |a| is
// below 256, and the third when |b| is too. The reference is the 32-bit
// product, truncated toward zero as the routine truncates; with one operand
// that short it always fits. Each pair goes in both orders, so the four
// product path is held to the same answers.
static const int16_t kShortA[] = {1, 3, 100, 128, 255, -1, -77, -255};

static void _fastmul_short(int16_t a, int16_t b) {
  int32_t p = (int32_t)a * b;
  const int16_t want = (int16_t)(p < 0 ? -((-p) >> 8) : (p >> 8));
  expect_id(vec_fastmul8p8(a, b), want);
  expect_id(vec_fastmul8p8(b, a), want);
}

static void test_fastmul_short(void) {
  _cur_id = 9;
  for (uint8_t i = 0; i < sizeof(kShortA) / sizeof(kShortA[0]); ++i) {
    const int16_t a = kShortA[i];
    for (int32_t b = -32768; b <= 32767; b += 1021) {
      _fastmul_short(a, (int16_t)b);
    }
    for (int16_t b = -255; b <= 255; b += 17) {
      _fastmul_short(a, b);
    }
  }
}

// ---------------------------------------------------------------------------
// Backing arrays and C references for poly_asm.cc, world_asm.cc, render_asm.cc
// ---------------------------------------------------------------------------
// Place test scratch/reference screens in $F200-$FBAF (under banked-out KERNAL
// ROM).
static uint8_t *const test_scr = (uint8_t *)0xF200;
static uint8_t *const test_clr = (uint8_t *)0xF480;
static uint8_t *const ref_scr = (uint8_t *)0xF700;
static uint8_t *const ref_clr = (uint8_t *)0xF980;

#pragma bss(opp_bss)
__striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
#pragma bss(bss)
__striped uint8_t *const mem_color_row_ptrs[kViewportHeight] = {
    (uint8_t *)0xF480 + 0 * 40,  (uint8_t *)0xF480 + 1 * 40,
    (uint8_t *)0xF480 + 2 * 40,  (uint8_t *)0xF480 + 3 * 40,
    (uint8_t *)0xF480 + 4 * 40,  (uint8_t *)0xF480 + 5 * 40,
    (uint8_t *)0xF480 + 6 * 40,  (uint8_t *)0xF480 + 7 * 40,
    (uint8_t *)0xF480 + 8 * 40,  (uint8_t *)0xF480 + 9 * 40,
    (uint8_t *)0xF480 + 10 * 40, (uint8_t *)0xF480 + 11 * 40,
    (uint8_t *)0xF480 + 12 * 40, (uint8_t *)0xF480 + 13 * 40};
bool mem_using_alt_buffer;

// _poly_clip3_buf, _poly_min_x, _poly_max_x are defined in poly_asm.cc.

__zeropage uint8_t _world_grid_radius;
__zeropage uint8_t _world_start_cx;
__zeropage uint8_t _world_start_cy;
__zeropage vec3_t _world_p_start;
__zeropage vec3_t _world_dx_vec;
__zeropage vec3_t _world_dy_vec;
__zeropage int8_t _world_step_x;
__zeropage int8_t _world_step_y;
__zeropage vec3_t _world_vec_v;
#pragma bss(cld_bss)
vec3_t _world_d4[18];
vec3_t _world_poly_verts[kPolyMaxVertices];
#pragma bss(bss)
__striped int16_t _world_mitch_x[16];
__striped int16_t _world_mitch_y[16];
__striped int16_t _world_mitch_z[16];

// Minimal test world map + object tables matching world_map.cc layouts.
const uint8_t KWorldDotColors[7] = {0, kGfxDotGround, 0, 1, 3, 6, 7};
const uint8_t kWorldObjX[kWorldObjVerts] = {
    0,  48, 48, 0,  0,  24, 48, 12, 0,  12, 48, 18,
    6,  30, 42, 36, 12, 0,  12, 36, 48, 30, 12};
const uint8_t kWorldObjY[kWorldObjVerts] = {
    24, 24, 18, 18, 12, 0,  30, 48, 36, 0,  18, 48,
    18, 6,  24, 36, 42, 18, 0,  6,  24, 42, 48};
const uint8_t kWorldObjFirstVert[kWorldObjDim] = {0, 4, 4, 8, 8, 12, 17, 8, 17};
const uint8_t kWorldObjNumVerts[kWorldObjDim] = {4, 4, 4, 4, 4, 5, 6, 4, 6};
// Mix of MAP_NOTHING (0), dot types (1..6), and object types (7..15).
const uint8_t kWorldMap[kWorldMapHeight][kWorldMapWidth / 2] = {
    {0x10, 0x23, 0x74, 0x85, 0x96, 0x10, 0x20, 0x31, 0x42, 0x53, 0x64, 0x75,
     0x86, 0x90, 0xA1, 0xB2},
    {0xC3, 0xD4, 0xE5, 0xF6, 0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x60, 0x71,
     0x82, 0x93, 0xA4, 0xB5},
    {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
     0xDD, 0xEE, 0xFF, 0x00},
    {0x07, 0x18, 0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90, 0x01, 0x12,
     0x23, 0x34, 0x45, 0x56},
    {0x21, 0x43, 0x65, 0x07, 0x81, 0xA2, 0xC3, 0xE4, 0x15, 0x36, 0x50, 0x71,
     0x92, 0xB3, 0xD4, 0xF5},
    {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x01, 0x23, 0x45, 0x67,
     0x89, 0xAB, 0xCD, 0xEF},
    {0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x00, 0x71, 0x82, 0x93, 0xA4, 0xB5,
     0xC6, 0xD0, 0xE1, 0xF2},
    {0x00, 0x11, 0x02, 0x13, 0x04, 0x15, 0x06, 0x17, 0x08, 0x19, 0x0A, 0x1B,
     0x0C, 0x1D, 0x0E, 0x1F},
    {0x10, 0x23, 0x74, 0x85, 0x96, 0x10, 0x20, 0x31, 0x42, 0x53, 0x64, 0x75,
     0x86, 0x90, 0xA1, 0xB2},
    {0xC3, 0xD4, 0xE5, 0xF6, 0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x60, 0x71,
     0x82, 0x93, 0xA4, 0xB5},
    {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
     0xDD, 0xEE, 0xFF, 0x00},
    {0x07, 0x18, 0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90, 0x01, 0x12,
     0x23, 0x34, 0x45, 0x56},
    {0x21, 0x43, 0x65, 0x07, 0x81, 0xA2, 0xC3, 0xE4, 0x15, 0x36, 0x50, 0x71,
     0x92, 0xB3, 0xD4, 0xF5},
    {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x01, 0x23, 0x45, 0x67,
     0x89, 0xAB, 0xCD, 0xEF},
    {0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x00, 0x71, 0x82, 0x93, 0xA4, 0xB5,
     0xC6, 0xD0, 0xE1, 0xF2},
    {0x00, 0x11, 0x02, 0x13, 0x04, 0x15, 0x06, 0x17, 0x08, 0x19, 0x0A, 0x1B,
     0x0C, 0x1D, 0x0E, 0x1F},
};

// Records visible object callbacks from _world_render_grid_walk() (after
// _world_build_obj_verts() + 3D frustum test, matching poly_draw_3d()).
static volatile uint16_t _test_wg_obj_hash;
static volatile uint8_t _test_wg_obj_cnt;

static bool _test_poly_visible(const vec3_t *v, uint8_t n) {
  uint8_t out3 = 0x0f;
  bool any_in = false;
  for (uint8_t i = 0; i < n; ++i) {
    int32_t vx = v[i].x;
    int32_t vy = v[i].y;
    int32_t vz4 = (int32_t)v[i].z << 2;
    if (vx >= 8) {
      any_in = true;
    }
    uint8_t c = 0;
    if (vy > vx) {
      c |= 1;
    }
    if (vy < -vx) {
      c |= 2;
    }
    if (vz4 > vx) {
      c |= 4;
    }
    if (vz4 < -vx) {
      c |= 8;
    }
    out3 &= c;
  }
  return any_in && out3 == 0;
}

void _world_render_object_cur(void) {
  uint8_t n = _world_build_obj_verts(_world_obj_idx);
  if (n && _test_poly_visible(_world_poly_verts, n)) {
    ++_test_wg_obj_cnt;
    _test_wg_obj_hash =
        (uint16_t)((_test_wg_obj_hash * 31) ^ _world_obj_idx ^
                   (uint16_t)_world_poly_verts[0].x ^
                   (uint16_t)_world_poly_verts[0].y);
  }
}

uint8_t _render_sky_rows;
int16_t render_cx_pixels;
int16_t render_cy_pixels;
int8_t render_cx_chars;
int8_t render_cy_chars;
bool render_alt_box;
int8_t render_alt_shift_x;
int8_t render_alt_shift_y;
uint8_t render_sky_rows;
int8_t roll_dx;
int8_t roll_dy;
int16_t roll_dx_div_dy;
uint8_t roll_period;
bool roll_x_is_major;
bool roll_shift_2chars;
int8_t roll_ex_dy[8];
int8_t roll_ey_dx[8];

static void _init_row_ptrs(void) {
  for (uint8_t r = 0; r < kScreenHeight; ++r) {
    mem_screen_row_ptrs[r] = test_scr + r * 40;
  }
}

static __noinline void _check_screens(uint8_t id) {
  _cur_id = id;
  for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
    expect_id(test_scr[i], ref_scr[i]);
    expect_id(test_clr[i], ref_clr[i]);
  }
}

static void _clear_test_screens(uint8_t scr_val, uint8_t clr_val) {
  for (uint16_t i = 0; i < kScreenHeight * 40; ++i) {
    test_scr[i] = scr_val;
  }
  for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
    test_clr[i] = clr_val;
    ref_scr[i] = scr_val;
    ref_clr[i] = clr_val;
  }
}

// 10. poly_asm.cc _poly_frustum_all_out against 16-bit C reference.
static bool _ref_frustum_all_out(uint8_t end3) {
  uint8_t out3 = 0x0f;
  for (uint8_t o = 0; o != end3; o += sizeof(vec3_t)) {
    const vec3_t *v = (const vec3_t *)((const uint8_t *)_poly_clip3_buf + o);
    uint16_t ux = (uint16_t)v->x;
    uint16_t zx = ux >> 2;
    uint16_t yx = ux - zx;
    int16_t vy = v->y;
    int16_t vz = v->z;
    uint8_t c = 0;
    if ((uint16_t)_abs16(vy) > yx) {
      c = vy < 0 ? 2 : 1;
    }
    if ((uint16_t)_abs16(vz) > zx) {
      c |= vz < 0 ? 8 : 4;
    }
    out3 &= c;
    if (out3 == 0) {
      break;
    }
  }
  return out3 != 0;
}

static void test_poly_frustum_asm(void) {
  _cur_id = 10;
  static const int8_t kCases8[9][3][3] = {
      {{100, 101, 0}, {120, 125, 0}, {50, 51, 0}},    // all y > x
      {{100, -101, 0}, {120, -125, 0}, {50, -51, 0}}, // all y < -x
      {{100, 0, 26}, {120, 0, 31}, {40, 0, 11}},      // all z > x>>2
      {{100, 0, -26}, {120, 0, -31}, {40, 0, -11}},   // all z < -(x>>2)
      {{100, 0, 25}, {120, 0, 31}, {40, 0, 11}},      // z == x>>2 boundary
      {{100, 120, 0}, {100, -120, 0}, {100, 5, 0}},   // straddling x/y
      {{20, 25, 0}, {22, 26, 0}, {18, 20, 0}},        // scaled * 1000 (> 16k)
      {{100, 76, 0}, {120, 91, 0}, {40, 31, 0}},      // all y > 0.75*x
      {{100, 75, 0}, {120, 91, 0}, {40, 31, 0}},      // y == 0.75*x boundary
  };
  for (uint8_t c = 0; c < 9; ++c) {
    int16_t scale = (c == 6) ? 1000 : 1;
    for (uint8_t i = 0; i < 3; ++i) {
      _poly_clip3_buf[i] = make_vector(kCases8[c][i][0] * scale,
                                       kCases8[c][i][1] * scale,
                                       kCases8[c][i][2] * scale);
    }
    expect_id(_poly_frustum_all_out(3 * sizeof(vec3_t)) ? 1 : 0,
              _ref_frustum_all_out(3 * sizeof(vec3_t)) ? 1 : 0);
  }
}

// 11. poly_asm.cc _poly_scan_lines2 against C reference byte-for-byte.
static void _ref_scan_lines2(uint8_t fill_char_start_idx, uint8_t color,
                             uint8_t py_start, uint8_t py_end) {
  for (uint8_t py = py_start; py <= py_end; ++py) {
    uint8_t sy = py << 1;
    uint8_t t_min = _poly_min_x[sy];
    uint8_t b_min = _poly_min_x[sy + 1];
    uint8_t min_x = _min8(t_min, b_min);
    if (min_x == kScreenWidth * 2) {
      continue;
    }
    uint8_t t_max = _poly_max_x[sy];
    uint8_t b_max = _poly_max_x[sy + 1];
    uint8_t *row = ref_scr + py * 40;
    uint8_t *color_row = ref_clr + py * 40;
    uint8_t min_px = min_x >> 1;
    uint8_t max_px = _max8(t_max, b_max) >> 1;
    uint8_t overlap_start = (uint8_t)(_max8(t_min, b_min) + 1) >> 1;
    uint8_t overlap_stop = (uint8_t)(_min8(t_max, b_max) + 1) >> 1;
    if (overlap_stop > overlap_start) {
      uint8_t cnt = overlap_stop - overlap_start;
      if (color) {
        _poly_set_color_line(row + overlap_start, fill_char_start_idx | 0xF,
                             color_row + overlap_start, color, cnt);
      } else {
        _poly_set_line(row + overlap_start, fill_char_start_idx | 0xF, cnt);
      }
    } else {
      overlap_start = kViewportWidth;
    }
    for (uint8_t px = min_px; px <= max_px; ++px) {
      if (px == overlap_start) {
        px = overlap_stop - 1;
        continue;
      }
      if (row[px] < kCharSolidGround) {
        continue;
      }
      uint8_t mask = 0;
      uint8_t sx_left = px << 1;
      uint8_t sx_right = sx_left + 1;
      if (sx_left >= t_min && sx_left <= t_max) {
        mask |= 1;
      }
      if (sx_right >= t_min && sx_right <= t_max) {
        mask |= 2;
      }
      if (sx_left >= b_min && sx_left <= b_max) {
        mask |= 4;
      }
      if (sx_right >= b_min && sx_right <= b_max) {
        mask |= 8;
      }
      if (mask) {
        row[px] = fill_char_start_idx + mask;
        if (color) {
          color_row[px] = color;
        }
      }
    }
  }
}

static void test_poly_scan_lines2_asm(void) {
  _cur_id = 11;
  _init_row_ptrs();
  for (uint8_t color_pass = 0; color_pass < 2; ++color_pass) {
    uint8_t col = color_pass ? 0x0B : 0;
    uint8_t base_ch = color_pass ? 160 : 176;
    _clear_test_screens(kCharSolidGround, 0x55);
    for (uint8_t i = 0; i < 2 * kViewportHeight; ++i) {
      _poly_min_x[i] = kScreenWidth * 2;
      _poly_max_x[i] = 0;
    }
    // Row 2: wide core + both fringes, plus non-ground guard cell inside core.
    _poly_min_x[4] = 5;
    _poly_max_x[4] = 16;
    _poly_min_x[5] = 7;
    _poly_max_x[5] = 15;
    test_scr[2 * 40 + 7] = 42;
    ref_scr[2 * 40 + 7] = 42;
    // Row 3: left empty (gap row), Row 4: narrow single-subrow sliver (no core).
    _poly_min_x[8] = 10;
    _poly_max_x[8] = 11;
    // Row 5: single pixel dot in bottom subrow only.
    _poly_min_x[11] = 21;
    _poly_max_x[11] = 21;

    _poly_scan_lines2(base_ch, col, 2, 5);
    _ref_scan_lines2(base_ch, col, 2, 5);
    _check_screens(11);
  }
}

// 12. world_asm.cc _world_draw_box_points against C reference.
static void _ref_world_draw_box_points(uint8_t start_idx, uint8_t num_points,
                                       uint8_t color) {
  uint8_t idx = start_idx & 0x0F;
  for (uint8_t i = num_points;;) {
    vec_v.x = _world_vec_v.x + _world_mitch_x[idx];
    if (vec_viewport_ahead()) {
      vec_v.y = _world_vec_v.y + _world_mitch_y[idx];
      if (vec_viewport_beside()) {
        vec_v.z = _world_vec_v.z + _world_mitch_z[idx];
        if (vec_viewport_project()) {
          int16_t px = 160 - vec_sx;
          int16_t py = 56 - vec_sy;
          if ((uint16_t)px < 320 && (uint16_t)py < 112) {
            uint8_t row = (uint8_t)(py >> 3);
            uint8_t col = (uint8_t)(px >> 3);
            uint16_t off = (uint16_t)row * 40 + col;
            if (ref_scr[off] == kCharSolidGround) {
              uint8_t bits =
                  (uint8_t)((px >> 1) & 3) | (uint8_t)((py & 6) << 1);
              if (color == kGfxDotGround) {
                ref_scr[off] = 224 + bits;
              } else {
                ref_scr[off] = 240 + bits;
                ref_clr[off] = (uint8_t)(0x08 | color);
              }
            }
          }
        }
      }
    }
    if (--i == 0) {
      break;
    }
    idx = (idx + 1) & 0x0F;
  }
}

static void test_world_draw_box_points_asm(void) {
  _init_row_ptrs();
  for (uint8_t i = 0; i < 16; ++i) {
    _world_mitch_x[i] = 0;
    _world_mitch_y[i] = 0;
    _world_mitch_z[i] = 0;
  }
  _world_vec_v = make_vector(0, 0, 0);
  // Cover: behind (x<9), lateral tighten fail, vertical cone fail, large
  // x (>255 shr loop), small x (<128 shl loop), negative coords, wraparound.
  _world_mitch_x[14] = 8;
  _world_mitch_x[15] = 200;
  _world_mitch_y[15] = 160;
  _world_mitch_x[0] = 200;
  _world_mitch_z[0] = 51;
  _world_mitch_x[1] = 256;
  _world_mitch_x[2] = 512;
  _world_mitch_y[2] = -128;
  _world_mitch_z[2] = 32;
  _world_mitch_x[3] = 64;
  _world_mitch_y[3] = 20;
  _world_mitch_z[3] = -8;

  for (uint8_t pass = 0; pass < 2; ++pass) {
    uint8_t col = pass ? 3 : kGfxDotGround;
    _clear_test_screens(kCharSolidGround, 0x55);
    test_scr[7 * 40 + 20] = (pass == 0) ? kCharSolidGround : 42;
    ref_scr[7 * 40 + 20] = test_scr[7 * 40 + 20];

    _world_draw_box_points(14, 6, col);
    _ref_world_draw_box_points(14, 6, col);
    _check_screens(12);
  }
}

// 13. render_asm.cc _render_snap_center_asm against C reference.
static uint8_t _ref_roll_get_dist(int8_t px, int8_t py) {
  int8_t d = roll_ex_dy[(px + 4) & 7] - roll_ey_dx[(py + 4) & 7];
  return (uint8_t)(d < 0 ? -d : d);
}

static void _ref_snap_center(int16_t px, int16_t py, int8_t *out_cx,
                             int8_t *out_cy, bool *out_alt, int8_t *out_sx,
                             int8_t *out_sy) {
  int16_t best_px = px;
  int16_t best_py = py;
  *out_alt = false;
  if (roll_period == 1) {
    if (roll_x_is_major) {
      *out_sx = 0;
      *out_sy = 4;
    } else {
      *out_sx = 4;
      *out_sy = 0;
    }
    uint8_t min_d = _ref_roll_get_dist((int8_t)px, (int8_t)py);
    int16_t ax = px - (uint8_t)*out_sx;
    int16_t ay = py - (uint8_t)*out_sy;
    if (_ref_roll_get_dist((int8_t)ax, (int8_t)ay) < min_d) {
      best_px = ax;
      best_py = ay;
      *out_alt = true;
    }
  } else {
    uint8_t min_d = 0xff;
    for (uint8_t i = 0; i < roll_period; ++i) {
      uint8_t d = _ref_roll_get_dist((int8_t)px, (int8_t)py);
      if (d < min_d) {
        min_d = d;
        best_px = px;
        best_py = py;
      }
      px += roll_dx;
      py += roll_dy;
    }
  }
  *out_cx = (int8_t)((best_px + 4) >> 3);
  *out_cy = (int8_t)((best_py + 4) >> 3);
}

static void test_render_snap_center_asm(void) {
  _cur_id = 13;
  struct SnapCase {
    uint8_t period;
    bool x_major;
    int8_t dx, dy;
    int16_t px, py;
  };
  static const SnapCase kSnapCases[] = {
      {1, true, 16, 0, 8, 8},     // main wins
      {1, true, 16, 0, 8, 12},    // alt wins (y shift 4)
      {1, false, 0, -16, 12, 8},  // alt wins (x shift 4)
      {2, true, 8, 2, 160, 56},
      {4, false, -4, 8, 155, 61},
      {8, true, -10, -3, -20, 15},
  };
  for (uint8_t c = 0; c < sizeof(kSnapCases) / sizeof(kSnapCases[0]); ++c) {
    roll_period = kSnapCases[c].period;
    roll_x_is_major = kSnapCases[c].x_major;
    roll_dx = kSnapCases[c].dx;
    roll_dy = kSnapCases[c].dy;
    for (uint8_t e = 0; e < 8; ++e) {
      int8_t off = (int8_t)e - 4;
      roll_ex_dy[e] = (int8_t)(roll_dy * off);
      roll_ey_dx[e] = (int8_t)(roll_dx * off);
    }
    vec_proj_mul_a = kSnapCases[c].px;
    vec_proj_mul_b = kSnapCases[c].py;
    *(volatile int8_t *)&render_alt_shift_x = 0;
    *(volatile int8_t *)&render_alt_shift_y = 0;
    _render_snap_center_asm();

    int8_t r_cx = 0, r_cy = 0, r_sx = 0, r_sy = 0;
    bool r_alt = false;
    _ref_snap_center(kSnapCases[c].px, kSnapCases[c].py, &r_cx, &r_cy, &r_alt,
                     &r_sx, &r_sy);
    expect_id(render_cx_chars, r_cx);
    expect_id(render_cy_chars, r_cy);
    expect_id(render_alt_box ? 1 : 0, r_alt ? 1 : 0);
    if (roll_period == 1) {
      expect_id(*(volatile int8_t *)&render_alt_shift_x, r_sx);
      expect_id(*(volatile int8_t *)&render_alt_shift_y, r_sy);
    }
  }
}

// 14. render_asm.cc _render_flush_span on $E800 (main) and $EC00 (alt).
static void test_render_flush_span_asm(void) {
  _cur_id = 14;
  volatile uint8_t *const main_ram = (volatile uint8_t *)0xE800;
  volatile uint8_t *const alt_ram = (volatile uint8_t *)0xEC00;
  uint8_t saved_p01 = *(volatile uint8_t *)0x01;

  for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
    main_ram[i] = 0xAA;
    alt_ram[i] = 0xAA;
    test_clr[i] = 0x55;
  }

  // No-op paths: odd char, inverted/empty range.
  mem_using_alt_buffer = false;
  _render_sky_rows = 0;
  render_sky_rows = 2;
  _render_flush_span(1, 0, 14);
  _render_flush_span(kCharSolid11, 5, 5);
  _render_flush_span(kCharSolid11, 8, 4);

  // Main buffer: sky [0, 5) + ground [11, 20->14).
  _render_flush_span(kCharSolid11, 0, 5);
  expect_id(_render_sky_rows, 5);
  _render_flush_span(128, 11, 20);

  // Alt buffer: single-row sky [7, 8) and [9, 12).
  mem_using_alt_buffer = true;
  _render_flush_span(kCharSolid11, 7, 8);
  expect_id(_render_sky_rows, 5); // gap: should not advance _render_sky_rows
  _render_flush_span(128, 9, 12);

  // Switch ROM off temporarily so reads from $E800/$EC00 inspect underlying RAM.
  *(volatile uint8_t *)0x01 = 0x35;
  for (uint8_t y = 0; y < kViewportHeight; ++y) {
    uint8_t want_main = (y < 5) ? kCharSolid11 : (y >= 11 ? 128 : 0xAA);
    uint8_t want_alt = (y == 7) ? kCharSolid11 : ((y >= 9 && y < 12) ? 128 : 0xAA);
    uint8_t want_clr = ((y >= 2 && y < 5) || y == 7) ? (0x08 | 6) : 0x55;
    for (uint8_t x = 0; x < 40; ++x) {
      uint16_t off = (uint16_t)y * 40 + x;
      expect_id(main_ram[off], want_main);
      expect_id(alt_ram[off], want_alt);
      expect_id(test_clr[off], want_clr);
    }
  }
  *(volatile uint8_t *)0x01 = saved_p01;
}

// 15. poly_asm.h _poly_bounds_and_clear + _poly_trace_edges against C Bresenham.
static uint8_t *const _ref_min_x = (uint8_t *)0xFC40;
static uint8_t *const _ref_max_x = (uint8_t *)0xFC60;

static void _ref_trace_edge(int8_t xa, uint8_t ya, int8_t xb, uint8_t yb) {
  if (ya == yb) {
    return;
  }
  int8_t x1, x2;
  uint8_t y1, y2;
  if (ya > yb) {
    x1 = xb;
    y1 = yb;
    x2 = xa;
    y2 = ya;
  } else {
    x1 = xa;
    y1 = ya;
    x2 = xb;
    y2 = yb;
  }
  uint8_t dy = y2 - y1;
  uint8_t err = dy >> 1;
  bool rising = x2 > x1;
  uint8_t dx = rising ? (uint8_t)(x2 - x1) : (uint8_t)(x1 - x2);
  uint8_t x = (uint8_t)x1;
  if (dx > dy) {
    uint8_t q = 0, r = dx;
    do {
      r -= dy;
      ++q;
    } while (r >= dy);
    if (rising) {
      for (uint8_t y = y1; y < y2; ++y) {
        uint8_t sx = x;
        x += q;
        err += r;
        if (err >= dy) {
          ++x;
          err -= dy;
        }
        if (sx < _ref_min_x[y]) {
          _ref_min_x[y] = sx;
        }
        if (x > _ref_max_x[y]) {
          _ref_max_x[y] = x;
        }
      }
    } else {
      for (uint8_t y = y1; y < y2; ++y) {
        uint8_t sx = x;
        x -= q;
        err += r;
        if (err >= dy) {
          --x;
          err -= dy;
        }
        if (x < _ref_min_x[y]) {
          _ref_min_x[y] = x;
        }
        if (sx > _ref_max_x[y]) {
          _ref_max_x[y] = sx;
        }
      }
    }
  } else {
    int8_t step = rising ? 1 : -1;
    for (uint8_t y = y1; y < y2; ++y) {
      if (x < _ref_min_x[y]) {
        _ref_min_x[y] = x;
      }
      if (x > _ref_max_x[y]) {
        _ref_max_x[y] = x;
      }
      err += dx;
      if (err >= dy) {
        x = (uint8_t)(x + step);
        err -= dy;
      }
    }
  }
  if (x < _ref_min_x[y2]) {
    _ref_min_x[y2] = x;
  }
  if (x > _ref_max_x[y2]) {
    _ref_max_x[y2] = x;
  }
}

static __noinline void test_poly_trace_edges_asm(void) {
  _cur_id = 15;
  struct PolyCase {
    uint8_t n;
    int8_t v[12];
  };
  static const PolyCase kPolys[] = {
      // Wide X-major rising/falling with horizontal top & carries.
      {4, {36, 3, 44, 3, 70, 17, 10, 17, 0, 0, 0, 0}},
      // Asymmetric X-major + Y-major quadrilateral.
      {4, {20, 2, 75, 7, 22, 25, 3, 19, 0, 0, 0, 0}},
      // Full-viewport rectangle with vertical & horizontal edges [0..79]x[0..27].
      {4, {0, 0, 79, 0, 79, 27, 0, 27, 0, 0, 0, 0}},
      // Hexagon mixing steep Y-major, dx == dy diagonal, and X-major edges.
      {6, {30, 1, 52, 5, 58, 14, 35, 22, 12, 18, 8, 6}},
      // Narrow 1-subrow-tall / steep triangle on odd sub-rows.
      {3, {41, 11, 43, 21, 39, 17, 0, 0, 0, 0, 0, 0}},
  };
  _init_row_ptrs();
  for (uint8_t c = 0; c < sizeof(kPolys) / sizeof(kPolys[0]); ++c) {
    const uint8_t n = kPolys[c].n;
    const int8_t *v = kPolys[c].v;
    for (uint8_t i = 0; i < 2 * kViewportHeight; ++i) {
      _poly_min_x[i] = 0xAA;
      _poly_max_x[i] = 0x55;
      _ref_min_x[i] = 0xAA;
      _ref_max_x[i] = 0x55;
    }
    uint8_t min_y = (uint8_t)v[1], max_y = (uint8_t)v[1];
    for (uint8_t i = 1; i < n; ++i) {
      uint8_t vy = (uint8_t)v[2 * i + 1];
      if (vy < min_y) {
        min_y = vy;
      }
      if (vy > max_y) {
        max_y = vy;
      }
    }
    uint8_t want_s = min_y >> 1, want_e = max_y >> 1;
    for (uint8_t sy = (uint8_t)(want_s << 1); sy <= (uint8_t)((want_e << 1) | 1);
         ++sy) {
      _ref_min_x[sy] = kScreenWidth * 2;
      _ref_max_x[sy] = 0;
    }
    for (uint8_t i = 0; i < n; ++i) {
      uint8_t nx = (i + 1 == n) ? 0 : (uint8_t)(i + 1);
      _ref_trace_edge(v[2 * i], (uint8_t)v[2 * i + 1], v[2 * nx],
                      (uint8_t)v[2 * nx + 1]);
    }

    uint8_t got_s = 99, got_e = 99;
    _poly_bounds_and_clear(v, n, &got_s, &got_e);
    expect_id((int16_t)(((uint16_t)got_e << 8) | got_s),
           (int16_t)(((uint16_t)want_e << 8) | want_s));
    _poly_trace_edges(v, n);

    for (uint8_t i = 0; i < 2 * kViewportHeight; ++i) {
      expect_id(_poly_min_x[i], _ref_min_x[i]);
      expect_id(_poly_max_x[i], _ref_max_x[i]);
    }

    // Also verify end-to-end _poly_scan_lines2 output on the traced spans.
    _clear_test_screens(kCharSolidGround, 0x33);
    uint8_t col = (c & 1) ? 0x0D : 0;
    _poly_scan_lines2(160, col, want_s, want_e);
    _ref_scan_lines2(160, col, want_s, want_e);
    _check_screens(15);
  }
}
// 16. Split planes_project + planes_reslide + planes_fill, planes_place cut
// boundaries, and full-buffer-slide empty fill on the 6502 routines.
static planes_state_t *const _test_psa = (planes_state_t *)0xFD00;
static planes_state_t *const _test_psb = (planes_state_t *)0xFD60;
#pragma bss(opp_bss)
static planes_frame_t _ts_f1, _ts_f2;
static mat3_t _ts_m1, _ts_m2;
static vec3_t _ts_c0, _ts_c1;
#pragma bss(bss)

static void test_planes_split_reslide_asm(void) {
  _cur_id = 16;
  const planes_view_t view = {kTargetCx0, kTargetCy0, kTargetCut1, kTargetCut2,
                              kPlaneKMax, kPlaneKnModel};
  _ts_m1.front = make_vector(-170, 170, 88);
  _ts_m1.left = make_vector(-170, -170, 88);
  _ts_m1.up = make_vector(88, 0, 241);

  static const int16_t kDist[2] = {600, 260}; // ys=1 and ys=2 silhouettes
  for (uint8_t t = 0; t < 2; ++t) {
    planes_state_init(_test_psa);
    planes_state_init(_test_psb);
    _ts_c0 = make_vector(kDist[t], 20, 40);
    _ts_c1 = make_vector(kDist[t], 20, -140);

    planes_project(_test_psa, &view, &_ts_c0, &_ts_m1, &_ts_f1);
    planes_project(_test_psb, &view, &_ts_c0, &_ts_m1, &_ts_f2);

    bool ok = planes_centre(&view, &_ts_c1, &_ts_f1);
    expect_id(ok ? 1 : 0, 1);
    bool moved = planes_reslide(_test_psa, &view, &_ts_f1, kPlaneReslideAlways);
    expect_id(moved ? 1 : 0, 1);
    planes_fill(_test_psa, _planes_sets[0][0]);

    planes_render(_test_psb, &view, &_ts_c1, &_ts_m1, _planes_sets[1][0], &_ts_f2);
    expect_id(_ts_f1.x, _ts_f2.x);
    expect_id(_ts_f1.slid, (uint8_t)(_test_psa->lay.slid));
    if (_test_psa->lay.slid == _test_psb->lay.slid) {
      for (uint8_t b = 0; b < _ts_f2.cols * _ts_f2.rows; ++b) {
        for (uint8_t i = 0; i < kPlaneBlockBytes; ++i) {
          expect_id(_planes_sets[0][b][i], _planes_sets[1][b][i]);
        }
      }
    }

    // Slide >= buffer height must clear all blocks and draw nothing.
    _test_psa->lay.slid = 200;
    uint8_t *flat0 = _planes_sets[0][0];
    for (uint16_t i = 0; i < 4 * kPlaneBlockStride; ++i) {
      flat0[i] = 0xAA;
    }
    planes_fill(_test_psa, flat0);
    uint16_t nbytes =
        (uint16_t)(_test_psa->lay.cols * _test_psa->lay.rows) * kPlaneBlockStride;
    for (uint16_t i = 0; i < nbytes; ++i) {
      expect_id(flat0[i], 0);
    }
  }

  // Dot-tier exact cut1 boundary: y == cut1 is shown, y == cut1 + 1 is hidden.
  static const planes_layout_t dot_lay = {kPlaneLevelDot, 1, 1, 1, 0, 0, 0};
  _ts_f1.hidden = kPlaneShown;
  _ts_f1.cx = 160;
  _ts_f1.cy = (int16_t)(kTargetCut1 + kPlaneDotY);
  expect_id(planes_place(&dot_lay, &view, &_ts_f1), 0);
  expect_id(_ts_f1.hidden, kPlaneShown);
  _ts_f1.cy = (int16_t)(kTargetCut1 + kPlaneDotY + 1);
  expect_id(planes_place(&dot_lay, &view, &_ts_f1), 0);
  expect_id(_ts_f1.hidden, kPlaneBelowCut);

  // planes_project_axes cache: x components of axes ignored, every (y, z) byte
  // detected and triggers re-projection.
  planes_state_init(_test_psa);
  _ts_c0 = make_vector(400, 0, 0);
  _ts_m2 = _ts_m1;
  planes_project(_test_psa, &view, &_ts_c0, &_ts_m2, &_ts_f1);
  expect_id(_ts_f1.cached ? 1 : 0, 0);
  planes_project(_test_psa, &view, &_ts_c0, &_ts_m2, &_ts_f1);
  expect_id(_ts_f1.cached ? 1 : 0, 1);
  _ts_m2.front.x ^= 0x1234;
  _ts_m2.left.x ^= 0x5678;
  _ts_m2.up.x ^= 0x2345;
  planes_project(_test_psa, &view, &_ts_c0, &_ts_m2, &_ts_f1);
  expect_id(_ts_f1.cached ? 1 : 0, 1);
  uint8_t *mb = (uint8_t *)&_ts_m2;
  uint8_t ax_i = 0;
  for (uint8_t r = 0; r < 3; ++r) {
    for (uint8_t c = 2; c < 6; c += 2, ++ax_i) {
      uint8_t off = (uint8_t)(r * 6 + c);
      mb[off] ^= 0x40;
      planes_project_axes(_test_psa, &view, &_ts_m2, &_ts_f1);
      expect_id(_test_psa->axes[ax_i], ((const int16_t *)&_ts_m2)[off >> 1]);
      planes_project_axes(_test_psa, &view, &_ts_m2, &_ts_f1);
      expect_id(_ts_f1.cached ? 1 : 0, 1);
      mb[off] ^= 0x40;
      planes_project_axes(_test_psa, &view, &_ts_m2, &_ts_f1);
    }
  }

  // planes_reslide thresholds (Deep/Off/Always), ys=1 & ys=2 odd parity, band, key_count=0.
  static const int16_t kDeltas[] = {-300, -3, -2, -1, 0, 1, 2, 3, 130};
  for (uint8_t ys = 1; ys <= 2; ++ys) {
    for (uint8_t when = 0; when <= 2; ++when) {
      for (uint8_t di = 0; di < sizeof(kDeltas) / sizeof(kDeltas[0]); ++di) {
        int16_t want_d = kDeltas[di];
        int16_t base_slid = 310;
        int16_t need = (int16_t)(base_slid + want_d);
        planes_state_init(_test_psa);
        _test_psa->lay.level = kPlaneLevelX;
        _test_psa->lay.ys = ys;
        _test_psa->lay.cols = 1;
        _test_psa->lay.rows = 1;
        _test_psa->lay.ox = -12;
        _test_psa->lay.oy = 0;
        _test_psa->lay.slid = base_slid;
        _test_psa->key_count = (di == 4) ? 0 : 4;
        _test_psa->key_band = 99;
        for (uint8_t v = 0; v < 4; ++v) {
          _test_psa->key_y[v] = (uint8_t)(10 + v * 7);
        }
        _ts_f1.hidden = kPlaneShown;
        _ts_f1.cx = 160;
        _ts_f1.cy = (int16_t)((ys == 2 ? view.cut2 : view.cut1) + need);
        bool want_mv = !(when != kPlaneReslideAlways && want_d > -2 &&
                         (when == kPlaneReslideDeep || want_d < 2));
        bool got_mv = planes_reslide(_test_psa, &view, &_ts_f1, when);
        expect_id(got_mv ? 1 : 0, want_mv ? 1 : 0);
        if (want_mv) {
          int16_t ed = want_d;
          if (ys == 2) {
            ed = (int16_t)(ed + (ed & 1));
          }
          int16_t want_slid = (int16_t)(base_slid + ed);
          uint8_t dy = (uint8_t)(ys == 2 ? (ed >> 1) : ed);
          expect_id(_test_psa->lay.slid, want_slid);
          expect_id(_test_psa->key_band, (uint8_t)((uint16_t)want_slid >> 7));
          for (uint8_t v = 0; v < _test_psa->key_count; ++v) {
            expect_id(_test_psa->key_y[v], (uint8_t)(10 + v * 7 + dy));
          }
        } else {
          expect_id(_test_psa->lay.slid, base_slid);
          expect_id(_test_psa->key_band, 99);
        }
      }
    }
  }

  // Axis-aligned attitude (0 and +-256 screen components) through _planes_axis_products.
  _ts_m1.front = make_vector(0, 256, 0);
  _ts_m1.left = make_vector(-256, 0, 0);
  _ts_m1.up = make_vector(0, 0, -256);
  _planes_mags(100);
  _planes_axis_products(&_ts_m1);
  expect_id(_planes_tx[0], 0);
  expect_id(_planes_ty[0], 0);
  for (uint8_t p = 1; p < 16; ++p) {
    int16_t pv = (int16_t)_planes_pv[p];
    int16_t wx = p < 12 ? pv : 0;
    int16_t wy = p >= 14 ? (int16_t)-pv : 0;
    expect_id(_planes_tx[16 + p], wx);
    expect_id(_planes_tx[p], (int16_t)-wx);
    expect_id(_planes_ty[16 + p], wy);
    expect_id(_planes_ty[p], (int16_t)-wy);
  }
}

// 17. opponent_asm.cc: _opponent_world_delta, _opponent_toward, _opponent_pos,
// _opponent_latch_shown, _opponent_copy_cs, and _opponent_steer against 16-bit
// C references on the emulated 6510.
static int32_t _test_eye_x, _test_eye_y, _test_eye_z;
int32_t *const kFlightEye[3] = {&_test_eye_x, &_test_eye_y, &_test_eye_z};
#include "../opponent_asm.h"

static uint8_t _ref_world_delta(uint8_t i, vec3_t *out) {
  uint8_t range = kOpponentNear;
  uint8_t k = i;
  uint16_t fd[3];
  int8_t fs[3];
  for (uint8_t c = 0; c < 3; ++c, k += 2) {
    int32_t d = _opponent_xyz[k] - *kFlightEye[c];
    fd[c] = (uint16_t)d;
    uint8_t u = (uint8_t)(d >> 16);
    if (c == 0) {
      u &= 0x7F;
      if (u & 0x40) {
        u |= 0x80;
      }
    }
    int8_t s = (int8_t)u;
    fs[c] = s;
    if (s >= 32 || s < -31) {
      return kOpponentOut;
    }
    if (s >= 16 || s < -15) {
      range = kOpponentFar;
    }
  }
  for (uint8_t c = 0; c < 3; ++c) {
    uint16_t f = fd[c];
    uint16_t v = ((uint16_t)(uint8_t)fs[c] << 8) | (f >> 8);
    if (range == kOpponentNear) {
      v = (v << 2) | ((uint8_t)f >> 6);
    }
    (&out->x)[c] = (int16_t)v;
  }
  return range;
}

static __noinline void _check_steer(uint8_t slot, int16_t d, int16_t dy_in,
                                    int16_t dz_in, int16_t hx, int16_t hy) {
  _opponent_head[slot] = make_vector(hx, hy, 0);
  vec_v = make_vector(d, dy_in, dz_in);
  uint8_t got_cut = _opponent_steer(slot);
  int16_t rdx = d, rdy = dy_in, rdz = dz_in;
  uint8_t want_cut = 0;
  while ((uint16_t)(rdx + 63) >= 127 || (uint16_t)(rdy + 63) >= 127) {
    rdx >>= 1;
    rdy >>= 1;
    rdz >>= 1;
    ++want_cut;
  }
  int16_t wc =
      vec_fastmul8p8(rdy << 8, hx) + vec_fastmul8p8(-(rdx << 8), hy);
  int16_t wd = vec_fastmul8p8(rdx << 8, hx) + vec_fastmul8p8(rdy << 8, hy);
  expect_id((int16_t)(((uint16_t)got_cut << 8) | (uint8_t)vec_v.x),
            (int16_t)(((uint16_t)want_cut << 8) | (uint8_t)rdx));
  if (vec_v.y != rdy || vec_v.z != rdz || vec_sx != wc || vec_sy != wd) {
    expect_id(vec_sx, wc ^ 1);
  }
}

static __noinline void test_opponent_world_delta_asm(void) {
  _cur_id = 17;
  __striped static const int32_t kEyes[3][3] = {
      {0x00102000L, 0x00204000L, 0x00041000L},
      {0x007E0080L, 0x00FD80C0L, 0x00060040L}, // near x=128 & y=256 wrap edges
      {0x00018040L, 0x00020080L, 0x000800C0L},
  };
  __striped static const int32_t kOffs[11][3] = {
      {0, 0, 0},
      {0x000001C0L, -0x00000280L, 0x00000040L},
      {0x000FFFFFL, -0x000F0000L, 0x00071240L},
      {0x00100000L, 0x00000180L, -0x00010040L},
      {0x00000100L, -0x000F0001L, 0x00000000L},
      {0x001FFFFFL, -0x001F0000L, 0x00104080L},
      {0x00200000L, 0, 0},
      {0, -0x001F0001L, 0},
      {0, 0, 0x00250000L},
      {-0x007C0040L, -0x00FB0080L, 0x00010040L},
      {0x007B00C0L, 0x00EE0040L, -0x00010080L},
  };
  for (uint8_t e = 0; e < 3; ++e) {
    _test_eye_x = kEyes[e][0];
    _test_eye_y = kEyes[e][1];
    _test_eye_z = kEyes[e][2];
    for (uint8_t c = 0; c < 11; ++c) {
      for (uint8_t slot = 0; slot < 2; ++slot) {
        uint8_t other = slot ^ 1;
        _opponent_xyz[other] = 0x55AA55AAL;
        _opponent_xyz[other + 2] = -0x33445566L;
        _opponent_xyz[other + 4] = 0x11223344L;
        _opponent_xyz[slot] = (_test_eye_x + kOffs[c][0]) & 0x007FFFFFL;
        _opponent_xyz[slot + 2] = (_test_eye_y + kOffs[c][1]) & 0x00FFFFFFL;
        _opponent_xyz[slot + 4] = _test_eye_z + kOffs[c][2];

        uint8_t want_r = _ref_world_delta(slot, &_ts_c0);
        vec_v = make_vector(1234, -5678, 9999);
        expect_id(_opponent_world_delta(slot), want_r);
        if (want_r != kOpponentOut) {
          for (uint8_t a = 0; a < 3; ++a) {
            expect_id((&vec_v.x)[a], (&_ts_c0.x)[a]);
          }
        }
      }
    }
  }

  // _opponent_toward, _opponent_pos, _opponent_latch_shown, _opponent_copy_cs.
  static const int32_t kTwPos[6] = {
      0x00102000L,  0x00102001L, 0x007F80FFL,
      -0x003F4080L, 0x00400000L, 0x12345678L,
  };
  static const uint16_t kTwW[8] = {0x0000, 0x1020, 0x1021, 0x5020,
                                   0x5021, 0x9020, 0xFF80, 0xFFFF};
  for (uint8_t idx = 0; idx < 6; ++idx) {
    _opponent_xyz[idx] = kTwPos[idx];
  }
  for (uint8_t idx = 0; idx < 6; ++idx) {
    const uint8_t *pb = (const uint8_t *)&_opponent_xyz[idx];
    uint16_t p16 = *(const uint16_t *)(pb + 1) + (pb[0] != 0 ? 1u : 0u);
    for (uint8_t wi = 0; wi < 8; ++wi) {
      int16_t d = (int16_t)(kTwW[wi] - p16);
      expect_id(_opponent_toward(kTwW[wi], idx), (int16_t)(d << 1) >> 1);
    }
  }
  for (uint8_t slot = 0; slot < 2; ++slot) {
    #pragma bss(opp_bss)
    static int32_t pos_out[3];
    #pragma bss(bss)
    pos_out[0] = pos_out[1] = pos_out[2] = -1;
    _opponent_pos(slot, pos_out);
    for (uint8_t b = 0; b < 12; ++b) {
      uint8_t src = (uint8_t)((slot << 2) + (b & 3) + ((b & ~3) << 1));
      expect_id(((const uint8_t *)pos_out)[b],
                ((const uint8_t *)_opponent_xyz)[src]);
    }
  }
  for (uint8_t b = 0; b < 8; ++b) {
    ((uint8_t *)&_opponent_planes[0].lay)[b] = (uint8_t)(0x10 + b);
    ((uint8_t *)&_opponent_planes[1].lay)[b] = (uint8_t)(0xA0 + b);
    ((uint8_t *)_opponent_shown)[b] = 0;
    ((uint8_t *)_opponent_shown)[8 + b] = 0;
    if (b < 6) {
      ((uint8_t *)&_opponent_cs[0])[b] = (uint8_t)(0x21 + b);
      ((uint8_t *)&_opponent_cs[1])[b] = (uint8_t)(0xD5 - b);
    }
  }
  for (uint8_t slot = 0; slot < 2; ++slot) {
    _opponent_latch_shown(slot);
    if (slot == 0) {
      expect_id(((uint8_t *)_opponent_shown)[8], 0);
    }
    for (uint8_t b = 0; b < 8; ++b) {
      expect_id(((uint8_t *)&_opponent_shown[slot])[b],
                ((uint8_t *)&_opponent_planes[slot].lay)[b]);
    }
    _opponent_copy_cs(slot);
    for (uint8_t b = 0; b < 6; ++b) {
      expect_id(((uint8_t *)&_opponent_c)[b],
                ((uint8_t *)&_opponent_cs[slot])[b]);
    }
  }

  // _opponent_steer across all d8 in [-64, 63] and all h in [-256, 256].
  static const int16_t kSteerHX[4] = {-256, -255, 0, 255};
  static const int16_t kSteerHY[4] = {256, 181, -181, -1};
  for (uint8_t di = 0; di < 128; ++di) {
    int16_t d = (int16_t)di - 64;
    for (uint8_t hi = 0; hi < 4; ++hi) {
      _check_steer((uint8_t)(di & 1), d, (int16_t)(-1 - d), -300, kSteerHX[hi],
                   kSteerHY[hi]);
    }
  }
  for (uint16_t hi = 0; hi <= 512; ++hi) {
    int16_t h = (int16_t)hi - 256;
    _check_steer(0, -63, 57, 1024, h, (int16_t)-h);
  }
  _check_steer(1, -16384, 16383, -4095, 181, -181);
}

// 18. gun_asm.cc: _gun_track_all, _gun_rel_from, _gun_hits, _gun_collides,
// _gun_store_enemy_round, _gun_step_rounds, _gun_shown, _gun_dir, _gun_view,
// _gun_view_round, and _gun_vel_round against 16-bit C references on the
// emulated 6510.
uint8_t flight_step_shift;
uint8_t kFlightSubstepMask;
mat3_t world_cam;
#include "../gun_asm.h"

static bool _ref_gun_rel(uint8_t c, int16_t *out_val) {
  int32_t d = _gun_p[c] - _gun_eye[c];
  uint8_t u = (uint8_t)(d >> 16);
  if (c == 0) {
    u &= 0x7F;
    if (u & 0x40) {
      u |= 0x80;
    }
  }
  int8_t s = (int8_t)u;
  if (s < -15 || s > 14) {
    return false;
  }
  *out_val = (int16_t)(((int16_t)s << 11) + (int16_t)((uint16_t)d >> 5));
  return true;
}

static bool _ref_gun_hits(uint8_t r, int16_t box) {
  int16_t gd[3], gq[3];
  uint8_t i = r;
  for (uint8_t c = 0; c < 3; ++c, i += 8) {
    int16_t d = _gun_t[c] - _round_p[i];
    gd[c] = d;
    int16_t v = _round_v[i];
    for (uint8_t s = flight_step_shift; s != 0; --s) {
      v >>= 1;
    }
    gq[c] = v >> 2;
    uint16_t lim = _abs16(v) + (uint16_t)box;
    if (_abs16(d) > lim) {
      return false;
    }
  }
  for (uint8_t n = 0; n < 4; ++n) {
    bool in = true;
    for (uint8_t c = 0; c < 3; ++c) {
      int16_t d = gd[c] - gq[c];
      gd[c] = d;
      if (_abs16(d) > (uint16_t)box) {
        in = false;
      }
    }
    if (in) {
      return true;
    }
  }
  return false;
}

static bool _ref_gun_collides(uint8_t k, bool prev, int16_t *prev_buf) {
  bool now = true, mid = prev;
  for (uint8_t c = 0; c < 3; ++c, k += 2) {
    int16_t t = _gun_t[c];
    if (_abs16(t) > 192) {
      now = false;
    }
    if (_abs16((int16_t)((t + prev_buf[k]) >> 1)) > 192) {
      mid = false;
    }
    prev_buf[k] = t;
  }
  return now || mid;
}

static __noinline void test_gun_asm(void) {
  _cur_id = 18;
  // _gun_track_all: sub-quantum preservation, positive/negative borrow across
  // bit 16, exact d >> 5 in _gun_moved, and _gun_t zeroed.
  __striped static const int32_t kSteps[3][3] = {
      {0x0000001FL, -0x00000001L, 0x00000020L},
      {0x00000F15L, -0x00000E33L, 0x000001FFL},
      {-0x00001205L, 0x0000185AL, 0x00000000L},
  };
  static const int32_t kInitEye[3] = {0x0010FFE8L, 0x00200010L, 0x00058004L};
  #pragma bss(opp_bss)
  static int32_t ref_eye[3];
  static int16_t want_m[3];
  #pragma bss(bss)
  for (uint8_t c = 0; c < 3; ++c) {
    ref_eye[c] = kInitEye[c];
    _gun_eye[c] = ref_eye[c];
    *kFlightEye[c] = ref_eye[c];
  }
  for (uint8_t s = 0; s < 3; ++s) {
    for (uint8_t c = 0; c < 3; ++c) {
      *kFlightEye[c] += kSteps[s][c];
      int16_t d = (int16_t)((uint16_t)(*kFlightEye[c]) - (uint16_t)ref_eye[c]);
      want_m[c] = d >> 5;
      ref_eye[c] += (int16_t)(d & ~31);
      _gun_t[c] = 1234;
    }
    _gun_track_all();
    for (uint8_t c = 0; c < 3; ++c) {
      expect_id((int16_t)_gun_eye[c], (int16_t)ref_eye[c]);
      expect_id((int16_t)(_gun_eye[c] >> 16), (int16_t)(ref_eye[c] >> 16));
      expect_id(_gun_moved[c], want_m[c]);
      expect_id(_gun_t[c], 0);
    }
  }

  // _gun_rel_from(0) and _gun_rel_from(2): wraparound on x/y and [-15, 14].
  static const int32_t kRelEye[3] = {0x007E0040L, 0x00FC8020L, 0x00040100L};
  for (uint8_t c = 0; c < 3; ++c) {
    _gun_eye[c] = kRelEye[c];
  }
  __striped static const int32_t kRelOff[7][3] = {
      {0, 0, 0},
      {0x000EFFFDL, -0x000F0000L, -0x00040100L},
      {0x000F0000L, 0, 0},
      {0, -0x000F0001L, 0},
      {0, 0, 0x000F0000L},
      {-0x007A0020L, -0x00F60040L, -0x00010060L},
      {0x00790060L, 0x00F50080L, 0x000200A0L},
  };
  for (uint8_t k = 0; k < 7; ++k) {
    _gun_p[0] = (_gun_eye[0] + kRelOff[k][0]) & 0x007FFFFFL;
    _gun_p[1] = (_gun_eye[1] + kRelOff[k][1]) & 0x00FFFFFFL;
    _gun_p[2] = _gun_eye[2] + kRelOff[k][2];
    bool want_ok = _ref_gun_rel(0, &want_m[0]) && _ref_gun_rel(1, &want_m[1]) &&
                   _ref_gun_rel(2, &want_m[2]);
    bool got_ok = _gun_rel3_to() != 0;
    expect_id(got_ok, want_ok);
    if (want_ok) {
      for (uint8_t a = 0; a < 3; ++a) {
        expect_id(_gun_t[a], want_m[a]);
      }
    }
    int16_t want_z = 33;
    bool want_zok = _ref_gun_rel(2, &want_z);
    _gun_t[0] = 11;
    _gun_t[1] = 22;
    expect_id(_gun_rel2() != 0, want_zok);
    expect_id((int16_t)((_gun_t[0] << 8) | _gun_t[1]), (11 << 8) | 22);
    if (want_zok) {
      expect_id(_gun_t[2], want_z);
    }
  }

  // _gun_hits: substeps, reach boundary, box boundary, and flight_step_shift.
  __striped static const int16_t kHitCases[7][7] = {
      {500, -200, 100, 800, -400, 200, 160},   // clean hit on substep 2
      {960, 0, 0, 800, 0, 0, 160},             // exact |d| == lim, hits on substep 4
      {961, 0, 0, 800, 0, 0, 160},             // |d| == lim + 1 -> early out
      {500, 300, 0, 800, -400, 0, 160},        // y diverges -> miss in Pass 2
      {-700, -160, 160, -800, 0, 0, 160},      // negative velocity boundary hit
      {-700, -161, 160, -800, 0, 0, 160},      // off by 1 outside box -> miss
      {600, -500, 400, 768, -600, 500, 640},   // near-miss box = 640
  };
  for (uint8_t sh = 0; sh <= 2; ++sh) {
    flight_step_shift = sh;
    for (uint8_t c = 0; c < sizeof(kHitCases) / sizeof(kHitCases[0]); ++c) {
      uint8_t r = (uint8_t)(c & 7);
      static const int8_t kRp[3] = {40, -20, 10};
      for (uint8_t a = 0; a < 3; ++a) {
        _gun_t[a] = kHitCases[c][a];
        _round_p[r + 8 * a] = kRp[a];
        _round_v[r + 8 * a] = kHitCases[c][a + 3];
      }
      expect_id(_gun_hits(r, kHitCases[c][6]), _ref_gun_hits(r, kHitCases[c][6]));
    }
  }
  flight_step_shift = 0;

  // _gun_collides: +-192 vs +-193 on now and mid, prev=false vs true, slots 0/1.
  __striped static const int16_t kColSeq[10][3] = {
      {400, 0, 0},      // outside
      {-384, 192, -192}, // now outside (-384), mid = (+8, +96, -96) -> mid hit if prev!
      {500, 0, 0},      // outside
      {-114, 0, 0},     // mid = +193 -> outside; now = -114 -> now hit!
      {500, 385, 0},
      {-500, -1, 0},    // mid = (0, +192, 0) -> mid hit!
      {500, 387, 0},
      {-500, -1, 0},    // mid = (0, +193, 0) -> miss!
      {192, -192, 192}, // exact boundary now hit
      {193, 0, 0},      // +193 outside (unless mid hits: (192+193)>>1 = 192 -> mid hits if prev!)
  };
  #pragma bss(opp_bss)
  static int16_t ref_prev[6];
  #pragma bss(bss)
  for (uint8_t i = 0; i < 6; ++i) {
    ref_prev[i] = 0;
    _gun_prev[i] = 0;
  }
  for (uint8_t s = 0; s < sizeof(kColSeq) / sizeof(kColSeq[0]); ++s) {
    uint8_t slot = s & 1;
    bool prev = (s >= 2);
    _gun_t[0] = kColSeq[s][0];
    _gun_t[1] = kColSeq[s][1];
    _gun_t[2] = kColSeq[s][2];
    bool want = _ref_gun_collides(slot, prev, ref_prev);
    bool got = _gun_collides(slot, prev);
    expect_id(got, want);
    for (uint8_t i = 0; i < 6; ++i) {
      expect_id(_gun_prev[i], ref_prev[i]);
    }
  }

  // _gun_dir across all 16 directions, zero axes, small/large magnitudes, and
  // int16_t extremes (-32768, 32767).
  static const int16_t kDirVals[] = {
      0, 1, -1, 25, -25, 78, -78, 127, -128, 128, 210, -210, 32767, -32768};
  for (uint8_t xi = 0; xi < sizeof(kDirVals) / sizeof(kDirVals[0]); ++xi) {
    for (uint8_t yi = 0; yi < sizeof(kDirVals) / sizeof(kDirVals[0]); ++yi) {
      int16_t dx = kDirVals[xi], dy = kDirVals[yi];
      int16_t rdx = dx, rdy = dy;
      if (rdx < 0) {
        rdx = -rdx;
        rdy = -rdy;
      }
      uint16_t a = _abs16(rdy), b = (uint16_t)rdx;
      while ((a | b) >= 128) {
        a >>= 1;
        b >>= 1;
      }
      bool steep = (uint8_t)b <= (uint8_t)a;
      uint8_t n = steep ? (uint8_t)b : (uint8_t)a;
      uint8_t m = steep ? (uint8_t)a : (uint8_t)b;
      uint8_t k = 0;
      for (uint8_t t = 0; t < 4; ++t) {
        if ((uint16_t)((uint16_t)n << 8) > vec_mul8x8(m, kTracerTan[t])) {
          ++k;
        }
      }
      if (!steep) {
        k = (uint8_t)(8 - k);
      }
      expect_id(_gun_dir(dx, dy), rdy < 0 ? k : (uint8_t)((16 - k) & 15));
    }
  }

  // _gun_view, _gun_view_round, and _gun_vel_round across behind-camera, cone
  // edges, 8-bit and 16-bit divisors, and rotated camera basis.
  world_cam.front = make_vector(204, 153, 0);
  world_cam.left = make_vector(-153, 204, 0);
  world_cam.up = make_vector(0, 0, 256);
  static const int8_t kViewPts[][3] = {
      {-50, 0, 0},   {50, 38, 0},    {64, 48, 0},    // x <= 64
      {80, 60, 100}, {80, -80, 10},  {100, 90, -80}, // cone out & x < 256 in
      {120, 30, -20},                                // scaled << 5 below
  };
  for (uint8_t pi = 0; pi < sizeof(kViewPts) / sizeof(kViewPts[0]); ++pi) {
    uint8_t r = (uint8_t)(pi & 7);
    for (uint8_t c = 0; c < 3; ++c) {
      int16_t pv = kViewPts[pi][c];
      _round_p[r + 8 * c] = (pi == 6) ? (pv << 6) : (pv << 1);
    }
    _ts_c0 = make_vector(_round_p[r] >> 1, _round_p[r + 8] >> 1,
                         _round_p[r + 16] >> 1);
    vec_transform_inv(&world_cam, &_ts_c0, &_ts_c1);
    bool want_vis = _ts_c1.x > 64 && _abs16(_ts_c1.z) < (uint16_t)_ts_c1.x &&
                    _abs16(_ts_c1.y) < (uint16_t)_ts_c1.x;
    expect_id(_gun_view_round(r) ? 1 : 0, want_vis ? 1 : 0);
    if (want_vis) {
      expect_id(_gun_c.x, _ts_c1.x);
      expect_id(_gun_s[0], (int16_t)(160 - vec_div8p8(_ts_c1.y, _ts_c1.x)));
      expect_id(_gun_s[1], (int16_t)(56 - vec_div8p8(_ts_c1.z, _ts_c1.x)));
    }
    _gun_c = _ts_c0;
    expect_id(_gun_view() ? 1 : 0, want_vis ? 1 : 0);
  }
  static const int16_t kVel3[3] = {700, -351, 123};
  for (uint8_t c = 0; c < 3; ++c) {
    _round_v[3 + 8 * c] = kVel3[c];
  }
  _gun_vel_round(3);
  expect_id(vec_v.x, vec_fastmul8p8(204, 350) + vec_fastmul8p8(153, -176));
  expect_id(vec_v.z, 61);

  // _gun_shown + _gun_store_enemy_round + _gun_step_rounds.
  for (uint8_t r = 0; r < 8; ++r) {
    _round_age[r] = 0;
  }
  *(volatile uint8_t *)&_gun_last = kRoundNone;
  expect_id(_gun_shown(), kRoundNone);
  static const int16_t kInitTV[4][3] = {
      {333, -222, -500}, {40, -20, -40}, {200, -100, 0}, {64, -32, 16}};
  for (uint8_t c = 0; c < 3; ++c) {
    _gun_t[c] = kInitTV[0][c];
    _gun_v[c] = kInitTV[1][c];
    _round_p[8 * c] = kInitTV[2][c];
    _round_v[8 * c] = kInitTV[3][c];
  }
  _gun_store_enemy_round(3, 2);
  *(volatile uint8_t *)&_round_age[0] = 12;
  *(volatile uint8_t *)&_round_owner[0] = 0;
  expect_id(_gun_shown(), 3);

  static const int8_t kMv[3] = {12, -7, 4};
  for (uint8_t c = 0; c < 3; ++c) {
    _gun_moved[c] = kMv[c];
  }
  *(volatile int32_t *)&_gun_eye[2] = 0x00004000L; // ground at -512 sixteenths
  volatile uint8_t *vz = (volatile uint8_t *)&_gun_z;
  volatile const uint8_t *vp = (volatile const uint8_t *)_gun_puff;
  vz[11] = 0; // _gun_substep
  vz[12] = 0; // _gun_puff_t
  _gun_last = 3;
  flight_step_shift = 1;
  kFlightSubstepMask = 1;
  expect_id(_gun_step_rounds() ? 1 : 0, 0);
  expect_id((int16_t)((vz[0] << 8) | vz[3]), 12 << 8);
  expect_id(_gun_last, kRoundNone);
  expect_id(_gun_puff[0], 341);
  expect_id(_gun_puff[2], -512);

  flight_step_shift = 0;
  _gun_last = 0;
  expect_id(_gun_step_rounds() ? 1 : 0, 1);
  kFlightSubstepMask = 0;
  expect_id((int16_t)((vz[0] << 8) | vz[12]), kPuffFrames - 1);
  expect_id(_gun_last, kRoundNone);
  expect_id((int16_t)(((uint16_t)vp[3] << 8) | vp[0]), 329);
}

// 19. vec_asm.cc restoring division (vec_div8p8, vec_frac16, vec_fracn across
// 8-bit and 16-bit divisors, zero/equal/integer/saturating boundaries) and
// planes_asm.cc planes_centre (behind-camera, cone, scale clamp, and diameter).
static uint16_t _ref_uabs16(int16_t v) {
  return v < 0 ? (uint16_t)(-(uint16_t)v) : (uint16_t)v;
}

static __noinline uint16_t _ref_fracn(int16_t a, int16_t b, uint8_t bits) {
  uint16_t ua = a < 0 ? (uint16_t)(-(uint16_t)a) : (uint16_t)a;
  uint16_t ub = b < 0 ? (uint16_t)(-(uint16_t)b) : (uint16_t)b;
  uint16_t ones = 0;
  for (uint8_t i = 0; i < bits; ++i) {
    ones = (uint16_t)((ones << 1) | 1);
  }
  if (ua >= ub) {
    return ones;
  }
  uint16_t q = 0;
  uint16_t r = ua;
  for (uint8_t i = 0; i < bits; ++i) {
    q <<= 1;
    r <<= 1;
    if (r >= ub) {
      r -= ub;
      q |= 1;
    }
  }
  return q;
}

static __noinline int16_t _ref_div8p8(int16_t a, int16_t b) {
  uint16_t ua = _ref_uabs16(a);
  uint16_t ub = _ref_uabs16(b);
  uint16_t q;
  if (ua < ub) {
    q = _ref_fracn(a, b, 8);
  } else if (ub == 0) {
    if (ua == 0) {
      return 0;
    }
    q = 32767;
  } else {
    uint16_t d = ub;
    uint8_t k = 0;
    while (k < 7 && d <= (ua >> 1)) {
      d <<= 1;
      ++k;
    }
    if (k == 7) {
      q = 32767;
    } else {
      uint8_t qi = 0;
      for (;;) {
        qi <<= 1;
        if (ua >= d) {
          ua -= d;
          qi |= 1;
        }
        if (k == 0) {
          break;
        }
        --k;
        d >>= 1;
      }
      q = (uint16_t)(((uint16_t)qi << 8) | _ref_fracn((int16_t)ua, b, 8));
    }
  }
  return ((a ^ b) < 0) ? (int16_t)-(int16_t)q : (int16_t)q;
}

static bool _ref_planes_centre(const planes_view_t *view, const vec3_t *c,
                               planes_frame_t *f) {
  f->hidden = kPlaneShown;
  f->cached = false;
  f->slid = 0;
  if (c->x <= 64) {
    f->hidden = kPlaneBehindCamera;
    return false;
  }
  if (_ref_uabs16(c->y) >= (uint16_t)c->x ||
      _ref_uabs16(c->z) >= (uint16_t)c->x) {
    f->hidden = kPlaneOutOfRange;
    return false;
  }
  f->cx = (int16_t)(view->cx0 - _ref_div8p8(c->y, c->x));
  f->cy = (int16_t)(view->cy0 - _ref_div8p8(c->z, c->x));
  uint16_t k = c->x > view->kn ? _ref_fracn(view->kn, c->x, 9) : 0x1FFu;
  f->clamped = k > view->kmax;
  if (f->clamped) {
    k = view->kmax;
  }
  f->k = (uint8_t)k;
  f->d = (uint8_t)(vec_mul8x8(kPlaneMaxRadius << 1, (uint8_t)k) >> 8);
  return true;
}

static void test_vec_div_and_planes_centre_asm(void) {
  _cur_id = 19;
  static const int16_t kDivB[] = {0,    1,     -1,    2,     -3,   63,   64,
                                  -64,  65,    -65,   127,   128,  -129, 200,
                                  255,  -255,  256,   -256,  257,  511,  -1000,
                                  8192, 32767, -32768};
  static const int16_t kDivA[] = {0,     1,      -1,    2,     -63,   64,
                                  -65,   127,    -128,  129,   200,   -254,
                                  255,   -256,   511,   8191,  -8192, 8320,
                                  32767, -32768};
  for (uint8_t bi = 0; bi < sizeof(kDivB) / sizeof(kDivB[0]); ++bi) {
    int16_t b = kDivB[bi];
    for (uint8_t ai = 0; ai < sizeof(kDivA) / sizeof(kDivA[0]); ++ai) {
      int16_t a = kDivA[ai];
      int16_t want_d = _ref_div8p8(a, b);
      int16_t got_d = vec_div8p8(a, b);
      expect_id(got_d, want_d);
      uint16_t want_f = _ref_fracn(a, b, 16);
      uint16_t got_f = vec_frac16(a, b);
      expect_id((int16_t)got_f, (int16_t)want_f);
    }
  }
  // Exact literal sanity anchor against _ref_div8p8 / _ref_fracn.
  expect_id(vec_div8p8(8191, 65), 32259);
  expect_id(vec_div8p8(-32768, -32768), 256);

  // vec_fracn across odd bit counts and saturation.
  static const uint8_t kBits[] = {1, 7, 8, 9, 12, 16};
  __striped static const int16_t kFnPairs[15][2] = {
      {0, 65},    {1, 3},      {2, 3},       {64, 128},    {64, 129},
      {128, 129}, {-254, 255}, {255, -256},  {300, 1000},  {32767, -32768},
      {200, 200}, {500, 200},  {-32768, -32768}, {10, 0},  {0, 0},
  };
  for (uint8_t bi = 0; bi < sizeof(kBits) / sizeof(kBits[0]); ++bi) {
    uint8_t bits = kBits[bi];
    for (uint8_t pi = 0; pi < sizeof(kFnPairs) / sizeof(kFnPairs[0]); ++pi) {
      int16_t a = kFnPairs[pi][0], b = kFnPairs[pi][1];
      uint16_t got_fn = vec_fracn(a, b, bits);
      uint16_t want_fn = _ref_fracn(a, b, bits);
      expect_id((int16_t)got_fn, (int16_t)want_fn);
    }
  }

  // planes_centre across views, behind-camera, cone edges, and clamp edges.
  static const planes_view_t kViews[] = {
      {160, 56, 89, 68, 150, 64},
      {160, 80, 140, 119, 88, 64},
      {120, 40, 200, 200, 255, 200},
  };
  __striped static const int16_t kCases[23][3] = {
      {-32768, 0, 0},    {-1, 0, 0},       {0, 0, 0},        {64, 0, 0},
      {65, 0, 0},        {65, 64, -64},    {65, -64, 64},    {65, 65, 0},
      {65, -65, 0},      {65, 0, 65},      {65, 0, -65},     {65, -32768, 0},
      {65, 10, -32768},  {128, -127, 127}, {129, 128, -128}, {218, -50, 30},
      {219, 50, -30},    {372, -371, 200}, {373, 100, -372}, {400, 0, 0},
      {401, 1, -1},      {2500, -1999, 800}, {32767, -32766, 32766},
  };
  for (uint8_t vi = 0; vi < sizeof(kViews) / sizeof(kViews[0]); ++vi) {
    for (uint8_t ci = 0; ci < sizeof(kCases) / sizeof(kCases[0]); ++ci) {
      _ts_c0 = make_vector(kCases[ci][0], kCases[ci][1], kCases[ci][2]);
      _ts_f1.hidden = _ts_f2.hidden = 99;
      _ts_f1.cached = _ts_f2.cached = true;
      _ts_f1.slid = _ts_f2.slid = 77;
      bool got_ok = planes_centre(&kViews[vi], &_ts_c0, &_ts_f1);
      bool want_ok = _ref_planes_centre(&kViews[vi], &_ts_c0, &_ts_f2);
      expect_id((int16_t)(((got_ok ? 1 : 0) << 8) | _ts_f1.hidden),
                (int16_t)(((want_ok ? 1 : 0) << 8) | _ts_f2.hidden));
      expect_id((int16_t)(((_ts_f1.cached ? 1 : 0) << 8) | _ts_f1.slid),
                (int16_t)(((_ts_f2.cached ? 1 : 0) << 8) | _ts_f2.slid));
      if (want_ok) {
        expect_id((int16_t)(((_ts_f1.clamped ? 1 : 0) << 8) | _ts_f1.k),
                  (int16_t)(((_ts_f2.clamped ? 1 : 0) << 8) | _ts_f2.k));
        expect_id(_ts_f1.d, _ts_f2.d);
        expect_id(_ts_f1.cx, _ts_f2.cx);
        expect_id(_ts_f1.cy, _ts_f2.cy);
      }
    }
  }
}

// 20. sprites_asm.cc sprites_stack_add, sprites_stack_commit,
// _sprites_set_instrument_sprite, and sprites_set_alt against 16-bit C
// references across culls, dither lattice, message box, sort/overflow, 1x1..2x2
// slots, unused parking / past-last preservation, orientation mark, fin, all
// gauge indices/directions, and altitudes 0..65535.
uint16_t msg_span_x0;
uint16_t msg_span_x1;
static bool _test_msg_on;
__noinline bool msg_active(void) { return _test_msg_on; }
#include "../sprites_asm.h"

static sprite_cand_t *const _ref_cand = (sprite_cand_t *)0xFC00;
static sprite_frame_t *const _ref_frames = (sprite_frame_t *)0xFC80;
static uint8_t _ref_cand_end;

static bool _ref_stack_add(int16_t depth, int16_t x, int16_t y, int8_t pivot_x,
                           int8_t pivot_y, uint8_t bitmap, uint8_t bitmap2,
                           uint8_t color, uint8_t flags) {
  uint8_t width = kSpriteWidthPixels;
  uint8_t px = (uint8_t)pivot_x;
  if (flags & kSpriteFlagExpandX) {
    width <<= 1;
    px <<= 1;
  }
  x -= px;
  if (flags & kSpriteFlagWide) {
    width <<= 1;
  }
  uint8_t pitch = kSpriteHeightPixels;
  if (flags & kSpriteFlagExpandY) {
    pitch <<= 1;
  }
  uint8_t h_m1 = pitch - 1;
  if (bitmap2 != kSpriteNoBitmap) {
    h_m1 += pitch;
  }
  y -= (uint8_t)pivot_y;
  if (flags & kSpriteFlagAlignDither) {
    x &= ~1;
    y = (y & ~1) | ((x >> 1) & 1);
  }
  if (x >= (int16_t)kScreenWidthPixels || x + width <= 0) {
    return false;
  }
  if ((uint16_t)(y + h_m1) >=
      kSpriteVisibleEndYPixels + kSpriteHeightPixels - 1) {
    return false;
  }
  int16_t vx = x + (int16_t)kSpriteOffsetX;
  uint8_t vy = (uint8_t)y + kSpriteOffsetY;
  if (vx < 0) {
    return false;
  }
  if (_test_msg_on && vy < kSpritesMsgBoxBottom) {
    int16_t x0 = kSpriteOffsetX + (int16_t)msg_span_x0;
    int16_t x1 = kSpriteOffsetX + (int16_t)msg_span_x1;
    if (vx < x1 && vx + width > x0) {
      return false;
    }
  }
  uint8_t o = _ref_cand_end;
  if (o == kSpriteCandBytes) {
    if ((uint16_t)depth >= (uint16_t)_ref_cand[kSpriteStackSize - 1].depth) {
      return false;
    }
    o -= kSpriteCandSize;
  } else {
    _ref_cand_end = o + kSpriteCandSize;
  }
  while (o != 0) {
    uint8_t prev = o - kSpriteCandSize;
    const sprite_cand_t *p =
        (const sprite_cand_t *)((const uint8_t *)_ref_cand + prev);
    if ((uint16_t)p->depth <= (uint16_t)depth) {
      break;
    }
    uint8_t *b = (uint8_t *)_ref_cand;
    uint8_t k = o + kSpriteCandSize;
    do {
      --k;
      b[k] = b[k - kSpriteCandSize];
    } while (k != o);
    o = prev;
  }
  sprite_cand_t *c = (sprite_cand_t *)((uint8_t *)_ref_cand + o);
  c->depth = depth;
  c->x = vx;
  c->y = vy;
  c->bitmap = bitmap;
  c->bitmap2 = bitmap2;
  c->color = color;
  c->flags = flags;
  return true;
}

static void _ref_stack_commit(void) {
  sprite_frame_t *f = mem_using_alt_buffer ? &_ref_frames[1] : &_ref_frames[0];
  uint8_t idx = kSpriteStackFirst;
  uint8_t bit = 1 << kSpriteStackFirst;
  if (_sprites_fin_off == 0) {
    idx = kSpriteFinFirst + kSpriteFinCount;
    bit = 1 << (kSpriteFinFirst + kSpriteFinCount);
  }
  uint8_t msbx = 0, expand = 0, expand_y = 0, enable = 0;
  for (uint8_t o = 0; o != _ref_cand_end; o += kSpriteCandSize) {
    const sprite_cand_t *c =
        (const sprite_cand_t *)((const uint8_t *)_ref_cand + o);
    uint8_t flags = c->flags;
    uint8_t cols = (flags & kSpriteFlagWide) ? 2 : 1;
    uint8_t slots =
        (c->bitmap2 == kSpriteNoBitmap) ? cols : (uint8_t)(cols << 1);
    if (idx + slots > kSpriteStackSize) {
      break;
    }
    uint8_t ptr = c->bitmap;
    uint8_t sy = c->y;
    uint8_t col_w = (flags & kSpriteFlagExpandX) ? 2 * kSpriteWidthPixels
                                                 : kSpriteWidthPixels;
    uint8_t pitch = (flags & kSpriteFlagExpandY) ? 2 * kSpriteHeightPixels
                                                 : kSpriteHeightPixels;
    for (uint8_t s = 0; s < slots; ++s) {
      uint16_t sx = (uint16_t)c->x;
      uint8_t p = ptr;
      if ((cols & 2) && (s & 1)) {
        sx += col_w;
        ++p;
      }
      f->pos[idx << 1] = (uint8_t)sx;
      f->pos[(idx << 1) + 1] = sy;
      f->ptr[idx] = p;
      f->color[idx] = c->color;
      enable |= bit;
      if (sx & 0x100) {
        msbx |= bit;
      }
      if (flags & kSpriteFlagExpandX) {
        expand |= bit;
      }
      if (flags & kSpriteFlagExpandY) {
        expand_y |= bit;
      }
      if (s + 1 == cols) {
        ptr = c->bitmap2;
        sy += pitch;
      }
      bit <<= 1;
      ++idx;
    }
  }
  f->last = idx - 1;
  while (idx < kSpriteStackSize) {
    f->pos[idx << 1] = 0;
    f->pos[(idx << 1) + 1] = 0;
    ++idx;
  }
  uint8_t ox = 0, oy = 0, op = kSpriteDefSun.bitmap_idx, oc = kColorInstrument;
  if (_sprites_orient_off == 0) {
    ox = kSpriteOrientVX;
    oy = kSpriteOrientVY;
    op = kSpriteDefOrient.bitmap_idx;
    oc = kColorOrientation;
    enable |= kSpriteOrientBit;
  }
  f->pos[0] = ox;
  f->pos[1] = oy;
  f->ptr[0] = op;
  f->color[0] = oc;
  if (_sprites_fin_off == 0) {
    f->pos[0] = kSpriteFinVX;
    f->pos[1] = kSpriteFinTopVY;
    f->pos[2] = kSpriteFinVX;
    f->pos[3] = kSpriteFinBottomVY;
    f->ptr[0] = kFinSpriteBlock;
    f->ptr[1] = kFinSpriteBlock + 1;
    f->color[0] = kColorAircraft;
    f->color[1] = kColorAircraft;
    enable |= kSpriteFinMask;
    expand_y |= kSpriteFinMask;
  }
  f->msbx = msbx;
  f->expand = expand;
  f->expand_y = expand_y;
  f->enable = enable;
}

static void _check_frames_eq(void) {
  const volatile uint8_t *got = (const volatile uint8_t *)_sprites_frames;
  const uint8_t *want = (const uint8_t *)_ref_frames;
  for (uint8_t i = 0; i < sizeof(_sprites_frames); ++i) {
    expect_id(got[i], want[i]);
  }
}

struct CullPreset {
  int8_t px, py;
  uint8_t b1, b2, col, fl;
};

struct CullShort {
  int16_t x, y;
  uint8_t p;
};

struct CullCase {
  int16_t x, y;
  int8_t px, py;
  uint8_t b1, b2, col, fl;
};

struct AddCase {
  int16_t d;
  CullCase c;
};

static __noinline void _run_cull(int16_t d, const CullCase *c) {
  bool g = sprites_stack_add(d, c->x, c->y, c->px, c->py, c->b1, c->b2, c->col,
                             c->fl);
  bool w =
      _ref_stack_add(d, c->x, c->y, c->px, c->py, c->b1, c->b2, c->col, c->fl);
  expect_id(g ? 1 : 0, w ? 1 : 0);
}

static __noinline void test_sprites_asm(void) {
  _cur_id = 20;
  static const CullPreset kCP[7] = {
      {12, 10, 95, 0xFF, 7, 0},    {12, 10, 81, 0xFF, 1, 0x01},
      {12, 20, 85, 86, 1, 0x01},   {0, 0, 69, 71, 12, 0x0D},
      {12, 10, 82, 0xFF, 1, 0x03}, {0, 0, 95, 0xFF, 7, 0},
      {0, 10, 95, 0xFF, 7, 0},
  };
  __striped static const CullShort kCulls[28] = {
      {-13, 50, 0},    {-12, 50, 0},   {-11, 50, 0},    {0, 50, 1},
      {-1, 50, 1},     {331, 50, 0},   {332, 50, 0},    {160, -11, 0},
      {160, -10, 0},   {160, 99, 0},   {160, 100, 0},   {160, 88, 2},
      {160, 89, 2},    {100, 26, 3},   {100, 27, 3},    {161, 55, 4},
      {162, 55, 4},    {163, 56, 4},   {5, -9, 4},      {88, 17, 0},
      {89, 17, 0},     {232, 17, 0},   {231, 17, 0},    {160, 18, 0},
      {-32768, 50, 0}, {32767, 50, 5}, {160, -32768, 6},{160, 32767, 5},
  };
  msg_span_x0 = 100;
  msg_span_x1 = 220;
  for (uint8_t m = 0; m < 2; ++m) {
    _test_msg_on = (m != 0);
    for (uint8_t i = 0; i < 28; ++i) {
      _sprites_cand_end = 0;
      _ref_cand_end = 0;
      const CullPreset *cp = &kCP[kCulls[i].p];
      #pragma bss(opp_bss)
      static CullCase cc;
      #pragma bss(bss)
      cc.x = kCulls[i].x;
      cc.y = kCulls[i].y;
      cc.px = cp->px;
      cc.py = cp->py;
      cc.b1 = cp->b1;
      cc.b2 = cp->b2;
      cc.col = cp->col;
      cc.fl = cp->fl;
      _run_cull(100, &cc);
      expect_id(_sprites_cand_end, _ref_cand_end);
      if (_ref_cand_end != 0) {
        for (uint8_t b = 0; b < kSpriteCandSize; ++b) {
          expect_id(((const uint8_t *)_sprites_cand)[b],
                    ((const uint8_t *)_ref_cand)[b]);
        }
      }
    }
  }
  _test_msg_on = false;

  // Multi-frame commit tests: empty, 9-item overflow + ties, mixed 2x2 + 1x2 +
  // msbx split, orientation mark, tail fin, and alt-buffer sentinel check.
  static const AddCase kMixed[5] = {
      {0x7FFF, {300, 40, 12, 10, 95, 0xFF, 7, 0}}, // sun with msbx
      {450, {230, 12, 0, 0, 69, 71, 11, 0x0D}},    // 2x2 plane crossing 256
      {300, {161, 45, 12, 20, 85, 86, 1, 0x03}},   // 1x2 dithered cloud
      {300, {80, 30, 12, 10, 82, 0xFF, 1, 0x03}},  // tie at depth 300
      {120, {40, 20, 12, 10, 90, 0xFF, 2, 0}},     // close tracer
  };
  volatile uint8_t *raw_got = (volatile uint8_t *)_sprites_frames;
  uint8_t *raw_want = (uint8_t *)_ref_frames;
  for (uint8_t b = 0; b < sizeof(_sprites_frames); ++b) {
    raw_got[b] = 0xAA;
    raw_want[b] = 0xAA;
  }
  #pragma bss(opp_bss)
  static CullCase mc;
  static uint8_t alt_g[6];
  #pragma bss(bss)
  for (uint8_t mode = 0; mode < 4; ++mode) {
    mem_using_alt_buffer = (mode & 1) != 0;
    _sprites_orient_off = (mode == 1) ? 0 : 0xFF;
    _sprites_fin_off = (mode == 2) ? 0 : 0xFF;
    _sprites_cand_end = 0;
    _ref_cand_end = 0;
    if (mode == 3) {
      // 10 descending/interleaved items to exercise full-stack eviction & rejection.
      for (uint8_t i = 0; i < 10; ++i) {
        mc.x = (int16_t)(20 + i * 28);
        mc.y = 35;
        mc.px = 12;
        mc.py = 10;
        mc.b1 = (uint8_t)(40 + i);
        mc.b2 = 0xFF;
        mc.col = (uint8_t)(i & 15);
        mc.fl = 0;
        _run_cull((i == 9) ? (int16_t)999 : (int16_t)(900 - i * 80), &mc);
      }
    } else if (mode != 0) {
      for (uint8_t i = 0; i < 5; ++i) {
        _run_cull(kMixed[i].d, &kMixed[i].c);
      }
    }
    sprites_stack_commit();
    _ref_stack_commit();
    _check_frames_eq();
  }
  mem_using_alt_buffer = false;

  // Instrument needle placement across all 6 gauge slots and dir bytes,
  // including mirrored directions (dir & 16) and wrapped upper bits.
  static const uint8_t kGaugeIdxs[] = {0, 1, 4, 5, 6, 7};
  static const uint8_t kGaugeDirs[] = {
      0,  1,  7,  8,  15, 16, 17, 23, 24, 31, 32, 47, 48, 63, 0x94, 0xFF,
  };
  for (uint8_t gi = 0; gi < sizeof(kGaugeIdxs); ++gi) {
    uint8_t idx = kGaugeIdxs[gi];
    for (uint8_t di = 0; di < sizeof(kGaugeDirs); ++di) {
      uint8_t dir = kGaugeDirs[di];
      _sprites_set_instrument_sprite(idx, dir);
      uint8_t arm_idx = dir & (kSpriteDefArmCount - 1);
      uint8_t off = (uint8_t)(arm_idx << 1);
      int8_t px, py;
      uint8_t bitmap;
      if (idx == kSpriteIdxSpeed || idx == kSpriteIdxAlt1) {
        px = ((const int8_t *)kSpriteDefLongArm)[off];
        py = ((const int8_t *)kSpriteDefLongArm)[off + 1];
        bitmap = kSpriteDefLongArmBitmap;
      } else {
        px = ((const int8_t *)kSpriteDefShortArm)[off];
        py = ((const int8_t *)kSpriteDefShortArm)[off + 1];
        bitmap = kSpriteDefShortArmBitmap;
      }
      if (dir & kSpriteDefArmCount) {
        px = 2 * kSpriteDefArmCenterX - px;
        py = 2 * kSpriteDefArmCenterY - py;
      }
      expect_id(
          (int16_t)((_sprites_instrument_xy[idx].x << 8) |
                    _sprites_instrument_xy[idx].y),
          (int16_t)(((uint8_t)(kSpriteInstrumentPivotX[idx] - px) << 8) |
                    (uint8_t)(kSpriteInstrumentPivotY[idx] - py)));
      expect_id(_sprites_instrument_idx[idx], (uint8_t)(bitmap + arm_idx));
    }
  }

  // sprites_set_alt across 0..65535 boundaries and a dense sweep of a = 0..4095.
  for (uint16_t a = 0; a <= 4095; a = (a < 32 || a > 4064) ? (a + 1) : (a + 11)) {
    sprites_set_alt((uint16_t)((a << 4) | (a & 15)));
    alt_g[0] = _sprites_instrument_xy[4].x;
    alt_g[1] = _sprites_instrument_xy[4].y;
    alt_g[2] = _sprites_instrument_xy[5].x;
    alt_g[3] = _sprites_instrument_xy[5].y;
    alt_g[4] = _sprites_instrument_idx[4];
    alt_g[5] = _sprites_instrument_idx[5];
    _sprites_set_instrument_sprite(4, (uint8_t)a);
    _sprites_set_instrument_sprite(
        5, ((uint8_t)vec_fastmul8p8((int16_t)a, 205)) >> 3);
    if (alt_g[0] != _sprites_instrument_xy[4].x ||
        alt_g[1] != _sprites_instrument_xy[4].y ||
        alt_g[2] != _sprites_instrument_xy[5].x ||
        alt_g[3] != _sprites_instrument_xy[5].y ||
        alt_g[4] != _sprites_instrument_idx[4]) {
      expect_id(-1, 0);
    }
    expect_id(alt_g[5], _sprites_instrument_idx[5]);
    if (a == 4095) {
      break;
    }
  }

  // sprites_set_vspeed across [-32768, 32767] clamp and 64-step boundaries.
  static const int16_t kVsEdges[] = {
      -32768, -1024, -832, -769, -768, -767, -705, -704, -65, -64, -1, 0,
      1,      63,    64,   703,  704,  767,  768,  769,  831, 832, 32767,
  };
  for (uint8_t vi = 0; vi < sizeof(kVsEdges) / sizeof(kVsEdges[0]); ++vi) {
    int16_t vs = kVsEdges[vi];
    sprites_set_vspeed(vs);
    uint8_t gx = _sprites_instrument_xy[0].x, gy = _sprites_instrument_xy[0].y;
    uint8_t gi = _sprites_instrument_idx[0];
    int16_t vc = vs > 0x300 ? 0x300 : (vs < -0x300 ? -0x300 : vs);
    _sprites_set_instrument_sprite(0, (uint8_t)(0x18 + (vc >> 6)));
    expect_id((int16_t)((gx << 8) | gy),
           (int16_t)((_sprites_instrument_xy[0].x << 8) |
                     _sprites_instrument_xy[0].y));
    expect_id(gi, _sprites_instrument_idx[0]);
  }
}

// 21. clouds_asm.cc: _clouds_build_basis, _clouds_dot, _clouds_apply_offset,
// _clouds_scan_init, and _clouds_scan_next against 16-bit C references.
#pragma bss(cld_bss)
int32_t flight_eye_x;
int32_t flight_eye_y;
int32_t flight_eye_z;
#pragma bss(bss)
#include "../clouds_asm.h"

static bool _ref_clouds_outside(int16_t v, int16_t r) {
  return (uint16_t)(v + r) > (uint16_t)(r << 1);
}

static __noinline void test_clouds_asm(void) {
  _cur_id = 21;

  // 21a. _clouds_build_basis exhaustively over all 513 values in [-256, 256].
  int16_t *cam_elem = &world_cam.front.x;
  for (int16_t base = -256; base <= 256; base += 9) {
    for (uint8_t k = 0; k < 9; ++k) {
      int16_t v = base + k;
      cam_elem[k] = v > 256 ? 256 : v;
    }
    _clouds_build_basis();
    for (uint8_t k = 0; k < 9; ++k) {
      expect_id(_clouds_half_basis[k], vec_fastmul8p8(cam_elem[k], 16));
    }
  }

  // 21b. _clouds_dot across 0, +-256, general +-1..255, cold & cached z.
  static const mat3_t kCloudCams[] = {
      {{256, 0, 0}, {0, 256, 0}, {0, 0, 256}},
      {{-256, 0, 0}, {0, -256, 0}, {0, 0, -256}},
      {{204, 153, -17}, {-153, 204, 42}, {33, -29, 252}},
      {{1, -255, 128}, {-127, 64, -193}, {255, -1, 0}},
  };
  static const int16_t kDotV[] = {-4096, -2049, -256, -1, 0, 1, 256, 1777, 4096};
  static const int16_t kDotDz[] = {-1200, -1, 0, 700};
  for (uint8_t ci = 0; ci < sizeof(kCloudCams) / sizeof(kCloudCams[0]); ++ci) {
    world_cam = kCloudCams[ci];
    for (uint8_t di = 0; di < sizeof(kDotDz) / sizeof(kDotDz[0]); ++di) {
      _clouds_dz = kDotDz[di];
      _clouds_z_valid[0] = 0;
      _clouds_z_valid[1] = 0;
      _clouds_z_valid[2] = 0;
      for (uint8_t vi = 0; vi < sizeof(kDotV) / sizeof(kDotV[0]); ++vi) {
        vec_v.x = kDotV[vi];
        vec_v.y = kDotV[(uint8_t)(vi + 3) % (sizeof(kDotV) / sizeof(kDotV[0]))];
        for (uint8_t a = 0; a < 3; ++a) {
          const vec3_t *row = &world_cam.front + a;
          int16_t want = vec_fastmul8p8(row->z, _clouds_dz) +
                         vec_fastmul8p8(row->x, vec_v.x) +
                         vec_fastmul8p8(row->y, vec_v.y);
          expect_id(_clouds_dot(a), want);
        }
      }
    }
  }

  // 21c. _clouds_apply_offset across all 4 patterns x 3 blobs & carry edges.
  static const vec3_t kCentres[] = {
      {0, 0, 0},
      {255, -256, 1024},
      {16, 256, -1},
      {4096, -4096, 512},
  };
  world_cam = kCloudCams[2];
  _clouds_build_basis();
  for (uint8_t ci = 0; ci < sizeof(kCentres) / sizeof(kCentres[0]); ++ci) {
    _clouds_centre = kCentres[ci];
    for (uint8_t p = 0; p < kCloudPatternCount; ++p) {
      _clouds_pat_off = (uint8_t)(p * 9);
      for (uint8_t b = 0; b < kCloudBlobsPerGroup; ++b) {
        int16_t wx = _clouds_centre.x;
        int16_t wy = _clouds_centre.y;
        int16_t wz = _clouds_centre.z;
        for (uint8_t ax = 0; ax < 3; ++ax) {
          int8_t c = kCloudGroupOffset[p][b][ax];
          wx += (int16_t)c * (int16_t)_clouds_half_basis[ax + 0];
          wy += (int16_t)c * (int16_t)_clouds_half_basis[ax + 3];
          wz += (int16_t)c * (int16_t)_clouds_half_basis[ax + 6];
        }
        int16_t ret_x = _clouds_apply_offset();
        expect_id(ret_x, wx);
        expect_id(*(volatile int16_t *)&vec_v.x, wx);
        expect_id(*(volatile int16_t *)&vec_v.y, wy);
        expect_id(*(volatile int16_t *)&vec_v.z, wz);
        expect_id(*(volatile uint8_t *)&_clouds_pat_off,
                  (int16_t)(p * 9 + (b + 1) * 3));
      }
    }
  }

  // 21d. Full _clouds_scan_init + _clouds_scan_next across diverse poses.
  static const int32_t kScanEyes[][3] = {
      {0x00200000L, 0x00400000L, 0x00010000L},
      {0x00371234L, 0x00589ABCL, 0x00028000L},
      {0x00000200L, 0x00000000L, 0x00080000L},
      {0x007FFFE0L, 0x00FFF010L, 0x00002000L},
  };
  for (uint8_t pi = 0; pi < sizeof(kScanEyes) / sizeof(kScanEyes[0]); ++pi) {
    flight_eye_x = kScanEyes[pi][0];
    flight_eye_y = kScanEyes[pi][1];
    flight_eye_z = kScanEyes[pi][2];
    world_cam = kCloudCams[pi];

    _clouds_scan_init();

    // Independent C reference scan matching pre-Pass 153 clouds.cc.
    const uint16_t mx = (uint16_t)((uint32_t)flight_eye_x >> 9);
    const uint16_t my = (uint16_t)((uint32_t)flight_eye_y >> 9);
    const int16_t rdz = kCloudDeckU - (int16_t)((uint32_t)flight_eye_z >> 9);
    expect_id(_clouds_dz, rdz);
    const uint16_t frac_x = (uint16_t)-(int16_t)(mx & (kCloudCellU - 1));
    const uint16_t frac_y = (uint16_t)-(int16_t)(my & (kCloudCellU - 1));
    uint8_t cx = (uint8_t)(mx >> kCloudCellShift) - kCloudScanRadius;
    const uint8_t start_cy =
        (uint8_t)(my >> kCloudCellShift) - kCloudScanRadius;
    for (uint8_t ux_hi = 0; ux_hi < 80; ux_hi += 16, ++cx) {
      const uint8_t hx = kCloudHashX[cx & kCloudCellMaskX];
      uint8_t cy = start_cy;
      for (uint8_t uy_hi = 0; uy_hi < 80; uy_hi += 16, ++cy) {
        const uint8_t idx = (hx ^ kCloudHashY[cy & kCloudCellMaskY]) & 31;
        const uint8_t ha = kCloudHashA[idx];
        if ((ha & kCloudGateBits) >= kCloudGateLimit) {
          continue;
        }
        const uint8_t hb = kCloudHashB[idx];
        const uint16_t ox =
            frac_x + ((uint16_t)(ux_hi | (hb & 0x0F)) << kCloudJitterShift);
        if (ox > (uint16_t)(kCloudCullU << 1)) {
          continue;
        }
        const uint16_t oy =
            frac_y + ((uint16_t)(uy_hi | (hb >> 4)) << kCloudJitterShift);
        if (oy > (uint16_t)(kCloudCullU << 1)) {
          continue;
        }
        int16_t vx = (int16_t)(ox - kCloudCullU);
        int16_t vy = (int16_t)(oy - kCloudCullU);
        int16_t rx = vec_fastmul8p8(world_cam.front.z, rdz) +
                     vec_fastmul8p8(world_cam.front.x, vx) +
                     vec_fastmul8p8(world_cam.front.y, vy);
        if (rx <= 8 || rx > kCloudRungDepth[0]) {
          continue;
        }
        int16_t vert = (rx >> 2) + 128;
        int16_t rz = vec_fastmul8p8(world_cam.up.z, rdz) +
                     vec_fastmul8p8(world_cam.up.x, vx) +
                     vec_fastmul8p8(world_cam.up.y, vy);
        if (_ref_clouds_outside(rz, vert)) {
          continue;
        }
        int16_t lat = (rx >> 1) + (vert >> 1) + 64;
        int16_t ry = vec_fastmul8p8(world_cam.left.z, rdz) +
                     vec_fastmul8p8(world_cam.left.x, vx) +
                     vec_fastmul8p8(world_cam.left.y, vy);
        if (_ref_clouds_outside(ry, lat)) {
          continue;
        }
        uint8_t wrung = 0;
        while (wrung < kCloudRungCount - 1 &&
               rx <= kCloudRungDepth[wrung + 1]) {
          ++wrung;
        }

        uint8_t got_rung = _clouds_scan_next();
        expect_id(got_rung, wrung);
        expect_id(*(volatile int16_t *)&_clouds_centre.x, rx);
        expect_id(*(volatile int16_t *)&_clouds_centre.y, ry);
        expect_id(*(volatile int16_t *)&_clouds_centre.z, rz);
        if (wrung < kCloudRungCollapsed) {
          expect_id(*(volatile int16_t *)&vec_v.x, rx);
          expect_id(*(volatile int16_t *)&vec_v.y, ry);
          expect_id(*(volatile int16_t *)&vec_v.z, rz);
        } else {
          expect_id(*(volatile uint8_t *)&_clouds_pat_off,
                    (int16_t)((ha >> 6) * 9));
          expect_id(*(volatile uint8_t *)&_clouds_basis_valid, 1);
        }
      }
    }
    expect_id(_clouds_scan_next(), 0xFF);
    expect_id(_clouds_scan_next(), 0xFF);
  }
}

// ---------------------------------------------------------------------------
// 22. world_asm.cc: _world_init_start_dx_dy, _world_build_obj_verts,
//     _world_render_grid_walk against exact 16-bit C references.
// ---------------------------------------------------------------------------
static void _ref_world_split_vec(vec3_t *v, vec3_t d9[9]) {
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

static const uint8_t kRefMitchX[16] = {0, 36, 0,  12, 36, 12, 24, 0,
                                       36, 24, 0, 12, 24, 24, 12, 24};
static const uint8_t kRefMitchY[16] = {0, 0,  36, 12, 24, 36, 0,  12,
                                       36, 24, 24, 0, 12, 12, 24, 36};
static const uint8_t kRefNumPts3[4] = {8, 4, 2, 1};
static const uint8_t kRefNumPts4[5] = {4, 2, 2, 1, 1};

static __noinline void test_world_grid_asm(void) {
  // 4 orthonormal-ish 8.8 camera poses covering positive, negative, and zero
  // front.{x,y}, level flight, banked roll, and nose-down pitch, across all
  // three altitude radii (2, 3, 4).
  static const struct {
    mat3_t cam;
    int32_t ex, ey, ez;
  } kCases[4] = {
      // Case 0: radius 2 (low), front.x > 0, front.y == 0
      {{{256, 0, 0}, {0, 256, 0}, {0, 0, 256}}, 0x105E83, 0x17802A, 0x004000},
      // Case 1: radius 3 (mid), pitched down + rolled, front.x < 0, front.y > 0
      {{{-177, 177, -51}, {-181, -181, 0}, {-36, 36, 251}},
       0x223456,
       0x345678,
       0x010000},
      // Case 2: radius 4 (high), level diagonal, front.x > 0, front.y < 0
      {{{181, -181, 0}, {181, 181, 0}, {0, 0, 256}},
       0x400000,
       0x580200,
       0x038000},
      // Case 3: radius 3 (mid), front.x == 0, front.y < 0, steep roll
      {{{0, -256, 0}, {222, 0, -128}, {128, 0, 222}},
       0x051234,
       0x0A5678,
       0x018000},
  };

#pragma bss(cld_bss)
  static vec3_t ref_d4[18];
  static int16_t ref_mx[16], ref_my[16], ref_mz[16];
#pragma bss(bss)
  _init_row_ptrs();

  for (uint8_t ci = 0; ci < 4; ++ci) {
    world_cam = kCases[ci].cam;
    flight_eye_x = kCases[ci].ex;
    flight_eye_y = kCases[ci].ey;
    flight_eye_z = kCases[ci].ez;

    // 1. Run ASM _world_init_start_dx_dy().
    _world_init_start_dx_dy();

    // Compute exact C reference.
    uint16_t nz = (uint16_t)((uint32_t)flight_eye_z >> 9);
    uint8_t ref_r = (nz >= 256) ? 4 : (nz >= 64) ? 3 : 2;
    int16_t spacing = (int16_t)(0x400 * ref_r);
    vec3_t ref_dx = {0, 0, 0}, ref_dy = {0, 0, 0}, ref_ps = {0, 0, 0};
    int8_t ref_sx = 0, ref_sy = 0;
    uint8_t ref_cx = 0, ref_cy = 0;

    for (uint8_t ax = 0; ax < 2; ++ax) {
      const int16_t *cam = &world_cam.front.x + ax;
      vec3_t dv = {cam[0], cam[3], cam[6]};
      _ref_world_split_vec(&dv, ax ? (ref_d4 + 9) : ref_d4);
      uint16_t m =
          (uint16_t)((uint32_t)(ax ? flight_eye_y : flight_eye_x) >> 9);
      uint8_t grid = (uint8_t)((m + 512) >> 10);
      int16_t p = spacing;
      int8_t st = -1;
      uint8_t stc = (uint8_t)(grid + ref_r);
      if (cam[0] > 0) {
        dv.x = -dv.x;
        dv.y = -dv.y;
        dv.z = -dv.z;
      } else {
        p = -p;
        st = 1;
        stc = (uint8_t)(grid - ref_r);
      }
      int16_t coord = p - (int16_t)(m - ((uint16_t)grid << 10));
      if (ax == 0) {
        ref_dx = dv;
        ref_sx = st;
        ref_cx = stc;
        ref_ps.x = coord;
      } else {
        ref_dy = dv;
        ref_sy = st;
        ref_cy = stc;
        ref_ps.y = coord;
      }
    }
    ref_ps.z = -(int16_t)nz;
    for (uint8_t i = 0; i < 16; ++i) {
      const vec3_t *dx =
          (const vec3_t *)((const uint8_t *)ref_d4 + kRefMitchX[i]);
      const vec3_t *dy =
          (const vec3_t *)((const uint8_t *)(ref_d4 + 9) + kRefMitchY[i]);
      ref_mx[i] = dx->x + dy->x;
      ref_my[i] = dx->y + dy->y;
      ref_mz[i] = dx->z + dy->z;
    }
    vec_transform_inv(&world_cam, &ref_ps, &ref_ps);

    expect(22, _world_grid_radius, ref_r);
    expect(22, _world_step_x, ref_sx);
    expect(22, _world_step_y, ref_sy);
    expect(22, _world_start_cx, ref_cx);
    expect(22, _world_start_cy, ref_cy);
    expect(22, _world_dx_vec.x, ref_dx.x);
    expect(22, _world_dx_vec.y, ref_dx.y);
    expect(22, _world_dx_vec.z, ref_dx.z);
    expect(22, _world_dy_vec.x, ref_dy.x);
    expect(22, _world_dy_vec.y, ref_dy.y);
    expect(22, _world_dy_vec.z, ref_dy.z);
    expect(22, _world_p_start.x, ref_ps.x);
    expect(22, _world_p_start.y, ref_ps.y);
    expect(22, _world_p_start.z, ref_ps.z);
    for (uint8_t i = 0; i < 18; ++i) {
      expect(22, _world_d4[i].x, ref_d4[i].x);
      expect(22, _world_d4[i].y, ref_d4[i].y);
      expect(22, _world_d4[i].z, ref_d4[i].z);
    }
    for (uint8_t i = 0; i < 16; ++i) {
      expect(22, _world_mitch_x[i], ref_mx[i]);
      expect(22, _world_mitch_y[i], ref_my[i]);
      expect(22, _world_mitch_z[i], ref_mz[i]);
    }

    // 2. Test _world_build_obj_verts() across all 9 object types (both visible
    //    and all-behind x < 8 early-reject).
    for (uint8_t oi = 0; oi < kWorldObjDim; ++oi) {
      for (uint8_t behind = 0; behind < 2; ++behind) {
        _world_vec_v.x = behind ? -1500 : 400;
        _world_vec_v.y = (int16_t)(ci * 130 - 200);
        _world_vec_v.z = -90;
        uint8_t got_n = _world_build_obj_verts(oi);
        uint8_t nv = kWorldObjNumVerts[oi];
        uint8_t k = kWorldObjFirstVert[oi];
        bool any_in = false;
        vec3_t rv[kPolyMaxVertices];
        for (uint8_t vi = 0; vi < nv; ++vi, ++k) {
          const vec3_t *dx =
              (const vec3_t *)((const uint8_t *)ref_d4 + kWorldObjX[k]);
          const vec3_t *dy =
              (const vec3_t *)((const uint8_t *)(ref_d4 + 9) + kWorldObjY[k]);
          rv[vi].x = _world_vec_v.x + dx->x + dy->x;
          rv[vi].y = _world_vec_v.y + dx->y + dy->y;
          rv[vi].z = _world_vec_v.z + dx->z + dy->z;
          if (rv[vi].x >= 8) {
            any_in = true;
          }
        }
        uint8_t want_n = any_in ? nv : 0;
        expect(22, got_n, want_n);
        if (want_n) {
          for (uint8_t vi = 0; vi < nv; ++vi) {
            expect(22, _world_poly_verts[vi].x, rv[vi].x);
            expect(22, _world_poly_verts[vi].y, rv[vi].y);
            expect(22, _world_poly_verts[vi].z, rv[vi].z);
          }
        }
      }
    }

    // 3. Test _world_render_grid_walk() end-to-end against exact C reference:
    //    both dot rendering into screen/color RAM and visible object dispatch.
    _clear_test_screens(kCharSolidGround, 0x55);
    _test_wg_obj_hash = 0;
    _test_wg_obj_cnt = 0;
    _world_init_start_dx_dy();
    _world_render_grid_walk();
    uint16_t got_oh = _test_wg_obj_hash;
    uint8_t got_oc = _test_wg_obj_cnt;

    // Run pure C reference walk using ref_ps/ref_dx/ref_dy/ref_d4.
    int16_t a0 = ref_d4[0].x < 0 ? -ref_d4[0].x : ref_d4[0].x;
    int16_t b0 = ref_d4[9].x < 0 ? -ref_d4[9].x : ref_d4[9].x;
    int16_t bailout = -(a0 > b0 ? a0 : b0);
    const uint8_t *npts = (ref_r == 4) ? kRefNumPts4 : kRefNumPts3;
    uint16_t ref_oh = 0;
    uint8_t ref_oc = 0;
    vec3_t ps = ref_ps;
    uint8_t cx = ref_cx;
    for (int8_t gx = -(int8_t)ref_r;;) {
      _world_vec_v = ps;
      uint8_t ax = (uint8_t)(gx < 0 ? -gx : gx);
      const uint8_t *row = kWorldMap[cx & 0x0F];
      uint8_t cy = ref_cy;
      for (int8_t gy = -(int8_t)ref_r;;) {
        uint8_t col = cy & 0x1F;
        uint8_t pair = row[col >> 1];
        uint8_t mt = (col & 1) ? (uint8_t)(pair >> 4) : (uint8_t)(pair & 0x0F);
        if (mt >= kWorldMapObjStart) {
          uint8_t oi = (uint8_t)(mt - kWorldMapObjStart);
          uint8_t nv = kWorldObjNumVerts[oi];
          uint8_t k = kWorldObjFirstVert[oi];
          vec3_t rv[kPolyMaxVertices];
          for (uint8_t vi = 0; vi < nv; ++vi, ++k) {
            const vec3_t *dx =
                (const vec3_t *)((const uint8_t *)ref_d4 + kWorldObjX[k]);
            const vec3_t *dy =
                (const vec3_t *)((const uint8_t *)(ref_d4 + 9) + kWorldObjY[k]);
            rv[vi].x = _world_vec_v.x + dx->x + dy->x;
            rv[vi].y = _world_vec_v.y + dx->y + dy->y;
            rv[vi].z = _world_vec_v.z + dx->z + dy->z;
          }
          if (_test_poly_visible(rv, nv)) {
            ++ref_oc;
            ref_oh = (uint16_t)((ref_oh * 31) ^ oi ^ (uint16_t)rv[0].x ^
                                (uint16_t)rv[0].y);
          }
        } else if (mt != 0) {
          uint8_t ay = (uint8_t)(gy < 0 ? -gy : gy);
          uint8_t d = ax > ay ? ax : ay;
          _ref_world_draw_box_points((uint8_t)((cx << 1) + cy), npts[d],
                                     KWorldDotColors[mt]);
        }
        if (gy == (int8_t)ref_r) {
          break;
        }
        ++gy;
        cy = (uint8_t)(cy + ref_sy);
        _world_vec_v.x += ref_dy.x;
        _world_vec_v.y += ref_dy.y;
        _world_vec_v.z += ref_dy.z;
        if ((int16_t)(_world_vec_v.x - bailout) < 0) {
          break;
        }
      }
      if (gx == (int8_t)ref_r) {
        break;
      }
      ++gx;
      cx = (uint8_t)(cx + ref_sx);
      ps.x += ref_dx.x;
      ps.y += ref_dx.y;
      ps.z += ref_dx.z;
      if ((int16_t)(ps.x - bailout) < 0) {
        break;
      }
    }
    _check_screens(22);
    expect(22, got_oc, ref_oc);
    expect(22, (int16_t)got_oh, (int16_t)ref_oh);
  }
}
// 23. poly_asm.cc _poly_project_vertices against 16-bit C reference.
static inline uint8_t _ref_mag_hi(int16_t v) {
  uint8_t hi = (uint8_t)((uint16_t)v >> 8);
  return (hi & 0x80) ? (uint8_t)(hi ^ 0xff) : hi;
}

static uint8_t _ref_clip_2d(const int16_t *in_x, const int16_t *in_y,
                            uint8_t num_in, int16_t *out_x, int16_t *out_y,
                            uint8_t e) {
  uint8_t num_out = 0;
  int16_t lim = (e == 1) ? 79 : (e == 3 ? 27 : 0);
  bool keep_gt = (e & 1) == 0;
  bool axis_y = (e >= 2);
  int16_t prev_x = in_x[num_in - 1];
  int16_t prev_y = in_y[num_in - 1];
  int16_t pa = axis_y ? prev_y : prev_x;
  bool prev_in = keep_gt ? (pa >= lim) : (pa <= lim);

  for (uint8_t i = 0; i < num_in; ++i) {
    int16_t curr_x = in_x[i];
    int16_t curr_y = in_y[i];
    int16_t ca = axis_y ? curr_y : curr_x;
    bool curr_in = keep_gt ? (ca >= lim) : (ca <= lim);
    if (curr_in != prev_in) {
      int16_t po = axis_y ? prev_x : prev_y;
      int16_t co = axis_y ? curr_x : curr_y;
      int16_t d = (int16_t)(co - po);
      int16_t a = (int16_t)(lim - pa);
      int16_t den = (int16_t)(ca - pa);
      int16_t inter;
      if (d >= -255 && d <= 255) {
        inter = (int16_t)(po + vec_fastmul8p8(vec_div8p8(a, den), d));
      } else {
        inter = (int16_t)(po + vec_mulfrac(vec_frac16(a, den), d));
      }
      if (axis_y) {
        out_x[num_out] = inter;
        out_y[num_out] = lim;
      } else {
        out_x[num_out] = lim;
        out_y[num_out] = inter;
      }
      ++num_out;
    }
    if (curr_in) {
      out_x[num_out] = curr_x;
      out_y[num_out] = curr_y;
      ++num_out;
    }
    prev_x = curr_x;
    prev_y = curr_y;
    pa = ca;
    prev_in = curr_in;
  }
  return num_out;
}

static uint8_t _ref_project_vertices(const vec3_t *in, uint8_t num_in,
                                     int8_t *out2d) {
  static vec3_t c3[10];
  static int16_t bx0[12], by0[12], bx1[12], by1[12];
  if (num_in == 0) {
    return 0;
  }
  uint8_t n3 = 0;
  const vec3_t *prev = &in[num_in - 1];
  bool prev_in = prev->x >= 8;
  for (uint8_t i = 0; i < num_in; ++i) {
    const vec3_t *curr = &in[i];
    bool curr_in = curr->x >= 8;
    if (curr_in != prev_in) {
      int16_t dx = (int16_t)(curr->x - prev->x);
      int16_t a = (int16_t)(8 - prev->x);
      int16_t dy = (int16_t)(curr->y - prev->y);
      int16_t dz = (int16_t)(curr->z - prev->z);
      uint16_t t = vec_frac16(a, dx);
      int16_t x = 8, py = prev->y, pz = prev->z;
      if ((_ref_mag_hi(py) | _ref_mag_hi(pz) | _ref_mag_hi(curr->y) |
           _ref_mag_hi(curr->z) | _ref_mag_hi(dy) | _ref_mag_hi(dz)) < 15) {
        x = 64;
        py <<= 3;
        pz <<= 3;
        dy <<= 3;
        dz <<= 3;
      }
      c3[n3].x = x;
      c3[n3].y = (int16_t)(py + vec_mulfrac(t, dy));
      c3[n3].z = (int16_t)(pz + vec_mulfrac(t, dz));
      ++n3;
    }
    if (curr_in) {
      c3[n3++] = *curr;
    }
    prev = curr;
    prev_in = curr_in;
  }
  if (n3 < 3) {
    return 0;
  }
  uint8_t all_out = 0x0f;
  uint8_t edges = 0;
  for (uint8_t i = 0; i < n3; ++i) {
    vec_v = c3[i];
    vec_project_nocull();
    int16_t px = (int16_t)(80 - (vec_sx >> 1)) >> 1;
    int16_t py = (int16_t)(28 - (vec_sy >> 1)) >> 1;
    bx0[i] = px;
    by0[i] = py;
    uint8_t out = 0;
    if ((uint16_t)px >= 80) {
      out = (px < 0) ? 1 : 2;
    }
    if ((uint16_t)py >= 28) {
      out |= (py < 0) ? 4 : 8;
    }
    all_out &= out;
    edges |= out;
  }
  if (all_out) {
    return 0;
  }
  uint8_t n = n3;
  bool use1 = false;
  for (uint8_t e = 0; edges; ++e, edges >>= 1) {
    if (edges & 1) {
      if (!use1) {
        n = _ref_clip_2d(bx0, by0, n, bx1, by1, e);
      } else {
        n = _ref_clip_2d(bx1, by1, n, bx0, by0, e);
      }
      if (n < 3) {
        return 0;
      }
      use1 = !use1;
    }
  }
  const int16_t *fx = use1 ? bx1 : bx0;
  const int16_t *fy = use1 ? by1 : by0;
  for (uint8_t i = 0; i < n; ++i) {
    out2d[2 * i] = (int8_t)fx[i];
    out2d[2 * i + 1] = (int8_t)fy[i];
  }
  return n;
}

static void test_poly_project_vertices_asm(void) {
  _cur_id = 23;
  static const int16_t kPolys[12][4][3] = {
      // 0: All behind near plane (x < 8)
      {{-20, 0, 0}, {7, 10, 5}, {-5, -10, -5}, {0, 0, 0}},
      // 1: All inside viewport (no near clip, no 2D clip)
      {{400, 60, 20}, {400, -60, 20}, {500, -50, -30}, {500, 50, -30}},
      // 2: All outside frustum in 0.75 < |y|/x < 1.0 band
      {{200, 170, 0}, {300, 250, 10}, {250, 210, -10}, {220, 185, 5}},
      // 3: Mixed near-clip (lifted << 3, dy != 0, dz == 0) + multi-edge 2D clip
      {{-120, 40, -18}, {-120, -40, -18}, {600, -50, -18}, {600, 50, -18}},
      // 4: Mixed near-clip (unlifted, |y| > 3840, dy == 0, dz == 0) + long-span 2D clip
      {{-200, 4500, -100}, {-200, -4500, -100}, {300, -4500, -100}, {300, 4500, -100}},
      // 5: 2D clip across all 4 edges (huge quad covering full viewport, arg_d == 0)
      {{100, 150, 60}, {100, -150, 60}, {100, -150, -60}, {100, 150, -60}},
      // 6: 2D clip right & bottom edges only
      {{200, -160, -10}, {200, -40, -10}, {200, -40, -70}, {200, -160, -70}},
      // 7: 2D clip left & top edges only
      {{200, 160, 70}, {200, 40, 70}, {200, 40, 10}, {200, 160, 10}},
      // 8: Corner straddler rejected after 2D clip (< 3 vertices)
      {{100, 80, 30}, {100, 120, 15}, {100, 95, 45}, {100, 110, 35}},
      // 9: Mixed near-clip (lifted << 3, dy == 0 && dz == 0: parked runway edges)
      {{-100, 32, -16}, {-100, -32, -16}, {500, -32, -16}, {500, 32, -16}},
      // 10: Mixed near-clip (lifted << 3, dy == 0 && dz != 0)
      {{-80, 20, -40}, {-80, -20, -40}, {320, -20, 20}, {320, 20, 20}},
      // 11: Mixed near-clip (lifted << 3, dy != 0 && dz != 0: pitched/rolled)
      {{-90, 60, -35}, {-90, -10, -5}, {400, -80, 30}, {400, 30, -25}},
  };
  static vec3_t verts[4];
  static int8_t got2d[2 * kPolyMax2dVertices];
  static int8_t want2d[2 * kPolyMax2dVertices];
  for (uint8_t c = 0; c < 12; ++c) {
    uint8_t nv = (c == 0 || c == 8) ? 3 : 4;
    for (uint8_t i = 0; i < nv; ++i) {
      verts[i] = make_vector(kPolys[c][i][0], kPolys[c][i][1], kPolys[c][i][2]);
    }
    uint8_t gn = _poly_project_vertices(verts, nv, got2d);
    uint8_t wn = _ref_project_vertices(verts, nv, want2d);
    expect_id(gn, wn);
    if (gn == wn) {
      for (uint8_t i = 0; i < (uint8_t)(wn * 2); ++i) {
        expect_id((uint8_t)got2d[i], (uint8_t)want2d[i]);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 24. box_asm.cc _box_prepare_fill & _box_draw_asm against 16-bit C ref.
// ---------------------------------------------------------------------------
boxdef_t boxdef;
const uint8_t *boxdef_char_idx;
uint8_t mem_box_char_start;
const uint8_t *_cur_box_chars;

// Synthetic 3-page chardef stream (600 bytes) and offset table (224 entries)
// in main data so page 0 (<91), page 1 (91..163), and page 2 (>=164) are all
// exercised with distinct bytes without crowding opp_bss.
uint8_t chardef_bytes[600] = {1};
uint8_t chardef_offset_lo[224] = {1};

static void _ref_box_prepare_fill(uint8_t *ref_char_ram, uint8_t *ref_chars,
                                  uint8_t *ref_colors) {
  uint8_t *dst_ram = ref_char_ram + ((uint16_t)mem_box_char_start << 3);
  const uint8_t *src_idx = boxdef_char_idx;
  const uint8_t flip_start =
      boxdef.grad1_color_start - (boxdef.flip_around & 0x0F);
  const uint8_t flip_end = boxdef.grad1_color_start + (boxdef.flip_around >> 4);
  for (uint8_t i = 0; i < boxdef.char_count; ++i) {
    const uint8_t c = src_idx[i];
    const uint8_t *src = chardef_bytes + chardef_offset_lo[c];
    if (c >= kCharDefPage1) {
      src += 256;
      if (c >= kCharDefPage2) {
        src += 256;
      }
    }
    for (uint8_t row = 0; row < 8; ++row) {
      dst_ram[row] = (i >= flip_start && i < flip_end) ? src[7 - row] : src[row];
    }
    dst_ram += 8;
  }
  uint8_t cl[35], kl[35];
  cl[0] = kCharSolidGround;
  kl[0] = COLOR_GRAD1_8;
  cl[1] = kCharSolidSky;
  kl[1] = COLOR_SKY8;
  cl[2] = kCharSolid11;
  kl[2] = COLOR_GRAD1_8;
  for (uint8_t i = 0; i < boxdef.char_count; ++i) {
    cl[3 + i] = (uint8_t)(mem_box_char_start + i);
    kl[3 + i] = (i >= boxdef.grad1_color_start) ? COLOR_GRAD1_8 : COLOR_SKY8;
  }
  const uint8_t *src = boxdef_char_idx + boxdef.char_count;
  uint8_t total = (uint8_t)(boxdef.w * boxdef.h);
  for (uint8_t i = 0; i < total; ++i) {
    uint8_t idx = src[i];
    ref_chars[i] = cl[idx];
    ref_colors[i] = kl[idx];
  }
}

static uint8_t _ref_box_sky_rows;

static void _ref_draw_one_box(int8_t cx, int8_t cy) {
  const uint8_t *src_chr = _cur_box_chars;
  const uint8_t *src_col = _cur_box_chars + 0x100;
  int8_t h = (int8_t)boxdef.h;
  if (cy < 0) {
    h = (int8_t)(h + cy);
    if (h <= 0) {
      return;
    }
    int16_t skip = (int16_t)cy * (int16_t)boxdef.w;
    src_chr -= skip;
    src_col -= skip;
    cy = 0;
  }
  int8_t ov = (int8_t)(cy + h - kViewportHeight);
  if (ov > 0) {
    h = (int8_t)(h - ov);
  }
  if (h <= 0) {
    return;
  }
  int8_t w = (int8_t)boxdef.w;
  if (cx < 0) {
    w = (int8_t)(w + cx);
    src_chr -= cx;
    src_col -= cx;
    cx = 0;
  }
  ov = (int8_t)(cx + w - 40);
  if (ov > 0) {
    w = (int8_t)(w - ov);
  }
  if (w <= 0) {
    return;
  }
  if ((uint8_t)cy < _ref_box_sky_rows) {
    _ref_box_sky_rows = (uint8_t)cy;
  }
  uint8_t *dst_chr = ref_scr + (uint16_t)cy * 40 + (uint8_t)cx;
  uint8_t *dst_col = ref_clr + (uint16_t)cy * 40 + (uint8_t)cx;
  for (int8_t y = 0; y < h; ++y) {
    for (int8_t x = 0; x < w; ++x) {
      dst_chr[x] = src_chr[x];
      dst_col[x] = src_col[x];
    }
    src_chr += boxdef.w;
    src_col += boxdef.w;
    dst_chr += 40;
    dst_col += 40;
  }
}

static bool _ref_box_oob(int8_t cx, int8_t cy, int8_t sx, int8_t sy) {
  if (sx > 0) {
    if (cx >= 40) return true;
  } else {
    if ((int8_t)(cx + (int8_t)boxdef.w) <= 0) return true;
  }
  if (sy > 0) {
    if (cy >= kViewportHeight) return true;
  } else {
    if ((int8_t)(cy + (int8_t)boxdef.h) <= 0) return true;
  }
  return false;
}

static void _ref_box_draw(void) {
  const int8_t base_cx = (int8_t)(render_cx_chars + boxdef.rel_x);
  const int8_t base_cy = (int8_t)(render_cy_chars + boxdef.rel_y);
  int8_t sx = boxdef.step_x;
  int8_t sy = boxdef.step_y;
  int8_t cx = base_cx;
  int8_t cy = base_cy;
  for (uint8_t pass = 2;;) {
    do {
      _ref_draw_one_box(cx, cy);
      cx = (int8_t)(cx + sx);
      cy = (int8_t)(cy + sy);
    } while (!_ref_box_oob(cx, cy, sx, sy));
    if (--pass == 0) {
      break;
    }
    sx = (int8_t)(-sx);
    sy = (int8_t)(-sy);
    cx = (int8_t)(base_cx + sx);
    cy = (int8_t)(base_cy + sy);
  }
}

static void test_box_asm(void) {
  _cur_id = 24;
  _init_row_ptrs();

  // Populate chardef_bytes & chardef_offset_lo in place (RAM under oscar64).
  uint8_t *cb = (uint8_t *)chardef_bytes;
  uint8_t *co = (uint8_t *)chardef_offset_lo;
  for (uint16_t i = 0; i < 600; ++i) {
    cb[i] = (uint8_t)((i * 37 + 13) ^ (i >> 2));
  }
  for (uint8_t c = 0; c < 224; ++c) {
    if (c < kCharDefPage1) {
      co[c] = (uint8_t)((c * 2) & 0xF0);
    } else if (c < kCharDefPage2) {
      co[c] = (uint8_t)(((c - kCharDefPage1) * 3) & 0xF0);
    } else {
      co[c] = (uint8_t)(((c - kCharDefPage2) * 1) & 0x38);
    }
  }

  // Test 24a: _box_prepare_fill on both slots, all 3 chardef pages, and flips.
  static const uint8_t kPrepMap0[] = {
      // 6 char indices across pages 0 (<91), 1 (91..163), 2 (>=164)
      5, 90, 91, 120, 164, 210,
      // 4 x 5 uniform-row cells (like box_r8)
      3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 2, 2, 2, 2};
  static const uint8_t kPrepMap1[] = {
      // 8 char indices across all 3 pages
      0, 42, 91, 163, 164, 180, 200, 223,
      // 6 x 4 varied cells (tilted/vertical box)
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0,
      1, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

  uint8_t *const test_cram = (uint8_t *)((uint16_t)BOX_CHAR_RAM_HI << 8);
  uint8_t *const ref_cram = (uint8_t *)0xEC00;
  uint8_t *const ref_ch = (uint8_t *)0xFE00;
  uint8_t *const ref_cl = (uint8_t *)0xFE60;

  for (uint8_t slot = 0; slot < 2; ++slot) {
    mem_box_char_start = slot ? 0x61 : 0x01;
    _cur_box_chars = (const uint8_t *)(BOX_SCRATCH_BASE + (slot ? 0xA0 : 0));
    if (slot == 0) {
      boxdef.w = 4;
      boxdef.h = 5;
      boxdef.grad1_color_start = 3;
      boxdef.char_count = 6;
      boxdef.flip_around = 0x21; // flip indices 2..4 (inclusive)
      boxdef_char_idx = kPrepMap0;
    } else {
      boxdef.w = 6;
      boxdef.h = 4;
      boxdef.grad1_color_start = 4;
      boxdef.char_count = 8;
      boxdef.flip_around = 0x32; // flip indices 2..6 (inclusive)
      boxdef_char_idx = kPrepMap1;
    }
    uint16_t off = (uint16_t)mem_box_char_start << 3;
    uint16_t nbytes = (uint16_t)boxdef.char_count << 3;
    for (uint16_t i = 0; i < nbytes; ++i) {
      test_cram[off + i] = 0xAA;
      ref_cram[off + i] = 0xAA;
    }
    _box_prepare_fill();
    _ref_box_prepare_fill(ref_cram, ref_ch, ref_cl);
    for (uint16_t i = 0; i < nbytes; ++i) {
      expect_id(test_cram[off + i], ref_cram[off + i]);
    }
    uint8_t total = (uint8_t)(boxdef.w * boxdef.h);
    for (uint8_t i = 0; i < total; ++i) {
      expect_id(_cur_box_chars[i], ref_ch[i]);
      expect_id((_cur_box_chars + 0x100)[i], ref_cl[i]);
    }
  }

  // Test 24b: _box_draw_asm Path A (step_y == 0, horizontal fast path).
  mem_box_char_start = 0x01;
  _cur_box_chars = (const uint8_t *)BOX_SCRATCH_BASE;
  boxdef.w = 4;
  boxdef.h = 5;
  boxdef.step_x = 4;
  boxdef.step_y = 0;
  boxdef.rel_x = 0;
  boxdef.rel_y = -5;
  boxdef.grad1_color_start = 2;
  boxdef.char_count = 6;
  boxdef.flip_around = 0x00;
  boxdef_char_idx = kPrepMap0;
  _box_prepare_fill();

  static const int8_t kHorizCy[] = {8, 3, 16, 0, -3, 19, -120, 60};
  static const uint8_t kHorizSky[] = {10, 4, 14, 6, 8, 5, 7, 9};
  for (uint8_t c = 0; c < sizeof(kHorizCy); ++c) {
    _clear_test_screens(0x55, 0x0E);
    render_cx_chars = 20;
    render_cy_chars = kHorizCy[c];
    render_sky_rows = kHorizSky[c];
    _ref_box_sky_rows = kHorizSky[c];
    _box_draw_asm();
    _ref_box_draw();
    _cur_id = 24;
    expect_id(*(volatile uint8_t *)&render_sky_rows, _ref_box_sky_rows);
    _check_screens(24);
  }

  // Test 24c: _box_draw_asm Path B (step_y != 0, tilted & vertical tiles).
  static const struct {
    uint8_t w, h;
    int8_t sx, sy, rx, ry;
    int8_t cx, cy;
    uint8_t sky;
  } kTiltCases[] = {
      {8, 5, 8, 2, -4, -2, 20, 7, 10},   // sx > 0, sy > 0
      {8, 5, -8, 2, -4, -2, 18, 6, 8},   // sx < 0, sy > 0
      {8, 5, 8, -2, -4, -2, 20, 8, 12},  // sx > 0, sy < 0
      {8, 5, -8, -2, -4, -2, 16, 7, 9},  // sx < 0, sy < 0
      {5, 4, 0, 4, 0, 0, 19, 5, 6},      // vertical (sx == 0, sy > 0)
      {5, 4, 0, -4, -5, -3, 3, 13, 14},  // vertical clipped left/bottom
      {11, 6, -5, 6, -5, 0, 35, -3, 5},  // clipped right/top
  };
  mem_box_char_start = 0x61;
  _cur_box_chars = (const uint8_t *)(BOX_SCRATCH_BASE + 0xA0);
  uint8_t *slot_chr = (uint8_t *)_cur_box_chars;
  uint8_t *slot_col = slot_chr + 0x100;
  for (uint8_t i = 0; i < 66; ++i) {
    slot_chr[i] = (uint8_t)(0x61 + (i & 31));
    slot_col[i] = (i & 1) ? COLOR_GRAD1_8 : COLOR_SKY8;
  }
  for (uint8_t c = 0; c < sizeof(kTiltCases) / sizeof(kTiltCases[0]); ++c) {
    boxdef.w = kTiltCases[c].w;
    boxdef.h = kTiltCases[c].h;
    boxdef.step_x = kTiltCases[c].sx;
    boxdef.step_y = kTiltCases[c].sy;
    boxdef.rel_x = kTiltCases[c].rx;
    boxdef.rel_y = kTiltCases[c].ry;
    _clear_test_screens(0x33, 0x0E);
    render_cx_chars = kTiltCases[c].cx;
    render_cy_chars = kTiltCases[c].cy;
    render_sky_rows = kTiltCases[c].sky;
    _ref_box_sky_rows = kTiltCases[c].sky;
    _box_draw_asm();
    _ref_box_draw();
    _cur_id = 24;
    expect_id(*(volatile uint8_t *)&render_sky_rows, _ref_box_sky_rows);
    _check_screens(24);
  }
}

// 25. vec_asm.cc vector operations: vec_fastsqr8p8, vec_dot (preserving
// frac_d0/d1/q1 and vec_v), vec_transform_inv (aliased & unaliased),
// vec_turn_xy, vec_cross, vec_rotate_step, vec_normalize, vec_orthonormalize.
static __noinline void _ref_vec_limit(vec3_t *v) {
  int16_t *c = &v->x;
  for (uint8_t i = 0; i < 3; ++i) {
    if (c[i] <= -255) {
      c[i] = -256;
    } else if (c[i] >= 255) {
      c[i] = 256;
    }
  }
}

static __noinline void _ref_vec_normalize(vec3_t *v) {
  uint16_t L2 = (uint16_t)((uint16_t)vec_fastmul8p8(v->x, v->x) +
                           (uint16_t)vec_fastmul8p8(v->y, v->y) +
                           (uint16_t)vec_fastmul8p8(v->z, v->z));
  if (L2 == 0) {
    return;
  }
  L2 <<= 6;
  uint16_t res = 0;
  uint16_t bit = (uint16_t)1 << 14;
  while (bit > L2) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (L2 >= res + bit) {
      L2 -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  if (res == 0) {
    return;
  }
  res <<= 1;
  v->x = vec_div8p8(v->x, (int16_t)res);
  v->y = vec_div8p8(v->y, (int16_t)res);
  v->z = vec_div8p8(v->z, (int16_t)res);
  _ref_vec_limit(v);
}

static __noinline void test_vec_ops_asm(void) {
  _cur_id = 25;

  // 25a. vec_fastmul8p8 fast paths (0, +-256, +-512, -32768) & vec_fastsqr8p8.
  static const int16_t kSqrVals[] = {0,    1,     -1,    15,    -15,   200,
                                     -200, 255,   -255,  256,   -256,  512,
                                     -512, 1760,  -1760, 4095,  -4095, 32767,
                                     -32767, -32768};
  for (uint8_t i = 0; i < sizeof(kSqrVals) / sizeof(kSqrVals[0]); ++i) {
    int16_t a = kSqrVals[i];
    expect_id((int16_t)vec_fastsqr8p8(a), vec_fastmul8p8(a, a));
    expect_id(vec_fastmul8p8(0, a), 0);
    expect_id(vec_fastmul8p8(a, 0), 0);
    expect_id(vec_fastmul8p8(256, a), a);
    expect_id(vec_fastmul8p8(a, 256), a);
    expect_id(vec_fastmul8p8(-256, a), (int16_t)(-a));
    expect_id(vec_fastmul8p8(a, -256), (int16_t)(-a));
  }

  // 25b. vec_dot + preservation of frac_d0/d1/q1 and vec_v.
  static const mat3_t kMat = {
      {196, 128, -104}, {-152, 206, -18}, {76, 62, 236}};
  static const vec3_t kVecs[] = {
      {256, 0, 0}, {0, -256, 128}, {310, -420, 185}, {-1200, 950, -340}};
  for (uint8_t r = 0; r < 3; ++r) {
    const vec3_t *row = &kMat.front + r;
    for (uint8_t k = 0; k < sizeof(kVecs) / sizeof(kVecs[0]); ++k) {
      vec_v = kVecs[k];
      frac_d0 = 0x5A;
      frac_d1 = 0xA5;
      frac_q1 = 0x04;
      int16_t got = vec_dot(row, &vec_v);
      int16_t ref = (int16_t)(vec_fastmul8p8(row->x, kVecs[k].x) +
                              vec_fastmul8p8(row->y, kVecs[k].y) +
                              vec_fastmul8p8(row->z, kVecs[k].z));
      expect_id(got, ref);
      expect_id(frac_d0, 0x5A);
      expect_id(frac_d1, 0xA5);
      expect_id(frac_q1, 0x04);
      expect_id(vec_v.x, kVecs[k].x);
      expect_id(vec_v.y, kVecs[k].y);
      expect_id(vec_v.z, kVecs[k].z);
    }
  }

  // 25c. vec_transform_inv (separate and in-place res == u).
  for (uint8_t k = 0; k < sizeof(kVecs) / sizeof(kVecs[0]); ++k) {
    vec3_t out, alias = kVecs[k];
    vec_transform_inv(&kMat, &kVecs[k], &out);
    vec_transform_inv(&kMat, &alias, &alias);
    int16_t rx = vec_dot(&kMat.front, &kVecs[k]);
    expect_id(out.x, rx);
    expect_id(alias.x, rx);
    int16_t ry = vec_dot(&kMat.left, &kVecs[k]);
    expect_id(out.y, ry);
    expect_id(alias.y, ry);
    int16_t rz = vec_dot(&kMat.up, &kVecs[k]);
    expect_id(out.z, rz);
    expect_id(alias.z, rz);
  }

  // 25d. vec_turn_xy.
  static const int16_t kTurnS[] = {0, 8, -8, 32, -32, 256, -256};
  for (uint8_t si = 0; si < sizeof(kTurnS) / sizeof(kTurnS[0]); ++si) {
    vec3_t v = {181, -181, 42};
    int16_t rx = (int16_t)(v.x + vec_fastmul8p8(kTurnS[si], v.y));
    int16_t ry = (int16_t)(v.y - vec_fastmul8p8(kTurnS[si], v.x));
    vec_turn_xy(kTurnS[si], &v);
    expect_id(v.x, rx);
    expect_id(v.y, ry);
    expect_id(v.z, 42);
  }

  // 25e. vec_cross.
  vec3_t cr;
  vec_cross(&kMat.front, &kMat.left, &cr);
  expect_id(cr.x, (int16_t)(vec_fastmul8p8(kMat.front.y, kMat.left.z) -
                            vec_fastmul8p8(kMat.front.z, kMat.left.y)));
  expect_id(cr.y, (int16_t)(vec_fastmul8p8(kMat.front.z, kMat.left.x) -
                            vec_fastmul8p8(kMat.front.x, kMat.left.z)));
  expect_id(cr.z, (int16_t)(vec_fastmul8p8(kMat.front.x, kMat.left.y) -
                            vec_fastmul8p8(kMat.front.y, kMat.left.x)));

  // 25f. vec_rotate_step across all 6 steps and shifts 0, 1.
  for (uint8_t sh = 0; sh <= 1; ++sh) {
    vec_set_rotation_shift(sh);
    for (uint8_t st = 0; st < VEC_STEP_COUNT; ++st) {
      mat3_t got_m = kMat;
      mat3_t ref_m = kMat;
      int16_t *p = (st < VEC_STEP_PITCH_UP) ? &ref_m.left.x : &ref_m.front.x;
      int16_t *q = (st >= VEC_STEP_YAW_LEFT) ? &ref_m.left.x : &ref_m.up.x;
      int16_t s = _vec_step[st];
      for (uint8_t i = 0; i < 3; ++i) {
        int16_t a = p[i], b = q[i];
        p[i] = (int16_t)(a + vec_fastmul8p8(s, b));
        q[i] = (int16_t)(b - vec_fastmul8p8(s, a));
      }
      vec_rotate_step(st, &got_m);
      int16_t *gm = &got_m.front.x, *rm = &ref_m.front.x;
      for (uint8_t i = 0; i < 9; ++i) {
        expect_id(gm[i], rm[i]);
      }
    }
  }
  vec_set_rotation_shift(0);

  // 25g. vec_normalize & vec_orthonormalize.
  static const vec3_t kNormCases[] = {
      {0, 0, 0},       {256, 0, 0},     {-255, 0, 0},   {255, 1, -1},
      {180, 180, 0},   {196, 128, -104}, {-120, 90, 210}, {10, -20, 30}};
  for (uint8_t k = 0; k < sizeof(kNormCases) / sizeof(kNormCases[0]); ++k) {
    vec3_t gv = kNormCases[k], rv = kNormCases[k];
    vec_normalize(&gv);
    _ref_vec_normalize(&rv);
    expect_id(gv.x, rv.x);
    expect_id(gv.y, rv.y);
    expect_id(gv.z, rv.z);
  }
  mat3_t go = kMat, ro = kMat;
  vec_orthonormalize(&go);
  _ref_vec_normalize(&ro.front);
  vec_cross(&ro.up, &ro.front, &ro.left);
  _ref_vec_normalize(&ro.left);
  vec_cross(&ro.front, &ro.left, &ro.up);
  _ref_vec_limit(&ro.up);
  int16_t *gop = &go.front.x, *rop = &ro.front.x;
  for (uint8_t i = 0; i < 9; ++i) {
    expect_id(gop[i], rop[i]);
  }
}

// 26. opponent_asm.cc _opponent_move, _opponent_ahead, _opponent_axes &
// _opponent_cull_slot against 16-bit C references on the emulated 6510.
static __noinline void _ref_opp_axes(uint8_t slot, mat3_t *out_m) {
  static const uint8_t kPCos[8] = {229, 248, 254, 0, 254, 248, 229, 205};
  static const uint8_t kBCos[5] = {128, 222, 0, 222, 128};
  static const int16_t kBSin[5] = {222, 128, 0, -128, -222};
  int16_t hx = _opponent_head[slot].x, hy = _opponent_head[slot].y;
  uint8_t turn = (uint8_t)(_opponent_bank[slot] + 2);
  uint8_t pitch = (uint8_t)(_opponent_pitch[slot] + 3);
  const vec3_t *row = &world_cam.left;
  int16_t *out = &out_m->front.y;
  for (uint8_t c = 0; c < 2; ++c, ++row, ++out) {
    int16_t ra = (int16_t)(vec_fastmul8p8(row->x, hx) +
                           vec_fastmul8p8(row->y, hy));
    int16_t rb = row->z;
    if (pitch != 3) {
      int16_t s = kOpponentPitchSin[pitch];
      uint8_t cp = kPCos[pitch];
      volatile int16_t a = ra;
      out[0] = (int16_t)(vec_fastmul8p8(cp, a) + vec_fastmul8p8(s, rb));
      rb = (int16_t)(vec_fastmul8p8(cp, rb) + vec_fastmul8p8(-s, a));
    } else {
      out[0] = ra;
    }
    ra = (int16_t)(vec_fastmul8p8(row->x, -hy) + vec_fastmul8p8(row->y, hx));
    if (turn != 2) {
      int16_t s = kBSin[turn];
      uint8_t cb = kBCos[turn];
      volatile int16_t a = ra;
      out[3] = (int16_t)(vec_fastmul8p8(cb, a) + vec_fastmul8p8(s, rb));
      rb = (int16_t)(vec_fastmul8p8(cb, rb) + vec_fastmul8p8(-s, a));
    } else {
      out[3] = ra;
    }
    out[6] = rb;
  }
}

static __noinline uint8_t _ref_opp_cull_slot(uint8_t slot, vec3_t *out_cs) {
  uint8_t range = _opponent_world_delta(slot);
  if (range == kOpponentOut) {
    return 0;
  }
  int16_t cx = (int16_t)(vec_fastmul8p8(world_cam.front.x, vec_v.x) +
                         vec_fastmul8p8(world_cam.front.y, vec_v.y) +
                         vec_fastmul8p8(world_cam.front.z, vec_v.z));
  if (cx <= 64) {
    return 0;
  }
  int16_t cz = (int16_t)(vec_fastmul8p8(world_cam.up.x, vec_v.x) +
                         vec_fastmul8p8(world_cam.up.y, vec_v.y) +
                         vec_fastmul8p8(world_cam.up.z, vec_v.z));
  uint16_t half = (uint16_t)cx >> 1;
  uint16_t quar = half >> 1;
  uint16_t eighth = quar >> 1;
  uint16_t u32 = eighth >> 2;
  uint16_t px4 = u32 >> 1;
  uint16_t lim_z = (uint16_t)(quar - u32 + px4 + 101);
  if ((uint16_t)((uint16_t)cz + lim_z) > (uint16_t)(lim_z << 1)) {
    return 0;
  }
  uint16_t lim_y = (uint16_t)(half + eighth + px4 + 135);
  int16_t cy = (int16_t)(vec_fastmul8p8(world_cam.left.x, vec_v.x) +
                         vec_fastmul8p8(world_cam.left.y, vec_v.y) +
                         vec_fastmul8p8(world_cam.left.z, vec_v.z));
  if ((uint16_t)((uint16_t)cy + lim_y) > (uint16_t)(lim_y << 1)) {
    return 0;
  }
  out_cs->x = cx;
  out_cs->y = cy;
  out_cs->z = cz;
  return (uint8_t)(range + 1);
}

static __noinline void test_opponent_hot_asm(void) {
  _cur_id = 26;

  // 26a. _opponent_move & _opponent_ahead across step shifts and edge headings.
  static const int16_t kMoveH[] = {0, 256, -256, 181, -181, -192, 64, -32};
  for (uint8_t sh = 0; sh <= 2; ++sh) {
    flight_step_shift = sh;
    _opponent_speed[0] = 1760;
    _opponent_speed[1] = 2800;
    for (uint8_t slot = 0; slot < 2; ++slot) {
      for (uint8_t k = 0; k < sizeof(kMoveH) / sizeof(kMoveH[0]); ++k) {
        uint8_t idx = (uint8_t)(slot + (k & 2) * 2);
        uint16_t base_lo = (k & 1) ? 0xFF80U : 0x0040U;
        uint16_t base_hi = (k & 1) ? 0x0012U : 0x0034U;
        uint8_t *bp = (uint8_t *)&_opponent_xyz[idx];
        bp[0] = (uint8_t)base_lo;
        bp[1] = (uint8_t)(base_lo >> 8);
        bp[2] = (uint8_t)base_hi;
        bp[3] = (uint8_t)(base_hi >> 8);
        int16_t step =
            vec_fastmul8p8(kMoveH[k], (int16_t)(_opponent_speed[slot] << 1));
        for (uint8_t s = 0; s < sh; ++s) {
          step >>= 1;
        }
        uint16_t want_lo = (uint16_t)(base_lo + (uint16_t)step);
        uint16_t carry = (want_lo < base_lo) ? 1 : 0;
        uint16_t want_hi =
            (uint16_t)(base_hi + (step < 0 ? 0xFFFFU : 0U) + carry);
        _opponent_move(slot, idx, kMoveH[k]);
        uint16_t got_lo = (uint16_t)(bp[0] | ((uint16_t)bp[1] << 8));
        uint16_t got_hi = (uint16_t)(bp[2] | ((uint16_t)bp[3] << 8));
        expect_id((int16_t)got_lo, (int16_t)want_lo);
        expect_id((int16_t)got_hi, (int16_t)want_hi);
      }
      _opponent_head[slot].x = slot ? -181 : 256;
      _opponent_head[slot].y = slot ? 181 : 0;
      _opponent_xyz[slot] = 0x0020FF00L;
      _opponent_xyz[slot + 2] = 0x00100020L;
      int16_t sx = vec_fastmul8p8(_opponent_head[slot].x,
                                  (int16_t)(_opponent_speed[slot] << 1)) >>
                   sh;
      int16_t sy = vec_fastmul8p8(_opponent_head[slot].y,
                                  (int16_t)(_opponent_speed[slot] << 1)) >>
                   sh;
      int32_t wx = _opponent_xyz[slot] + sx;
      int32_t wy = _opponent_xyz[slot + 2] + sy;
      _opponent_ahead(slot);
      int32_t gx = *(volatile int32_t *)&_opponent_xyz[slot];
      int32_t gy = *(volatile int32_t *)&_opponent_xyz[slot + 2];
      expect_id((int16_t)(gx & 0xFFFF), (int16_t)(wx & 0xFFFF));
      expect_id((int16_t)(gy & 0xFFFF), (int16_t)(wy & 0xFFFF));
    }
  }
  flight_step_shift = 0;

  // 26b. _opponent_axes across level & tilted cameras, all banks/pitches.
  static const mat3_t kAxCams[2] = {
      {{256, 0, 0}, {0, 256, 0}, {0, 0, 256}},
      {{196, 128, -104}, {-152, 206, -18}, {76, 62, 236}},
  };
  static const struct {
    int16_t hx, hy;
    int8_t bank, pitch;
  } kAxCases[] = {
      {256, 0, 0, 0},
      {0, -256, 1, 2},
      {-181, 181, -2, -3},
      {128, -222, 2, 4},
      {-240, -88, -1, -1},
  };
  for (uint8_t ci = 0; ci < 2; ++ci) {
    world_cam = kAxCams[ci];
    for (uint8_t slot = 0; slot < 2; ++slot) {
      for (uint8_t k = 0; k < sizeof(kAxCases) / sizeof(kAxCases[0]); ++k) {
        _opponent_head[slot].x = kAxCases[k].hx;
        _opponent_head[slot].y = kAxCases[k].hy;
        _opponent_bank[slot] = kAxCases[k].bank;
        _opponent_pitch[slot] = kAxCases[k].pitch;
        mat3_t ref_m;
        _ref_opp_axes(slot, &ref_m);
        _opponent_axes(slot);
        expect_id(_opponent_axes_m.front.y, ref_m.front.y);
        expect_id(_opponent_axes_m.front.z, ref_m.front.z);
        expect_id(_opponent_axes_m.left.y, ref_m.left.y);
        expect_id(_opponent_axes_m.left.z, ref_m.left.z);
        expect_id(_opponent_axes_m.up.y, ref_m.up.y);
        expect_id(_opponent_axes_m.up.z, ref_m.up.z);
      }
    }
  }

  // 26c. _opponent_cull_slot across level/tilted cameras, near/far/out/frustum.
  flight_eye_x = 0x00200000L;
  flight_eye_y = 0x00400000L;
  flight_eye_z = 0x00020000L;
  static const struct {
    int32_t dx, dy, dz;
  } kCullCases[] = {
      {0x00040000L, 0x00008000L, 0x00004000L},   // near, visible
      {0x00140000L, -0x00030000L, 0x00010000L},  // far, visible
      {0x00300000L, 0, 0},                       // out of range
      {-0x00020000L, 0, 0},                      // behind camera
      {0x00020000L, 0, 0x00080000L},             // above frustum (z cull)
      {0x00020000L, 0x000A0000L, 0},             // left of frustum (y cull)
  };
  for (uint8_t ci = 0; ci < 2; ++ci) {
    world_cam = kAxCams[ci];
    for (uint8_t slot = 0; slot < 2; ++slot) {
      for (uint8_t k = 0; k < sizeof(kCullCases) / sizeof(kCullCases[0]); ++k) {
        _opponent_xyz[slot] = flight_eye_x + kCullCases[k].dx;
        _opponent_xyz[slot + 2] = flight_eye_y + kCullCases[k].dy;
        _opponent_xyz[slot + 4] = flight_eye_z + kCullCases[k].dz;
        vec3_t ref_cs;
        uint8_t want = _ref_opp_cull_slot(slot, &ref_cs);
        uint8_t got = _opponent_cull_slot(slot);
        expect_id(got, want);
        if (want != 0) {
          expect_id(_opponent_cs[slot].x, ref_cs.x);
          expect_id(_opponent_cs[slot].y, ref_cs.y);
          expect_id(_opponent_cs[slot].z, ref_cs.z);
        }
      }
    }
  }
}

// 27. render_asm.cc _render_pull_and_snap_asm & _render_fill_sky_ground_asm.
static void _ref_pull_to_center(int16_t *out_px, int16_t *out_py) {
  int16_t c_maj, c_min, t;
  int8_t d_maj, d_min;
  if (roll_x_is_major) {
    c_maj = render_cx_pixels;
    c_min = render_cy_pixels;
    t = 160;
    d_maj = roll_dx;
    d_min = roll_dy;
  } else {
    c_maj = render_cy_pixels;
    c_min = render_cx_pixels;
    t = 64;
    d_maj = roll_dy;
    d_min = roll_dx;
  }
  t += (roll_shift_2chars ? 64 : 32) - c_maj;
  int16_t d = t >> 6;
  if (roll_shift_2chars) {
    d >>= 1;
  }
  int16_t p_maj = c_maj + (t & (roll_shift_2chars ? -128 : -64));
  if (d_maj < 0) {
    d = -d;
  }
  int16_t prod = vec_fastmul8p8(d, (int16_t)d_min << 8);
  int16_t p_min = c_min + (int16_t)(prod << 3);
  if (roll_x_is_major) {
    *out_px = p_maj;
    *out_py = p_min;
  } else {
    *out_px = p_min;
    *out_py = p_maj;
  }
}

static uint8_t _ref_clamp_pos(int8_t row) {
  return row < 0 ? 0 : (uint8_t)row;
}

static uint8_t _ref_fill_sky_ground(uint8_t init_sky_rows) {
  uint8_t cur_sky = 0;
  for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
    ref_scr[i] = 0xAA;
    ref_clr[i] = (i < (uint16_t)init_sky_rows * 40) ? COLOR_SKY8 : 0x55;
  }
  if (roll_dy == 0) {
    bool sky_left = roll_dx > 0;
    int8_t e = render_cy_chars;
    uint8_t top = _ref_clamp_pos(e);
    uint8_t bot;
    if (sky_left) {
      bot = top;
      top = _ref_clamp_pos((int8_t)(bot - 4));
    } else {
      bot = _ref_clamp_pos((int8_t)(e + 4));
      if (bot < top) {
        bot = top;
      }
    }
    uint8_t ch0 = sky_left ? kCharSolid11 : kCharSolidGround;
    uint8_t ch1 = (uint8_t)(ch0 ^ kCharSolidGround);
    uint8_t t_end = top > kViewportHeight ? kViewportHeight : top;
    if (ch0 == kCharSolid11 && t_end > 0) {
      cur_sky = t_end;
    }
    for (uint8_t y = 0; y < t_end; ++y) {
      for (uint8_t x = 0; x < 40; ++x) {
        ref_scr[(uint16_t)y * 40 + x] = ch0;
        if (ch0 == kCharSolid11 && y >= init_sky_rows) {
          ref_clr[(uint16_t)y * 40 + x] = COLOR_SKY8;
        }
      }
    }
    if (ch1 == kCharSolid11 && bot < kViewportHeight && bot == cur_sky) {
      cur_sky = kViewportHeight;
    }
    for (uint8_t y = bot; y < kViewportHeight; ++y) {
      for (uint8_t x = 0; x < 40; ++x) {
        ref_scr[(uint16_t)y * 40 + x] = ch1;
        if (ch1 == kCharSolid11 && y >= init_sky_rows) {
          ref_clr[(uint16_t)y * 40 + x] = COLOR_SKY8;
        }
      }
    }
  } else {
    bool sky_left = roll_dy < 0;
    int16_t lo = -vec_fastmul8p8(roll_dx_div_dy, (int16_t)render_cy_chars << 8) +
                 (int16_t)((int16_t)render_cx_chars << 4);
    if (roll_dx_div_dy > 0) {
      lo += 8;
    }
    if (render_alt_box) {
      if (render_alt_shift_x) {
        lo += 8;
      }
      if (render_alt_shift_y) {
        lo -= (roll_dx_div_dy >> 1);
      }
    }
    int16_t ad = roll_dx_div_dy < 0 ? -roll_dx_div_dy : roll_dx_div_dy;
    int16_t w = (int16_t)(ad << 2);
    if (sky_left) {
      lo -= w;
    }
    for (uint8_t y = 0; y < kViewportHeight; ++y) {
      int16_t hi = lo + w;
      if (hi < 0x10 || lo >= (int16_t)(40 << 4)) {
        uint8_t ch =
            ((hi < 0x10) != sky_left) ? kCharSolid11 : kCharSolidGround;
        if (ch == kCharSolid11 && y == cur_sky) {
          cur_sky = (uint8_t)(y + 1);
        }
        for (uint8_t x = 0; x < 40; ++x) {
          ref_scr[(uint16_t)y * 40 + x] = ch;
          if (ch == kCharSolid11 && y >= init_sky_rows) {
            ref_clr[(uint16_t)y * 40 + x] = COLOR_SKY8;
          }
        }
      } else {
        if (lo >= 0x10) {
          uint8_t lim = (uint8_t)((uint16_t)lo >> 4);
          uint8_t ch = sky_left ? kCharSolid11 : kCharSolidGround;
          for (uint8_t x = 0; x < lim; ++x) {
            ref_scr[(uint16_t)y * 40 + x] = ch;
            if (sky_left && y >= init_sky_rows) {
              ref_clr[(uint16_t)y * 40 + x] = COLOR_SKY8;
            }
          }
        }
        if (hi < (int16_t)(40 << 4)) {
          uint8_t r = (uint8_t)((uint16_t)hi >> 4);
          uint8_t ch = !sky_left ? kCharSolid11 : kCharSolidGround;
          for (uint8_t x = r; x < 40; ++x) {
            ref_scr[(uint16_t)y * 40 + x] = ch;
            if (!sky_left && y >= init_sky_rows) {
              ref_clr[(uint16_t)y * 40 + x] = COLOR_SKY8;
            }
          }
        }
      }
      lo += roll_dx_div_dy;
    }
  }
  return cur_sky;
}

static void test_render_pull_and_fill_asm(void) {
  _cur_id = 27;
  // Part A: _render_pull_and_snap_asm (level, tilted, x/y major, shift_2chars,
  // negative cx/cy & negative d_maj).
  struct PullCase {
    bool x_maj, shift2;
    uint8_t period;
    int8_t dx, dy;
    int16_t cx, cy;
  };
  static const PullCase kPullCases[] = {
      {true, false, 1, 8, 0, 160, 56},
      {true, false, 1, -8, 0, -200, 120},
      {true, true, 8, 16, -1, 500, -140},
      {true, false, 4, 8, -2, -310, 420},
      {false, false, 1, 0, -8, 90, 310},
      {false, true, 8, -2, -16, -420, -290},
      {false, false, 2, 4, 8, 350, -180},
  };
  for (uint8_t c = 0; c < sizeof(kPullCases) / sizeof(kPullCases[0]); ++c) {
    roll_x_is_major = kPullCases[c].x_maj;
    roll_shift_2chars = kPullCases[c].shift2;
    roll_period = kPullCases[c].period;
    roll_dx = kPullCases[c].dx;
    roll_dy = kPullCases[c].dy;
    render_cx_pixels = kPullCases[c].cx;
    render_cy_pixels = kPullCases[c].cy;
    for (uint8_t e = 0; e < 8; ++e) {
      int8_t off = (int8_t)e - 4;
      roll_ex_dy[e] = (int8_t)(roll_dy * off);
      roll_ey_dx[e] = (int8_t)(roll_dx * off);
    }
    *(volatile int8_t *)&render_alt_shift_x = 0;
    *(volatile int8_t *)&render_alt_shift_y = 0;
    _render_pull_and_snap_asm();

    int16_t r_px = 0, r_py = 0;
    _ref_pull_to_center(&r_px, &r_py);
    int8_t r_cx = 0, r_cy = 0, r_sx = 0, r_sy = 0;
    bool r_alt = false;
    _ref_snap_center(r_px, r_py, &r_cx, &r_cy, &r_alt, &r_sx, &r_sy);
    expect_id(*(volatile int8_t *)&render_cx_chars, r_cx);
    expect_id(*(volatile int8_t *)&render_cy_chars, r_cy);
    expect_id(*(volatile bool *)&render_alt_box ? 1 : 0, r_alt ? 1 : 0);
    if (roll_period == 1) {
      expect_id(*(volatile int8_t *)&render_alt_shift_x, r_sx);
      expect_id(*(volatile int8_t *)&render_alt_shift_y, r_sy);
    }
  }

  // Part B: _render_fill_sky_ground_asm on $E800 (main) and $EC00 (alt).
  struct FillCase {
    bool alt_buf, alt_box;
    int8_t dx, dy, cx, cy, sx, sy;
    int16_t dx_div_dy;
    uint8_t init_sky;
  };
  static const FillCase kFillCases[] = {
      // Level horizons (sky-top, ground-top, clamped edges, int8 wraparound).
      {false, false, 8, 0, 20, 7, 0, 0, 0, 2},
      {true, false, 8, 0, 20, 2, 0, 0, 0, 0},
      {false, false, 8, 0, 20, -5, 0, 0, 0, 4},
      {true, false, -8, 0, 20, 7, 0, 0, 0, 3},
      {false, false, -8, 0, 20, -2, 0, 0, 0, 0},
      {true, false, -8, 0, 20, 126, 0, 0, 0, 0},
      // Tilted horizons: sky_left (dy < 0) and sky_right (dy > 0), +/- slope,
      // alt_box shifts (x and y), cy == -128 overflow edge case, sky_rows skip.
      {false, false, 8, -4, 20, 7, 0, 0, -32, 3},
      {true, false, -8, -4, 20, 7, 0, 0, 32, 2},
      {false, true, 8, -8, 18, 6, 0, 4, -16, 4},
      {true, true, -2, 8, 22, 8, 4, 0, -4, 1},
      {false, true, 6, 8, 19, 5, 0, 4, 12, 0},
      {true, false, 8, -2, 20, -128, 0, 0, -64, 0},
  };
  uint8_t saved_p01 = *(volatile uint8_t *)0x01;
  for (uint8_t c = 0; c < sizeof(kFillCases) / sizeof(kFillCases[0]); ++c) {
    mem_using_alt_buffer = kFillCases[c].alt_buf;
    uint8_t *scr = (uint8_t *)(mem_using_alt_buffer ? 0xEC00 : 0xE800);
    for (uint8_t r = 0; r < kScreenHeight; ++r) {
      mem_screen_row_ptrs[r] = scr + (uint16_t)r * 40;
    }
    roll_dx = kFillCases[c].dx;
    roll_dy = kFillCases[c].dy;
    roll_dx_div_dy = kFillCases[c].dx_div_dy;
    render_cx_chars = kFillCases[c].cx;
    render_cy_chars = kFillCases[c].cy;
    render_alt_box = kFillCases[c].alt_box;
    render_alt_shift_x = kFillCases[c].sx;
    render_alt_shift_y = kFillCases[c].sy;
    uint8_t init_sky = kFillCases[c].init_sky;
    *(volatile uint8_t *)&render_sky_rows = init_sky;

    for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
      scr[i] = 0xAA;
      test_clr[i] = (i < (uint16_t)init_sky * 40) ? COLOR_SKY8 : 0x55;
    }
    _render_fill_sky_ground_asm();
    uint8_t want_sky = _ref_fill_sky_ground(init_sky);
    expect_id(*(volatile uint8_t *)&render_sky_rows, want_sky);

    *(volatile uint8_t *)0x01 = 0x35;
    for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
      expect_id(scr[i], ref_scr[i]);
      expect_id(test_clr[i], ref_clr[i]);
    }
    *(volatile uint8_t *)0x01 = saved_p01;
  }
  _init_row_ptrs();
}

#endif // __TARGET_TEST_PLANES__

#ifdef __TARGET_TEST_PLANES__

// ---------------------------------------------------------------------------
// 28. flight_asm.cc _flight_physics_move_asm, _flight_fuel_step_asm &
//     _flight_turn_rot_asm against exact pure-C arcade model reference.
// ---------------------------------------------------------------------------
uint8_t flight_step_shift;
bool model_on_ground;
bool model_need_normalize;
mat3_t flight_cam;
int16_t flight_speed;
int16_t flight_vspeed;
uint8_t flight_throttle;
uint32_t flight_fuel;
uint8_t flight_flap;
uint8_t flight_gear;
uint8_t flight_stall;
int32_t flight_eye_x;
int32_t flight_eye_y;
int32_t flight_eye_z;

static int16_t _ref_flight_step_s(int16_t v) {
  for (uint8_t n = flight_step_shift; n != 0; --n) {
    v >>= 1;
  }
  return v;
}

static uint8_t _ref_flight_physics_move(uint8_t whole_step, mat3_t *cam,
                                        int16_t *spd, int16_t *vspd,
                                        uint8_t *stall, bool *need_norm,
                                        int32_t *ex, int32_t *ey, int32_t *ez) {
  uint8_t alt_penalty = 128;
  uint16_t z8 = (uint16_t)(*ez >> 8);
  if ((uint8_t)(*ez >> 24) == 0 && z8 < 0x1010) {
    alt_penalty = z8 < 0x0800 ? 0 : (uint8_t)((z8 - 0x0800) >> 4);
  }
  uint16_t density = 256 - alt_penalty;

  uint16_t speed_sqr = vec_fastsqr8p8(*spd);
  const uint16_t drag = (uint16_t)_ref_flight_step_s((int16_t)(speed_sqr >> 1));
  const uint16_t extra = drag >> 11;
  *spd -= (int16_t)(drag >> 9);
  if (flight_gear) {
    *spd -= (int16_t)extra;
  }
  if (flight_flap) {
    *spd -= (int16_t)extra;
  }
  if (!model_on_ground) {
    *spd -= _ref_flight_step_s((int16_t)vec_fastsqr8p8(cam->left.z)) >> 5;
  }
  *spd -= _ref_flight_step_s(cam->front.z) >> 3;
  if (flight_fuel > 0) {
    *spd += _ref_flight_step_s(vec_fastmul8p8(flight_throttle, (int16_t)density));
  }

  int16_t sink_penalty = 0;
  uint16_t stall_speed = 0;
  if (!model_on_ground) {
    int16_t raw_lift = vec_fastmul8p8((int16_t)(speed_sqr >> 2), cam->up.z);
    int16_t lift = vec_fastmul8p8(raw_lift, (int16_t)density);
    if (flight_flap) {
      lift += lift >> 1;
    }
    int16_t deficit = kFlightAsmTrimLift - lift;
    if (deficit > 0) {
      sink_penalty = deficit >> 4;
      *spd -= _ref_flight_step_s(deficit) >> 10;
    }
    uint16_t base_stall_speed;
    if (flight_flap) {
      base_stall_speed =
          (cam->up.z >= 0) ? kFlightAsmStallWithFlaps : 0x0480;
    } else {
      base_stall_speed = kFlightAsmStallWithoutFlaps;
    }
    stall_speed = base_stall_speed + ((uint16_t)alt_penalty << 1);
  } else if (whole_step && flight_throttle == 0 && *spd > 0) {
    *spd -= 2;
  }

  if (*spd < 0) {
    *spd = 0;
  } else if (*spd > (int16_t)kMaxSpeed) {
    *spd = (int16_t)kMaxSpeed;
  }

  *stall = (uint16_t)*spd < stall_speed ? 1 : 0;
  if (*stall) {
    if (cam->front.z > kFlightAsmMaxStallPitchZ) {
      vec_rotate_step(cam->up.z < 0 ? VEC_STEP_PITCH_UP : VEC_STEP_PITCH_DOWN,
                      cam);
    } else {
      uint8_t s =
          (uint8_t)(_ref_flight_step_s((int16_t)(stall_speed - (uint16_t)*spd)) >> 5);
      if (s == 0) {
        s = whole_step ? 1 : 0;
      }
      cam->front.z -= s;
      if (cam->front.z < -256) {
        cam->front.z = -256;
      }
    }
    *need_norm = true;
  }

  if (!model_on_ground) {
    *vspd = vec_fastmul8p8(cam->front.z, *spd) - sink_penalty;
  } else {
    if (cam->front.z < 0) {
      cam->front.z = 0;
      *need_norm = true;
    }
    if (cam->left.z != 0) {
      cam->left.x = -cam->front.y;
      cam->left.y = cam->front.x;
      cam->left.z = 0;
      vec_cross(&cam->front, &cam->left, &cam->up);
      *need_norm = true;
    }
    *vspd = 0;
  }

  int16_t fspeed = (int16_t)(*spd << 1);
  *ex += _ref_flight_step_s(vec_fastmul8p8(cam->front.x, fspeed));
  *ey += _ref_flight_step_s(vec_fastmul8p8(cam->front.y, fspeed));
  *ez += _ref_flight_step_s(*vspd);
  if (*ez <= (int32_t)kFlightAsmMinEyeZ) {
    *ez = (int32_t)kFlightAsmMinEyeZ;
    return 1;
  }
  return 0;
}

static __noinline void _expect_u32(uint16_t id, uint32_t got, uint32_t want) {
  expect(id, (int16_t)(got & 0xFFFFu), (int16_t)(want & 0xFFFFu));
  expect((uint16_t)(id + 1), (int16_t)(got >> 16), (int16_t)(want >> 16));
}

static __noinline void test_flight_asm(void) {
  static const struct FlightCase {
    int16_t fx, fy, fz, lx, ly, lz, ux, uy, uz;
    int16_t speed;
    uint8_t throttle, flap, gear, on_ground, whole_step;
    uint32_t fuel;
    int32_t ex, ey, ez;
  } kCases[] = {
      // 0: level cruise at low altitude
      {256, 0, 0, 0, 256, 0, 0, 0, 256, 0x0800, 18, 0, 0, 0, 1, 0x10000L,
       0x100000L, 0x200000L, 0x040000L},
      // 1: climbing banked turn with gear & flaps, mid altitude decay
      {180, 120, 140, -100, 200, 128, -80, -60, 230, 0x0600, 24, 1, 1, 0, 1,
       0x00100L, 0x010000L, -0x020000L, 0x0C3400L},
      // 2: inverted with flaps, high altitude cap (0x100000)
      {200, -160, -40, 160, 200, -40, 0, 0, -256, 0x0470, 10, 1, 0, 0, 1,
       0x00001L, 0x000000L, 0x000000L, 0x100000L},
      // 3: altitude boundary 0x07FFFF (penalty 0) & fuel == 0
      {256, 0, 0, 0, 256, 0, 0, 0, 256, 0x0700, 20, 0, 0, 0, 1, 0L,
       0x000000L, 0x000000L, 0x07FFFFL},
      // 4: altitude boundary 0x080000 (penalty 0) & 0x0FFFFF
      {256, 0, 0, 0, 256, 0, 0, 0, 256, 0x0900, 24, 0, 0, 0, 1, 10L,
       0x000000L, 0x000000L, 0x0FFFFFL},
      // 5: altitude > 0x01000000 (byte 3 nonzero -> penalty 128), top speed clamp
      {256, 0, -256, 0, 256, 0, 256, 0, 0, 0x0EFE, 24, 0, 0, 0, 1, 10L,
       0x000000L, 0x000000L, 0x01020000L},
      // 6: stall direct break with s > 0 and front.z clamp to -256
      {0, 10, -250, 0, 256, 0, 256, 0, 10, 0x0100, 0, 0, 0, 0, 1, 0L,
       0x000000L, 0x000000L, 0x050000L},
      // 7: stall direct break with s == 0, whole_step = 0 vs 1
      {256, 0, 10, 0, 256, 0, 0, 0, 256, 0x03F8, 0, 0, 0, 0, 0, 0L,
       0x000000L, 0x000000L, 0x050000L},
      // 8: stall dead-spot front.z > 224, up.z >= 0 (PITCH_DOWN)
      {100, 0, 235, 0, 256, 0, -235, 0, 100, 0x0200, 0, 0, 0, 0, 1, 0L,
       0x000000L, 0x000000L, 0x050000L},
      // 9: stall dead-spot front.z > 224, up.z < 0 (PITCH_UP)
      {-100, 0, 235, 0, -256, 0, -235, 0, -100, 0x0200, 0, 0, 0, 0, 1, 0L,
       0x000000L, 0x000000L, 0x050000L},
      // 10: ground mode, wheel friction + speed clamp < 0 + negative front.z + tilted left.z
      {200, 150, -30, -150, 200, 40, 0, 0, 256, 1, 0, 1, 1, 1, 1, 0L,
       0x000000L, 0x000000L, 0x002000L},
      // 11: ground mode parked (speed == 0)
      {256, 0, 0, 0, 256, 0, 0, 0, 256, 0, 0, 0, 1, 1, 1, 0x20000L,
       0x005000L, 0x006000L, 0x002000L},
      // 12: floor clamp boundary (diving through 0x2000)
      {256, 0, -100, 0, 256, 0, 0, 0, 256, 0x0500, 10, 0, 0, 0, 1, 10L,
       0x000000L, 0x000000L, 0x002010L},
      // 13: floor clamp exact 0x2000 vs 0x2001 (level, vspeed 0)
      {256, 0, 0, 0, 256, 0, 0, 0, 256, 0x0800, 18, 0, 0, 0, 1, 10L,
       0x000000L, 0x000000L, 0x002001L},
  };

  for (uint8_t sh = 0; sh <= 2; ++sh) {
    flight_step_shift = sh;
    vec_set_rotation_shift(sh);
    for (uint8_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
      const struct FlightCase *tc = &kCases[i];
      mat3_t ref_cam = {{tc->fx, tc->fy, tc->fz},
                        {tc->lx, tc->ly, tc->lz},
                        {tc->ux, tc->uy, tc->uz}};
      flight_cam = ref_cam;
      flight_speed = tc->speed;
      flight_vspeed = 0x1234;
      flight_throttle = tc->throttle;
      flight_flap = tc->flap;
      flight_gear = tc->gear;
      flight_stall = 0;
      flight_fuel = tc->fuel;
      model_on_ground = tc->on_ground != 0;
      model_need_normalize = false;
      flight_eye_x = tc->ex;
      flight_eye_y = tc->ey;
      flight_eye_z = tc->ez;

      int16_t ref_spd = tc->speed;
      int16_t ref_vspd = 0x1234;
      uint8_t ref_stall = 0;
      bool ref_norm = false;
      int32_t ref_ex = tc->ex, ref_ey = tc->ey, ref_ez = tc->ez;

      uint8_t got_floor = _flight_physics_move_asm(tc->whole_step);
      uint8_t want_floor = _ref_flight_physics_move(
          tc->whole_step, &ref_cam, &ref_spd, &ref_vspd, &ref_stall, &ref_norm,
          &ref_ex, &ref_ey, &ref_ez);

      uint16_t id = (uint16_t)(28000 + sh * 400 + i * 20);
      expect(id + 0, got_floor, want_floor);
      expect(id + 1, flight_speed, ref_spd);
      expect(id + 2, flight_vspeed, ref_vspd);
      expect(id + 3, flight_stall, ref_stall);
      expect(id + 4, model_need_normalize ? 1 : 0, ref_norm ? 1 : 0);
      expect(id + 5, flight_cam.front.x, ref_cam.front.x);
      expect(id + 6, flight_cam.front.y, ref_cam.front.y);
      expect(id + 7, flight_cam.front.z, ref_cam.front.z);
      expect(id + 8, flight_cam.left.x, ref_cam.left.x);
      expect(id + 9, flight_cam.left.y, ref_cam.left.y);
      expect(id + 10, flight_cam.left.z, ref_cam.left.z);
      expect(id + 11, flight_cam.up.x, ref_cam.up.x);
      expect(id + 12, flight_cam.up.y, ref_cam.up.y);
      expect(id + 13, flight_cam.up.z, ref_cam.up.z);
      _expect_u32((uint16_t)(id + 14), (uint32_t)flight_eye_x, (uint32_t)ref_ex);
      _expect_u32((uint16_t)(id + 16), (uint32_t)flight_eye_y, (uint32_t)ref_ey);
      _expect_u32((uint16_t)(id + 18), (uint32_t)flight_eye_z, (uint32_t)ref_ez);
    }
  }
  flight_step_shift = 0;
  vec_set_rotation_shift(0);

  // Part B: _flight_fuel_step_asm across 32-bit borrow & exhaustion edges.
  static const struct FuelCase {
    uint32_t fuel;
    uint8_t fc;
  } kFuelCases[] = {
      {0x021FFFL, 24},
      {0x01000000L, 1},
      {0x00010000L, 1},
      {0x00000100L, 1},
      {0x00000105L, 10},
      {0x00000019L, 24},
      {0x00000018L, 24},
      {0x00000005L, 24},
      {0x00000000L, 0},
      {0x00000000L, 10},
      {0x00000001L, 0},
  };
  for (uint8_t i = 0; i < sizeof(kFuelCases) / sizeof(kFuelCases[0]); ++i) {
    flight_fuel = kFuelCases[i].fuel;
    flight_throttle = 18;
    uint8_t fc = kFuelCases[i].fc;
    uint8_t got_dry = _flight_fuel_step_asm(fc);

    uint32_t want_fuel = kFuelCases[i].fuel;
    uint8_t want_thr = 18;
    uint8_t want_dry = 0;
    if (want_fuel > fc) {
      want_fuel -= fc;
    } else {
      want_dry = (want_fuel != 0) ? 1 : 0;
      want_fuel = 0;
      want_thr = 0;
    }
    uint16_t id = (uint16_t)(29500 + i * 4);
    expect(id + 0, got_dry, want_dry);
    expect(id + 1, *(volatile uint8_t *)&flight_throttle, want_thr);
    _expect_u32((uint16_t)(id + 2), flight_fuel, want_fuel);
  }

  // Part C: _flight_turn_rot_asm across ground/air, sign edges, step shifts.
  static const int16_t kLeftZ[] = {-256, -128, -33, -32, -31, -1, 0, 1, 31, 32, 33, 128, 256};
  for (uint8_t gnd = 0; gnd <= 1; ++gnd) {
    model_on_ground = gnd != 0;
    for (uint8_t sh = 0; sh <= 2; ++sh) {
      flight_step_shift = sh;
      for (uint8_t i = 0; i < sizeof(kLeftZ) / sizeof(kLeftZ[0]); ++i) {
        flight_cam.left.z = kLeftZ[i];
        int8_t got_rot = _flight_turn_rot_asm();
        int8_t want_rot = gnd ? 0 : (int8_t)(_ref_flight_step_s(kLeftZ[i]) >> 5);
        expect((uint16_t)(28600 + gnd * 100 + sh * 20 + i), got_rot, want_rot);
      }
    }
  }
  flight_step_shift = 0;
}

// --- Test 29: panel_asm.cc panel_update_instruments() ---
uint16_t msg_span_x0;
uint16_t msg_span_x1;
bool msg_active(void) { return false; }
bool mem_using_alt_buffer;

uint8_t flight_true_heading;
uint8_t flight_nav_heading;
uint8_t roll_angle;
view_state_t view_state;
uint8_t _gfx_heading_bitmap_cache;
uint8_t gun_ammo[kGunAmmoDigits];
bool gun_ammo_shown;
const char kGfxAmmoDigits[80] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
};

static const uint8_t _ref_roll_to_bitmap[30] = {
    8,  7,  7,  6,  6,  5,  5,  4,  4,  3,  3,  2,  2,  1,  1,
    0,  15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9,  9};

static void _ref_set_arm(uint8_t idx, uint8_t dir, uint8_t *rx, uint8_t *ry,
                         uint8_t *ri) {
  uint8_t arm_idx = dir & 0x0F;
  uint8_t off = (uint8_t)(arm_idx << 1);
  int8_t px, py;
  uint8_t bm;
  if (idx == kSpriteIdxSpeed || idx == kSpriteIdxAlt1) {
    px = ((const int8_t *)kSpriteDefLongArm)[off];
    py = ((const int8_t *)kSpriteDefLongArm)[off + 1];
    bm = kSpriteDefLongArmBitmap;
  } else {
    px = ((const int8_t *)kSpriteDefShortArm)[off];
    py = ((const int8_t *)kSpriteDefShortArm)[off + 1];
    bm = kSpriteDefShortArmBitmap;
  }
  if (dir & 0x10) {
    px = (int8_t)(24 - px);
    py = (int8_t)(20 - py);
  }
  rx[idx] = (uint8_t)(kSpriteInstrumentPivotX[idx] - px);
  ry[idx] = (uint8_t)(kSpriteInstrumentPivotY[idx] - py);
  ri[idx] = (uint8_t)(bm + arm_idx);
}

static void _ref_panel_sprites(uint8_t *rx, uint8_t *ry, uint8_t *ri) {
  _ref_set_arm(kSpriteIdxSpeed, (uint8_t)((uint8_t)(flight_speed >> 6) >> 1),
               rx, ry, ri);
  uint16_t alt = (uint16_t)(flight_eye_z >> 8);
  uint16_t a = alt >> 4;
  _ref_set_arm(kSpriteIdxAlt1, (uint8_t)a, rx, ry, ri);
  _ref_set_arm(kSpriteIdxAlt2,
               (uint8_t)(((uint8_t)vec_fastmul8p8((int16_t)a, 205)) >> 3), rx,
               ry, ri);
  int16_t vs = flight_vspeed;
  if (vs > 0x300) {
    vs = 0x300;
  } else if (vs < -0x300) {
    vs = -0x300;
  }
  _ref_set_arm(kSpriteIdxVSpeed, (uint8_t)(0x18 + (vs >> 6)), rx, ry, ri);
  rx[kSpriteIdxRoll] = 0xAC;
  ry[kSpriteIdxRoll] = 0xD8;
  uint8_t r = roll_angle;
  if (r >= 30) {
    r -= 30;
  }
  ri[kSpriteIdxRoll] = (uint8_t)(0x60 + _ref_roll_to_bitmap[r]);
  rx[kSpriteIdxPitch] = 0xAC;
  int8_t pa = (int8_t)(flight_cam.front.z >> 2);
  ry[kSpriteIdxPitch] = (uint8_t)(0xD8 - (pa >> 2));
  ri[kSpriteIdxPitch] = 0x68;
  _ref_set_arm(kSpriteIdxThrottle, (uint8_t)(0x14 + flight_throttle), rx, ry,
               ri);
  _ref_set_arm(kSpriteIdxFuel,
               (uint8_t)(0x18 + ((uint16_t)(flight_fuel >> 8) >> 5)), rx, ry,
               ri);
}

static void _check_panel_sprites(uint16_t base_id) {
  uint8_t rx[8], ry[8], ri[8];
  _ref_panel_sprites(rx, ry, ri);
  for (uint8_t i = 0; i < 8; ++i) {
    expect((uint16_t)(base_id + i * 3 + 0), _sprites_instrument_xy[i].x, rx[i]);
    expect((uint16_t)(base_id + i * 3 + 1), _sprites_instrument_xy[i].y, ry[i]);
    expect((uint16_t)(base_id + i * 3 + 2), _sprites_instrument_idx[i], ri[i]);
  }
}

static void test_panel_asm(void) {
  // Seed the four heading strip sources ($F000-$F17F) with distinct bytes.
  volatile uint8_t *hdg_src = (volatile uint8_t *)0xF000;
  for (uint16_t i = 0; i < 0x180; ++i) {
    hdg_src[i] = (uint8_t)((i * 37 + (i >> 5) * 13 + 7) & 0xFF);
  }
  volatile uint8_t *hdg_dst = (volatile uint8_t *)0xF5C8;
  volatile uint8_t *ammo_c = (volatile uint8_t *)0xF6B0;
  volatile uint8_t *ammo_l = (volatile uint8_t *)0xF7B0;

  panel_asm_reset_cache();
  _gfx_heading_bitmap_cache = 0xFF;
  gun_ammo[0] = 0;
  gun_ammo[1] = 6;
  gun_ammo[2] = 0;
  gun_ammo_shown = true;
  view_state = VIEW_CENTER;
  flight_speed = 0x0880;
  flight_eye_z = 0x040000L;
  flight_vspeed = 0;
  flight_throttle = 12;
  flight_fuel = 0x00800000UL;
  flight_cam.front.z = 0;
  flight_true_heading = 0;
  flight_nav_heading = 0;
  flight_stall = 0;
  flight_flap = 0;
  flight_gear = 0;

  // 1) Sweep all 60 roll angles (checks closed-form roll formula + init).
  for (uint8_t r = 0; r < 60; ++r) {
    roll_angle = r;
    panel_update_instruments();
    _check_panel_sprites((uint16_t)(29000 + r));
  }

  // 2) Sweep pitch values across [-256, 256].
  static const int16_t kPitchZ[] = {-256, -255, -128, -17, -16, -4, -1,
                                    0,    1,    15,   16,  128, 255, 256};
  for (uint8_t i = 0; i < 14; ++i) {
    flight_cam.front.z = kPitchZ[i];
    panel_update_instruments();
    _check_panel_sprites((uint16_t)(29100 + i));
  }
  flight_cam.front.z = 0;

  // 3) Sweep speed, altitude (incl. a_hi == 0 and a_hi > 0), vspeed edges,
  // throttle, fuel, plus repeated identical calls (cache hits) and single-
  // gauge changes (cache misses).
  static const int16_t kSpd[] = {0, 0x0080, 0x03C0, 0x0880, 0x0E80, 0x0F00};
  static const int32_t kAlt[] = {0x000000L, 0x002000L, 0x041234L,
                                 0x0FF000L, 0x100000L, 0x15A300L};
  static const int16_t kVsp[] = {-0x400, -0x301, -0x300, -0x180, -1,
                                 0,      0x040,  0x2FF,  0x300,  0x500};
  static const uint32_t kFuel[] = {0x000000UL, 0x002000UL, 0x01E000UL,
                                   0x03FFFFUL, 0x054321UL, 0x0F8000UL};
  for (uint8_t i = 0; i < 6; ++i) {
    flight_speed = kSpd[i];
    flight_eye_z = kAlt[i];
    flight_throttle = (uint8_t)(i * 4);
    flight_fuel = kFuel[i];
    panel_update_instruments();
    _check_panel_sprites((uint16_t)(29200 + i * 30));
    // Repeat unchanged: cache hit path must preserve every sprite byte.
    panel_update_instruments();
    _check_panel_sprites((uint16_t)(29215 + i * 30));
  }
  for (uint8_t i = 0; i < 10; ++i) {
    flight_vspeed = kVsp[i];
    panel_update_instruments();
    _check_panel_sprites((uint16_t)(29400 + i * 25));
  }
  // Altitude fine change within same (flight_eye_z >> 12) vs next step.
  flight_eye_z = 0x123000L;
  panel_update_instruments();
  _check_panel_sprites(29700);
  flight_eye_z = 0x123FF0L;
  panel_update_instruments();
  _check_panel_sprites(29730);
  flight_eye_z = 0x124000L;
  panel_update_instruments();
  _check_panel_sprites(29760);

  // 4) Heading strip copy (all 4 sub-char shifts + wrap across 96) + cache.
  static const uint8_t kHdg[] = {0, 1, 2, 3, 35, 36, 37, 44, 47};
  static const uint16_t kHdgBase[4] = {0xF120, 0xF0C0, 0xF060, 0xF000};
  for (uint8_t hi = 0; hi < 9; ++hi) {
    uint8_t h = kHdg[hi];
    flight_true_heading = h;
    panel_update_instruments();
    uint8_t ch = (uint8_t)((h >> 2) + 3);
    if (ch >= 12) {
      ch -= 12;
    }
    uint8_t s = (uint8_t)(ch << 3);
    const volatile uint8_t *src =
        (const volatile uint8_t *)kHdgBase[h & 3];
    for (uint8_t d = 0; d < 48; ++d) {
      expect((uint16_t)(29800 + hi * 50 + d), hdg_dst[d], src[s]);
      if (++s == 96) {
        s = 0;
      }
    }
  }
  // Cache skip test: overwrite hdg_dst[0] and re-call with same heading.
  hdg_dst[0] = 0x55;
  panel_update_instruments();
  expect(30300, hdg_dst[0], 0x55);

  // 5) All 5 color-RAM lamps across nav_heading boundaries + stall/flap/gear.
  static const uint8_t kNav[] = {0, 1, 23, 24, 25, 47};
  for (uint8_t ni = 0; ni < 6; ++ni) {
    uint8_t nh = kNav[ni];
    flight_nav_heading = nh;
    flight_stall = (ni & 1);
    flight_flap = ((ni >> 1) & 1);
    flight_gear = ((ni >> 2) & 1);
    panel_update_instruments();
    uint8_t want_l = (nh == 0 || nh > 24) ? 0x0A : 0x00;
    uint8_t want_r = (nh < 24) ? 0x0A : 0x00;
    expect((uint16_t)(30310 + ni * 5 + 0),
           (*(volatile uint8_t *)0xDA6D) & 0x0F, want_l);
    expect((uint16_t)(30310 + ni * 5 + 1),
           (*(volatile uint8_t *)0xDA6E) & 0x0F, want_r);
    expect((uint16_t)(30310 + ni * 5 + 2),
           (*(volatile uint8_t *)0xDA66) & 0x0F, flight_stall ? 0x0A : 0x00);
    expect((uint16_t)(30310 + ni * 5 + 3),
           (*(volatile uint8_t *)0xDAB6) & 0x0F, flight_flap ? 0x0A : 0x00);
    expect((uint16_t)(30310 + ni * 5 + 4),
           (*(volatile uint8_t *)0xDADE) & 0x0F, flight_gear ? 0x0A : 0x00);
  }

  // 6) Ammo counter in VIEW_CENTER (3 digits at $F6B0) and VIEW_LEFT (2 at $F7B0),
  // plus VIEW_RIGHT / VIEW_BACK skipping heading, lamps, and ammo.
  for (uint8_t i = 0; i < 24; ++i) {
    ammo_c[i] = 0xEE;
  }
  for (uint8_t i = 0; i < 16; ++i) {
    ammo_l[i] = 0xDD;
  }
  gun_ammo[0] = 1;
  gun_ammo[1] = 7;
  gun_ammo[2] = 4;
  gun_ammo_shown = false;
  view_state = VIEW_CENTER;
  panel_update_instruments();
  expect(30400, *(volatile uint8_t *)&gun_ammo_shown, 1);
  for (uint8_t dig = 0; dig < 3; ++dig) {
    for (uint8_t b = 0; b < 8; ++b) {
      expect((uint16_t)(30401 + dig * 8 + b), ammo_c[dig * 8 + b],
             (uint8_t)kGfxAmmoDigits[gun_ammo[dig] * 8 + b]);
    }
  }
  // Cache skip when gun_ammo_shown == true.
  ammo_c[0] = 0xCC;
  panel_update_instruments();
  expect(30430, ammo_c[0], 0xCC);

  // VIEW_LEFT: 2 digits at $F7B0, lamps untouched.
  *(volatile uint8_t *)0xDA66 = 0x05;
  flight_stall = 1;
  gun_ammo[0] = 9;
  gun_ammo[1] = 2;
  gun_ammo[2] = 5;
  gun_ammo_shown = false;
  view_state = VIEW_LEFT;
  panel_update_instruments();
  expect(30440, *(volatile uint8_t *)&gun_ammo_shown, 1);
  expect(30441, (*(volatile uint8_t *)0xDA66) & 0x0F, 0x05);
  for (uint8_t dig = 0; dig < 2; ++dig) {
    for (uint8_t b = 0; b < 8; ++b) {
      expect((uint16_t)(30442 + dig * 8 + b), ammo_l[dig * 8 + b],
             (uint8_t)kGfxAmmoDigits[gun_ammo[dig] * 8 + b]);
    }
  }

  // VIEW_RIGHT and VIEW_BACK: gun_ammo_shown stays false.
  gun_ammo_shown = false;
  view_state = VIEW_RIGHT;
  panel_update_instruments();
  expect(30470, *(volatile uint8_t *)&gun_ammo_shown, 0);
  view_state = VIEW_BACK;
  panel_update_instruments();
  expect(30471, *(volatile uint8_t *)&gun_ammo_shown, 0);
}

// ---------------------------------------------------------------------------
// 30. sound_asm.cc sound_update() against a pure C reference across all
//     engine/wind/Voice 3 paths (throttle & speed clamps, LFSR + PWM wrap,
//     pause/volume/fuel/crash silence, touchdown/hit/gun/gear/flap priority,
//     stall warble on/gap/wrap, crash sweep 16..0 & R-restart abort).
// ---------------------------------------------------------------------------
bool flight_paused;
enum FlightStatus flight_status;
uint8_t flight_events;
uint8_t flight_gen;
uint8_t sound_volume;

static uint8_t _sref_shadow[kSoundRegCount];
static uint8_t _sref_gen;
static uint8_t _sref_pwm_phase;
static uint8_t _sref_rng;
static uint8_t _sref_v3_effect;
static uint8_t _sref_v3_frames;
static uint8_t _sref_stall_phase;
static uint8_t _sref_flight_gen_seen;

static uint8_t _ref_sound_rand(void) {
  bool lsb = (_sref_rng & 1) != 0;
  _sref_rng >>= 1;
  if (lsb) {
    _sref_rng ^= kSoundRngTaps;
  }
  return _sref_rng;
}

static bool _ref_pick_v3(uint8_t events, uint8_t from, uint8_t to) {
  if (events == 0) {
    return false;
  }
  for (uint8_t i = from; i < to; ++i) {
    if (events & kSoundV3Ev[i]) {
      _sref_v3_effect = kSoundV3Id[i];
      _sref_v3_frames = kSoundV3Dur[i];
      return true;
    }
  }
  return false;
}

static __noinline void _ref_sound_update(void) {
  uint8_t r_pw = _ref_sound_rand();
  uint8_t r_pitch = _ref_sound_rand();
  _sref_pwm_phase = (uint8_t)(_sref_pwm_phase + kSoundPwmStep + (r_pw & 3));

  bool driver_live = (sound_volume != 0 && !flight_paused);
  bool flying =
      driver_live && (flight_status < FLIGHT_CRASH_ROLL) && (flight_fuel != 0);

  uint8_t tri = (uint8_t)(_sref_pwm_phase << 1);
  if (_sref_pwm_phase & 0x80) {
    tri ^= 0xFE;
  }
  uint16_t pw = (uint16_t)(kSoundPwmMin + ((uint16_t)tri << 3) +
                           (r_pw & kSoundPwmJitterMask));

  uint8_t thr = flight_throttle > kMaxThrottle ? kMaxThrottle : flight_throttle;
  uint16_t base = (uint16_t)(((uint16_t)kSoundEngineFreqHi[thr] << 8) |
                             kSoundEngineFreqLo[thr]);
  uint8_t amp = (uint8_t)(base >> kSoundEngineJitterShift);
  uint16_t prod =
      (uint16_t)((uint16_t)(amp << 1) * (uint16_t)((r_pitch & 0x1F) << 3)) >> 8;
  uint16_t freq = (uint16_t)((int16_t)base + (int16_t)prod - (int16_t)amp);

  uint8_t events = 0;
  if (flight_gen != _sref_flight_gen_seen) {
    _sref_flight_gen_seen = flight_gen;
    events = flight_events;
  }
  bool retrigger = false;
  bool stall_gap = false;
  if (_sref_v3_frames != 0) {
    --_sref_v3_frames;
  }
  if (!driver_live) {
    _sref_v3_effect = V3_NONE;
    _sref_v3_frames = 0;
  } else if (events & FLIGHT_EV_CRASH) {
    _sref_v3_effect = V3_CRASH;
    _sref_v3_frames = kSoundCrashFrames;
    retrigger = true;
  } else if (_sref_v3_effect == V3_CRASH) {
    if (_sref_v3_frames == 0 || flight_status < FLIGHT_CRASH_ROLL) {
      _sref_v3_effect = V3_NONE;
      _sref_v3_frames = 0;
    }
  } else if (!flying) {
    _sref_v3_effect = V3_NONE;
    _sref_v3_frames = 0;
  } else if (_ref_pick_v3(events, 0, 2)) {
    retrigger = true;
  } else if (_sref_v3_frames != 0 && _sref_v3_effect != V3_STALL) {
  } else if (flight_stall) {
    if (_sref_v3_effect != V3_STALL) {
      _sref_stall_phase = 0;
    }
    _sref_v3_effect = V3_STALL;
    uint8_t sp = _sref_stall_phase;
    if (sp == 0) {
      retrigger = true;
    }
    stall_gap = sp >= kSoundStallOnFrames;
    ++sp;
    _sref_stall_phase = sp >= kSoundStallPeriodFrames ? 0 : sp;
  } else if (_ref_pick_v3(events, 2, 5)) {
    retrigger = true;
  } else if (_sref_v3_frames == 0) {
    _sref_v3_effect = V3_NONE;
  }
  if (retrigger) {
    ++_sref_gen;
  }

  uint8_t eff = _sref_v3_effect;
  uint16_t v3_freq = (uint16_t)((uint16_t)kSoundV3FreqHi[eff] << 8);
  if (eff == V3_CRASH) {
    uint8_t m = (uint8_t)((_sref_v3_frames << 3) - _sref_v3_frames +
                          (kSoundCrashFreqEnd >> 5));
    v3_freq = ((uint16_t)((uint16_t)m << 8)) >> 3;
  }
  _sref_shadow[14] = (uint8_t)v3_freq;
  _sref_shadow[15] = (uint8_t)(v3_freq >> 8);
  _sref_shadow[17] = eff == V3_STALL ? (uint8_t)(kSoundStallPw >> 8) : 0;
  _sref_shadow[19] = kSoundV3AttDec[eff];
  _sref_shadow[20] = kSoundV3SusRel[eff];
  _sref_shadow[18] = stall_gap ? SID_CTRL_RECT : kSoundV3Ctrl[eff];

  _sref_shadow[0] = (uint8_t)freq;
  _sref_shadow[1] = (uint8_t)(freq >> 8);
  _sref_shadow[2] = (uint8_t)pw;
  _sref_shadow[3] = (uint8_t)(pw >> 8) & 0x0F;
  _sref_shadow[5] = kSoundEngineAttDec;
  _sref_shadow[6] = kSoundEngineSusRel;
  _sref_shadow[4] = flying ? (SID_CTRL_RECT | SID_CTRL_GATE) : SID_CTRL_RECT;

  int16_t spd = flight_speed < 0 ? 0 : flight_speed;
  uint8_t widx = (uint8_t)((uint16_t)spd >> kSoundWindSpeedShift);
  if (widx >= kSoundWindSteps) {
    widx = kSoundWindSteps - 1;
  }
  _sref_shadow[7] = kSoundWindFreqLo[widx];
  _sref_shadow[8] = kSoundWindFreqHi[widx];
  _sref_shadow[12] = kSoundWindAttDec;
  _sref_shadow[13] = kSoundWindSusRel;
  _sref_shadow[11] = (flying && flight_speed >= (int16_t)kSoundWindMinSpeed)
                         ? (SID_CTRL_NOISE | SID_CTRL_GATE)
                         : SID_CTRL_NOISE;

  uint8_t vol = sound_volume < kSoundVolumeSteps ? sound_volume
                                                 : kSoundVolumeDefault;
  _sref_shadow[24] = (flying || eff != V3_NONE) ? kMasterVolume[vol] : 0;
}

static __noinline void _check_sound_step(uint16_t id) {
  sound_dirty = 0;
  sound_update();
  _ref_sound_update();
  expect(id, *(volatile uint8_t *)&sound_dirty, 1);
  expect((uint16_t)(id + 1), sound_gen, _sref_gen);
  expect((uint16_t)(id + 2), _sound_pwm_phase, _sref_pwm_phase);
  expect((uint16_t)(id + 3), _sound_rng, _sref_rng);
  expect((uint16_t)(id + 4), _sound_v3_effect, _sref_v3_effect);
  expect((uint16_t)(id + 5), _sound_v3_frames, _sref_v3_frames);
  expect((uint16_t)(id + 6), _sound_stall_phase, _sref_stall_phase);
  expect((uint16_t)(id + 7), _sound_flight_gen_seen, _sref_flight_gen_seen);
  for (uint8_t r = 0; r < kSoundRegCount; ++r) {
    expect((uint16_t)(id + 8 + r), sound_shadow[r], _sref_shadow[r]);
  }
}

static void test_sound_asm(void) {
  for (uint8_t r = 0; r < kSoundRegCount; ++r) {
    sound_shadow[r] = 0;
    _sref_shadow[r] = 0;
  }
  sound_gen = _sref_gen = 0;
  sound_gen_seen = 0;
  _sound_pwm_phase = _sref_pwm_phase = 0;
  _sound_rng = _sref_rng = kSoundRngSeed;
  _sound_v3_effect = _sref_v3_effect = V3_NONE;
  _sound_v3_frames = _sref_v3_frames = 0;
  _sound_stall_phase = _sref_stall_phase = 0;
  flight_gen = 1;
  _sound_flight_gen_seen = _sref_flight_gen_seen = 1;
  flight_events = 0;
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  flight_fuel = 0x00100000UL;
  flight_stall = 0;
  sound_volume = kSoundVolumeDefault;

  // 1) Sweep throttles (incl. clamp at 25/30) and speeds (negative, 0, 0x7F,
  //    0x80, 0xFF, 0x100, 0x0F00, 0x1200) across full PWM triangle wrap.
  static const uint8_t kThrs[8] = {0, 1, 6, 12, 19, 24, 25, 40};
  static const int16_t kSpds[8] = {-10, 0, 0x007F, 0x0080,
                                   0x00FF, 0x0100, 0x0F00, 0x1500};
  uint16_t id = 31000u;
  for (uint8_t i = 0; i < 40; ++i) {
    flight_throttle = kThrs[i & 7];
    flight_speed = kSpds[i & 7];
    _check_sound_step(id);
    id += 33;
  }

  // 2) Volume 1, 0, out-of-range (5 -> default 2), pause, fuel 0 (only byte 0
  //    vs byte 3 non-zero vs all zero).
  flight_throttle = 14;
  flight_speed = 0x0500;
  sound_volume = 1;
  _check_sound_step(32400);
  sound_volume = 0;
  _check_sound_step(32440);
  sound_volume = 5;
  _check_sound_step(32480);
  sound_volume = 2;
  flight_paused = true;
  _check_sound_step(32520);
  flight_paused = false;
  flight_fuel = 0;
  _check_sound_step(32560);
  flight_fuel = 1;
  _check_sound_step(32600);
  flight_fuel = 0x01000000UL;
  _check_sound_step(32640);

  // 3) Voice 3 one-shots & priority: flap -> gear ignored while flap running ->
  //    hit preempts -> same gen does not retrigger -> gun while idle ->
  //    touchdown preempts stall.
  flight_events = FLIGHT_EV_FLAP;
  ++flight_gen;
  _check_sound_step(32700);
  // Same flight_gen: events ignored, flap keeps counting down.
  _check_sound_step(32740);
  // Gear arrives while flap still active: dropped.
  flight_events = FLIGHT_EV_GEAR;
  ++flight_gen;
  _check_sound_step(32780);
  // Hit preempts active flap!
  flight_events = FLIGHT_EV_HIT | FLIGHT_EV_GUN;
  ++flight_gen;
  _check_sound_step(32820);
  _check_sound_step(32860);
  // Hit expires -> gun triggers.
  flight_events = FLIGHT_EV_GUN;
  ++flight_gen;
  _check_sound_step(32900);
  // Gun expires -> idle.
  _check_sound_step(32940);

  // 4) Stall warble: 6 frames covers phase 0,1 (on), 2,3 (gap), 0,1 (retrigger).
  flight_stall = 1;
  id = 33000u;
  for (uint8_t s = 0; s < 6; ++s) {
    _check_sound_step(id);
    id += 35;
  }
  // Touchdown preempts stall; stall resumes once touchdown finishes.
  flight_events = FLIGHT_EV_TOUCHDOWN;
  ++flight_gen;
  id = 33250u;
  for (uint8_t s = 0; s < 5; ++s) {
    _check_sound_step(id);
    id += 35;
  }
  flight_stall = 0;
  _check_sound_step(33450u);

  // 5) Crash sweep across all 17 frames (16..0 -> V3_NONE) while crashed.
  flight_status = FLIGHT_CRASH_ROLL;
  flight_events = FLIGHT_EV_CRASH;
  ++flight_gen;
  id = 33500u;
  for (uint8_t s = 0; s < 18; ++s) {
    _check_sound_step(id);
    id += 35;
  }

  // 6) Crash aborted mid-sweep by R restart (flight_status -> FLIGHT_ONGOING)
  //    and by pause (!driver_live).
  flight_status = FLIGHT_CRASH_ROLL;
  flight_events = FLIGHT_EV_CRASH;
  ++flight_gen;
  _check_sound_step(34200);
  flight_status = FLIGHT_ONGOING;
  _check_sound_step(34240);

  flight_status = FLIGHT_CRASH_ROLL;
  flight_events = FLIGHT_EV_CRASH;
  ++flight_gen;
  _check_sound_step(34280);
  flight_paused = true;
  _check_sound_step(34320);
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
}

// 31. opponent_asm.cc _opponent_ai_step_asm, _opponent_fly_and_move_asm,
// _opponent_fall_asm, and _opponent_cap_asm against pure 16-bit C references.
int32_t *const kFlightEye[3] = {&flight_eye_x, &flight_eye_y, &flight_eye_z};
mat3_t world_cam;

static const int8_t kRefOppClimbSpeed[7] = {32, 16, 8, 0, -8, -16, -32};
static const uint16_t kRefOppMinSpeed[2] = {1300, 1100};
static const uint16_t kRefOppMaxSpeed[2] = {2800, 2000};

static bool _ref_opp_below(int32_t z, uint8_t lim) {
  int16_t hm = *(const int16_t *)((const uint8_t *)&z + 1);
  return hm < (int16_t)lim;
}

static uint16_t _ref_opp_abs(int16_t v) { return (uint16_t)(v < 0 ? -v : v); }

static int16_t _ref_opp_dot(int16_t a, int16_t b, int16_t c, int16_t d) {
  return vec_fastmul8p8(a, b) + vec_fastmul8p8(c, d);
}

static int32_t _ref_opp_step_delta(int16_t h, int16_t speed) {
  int16_t v = vec_fastmul8p8(h, (int16_t)(speed << 1));
  for (uint8_t s = flight_step_shift; s != 0; --s) {
    v >>= 1;
  }
  return (int32_t)v;
}

static void _check_ai_case(uint16_t base_id, uint8_t i, uint8_t flags,
                           int16_t dx, int16_t dy, int16_t dz, int16_t hx,
                           int16_t hy, int32_t z, bool ground, uint8_t pullup,
                           int8_t climb, int8_t prev_turn, uint8_t gun,
                           int16_t cam_fx, int16_t cam_fy) {
  uint8_t other = i ^ 1;
  uint8_t bit = (uint8_t)(i + 1);
  bool hunt = (flags & kOppFlagHunt) != 0;
  bool turns = (flags & kOppFlagTurns) != 0;
  bool bomber = (flags & kOppFlagBomber) != 0;

  // Set up sentinel state in untouched slot `other`.
  _opponent_head[other].x = 123;
  _opponent_head[other].y = -45;
  _opponent_turn[other] = -2;
  _opponent_bank[other] = 1;
  _opponent_climb[other] = 2;
  _opponent_pitch[other] = -1;
  _opponent_gun[other] = 17;

  // Set up slot `i` inputs.
  _opponent_head[i].x = hx;
  _opponent_head[i].y = hy;
  _opponent_head[i].z = 0;
  _opponent_z[i] = z;
  _opponent_turn[i] = prev_turn;
  _opponent_bank[i] = 99;
  _opponent_climb[i] = climb;
  _opponent_gun[i] = gun;
  _opponent_pullup = pullup;
  _opponent_firing = (uint8_t)(other + 1); // preserve other bit
  model_on_ground = ground;
  flight_cam.front.x = cam_fx;
  flight_cam.front.y = cam_fy;
  flight_cam.front.z = 0;

  // Pure 16-bit C reference matching opponent.cc.
  int8_t ref_want = 0;
  if (dz > 40) {
    ref_want = 1;
  } else if (dz < -40) {
    ref_want = -1;
  }
  int16_t rdx = dx, rdy = dy, rdz = dz;
  uint8_t cut = 0;
  while ((uint16_t)(rdx + 63) >= 127 || (uint16_t)(rdy + 63) >= 127) {
    rdx >>= 1;
    rdy >>= 1;
    rdz >>= 1;
    ++cut;
  }
  int16_t cross = _ref_opp_dot((int16_t)(rdy << 8), hx,
                               (int16_t)(-(rdx << 8)), hy);
  int16_t dot = _ref_opp_dot((int16_t)(rdx << 8), hx,
                             (int16_t)(rdy << 8), hy);
  uint16_t across = _ref_opp_abs(cross);

  uint8_t ref_pullup = pullup;
  bool ref_aim = false;
  bool ref_break = false;
  bool straight = false;
  if (hunt) {
    uint8_t floor = 120;
    if (ref_pullup & bit) {
      if (!_ref_opp_below(z, 120) && (cut > 5 || !ground)) {
        ref_pullup &= (uint8_t)~bit;
      }
    } else if (ground && dot > 0 && across < ((uint16_t)dot >> 2) && cut > 2 &&
               !_ref_opp_below(z, 77)) {
      floor = 60;
    } else if (_ref_opp_below(z, 120)) {
      ref_pullup |= bit;
    }
    ref_aim = !(ref_pullup & bit) && cut <= 4 && dot > 0 &&
              across < ((uint16_t)dot >> 3);
    ref_break = !ground && dot > 0 &&
                (cut <= 1 ||
                 (cut == 2 && _ref_opp_dot(cam_fx, hx, cam_fy, hy) < 0));
    straight = (ref_pullup & bit) != 0;
    if (straight) {
      ref_want = 1;
    } else {
      uint16_t az16 = _ref_opp_abs(rdz);
      uint8_t az = az16 > 255 ? 255 : (uint8_t)az16;
      int8_t d8x = (int8_t)rdx, d8y = (int8_t)rdy;
      uint8_t ax = (uint8_t)(d8x < 0 ? -d8x : d8x);
      uint8_t ay = (uint8_t)(d8y < 0 ? -d8y : d8y);
      uint8_t h = ax > ay ? (uint8_t)(ax + (ay >> 1))
                          : (uint8_t)(ay + (ax >> 1));
      uint8_t h16 = h >> 4, h8 = h >> 3;
      int8_t w = 0;
      if (az > h16) {
        w = 1;
        if (az > (uint8_t)(h8 + h16)) {
          w = 2;
          if (az > (uint8_t)((h >> 2) + h8)) {
            w = 3;
          }
        }
      }
      int16_t wdz = rdz;
      if (ref_break) {
        w = 3;
        wdz = -wdz;
      }
      if (wdz < 0) {
        w = -w;
      }
      if (_ref_opp_below(z, floor)) {
        if (w < 1) {
          w = 1;
        }
      } else if (w < 0 && _ref_opp_below(z, (uint8_t)(floor + 16))) {
        w = 0;
      }
      ref_want = w;
    }
    if (ref_break) {
      cross = -cross;
      dot = -1;
    }
  } else {
    ref_aim = bomber;
  }

  uint8_t ref_gun = gun;
  uint8_t ref_firing = (uint8_t)(other + 1);
  if (turns) {
    if (ref_gun != 0) {
      --ref_gun;
    }
    if (ref_aim && ref_want == climb) {
      if (ref_gun == 0) {
        ref_gun = 26;
      }
      if (ref_gun >= 22 && !(ref_gun & 1)) {
        ref_firing |= bit;
      }
    }
  }

  int8_t ref_turn = 0;
  if (!straight && (dot < 0 || across > ((uint16_t)dot >> 5))) {
    ref_turn = (int8_t)(hunt ? 2 : 1);
    if (cross < 0) {
      ref_turn = -ref_turn;
    }
  }
  int8_t ref_bank = (ref_turn == prev_turn) ? ref_turn : 0;
  int8_t ref_ret = turns ? (int8_t)(-ref_turn * 8) : 0;

  // Run 6502 assembly.
  vec_v.x = dx;
  vec_v.y = dy;
  vec_v.z = dz;
  int16_t got_ret = _opponent_ai_step_asm(i, flags);

  expect(base_id + 0, got_ret, ref_ret);
  expect(base_id + 1, _opponent_step_want, ref_want);
  expect(base_id + 2, _opponent_pullup, ref_pullup);
  expect(base_id + 3, _opponent_aim ? 1 : 0, ref_aim ? 1 : 0);
  if (hunt) {
    expect(base_id + 4, _opponent_break ? 1 : 0, ref_break ? 1 : 0);
  }
  expect(base_id + 5, _opponent_gun[i], ref_gun);
  expect(base_id + 6, _opponent_firing, ref_firing);
  expect(base_id + 7, _opponent_turn[i], ref_turn);
  expect(base_id + 8, _opponent_bank[i], ref_bank);
  expect(base_id + 9, _opponent_turn[other], -2);
  expect(base_id + 10, _opponent_gun[other], 17);
}

static __noinline void test_opponent_step_asm(void) {
  // 31a. _opponent_ai_step_asm: path mode dz band edges, bomber gun, bank latch.
  static const int16_t kPathDz[7] = {-300, -41, -40, 0, 40, 41, 300};
  uint16_t id = 35000u;
  for (uint8_t k = 0; k < 7; ++k) {
    _check_ai_case(id, k & 1,
                   (uint8_t)((k & 1 ? kOppFlagTurns : 0) |
                             (k & 2 ? kOppFlagBomber : 0)),
                   500, (int16_t)(k * 30 - 90), kPathDz[k], 256, 0,
                   0x00100000L, false, 0, (int8_t)((k % 3) - 1),
                   (int8_t)(k & 1 ? 1 : -1), (uint8_t)(k == 3 ? 1 : 23), 256,
                   0);
    id += 16;
  }

  // 31b. _opponent_ai_step_asm: hunt strafe, pullup, floors, aim, break-off, want.
  // Ground strafe entry (cut=3, dot>0, across < dot>>2, z>=77), pullup clear,
  // pullup set below 120, floor+margin clamp, break-off cut<=1 & cut==2 head-on
  // vs tail chase, dz==0 break-off, and slope thresholds want=0..±3.
  _check_ai_case(35200u, 0, kOppFlagHunt | kOppFlagTurns, 400, 10, -200, 256, 0,
                 (int32_t)90 << 8, true, 0, -2, 0, 0, 256, 0);
  _check_ai_case(35220u, 1, kOppFlagHunt | kOppFlagTurns, 400, 10, -200, 256, 0,
                 (int32_t)76 << 8, true, 0, -1, 2, 25, 256, 0);
  _check_ai_case(35240u, 0, kOppFlagHunt | kOppFlagTurns, 2500, 0, 50, 256, 0,
                 (int32_t)125 << 8, true, 1, 1, 0, 1, 256, 0);
  _check_ai_case(35260u, 0, kOppFlagHunt, 1000, 0, -50, 256, 0,
                 (int32_t)125 << 8, true, 1, 0, 0, 0, 256, 0);
  _check_ai_case(35280u, 1, kOppFlagHunt | kOppFlagTurns, 1000, 0, -50, 256, 0,
                 (int32_t)125 << 8, false, 2, 0, 0, 0, 256, 0);
  // Break-off: cut=1 with dz=0 (climbs +3), cut=2 head-on (cam=-head, breaks),
  // cut=2 tail chase (cam=+head, does not break), and steep |dz|>255 clamp.
  _check_ai_case(35300u, 0, kOppFlagHunt | kOppFlagTurns, 80, 20, 0, 256, 0,
                 (int32_t)300 << 8, false, 0, 3, 2, 0, 256, 0);
  _check_ai_case(35320u, 1, kOppFlagHunt | kOppFlagTurns, 200, -30, 80, 256, 0,
                 (int32_t)300 << 8, false, 0, -3, -2, 23, -256, 0);
  _check_ai_case(35340u, 1, kOppFlagHunt | kOppFlagTurns, 200, -30, 80, 256, 0,
                 (int32_t)300 << 8, false, 0, 1, -2, 23, 256, 0);
  _check_ai_case(35360u, 0, kOppFlagHunt | kOppFlagTurns, 100, 40, -1500, 181,
                 181, (int32_t)130 << 8, false, 0, 0, 0, 0, 181, 181);
  _check_ai_case(35380u, 1, kOppFlagHunt | kOppFlagTurns, -300, -250, 600, 181,
                 -181, (int32_t)100 << 8, false, 0, 2, 2, 24, 0, 256);
}

static __noinline void _expect_i32(uint16_t id, int32_t got, int32_t want) {
  expect(id, (int16_t)(got & 0xFFFF), (int16_t)(want & 0xFFFF));
  expect((uint16_t)(id + 1), (int16_t)(got >> 16), (int16_t)(want >> 16));
}

static __noinline void _test_opp_fly_asm(void) {
  // 31c. _opponent_fly_and_move_asm across whole/substep, fighter/bomber,
  // min/max speed clamp, cruise accel/decel, climb slopes -3..+3, and z<=0 crash.
  uint16_t id = 35500u;
  static const int8_t kWants[7] = {-3, -2, -1, 0, 1, 2, 3};
  static const int16_t kSpeeds[4] = {1100, 1300, 2000, 2800};
  for (uint8_t s = 0; s < 3; ++s) {
    flight_step_shift = s;
    for (uint8_t k = 0; k < 7; ++k) {
      uint8_t slot = k & 1;
      uint8_t flags = (uint8_t)((k & 1 ? kOppFlagTurns : 0) |
                                (k & 2 ? kOppFlagBomber : 0));
      int16_t sp0 = kSpeeds[k & 3];
      int16_t cruise = (int16_t)(k & 4 ? 1750 : sp0);
      int8_t cl0 = (int8_t)(k == 2 ? kWants[k] : 0);
      int8_t pt0 = 1;
      int32_t x0 = 0x00123456L, y0 = 0x00654321L;
      int32_t z0 = (k == 0 && s == 0) ? 4L : 0x00010000L;
      int16_t hx = 204, hy = -153;

      // Pure C reference.
      int8_t rcl = cl0, rpt = pt0;
      uint16_t rsp = (uint16_t)sp0;
      if (flags & kOppFlagTurns) {
        uint8_t role = (flags & kOppFlagBomber) ? 1 : 0;
        rcl = kWants[k];
        if (rcl > 0 ? rsp <= kRefOppMinSpeed[role]
                    : rcl < 0 && rsp >= kRefOppMaxSpeed[role]) {
          rcl = 0;
        }
        if (rsp < (uint16_t)cruise) {
          rsp += 8;
        } else if (rsp > (uint16_t)cruise) {
          rsp -= 8;
        }
        rsp = (uint16_t)((int16_t)rsp + kRefOppClimbSpeed[rcl + 3]);
        if (rcl == cl0) {
          rpt = rcl;
        }
      }
      int32_t rx = x0, ry = y0, rz = z0;
      if (rcl != 0) {
        rz += _ref_opp_step_delta((int16_t)(-kRefOppClimbSpeed[rcl + 3] * 4),
                                  (int16_t)rsp);
      }
      uint8_t rcrash = (rz <= 0) ? 1 : 0;
      if (!rcrash) {
        rx += _ref_opp_step_delta(hx, (int16_t)rsp);
        ry += _ref_opp_step_delta(hy, (int16_t)rsp);
      }

      // Run 6502 assembly.
      _opponent_speed[slot] = sp0;
      _opponent_climb[slot] = cl0;
      _opponent_pitch[slot] = pt0;
      _opponent_head[slot].x = hx;
      _opponent_head[slot].y = hy;
      _opponent_x[slot] = x0;
      _opponent_y[slot] = y0;
      _opponent_z[slot] = z0;
      _opponent_step_want = kWants[k];
      uint8_t got_crash = _opponent_fly_and_move_asm(slot, flags, cruise);

      expect(id + 0, got_crash, rcrash);
      expect(id + 1, _opponent_speed[slot], (int16_t)rsp);
      expect(id + 2, _opponent_climb[slot], rcl);
      expect(id + 3, _opponent_pitch[slot], rpt);
      _expect_i32(id + 4, _opponent_x[slot], rx);
      _expect_i32(id + 6, _opponent_y[slot], ry);
      _expect_i32(id + 8, _opponent_z[slot], rz);
      id += 12;
    }
  }

}

static __noinline void _test_opp_fall_asm(void) {
  // 31d. _opponent_fall_asm: spinning vs non-spinning, whole vs substep,
  // parked (speed=0), and ground hit (z<=0).
  flight_step_shift = 0;
  uint16_t id = 36000u;
  for (uint8_t c = 0; c < 6; ++c) {
    uint8_t slot = c & 1;
    uint8_t bit = (uint8_t)(slot + 1);
    uint8_t whole = (c & 2) ? 1 : 0;
    uint8_t down0 = (uint8_t)((bit << 2) | (c >= 2 ? (bit << 4) : 0) | 0x01);
    int16_t sp0 = (c == 4) ? 0 : 1800;
    int32_t z0 = (c == 5) ? 100L : 50000L;
    int16_t hx0 = 200, hy0 = -160;
    int32_t x0 = 10000L, y0 = 20000L;

    // Reference.
    int16_t rhx = hx0, rhy = hy0;
    int8_t rbk = -1;
    if ((down0 & (uint8_t)(bit << 4)) && whole) {
      rhx = -hy0;
      rhy = hx0;
      rbk = 2;
    }
    int32_t rz = z0 + _ref_opp_step_delta(-192, sp0);
    int32_t rx = x0, ry = y0;
    uint8_t rdown = down0;
    uint8_t rdead = (rz <= 0 || sp0 == 0) ? 1 : 0;
    if (rdead) {
      rdown &= (uint8_t)~(bit << 2);
    } else {
      rx += _ref_opp_step_delta(rhx, sp0);
      ry += _ref_opp_step_delta(rhy, sp0);
    }

    _opponent_down = down0;
    _opponent_speed[slot] = sp0;
    _opponent_head[slot].x = hx0;
    _opponent_head[slot].y = hy0;
    _opponent_bank[slot] = -1;
    _opponent_pitch[slot] = 0;
    _opponent_x[slot] = x0;
    _opponent_y[slot] = y0;
    _opponent_z[slot] = z0;
    uint8_t got_dead = _opponent_fall_asm(slot, whole);

    expect(id + 0, got_dead, rdead);
    expect(id + 1, _opponent_down, rdown);
    expect(id + 2, _opponent_pitch[slot], 4);
    expect(id + 3, _opponent_bank[slot], rbk);
    expect(id + 4, _opponent_head[slot].x, rhx);
    expect(id + 5, _opponent_head[slot].y, rhy);
    _expect_i32(id + 6, _opponent_z[slot], rz);
    _expect_i32(id + 8, _opponent_x[slot], rx);
    _expect_i32(id + 10, _opponent_y[slot], ry);
    id += 12;
  }

}

static __noinline void _test_opp_cap_asm(void) {
  // 31e. _opponent_cap_asm: far=0..3, depth hysteresis boundary, cap_all=0/1,
  // newly-capped >2-sprite drop vs <=2-sprite preserve vs already-capped.
  static const struct {
    uint8_t far, cap_all, nearer0, capped0;
    uint16_t x0, x1;
    uint8_t lv0, c0, r0, lv1, c1, r1;
  } kCapCases[] = {
      // far=1 -> nearer=1, capped=1 (slot 0 newly capped, 2x2 -> dropped)
      {1, 0, 0, 0, 500, 1000, 3, 2, 2, 3, 2, 2},
      // far=2 -> nearer=0, capped=2 (slot 1 newly capped, 1x2 -> dropped)
      {2, 0, 1, 0, 1000, 500, 2, 1, 1, 2, 1, 2},
      // far=0, nearer=0, dn=800, dn-(dn>>3)=700: x1=700 (not < 700 -> stays 0)
      {0, 0, 0, 0, 800, 700, 2, 2, 2, 2, 1, 1},
      // far=0, nearer=0, dn=800, thresh=700: x1=699 (< 700 -> flips to 1)
      {0, 0, 0, 0, 800, 699, 2, 2, 2, 2, 2, 2},
      // far=3, nearer=1, dn=1600, thresh=1400: x0=1399 -> flips to 0, cap_all=1
      {3, 1, 1, 1, 1399, 1600, 2, 2, 2, 3, 2, 1},
      // already capped=3 -> fast exit, nothing dropped
      {0, 1, 0, 3, 400, 900, 3, 2, 2, 3, 2, 2},
  };
  uint16_t id = 36200u;
  for (uint8_t k = 0; k < sizeof(kCapCases) / sizeof(kCapCases[0]); ++k) {
    uint8_t far = kCapCases[k].far;
    uint8_t cap_all = kCapCases[k].cap_all;
    uint8_t rn = kCapCases[k].nearer0;
    uint8_t rc0 = kCapCases[k].capped0;
    uint16_t x0 = kCapCases[k].x0, x1 = kCapCases[k].x1;
    if (far == 1 || far == 2) {
      rn = far & 1;
    } else {
      uint16_t dn = rn == 0 ? x0 : x1;
      uint16_t xo = rn == 0 ? x1 : x0;
      if (xo < (uint16_t)(dn - (dn >> 3))) {
        rn ^= 1;
      }
    }
    uint8_t rcap = cap_all ? 3 : (uint8_t)((rn ^ 1) + 1);
    uint8_t rpend = 3;
    uint8_t rlv[2] = {kCapCases[k].lv0, kCapCases[k].lv1};
    uint8_t rsh[2] = {kCapCases[k].lv0, kCapCases[k].lv1};
    bool rkv[2] = {true, true};
    uint8_t cols[2] = {kCapCases[k].c0, kCapCases[k].c1};
    uint8_t rows[2] = {kCapCases[k].r0, kCapCases[k].r1};
    for (uint8_t s = 0; s < 2; ++s) {
      uint8_t bit = (uint8_t)(s + 1);
      if ((rcap & bit) && !(rc0 & bit) && rlv[s] != kPlaneLevelDot &&
          (uint8_t)(cols[s] + rows[s]) > 2) {
        rsh[s] = kPlaneLevelDot;
        rlv[s] = kPlaneLevelDot;
        rkv[s] = false;
        rpend &= (uint8_t)~bit;
      }
    }

    _opponent_nearer = kCapCases[k].nearer0;
    _opponent_capped = rc0;
    _opponent_pending = 3;
    _opponent_cs[0].x = (int16_t)x0;
    _opponent_cs[1].x = (int16_t)x1;
    for (uint8_t s = 0; s < 2; ++s) {
      _opponent_planes[s].lay.level = (s == 0 ? kCapCases[k].lv0 : kCapCases[k].lv1);
      _opponent_planes[s].lay.cols = cols[s];
      _opponent_planes[s].lay.rows = rows[s];
      _opponent_planes[s].key_valid = true;
      _opponent_shown[s].level = _opponent_planes[s].lay.level;
    }
    _opponent_cap_asm(far, cap_all);

    expect(id + 0, _opponent_nearer, rn);
    expect(id + 1, _opponent_capped, rcap);
    expect(id + 2, _opponent_pending, rpend);
    expect(id + 3, _opponent_planes[0].lay.level, rlv[0]);
    expect(id + 4, _opponent_shown[0].level, rsh[0]);
    expect(id + 5, _opponent_planes[0].key_valid ? 1 : 0, rkv[0] ? 1 : 0);
    expect(id + 6, _opponent_planes[1].lay.level, rlv[1]);
    expect(id + 7, _opponent_shown[1].level, rsh[1]);
    expect(id + 8, _opponent_planes[1].key_valid ? 1 : 0, rkv[1] ? 1 : 0);
    id += 12;
  }
}

// 32. sprites_asm.h raster-IRQ sprite programmers (terrain Frame A/B,
// enable==0 early exit, active-only 0..last copies, no-sprites, and panel
// top/bottom across all four views) against real VICE VIC-II registers
// ($D000-$D02E) and sprite pointer RAM ($EBF8-$EBFF / $EFF8-$EFFF).
static __noinline void test_sprites_irq_asm(void) {
  volatile uint8_t *const vic_p = (volatile uint8_t *)0xD000;
  volatile uint8_t *const ptr_a = (volatile uint8_t *)0xEBF8;
  volatile uint8_t *const ptr_b = (volatile uint8_t *)0xEFF8;
  uint16_t id = 38000u;

  for (uint8_t fi = 0; fi < 2; ++fi) {
    for (uint8_t s = 0; s < 8; ++s) {
      _sprites_frames[fi].pos[s * 2] = (uint8_t)(0x20 + fi * 0x40 + s * 3);
      _sprites_frames[fi].pos[s * 2 + 1] = (uint8_t)(0x50 + fi * 0x30 + s * 5);
      _sprites_frames[fi].ptr[s] = (uint8_t)(0x80 + fi * 0x20 + s);
      _sprites_frames[fi].color[s] = (uint8_t)((s + fi * 5 + 1) & 0x0F);
    }
    _sprites_frames[fi].msbx = fi ? 0x5A : 0xA5;
    _sprites_frames[fi].expand = fi ? 0x3C : 0xC3;
    _sprites_frames[fi].expand_y = fi ? 0x03 : 0x0F;
  }

  for (uint8_t fi = 0; fi < 2; ++fi) {
    mem_using_alt_buffer = (fi == 0);
    _sprites_frames[fi].enable = 0;
    _sprites_frames[fi].last = 0;
    vic_p[0x15] = 0xFF;
    vic_p[0x00] = 0x77;
    sprites_show_terrain_sprites();
    expect(id++, vic_p[0x15], 0);
    expect(id++, vic_p[0x00], 0x77);
  }

  static const uint8_t kLastCases[] = {0, 2, 7};
  for (uint8_t fi = 0; fi < 2; ++fi) {
    mem_using_alt_buffer = (fi == 0);
    volatile uint8_t *ptrs = (fi == 0) ? ptr_a : ptr_b;
    for (uint8_t li = 0; li < 3; ++li) {
      uint8_t last = kLastCases[li];
      uint8_t en = (uint8_t)((2 << last) - 1);
      _sprites_frames[fi].enable = en;
      _sprites_frames[fi].last = last;
      for (uint8_t s = 0; s < 8; ++s) {
        vic_p[s * 2] = 0xEE;
        vic_p[s * 2 + 1] = 0xDD;
        vic_p[0x27 + s] = 0x0E;
        ptrs[s] = 0xCC;
      }
      sprites_show_terrain_sprites();
      expect(id++, vic_p[0x15], en);
      expect(id++, vic_p[0x10], _sprites_frames[fi].msbx);
      expect(id++, vic_p[0x1D], _sprites_frames[fi].expand);
      expect(id++, vic_p[0x17], _sprites_frames[fi].expand_y);
      for (uint8_t s = 0; s <= last; ++s) {
        expect(id++, vic_p[s * 2], _sprites_frames[fi].pos[s * 2]);
        expect(id++, vic_p[s * 2 + 1], _sprites_frames[fi].pos[s * 2 + 1]);
        expect(id++, ptrs[s], _sprites_frames[fi].ptr[s]);
        expect(id++, vic_p[0x27 + s] & 0x0F, _sprites_frames[fi].color[s]);
      }
      for (uint8_t s = last + 1; s < 8; ++s) {
        expect(id++, vic_p[s * 2], 0xEE);
        expect(id++, ptrs[s], 0xCC);
      }
    }
  }
  mem_using_alt_buffer = false;

  vic_p[0x15] = 0xFF;
  vic_p[0x1D] = 0xFF;
  vic_p[0x17] = 0xFF;
  vic_p[0x10] = 0xFF;
  sprites_show_no_sprites();
  expect(id++, vic_p[0x15], 0);
  expect(id++, vic_p[0x1D], 0);
  expect(id++, vic_p[0x17], 0);
  expect(id++, vic_p[0x10], 0);

  for (uint8_t s = 0; s < 8; ++s) {
    _sprites_instrument_xy[s].x = (uint8_t)(0x30 + s * 7);
    _sprites_instrument_xy[s].y = (uint8_t)(0xB0 + s * 3);
    _sprites_instrument_idx[s] = (uint8_t)(0x60 + s);
    vic_p[0x27 + s] = 0x02;
  }
  view_state = VIEW_CENTER;
  vic_p[0x1D] = 0xFF;
  vic_p[0x17] = 0xFF;
  vic_p[0x10] = 0xFF;
  sprites_show_panel_top_sprites();
  expect(id++, vic_p[0x15], 0x01);
  expect(id++, vic_p[0x1D], 0);
  expect(id++, vic_p[0x17], 0);
  expect(id++, vic_p[0x10], 0);
  expect(id++, vic_p[0x27] & 0x0F, 1);
  expect(id++, vic_p[0x00], _sprites_instrument_xy[0].x);
  expect(id++, vic_p[0x01], _sprites_instrument_xy[0].y);
  expect(id++, ptr_b[0], _sprites_instrument_idx[0]);
  sprites_show_panel_bottom_sprites();
  expect(id++, vic_p[0x15], 0xFF);
  expect(id++, vic_p[0x10], 0x40);
  for (uint8_t s = 0; s < 8; ++s) {
    expect(id++, vic_p[0x27 + s] & 0x0F, 1);
    expect(id++, vic_p[s * 2], _sprites_instrument_xy[s].x);
    expect(id++, vic_p[s * 2 + 1], _sprites_instrument_xy[s].y);
    expect(id++, ptr_b[s], _sprites_instrument_idx[s]);
  }
  view_state = VIEW_LEFT;
  vic_p[0x27 + 7] = 0x02;
  sprites_show_panel_top_sprites();
  expect(id++, vic_p[0x15], 0x00);
  sprites_show_panel_bottom_sprites();
  expect(id++, vic_p[0x15], 0x80);
  expect(id++, vic_p[0x10], 0x80);
  expect(id++, vic_p[0x27 + 7] & 0x0F, 1);
  expect(id++, vic_p[14], _sprites_instrument_xy[7].x);
  expect(id++, vic_p[15], _sprites_instrument_xy[7].y);
  expect(id++, ptr_b[7], _sprites_instrument_idx[7]);
  view_state = VIEW_RIGHT;
  vic_p[0x27 + 6] = 0x02;
  sprites_show_panel_top_sprites();
  expect(id++, vic_p[0x15], 0x00);
  sprites_show_panel_bottom_sprites();
  expect(id++, vic_p[0x15], 0x40);
  expect(id++, vic_p[0x10], 0x00);
  expect(id++, vic_p[0x27 + 6] & 0x0F, 1);
  expect(id++, vic_p[12], _sprites_instrument_xy[6].x);
  expect(id++, vic_p[13], _sprites_instrument_xy[6].y);
  expect(id++, ptr_b[6], _sprites_instrument_idx[6]);
  view_state = VIEW_BACK;
  sprites_show_panel_top_sprites();
  expect(id++, vic_p[0x15], 0x00);
  sprites_show_panel_bottom_sprites();
  expect(id++, vic_p[0x15], 0x00);
  view_state = VIEW_CENTER;
}

#endif // __TARGET_TEST_PLANES__
#pragma optimize(pop)

int main(void) {
  // The KERNAL's timer interrupt keeps its clock and keyboard state in the
  // zero page the region above hands out, and main/scratch extend under BASIC
  // and KERNAL ROM. Bank both out ($01 = $35) and disable IRQs.
  __asm {
    sei;
    lda #$35;
    sta $01;
  }
  g_failures = 0;
  g_first_fail = 0;
  g_cases = 0;

#ifdef __TARGET_TEST_PLANES__
  test_planes();
  test_planes_sweep();
  test_flight_asm();
  test_panel_asm();
  test_sound_asm();
  test_opponent_step_asm();
  _test_opp_fly_asm();
  _test_opp_fall_asm();
  _test_opp_cap_asm();
  test_sprites_irq_asm();
#else
  test_poly_rounding();
  test_int8_negate_then_shift();
  test_fastmul_edges();
  test_mul_against_16bit_product();
  test_div_by_64();
  test_fastmul_short();
  test_poly_frustum_asm();
  test_poly_scan_lines2_asm();
  test_world_draw_box_points_asm();
  test_render_snap_center_asm();
  test_render_flush_span_asm();
  test_poly_trace_edges_asm();
  test_planes_split_reslide_asm();
  test_opponent_world_delta_asm();
  test_gun_asm();
  test_vec_div_and_planes_centre_asm();
  test_sprites_asm();
  test_clouds_asm();
  test_world_grid_asm();
  test_poly_project_vertices_asm();
  test_box_asm();
  test_vec_ops_asm();
  test_opponent_hot_asm();
  test_render_pull_and_fill_asm();
#endif

  // Something for vice_dump.sh's @spin to break on. Everything above has
  // landed in the globals by the time the loop is reached.
  for (;;) {
  }
}
