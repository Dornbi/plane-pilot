#include "planes_asm.h"

#include <stddef.h>
#include <stdint.h>

#include "planedef.h"

// The assembly half of planes.cc (planes_asm.h): centre projection and scale,
// axis products, fuselage stations/tube/octagon, bounding box, layout
// hysteresis, placement, resliding, vertex cache key, and polygon fill.
// Axis-pair byte multiplies inline the quarter-square high-byte lookup
// directly (skipping zero components), other byte multiplies go through
// vec_mul8, and centre divisions share c->x through vec_frac8.

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

#ifdef __PLANES_PROFILE__
#include "benchmark.h"
static const char kProfCentre[] = SCREEN_STR("centre ");
static const char kProfFill[] = SCREEN_STR("fill ");
#endif

extern const uint8_t vec_sqr_lo[];
extern const uint8_t vec_sqr_hi[];

// Struct-field and constant assertions for the assembly routines below.
static_assert(kPlaneLevelDot == 0 && kPlaneLevel1x == 1 && kPlaneLevelX == 2,
              "planes_asm level constants");
static_assert(kPlaneReslideDeep == 0 && kPlaneReslideOff == 1 &&
                  kPlaneReslideAlways == 2,
              "planes_asm reslide constants");
static_assert(kPlaneShown == 0 && kPlaneBehindCamera == 1 &&
                  kPlaneOutOfRange == 2 && kPlaneBelowCut == 3 &&
                  kPlaneDotX == 12 && kPlaneDotY == 19,
              "planes_asm hidden and dot constants");
static_assert((kPlaneMaxRadius << 1) == 136, "planes_asm 2 * kPlaneMaxRadius");
static_assert(kPlaneDDot == 3 && kPlaneDDotDown == 2 &&
                  kPlaneD1x == 21 && kPlaneD1xDown == 18,
              "planes_asm level ladder thresholds");
static_assert(kPlaneYLimit == 41 && kPlaneYHold == 35 &&
                  kPlaneColsHold == 20 && kPlaneRowsHold == 17,
              "planes_asm layout thresholds");
// kPlanePolyStart is {0, 6, 12, 16} (wing 6, tailplane 6, fin 4, body 6, oct 8).
static_assert(kPlaneVertCount == 16 && kPlaneBodyCount == 3 &&
                  kPlaneVertMax == 30,
              "planes_asm polygon counts");
static_assert(offsetof(vec3_t, x) == 0 && offsetof(vec3_t, y) == 2 &&
                  offsetof(vec3_t, z) == 4 && offsetof(mat3_t, front) == 0 &&
                  offsetof(mat3_t, left) == 6 && offsetof(mat3_t, up) == 12,
              "vec3_t/mat3_t layout changed");
static_assert(offsetof(planes_layout_t, level) == 0 &&
                  offsetof(planes_layout_t, ys) == 1 &&
                  offsetof(planes_layout_t, cols) == 2 &&
                  offsetof(planes_layout_t, rows) == 3 &&
                  offsetof(planes_layout_t, ox) == 4 &&
                  offsetof(planes_layout_t, oy) == 5 &&
                  offsetof(planes_layout_t, slid) == 6 &&
                  sizeof(planes_layout_t) == 8,
              "planes_layout_t layout changed");
static_assert(offsetof(planes_state_t, lay) == 0 &&
                  offsetof(planes_state_t, key_valid) == 8 &&
                  offsetof(planes_state_t, key_count) == 9 &&
                  offsetof(planes_state_t, key_band) == 10 &&
                  offsetof(planes_state_t, key_x) == 11 &&
                  offsetof(planes_state_t, key_y) == 41 &&
                  offsetof(planes_state_t, k) == 71 &&
                  offsetof(planes_state_t, axes) == 72,
              "planes_state_t layout changed");
static_assert(offsetof(planes_view_t, cx0) == 0 &&
                  offsetof(planes_view_t, cy0) == 2 &&
                  offsetof(planes_view_t, cut1) == 4 &&
                  offsetof(planes_view_t, cut2) == 6 &&
                  offsetof(planes_view_t, kmax) == 8 &&
                  offsetof(planes_view_t, kn) == 9,
              "planes_view_t layout changed");
static_assert(offsetof(planes_frame_t, hidden) == 0 &&
                  offsetof(planes_frame_t, level) == 1 &&
                  offsetof(planes_frame_t, xs) == 2 &&
                  offsetof(planes_frame_t, ys) == 3 &&
                  offsetof(planes_frame_t, cols) == 4 &&
                  offsetof(planes_frame_t, rows) == 5 &&
                  offsetof(planes_frame_t, x) == 6 &&
                  offsetof(planes_frame_t, y) == 8 &&
                  offsetof(planes_frame_t, slid) == 10 &&
                  offsetof(planes_frame_t, clamped) == 11 &&
                  offsetof(planes_frame_t, cached) == 12 &&
                  offsetof(planes_frame_t, k) == 13 &&
                  offsetof(planes_frame_t, d) == 14 &&
                  offsetof(planes_frame_t, cx) == 15 &&
                  offsetof(planes_frame_t, cy) == 17,
              "planes_frame_t layout changed");

// ---------------------------------------------------------------------------
// Products.

// One axis's pairs, for _planes_axis_products(): the loop bounds, and each
// screen component's magnitude split into a low byte and a 256, with a mask
// that is $00 for a negative component and $FF for a positive one.
// fpilot keeps these per-call statics, and the fill's below, in the screen's
// dead rows (mem.h tscratch), as planes.cc does its arrays.
#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
static uint8_t _ap, _ap_end;
static uint8_t _ay_lo, _ay_hi, _ay_m, _az_lo, _az_hi, _az_m;
static uint8_t _mi;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

// For p in _ap .. _ap_end - 1, with v = _planes_pv[p]: r = hi(lo * v), plus v
// if the component reached 256, is trunc(|c| * v / 256), and s = (r ^ m) - m
// is -r for a positive component and r for a negative -- the negated product.
// Quarter-square high-byte multiplies are inlined with zero-byte fast-skips
// (cmp vec_sqr_lo, x sets the borrow for sbc vec_sqr_hi, x without storing the
// low byte).
static void _pair_products(void) {
  // clang-format off
  __asm {
        lda _ay_lo;
        sta vec_proj_tmp1;
        lda _az_lo;
        sta vec_proj_recip;
        ldy _ap;
    L_pair:
        lda _planes_pv, y;
        sta vec_mul8_b;

        lda vec_proj_tmp1;
        beq L_x_hi;
        sec;
        sbc vec_mul8_b;
        bcs L_x_dpos;
        eor #$ff;
        adc #1;
    L_x_dpos:
        tax;
        lda vec_proj_tmp1;
        clc;
        adc vec_mul8_b;
        tay;
        bcs L_x_shi;
        lda vec_sqr_lo, y;
        cmp vec_sqr_lo, x;
        lda vec_sqr_hi, y;
        sbc vec_sqr_hi, x;
        jmp L_x_hi;
    L_x_shi:
        lda vec_sqr_lo + 256, y;
        cmp vec_sqr_lo, x;
        lda vec_sqr_hi + 256, y;
        sbc vec_sqr_hi, x;
    L_x_hi:
        ldx _ay_hi;
        beq L_x_lo;
        clc;
        adc vec_mul8_b;
    L_x_lo:
        ldy _ap;
        eor _ay_m;
        sec;
        sbc _ay_m;
        sta _planes_tx + 16, y;
        eor #$ff;
        clc;
        adc #1;
        sta _planes_tx, y;

        lda vec_proj_recip;
        beq L_y_hi;
        sec;
        sbc vec_mul8_b;
        bcs L_y_dpos;
        eor #$ff;
        adc #1;
    L_y_dpos:
        tax;
        lda vec_proj_recip;
        clc;
        adc vec_mul8_b;
        tay;
        bcs L_y_shi;
        lda vec_sqr_lo, y;
        cmp vec_sqr_lo, x;
        lda vec_sqr_hi, y;
        sbc vec_sqr_hi, x;
        jmp L_y_hi;
    L_y_shi:
        lda vec_sqr_lo + 256, y;
        cmp vec_sqr_lo, x;
        lda vec_sqr_hi + 256, y;
        sbc vec_sqr_hi, x;
    L_y_hi:
        ldx _az_hi;
        beq L_y_lo;
        clc;
        adc vec_mul8_b;
    L_y_lo:
        ldy _ap;
        eor _az_m;
        sec;
        sbc _az_m;
        sta _planes_ty + 16, y;
        eor #$ff;
        clc;
        adc #1;
        sta _planes_ty, y;

        iny;
        sty _ap;
        cpy _ap_end;
        beq L_pair_done;
        jmp L_pair;
    L_pair_done:
  }
  // clang-format on
}

static_assert(kPlaneMagCount == 16, "planes_asm.h sizes _planes_px by hand");

// _planes_mags(): sixteen products on vec_mul8, k in vec_mul8_b throughout,
// which vec_mul8 only reads. A magnitude doubled still fits a byte (the
// largest is 66), and (hi + 1) >> 1 takes the carry into bit 7 for hi 255.
static void _mags(void) {
  // clang-format off
  __asm {
        ldy #15;
    L_mag:
        sty _mi;
        lda kPlaneMags, y;
        asl;
        jsr vec_mul8;
        clc;
        adc #1;
        ror;
        ldy _mi;
        sta _planes_px, y;
        dey;
        bpl L_mag;

        // Each pair's magnitude times k, pairs 15 .. 1.
        ldy #15;
    L_pair_mag:
        ldx kPlanePairMag, y;
        lda _planes_px, x;
        sta _planes_pv, y;
        dey;
        bne L_pair_mag;
  }
  // clang-format on
}

void _planes_mags(uint8_t k) {
  vec_mul8_b = k;
  _mags();
}

// ---------------------------------------------------------------------------
// The flat surfaces' vertices, and the bounding box.

static_assert(kPlaneVertCount == 16, "_planes_flat() counts 15 .. 0");

// Each term's product, summed, plus 128 - which is an eor of bit 7.
void _planes_flat(void) {
  // clang-format off
  __asm {
        ldx #15;
    L_flat:
        ldy kPlaneVertFore, x;
        lda _planes_tx, y;
        ldy kPlaneVertLeft, x;
        clc;
        adc _planes_tx, y;
        ldy kPlaneVertUp, x;
        clc;
        adc _planes_tx, y;
        eor #$80;
        sta _planes_vx, x;
        ldy kPlaneVertFore, x;
        lda _planes_ty, y;
        ldy kPlaneVertLeft, x;
        clc;
        adc _planes_ty, y;
        ldy kPlaneVertUp, x;
        clc;
        adc _planes_ty, y;
        eor #$80;
        sta _planes_vy, x;
        dex;
        bpl L_flat;
  }
  // clang-format on
}

#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
uint8_t _planes_box_x0, _planes_box_x1, _planes_box_y0, _planes_box_y1;
static uint8_t _bn;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

void _planes_box(uint8_t count) {
  _bn = count;
  // clang-format off
  __asm {
        lda _planes_vx;
        sta _planes_box_x0;
        sta _planes_box_x1;
        lda _planes_vy;
        sta _planes_box_y0;
        sta _planes_box_y1;
        ldy #1;
        cpy _bn;
        beq L_box_done;
    L_box:
        lda _planes_vx, y;
        cmp _planes_box_x0;
        bcs L_x_not_min;
        sta _planes_box_x0;
    L_x_not_min:
        cmp _planes_box_x1;
        bcc L_x_not_max;
        sta _planes_box_x1;
    L_x_not_max:
        lda _planes_vy, y;
        cmp _planes_box_y0;
        bcs L_y_not_min;
        sta _planes_box_y0;
    L_y_not_min:
        cmp _planes_box_y1;
        bcc L_y_not_max;
        sta _planes_box_y1;
    L_y_not_max:
        iny;
        cpy _bn;
        bne L_box;
    L_box_done:
  }
  // clang-format on
}

// ---------------------------------------------------------------------------
// The fuselage stations, tube outline, and end-on octagon.

static_assert(kPlaneBodyCount == 3, "_tube() walks three stations");

#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
// The offsets r * u per station, and the loop's station and vertex indices;
// |ux| and |uy| and their signs share _ay_* and _az_* above.
static int8_t _tox[3], _toy[3];
static uint8_t _ti, _tat;
static uint8_t _fwidest, _fmaxr, _fnorm;
static int8_t _ffx, _ffy;
// Shared 10-byte per-step scratch: holds the octagon's 10 corner offsets in
// _planes_fuselage(), then internal temporaries in planes_centre(),
// _planes_layout(), and planes_place() (which never overlap).
static uint8_t _foff[10];
uint8_t _planes_lbx, _planes_lby, _planes_lxs, _planes_lys;
bool _planes_lcached;
static uint8_t _kdiff;
static int16_t _place_need;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

// smul(u, r) per station: q = (|u| * r + 128) >> 8, which is the reference's
// ((p >> 7) + 1) >> 1 - the high byte plus bit 7 of the low one - and just r
// when |u| is 256; negated when u is.
static void _tube(void) {
  // clang-format off
  __asm {
        ldy #2;
    L_station:
        sty _ti;
        lda _planes_sr, y;
        sta vec_mul8_b;

        lda _ay_hi;
        beq L_ox_mul;
        lda vec_mul8_b;
        jmp L_ox_sign;
    L_ox_mul:
        lda _ay_lo;
        jsr vec_mul8;
        asl vec_mul8_lo;
        adc #0;
    L_ox_sign:
        eor _ay_m;
        sec;
        sbc _ay_m;
        ldy _ti;
        sta _tox, y;

        lda _az_hi;
        beq L_oy_mul;
        lda vec_mul8_b;
        jmp L_oy_sign;
    L_oy_mul:
        lda _az_lo;
        jsr vec_mul8;
        asl vec_mul8_lo;
        adc #0;
    L_oy_sign:
        eor _az_m;
        sec;
        sbc _az_m;
        ldy _ti;
        sta _toy, y;
        dey;
        bpl L_station;

        // Stations 0 .. 2 plus the offset, then 2 .. 0 minus it.
        ldx _tat;
        ldy #0;
    L_plus:
        lda _planes_sx, y;
        clc;
        adc _tox, y;
        sta _planes_vx, x;
        lda _planes_sy, y;
        clc;
        adc _toy, y;
        sta _planes_vy, x;
        inx;
        iny;
        cpy #3;
        bne L_plus;
        ldy #2;
    L_minus:
        lda _planes_sx, y;
        sec;
        sbc _tox, y;
        sta _planes_vx, x;
        lda _planes_sy, y;
        sec;
        sbc _toy, y;
        sta _planes_vy, x;
        inx;
        dey;
        bpl L_minus;
  }
  // clang-format on
}

static __noinline void _unpack_yz(int16_t y, int16_t z) {
  // clang-format off
  __asm {
        ldy #0;
        lda y;
        ldx y + 1;
        bpl L_yz_yp;
        dey;
        eor #$ff;
        clc;
        adc #1;
        pha;
        txa;
        eor #$ff;
        adc #0;
        tax;
        pla;
    L_yz_yp:
        sty _ay_m;
        sta _ay_lo;
        stx _ay_hi;

        ldy #0;
        lda z;
        ldx z + 1;
        bpl L_yz_zp;
        dey;
        eor #$ff;
        clc;
        adc #1;
        pha;
        txa;
        eor #$ff;
        adc #0;
        tax;
        pla;
    L_yz_zp:
        sty _az_m;
        sta _az_lo;
        stx _az_hi;
  }
  // clang-format on
}

void _planes_tube(int16_t ux, int16_t uy, uint8_t at) {
  _unpack_yz(ux, uy);
  _tat = at;
  _tube();
}

void _planes_axis_products(const mat3_t *axes) {
  _planes_tx[0] = 0;
  _planes_ty[0] = 0;
  _ap = 1;
  const vec3_t *a = &axes->front;
  for (uint8_t i = 1; i < 4; ++i, ++a) {
    _unpack_yz(a->y, a->z);
    _ap_end = kPlaneAxisPairs[i];
    _pair_products();
  }
}

void _planes_fuselage(void) {
  // clang-format off
  __asm {
        lda #16;
        sta _planes_vert_count;
        ldx #0;
        stx _fwidest;
        stx _fmaxr;
    L_fus_st:
        ldy kPlaneBodyFore, x;
        lda _planes_tx, y;
        eor #$80;
        sta _planes_sx, x;
        lda _planes_ty, y;
        eor #$80;
        sta _planes_sy, x;
        ldy kPlaneBodyRadius, x;
        lda _planes_px, y;
        sta _planes_sr, x;
        cmp _fmaxr;
        bcc L_fus_nw;
        beq L_fus_nw;
        sta _fmaxr;
        stx _fwidest;
    L_fus_nw:
        inx;
        cpx #3;
        bne L_fus_st;

        // fx = sx[0] - sx[2], fy = sy[0] - sy[2], _fnorm = _norm2(fx, fy).
        lda _planes_sx;
        sec;
        sbc _planes_sx + 2;
        sta _ffx;
        bpl L_fus_fxp;
        eor #$ff;
        clc;
        adc #1;
    L_fus_fxp:
        sta _foff;              // hi

        lda _planes_sy;
        sec;
        sbc _planes_sy + 2;
        sta _ffy;
        bpl L_fus_fyp;
        eor #$ff;
        clc;
        adc #1;
    L_fus_fyp:
        cmp _foff;
        bcc L_fus_hi;
        ldx _foff;
        sta _foff;
        txa;
    L_fus_hi:
        lsr;
        sta _foff + 1;          // lo >> 1
        lda _foff;
        lsr;
        lsr;
        lsr;
        sta _foff + 2;          // hi >> 3
        lda _foff;
        sec;
        sbc _foff + 2;
        clc;
        adc _foff + 1;
        cmp _foff;
        bcs L_fus_nrm;
        lda _foff;
    L_fus_nrm:
        sta _fnorm;
  }
  // clang-format on

  if (_fnorm != 0) {
    _planes_tube(vec_div8p8((int16_t)-_ffy, _fnorm), vec_div8p8(_ffx, _fnorm),
                 kPlaneVertCount);
    _planes_vert_count = kPlaneVertCount + 2 * kPlaneBodyCount;
  }

  // End-on octagon when _fnorm < (_fmaxr << 2).
  // clang-format off
  __asm {
        lda _fmaxr;
        cmp #64;
        bcs L_fus_do_oct;
        asl;
        asl;
        cmp _fnorm;
        beq L_fus_oct_done;
        bcc L_fus_oct_done;
    L_fus_do_oct:
        lda _fmaxr;
        bne L_fus_r_ok;
        lda #1;
        sta _fmaxr;
    L_fus_r_ok:
        ldx #106;
        stx vec_mul8_b;
        jsr vec_mul8;
        ldx vec_mul8_lo;
        beq L_fus_h_ok;
        clc;
        adc #1;
    L_fus_h_ok:
        // _foff[0..9] = { +h, +r, +r, +h, -h, -r, -r, -h, +h, +r }, so corner
        // i = 0..7 has dx = _foff[i + 2] and dy = _foff[i].
        sta _foff + 0;
        sta _foff + 3;
        sta _foff + 8;
        eor #$ff;
        clc;
        adc #1;
        sta _foff + 4;
        sta _foff + 7;
        lda _fmaxr;
        sta _foff + 1;
        sta _foff + 2;
        sta _foff + 9;
        eor #$ff;
        clc;
        adc #1;
        sta _foff + 5;
        sta _foff + 6;

        ldy _fwidest;
        lda _planes_sx, y;
        sta _ffx;
        lda _planes_sy, y;
        sta _ffy;

        ldx _planes_vert_count;
        ldy #0;
    L_fus_oct:
        lda _ffx;
        clc;
        adc _foff + 2, y;
        sta _planes_vx, x;
        lda _ffy;
        clc;
        adc _foff, y;
        sta _planes_vy, x;
        inx;
        iny;
        cpy #8;
        bne L_fus_oct;
        stx _planes_vert_count;
    L_fus_oct_done:
  }
  // clang-format on
}

// ---------------------------------------------------------------------------
// Layout hysteresis, centring anchor, and placement.

void _planes_layout(planes_state_t *state, uint8_t level) {
  _planes_box(_planes_vert_count);
  // _foff[0..6] = bw, bh, level, same, cols, rows, hw/hh.
  // clang-format off
  __asm {
        lda state;
        sta vec_proj_mul_a;
        lda state + 1;
        sta vec_proj_mul_a + 1;
        lda level;
        sta _foff + 2;

        lda _planes_box_x1;
        sec;
        sbc _planes_box_x0;
        sta _foff;              // bw
        lda _planes_box_y1;
        sec;
        sbc _planes_box_y0;
        sta _foff + 1;          // bh

        ldx #1;
        lda _foff + 2;
        cmp #1;                 // kPlaneLevel1x
        beq L_lay_xs;
        inx;
    L_lay_xs:
        stx _planes_lxs;

        lda #1;
        sta _planes_lys;
        lda _foff + 2;
        cmp #2;                 // kPlaneLevelX
        bne L_lay_ys_done;
        ldx #41;                // kPlaneYLimit
        ldy #0;                 // lay.level
        lda (vec_proj_mul_a), y;
        cmp #2;
        bne L_lay_ys_cmp;
        iny;                    // lay.ys
        lda (vec_proj_mul_a), y;
        cmp #2;
        bne L_lay_ys_cmp;
        ldx #35;                // kPlaneYHold
    L_lay_ys_cmp:
        txa;
        cmp _foff + 1;          // C=0 iff lim < bh (bh > lim)
        bcs L_lay_ys_done;
        inc _planes_lys;
    L_lay_ys_done:

        lda #0;
        sta _foff + 3;          // same
        ldy #0;
        lda (vec_proj_mul_a), y;
        cmp _foff + 2;
        bne L_lay_same_done;
        iny;                    // lay.ys
        lda (vec_proj_mul_a), y;
        cmp _planes_lys;
        bne L_lay_same_done;
        inc _foff + 3;
    L_lay_same_done:

        ldx #23;                // kCols - 1
        lda _foff + 3;
        beq L_lay_wlim;
        ldy #2;                 // lay.cols
        lda (vec_proj_mul_a), y;
        cmp #2;
        bne L_lay_wlim;
        ldx #20;                // kPlaneColsHold
    L_lay_wlim:
        txa;
        ldx _planes_lxs;
        cpx #2;
        bne L_lay_wcmp;
        asl;
    L_lay_wcmp:
        ldx #1;
        cmp _foff;              // C=0 iff wlim < bw (bw > wlim)
        bcs L_lay_cols;
        inx;
    L_lay_cols:
        stx _foff + 4;          // cols

        ldx #20;                // kRows - 1
        lda _foff + 3;
        beq L_lay_hlim;
        ldy #3;                 // lay.rows
        lda (vec_proj_mul_a), y;
        cmp #2;
        bne L_lay_hlim;
        ldx #17;                // kPlaneRowsHold
    L_lay_hlim:
        txa;
        ldx _planes_lys;
        cpx #2;
        bne L_lay_hcmp;
        asl;
    L_lay_hcmp:
        ldx #1;
        cmp _foff + 1;          // C=0 iff hlim < bh (bh > hlim)
        bcs L_lay_rows;
        inx;
    L_lay_rows:
        stx _foff + 5;          // rows

        lda #0;
        sta _planes_lcached;
        lda _foff + 3;
        beq L_lay_cache_done;
        ldy #8;                 // key_valid
        lda (vec_proj_mul_a), y;
        beq L_lay_cache_done;
        iny;                    // key_count == 9
        lda (vec_proj_mul_a), y;
        cmp _planes_vert_count;
        bne L_lay_cache_done;
        ldy #2;                 // lay.cols
        lda (vec_proj_mul_a), y;
        cmp _foff + 4;
        bne L_lay_cache_done;
        iny;                    // lay.rows == 3
        lda (vec_proj_mul_a), y;
        cmp _foff + 5;
        bne L_lay_cache_done;
        inc _planes_lcached;
    L_lay_cache_done:

        lda #48;
        ldx _planes_lxs;
        cpx #2;
        beq L_lay_hwc;
        lsr;
    L_lay_hwc:
        ldx _foff + 4;
        cpx #2;
        beq L_lay_hw;
        lsr;
    L_lay_hw:
        sta _foff + 6;
        sec;
        lda _planes_box_x0;
        adc _planes_box_x1;
        ror;
        sec;
        sbc _foff + 6;
        sta _planes_lbx;

        lda #42;
        ldx _planes_lys;
        cpx #2;
        beq L_lay_hhr;
        lsr;
    L_lay_hhr:
        ldx _foff + 5;
        cpx #2;
        beq L_lay_hh;
        lsr;
    L_lay_hh:
        sta _foff + 6;
        sec;
        lda _planes_box_y0;
        adc _planes_box_y1;
        ror;
        sec;
        sbc _foff + 6;
        sta _planes_lby;

        ldy #0;
        lda _foff + 2;
        sta (vec_proj_mul_a), y;
        iny;
        lda _planes_lys;
        sta (vec_proj_mul_a), y;
        iny;
        lda _foff + 4;
        sta (vec_proj_mul_a), y;
        iny;
        lda _foff + 5;
        sta (vec_proj_mul_a), y;
        iny;
        lda _planes_lbx;
        eor #$80;
        sta (vec_proj_mul_a), y;
        iny;
        lda _planes_lby;
        eor #$80;
        sta (vec_proj_mul_a), y;
        iny;
        lda #0;
        sta (vec_proj_mul_a), y;
        iny;
        sta (vec_proj_mul_a), y;
  }
  // clang-format on
}

static __zeropage uint8_t *_blk;    // frame pointer in planes_centre/planes_place, back buffer in planes_fill

// Steps 1-7 for the frame alone: hidden check, view-cone check, screen centre
// (view->cx0 - c->y / c->x, view->cy0 - c->z / c->x), perspective scale k
// (min(kmax, floor(kn * 512 / c->x))), and pixel diameter d (trunc(2R * k / 256)),
// sharing the positive divisor c->x > 64 across all three vec_frac8 calls.
bool planes_centre(const planes_view_t *view, const vec3_t *c,
                   planes_frame_t *frame) {
#ifdef __PLANES_PROFILE__
  bm_start();
#endif
  // _foff[0] steps 0 -> 2 across the two axes (c->y / cx0 / cx, then c->z / cy0 / cy).
  // clang-format off
  __asm {
        lda c;
        sta vec_proj_mul_a;
        lda c + 1;
        sta vec_proj_mul_a + 1;
        lda view;
        sta vec_proj_mul_b;
        lda view + 1;
        sta vec_proj_mul_b + 1;
        lda frame;
        sta _blk;
        lda frame + 1;
        sta _blk + 1;

        lda #0;
        tay;                    // frame->hidden == 0 (kPlaneShown)
        sta (_blk), y;
        ldy #10;                // frame->slid == 10
        sta (_blk), y;
        ldy #12;                // frame->cached == 12
        sta (_blk), y;

        // if (c->x <= 64) { frame->hidden = kPlaneBehindCamera; return false; }
        ldy #1;
        lda (vec_proj_mul_a), y;
        bmi L_pc_behind;
        sta frac_d1;
        dey;
        lda (vec_proj_mul_a), y;
        sta frac_d0;
        ldx frac_d1;
        bne L_pc_x_ok;
        cmp #65;
        bcs L_pc_x_ok;
    L_pc_behind:
        lda #1;                 // kPlaneBehindCamera
    L_pc_hide:
        ldy #0;                 // frame->hidden
        sta (_blk), y;
        lda #0;
        sta vec_proj_tmp1;
        jmp L_pc_done;

    L_pc_x_ok:
        // Cone check + 5. centre across _foff = 0 (c->y, cx0, cx) and 2 (c->z, cy0, cy).
        lda #0;
    L_pc_axis:
        sta _foff;
        tay;
        iny;
        iny;                    // c->y == 2, c->z == 4
        lda (vec_proj_mul_a), y;
        sta frac_r0;
        iny;
        lda (vec_proj_mul_a), y;
        sta vec_proj_tmp1;
        bpl L_pc_abs_pos;
        sec;
        lda #0;
        sbc frac_r0;
        sta frac_r0;
        lda #0;
        sbc vec_proj_tmp1;
    L_pc_abs_pos:
        sta frac_r1;
        ldx frac_r0;
        cpx frac_d0;
        sbc frac_d1;
        bcc L_pc_div;
        lda #2;                 // kPlaneOutOfRange
        bne L_pc_hide;

    L_pc_div:
        jsr vec_frac8;
        ldy _foff;              // view->cx0 == 0, view->cy0 == 2
        bit vec_proj_tmp1;
        bmi L_pc_add_q;
        lda (vec_proj_mul_b), y;
        sec;
        sbc frac_q0;
        tax;
        iny;
        lda (vec_proj_mul_b), y;
        sbc #0;
        jmp L_pc_store_c;
    L_pc_add_q:
        lda (vec_proj_mul_b), y;
        clc;
        adc frac_q0;
        tax;
        iny;
        lda (vec_proj_mul_b), y;
        adc #0;
    L_pc_store_c:
        pha;
        lda _foff;
        clc;
        adc #15;                // frame->cx == 15, frame->cy == 17
        tay;
        txa;
        sta (_blk), y;
        iny;
        pla;
        sta (_blk), y;
        lda _foff;
        eor #2;
        bne L_pc_axis;

        // 6 & 6b. k = min(kmax, c->x > kn ? floor(kn * 512 / c->x) : 0x1ff).
        // Since view->kmax <= 255, any 2 * kn >= c->x has k >= 256 > kmax and
        // clamps immediately; 2 * kn < c->x needs one 8-bit vec_frac8 step.
        ldy #9;                 // view->kn
        lda (vec_proj_mul_b), y;
        asl;
        sta frac_r0;
        lda #0;
        rol;
        sta frac_r1;
        ldx frac_r0;
        cpx frac_d0;
        sbc frac_d1;
        bcs L_pc_clamp;
        jsr vec_frac8;
        ldx frac_q0;
        ldy #8;                 // view->kmax
        lda (vec_proj_mul_b), y;
        cmp frac_q0;
        bcc L_pc_clamp_a;
        lda #0;
        beq L_pc_store_k;
    L_pc_clamp:
        ldy #8;                 // view->kmax
        lda (vec_proj_mul_b), y;
    L_pc_clamp_a:
        tax;
        lda #1;
    L_pc_store_k:
        ldy #11;                // frame->clamped
        sta (_blk), y;
        txa;
        ldy #13;                // frame->k
        sta (_blk), y;

        // 7. d = (2 * kPlaneMaxRadius * kb) >> 8
        sta vec_mul8_b;
        lda #136;               // kPlaneMaxRadius << 1
        jsr vec_mul8;
        ldy #14;                // frame->d
        sta (_blk), y;
        lda #1;
        sta vec_proj_tmp1;

    L_pc_done:
  }
  // clang-format on
#ifdef __PLANES_PROFILE__
  if (vec_proj_tmp1 != 0) {
    bm_end(2 * 40 + 20, kProfCentre);
  }
#endif
  return (bool)vec_proj_tmp1;
}

// Places layout (vec_proj_mul_a) under view (vec_proj_mul_b) into frame (_blk),
// leaving `need` in _place_need and preserving vec_proj_mul_a.
static __noinline void _place(void) {
  // _foff[0..5] = topl, toph, slidl, slidh, ys, rows.
  // clang-format off
  __asm {
        ldy #0;                 // lay->level
        lda (vec_proj_mul_a), y;
        tax;
        ldy #1;                 // frame->level
        sta (_blk), y;
        txa;
        bne L_plc_sil;

        // Dot tier: xs = ys = cols = rows = 1, x = cx - 12, y = cy - 19, slid = 0.
        lda #1;
        iny;                    // frame->xs == 2
        sta (_blk), y;
        iny;                    // frame->ys == 3
        sta (_blk), y;
        iny;                    // frame->cols == 4
        sta (_blk), y;
        iny;                    // frame->rows == 5
        sta (_blk), y;

        ldy #15;                // frame->cx
        lda (_blk), y;
        sec;
        sbc #12;                // kPlaneDotX
        pha;
        iny;
        lda (_blk), y;
        sbc #0;
        ldy #7;                 // frame->x + 1
        sta (_blk), y;
        dey;                    // frame->x == 6
        pla;
        sta (_blk), y;

        ldy #17;                // frame->cy
        lda (_blk), y;
        sec;
        sbc #19;                // kPlaneDotY
        sta _foff;
        iny;
        lda (_blk), y;
        sbc #0;
        sta _foff + 1;
        ldy #8;                 // frame->y
        lda _foff;
        sta (_blk), y;
        iny;                    // frame->y + 1 == 9
        lda _foff + 1;
        sta (_blk), y;
        iny;                    // frame->slid == 10
        lda #0;
        sta (_blk), y;
        sta _place_need;
        sta _place_need + 1;

        // if (frame->y > view->cut1) -> view->cut1 < frame->y (signed 16-bit).
        ldy #4;                 // view->cut1
        lda (vec_proj_mul_b), y;
        cmp _foff;
        iny;
        lda (vec_proj_mul_b), y;
        sbc _foff + 1;
        bvc L_plc_dot_vc;
        eor #$80;
    L_plc_dot_vc:
        bpl L_plc_done_jmp;
        ldy #0;                 // frame->hidden
        lda #3;                 // kPlaneBelowCut
        sta (_blk), y;
    L_plc_done_jmp:
        jmp L_plc_done;

    L_plc_sil:
        // X holds level (1 or 2). xs = (level == 1) ? 1 : 2.
        ldy #1;
        cpx #1;
        beq L_plc_xs;
        iny;
    L_plc_xs:
        tya;
        ldy #2;                 // frame->xs
        sta (_blk), y;

        ldy #1;                 // lay->ys
        lda (vec_proj_mul_a), y;
        sta _foff + 4;
        ldy #3;                 // frame->ys
        sta (_blk), y;

        ldy #2;                 // lay->cols
        lda (vec_proj_mul_a), y;
        ldy #4;                 // frame->cols
        sta (_blk), y;

        ldy #3;                 // lay->rows
        lda (vec_proj_mul_a), y;
        sta _foff + 5;
        ldy #5;                 // frame->rows
        sta (_blk), y;

        // frame->x = frame->cx + (int8_t)lay->ox.
        ldx #0;
        ldy #4;                 // lay->ox
        lda (vec_proj_mul_a), y;
        bpl L_plc_oxp;
        dex;
    L_plc_oxp:
        ldy #15;                // frame->cx
        clc;
        adc (_blk), y;
        pha;
        iny;
        txa;
        adc (_blk), y;
        ldy #7;                 // frame->x + 1
        sta (_blk), y;
        dey;                    // frame->x == 6
        pla;
        sta (_blk), y;

        // top = frame->cy + (int8_t)lay->oy -> _foff[0..1].
        ldx #0;
        ldy #5;                 // lay->oy
        lda (vec_proj_mul_a), y;
        bpl L_plc_oyp;
        dex;
    L_plc_oyp:
        ldy #17;                // frame->cy
        clc;
        adc (_blk), y;
        sta _foff;
        iny;
        txa;
        adc (_blk), y;
        sta _foff + 1;

        // over = top - (ys == 2 ? view->cut2 : view->cut1).
        ldy #4;                 // view->cut1
        lda _foff + 4;          // ys
        cmp #2;
        bne L_plc_cut;
        ldy #6;                 // view->cut2
    L_plc_cut:
        lda _foff;
        sec;
        sbc (vec_proj_mul_b), y;
        sta _place_need;
        iny;
        lda _foff + 1;
        sbc (vec_proj_mul_b), y;
        sta _place_need + 1;

        lda _foff + 5;          // rows
        cmp #2;
        bne L_plc_over_chk;
        lda #21;                // kRows
        ldx _foff + 4;          // ys
        cpx #2;
        bne L_plc_add_r;
        asl;                    // 42
    L_plc_add_r:
        clc;
        adc _place_need;
        sta _place_need;
        bcc L_plc_over_chk;
        inc _place_need + 1;
    L_plc_over_chk:
        lda _place_need + 1;
        bpl L_plc_need_pos;
        lda #0;
        sta _place_need;
        sta _place_need + 1;
    L_plc_need_pos:

        // slid = (lay->slid < need) ? need : lay->slid.
        lda _place_need;
        sta _foff + 2;
        lda _place_need + 1;
        sta _foff + 3;
        ldy #6;                 // lay->slid
        lda (vec_proj_mul_a), y;
        tax;
        cmp _place_need;
        iny;
        lda (vec_proj_mul_a), y;
        sbc _place_need + 1;
        bvc L_plc_slid_vc;
        eor #$80;
    L_plc_slid_vc:
        bmi L_plc_slid_ok;      // lay->slid < need -> keep need
        stx _foff + 2;
        lda (vec_proj_mul_a), y;
        sta _foff + 3;
    L_plc_slid_ok:
        lda _foff;
        sec;
        sbc _foff + 2;
        ldy #8;                 // frame->y
        sta (_blk), y;
        lda _foff + 1;
        sbc _foff + 3;
        iny;                    // frame->y + 1 == 9
        sta (_blk), y;
        iny;                    // frame->slid == 10
        lda _foff + 2;
        sta (_blk), y;
    L_plc_done:
  }
  // clang-format on
}

int16_t planes_place(const planes_layout_t *lay, const planes_view_t *view,
                     planes_frame_t *frame) {
  // clang-format off
  __asm {
        lda lay;
        sta vec_proj_mul_a;
        lda lay + 1;
        sta vec_proj_mul_a + 1;
        lda view;
        sta vec_proj_mul_b;
        lda view + 1;
        sta vec_proj_mul_b + 1;
        lda frame;
        sta _blk;
        lda frame + 1;
        sta _blk + 1;
  }
  // clang-format on
  _place();
  return _place_need;
}

bool planes_reslide(planes_state_t *state, const planes_view_t *view,
                    planes_frame_t *frame, uint8_t when) {
  _kdiff = when;
  // d = _place_need - lay->slid -> _foff[0..1].
  // clang-format off
  __asm {
        lda state;
        sta vec_proj_mul_a;
        lda state + 1;
        sta vec_proj_mul_a + 1;
        lda view;
        sta vec_proj_mul_b;
        lda view + 1;
        sta vec_proj_mul_b + 1;
        lda frame;
        sta _blk;
        lda frame + 1;
        sta _blk + 1;
        jsr _place;

        ldy #6;                 // lay->slid
        lda _place_need;
        sec;
        sbc (vec_proj_mul_a), y;
        sta _foff;
        iny;                    // lay->slid + 1 == 7
        lda _place_need + 1;
        sbc (vec_proj_mul_a), y;
        sta _foff + 1;

        // if (when != Always && d > -2 && (when == Deep || d < 2)) return false;
        ldx _kdiff;
        cpx #2;                 // kPlaneReslideAlways
        beq L_rs_move;
        ora #0;                 // d_hi
        bpl L_rs_d_pos;
        cmp #$ff;
        bne L_rs_move;          // d <= -257
        lda _foff;
        cmp #$ff;
        bne L_rs_move;          // -256 <= d <= -2
    L_rs_no:
        lda #0;
        sta vec_proj_tmp1;
        jmp L_rs_done;
    L_rs_d_pos:
        cpx #0;                 // kPlaneReslideDeep
        beq L_rs_no;
        cmp #0;                 // d_hi != 0 -> d >= 256 >= 2
        bne L_rs_move;
        lda _foff;
        cmp #2;
        bcc L_rs_no;

    L_rs_move:
        ldy #1;                 // lay->ys
        lda (vec_proj_mul_a), y;
        tax;
        cpx #2;
        bne L_rs_add_slid;
        lda _foff;
        and #1;
        beq L_rs_add_slid;
        inc _foff;
        bne L_rs_add_slid;
        inc _foff + 1;
    L_rs_add_slid:
        ldy #6;                 // lay->slid
        lda (vec_proj_mul_a), y;
        clc;
        adc _foff;
        sta (vec_proj_mul_a), y;
        sta _foff + 2;
        iny;                    // lay->slid + 1 == 7
        lda (vec_proj_mul_a), y;
        adc _foff + 1;
        sta (vec_proj_mul_a), y;
        sta _foff + 3;

        cpx #2;                 // lay->ys == 2?
        bne L_rs_dy_ok;
        lsr _foff + 1;
        ror _foff;
    L_rs_dy_ok:
        // state->key_band = (uint8_t)((uint16_t)lay->slid >> 7).
        lda _foff + 2;
        asl;
        lda _foff + 3;
        rol;
        ldy #10;                // state->key_band
        sta (vec_proj_mul_a), y;

        ldx _foff;              // dy == 0 leaves key_y unchanged
        beq L_rs_yes;
        dey;                    // state->key_count == 9
        lda (vec_proj_mul_a), y;
        beq L_rs_yes;
        clc;
        adc #40;                // offsetof(planes_state_t, key_y) - 1
        tay;
    L_rs_ky:
        lda (vec_proj_mul_a), y;
        clc;
        adc _foff;
        sta (vec_proj_mul_a), y;
        dey;
        cpy #40;
        bne L_rs_ky;
    L_rs_yes:
        lda #1;
        sta vec_proj_tmp1;
    L_rs_done:
  }
  // clang-format on
  return (bool)vec_proj_tmp1;
}

// ---------------------------------------------------------------------------
// The vertex cache's key.

static_assert(kPlaneVertMax == 30, "_planes_key() finds key_y 30 bytes after key_x");

// Self-modifying: the call's constants are written into the loop before it
// runs - the key's two addresses, bx, by and the slide's low byte as
// operands, and the expansions as opcodes - so that a vertex costs its
// loads, stores and compares and nothing else. Y-expanded, y is bits 1-8 of
// the nine-bit (v - by) + s9: ror brings the sum's carry in as bit 7, and
// eor flips it when the slide's own bit 8 is set.
bool _planes_key(uint8_t *key, uint16_t slid) {
  // clang-format off
  __asm {
        lda key;
        sta L_cx + 1;
        sta L_sx + 1;
        clc;
        adc #30;
        sta L_cy + 1;
        sta L_sy + 1;
        lda key + 1;
        sta L_cx + 2;
        sta L_sx + 2;
        adc #0;
        sta L_cy + 2;
        sta L_sy + 2;
        lda _planes_lbx;
        sta L_bx + 1;
        lda _planes_lby;
        sta L_by + 1;
        lda slid;
        sta L_s9 + 1;

        lda #$ea;               // nop
        ldx _planes_lxs;
        cpx #2;
        bne L_x_one;
        lda #$4a;               // lsr a
    L_x_one:
        sta L_xsh;
        lda #$ea;
        ldx #0;
        ldy _planes_lys;
        cpy #2;
        bne L_y_one;
        lda slid + 1;
        and #1;
        beq L_y_s9_0;
        ldx #$80;
    L_y_s9_0:
        lda #$6a;               // ror a
    L_y_one:
        sta L_ysh;
        stx L_yeor + 1;

        lda #0;
        sta _kdiff;
        ldy _planes_vert_count;
        dey;
    L_vert:
        lda _planes_vx, y;
        sec;
    L_bx:
        sbc #0;
    L_xsh:
        nop;
    L_cx:
        cmp $1234, y;
        beq L_x_same;
        inc _kdiff;
    L_x_same:
    L_sx:
        sta $1234, y;

        lda _planes_vy, y;
        sec;
    L_by:
        sbc #0;
        clc;
    L_s9:
        adc #0;
    L_ysh:
        nop;
    L_yeor:
        eor #0;
    L_cy:
        cmp $1234, y;
        beq L_y_same;
        inc _kdiff;
    L_y_same:
    L_sy:
        sta $1234, y;
        dey;
        bpl L_vert;
  }
  // clang-format on
  return _kdiff != 0;
}

// ---------------------------------------------------------------------------
// The fill: convex polygons into up to four sprite blocks 64 bytes apart.

#ifdef __FPILOT__
#pragma bss(tscratch)
#endif
static uint8_t _buf_h, _buf_w1;     // height, and width - 1
static uint8_t _bottom_adj;         // offset of the lower row of blocks, less 64
static uint8_t _lo[42], _hi[42];    // each row's extent while a polygon is traced
static uint8_t _fs, _fn;            // the polygon: first vertex, end/count

static uint8_t _y0, _y1;
static uint8_t _sl, _sh, _xbh, _t;
#ifdef __FPILOT__
#pragma bss(bss)
#endif

// Reuse vec.h's per-call zero-page scratch inside L_fill_poly (after L_fl_copy
// finishes reading vec_proj_mul_a/b; vec_mul8 only touches vec_mul8_b,
// vec_mul8_lo, and vec_proj_recip).
#define _xbl vec_proj_tmp1
#define _base vec_proj_mul_a
#define _jb vec_proj_mul_a + 1
#define _pb vec_proj_mul_b
#define _row vec_proj_mul_b + 1
#define _fi vec_sx
#define _dy vec_sx + 1
#define _xt vec_sy
#define _yt vec_sy + 1
#define _xe frac_q0
#define _dxl frac_q1
#define _dxh frac_r0

// kPlaneRecip is striped, its high bytes kPlaneRecipCount after the low ones,
// and starts at dy kPlaneRecipFirst; the inline assembler takes only a
// literal offset - so the fill spells them 39 - 3 and -3, and this holds the
// generator to them.
static_assert(kPlaneRecipCount == 39 && kPlaneRecipFirst == 3,
              "update kPlaneRecip + 36 and - 3 in the fill");

void planes_fill(const planes_state_t *state, uint8_t *back) {
#ifdef __PLANES_PROFILE__
  bm_start();
#endif
  // clang-format off
  __asm {
        lda state;
        sta vec_proj_mul_a;
        lda state + 1;
        sta vec_proj_mul_a + 1;
        lda back;
        sta _blk;
        lda back + 1;
        sta _blk + 1;

        // cols (1 or 2) and rows (1 or 2) -> height, w1, bottom_adj, and
        // blocks << 6 bytes cleared at (_blk) (64, 128, or 256 via Y = 0).
        ldx #23;
        ldy #0;
        sty _bottom_adj;
        ldy #2;                 // lay.cols
        lda (vec_proj_mul_a), y;
        sta _t;
        cmp #2;
        bne L_fl_cols1;
        ldx #47;
        lda #64;
        sta _bottom_adj;
    L_fl_cols1:
        stx _buf_w1;

        ldy #3;                 // lay.rows
        lda (vec_proj_mul_a), y;
        ldx #21;
        cmp #2;
        bne L_fl_rows1;
        ldx #42;
    L_fl_rows1:
        stx _buf_h;

        ldx _t;                 // cols
        cpx #2;
        bne L_fl_blks;
        asl;                    // blocks = rows << 1
    L_fl_blks:
        // blocks in {1, 2, 4} -> Y in {64, 128, 0 (= 256)}, a multiple of 8.
        lsr;
        ror;
        ror;
        tay;
        lda #0;
    L_fl_clr:
        dey;
        sta (_blk), y;
        dey;
        sta (_blk), y;
        dey;
        sta (_blk), y;
        dey;
        sta (_blk), y;
        dey;
        sta (_blk), y;
        dey;
        sta (_blk), y;
        dey;
        sta (_blk), y;
        dey;
        sta (_blk), y;
        bne L_fl_clr;

        // Skip fill when ((uint16_t)lay.slid >> (ys == 2)) >= _buf_h.
        ldy #6;                 // lay.slid
        lda (vec_proj_mul_a), y;
        sta _t;
        iny;                    // lay.slid + 1
        lda (vec_proj_mul_a), y;
        tax;
        ldy #1;                 // lay.ys
        lda (vec_proj_mul_a), y;
        cmp #2;
        bne L_fl_slid_chk;
        txa;
        lsr;
        tax;
        ror _t;
    L_fl_slid_chk:
        txa;
        bne L_fl_exit_jmp;
        lda _t;
        cmp _buf_h;
        bcs L_fl_exit_jmp;

        ldy #9;                 // key_count
        lda (vec_proj_mul_a), y;
        bne L_fl_has_verts;
    L_fl_exit_jmp:
        jmp L_fl_exit;

    L_fl_has_verts:
        sta _bn;
        lda vec_proj_mul_a;
        clc;
        adc #11;                // offsetof(planes_state_t, key_x)
        sta vec_proj_mul_a;
        tax;
        lda vec_proj_mul_a + 1;
        adc #0;
        sta vec_proj_mul_a + 1;
        sta vec_proj_mul_b + 1;
        txa;
        clc;
        adc #30;                // kPlaneVertMax -> key_y
        sta vec_proj_mul_b;
        bcc L_fl_ky_ok;
        inc vec_proj_mul_b + 1;
    L_fl_ky_ok:
        ldy _bn;
        dey;
    L_fl_copy:
        lda (vec_proj_mul_a), y;
        sta _planes_lx, y;
        lda (vec_proj_mul_b), y;
        sta _planes_ly, y;
        dey;
        bpl L_fl_copy;

        // Wing (0, 6), tailplane (6, 6), fin (12, 4), then fuselage/octagon.
        lda #0;
        sta _fs;
        lda #6;
        sta _fn;
        jsr L_fill_poly;

        lda #6;
        sta _fs;
        lda #6;
        sta _fn;
        jsr L_fill_poly;

        lda #12;
        sta _fs;
        lda #4;
        sta _fn;
        jsr L_fill_poly;

        lda #16;
        sta _fs;
        lda _bn;
        cmp #30;                // kPlaneVertMax
        bne L_fl_last_poly;
        lda #6;                 // 2 * kPlaneBodyCount
        sta _fn;
        jsr L_fill_poly;
        lda #22;
        sta _fs;
    L_fl_last_poly:
        lda _bn;
        sec;
        sbc _fs;
        beq L_fl_skip_last;
        bcc L_fl_skip_last;
        sta _fn;
        jsr L_fill_poly;
    L_fl_skip_last:
        jmp L_fl_exit;

        // Convex polygon fill on _fs and _fn.
    L_fill_poly:
        // The polygon's rows, y0 .. y1; none if it starts below the buffer.
        ldx _fs;
        lda #$ff;
        sta _y0;
        lda #0;
        sta _y1;
        ldy _fn;
    L_rows:
        lda _planes_ly, x;
        cmp _y0;
        bcs L_not_min;
        sta _y0;
    L_not_min:
        cmp _y1;
        bcc L_not_max;
        sta _y1;
    L_not_max:
        inx;
        dey;
        bne L_rows;
        stx _fn;                // _fs + count (one past last vertex)
        lda _y0;
        cmp _buf_h;
        bcc L_visible;
        rts;
    L_visible:
        lda _y1;
        cmp _buf_h;
        bcc L_y1_in;
        ldx _buf_h;
        dex;
        stx _y1;
    L_y1_in:
        ldx _y0;
    L_init:
        lda #$ff;
        sta _lo, x;
        lda #0;
        sta _hi, x;
        cpx _y1;
        inx;
        bcc L_init;

        // Each edge: top and bottom end points, stepping _fi = _fs .. _fn - 1.
        lda _fs;
        sta _fi;
    L_edge:
        ldx _fi;
        ldy _fi;
        iny;
        sty _fi;
        cpy _fn;
        bne L_b_next;
        ldy _fs;
    L_b_next:
        lda _planes_ly, y;
        cmp _planes_ly, x;
        bcs L_a_top;
        stx _t;
        tya;
        tax;
        ldy _t;
    L_a_top:
        lda _planes_ly, x;
        cmp _buf_h;
        bcc L_top_in;
        jmp L_next;
    L_top_in:
        sta _yt;
        lda _planes_lx, x;
        sta _xt;
        lda _planes_lx, y;
        sta _xe;
        lda _planes_ly, y;
        sec;
        sbc _yt;
        sta _dy;
        bne L_slanted;
        // Level: one row, end to end.
        ldx _yt;
        lda _xt;
        ldy _xe;
        jsr L_put;
        jmp L_next;

    L_slanted:
        // dx = xe - xt; skip slope math & multiplies on vertical edges (dx == 0).
        lda _xe;
        sec;
        sbc _xt;
        sta _dxl;
        bne L_dx_nz;
        sta _dxh;
        sta _sl;
        sta _sh;
        tax;
        beq L_first;
    L_dx_nz:
        lda #0;
        sbc #0;
        sta _dxh;
        cmp #$80;
        ror;
        lda _dxl;
        ror;
        sta _sh;
        tax;
        lda #0;
        ror;
        sta _sl;
        ldy _dy;
        dey;
        beq L_first;
        dey;
        beq L_halve;
        // dy 3 and on: slope = trunc(dx * 65536 / dy / 256) from the
        // reciprocal table, |dx| * hi + hi(|dx| * lo), the sign last.
        lda _dxl;
        ldx _dxh;
        bpl L_dx_pos;
        eor #$ff;
        clc;
        adc #1;
    L_dx_pos:
        sta _t;
        ldy _dy;
        lda kPlaneRecip + 36, y;  // striped: the high bytes, see above
        sta vec_mul8_b;
        lda kPlaneRecip - 3, y;
        sta _pb;
        lda _t;
        jsr vec_mul8;
        sta _sh;
        lda vec_mul8_lo;
        sta _sl;
        lda _pb;
        sta vec_mul8_b;
        lda _t;
        jsr vec_mul8;
        clc;
        adc _sl;
        sta _sl;
        lda _sh;
        adc #0;
        sta _sh;
        lda _dxh;
        bpl L_halve;
        sec;
        lda #0;
        sbc _sl;
        sta _sl;
        lda #0;
        sbc _sh;
        sta _sh;
    L_halve:
        lda _sh;
        cmp #$80;
        ror;
        tax;
        lda _sl;
        ror;

    L_first:
        // The top row takes half a step, from xt * 256 + 128. dx's sign says
        // which end of every middle row is the left one; rows only go down, so
        // the first at or below the buffer ends the edge.
        clc;
        adc #128;
        sta _xbl;
        txa;
        adc _xt;
        sta _xbh;
        ldx _yt;
        ldy _xt;
        jsr L_put;

        // The middle rows a whole step each, from the last row's end.
    L_middle:
        ldy _dy;
        dey;
        beq L_last;
        bit _dxh;
        bmi L_mid_neg;
    L_mid_pos:
        inx;
        cpx _buf_h;
        bcs L_next;
        lda _xbh;
        cmp _lo, x;
        bcs L_mp_step;
        sta _lo, x;
    L_mp_step:
        clc;
        lda _xbl;
        adc _sl;
        sta _xbl;
        lda _xbh;
        adc _sh;
        sta _xbh;
        cmp _hi, x;
        bcc L_mp_next;
        sta _hi, x;
    L_mp_next:
        dey;
        bne L_mid_pos;
        beq L_last;
    L_mid_neg:
        inx;
        cpx _buf_h;
        bcs L_next;
        lda _xbh;
        cmp _hi, x;
        bcc L_mn_step;
        sta _hi, x;
    L_mn_step:
        clc;
        lda _xbl;
        adc _sl;
        sta _xbl;
        lda _xbh;
        adc _sh;
        sta _xbh;
        cmp _lo, x;
        bcs L_mn_next;
        sta _lo, x;
    L_mn_next:
        dey;
        bne L_mid_neg;

        // The bottom row ends on the end point.
    L_last:
        inx;
        cpx _buf_h;
        bcs L_next;
        lda _xbh;
        ldy _xe;
        jsr L_put;
    L_next:
        lda _fi;
        cmp _fn;
        beq L_spans;
        jmp L_edge;

        // Each row from its leftmost pixel to its rightmost.
    L_spans:
        ldx _y0;
    L_span_row:
        stx _row;
        lda _hi, x;
        cmp _lo, x;
        bcc L_span_next;
        cmp _buf_w1;
        bcc L_b_in;
        lda _buf_w1;
    L_b_in:
        sta _pb;
        lsr;
        lsr;
        lsr;
        sta _jb;
        // The row's first byte: 3 * row, and in the lower blocks _bottom on
        // from row 21, which is 3 * row + _bottom - 63: the compare's carry
        // is the 1 that _bottom_adj, _bottom - 64, is short of it.
        stx _t;
        txa;
        asl;
        adc _t;
        cpx #21;
        bcc L_upper;
        adc _bottom_adj;
    L_upper:
        sta _base;
        lda _lo, x;
        cmp _buf_w1;
        bcc L_a_in;
        lda _buf_w1;
    L_a_in:
        // The left end's byte into X, its mask into A.
        tay;
        lsr;
        lsr;
        lsr;
        tax;
        tya;
        and #7;
        tay;
        lda kPlaneLeftMask, y;
        cpx _jb;
        beq L_right;
        sta _t;
        lda kPlaneByteOffset, x;
        clc;
        adc _base;
        tay;
        lda (_blk), y;
        ora _t;
        sta (_blk), y;
        lda #$ff;
        inx;
        cpx _jb;
        beq L_right;
    L_full:
        lda kPlaneByteOffset, x;
        clc;
        adc _base;
        tay;
        lda #$ff;
        sta (_blk), y;
        inx;
        cpx _jb;
        bne L_full;
    L_right:
        sta _t;
        lda _pb;
        and #7;
        tay;
        lda kPlaneRightMask, y;
        and _t;
        sta _t;
        lda kPlaneByteOffset, x;
        clc;
        adc _base;
        tay;
        lda (_blk), y;
        ora _t;
        sta (_blk), y;
    L_span_next:
        ldx _row;
        cpx _y1;
        inx;
        bcs L_done;
        jmp L_span_row;
    L_done:
        rts;

        // Widen row X's extent (X < _buf_h) to take pixels A and Y, in either
        // order. Preserves X.
    L_put:
        sty _t;
        cmp _t;
        bcc L_put_ay;
        tay;
        lda _t;
    L_put_ay:
        cmp _lo, x;
        bcs L_put_hi;
        sta _lo, x;
    L_put_hi:
        tya;
        cmp _hi, x;
        bcc L_put_done;
        sta _hi, x;
    L_put_done:
        rts;

    L_fl_exit:
  }
  // clang-format on
#ifdef __PLANES_PROFILE__
  bm_end(6 * 40 + 20, kProfFill);
#endif
}

#undef _xbl
#undef _base
#undef _jb
#undef _pb
#undef _row
#undef _fi
#undef _dy
#undef _xt
#undef _yt
#undef _xe
#undef _dxl
#undef _dxh

#pragma optimize(pop)
