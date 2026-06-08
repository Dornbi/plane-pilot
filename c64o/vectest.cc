#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "benchmark.h"
#include "keys.h"
#include "print.h"
#include "vec.h"

uint8_t *mem_screen_ram = (uint8_t *)0x400;
bool mem_debug_enabled = true;

void test_mul_case(uint16_t pos, int16_t a, int16_t b, const char *label) {
  print_label(pos, label);

  bm_start();
  const int16_t r1 = vec_fastmul8p8(a, b);
  // const int16_t r1 = lmul8f8s(a, b);
  bm_end(pos + 80, SCREEN_STR("bv:"));
  print_labeled_signed_bcd(pos + 40, SCREEN_STR(" v"), r1);

  const int16_t r2 = (int16_t)(((int32_t)a * b) / 256);
  print_labeled_signed_bcd(pos + 120, SCREEN_STR(" v"), r2);
}

void test_sqr_case(uint16_t pos, int16_t a, const char *label) {
  print_label(pos, label);

  bm_start();
  const int16_t r1 = vec_fastsqr8p8(a);
  // const int16_t r1 = lmul8f8s(a, b);
  bm_end(pos + 80, SCREEN_STR("bv:"));
  print_labeled_signed_bcd(pos + 40, SCREEN_STR(" v"), r1);

  const int16_t r2 = (int16_t)(((int32_t)a * a) / 256);
  print_labeled_signed_bcd(pos + 120, SCREEN_STR(" v"), r2);
}

void test_multiply() {
  print_label(0, SCREEN_STR("fastmul"));
  test_mul_case(40, 123, -81, SCREEN_STR("123.-81"));
  test_mul_case(53, -100, -100, SCREEN_STR("-100.-100"));
  test_mul_case(66, 200, 150, SCREEN_STR("200.150"));
  test_mul_case(240, -120, 256, SCREEN_STR("-120.256"));
  test_mul_case(253, 125, -256, SCREEN_STR("125.-256"));
  test_mul_case(266, 32000, 0, SCREEN_STR("32000.0"));
  test_mul_case(440, 32000, 1, SCREEN_STR("32000.1"));
  test_mul_case(453, 1000, 1000, SCREEN_STR("1000.1000"));
  test_mul_case(466, 4096, -2048, SCREEN_STR("4096.-2048"));

  print_label(680, SCREEN_STR("fastsqr"));
  test_sqr_case(720, 900, SCREEN_STR("900..2"));
  test_sqr_case(733, -200, SCREEN_STR("-200..2  "));
  test_sqr_case(746, 2896, SCREEN_STR("2896..2  "));
}

void test_div_case(uint16_t pos, int16_t a, int16_t b, const char *label) {
  print_label(pos, label);

  bm_start();
  const int16_t r1 = vec_div8p8(a, b);
  bm_end(pos + 80, SCREEN_STR("bv:"));
  print_labeled_signed_bcd(pos + 40, SCREEN_STR(" v"), r1);

  const int16_t r2 = ((int32_t)(a) * 256) / b;
  print_labeled_signed_bcd(pos + 120, SCREEN_STR(" v"), r2);
}

void test_division() {
  print_label(0, SCREEN_STR("fastdiv"));
  test_div_case(40, 1024, -81, SCREEN_STR("1024/-81"));
  test_div_case(53, 100, 100, SCREEN_STR("100/100"));
}

void _transform_reference(const mat3_t *mat, const vec3_t *u, vec3_t *res) {
  const int32_t x = (((int32_t)mat->front.x * u->x) / 256) +
                    (((int32_t)mat->left.x * u->y) / 256) +
                    (((int32_t)mat->up.x * u->z) / 256);
  const int32_t y = (((int32_t)mat->front.y * u->x) / 256) +
                    (((int32_t)mat->left.y * u->y) / 256) +
                    (((int32_t)mat->up.y * u->z) / 256);
  const int32_t z = (((int32_t)mat->front.z * u->x) / 256) +
                    (((int32_t)mat->left.z * u->y) / 256) +
                    (((int32_t)mat->up.z * u->z) / 256);

  res->x = (int16_t)x;
  res->y = (int16_t)y;
  res->z = (int16_t)z;
}

void test_transform_case(uint16_t pos, const mat3_t *m, const vec3_t *v) {
  vec3_t res;
  bm_start();
  vec_transform(m, v, &res);
  bm_end(pos + 120, SCREEN_STR("b:"));
  print_labeled_signed_bcd(pos, SCREEN_STR("x"), res.x);
  print_labeled_signed_bcd(pos + 40, SCREEN_STR("y"), res.y);
  print_labeled_signed_bcd(pos + 80, SCREEN_STR("z"), res.z);

  bm_start();
  _transform_reference(m, v, &res);
  bm_end(pos + 130, SCREEN_STR("c:"));
  print_labeled_signed_bcd(pos + 10, SCREEN_STR("x"), res.x);
  print_labeled_signed_bcd(pos + 50, SCREEN_STR("y"), res.y);
  print_labeled_signed_bcd(pos + 90, SCREEN_STR("z"), res.z);
}

void test_transform() {
  mat3_t m;
  vec3_t v;

  print_label(0, SCREEN_STR("transform"));

  m.front = make_vector(256, 0, 0);
  m.left = make_vector(0, 256, 0);
  m.up = make_vector(0, 0, 256);
  v = make_vector(1024, 0, -128);
  test_transform_case(80, &m, &v);

  m.front = make_vector(256, 0, 0);
  m.left = make_vector(0, 256, 16);
  m.up = make_vector(0, -16, 256);
  v = make_vector(1024, 0, -128);
  test_transform_case(280, &m, &v);

  m.front = make_vector(-181, 181, 0);
  m.left = make_vector(181, -181, 0);
  m.up = make_vector(0, 0, 256);
  v = make_vector(0, 1000, -200);
  test_transform_case(480, &m, &v);
}

bool _project_reference(const vec3_t *v) {
  if (v->x <= 8) {
    return false;
  }
  if (v->y < -v->x || v->y > v->x || v->z < -v->x || v->z > v->x) {
    return false;
  }
  vec_sx = (int16_t)(((int32_t)v->y * 256) / v->x);
  vec_sy = (int16_t)(((int32_t)v->z * 256) / v->x);
  return true;
}

void test_project_case(uint16_t pos, const vec3_t *v) {
  vec_v = *v;
  bm_start();
  bool ok = vec_project();
  bm_end(pos + 120, SCREEN_STR("v "));
  print_labeled_signed_bcd(pos, SCREEN_STR("x"), vec_sx);
  print_labeled_signed_bcd(pos + 40, SCREEN_STR("y"), vec_sy);
  print_labeled_signed_bcd(pos + 80, SCREEN_STR("k"), ok);

  bm_start();
  ok = _project_reference(v);
  bm_end(pos + 130, SCREEN_STR("c "));
  print_labeled_signed_bcd(pos + 10, SCREEN_STR("x"), vec_sx);
  print_labeled_signed_bcd(pos + 50, SCREEN_STR("y"), vec_sy);
  print_labeled_signed_bcd(pos + 90, SCREEN_STR("k"), ok);
}

void test_project() {
  vec3_t v;
  print_label(0, SCREEN_STR("project"));

  v = make_vector(256, 128, -64);
  test_project_case(80, &v);

  v = make_vector(1000, -500, 200);
  test_project_case(100, &v);

  v = make_vector(100, -50, 30);
  test_project_case(280, &v);

  v = make_vector(50, 60, 20); // y > x (out of bounds)
  test_project_case(480, &v);

  v = make_vector(8, 4, 4); // x <= 8
  test_project_case(680, &v);
}

void _normalize_reference(vec3_t *v) {
  int32_t x = v->x, y = v->y, z = v->z;
  uint32_t L2 = (uint32_t)x * x + (uint32_t)y * y + (uint32_t)z * z;
  uint32_t temp;
  uint32_t res;
  uint32_t bit;
  if (L2 == 0)
    return;

  temp = L2;
  res = 0;
  bit = (uint32_t)1 << 30;

  while (bit > temp)
    bit >>= 2;

  while (bit != 0) {
    if (temp >= res + bit) {
      temp -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }

  if (res == 0)
    return;

  v->x = (int16_t)((x << 8) / (int32_t)res);
  v->y = (int16_t)((y << 8) / (int32_t)res);
  v->z = (int16_t)((z << 8) / (int32_t)res);
}

static void test_normalize_case(uint16_t pos, const vec3_t *v) {
  vec3_t u = *v;
  bm_start();
  vec_normalize(&u);
  bm_end(pos + 120, SCREEN_STR("b "));
  print_labeled_signed_bcd(pos, SCREEN_STR("x"), u.x);
  print_labeled_signed_bcd(pos + 40, SCREEN_STR("y"), u.y);
  print_labeled_signed_bcd(pos + 80, SCREEN_STR("z"), u.z);

  u = *v;
  bm_start();
  _normalize_reference(&u);
  bm_end(pos + 130, SCREEN_STR("c "));
  print_labeled_signed_bcd(pos + 10, SCREEN_STR("x"), u.x);
  print_labeled_signed_bcd(pos + 50, SCREEN_STR("y"), u.y);
  print_labeled_signed_bcd(pos + 90, SCREEN_STR("z"), u.z);
}

void _cross_reference(const vec3_t *u, const vec3_t *v, vec3_t *res) {
  int32_t x = (((int32_t)u->y * v->z) / 256) - (((int32_t)u->z * v->y) / 256);
  int32_t y = (((int32_t)u->z * v->x) / 256) - (((int32_t)u->x * v->z) / 256);
  int32_t z = (((int32_t)u->x * v->y) / 256) - (((int32_t)u->y * v->x) / 256);

  res->x = (int16_t)x;
  res->y = (int16_t)y;
  res->z = (int16_t)z;
}

void test_cross_case(uint16_t pos, const vec3_t *u, const vec3_t *v) {
  vec3_t res;

  bm_start();
  vec_cross(u, v, &res);
  bm_end(pos + 120, SCREEN_STR("b "));
  print_labeled_signed_bcd(pos, SCREEN_STR("x"), res.x);
  print_labeled_signed_bcd(pos + 40, SCREEN_STR("y"), res.y);
  print_labeled_signed_bcd(pos + 80, SCREEN_STR("z"), res.z);

  bm_start();
  _cross_reference(u, v, &res);
  bm_end(pos + 130, SCREEN_STR("c "));
  print_labeled_signed_bcd(pos + 10, SCREEN_STR("x"), res.x);
  print_labeled_signed_bcd(pos + 50, SCREEN_STR("y"), res.y);
  print_labeled_signed_bcd(pos + 90, SCREEN_STR("z"), res.z);
}

void test_vecops() {
  vec3_t u, v;
  print_label(0, SCREEN_STR("normalize"));

  u = make_vector(200, 0, 0);
  test_normalize_case(40, &u);

  u = make_vector(200, 200, 0);
  test_normalize_case(60, &u);

  u = make_vector(100, -200, 300);
  test_normalize_case(240, &u);

  u = make_vector(8, -8, 256);
  test_normalize_case(260, &u);

  print_label(600, SCREEN_STR("cross"));
  u = make_vector(256, 0, 0);
  v = make_vector(0, 256, 0);
  test_cross_case(640, &u, &v);

  u = make_vector(100, -50, 200);
  v = make_vector(256, 128, -256);
  test_cross_case(660, &u, &v);
}

void test_ortho_case(uint16_t pos, mat3_t *mat) {
  bm_start();
  vec_orthonormalize(mat);
  bm_end(pos + 120, SCREEN_STR("o "));
  print_labeled_signed_bcd(pos, SCREEN_STR("fx"), mat->front.x);
  print_labeled_signed_bcd(pos + 40, SCREEN_STR("fy"), mat->front.y);
  print_labeled_signed_bcd(pos + 80, SCREEN_STR("fz"), mat->front.z);
  print_labeled_signed_bcd(pos + 12, SCREEN_STR("lx"), mat->left.x);
  print_labeled_signed_bcd(pos + 52, SCREEN_STR("ly"), mat->left.y);
  print_labeled_signed_bcd(pos + 92, SCREEN_STR("lz"), mat->left.z);
  print_labeled_signed_bcd(pos + 24, SCREEN_STR("ux"), mat->up.x);
  print_labeled_signed_bcd(pos + 64, SCREEN_STR("uy"), mat->up.y);
  print_labeled_signed_bcd(pos + 104, SCREEN_STR("uz"), mat->up.z);
}

void test_ortho() {
  print_label(0, SCREEN_STR("ortho"));

  mat3_t mat;
  mat.front = make_vector(256, 0, 0);
  mat.left = make_vector(0, 256, 0);
  mat.up = make_vector(0, 0, 256);
  test_ortho_case(80, &mat);

  mat.front = make_vector(200, 0, 0);
  mat.left = make_vector(0, 300, 100);
  mat.up = make_vector(0, -50, 200);
  test_ortho_case(240, &mat);

  mat.front = make_vector(256, 128, -64);
  mat.left = make_vector(-128, 256, 0);
  mat.up = make_vector(64, 0, 256);
  test_ortho_case(400, &mat);

  mat.front = make_vector(250, 10, 10);
  mat.left = make_vector(0, 250, -10);
  mat.up = make_vector(-10, 0, 250);
  test_ortho_case(560, &mat);
}

int main() {
  uint8_t mode = 0;
  mem_screen_ram = (uint8_t *)0x0400;
  memset(mem_screen_ram, ' ', 1000);
  bm_init();
  mem_debug_enabled = true;

  while (1) {
    if (mode == 0) {
      test_division();
    } else if (mode == 1) {
      test_multiply();
    } else if (mode == 2) {
      test_transform();
    } else if (mode == 3) {
      test_project();
    } else if (mode == 4) {
      test_vecops();
    } else if (mode == 5) {
      test_ortho();
    }
    keyb_poll();
    if (key_pressed(KSCAN_SPACE)) {
      mode = (mode + 1) % 6;
      memset(mem_screen_ram, ' ', 1000);
    }
  };
  return 0;
}
