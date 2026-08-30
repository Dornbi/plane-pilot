// What poly_draw_3d's projection does to a polygon, against the same
// geometry projected in double precision.
//
// This exists because the clippers used to be wrong in a way no unit test
// could see: every routine returned a plausible number, and the polygon still
// came out as a wedge running to a corner of the screen. The measure that
// catches it is the shape itself - rasterize both answers on the 80x28
// sub-pixel grid the filler works in and count the cells that differ.
//
// The failure was concentrated where a polygon straddles the near plane,
// which is to say at takeoff and landing height, so that is the sweep. The
// thresholds below are what the fixed code measures with headroom, not what
// the geometry deserves: one sub-pixel of error is inherent (the 2d vertices
// are integers) and a long edge off by one fills a lot of cells.
//
// poly.cc is included rather than linked: its buffers and helpers are static,
// and the test wants the projection, not the four exported bytes.
//
// What this suite cannot see: anything that only goes wrong in 16 bits. Here
// `int` is 32 bits, so an expression that overflows int16 on the C64 quietly
// fits, and both sides of the comparison below get the same right answer.
// The rounding in _project_vertices was exactly that bug, and it took a
// screenshot from x64sc to find. See docs/emulator.md.

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "int16.h"

#include "../mem.h"
#include "../vec.h"

// The target-only surroundings, as plain arrays. Nothing here is read by the
// projection; poly.cc's filler needs them to exist.
static uint8_t fake_screen[kScreenHeight * kScreenWidth];
static uint8_t fake_color[kScreenHeight * kScreenWidth];
uint8_t *kScreenRamMain = fake_screen;
uint8_t *kScreenRamAlt = fake_screen;
uint8_t *kColorRam = fake_color;
uint8_t *mem_screen_ram = fake_screen;
uint8_t *mem_screen_row_ptrs[kScreenHeight];
uint8_t *const mem_color_buffer = fake_color;
uint8_t *const mem_color_row_ptrs[kViewportHeight] = {
    fake_color + 0,   fake_color + 40,  fake_color + 80,  fake_color + 120,
    fake_color + 160, fake_color + 200, fake_color + 240, fake_color + 280,
    fake_color + 320, fake_color + 360, fake_color + 400, fake_color + 440,
    fake_color + 480, fake_color + 520};

// vec_asm.cc is 6502 only. This is the C half of it, copied, and it is the
// whole of vec_project_nocull.
bool vec_project_nocull() {
  if (vec_v.x < 8) {
    return false;
  }
  vec_sx = vec_div8p8(vec_v.y, vec_v.x);
  vec_sy = vec_div8p8(vec_v.z, vec_v.x);
  return true;
}

#include "../poly.cc"

// ---------------------------------------------------------------------------
// The reference: the same pipeline in double precision.

struct dvec3 {
  double x, y, z;
};
struct dpt {
  double x, y;
};

static const uint8_t kMaxRefVerts = 16;

struct dpoly {
  dpt v[kMaxRefVerts];
  uint8_t n;
};

static void _ref_clip_near(const dvec3 *in, uint8_t n, dvec3 *out,
                           uint8_t *num_out) {
  *num_out = 0;
  for (uint8_t i = 0; i < n; ++i) {
    const dvec3 &prev = in[(i + n - 1) % n];
    const dvec3 &curr = in[i];
    bool pi = prev.x >= 8, ci = curr.x >= 8;
    if (ci != pi) {
      double t = (8.0 - prev.x) / (curr.x - prev.x);
      out[(*num_out)++] = {8.0, prev.y + t * (curr.y - prev.y),
                           prev.z + t * (curr.z - prev.z)};
    }
    if (ci) {
      out[(*num_out)++] = curr;
    }
  }
}

static void _ref_clip_edge(dpoly *p, uint8_t axis, double limit,
                           bool keep_greater) {
  dpoly out;
  out.n = 0;
  for (uint8_t i = 0; i < p->n; ++i) {
    dpt prev = p->v[(i + p->n - 1) % p->n];
    dpt curr = p->v[i];
    double pa = axis ? prev.y : prev.x;
    double ca = axis ? curr.y : curr.x;
    bool pi = keep_greater ? pa >= limit : pa <= limit;
    bool ci = keep_greater ? ca >= limit : ca <= limit;
    if (ci != pi) {
      double t = (limit - pa) / (ca - pa);
      dpt cut;
      if (axis == 0) {
        cut.x = limit;
        cut.y = prev.y + t * (curr.y - prev.y);
      } else {
        cut.y = limit;
        cut.x = prev.x + t * (curr.x - prev.x);
      }
      out.v[out.n++] = cut;
    }
    if (ci) {
      out.v[out.n++] = curr;
    }
  }
  *p = out;
}

static void _ref_project(const dvec3 *in, uint8_t n, dpoly *out) {
  dvec3 near_clipped[kMaxRefVerts];
  uint8_t nc = 0;
  _ref_clip_near(in, n, near_clipped, &nc);
  out->n = 0;
  if (nc < 3) {
    return;
  }
  for (uint8_t i = 0; i < nc; ++i) {
    out->v[out->n].x = 40.0 - (near_clipped[i].y * 256.0 / near_clipped[i].x) / 4.0;
    out->v[out->n].y = 14.0 - (near_clipped[i].z * 256.0 / near_clipped[i].x) / 4.0;
    ++out->n;
  }
  _ref_clip_edge(out, 0, 0, true);
  _ref_clip_edge(out, 0, kViewportWidth * 2 - 1, false);
  _ref_clip_edge(out, 1, 0, true);
  _ref_clip_edge(out, 1, kViewportHeight * 2 - 1, false);
  if (out->n < 3) {
    out->n = 0;
  }
}

// Coverage of the sub-pixel grid, by the centre of each cell.
static void _raster(const dpt *poly, uint8_t n, uint8_t *cov) {
  for (int i = 0; i < 80 * 28; ++i) {
    cov[i] = 0;
  }
  if (n < 3) {
    return;
  }
  for (int sy = 0; sy < 28; ++sy) {
    for (int sx = 0; sx < 80; ++sx) {
      double px = sx + 0.5, py = sy + 0.5;
      bool in = false;
      for (uint8_t i = 0, j = n - 1; i < n; j = i++) {
        if ((poly[i].y > py) != (poly[j].y > py)) {
          double at = poly[i].x + (py - poly[i].y) * (poly[j].x - poly[i].x) /
                                      (poly[j].y - poly[i].y);
          if (px < at) {
            in = !in;
          }
        }
      }
      cov[sy * 80 + sx] = in;
    }
  }
}

// Sub-pixels the integer pipeline fills and the exact one does not, or the
// other way round.
static int _wrong_sub_pixels(const vec3_t *verts, uint8_t n) {
  static vertex_t got[kPolyMax2dVertices];
  uint8_t gn = _project_vertices(verts, n, got);
  dpt gp[kPolyMax2dVertices];
  for (uint8_t i = 0; i < gn; ++i) {
    gp[i].x = got[i].x;
    gp[i].y = got[i].y;
  }

  dvec3 ex[kPolyMaxVertices];
  for (uint8_t i = 0; i < n; ++i) {
    ex[i] = {(double)verts[i].x, (double)verts[i].y, (double)verts[i].z};
  }
  dpoly want;
  _ref_project(ex, n, &want);

  static uint8_t ca[80 * 28], cb[80 * 28];
  _raster(gp, gn, ca);
  _raster(want.v, want.n, cb);
  int diff = 0;
  for (int i = 0; i < 80 * 28; ++i) {
    diff += (ca[i] != cb[i]);
  }
  return diff;
}

// ---------------------------------------------------------------------------
// Scenes. A camera pose, and the runway quad of world.cc built against it.

struct pose_t {
  double yaw, pitch, roll;
};

static int16_t _r16(double v) { return (int16_t)lround(v); }

static void _build_cam(mat3_t *m, pose_t p) {
  double cy = cos(p.yaw), sy = sin(p.yaw);
  double cp = cos(p.pitch), sp = sin(p.pitch);
  double cr = cos(p.roll), sr = sin(p.roll);
  double lx = -sy, ly = cy, lz = 0;
  double ux = -cy * sp, uy = -sy * sp, uz = cp;
  m->front = make_vector(_r16(cy * cp * 256), _r16(sy * cp * 256), _r16(sp * 256));
  m->left = make_vector(_r16((lx * cr + ux * sr) * 256),
                        _r16((ly * cr + uy * sr) * 256),
                        _r16((lz * cr + uz * sr) * 256));
  m->up = make_vector(_r16((ux * cr - lx * sr) * 256),
                      _r16((uy * cr - ly * sr) * 256),
                      _r16((uz * cr - lz * sr) * 256));
}

// world.cc's _world_split_vec, in the shape the runway corners need.
static void _split_vec(vec3_t v, vec3_t d9[9]) {
  int16_t *vp = (int16_t *)&v;
  for (uint8_t i = 0; i < 3; ++i) {
    int16_t val = vp[i];
    int16_t hlf = val >> 1;
    ((int16_t *)&d9[4])[i] = 0;
    ((int16_t *)&d9[6])[i] = val;
    ((int16_t *)&d9[2])[i] = -val;
    ((int16_t *)&d9[5])[i] = hlf;
    ((int16_t *)&d9[3])[i] = -hlf;
    // i16() on the four that can leave 16 bits: on the 6510 these wrap, and a
    // fixture that wraps differently is not the same fixture. Note that the
    // narrowing warning flags the first of these and not the second, though
    // the hazard is identical - see narrowing.baseline.
    ((int16_t *)&d9[1])[i] = i16(-val - hlf);
    ((int16_t *)&d9[7])[i] = i16(val + hlf);
    ((int16_t *)&d9[8])[i] = i16(val << 1);
    ((int16_t *)&d9[0])[i] = i16(-i16(val << 1));
  }
}

// From world_map.cc: the runway is a long thin quad, a field covers most of
// a cell. The field is the one that broke in flight - a big polygon has the
// large coordinates, and it is the one that fills the screen when it goes
// wrong.
static const uint8_t kRwyX[4] = {0, 8, 8, 0};
static const uint8_t kRwyY[4] = {4, 4, 3, 3};
static const uint8_t kFieldX[4] = {0, 4, 8, 2};
static const uint8_t kFieldY[4] = {2, 0, 5, 8};

static void _obj_verts(const mat3_t *cam, int16_t ox, int16_t oy, int16_t oz,
                       const uint8_t *vx, const uint8_t *vy, vec3_t out[4]) {
  vec3_t dx9[9], dy9[9];
  _split_vec(make_vector(cam->front.x, cam->left.x, cam->up.x), dx9);
  _split_vec(make_vector(cam->front.y, cam->left.y, cam->up.y), dy9);
  vec3_t world = make_vector(ox, oy, oz);
  vec3_t base;
  vec_transform_inv(cam, &world, &base);
  for (uint8_t i = 0; i < 4; ++i) {
    vec3_t v = base;
    const vec3_t &dx = dx9[vx[i]];
    const vec3_t &dy = dy9[vy[i]];
    v.x += dx.x + dy.x;
    v.y += dx.y + dy.y;
    v.z += dx.z + dy.z;
    out[i] = v;
  }
}

static void _runway_verts(const mat3_t *cam, int16_t ox, int16_t oy, int16_t oz,
                          vec3_t out[4]) {
  _obj_verts(cam, ox, oy, oz, kRwyX, kRwyY, out);
}

static void _field_verts(const mat3_t *cam, int16_t ox, int16_t oy, int16_t oz,
                         vec3_t out[4]) {
  _obj_verts(cam, ox, oy, oz, kFieldX, kFieldY, out);
}

// ---------------------------------------------------------------------------

// 1. The pose that used to collapse. A polygon covering most of the ground
// came out as a thin wedge running into a corner: 1266 of the 2240 sub-pixels
// wrong, more than half the viewport.
static void test_the_wedge() {
  printf("Running test_the_wedge...\n");

  mat3_t cam;
  _build_cam(&cam, {210 * M_PI / 180, -15 * M_PI / 180, 15 * M_PI / 180});
  vec3_t verts[4];
  _runway_verts(&cam, -256, 0, -4, verts);

  int wrong = _wrong_sub_pixels(verts, 4);
  printf("  wrong sub-pixels: %d (was 1266)\n", wrong);
  // 43 as it stands: a polygon this size has a long perimeter, and an edge
  // that is one sub-pixel out fills a lot of cells along it.
  assert(wrong < 80);

  printf("  PASS\n\n");
}

// 2. Two vertices behind the camera and one intersection landing on screen:
// the near-plane parameter has to be worth more than a part in 256, and the
// intersection has to keep a fraction of a world unit.
static void test_near_plane_intersection_lands_on_screen() {
  printf("Running test_near_plane_intersection_lands_on_screen...\n");

  // The same edge as above, isolated: (643,-328,264) is in front, and
  // (-213,108,-92) is behind. The crossing sits at y = -4.6, z = -0.09, which
  // projects to (76.5, 14.7) - the middle of the screen. Rounded to whole
  // units it would be (8,-7,2), which projects to (96,-2): off screen twice
  // over.
  static vec3_t edge[3] = {
      {643, -328, 264}, {-213, 108, -92}, {705, -216, 252}};
  static vertex_t got[kPolyMax2dVertices];
  uint8_t n = _project_vertices(edge, 3, got);
  assert(n >= 3);

  // The clipped vertex must project within a sub-pixel or two of (76.5, 14.7)
  // rather than off the right edge.
  bool found = false;
  for (uint8_t i = 0; i < n; ++i) {
    if (abs(got[i].x - 76) <= 2 && abs(got[i].y - 15) <= 2) {
      found = true;
    }
  }
  if (!found) {
    printf("  projected:");
    for (uint8_t i = 0; i < n; ++i) {
      printf(" (%d,%d)", got[i].x, got[i].y);
    }
    printf("\n");
  }
  assert(found);

  printf("  PASS\n\n");
}

// 3. The scale on the near-plane intersection has to stay inside sixteen
// bits. It did not: the test that guards it bounded prev and the deltas but
// not curr, and the intersection lies between prev and curr, so it could
// reach |prev| + |delta| and wrap. A wrapped intersection projects to the far
// side of the screen, and the polygon it belongs to fills the viewport - the
// exact artifact the scaling exists to prevent, at high altitude where the
// grid coordinates are largest. Found in flight on mission 4, not by this
// suite, which is why the sweeps below now reach that far out.
static void test_near_clip_scaling_never_overflows() {
  printf("Running test_near_clip_scaling_never_overflows...\n");

  int checked = 0, worst = 0;
  for (int py = -4000; py <= 4000; py += 137) {
    for (int dy = -4000; dy <= 4000; dy += 211) {
      for (int pz = -3000; pz <= 3000; pz += 1500) {
        vec3_t in[3] = {{1000, (int16_t)py, (int16_t)pz},
                        {-1000, (int16_t)(py + dy), (int16_t)(pz + dy / 2)},
                        {1200, (int16_t)py, (int16_t)(pz + 900)}};
        vec3_t out[8];
        uint8_t n = _clip_near(in, 3, out);

        // The same two crossings in double precision.
        dvec3 ex[3];
        for (uint8_t i = 0; i < 3; ++i) {
          ex[i] = {(double)in[i].x, (double)in[i].y, (double)in[i].z};
        }
        dvec3 ref[8];
        uint8_t rn = 0;
        _ref_clip_near(ex, 3, ref, &rn);

        for (uint8_t i = 0; i < n; ++i) {
          if (out[i].x != 64) {
            continue; // not a scaled intersection
          }
          // Match it to whichever exact intersection is nearer, and require
          // that one to be close. A wrap lands thousands of units away.
          double best = 1e9;
          for (uint8_t j = 0; j < rn; ++j) {
            if (ref[j].x != 8.0) {
              continue;
            }
            double dyy = out[i].y - ref[j].y * 8.0;
            double dzz = out[i].z - ref[j].z * 8.0;
            double d = fabs(dyy) > fabs(dzz) ? fabs(dyy) : fabs(dzz);
            if (d < best) {
              best = d;
            }
          }
          ++checked;
          if (best > worst) {
            worst = (int)best;
          }
        }
      }
    }
  }
  printf("  %d scaled intersections, worst off by %d eighths of a unit\n",
         checked, worst);
  assert(checked > 1000);
  assert(worst <= 8); // one whole unit; a wrap was tens of thousands

  printf("  PASS\n\n");
}

// 4. The sweep. Low over the ground, every attitude, the polygon at and
// around the aircraft: this is the takeoff and landing case, and the one
// that was broken. Nothing may be grossly wrong, and the average has to stay
// where the fix put it.
static void test_low_sweep_has_no_gross_errors() {
  printf("Running test_low_sweep_has_no_gross_errors...\n");

  long total = 0;
  int trials = 0, worst = 0, over_32 = 0;
  for (int yawi = 0; yawi < 24; yawi += 2) {
    for (int rolli = -6; rolli <= 6; rolli += 2) {
      for (int pitchi = -3; pitchi <= 3; ++pitchi) {
        for (int alt = 4; alt <= 128; alt *= 2) {
          for (int along = -2; along <= 2; ++along) {
            mat3_t cam;
            _build_cam(&cam, {yawi * M_PI / 12, pitchi * M_PI / 24,
                              rolli * M_PI / 12});
            vec3_t verts[4];
            _runway_verts(&cam, (int16_t)(along * 256), 0, (int16_t)-alt,
                          verts);
            int wrong = _wrong_sub_pixels(verts, 4);
            total += wrong;
            ++trials;
            if (wrong > worst) {
              worst = wrong;
            }
            if (wrong > 32) {
              ++over_32;
            }
          }
        }
      }
    }
  }
  double mean = (double)total / trials;
  printf("  %d poses, mean %.2f wrong sub-pixels, worst %d, over 32: %d\n",
         trials, mean, worst, over_32);
  // Before the fix, over the same poses: mean 28.8, worst 1266, and 5.7% of
  // them more than 128 sub-pixels out. Now: mean 7.0, worst 342, and none
  // over 128 except a handful.
  //
  // The tail that is left is not in the clippers. An exact 32-bit (a * b) / c
  // in place of vec_frac16 and vec_mulfrac measures 7.64 against 7.75 on the
  // full sweep, with the same worst case; rounding the projection's /4
  // symmetrically instead of with a shift is worth more (7.15), and 206
  // bytes. Under that sits a long edge one sub-pixel out, which is the floor
  // of a design whose 2d vertices are integers.
  assert(worst < 512);
  assert(mean < 12.0);
  assert(over_32 * 10 < trials); // under 10% of poses

  printf("  PASS\n\n");
}

// 5. The ordinary case has to stay ordinary: a polygon well in front of the
// camera never touches the new path, and must still land within the
// sub-pixel the 2d vertices are rounded to.
static void test_distant_polygons_stay_accurate() {
  printf("Running test_distant_polygons_stay_accurate...\n");

  long total = 0;
  int trials = 0, worst = 0;
  for (int yawi = 0; yawi < 24; yawi += 2) {
    for (int rolli = -4; rolli <= 4; rolli += 2) {
      for (int dist = 1; dist <= 8; ++dist) {
        mat3_t cam;
        _build_cam(&cam, {yawi * M_PI / 12, 0, rolli * M_PI / 12});
        vec3_t verts[4];
        _runway_verts(&cam, (int16_t)(dist * 512), 0, -60, verts);
        int16_t min_x = 32767;
        for (uint8_t i = 0; i < 4; ++i) {
          if (verts[i].x < min_x) {
            min_x = verts[i].x;
          }
        }
        if (min_x < 400) {
          continue; // near-plane cases are test 3's business
        }
        int wrong = _wrong_sub_pixels(verts, 4);
        total += wrong;
        ++trials;
        if (wrong > worst) {
          worst = wrong;
        }
      }
    }
  }
  printf("  %d poses, mean %.2f wrong sub-pixels, worst %d\n", trials,
         (double)total / trials, worst);
  assert(worst < 40);
  assert((double)total / trials < 2.0);

  printf("  PASS\n\n");
}

// 6. The same shape check, at the altitude and grid radius mission 4 flies
// at: a kilometre up, where world.cc widens the grid to radius 4 and cell
// corners reach four thousand units from the eye. This is where the wrapped
// intersection showed up in flight, and the low sweep never went near it.
static void test_high_altitude_sweep() {
  printf("Running test_high_altitude_sweep...\n");

  long total = 0;
  int trials = 0, worst = 0, over_128 = 0;
  for (int yawi = 0; yawi < 24; yawi += 2) {
    for (int rolli = -6; rolli <= 6; rolli += 2) {
      for (int pitchi = -3; pitchi <= 3; ++pitchi) {
        for (int cell = -4; cell <= 4; ++cell) {
          for (int alt = 128; alt <= 1024; alt *= 2) {
            mat3_t cam;
            _build_cam(&cam, {yawi * M_PI / 12, pitchi * M_PI / 24,
                              rolli * M_PI / 12});
            vec3_t verts[4];
            _field_verts(&cam, (int16_t)(cell * 1024), (int16_t)(cell * 512),
                         (int16_t)-alt, verts);
            int wrong = _wrong_sub_pixels(verts, 4);
            total += wrong;
            ++trials;
            if (wrong > worst) {
              worst = wrong;
            }
            if (wrong > 128) {
              ++over_128;
            }
          }
        }
      }
    }
  }
  printf("  %d poses, mean %.2f wrong sub-pixels, worst %d, over 128: %d\n",
         trials, (double)total / trials, worst, over_128);
  assert(worst < 512);
  assert(over_128 == 0);

  printf("  PASS\n\n");
}

int main() {
  printf("\n=== POLYGON PROJECTION SUITE (6 TESTS) ===\n\n");
  for (uint8_t i = 0; i < kScreenHeight; ++i) {
    mem_screen_row_ptrs[i] = fake_screen + i * kScreenWidth;
  }

  test_the_wedge();
  test_near_plane_intersection_lands_on_screen();
  test_near_clip_scaling_never_overflows();
  test_low_sweep_has_no_gross_errors();
  test_distant_polygons_stay_accurate();
  test_high_altitude_sweep();

  printf("ALL 6 TESTS PASSED SUCCESSFULLY!\n");
  return 0;
}
