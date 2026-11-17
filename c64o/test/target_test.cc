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
//   17  opponent_asm.h _opponent_world_delta near/far/out & world wraparound
//   18  gun_asm.h _gun_track_all, _gun_rel_from, _gun_hits & _gun_collides
//   19  vec_asm.cc vec_div8p8 / vec_frac16 / vec_fracn & planes_asm.cc planes_centre
//   20  sprites_asm.h sprites_stack_add & sprites_stack_commit against C ref

#include <stdint.h>

// Test 7's cases + asm suites need more room than oscar64's default layout
// leaves: code and data up to $C600 (main() banks out BASIC + KERNAL ROM at
// entry), bss in $0400-$07FF, stack in $C600-$C7FF, and test buffers in
// $C800-$CFFF and $F000-$F5FF.
#pragma heapsize(0)
#pragma stacksize(0x200)
#pragma region(main, 0x0880, 0xC600, , , {code, data})
#pragma region(bssregion, 0x0400, 0x0800, , , {bss})
#pragma region(stackregion, 0xC600, 0xC800, , , {heap, stack})
// And the zero page the programs under test get from -xz: vec_asm.cc's
// routines keep their operands there.
#pragma region(zeropage, 0x80, 0x100, , , {zeropage})

#include "../planedef.h"
#include "../planes.h"
#include "../poly_asm.h"
#include "../render_asm.h"
#include "../vec.h"
#include "../world_asm.h"
#include "planes_target_cases.h"

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

static void expect(uint16_t id, int16_t got, int16_t want) {
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

// Opaque to the optimiser, so a case cannot be folded into its own answer.
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

// 6. The host suite holds planes.cc to lib/planes.py over four thousand cases,
// and the first version passed it and drew only the left edge of every
// polygon on the C64: oscar64 mis-compiled the endpoint swap in _fill_poly
// (bugs/). So a few dozen cases run here too, with every expected byte from
// the reference, rendered through a front and a back buffer as a program
// would and compared byte for byte.
static uint8_t (*const _planes_sets)[4][kPlaneBlockStride] =
    (uint8_t (*)[4][kPlaneBlockStride])0xF000;
static uint8_t _planes_dot[kPlaneBlockBytes];

static void test_planes(void) {
  const planes_view_t view = {kTargetCx0, kTargetCy0, kTargetCut1, kTargetCut2,
                              kPlaneKMax, kPlaneKnModel};
  planes_state_t state;
  planes_state_init(&state);
  planes_dot_bitmap(_planes_dot);
  uint8_t front = 0;
  for (uint8_t n = 0; n < kPlaneTargetCount; ++n) {
    const plane_target_case_t *pc = &kPlaneTargetCases[n];
    const uint16_t id = 1000 + n;
    if (pc->fresh) {
      planes_state_init(&state);
    }
    vec3_t c = make_vector(pc->c[0], pc->c[1], pc->c[2]);
    mat3_t axes;
    axes.front = make_vector(pc->axes[0][0], pc->axes[0][1], pc->axes[0][2]);
    axes.left = make_vector(pc->axes[1][0], pc->axes[1][1], pc->axes[1][2]);
    axes.up = make_vector(pc->axes[2][0], pc->axes[2][1], pc->axes[2][2]);
    uint8_t *back = _planes_sets[front ^ 1][0];
    planes_frame_t frame;
    planes_render(&state, &view, &c, &axes, back, &frame);

    expect(id, frame.hidden, pc->hidden);
    if (pc->hidden) {
      continue;
    }
    expect(id, frame.level, pc->level);
    expect(id, frame.ys, pc->ys);
    expect(id, frame.cols, pc->cols);
    expect(id, frame.rows, pc->rows);
    expect(id, frame.x, pc->x);
    expect(id, frame.y, pc->y);
    expect(id, frame.slid, pc->slid);
    expect(id, frame.clamped ? 1 : 0, pc->clamped);
    expect(id, frame.cached ? 1 : 0, pc->cached);
    const uint8_t *want = kPlaneTargetBytes + pc->bytes;
    if (frame.level == kPlaneLevelDot) {
      for (uint8_t i = 0; i < kPlaneBlockBytes; ++i) {
        expect(id, _planes_dot[i], want[i]);
      }
      continue;
    }
    if (!frame.cached) {
      front ^= 1;
    }
    for (uint8_t b = 0; b < frame.cols * frame.rows; ++b) {
      for (uint8_t i = 0; i < kPlaneBlockBytes; ++i) {
        expect(id, _planes_sets[front][b][i], want[b * kPlaneBlockBytes + i]);
      }
    }
  }
}

// 7. The same through a few hundred frames, compared by checksum rather than
// byte by byte so that they fit: attitudes and distances, an approach through
// the hysteresis, and drifts through both caches and the slide. This is what
// holds planes.cc's assembly to the reference; the host test cannot run it.
static uint8_t _sum1, _sum2;

static void _sum(uint8_t b) {
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
  planes_state_t state;
  planes_state_init(&state);
  uint8_t front = 0;
  for (uint16_t n = 0; n < kPlaneSweepCount; ++n) {
    const plane_sweep_case_t *pc = &kPlaneSweep[n];
    if (pc->fresh) {
      planes_state_init(&state);
    }
    vec3_t c = make_vector(pc->c[0], pc->c[1], pc->c[2]);
    mat3_t axes;
    axes.front = make_vector(pc->axes[0][0], pc->axes[0][1], pc->axes[0][2]);
    axes.left = make_vector(pc->axes[1][0], pc->axes[1][1], pc->axes[1][2]);
    axes.up = make_vector(pc->axes[2][0], pc->axes[2][1], pc->axes[2][2]);
    uint8_t *back = _planes_sets[front ^ 1][0];
    planes_frame_t frame;
    planes_render(&state, &view, &c, &axes, back, &frame);

    _sum1 = _sum2 = 0;
    _sum(frame.hidden);
    if (frame.hidden == kPlaneShown) {
      _sum(frame.level);
      _sum(frame.ys);
      _sum(frame.cols);
      _sum(frame.rows);
      _sum((uint8_t)frame.x);
      _sum((uint8_t)((uint16_t)frame.x >> 8));
      _sum((uint8_t)frame.y);
      _sum((uint8_t)((uint16_t)frame.y >> 8));
      _sum(frame.slid);
      _sum(frame.clamped ? 1 : 0);
      _sum(frame.cached ? 1 : 0);
      if (frame.level == kPlaneLevelDot) {
        _sum_bytes(_planes_dot, kPlaneBlockBytes);
      } else {
        if (!frame.cached) {
          front ^= 1;
        }
        for (uint8_t b = 0; b < frame.cols * frame.rows; ++b) {
          _sum_bytes(_planes_sets[front][b], kPlaneBlockBytes);
        }
      }
    }
    expect((uint16_t)(2000 + n), (int16_t)(((uint16_t)_sum2 << 8) | _sum1), (int16_t)pc->check);
  }
}

// 8. vec_div8p8 answers a divisor of 64 or -64 with a shift instead of its
// loop (the near clip's lifted vertices, poly.cc). 2a / 128 is the same
// quotient and takes the loop, so the routine is its own reference; the
// literals are the ends of the range and both sides of where it saturates.
static void test_div_by_64(void) {
  for (int16_t a = -16383; a <= 16383 - 109; a += 109) {
    expect(8, vec_div8p8(a, 64), vec_div8p8((int16_t)(a << 1), 128));
    expect(8, vec_div8p8(a, -64), vec_div8p8((int16_t)(a << 1), -128));
  }
  expect(8, vec_div8p8(0, 64), 0);
  expect(8, vec_div8p8(63, 64), 252);
  expect(8, vec_div8p8(-100, 64), -400);
  expect(8, vec_div8p8(8191, 64), 32764);
  expect(8, vec_div8p8(8192, 64), 32767);
  expect(8, vec_div8p8(-8191, -64), 32764);
  expect(8, vec_div8p8(-8192, 64), -32767);
  expect(8, vec_div8p8(32767, -64), -32767);
  expect(8, vec_div8p8(-32768, 64), -32767);
}

// 9. vec_fastmul8p8 skips the high byte's two partial products when |a| is
// below 256, and the third when |b| is too. The reference is the 32-bit
// product, truncated toward zero as the routine truncates; with one operand
// that short it always fits. Each pair goes in both orders, so the four
// product path is held to the same answers.
static const int16_t kShortA[] = {1, 3, 100, 128, 255, -1, -77, -255};

static void _fastmul_short(int16_t a, int16_t b) {
  const int16_t want = (int16_t)(((int32_t)a * b) / 256);
  expect(9, vec_fastmul8p8(a, b), want);
  expect(9, vec_fastmul8p8(b, a), want);
}

static void test_fastmul_short(void) {
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
// Place test scratch/reference screens in $F200-$F5FF (under banked-out KERNAL
// ROM) and $C800-$CFFF.
static uint8_t *const test_scr = (uint8_t *)0xF200;
static uint8_t *const test_clr = (uint8_t *)0xC800;
static uint8_t *const ref_scr = (uint8_t *)0xCA80;
static uint8_t *const ref_clr = (uint8_t *)0xCD00;

__striped uint8_t *mem_screen_row_ptrs[kScreenHeight];
__striped uint8_t *const mem_color_row_ptrs[kViewportHeight] = {
    (uint8_t *)0xC800 + 0 * 40,  (uint8_t *)0xC800 + 1 * 40,
    (uint8_t *)0xC800 + 2 * 40,  (uint8_t *)0xC800 + 3 * 40,
    (uint8_t *)0xC800 + 4 * 40,  (uint8_t *)0xC800 + 5 * 40,
    (uint8_t *)0xC800 + 6 * 40,  (uint8_t *)0xC800 + 7 * 40,
    (uint8_t *)0xC800 + 8 * 40,  (uint8_t *)0xC800 + 9 * 40,
    (uint8_t *)0xC800 + 10 * 40, (uint8_t *)0xC800 + 11 * 40,
    (uint8_t *)0xC800 + 12 * 40, (uint8_t *)0xC800 + 13 * 40};
bool mem_using_alt_buffer;

vec3_t _poly_clip3_buf[10];
uint8_t _poly_min_x[2 * kViewportHeight];
uint8_t _poly_max_x[2 * kViewportHeight];

__striped int16_t _world_mitch_x[16];
__striped int16_t _world_mitch_y[16];
__striped int16_t _world_mitch_z[16];
__zeropage vec3_t _world_vec_v;

uint8_t _render_sky_rows;
int8_t render_cx_chars;
int8_t render_cy_chars;
bool render_alt_box;
int8_t render_alt_shift_x;
int8_t render_alt_shift_y;
uint8_t render_sky_rows;
int8_t roll_dx;
int8_t roll_dy;
uint8_t roll_period;
bool roll_x_is_major;
int8_t roll_ex_dy[8];
int8_t roll_ey_dx[8];

static void _init_row_ptrs(void) {
  for (uint8_t r = 0; r < kScreenHeight; ++r) {
    mem_screen_row_ptrs[r] = test_scr + r * 40;
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
    int16_t vy = v->y;
    int16_t vz = v->z;
    uint8_t c = 0;
    if ((uint16_t)_abs16(vy) > ux) {
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
  static const int16_t kCases[][3][3] = {
      {{100, 101, 0}, {200, 205, 0}, {50, 51, 0}},     // all y > x
      {{100, -101, 0}, {200, -205, 0}, {50, -51, 0}},  // all y < -x
      {{100, 0, 26}, {200, 0, 51}, {40, 0, 11}},       // all z > x>>2
      {{100, 0, -26}, {200, 0, -51}, {40, 0, -11}},    // all z < -(x>>2)
      {{100, 0, 25}, {200, 0, 51}, {40, 0, 11}},       // z == x>>2 boundary
      {{100, 120, 0}, {100, -120, 0}, {100, 5, 0}},    // straddling x/y
      {{20000, 25000, 0}, {22000, 26000, 0}, {18000, 20000, 0}}, // > 16k
  };
  for (uint8_t c = 0; c < sizeof(kCases) / sizeof(kCases[0]); ++c) {
    for (uint8_t i = 0; i < 3; ++i) {
      _poly_clip3_buf[i] =
          make_vector(kCases[c][i][0], kCases[c][i][1], kCases[c][i][2]);
    }
    expect(10, _poly_frustum_all_out(3 * sizeof(vec3_t)) ? 1 : 0,
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

    for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
      expect(11, test_scr[i], ref_scr[i]);
      expect(11, test_clr[i], ref_clr[i]);
    }
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

    for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
      expect(12, test_scr[i], ref_scr[i]);
      expect(12, test_clr[i], ref_clr[i]);
    }
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
    render_alt_shift_x = 0;
    render_alt_shift_y = 0;
    _render_snap_center_asm();

    int8_t r_cx = 0, r_cy = 0, r_sx = 0, r_sy = 0;
    bool r_alt = false;
    _ref_snap_center(kSnapCases[c].px, kSnapCases[c].py, &r_cx, &r_cy, &r_alt,
                     &r_sx, &r_sy);
    expect(13, render_cx_chars, r_cx);
    expect(13, render_cy_chars, r_cy);
    expect(13, render_alt_box ? 1 : 0, r_alt ? 1 : 0);
    if (roll_period == 1) {
      expect(13, render_alt_shift_x, r_sx);
      expect(13, render_alt_shift_y, r_sy);
    }
  }
}

// 14. render_asm.cc _render_flush_span on $E800 (main) and $EC00 (alt).
static void test_render_flush_span_asm(void) {
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
  expect(14, _render_sky_rows, 5);
  _render_flush_span(128, 11, 20);

  // Alt buffer: single-row sky [7, 8) and [9, 12).
  mem_using_alt_buffer = true;
  _render_flush_span(kCharSolid11, 7, 8);
  expect(14, _render_sky_rows, 5); // gap: should not advance _render_sky_rows
  _render_flush_span(128, 9, 12);

  // Switch ROM off temporarily so reads from $E800/$EC00 inspect underlying RAM.
  *(volatile uint8_t *)0x01 = 0x35;
  for (uint8_t y = 0; y < kViewportHeight; ++y) {
    uint8_t want_main = (y < 5) ? kCharSolid11 : (y >= 11 ? 128 : 0xAA);
    uint8_t want_alt = (y == 7) ? kCharSolid11 : ((y >= 9 && y < 12) ? 128 : 0xAA);
    uint8_t want_clr = ((y >= 2 && y < 5) || y == 7) ? (0x08 | 6) : 0x55;
    for (uint8_t x = 0; x < 40; ++x) {
      uint16_t off = (uint16_t)y * 40 + x;
      expect(14, main_ram[off], want_main);
      expect(14, alt_ram[off], want_alt);
      expect(14, test_clr[off], want_clr);
    }
  }
  *(volatile uint8_t *)0x01 = saved_p01;
}

// 15. poly_asm.h _poly_bounds_and_clear + _poly_trace_edges against C Bresenham.
static uint8_t *const _ref_min_x = (uint8_t *)0xCF40;
static uint8_t *const _ref_max_x = (uint8_t *)0xCF60;

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

#pragma optimize(push, nooutline)
static void test_poly_trace_edges_asm(void) {
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
    expect(15, (int16_t)(((uint16_t)got_e << 8) | got_s),
           (int16_t)(((uint16_t)want_e << 8) | want_s));
    _poly_trace_edges(v, n);

    for (uint8_t i = 0; i < 2 * kViewportHeight; ++i) {
      expect(15, _poly_min_x[i], _ref_min_x[i]);
      expect(15, _poly_max_x[i], _ref_max_x[i]);
    }

    // Also verify end-to-end _poly_scan_lines2 output on the traced spans.
    _clear_test_screens(kCharSolidGround, 0x33);
    uint8_t col = (c & 1) ? 0x0D : 0;
    _poly_scan_lines2(160, col, want_s, want_e);
    _ref_scan_lines2(160, col, want_s, want_e);
    for (uint16_t i = 0; i < kViewportHeight * 40; ++i) {
      expect(15, test_scr[i], ref_scr[i]);
      expect(15, test_clr[i], ref_clr[i]);
    }
  }
}

// 16. Split planes_project + planes_reslide + planes_fill, planes_place cut
// boundaries, and full-buffer-slide empty fill on the 6502 routines.
static void test_planes_split_reslide_asm(void) {
  const planes_view_t view = {kTargetCx0, kTargetCy0, kTargetCut1, kTargetCut2,
                              kPlaneKMax, kPlaneKnModel};
  mat3_t axes;
  axes.front = make_vector(-170, 170, 88);
  axes.left = make_vector(-170, -170, 88);
  axes.up = make_vector(88, 0, 241);

  // Project high up (no slide), then drop near the bottom so reslide moves the
  // cached vertices into the slid band before planes_fill runs; must match a
  // direct planes_render from the same prior layout latches.
  static const int16_t kDist[2] = {600, 260}; // ys=1 and ys=2 silhouettes
  for (uint8_t t = 0; t < 2; ++t) {
    planes_state_t s_split, s_direct;
    planes_state_init(&s_split);
    planes_state_init(&s_direct);
    vec3_t c0 = make_vector(kDist[t], 20, 40);
    vec3_t c1 = make_vector(kDist[t], 20, -140);
    planes_frame_t f_split, f_direct;

    planes_project(&s_split, &view, &c0, &axes, &f_split);
    planes_project(&s_direct, &view, &c0, &axes, &f_direct);

    bool ok = planes_centre(&view, &c1, &f_split);
    expect(16, ok ? 1 : 0, 1);
    bool moved = planes_reslide(&s_split, &view, &f_split, kPlaneReslideAlways);
    expect(16, moved ? 1 : 0, 1);
    planes_fill(&s_split, _planes_sets[0][0]);

    planes_render(&s_direct, &view, &c1, &axes, _planes_sets[1][0], &f_direct);
    expect(16, f_split.x, f_direct.x);
    // For ys == 2 an odd reslide rounds up by 1 line, matching lay.slid.
    expect(16, f_split.slid, (uint8_t)s_split.lay.slid);
    if (s_split.lay.slid == s_direct.lay.slid) {
      for (uint8_t b = 0; b < f_direct.cols * f_direct.rows; ++b) {
        for (uint8_t i = 0; i < kPlaneBlockBytes; ++i) {
          expect(16, _planes_sets[0][b][i], _planes_sets[1][b][i]);
        }
      }
    }

    // Slide >= buffer height must clear all blocks and draw nothing.
    s_split.lay.slid = 200;
    for (uint8_t b = 0; b < 4; ++b) {
      for (uint8_t i = 0; i < kPlaneBlockStride; ++i) {
        _planes_sets[0][b][i] = 0xAA;
      }
    }
    planes_fill(&s_split, _planes_sets[0][0]);
    uint8_t blocks = (uint8_t)(s_split.lay.cols * s_split.lay.rows);
    for (uint8_t b = 0; b < blocks; ++b) {
      for (uint8_t i = 0; i < kPlaneBlockStride; ++i) {
        expect(16, _planes_sets[0][b][i], 0);
      }
    }
  }

  // Dot-tier exact cut1 boundary: y == cut1 is shown, y == cut1 + 1 is hidden.
  planes_layout_t dot_lay = {kPlaneLevelDot, 1, 1, 1, 0, 0, 0};
  planes_frame_t df;
  df.hidden = kPlaneShown;
  df.cx = 160;
  df.cy = (int16_t)(kTargetCut1 + kPlaneDotY);
  expect(16, planes_place(&dot_lay, &view, &df), 0);
  expect(16, df.hidden, kPlaneShown);
  df.cy = (int16_t)(kTargetCut1 + kPlaneDotY + 1);
  expect(16, planes_place(&dot_lay, &view, &df), 0);
  expect(16, df.hidden, kPlaneBelowCut);
}

// 17. opponent_asm.h _opponent_world_delta: near/far/out classification,
// sub-half-metre quarter shifts, x 7-bit wraparound, y 8-bit wraparound, and
// slot 0 vs slot 1 indexing.
static int32_t _test_eye_x, _test_eye_y, _test_eye_z;
int32_t *const kFlightEye[3] = {&_test_eye_x, &_test_eye_y, &_test_eye_z};
static int32_t _opponent_xyz[6];
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

static void test_opponent_world_delta_asm(void) {
  static const int32_t kEyes[3][3] = {
      {0x00102000L, 0x00204000L, 0x00041000L},
      {0x007E0080L, 0x00FD80C0L, 0x00060040L}, // near x=128 & y=256 wrap edges
      {0x00018040L, 0x00020080L, 0x000800C0L},
  };
  static const int32_t kOffs[][3] = {
      {0, 0, 0},
      {0x000001C0L, -0x00000280L, 0x00000040L},  // fractional quarters
      {0x000FFFFFL, -0x000F0000L, 0x00071240L},  // max near boundaries [-15, 15]
      {0x00100000L, 0x00000180L, -0x00010040L},  // far boundary s = +16
      {0x00000100L, -0x000F0001L, 0x00000000L},  // far boundary s = -16
      {0x001FFFFFL, -0x001F0000L, 0x00104080L},  // max far boundaries [-31, 31]
      {0x00200000L, 0, 0},                       // out boundary s = +32
      {0, -0x001F0001L, 0},                      // out boundary s = -32
      {0, 0, 0x00250000L},                       // z out
      {-0x007C0040L, -0x00FB0080L, 0x00010040L}, // wrap across x=128 & y=256 (+4, +5)
      {0x007B00C0L, 0x00EE0040L, -0x00010080L},  // wrap across x=128 & y=256 (-5, -18)
  };
  for (uint8_t e = 0; e < 3; ++e) {
    _test_eye_x = kEyes[e][0];
    _test_eye_y = kEyes[e][1];
    _test_eye_z = kEyes[e][2];
    for (uint8_t c = 0; c < sizeof(kOffs) / sizeof(kOffs[0]); ++c) {
      for (uint8_t slot = 0; slot < 2; ++slot) {
        uint8_t other = slot ^ 1;
        _opponent_xyz[other] = 0x55AA55AAL;
        _opponent_xyz[other + 2] = -0x33445566L;
        _opponent_xyz[other + 4] = 0x11223344L;
        _opponent_xyz[slot] = (_test_eye_x + kOffs[c][0]) & 0x007FFFFFL;
        _opponent_xyz[slot + 2] = (_test_eye_y + kOffs[c][1]) & 0x00FFFFFFL;
        _opponent_xyz[slot + 4] = _test_eye_z + kOffs[c][2];

        vec3_t want_v = {0, 0, 0};
        uint8_t want_r = _ref_world_delta(slot, &want_v);
        vec_v = make_vector(1234, -5678, 9999);
        uint8_t got_r = _opponent_world_delta(slot);
        expect(17, got_r, want_r);
        if (want_r != kOpponentOut) {
          expect(17, vec_v.x, want_v.x);
          expect(17, vec_v.y, want_v.y);
          expect(17, vec_v.z, want_v.z);
        }
      }
    }
  }
}

// 18. gun_asm.h: _gun_track_all, _gun_rel_from, _gun_hits, and _gun_collides
// against 16-bit C references on the emulated 6510.
static const uint8_t kOpponentMax = 2;
static const uint8_t kRoundMax = 8;
static const int16_t kGunCollide = 192;
uint8_t flight_step_shift;
static int32_t _gun_eye[3];
static int32_t _gun_p[3];
static __striped int16_t _gun_t[3];
static __striped int16_t _gun_d[3];
static __striped int16_t _gun_q[3];
static __striped int16_t _gun_moved[3];
static __striped int16_t _gun_prev[6];
static __striped int16_t _round_p[24];
static __striped int16_t _round_v[24];
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

static void test_gun_asm(void) {
  // _gun_track_all: sub-quantum preservation, positive/negative borrow across
  // bit 16, exact d >> 5 in _gun_moved, and _gun_t zeroed.
  static const int32_t kSteps[4][3] = {
      {0x0000001FL, -0x00000001L, 0x00000020L},
      {0x00000F15L, -0x00000E33L, 0x000001FFL},
      {-0x00001205L, 0x0000185AL, -0x00000420L},
      {0x00000083L, -0x0000009EL, 0x00000000L},
  };
  int32_t ref_eye[3] = {0x0010FFE8L, 0x00200010L, 0x00058004L};
  for (uint8_t c = 0; c < 3; ++c) {
    _gun_eye[c] = ref_eye[c];
    *kFlightEye[c] = ref_eye[c];
  }
  for (uint8_t s = 0; s < 4; ++s) {
    int16_t want_m[3];
    for (uint8_t c = 0; c < 3; ++c) {
      *kFlightEye[c] += kSteps[s][c];
      int16_t d = (int16_t)((uint16_t)(*kFlightEye[c]) - (uint16_t)ref_eye[c]);
      want_m[c] = d >> 5;
      ref_eye[c] += (int16_t)(d & ~31);
      _gun_t[c] = 1234;
    }
    _gun_track_all();
    for (uint8_t c = 0; c < 3; ++c) {
      expect(18, (int16_t)_gun_eye[c], (int16_t)ref_eye[c]);
      expect(18, (int16_t)(_gun_eye[c] >> 16), (int16_t)(ref_eye[c] >> 16));
      expect(18, _gun_moved[c], want_m[c]);
      expect(18, _gun_t[c], 0);
    }
  }

  // _gun_rel_from(0) and _gun_rel_from(2): wraparound on x/y and [-15, 14].
  _gun_eye[0] = 0x007E0040L;
  _gun_eye[1] = 0x00FC8020L;
  _gun_eye[2] = 0x00040100L;
  static const int32_t kRelOff[][3] = {
      {0, 0, 0},
      {0x000EFFFDL, -0x000F0000L, -0x00040100L}, // max in-range +14, -15
      {0x000F0000L, 0, 0},                        // +15 out
      {0, -0x000F0001L, 0},                       // -16 out
      {0, 0, 0x000F0000L},                        // z out
      {-0x007A0020L, -0x00F60040L, -0x00010060L}, // wrap across x=128 & y=256
      {0x00790060L, 0x00F50080L, 0x000200A0L},    // negative wrap
  };
  for (uint8_t k = 0; k < sizeof(kRelOff) / sizeof(kRelOff[0]); ++k) {
    _gun_p[0] = (_gun_eye[0] + kRelOff[k][0]) & 0x007FFFFFL;
    _gun_p[1] = (_gun_eye[1] + kRelOff[k][1]) & 0x00FFFFFFL;
    _gun_p[2] = _gun_eye[2] + kRelOff[k][2];
    int16_t want_t[3] = {77, 88, 99};
    bool want_ok = _ref_gun_rel(0, &want_t[0]) && _ref_gun_rel(1, &want_t[1]) &&
                   _ref_gun_rel(2, &want_t[2]);
    bool got_ok = _gun_rel3_to() != 0;
    expect(18, got_ok, want_ok);
    if (want_ok) {
      expect(18, _gun_t[0], want_t[0]);
      expect(18, _gun_t[1], want_t[1]);
      expect(18, _gun_t[2], want_t[2]);
    }
    int16_t want_z = 33;
    bool want_zok = _ref_gun_rel(2, &want_z);
    _gun_t[0] = 11;
    _gun_t[1] = 22;
    bool got_zok = _gun_rel2() != 0;
    expect(18, got_zok, want_zok);
    expect(18, _gun_t[0], 11);
    expect(18, _gun_t[1], 22);
    if (want_zok) {
      expect(18, _gun_t[2], want_z);
    }
  }

  // _gun_hits: substeps, reach boundary, box boundary, and flight_step_shift.
  static const int16_t kHitCases[][7] = {
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
      _gun_t[0] = kHitCases[c][0];
      _gun_t[1] = kHitCases[c][1];
      _gun_t[2] = kHitCases[c][2];
      _round_p[r] = 40;
      _round_p[r + 8] = -20;
      _round_p[r + 16] = 10;
      _round_v[r] = kHitCases[c][3];
      _round_v[r + 8] = kHitCases[c][4];
      _round_v[r + 16] = kHitCases[c][5];
      int16_t box = kHitCases[c][6];
      expect(18, _gun_hits(r, box), _ref_gun_hits(r, box));
    }
  }
  flight_step_shift = 0;

  // _gun_collides: +-192 vs +-193 on now and mid, prev=false vs true, slots 0/1.
  static const int16_t kColSeq[][3] = {
      {400, 0, 0},      // outside
      {-384, 192, -192}, // now outside (-384), mid = (+8, +96, -96) -> mid hit if prev!
      {500, 0, 0},      // outside
      {-114, 0, 0},     // mid = +193 -> outside! now = -114 -> now hit!
      {500, 0, 0},
      {-113, 0, 0},     // mid = +193 -> outside; Wait: (500 + -114)>>1 = 193, (500 + -116)>>1 = 192
      {500, 385, 0},
      {-500, -1, 0},    // mid = (0, +192, 0) -> mid hit!
      {500, 387, 0},
      {-500, -1, 0},    // mid = (0, +193, 0) -> miss!
      {192, -192, 192}, // exact boundary now hit
      {193, 0, 0},      // +193 outside (unless mid hits: (192+193)>>1 = 192 -> mid hits if prev!)
  };
  int16_t ref_prev[6] = {0, 0, 0, 0, 0, 0};
  for (uint8_t i = 0; i < 6; ++i) {
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
    expect(18, got, want);
    for (uint8_t i = 0; i < 6; ++i) {
      expect(18, _gun_prev[i], ref_prev[i]);
    }
  }
}

// 19. vec_asm.cc restoring division (vec_div8p8, vec_frac16, vec_fracn across
// 8-bit and 16-bit divisors, zero/equal/integer/saturating boundaries) and
// planes_asm.cc planes_centre (behind-camera, cone, scale clamp, and diameter).
static uint16_t _ref_uabs16(int16_t v) {
  return v < 0 ? (uint16_t)(-(uint16_t)v) : (uint16_t)v;
}

static uint16_t _ref_fracn(int16_t a, int16_t b, uint8_t bits) {
  uint16_t ua = _ref_uabs16(a);
  uint16_t ub = _ref_uabs16(b);
  uint16_t ones = bits == 16 ? 0xFFFFu : (uint16_t)(((uint16_t)1 << bits) - 1);
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

static int16_t _ref_div8p8(int16_t a, int16_t b) {
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
      expect(19, got_d, want_d);
      uint16_t want_f = _ref_fracn(a, b, 16);
      uint16_t got_f = vec_frac16(a, b);
      expect(19, (int16_t)got_f, (int16_t)want_f);
    }
  }
  // Exact literals on tricky division corners.
  expect(19, vec_div8p8(0, 0), 0);
  expect(19, vec_div8p8(5, 0), 32767);
  expect(19, vec_div8p8(-5, 0), -32767);
  expect(19, vec_div8p8(-32768, -32768), 256);
  expect(19, vec_div8p8(8191, 65), 32259);
  expect(19, vec_div8p8(8320, 65), 32767);
  expect(19, vec_div8p8(-32768, 256), -32767);
  expect(19, (int16_t)vec_frac16(32767, -32768), (int16_t)0xFFFEu);
  expect(19, (int16_t)vec_frac16(-32768, -32768), (int16_t)0xFFFFu);

  // vec_fracn across odd bit counts and saturation.
  static const uint8_t kBits[] = {1, 7, 8, 9, 12, 16};
  static const int16_t kFnPairs[][2] = {
      {0, 65},    {1, 3},      {2, 3},       {64, 128},    {64, 129},
      {128, 129}, {-254, 255}, {255, -256},  {300, 1000},  {32767, -32768},
      {200, 200}, {500, 200},  {-32768, -32768}, {10, 0},  {0, 0},
  };
  for (uint8_t bi = 0; bi < sizeof(kBits) / sizeof(kBits[0]); ++bi) {
    uint8_t bits = kBits[bi];
    for (uint8_t pi = 0; pi < sizeof(kFnPairs) / sizeof(kFnPairs[0]); ++pi) {
      int16_t a = kFnPairs[pi][0], b = kFnPairs[pi][1];
      uint16_t want_fn = _ref_fracn(a, b, bits);
      uint16_t got_fn = vec_fracn(a, b, bits);
      expect(19, (int16_t)got_fn, (int16_t)want_fn);
    }
  }

  // planes_centre across views, behind-camera, cone edges, and clamp edges.
  static const planes_view_t kViews[] = {
      {160, 56, 89, 68, 150, 64},
      {160, 80, 140, 119, 88, 64},
      {120, 40, 200, 200, 255, 200},
  };
  static const int16_t kCases[][3] = {
      {-32768, 0, 0},    {-1, 0, 0},       {0, 0, 0},        {64, 0, 0},
      {65, 0, 0},        {65, 64, -64},    {65, -64, 64},    {65, 65, 0},
      {65, -65, 0},      {65, 0, 65},      {65, 0, -65},     {65, -32768, 0},
      {65, 10, -32768},  {128, -127, 127}, {129, 128, -128}, {218, -50, 30},
      {219, 50, -30},    {372, -371, 200}, {373, 100, -372}, {400, 0, 0},
      {401, 1, -1},      {2500, -1999, 800}, {32767, -32766, 32766},
  };
  for (uint8_t vi = 0; vi < sizeof(kViews) / sizeof(kViews[0]); ++vi) {
    for (uint8_t ci = 0; ci < sizeof(kCases) / sizeof(kCases[0]); ++ci) {
      vec3_t c = make_vector(kCases[ci][0], kCases[ci][1], kCases[ci][2]);
      planes_frame_t got, want;
      got.hidden = want.hidden = 99;
      got.cached = want.cached = true;
      got.slid = want.slid = 77;
      bool got_ok = planes_centre(&kViews[vi], &c, &got);
      bool want_ok = _ref_planes_centre(&kViews[vi], &c, &want);
      expect(19, got_ok ? 1 : 0, want_ok ? 1 : 0);
      expect(19, got.hidden, want.hidden);
      expect(19, got.cached ? 1 : 0, want.cached ? 1 : 0);
      expect(19, got.slid, want.slid);
      if (want_ok) {
        expect(19, got.clamped ? 1 : 0, want.clamped ? 1 : 0);
        expect(19, got.k, want.k);
        expect(19, got.d, want.d);
        expect(19, got.cx, want.cx);
        expect(19, got.cy, want.cy);
      }
    }
  }
}
#pragma optimize(pop)

// 20. sprites_asm.h sprites_stack_add and sprites_stack_commit against 16-bit C
// references across culls, dither lattice, message box, sort/overflow, 1x1..2x2
// slots, unused parking / past-last preservation, orientation mark, and fin.
uint16_t msg_span_x0;
uint16_t msg_span_x1;
static bool _test_msg_on;
__noinline bool msg_active(void) { return _test_msg_on; }
#include "../sprites_asm.h"
const sprite_meta_t kSpriteDefSun = {12, 10, 95};
const sprite_meta_t kSpriteDefOrient = {12, 10, 80};

static sprite_cand_t *const _ref_cand = (sprite_cand_t *)0xCF00;
static sprite_frame_t *const _ref_frames = (sprite_frame_t *)0xCF60;
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
    expect(20, got[i], want[i]);
  }
}

static void test_sprites_asm(void) {
  struct AddCase {
    int16_t d, x, y;
    int8_t px, py;
    uint8_t b1, b2, col, fl;
  };
  static const AddCase kCulls[] = {
      // Exact left/right/top/bottom boundaries for 1x1, ExpandX, 1x2, 2x2.
      {100, -13, 50, 12, 10, 95, 0xFF, 7, 0},   // vx == -1 -> out
      {100, -12, 50, 12, 10, 95, 0xFF, 7, 0},   // vx == 0, w == 24 -> out
      {100, -11, 50, 12, 10, 95, 0xFF, 7, 0},   // vx == 1, w == 24 -> in
      {100, 0, 50, 12, 10, 81, 0xFF, 1, 0x01},  // x == -24, w == 48 -> in (vx == 0)
      {100, -1, 50, 12, 10, 81, 0xFF, 1, 0x01}, // x == -25, w == 48 -> out
      {100, 331, 50, 12, 10, 95, 0xFF, 7, 0},   // x == 319 -> in
      {100, 332, 50, 12, 10, 95, 0xFF, 7, 0},   // x == 320 -> out
      {100, 160, -11, 12, 10, 95, 0xFF, 7, 0},  // y + 20 == -1 -> out
      {100, 160, -10, 12, 10, 95, 0xFF, 7, 0},  // y + 20 == 0 -> in
      {100, 160, 99, 12, 10, 95, 0xFF, 7, 0},   // y == 89, y + 20 == 109 -> in
      {100, 160, 100, 12, 10, 95, 0xFF, 7, 0},  // y == 90, y + 20 == 110 -> out
      {100, 160, 88, 12, 20, 85, 86, 1, 0x01},  // 1x2: y == 68, +41 == 109 -> in
      {100, 160, 89, 12, 20, 85, 86, 1, 0x01},  // 1x2: y == 69, +41 == 110 -> out
      {100, 100, 26, 0, 0, 69, 71, 12, 0x0D},   // 2x2 ExpandY: +83 == 109 -> in
      {100, 100, 27, 0, 0, 69, 71, 12, 0x0D},   // 2x2 ExpandY: +83 == 110 -> out
      // Dither parity across odd/even & negative top-lefts.
      {100, 161, 55, 12, 10, 82, 0xFF, 1, 0x03},
      {100, 162, 55, 12, 10, 82, 0xFF, 1, 0x03},
      {100, 163, 56, 12, 10, 82, 0xFF, 1, 0x03},
      {100, 5, -9, 12, 10, 82, 0xFF, 1, 0x03},
      // Message box edges when msg_on ([100, 220), row 0 vy < 58).
      {100, 88, 17, 12, 10, 95, 0xFF, 7, 0},    // vx=100, +24==124==x0 -> in
      {100, 89, 17, 12, 10, 95, 0xFF, 7, 0},    // vx=101, +24==125>x0 -> hidden
      {100, 232, 17, 12, 10, 95, 0xFF, 7, 0},   // vx=244==x1 -> in
      {100, 231, 17, 12, 10, 95, 0xFF, 7, 0},   // vx=243<x1 -> hidden
      {100, 160, 18, 12, 10, 95, 0xFF, 7, 0},   // y=8 -> vy=58 -> in
      // Extreme 16-bit signed wrap edges.
      {100, -32768, 50, 12, 10, 95, 0xFF, 7, 0},
      {100, 32767, 50, 0, 0, 95, 0xFF, 7, 0},
      {100, 160, -32768, 0, 10, 95, 0xFF, 7, 0},
      {100, 160, 32767, 0, 0, 95, 0xFF, 7, 0},
  };
  msg_span_x0 = 100;
  msg_span_x1 = 220;
  for (uint8_t m = 0; m < 2; ++m) {
    _test_msg_on = (m != 0);
    for (uint8_t i = 0; i < sizeof(kCulls) / sizeof(kCulls[0]); ++i) {
      _sprites_cand_end = 0;
      _ref_cand_end = 0;
      const AddCase *ac = &kCulls[i];
      bool got = sprites_stack_add(ac->d, ac->x, ac->y, ac->px, ac->py, ac->b1,
                                   ac->b2, ac->col, ac->fl);
      bool want = _ref_stack_add(ac->d, ac->x, ac->y, ac->px, ac->py, ac->b1,
                                 ac->b2, ac->col, ac->fl);
      expect(20, got ? 1 : 0, want ? 1 : 0);
      expect(20, _sprites_cand_end, _ref_cand_end);
      if (want) {
        const uint8_t *gb = (const uint8_t *)_sprites_cand;
        const uint8_t *wb = (const uint8_t *)_ref_cand;
        for (uint8_t b = 0; b < kSpriteCandSize; ++b) {
          expect(20, gb[b], wb[b]);
        }
      }
    }
  }
  _test_msg_on = false;

  // Multi-frame commit tests: empty, 9-item overflow + ties, mixed 2x2 + 1x2 +
  // msbx split, orientation mark, tail fin, and alt-buffer sentinel check.
  static const AddCase kMixed[] = {
      {0x7FFF, 300, 40, 12, 10, 95, 0xFF, 7, 0},      // sun with msbx
      {450, 230, 12, 0, 0, 69, 71, 11, 0x0D},         // 2x2 plane crossing 256
      {300, 161, 45, 12, 20, 85, 86, 1, 0x03},        // 1x2 dithered cloud
      {300, 80, 30, 12, 10, 82, 0xFF, 1, 0x03},       // tie at depth 300
      {120, 40, 20, 12, 10, 90, 0xFF, 2, 0},          // close tracer
  };
  uint8_t *raw_got = (uint8_t *)_sprites_frames;
  uint8_t *raw_want = (uint8_t *)_ref_frames;
  for (uint8_t b = 0; b < sizeof(_sprites_frames); ++b) {
    raw_got[b] = 0xAA;
    raw_want[b] = 0xAA;
  }
  for (uint8_t mode = 0; mode < 4; ++mode) {
    mem_using_alt_buffer = (mode & 1) != 0;
    _sprites_orient_off = (mode == 1) ? 0 : 0xFF;
    _sprites_fin_off = (mode == 2) ? 0 : 0xFF;
    _sprites_cand_end = 0;
    _ref_cand_end = 0;
    if (mode == 3) {
      // 10 descending/interleaved items to exercise full-stack eviction & rejection.
      for (uint8_t i = 0; i < 10; ++i) {
        int16_t d = (i == 9) ? 999 : (int16_t)(900 - i * 80);
        bool g = sprites_stack_add(d, (int16_t)(20 + i * 28), 35, 12, 10,
                                   (uint8_t)(40 + i), 0xFF, (uint8_t)(i & 15), 0);
        bool w = _ref_stack_add(d, (int16_t)(20 + i * 28), 35, 12, 10,
                                (uint8_t)(40 + i), 0xFF, (uint8_t)(i & 15), 0);
        expect(20, g ? 1 : 0, w ? 1 : 0);
      }
    } else if (mode != 0) {
      for (uint8_t i = 0; i < sizeof(kMixed) / sizeof(kMixed[0]); ++i) {
        const AddCase *ac = &kMixed[i];
        bool g = sprites_stack_add(ac->d, ac->x, ac->y, ac->px, ac->py, ac->b1,
                                   ac->b2, ac->col, ac->fl);
        bool w = _ref_stack_add(ac->d, ac->x, ac->y, ac->px, ac->py, ac->b1,
                                ac->b2, ac->col, ac->fl);
        expect(20, g ? 1 : 0, w ? 1 : 0);
      }
    }
    sprites_stack_commit();
    _ref_stack_commit();
    _check_frames_eq();
  }
  mem_using_alt_buffer = false;
}

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

  test_poly_rounding();
  test_int8_negate_then_shift();
  test_fastmul_edges();
  test_mul_against_16bit_product();
  test_planes();
  test_planes_sweep();
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

  // Something for vice_dump.sh's @spin to break on. Everything above has
  // landed in the globals by the time the loop is reached.
  for (;;) {
  }
}
