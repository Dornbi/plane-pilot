#include "flight_asm.h"

#include <stddef.h>
#include <stdint.h>

#if defined(__OSCAR64__) && !defined(__FLIGHT_AOA__)

// Pin struct layouts and constants embedded as literals in the 6502 assembly.
static_assert(offsetof(mat3_t, front) == 0 && offsetof(mat3_t, left) == 6 &&
                  offsetof(mat3_t, up) == 12,
              "mat3_t row offsets pinned by flight_asm.cc");
static_assert(offsetof(vec3_t, x) == 0 && offsetof(vec3_t, y) == 2 &&
                  offsetof(vec3_t, z) == 4,
              "vec3_t component offsets pinned by flight_asm.cc");
static_assert(kMaxSpeed == 0x0F00,
              "kMaxSpeed pinned by flight_asm.cc");
static_assert(kFlightAsmMinEyeZ == 0x2000,
              "kFlightAsmMinEyeZ pinned by flight_asm.cc");
static_assert(kFlightAsmTrimLift == 0x1000,
              "kFlightAsmTrimLift pinned by flight_asm.cc");
static_assert(kFlightAsmStallWithoutFlaps == 0x0400 &&
                  kFlightAsmStallWithFlaps == 0x0340,
              "stall speeds pinned by flight_asm.cc");
static_assert(kFlightAsmMaxStallPitchZ == 224,
              "kFlightAsmMaxStallPitchZ pinned by flight_asm.cc");

// Arithmetic right shift of signed 16-bit vec_mul_res by (X + flight_step_shift)
// bits. Entry: X = extra right-shift count (0, 3, 5, or 6). Clobbers A, X.
// Preserves Y, vec_v, vec_sx, vec_sy.
static __noinline void _flight_asm_step_asr_x(void) {
  // clang-format off
  __asm {
        lda vec_mul_res;
        ora vec_mul_res + 1;
        beq L_fs_done;
        txa;
        clc;
        adc flight_step_shift;
        beq L_fs_done;
        tax;
    L_fs_loop:
        lda vec_mul_res + 1;
        cmp #$80;
        ror vec_mul_res + 1;
        ror vec_mul_res;
        dex;
        bne L_fs_loop;
    L_fs_done:
  }
  // clang-format on
}

// Loads flight_cam[X..X+1] into vec_proj_mul_a and tail-calls vec_mul_ab.
static __noinline void _flight_asm_mul_cam_a(void) {
  // clang-format off
  __asm {
        lda flight_cam, x;
        sta vec_proj_mul_a;
        lda flight_cam + 1, x;
        sta vec_proj_mul_a + 1;
        jmp vec_mul_ab;
  }
  // clang-format on
}

// Subtracts unsigned byte A from 16-bit flight_speed. Clobbers A.
static __noinline void _flight_asm_sub_speed_u8(void) {
  // clang-format off
  __asm {
        eor #$ff;
        sec;
        adc flight_speed;
        sta flight_speed;
        bcs L_ssu8_done;
        dec flight_speed + 1;
    L_ssu8_done:
  }
  // clang-format on
}

// Subtracts signed 16-bit vec_mul_res from flight_speed. Clobbers A.
static __noinline void _flight_asm_sub_speed_mr(void) {
  // clang-format off
  __asm {
        sec;
        lda flight_speed;
        sbc vec_mul_res;
        sta flight_speed;
        lda flight_speed + 1;
        sbc vec_mul_res + 1;
        sta flight_speed + 1;
  }
  // clang-format on
}

// Adds signed 16-bit _flight_step_s(vec_mul_res) to the 32-bit int32_t at
// address (A = lo, X = hi). Uses vec_proj_mul_a as ZP pointer. Clobbers A,X,Y.
static __noinline void _flight_asm_eye_add(void) {
  // clang-format off
  __asm {
        sta vec_proj_mul_a;
        stx vec_proj_mul_a + 1;
        ldx #0;
        jsr _flight_asm_step_asr_x;
        ldy #0;
        clc;
        lda (vec_proj_mul_a), y;
        adc vec_mul_res;
        sta (vec_proj_mul_a), y;
        iny;
        lda (vec_proj_mul_a), y;
        adc vec_mul_res + 1;
        sta (vec_proj_mul_a), y;
        // Sign-extend vec_mul_res + 1 into bytes 2 and 3 without disturbing C.
        lda #0;
        bit vec_mul_res + 1;
        bpl L_ea_pos;
        lda #$ff;
    L_ea_pos:
        tax;
        iny;
        adc (vec_proj_mul_a), y;
        sta (vec_proj_mul_a), y;
        iny;
        txa;
        adc (vec_proj_mul_a), y;
        sta (vec_proj_mul_a), y;
  }
  // clang-format on
}

// Horizontal eye move helper: adds _flight_step_s(mul(flight_cam[X], vec_sx))
// to the 32-bit int32_t at address (A = lo, Y = hi).
static __noinline void _flight_asm_move_xy(void) {
  // clang-format off
  __asm {
        pha;
        tya;
        pha;
        lda vec_sx;
        sta vec_proj_mul_b;
        lda vec_sx + 1;
        sta vec_proj_mul_b + 1;
        jsr _flight_asm_mul_cam_a;
        pla;
        tax;
        pla;
        jmp _flight_asm_eye_add;
  }
  // clang-format on
}

// Rare stall dead-spot rotation (front.z > 224). Preserves vec_sx (sink_penalty).
static __noinline void _flight_asm_stall_rotate(void) {
  vec_rotate_step(flight_cam.up.z < 0 ? VEC_STEP_PITCH_UP : VEC_STEP_PITCH_DOWN,
                  &flight_cam);
}

// Rare ground wing-leveling rebuild (left.z != 0).
static __noinline void _flight_asm_ground_level_wings(void) {
  flight_cam.left.x = -flight_cam.front.y;
  flight_cam.left.y = flight_cam.front.x;
  flight_cam.left.z = 0;
  vec_cross(&flight_cam.front, &flight_cam.left, &flight_cam.up);
  model_need_normalize = true;
}

// ZP scratch layout (all preserved across vec_mul_ab; vec_sx also preserved
// across _flight_asm_stall_rotate):
//   vec_v + 0     = whole_step (0 or 1)
//   vec_v + 1     = alt_penalty (0..128)
//   vec_v + 2..3  = density = 256 - alt_penalty
//   vec_v + 4..5  = speed_sqr = vec_fastsqr8p8(flight_speed)
//   vec_sx + 0..1 = sink_penalty (then fspeed = flight_speed << 1 during move)
//   vec_sy + 0..1 = stall_speed
//   vec_proj_tmp1 = return value (on_floor: 0 or 1)
__noinline uint8_t _flight_physics_move_asm(uint8_t whole_step) {
  // clang-format off
  __asm {
        lda whole_step;
        sta vec_v;

        // --- Altitude density decay: alt_penalty in 0..128, density = 256 - alt_penalty ---
        lda flight_eye_z + 3;
        bne L_alt_max;
        lda flight_eye_z + 2;
        cmp #$08;
        bcs L_alt_ge8;
        lda #0;
        beq L_alt_store;
    L_alt_ge8:
        cmp #$10;
        bcs L_alt_max;
        and #$07;
        asl;
        asl;
        asl;
        asl;
        sta vec_v + 1;
        lda flight_eye_z + 1;
        lsr;
        lsr;
        lsr;
        lsr;
        ora vec_v + 1;
        sta vec_v + 1;
        bpl L_alt_dens;         // 0..127 always has N=0
    L_alt_max:
        lda #$80;
    L_alt_store:
        sta vec_v + 1;
    L_alt_dens:
        sec;
        lda #0;
        sbc vec_v + 1;
        sta vec_v + 2;          // density lo
        lda #1;
        sbc #0;
        sta vec_v + 3;          // density hi

        // --- speed_sqr = vec_fastsqr8p8(flight_speed) ---
        lda flight_speed;
        sta vec_proj_mul_a;
        sta vec_proj_mul_b;
        lda flight_speed + 1;
        sta vec_proj_mul_a + 1;
        sta vec_proj_mul_b + 1;
        jsr vec_mul_ab;
        lda vec_mul_res;
        sta vec_v + 4;
        lda vec_mul_res + 1;
        sta vec_v + 5;

        // --- Drag: drag = _flight_step_s(speed_sqr >> 1) (unsigned >> 1) ---
        lsr vec_mul_res + 1;
        ror vec_mul_res;
        ldx #0;
        jsr _flight_asm_step_asr_x;
        // flight_speed -= (drag >> 9) + (gear ? drag>>11 : 0) + (flap ? drag>>11 : 0)
        lda vec_mul_res + 1;
        lsr;                    // drag >> 9
        tay;
        lsr;
        lsr;                    // extra = drag >> 11
        sta vec_proj_tmp1;
        tya;
        ldx flight_gear;
        beq L_no_gear_drag;
        clc;
        adc vec_proj_tmp1;
    L_no_gear_drag:
        ldx flight_flap;
        beq L_no_flap_drag;
        clc;
        adc vec_proj_tmp1;
    L_no_flap_drag:
        jsr _flight_asm_sub_speed_u8;

        // --- Bank drag: if (!model_on_ground) flight_speed -= _step_s(sqr(left.z)) >> 5 ---
        lda model_on_ground;
        bne L_skip_bank_drag;
        lda flight_cam + 10;
        ora flight_cam + 11;
        beq L_skip_bank_drag;
        lda flight_cam + 10;
        sta vec_proj_mul_b;
        lda flight_cam + 11;
        sta vec_proj_mul_b + 1;
        ldx #10;
        jsr _flight_asm_mul_cam_a;
        ldx #5;
        jsr _flight_asm_step_asr_x;
        jsr _flight_asm_sub_speed_mr;
    L_skip_bank_drag:

        // --- Gravity: flight_speed -= _flight_step_s(flight_cam.front.z) >> 3 ---
        lda flight_cam + 4;
        ora flight_cam + 5;
        beq L_skip_grav;
        lda flight_cam + 4;
        sta vec_mul_res;
        lda flight_cam + 5;
        sta vec_mul_res + 1;
        ldx #3;
        jsr _flight_asm_step_asr_x;
        jsr _flight_asm_sub_speed_mr;
    L_skip_grav:

        // --- Throttle thrust: if (flight_fuel > 0) speed += _step_s(mul(density, throttle)) ---
        lda flight_fuel;
        ora flight_fuel + 1;
        ora flight_fuel + 2;
        ora flight_fuel + 3;
        beq L_no_thrust;
        lda flight_throttle;
        beq L_no_thrust;
        sta vec_proj_mul_b;
        lda #0;
        sta vec_proj_mul_b + 1;
        lda vec_v + 2;          // density in a (+256 fast path below 0x080000)
        sta vec_proj_mul_a;
        lda vec_v + 3;
        sta vec_proj_mul_a + 1;
        jsr vec_mul_ab;
        ldx #0;
        jsr _flight_asm_step_asr_x;
        clc;
        lda flight_speed;
        adc vec_mul_res;
        sta flight_speed;
        lda flight_speed + 1;
        adc vec_mul_res + 1;
        sta flight_speed + 1;
    L_no_thrust:

        // --- Lift, sink_penalty, deficit drag, stall_speed / ground friction ---
        lda #0;
        sta vec_sx;             // sink_penalty = 0
        sta vec_sx + 1;
        sta vec_sy;             // stall_speed = 0
        sta vec_sy + 1;
        lda model_on_ground;
        beq L_air_lift;

        // Ground mode: if (whole_step && flight_throttle == 0 && flight_speed > 0) speed -= 2
        lda vec_v;
        beq L_gnd_done;
        lda flight_throttle;
        bne L_gnd_done;
        lda flight_speed + 1;
        bmi L_gnd_done;
        ora flight_speed;
        beq L_gnd_done;
        lda #2;
        jsr _flight_asm_sub_speed_u8;
    L_gnd_done:
        jmp L_clamp_speed;

    L_air_lift:
        // raw_lift = vec_fastmul8p8(flight_cam.up.z, (int16_t)(speed_sqr >> 2))
        lda vec_v + 4;
        sta vec_proj_mul_b;
        lda vec_v + 5;
        lsr;
        ror vec_proj_mul_b;
        lsr;
        ror vec_proj_mul_b;
        sta vec_proj_mul_b + 1;
        ldx #16;                // up.z in a (<= 256 fast path)
        jsr _flight_asm_mul_cam_a;

        // lift = vec_fastmul8p8(density, raw_lift)
        lda vec_mul_res;
        sta vec_proj_mul_b;
        lda vec_mul_res + 1;
        sta vec_proj_mul_b + 1;
        lda vec_v + 2;          // density in a (+256 fast path below 0x080000)
        sta vec_proj_mul_a;
        lda vec_v + 3;
        sta vec_proj_mul_a + 1;
        jsr vec_mul_ab;

        // if (flight_flap) lift += lift >> 1 (signed)
        lda flight_flap;
        beq L_no_flap_lift;
        lda vec_mul_res + 1;
        cmp #$80;
        ror;
        tax;
        lda vec_mul_res;
        ror;
        clc;
        adc vec_mul_res;
        sta vec_mul_res;
        txa;
        adc vec_mul_res + 1;
        sta vec_mul_res + 1;
    L_no_flap_lift:

        // deficit = kFlightTrimLift (0x1000) - lift
        sec;
        lda #$00;
        sbc vec_mul_res;
        sta vec_mul_res;
        lda #$10;
        sbc vec_mul_res + 1;
        sta vec_mul_res + 1;
        bmi L_no_deficit;
        ora vec_mul_res;
        beq L_no_deficit;

        // sink_penalty = deficit >> 4 (deficit > 0)
        ldx #4;
    L_def_shr4:
        lsr vec_mul_res + 1;
        ror vec_mul_res;
        dex;
        bne L_def_shr4;
        lda vec_mul_res;
        sta vec_sx;
        lda vec_mul_res + 1;
        sta vec_sx + 1;

        // flight_speed -= _flight_step_s(deficit) >> 10 == (deficit >> 4) >> (6 + shift)
        ldx #6;
        jsr _flight_asm_step_asr_x;
        lda vec_mul_res;
        jsr _flight_asm_sub_speed_u8;

    L_no_deficit:
        // base_stall_speed: !flap -> $0400; flap && up.z >= 0 -> $0340; flap && up.z < 0 -> $0480
        lda #$00;
        ldx #$04;
        ldy flight_flap;
        beq L_base_stall;
        ldy flight_cam + 17;
        bmi L_stall_inv;
        lda #$40;
        ldx #$03;
        bne L_base_stall;
    L_stall_inv:
        lda #$80;
    L_base_stall:
        // stall_speed = base_stall_speed + ((uint16_t)alt_penalty << 1)
        ldy #0;
        asl vec_v + 1;          // low byte; C=1 iff alt_penalty == 128
        bcc L_ap_no_hi;
        iny;
    L_ap_no_hi:
        sty vec_proj_tmp1;
        clc;
        adc vec_v + 1;
        sta vec_sy;
        txa;
        adc vec_proj_tmp1;
        sta vec_sy + 1;

        // --- Clamp flight_speed to [0, kMaxSpeed (0x0F00)] ---
    L_clamp_speed:
        lda flight_speed + 1;
        bpl L_spd_nonneg;
        lda #0;
        sta flight_speed;
        sta flight_speed + 1;
        beq L_spd_clamped;
    L_spd_nonneg:
        cmp #$0f;
        bcc L_spd_clamped;
        lda #0;
        sta flight_speed;
        lda #$0f;
        sta flight_speed + 1;
    L_spd_clamped:

        // --- Stall test & break ---
        lda flight_speed;
        cmp vec_sy;
        lda flight_speed + 1;
        sbc vec_sy + 1;
        lda #0;
        bcs L_not_stalled;
        lda #1;
    L_not_stalled:
        sta flight_stall;
        beq L_after_stall;

        // if (flight_cam.front.z > 224)
        lda flight_cam + 5;
        bmi L_stall_direct;
        bne L_stall_rot;
        lda flight_cam + 4;
        cmp #$e1;               // >= 225 <=> > 224
        bcc L_stall_direct;
    L_stall_rot:
        jsr _flight_asm_stall_rotate;
        jmp L_stall_norm;

    L_stall_direct:
        sec;
        lda vec_sy;
        sbc flight_speed;
        sta vec_mul_res;
        lda vec_sy + 1;
        sbc flight_speed + 1;
        sta vec_mul_res + 1;
        ldx #5;
        jsr _flight_asm_step_asr_x;
        lda vec_mul_res;
        bne L_stall_s_ok;
        lda vec_v;              // whole_step ? 1 : 0
    L_stall_s_ok:
        eor #$ff;
        sec;
        adc flight_cam + 4;
        sta flight_cam + 4;
        bcs L_fz_noborrow;
        dec flight_cam + 5;
    L_fz_noborrow:
        // if (flight_cam.front.z < -256) flight_cam.front.z = -256 ($FF00)
        lda flight_cam + 5;
        bpl L_stall_norm;
        cmp #$ff;
        beq L_stall_norm;
        lda #0;
        sta flight_cam + 4;
        lda #$ff;
        sta flight_cam + 5;
    L_stall_norm:
        lda #1;
        sta model_need_normalize;
    L_after_stall:

        // --- Vertical speed & ground attitude constraints ---
        lda model_on_ground;
        bne L_ground_att;
        // flight_vspeed = vec_fastmul8p8(flight_cam.front.z, flight_speed) - sink_penalty
        lda flight_speed;
        sta vec_proj_mul_b;
        lda flight_speed + 1;
        sta vec_proj_mul_b + 1;
        ldx #4;                 // front.z in a (<= 256 fast path)
        jsr _flight_asm_mul_cam_a;
        sec;
        lda vec_mul_res;
        sbc vec_sx;
        sta flight_vspeed;
        lda vec_mul_res + 1;
        sbc vec_sx + 1;
        sta flight_vspeed + 1;
        jmp L_move_fwd;

    L_ground_att:
        lda flight_cam + 5;
        bpl L_gnd_fz_ok;
        lda #0;
        sta flight_cam + 4;
        sta flight_cam + 5;
        lda #1;
        sta model_need_normalize;
    L_gnd_fz_ok:
        lda flight_cam + 10;
        ora flight_cam + 11;
        beq L_gnd_lz_ok;
        jsr _flight_asm_ground_level_wings;
    L_gnd_lz_ok:
        lda #0;
        sta flight_vspeed;
        sta flight_vspeed + 1;

        // --- _flight_move_forward(flight_speed << 1, flight_vspeed) ---
    L_move_fwd:
        lda flight_speed;
        ora flight_speed + 1;
        beq L_move_z;
        lda flight_speed;
        asl;
        sta vec_sx;             // fspeed lo
        lda flight_speed + 1;
        rol;
        sta vec_sx + 1;         // fspeed hi

        // X axis
        ldx #0;
        lda #<flight_eye_x;
        ldy #>flight_eye_x;
        jsr _flight_asm_move_xy;

        // Y axis
        ldx #2;
        lda #<flight_eye_y;
        ldy #>flight_eye_y;
        jsr _flight_asm_move_xy;

        // Z axis
    L_move_z:
        lda flight_vspeed;
        ora flight_vspeed + 1;
        beq L_floor_chk;
        lda flight_vspeed;
        sta vec_mul_res;
        lda flight_vspeed + 1;
        sta vec_mul_res + 1;
        lda #<flight_eye_z;
        ldx #>flight_eye_z;
        jsr _flight_asm_eye_add;

        // Floor test & clamp: flight_eye_z <= 0x00002000 (signed int32_t)
    L_floor_chk:
        lda flight_eye_z + 3;
        bmi L_hit_floor;
        bne L_above_floor;
        lda flight_eye_z + 2;
        bne L_above_floor;
        lda flight_eye_z + 1;
        cmp #$20;
        bcc L_hit_floor;
        bne L_above_floor;
        lda flight_eye_z;
        beq L_hit_floor;
    L_above_floor:
        lda #0;
        beq L_ret_floor;
    L_hit_floor:
        lda #0;
        sta flight_eye_z;
        sta flight_eye_z + 2;
        sta flight_eye_z + 3;
        lda #$20;
        sta flight_eye_z + 1;
        lda #1;
    L_ret_floor:
        sta vec_proj_tmp1;
  }
  // clang-format on
  return vec_proj_tmp1;
}

__noinline uint8_t _flight_fuel_step_asm(uint8_t fuel_consumption) {
  // clang-format off
  __asm {
        // Test flight_fuel > fuel_consumption (uint32_t vs uint8_t).
        lda flight_fuel + 1;
        ora flight_fuel + 2;
        ora flight_fuel + 3;
        bne L_fuel_sub;
        lda fuel_consumption;
        cmp flight_fuel;
        bcc L_fuel_sub;         // fc < lo => flight_fuel > fc

        // flight_fuel <= fuel_consumption: ran_dry = (flight_fuel != 0), zero tank & throttle.
        lda flight_fuel;
        beq L_fuel_dry_zero;
        lda #1;
    L_fuel_dry_zero:
        sta vec_proj_tmp1;
        lda #0;
        sta flight_fuel;
        sta flight_fuel + 1;
        sta flight_fuel + 2;
        sta flight_fuel + 3;
        sta flight_throttle;
        beq L_fuel_done;

    L_fuel_sub:
        sec;
        lda flight_fuel;
        sbc fuel_consumption;
        sta flight_fuel;
        bcs L_fuel_ok;
        lda flight_fuel + 1;
        bne L_fuel_dec1;
        lda flight_fuel + 2;
        bne L_fuel_dec2;
        dec flight_fuel + 3;
    L_fuel_dec2:
        dec flight_fuel + 2;
    L_fuel_dec1:
        dec flight_fuel + 1;
    L_fuel_ok:
        lda #0;
        sta vec_proj_tmp1;
    L_fuel_done:
  }
  // clang-format on
  return vec_proj_tmp1;
}

__noinline int8_t _flight_turn_rot_asm(void) {
  // clang-format off
  __asm {
        lda model_on_ground;
        bne L_rot_zero;
        lda flight_cam + 10;
        ora flight_cam + 11;
        beq L_rot_zero;
        lda flight_cam + 10;
        sta vec_mul_res;
        lda flight_cam + 11;
        sta vec_mul_res + 1;
        ldx #5;
        jsr _flight_asm_step_asr_x;
        lda vec_mul_res;
        jmp L_rot_store;
    L_rot_zero:
        lda #0;
    L_rot_store:
        sta vec_proj_tmp1;
  }
  // clang-format on
  return (int8_t)vec_proj_tmp1;
}

#endif // defined(__OSCAR64__) && !defined(__FLIGHT_AOA__)
