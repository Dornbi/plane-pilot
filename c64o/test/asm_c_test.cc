// Host test suite for the C fallback counterparts of world_asm and
// render_asm (kept in world.cc and render.cc under #ifndef __OSCAR64__):
//   - Pass 138: _world_draw_box_points (world.cc / world_asm.h)
//   - Pass 139: _render_snap_center_asm & _render_flush_span (render.cc /
//     render_asm.h)

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../chardefs.h"
#include "../color.h"
#include "../fmath.h"
#include "../gfx.h"
#include "../mem.h"
#include "../poly.h"
#include "../render.h"
#include "../render_asm.h"
#include "../roll.h"
#include "../spritedef.h"
#include "../sprites.h"
#include "../vec.h"
#include "../world.h"
#include "../world_asm.h"

// Fake screen & color RAM arrays (referenced by mem.h externs).
static uint8_t fake_screen_main[kScreenHeight * kScreenWidth];
static uint8_t fake_screen_alt[kScreenHeight * kScreenWidth];
static uint8_t fake_color[kScreenHeight * kScreenWidth];
uint8_t *kScreenRamMain = fake_screen_main;
uint8_t *kScreenRamAlt = fake_screen_alt;
uint8_t *kColorRam = fake_color;
uint8_t *mem_screen_ram = fake_screen_main;
bool mem_using_alt_buffer = false;
uint8_t *mem_screen_row_ptrs[kScreenHeight];
uint8_t *const mem_color_buffer = fake_color;
uint8_t *const mem_color_row_ptrs[kViewportHeight] = {
    fake_color + 0 * 40,  fake_color + 1 * 40,  fake_color + 2 * 40,
    fake_color + 3 * 40,  fake_color + 4 * 40,  fake_color + 5 * 40,
    fake_color + 6 * 40,  fake_color + 7 * 40,  fake_color + 8 * 40,
    fake_color + 9 * 40,  fake_color + 10 * 40, fake_color + 11 * 40,
    fake_color + 12 * 40, fake_color + 13 * 40};

// Host stand-in for vec_asm.cc's vec_project_nocull() (not called here, but
// referenced by poly.cc if linked directly). Here we link world.cc + render.cc
// directly; stub the unrelated modules they call outside these functions.
int32_t flight_eye_x = 0, flight_eye_y = 0, flight_eye_z = 0x8000;
void sprites_stack_reset() {}
bool sprites_stack_add(int16_t, int16_t, int16_t, int8_t, int8_t, uint8_t,
                       uint8_t, uint8_t, uint8_t) {
  return true;
}
void sprites_stack_commit() {}
void sprites_set_orientation() {}
void sprites_set_fin() {}
void opponent_add_sprites() {}
void gun_add_sprites() {}
void poly_draw_3d(const vec3_t *, uint8_t, uint8_t, uint8_t) {}

uint8_t roll_get_dist(int8_t px, int8_t py) {
  int8_t d = roll_ex_dy[(px + 4) & 7] - roll_ey_dx[(py + 4) & 7];
  return (uint8_t)(d < 0 ? -d : d);
}

void vec_add(vec3_t *dest, const vec3_t *src) {
  dest->x += src->x;
  dest->y += src->y;
  dest->z += src->z;
}

bool vec_project_once() {
  return vec_viewport_ahead() && vec_viewport_beside() && vec_viewport_project();
}

bool vec_viewport_ahead() { return vec_v.x >= 9; }

bool vec_viewport_beside() {
  uint16_t ux = (uint16_t)vec_v.x;
  return (uint16_t)_abs16(vec_v.y) <= (uint16_t)(ux - (ux >> 2));
}

bool vec_viewport_project() {
  uint16_t ux = (uint16_t)vec_v.x;
  if ((uint16_t)_abs16(vec_v.z) > (ux >> 2)) {
    return false;
  }
  vec_sx = vec_div8p8(vec_v.y, vec_v.x);
  vec_sy = vec_div8p8(vec_v.z, vec_v.x);
  return true;
}

// Mirror gfx.cc's gfx_draw_projected() dot rasterisation using vec_sx, vec_sy
// so _world_draw_box_points() exercises real screen/color RAM cell writes.
void gfx_draw_projected(uint8_t color) {
  int16_t px = 160 - vec_sx;
  int16_t py = 56 - vec_sy;
  if ((uint16_t)px >= 320 || (uint16_t)py >= 112) {
    return;
  }
  uint8_t row = (uint8_t)(py >> 3);
  uint8_t col = (uint8_t)(px >> 3);
  uint8_t *dst = mem_screen_row_ptrs[row] + col;
  if (*dst != kCharSolidGround) {
    return;
  }
  uint8_t bits = (uint8_t)((px >> 1) & 3) | (uint8_t)((py & 6) << 1);
  if (color == kGfxDotGround) {
    *dst = (uint8_t)(kGfxGroundPoints + bits);
  } else {
    *dst = (uint8_t)(kGfxColorPoints + bits);
    mem_color_row_ptrs[row][col] = (uint8_t)(0x08 | color);
  }
}

static void reset_buffers(uint8_t screen_fill, uint8_t color_fill) {
  memset(fake_screen_main, screen_fill, sizeof(fake_screen_main));
  memset(fake_screen_alt, screen_fill, sizeof(fake_screen_alt));
  memset(fake_color, color_fill, sizeof(fake_color));
  for (uint8_t r = 0; r < kScreenHeight; ++r) {
    mem_screen_row_ptrs[r] = fake_screen_main + r * kScreenWidth;
  }
  mem_using_alt_buffer = false;
  render_sky_rows = 0;
  _render_sky_rows = 0;
}

static void test_world_draw_box_points_c() {
  printf("Test 1: world.cc C _world_draw_box_points (Pass 138 C fallback)...\n");
  reset_buffers(kCharSolidGround, 0x55);

  memset(_world_mitch_x, 0, sizeof(_world_mitch_x));
  memset(_world_mitch_y, 0, sizeof(_world_mitch_y));
  memset(_world_mitch_z, 0, sizeof(_world_mitch_z));

  _world_vec_v = make_vector(0, 0, 0);
  // Start at 14 with count 5 -> visits indices 14, 15, 0, 1, 2.
  _world_mitch_x[14] = 8; // behind/too-close (< 9) -> culled
  _world_mitch_x[15] = 200;
  _world_mitch_y[15] = 160; // |y| = 160 > 200 - 50 -> culled by lateral tighten
  _world_mitch_x[0] = 200;
  _world_mitch_y[0] = 0;
  _world_mitch_z[0] = 51; // |z| = 51 > 200 >> 2 -> culled by vertical cone
  _world_mitch_x[1] = 256;
  _world_mitch_y[1] = 0;
  _world_mitch_z[1] = 0; // lands at center (160, 56) -> row 7, col 20
  _world_mitch_x[2] = 256;
  _world_mitch_y[2] = 64;
  _world_mitch_z[2] = 16; // vec_sx=64 -> px=96 -> col 12; vec_sy=16 -> py=40 -> row 5

  // Protect cell (5, 12) with non-ground character to test skip guard.
  fake_screen_main[5 * 40 + 12] = 42;
  _world_draw_box_points(14, 5, kGfxDotGround);
  assert(fake_screen_main[7 * 40 + 20] == kGfxGroundPoints);
  assert(fake_color[7 * 40 + 20] == 0x55); // ground dot preserves colour RAM
  assert(fake_screen_main[5 * 40 + 12] == 42);

  // Colored dot at (5, 12) once cell is ground again.
  fake_screen_main[5 * 40 + 12] = kCharSolidGround;
  _world_draw_box_points(2, 1, kColorCyan);
  assert(fake_screen_main[5 * 40 + 12] == kGfxColorPoints);
  assert(fake_color[5 * 40 + 12] == (0x08 | kColorCyan));

  // Verify start_idx wraps cleanly with & 0x0F even when start_idx >= 16.
  fake_screen_main[7 * 40 + 20] = kCharSolidGround;
  _world_draw_box_points(17, 1, kColorYellow);
  assert(fake_screen_main[7 * 40 + 20] == kGfxColorPoints);
  assert(fake_color[7 * 40 + 20] == (0x08 | kColorYellow));

  printf("  PASS\n\n");
}

static void test_render_snap_center_c() {
  printf("Test 2: render.cc C _render_snap_center_asm (Pass 139 C fallback)...\n");

  // Sweep roll_angle across levels where roll_period == 1 and > 1.
  for (uint8_t angle = 0; angle < kRollMax; ++angle) {
    roll_angle = angle;
    roll_update_state();
    render_cx_pixels = (int16_t)(160 + (angle % 7) - 3);
    render_cy_pixels = (int16_t)(56 + (angle % 5) - 2);
    render_snap_center_chars();

    // Independently check chosen lattice center sits within expected bounds.
    assert(render_alt_box == 0 || roll_period == 1);
    if (roll_period == 1) {
      assert((render_alt_shift_x == 0 && render_alt_shift_y == 4) ||
             (render_alt_shift_x == 4 && render_alt_shift_y == 0));
    }
  }

  // Specific case with roll_period == 1 where alt lattice beats main lattice.
  roll_angle = 0; // roll_period=1, roll_dx=16, roll_dy=0 -> dist(px,py)=0 unless shifted
  roll_update_state();
  // Set roll_period = 1, roll_x_is_major = true -> alt shift = (0, 4).
  // Put px=0, py=4 so main dist is |16*4| = 64 while alt (0, 0) has dist 0.
  render_px_pixels = 8;
  render_py_pixels = 12;
  _render_snap_center_asm();
  assert(render_alt_box == true);
  assert(render_alt_shift_x == 0 && render_alt_shift_y == 4);
  assert(render_cx_chars == 1 && render_cy_chars == 1);

  printf("  PASS\n\n");
}

static void test_render_flush_span_c() {
  printf("Test 3: render.cc C _render_flush_span (Pass 139 C fallback)...\n");
  reset_buffers(0xAA, 0x55);

  // Odd character (bit 0 set) or empty/inverted span is a no-op.
  _render_flush_span(1, 0, 14);
  _render_flush_span(kCharSolid11, 5, 5);
  _render_flush_span(kCharSolid11, 6, 3);
  assert(fake_screen_main[0] == 0xAA);
  assert(fake_color[0] == 0x55);

  // Sky span [0, 5) updates _render_sky_rows and writes COLOR_SKY8 on [render_sky_rows, 5).
  render_sky_rows = 2;
  _render_flush_span(kCharSolid11, 0, 5);
  assert(_render_sky_rows == 5);
  for (uint8_t y = 0; y < 5; ++y) {
    for (uint8_t x = 0; x < 40; ++x) {
      assert(fake_screen_main[y * 40 + x] == kCharSolid11);
      assert(fake_color[y * 40 + x] == (y < 2 ? 0x55 : COLOR_SKY8));
    }
  }
  assert(fake_screen_main[5 * 40] == 0xAA);

  // Ground span clamps end_row > kViewportHeight and leaves colour RAM alone.
  _render_flush_span(kCharSolidGround, 11, 20);
  for (uint8_t y = 11; y < 14; ++y) {
    for (uint8_t x = 0; x < 40; ++x) {
      assert(fake_screen_main[y * 40 + x] == kCharSolidGround);
      assert(fake_color[y * 40 + x] == 0x55);
    }
  }

  printf("  PASS\n\n");
}

int main() {
  printf("\n=== HOST C COUNTERPART TESTS FOR ASM MODULES ===\n\n");
  test_world_draw_box_points_c();
  test_render_snap_center_c();
  test_render_flush_span_c();
  printf("ALL 3 HOST C TESTS PASSED!\n");
  return 0;
}
