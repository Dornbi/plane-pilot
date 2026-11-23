#include "panel_asm.h"

#include <stddef.h>
#include <stdint.h>

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

#ifdef __OSCAR64__

#ifdef __MAX_RAM__
#pragma bss(bss2)
#endif

// Cached last-written gauge values, zeroed by mem_init() at boot. Every live
// entry stores bit 7 set ($80..$FF) so the initial 0x00 always misses:
//   [0,1,4,5,6,7] = (dir & 0x1F) | 0x80 for the six _set_arm gauges
//   [2]           = roll_angle | 0x80 (also triggers one-time init of the
//                   four constant roll/pitch sprite bytes)
//   [3]           = ((flight_eye_z >> 12) >> 8) | 0x80 (alt high nibble)
// _panel_alt_lo holds the matching low byte of (flight_eye_z >> 12).
static uint8_t _panel_arm_dir[8];
static uint8_t _panel_alt_lo;

#ifdef __MAX_RAM__
#pragma bss(bss)
#endif

// Heading strip source addresses ($F120, $F0C0, $F060, $F000), striped so
// heading & 3 indexes directly without a shift.
static const uint8_t kPanelHdgLo[4] = {0x20, 0xC0, 0x60, 0x00};
static const uint8_t kPanelHdgHi[4] = {0xF1, 0xF0, 0xF0, 0xF0};

static_assert(kSpriteIdxVSpeed == 0 && kSpriteIdxSpeed == 1 &&
                  kSpriteIdxRoll == 2 && kSpriteIdxPitch == 3 &&
                  kSpriteIdxAlt1 == 4 && kSpriteIdxAlt2 == 5 &&
                  kSpriteIdxThrottle == 6 && kSpriteIdxFuel == 7,
              "panel_asm sprite indices");
static_assert(kSpriteOffsetX + kSpriteRollPivotX - 12 == 0xAC &&
                  kSpriteOffsetY + kSpriteRollPivotY - 10 == 0xD8 &&
                  kSpriteDefLongArmBitmap == 0x60 &&
                  kSpriteDefShortArmBitmap == 0x70 &&
                  kSpriteDefArmCount == 16 &&
                  2 * kSpriteDefArmCenterX == 24 &&
                  2 * kSpriteDefArmCenterY == 20,
              "panel_asm roll/pitch/arm geometry");
static_assert(offsetof(mat3_t, front) == 0 && offsetof(vec3_t, z) == 4 &&
                  sizeof(sprite_xy_t) == 2 && offsetof(sprite_xy_t, x) == 0 &&
                  offsetof(sprite_xy_t, y) == 1,
              "panel_asm struct offsets");
static_assert(kRollMax == 60 && kHeadingMax == 48 && VIEW_LEFT == 1 &&
                  VIEW_CENTER == 2 && kColorBlack == 0x00 &&
                  kColorLightRed == 0x0A && kScreenWidth == 40 &&
                  kCopyWidthChars == 8 && kFillWidthChars == 32,
              "panel_asm view/color/heading constants");
#if defined(__ENABLE_OPPONENT__) || defined(__TARGET_TEST__)
static_assert(kGunAmmoDigits == 3 && kCopyWidthChars - 6 == 2,
              "panel_asm ammo constants");
#endif

#ifdef __TARGET_TEST__
void panel_asm_reset_cache(void) {
  for (uint8_t i = 0; i < 8; ++i) {
    _panel_arm_dir[i] = 0;
  }
  _panel_alt_lo = 0;
}
#endif

void panel_update_instruments(void) {
  // clang-format off
  __asm {
        // 1) Airspeed: dir = (uint8_t)(flight_speed >> 6) >> 1 = bits 7..11
        lda flight_speed;
        asl;
        lda flight_speed + 1;
        rol;
        ldx #1;                         // kSpriteIdxSpeed
        jsr L_set_arm;

        // 2) Altitude: a = (uint16_t)(flight_eye_z >> 8) >> 4 (12 bits).
        // Skip both Alt1, vec_mul8, and Alt2 whenever a is unchanged.
        lda flight_eye_z + 2;
        lsr;
        sta vec_proj_mul_a + 1;
        lda flight_eye_z + 1;
        ror;
        lsr vec_proj_mul_a + 1;
        ror;
        lsr vec_proj_mul_a + 1;
        ror;
        lsr vec_proj_mul_a + 1;
        ror;
        sta vec_proj_mul_a;             // a_lo
        lda vec_proj_mul_a + 1;         // a_hi (0..15)
        ora #$80;
        tax;
        lda vec_proj_mul_a;
        cpx _panel_arm_dir + 3;
        bne L_alt_upd;
        cmp _panel_alt_lo;
        beq L_alt_done;
    L_alt_upd:
        stx _panel_arm_dir + 3;
        sta _panel_alt_lo;
        ldx #4;                         // kSpriteIdxAlt1
        jsr L_set_arm;
        // ((a * 205) >> 11) matches ((uint8_t)vec_fastmul8p8(a, 205)) >> 3.
        lda #205;
        sta vec_mul8_b;
        lda vec_proj_mul_a;
        jsr vec_mul8;
        ldx vec_proj_mul_a + 1;
        beq L_alt2_sh;
        sta vec_proj_tmp1;
        txa;
        jsr vec_mul8;
        lda vec_mul8_lo;
        clc;
        adc vec_proj_tmp1;
    L_alt2_sh:
        lsr;
        lsr;
        lsr;
        ldx #5;                         // kSpriteIdxAlt2
        jsr L_set_arm;
    L_alt_done:

        // 3) Vertical speed: clamp flight_vspeed to [-$300, +$300],
        // dir = 0x18 + (vspeed >> 6).
        lda flight_vspeed + 1;
        bmi L_vs_neg;
        cmp #3;
        bcc L_vs_in;
        lda #$24;
        bne L_vs_set;
    L_vs_neg:
        cmp #$fd;
        bcs L_vs_in;
        lda #$0c;
        bne L_vs_set;
    L_vs_in:
        ldx flight_vspeed;
        stx vec_proj_tmp1;
        asl vec_proj_tmp1;
        rol;
        asl vec_proj_tmp1;
        rol;
        clc;
        adc #$18;
    L_vs_set:
        ldx #0;                         // kSpriteIdxVSpeed
        jsr L_set_arm;

        // 4) Roll dial + one-time init of the four fixed roll/pitch bytes.
        // Closed form for _sprites_roll_to_bitmap[i] (i = roll_angle % 30):
        //   i < 16  -> (16 - i) >> 1   (8, 7, 7, ..., 1, 1, 0)
        //   i >= 16 -> ((15 - i) & 31) >> 1 (15, 15, 14, ..., 9, 9)
        lda roll_angle;
        ora #$80;
        cmp _panel_arm_dir + 2;
        beq L_roll_done;
        sta _panel_arm_dir + 2;
        lda #$ac;                       // kSpriteOffsetX + kSpriteRollPivotX - 12
        sta _sprites_instrument_xy + 4; // Roll.x
        sta _sprites_instrument_xy + 6; // Pitch.x
        lda #$d8;                       // kSpriteOffsetY + kSpriteRollPivotY - 10
        sta _sprites_instrument_xy + 5; // Roll.y
        lda #$68;                       // kSpriteDefLongArmBitmap + 8
        sta _sprites_instrument_idx + 3;// Pitch.idx
        lda roll_angle;
        cmp #30;
        bcc L_r30;
        sbc #30;
    L_r30:
        sta vec_proj_tmp1;
        lda #15;
        cmp vec_proj_tmp1;              // C=1 iff i <= 15
        lda #16;
        sbc vec_proj_tmp1;              // 16 - i (i<=15) or 15 - i (i>=16)
        and #$1f;
        lsr;
        ora #$60;                       // kSpriteDefLongArmBitmap
        sta _sprites_instrument_idx + 2;// Roll.idx
    L_roll_done:

        // 5) Pitch bar: y = 0xD8 - ((int8_t)(flight_cam.front.z >> 2) >> 2).
        // front.z is in [-256, 256], so (lo >> 4) | (hi << 4) is exact >> 4.
        lda flight_cam + 4;
        lsr;
        lsr;
        lsr;
        lsr;
        sta vec_proj_tmp1;
        lda flight_cam + 5;
        asl;
        asl;
        asl;
        asl;
        ora vec_proj_tmp1;
        eor #$ff;
        clc;
        adc #$d9;                       // 0xD8 - A = 0xD9 + ~A
        sta _sprites_instrument_xy + 7; // Pitch.y

        // 6) Throttle: dir = 0x14 + flight_throttle.
        lda flight_throttle;
        clc;
        adc #$14;
        ldx #6;                         // kSpriteIdxThrottle
        jsr L_set_arm;

        // 7) Fuel: dir = 0x18 + ((uint16_t)(flight_fuel >> 8) >> 5).
        lda flight_fuel + 1;
        sta vec_proj_tmp1;
        lda flight_fuel + 2;
        asl vec_proj_tmp1;
        rol;
        asl vec_proj_tmp1;
        rol;
        asl vec_proj_tmp1;
        rol;
        clc;
        adc #$18;
        ldx #7;                         // kSpriteIdxFuel
        jsr L_set_arm;

        // 8) Heading strip, five color-RAM lamps, and ammo counter.
#ifdef __ENABLE_DEBUG__
        lda mem_debug_enabled;
        beq L_dbg_ok;
        rts;
    L_dbg_ok:
#endif
        lda view_state;
        cmp #2;                         // VIEW_CENTER
        beq L_center;
#if defined(__ENABLE_OPPONENT__) || defined(__TARGET_TEST__)
        // Ammo counter in VIEW_LEFT: 2 digits at $F7B0 ($F6B0 + 32*8).
        cmp #1;                         // VIEW_LEFT
        bne L_done_early;
        lda gun_ammo_shown;
        bne L_done_early;
        lda #$f7;
        ldx #2;
        jmp L_ammo_draw;
#endif
    L_done_early:
        rts;

    L_center:
        // Heading bitmap strip at $F5C8 (48 bytes), skipped when unchanged.
        lda flight_true_heading;
        cmp _gfx_heading_bitmap_cache;
        beq L_lamps;
        sta _gfx_heading_bitmap_cache;
        sta vec_proj_tmp1;
        and #$03;
        tax;
        lda kPanelHdgLo, x;
        sta vec_proj_mul_a;
        lda kPanelHdgHi, x;
        sta vec_proj_mul_a + 1;
        lda vec_proj_tmp1;
        lsr;
        lsr;
        clc;
        adc #3;
        cmp #12;
        bcc L_hdg_ok;
        sbc #12;
    L_hdg_ok:
        asl;
        asl;
        asl;
        tay;                            // s = heading_ch << 3 (0, 8, .., 88)
        ldx #0;                         // d = 0..47
    L_hdg_loop:
        lda (vec_proj_mul_a), y;
        sta $f5c8, x;
        iny;
        cpy #96;
        bne L_hdg_nw;
        ldy #0;
    L_hdg_nw:
        inx;
        cpx #48;
        bne L_hdg_loop;

    L_lamps:
        // Nav heading right lamp ($DA6E): heading < 24 ? $0A : $00.
        lda flight_nav_heading;
        cmp #24;
        ldx #$00;
        bcs L_nr_st;
        ldx #$0a;
    L_nr_st:
        stx $da6e;
        // Nav heading left lamp ($DA6D): heading == 0 || heading > 24 ? $0A : $00.
        ldx #$0a;
        cmp #0;
        beq L_nl_st;
        cmp #25;
        bcs L_nl_st;
        ldx #$00;
    L_nl_st:
        stx $da6d;
        // Stall ($DA66), Flap ($DAB6), Gear ($DADE).
#ifdef __FLIGHT_AOA__
        lda flight_stall_warn;
#else
        lda flight_stall;
#endif
        beq L_st_st;
        lda #$0a;
    L_st_st:
        sta $da66;
        lda flight_flap;
        beq L_fl_st;
        lda #$0a;
    L_fl_st:
        sta $dab6;
        lda flight_gear;
        beq L_ge_st;
        lda #$0a;
    L_ge_st:
        sta $dade;

#if defined(__ENABLE_OPPONENT__) || defined(__TARGET_TEST__)
        // Ammo counter in VIEW_CENTER: 3 digits at $F6B0.
        lda gun_ammo_shown;
        bne L_done;
        lda #$f6;
        ldx #3;

    L_ammo_draw:
        sta vec_proj_mul_a + 1;
        lda #$b0;
        sta vec_proj_mul_a;
        stx vec_proj_tmp1;              // digits (2 or 3)
        lda #1;
        sta gun_ammo_shown;
        ldy #0;                         // d = 0..digits*8-1
        sty vec_mul8_b;                 // i = 0..digits-1
    L_ammo_dig:
        ldx vec_mul8_b;
        lda gun_ammo, x;
        asl;
        asl;
        asl;
        tax;
        lda #8;
        sta vec_proj_recip;
    L_ammo_row:
        lda kGfxAmmoDigits, x;
        sta (vec_proj_mul_a), y;
        iny;
        inx;
        dec vec_proj_recip;
        bne L_ammo_row;
        inc vec_mul8_b;
        lda vec_mul8_b;
        cmp vec_proj_tmp1;
        bcc L_ammo_dig;
#endif
    L_done:
        rts;

        // Internal helper: update arm sprite X (0,1,4,5,6,7) to direction A.
        // Preserves vec_proj_mul_a (used across Alt1 -> Alt2).
    L_set_arm:
        and #$1f;
        ora #$80;
        cmp _panel_arm_dir, x;
        beq L_sa_ret;
        sta _panel_arm_dir, x;
        and #$0f;
        sta vec_proj_tmp1;              // arm_idx (0..15)
        asl;
        tay;                            // off = arm_idx << 1 (0..30)
        cpx #1;                         // kSpriteIdxSpeed
        beq L_sa_long;
        cpx #4;                         // kSpriteIdxAlt1
        beq L_sa_long;
        lda kSpriteDefShortArm, y;
        sta vec_mul8_b;                 // px
        lda kSpriteDefShortArm + 1, y;
        ldy #$70;                       // kSpriteDefShortArmBitmap
        bne L_sa_loaded;
    L_sa_long:
        lda kSpriteDefLongArm, y;
        sta vec_mul8_b;                 // px
        lda kSpriteDefLongArm + 1, y;
        ldy #$60;                       // kSpriteDefLongArmBitmap
    L_sa_loaded:
        sta vec_proj_recip;             // py
        tya;
        ora vec_proj_tmp1;              // bitmap + arm_idx
        sta _sprites_instrument_idx, x;
        lda _panel_arm_dir, x;
        and #$10;                       // kSpriteDefArmCount (mirror bit)
        beq L_sa_sub;
        lda #24;                        // 2 * kSpriteDefArmCenterX
        sec;
        sbc vec_mul8_b;
        sta vec_mul8_b;
        lda #20;                        // 2 * kSpriteDefArmCenterY
        sec;
        sbc vec_proj_recip;
        sta vec_proj_recip;
    L_sa_sub:
        txa;
        asl;
        tay;                            // 2 * idx
        lda kSpriteInstrumentPivotX, x;
        sec;
        sbc vec_mul8_b;
        sta _sprites_instrument_xy, y;
        lda kSpriteInstrumentPivotY, x;
        sec;
        sbc vec_proj_recip;
        sta _sprites_instrument_xy + 1, y;
    L_sa_ret:
        rts;
  }
  // clang-format on
}

#endif // __OSCAR64__

#pragma optimize(pop)
