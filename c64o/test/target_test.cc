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

#include <stdint.h>

// Test 7's cases need more room than oscar64's default layout leaves: one
// region up to the BASIC ROM, no heap (nothing here allocates) and a 1 KB
// stack rather than 4.
#pragma heapsize(0)
#pragma stacksize(0x400)
#pragma region(main, 0x0880, 0xA000, , , {code, data, bss})
#pragma region(stackregion, 0xC000, 0xC400, , , {heap, stack})
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
static uint8_t _planes_sets[2][4][kPlaneBlockStride];
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
// Place test scratch/reference screens in the free 4 KB RAM block at $C000
// ($C000-$C3FF holds stack/heap; $C400-$CF2F holds the test buffers cleanly).
static uint8_t *const test_scr = (uint8_t *)0xC400;
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

int main(void) {
  // The KERNAL's timer interrupt keeps its clock and keyboard state in the
  // zero page the region above hands out. Nothing here needs it.
  __asm { sei }
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

  // Something for vice_dump.sh's @spin to break on. Everything above has
  // landed in the globals by the time the loop is reached.
  for (;;) {
  }
}
