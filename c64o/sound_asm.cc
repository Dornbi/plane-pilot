#include "sound_asm.h"

#include <stdint.h>

// Per-frame path: the outliner (-Oo) would trade cycles for bytes here.
#pragma optimize(push, nooutline)

#if defined(__OSCAR64__) && !defined(__TARGET_TEST__)
#pragma bss(bss2)
#endif
uint8_t sound_shadow[kSoundRegCount];
#if defined(__OSCAR64__) && !defined(__TARGET_TEST__)
#pragma bss(bss)
#endif
uint8_t sound_gen;
uint8_t sound_gen_seen;
volatile uint8_t sound_dirty;
uint8_t _sound_pwm_phase;
uint8_t _sound_rng;
uint8_t _sound_v3_effect;
uint8_t _sound_v3_frames;
uint8_t _sound_stall_phase;
uint8_t _sound_flight_gen_seen;

const uint8_t kMasterVolume[kSoundVolumeSteps] = {0, 7, 15};

// Striped Voice 1 engine frequency table (50.0 .. 105.0 Hz PAL, geometric).
const uint8_t kSoundEngineFreqLo[kMaxThrottle + 1] = {
    0x53, 0x6E, 0x8A, 0xA6, 0xC3, //  0.. 4
    0xE2, 0x01, 0x21, 0x42, 0x65, //  5.. 9
    0x88, 0xAC, 0xD2, 0xF9, 0x21, // 10..14
    0x4A, 0x74, 0xA0, 0xCD, 0xFC, // 15..19
    0x2C, 0x5E, 0x91, 0xC6, 0xFC, // 20..24
};
const uint8_t kSoundEngineFreqHi[kMaxThrottle + 1] = {
    0x03, 0x03, 0x03, 0x03, 0x03, //  0.. 4
    0x03, 0x04, 0x04, 0x04, 0x04, //  5.. 9
    0x04, 0x04, 0x04, 0x04, 0x05, // 10..14
    0x05, 0x05, 0x05, 0x05, 0x05, // 15..19
    0x06, 0x06, 0x06, 0x06, 0x06, // 20..24
};

// Striped Voice 2 wind noise frequency table (1443 .. 5773 shifts/sec).
const uint8_t kSoundWindFreqLo[kSoundWindSteps] = {
    0x00, 0x95, 0x38, 0xEB, //  0.. 3
    0xAF, 0x86, 0x72, 0x75, //  4.. 7
    0x91, 0xC9, 0x1E, 0x95, //  8..11
    0x30, 0xF3, 0xE2, 0x00, // 12..15
};
const uint8_t kSoundWindFreqHi[kSoundWindSteps] = {
    0x06, 0x06, 0x07, 0x07, //  0.. 3
    0x08, 0x09, 0x0A, 0x0B, //  4.. 7
    0x0C, 0x0D, 0x0F, 0x10, //  8..11
    0x12, 0x13, 0x15, 0x18, // 12..15
};

const uint8_t kSoundV3FreqHi[V3_HIT + 1] = {
    0,
    (uint8_t)(kSoundStallFreq >> 8),
    0,
    (uint8_t)(kSoundTouchdownFreq >> 8),
    (uint8_t)(kSoundGearFreq >> 8),
    (uint8_t)(kSoundFlapFreq >> 8),
    (uint8_t)(kSoundGunFreq >> 8),
    (uint8_t)(kSoundHitFreq >> 8),
};
const uint8_t kSoundV3Ctrl[V3_HIT + 1] = {
    0,
    SID_CTRL_RECT | SID_CTRL_GATE,
    SID_CTRL_NOISE | SID_CTRL_GATE,
    SID_CTRL_NOISE | SID_CTRL_GATE,
    SID_CTRL_NOISE | SID_CTRL_GATE,
    SID_CTRL_NOISE | SID_CTRL_GATE,
    SID_CTRL_NOISE | SID_CTRL_GATE,
    SID_CTRL_NOISE | SID_CTRL_GATE,
};
const uint8_t kSoundV3SusRel[V3_HIT + 1] = {
    0,
    kSoundStallSusRel,
    kSoundCrashSusRel,
    kSoundTouchdownSusRel,
    kSoundGearSusRel,
    kSoundFlapSusRel,
    kSoundGunSusRel,
    kSoundHitSusRel,
};
const uint8_t kSoundV3AttDec[V3_HIT + 1] = {
    0,
    kSoundStallAttDec,
    kSoundCrashAttDec,
    kSoundOneShotAttDec,
    kSoundOneShotAttDec,
    kSoundOneShotAttDec,
    kSoundGunAttDec,
    kSoundHitAttDec,
};

const uint8_t kSoundV3Ev[5] = {
    FLIGHT_EV_TOUCHDOWN, FLIGHT_EV_HIT, FLIGHT_EV_GUN, FLIGHT_EV_GEAR,
    FLIGHT_EV_FLAP,
};
const uint8_t kSoundV3Id[5] = {
    V3_TOUCHDOWN, V3_HIT, V3_GUN, V3_GEAR, V3_FLAP,
};
const uint8_t kSoundV3Dur[5] = {
    kSoundTouchdownFrames, kSoundHitFrames, kSoundGunFrames, kSoundGearFrames,
    kSoundFlapFrames,
};

static_assert(((kSoundCrashFreqStart - kSoundCrashFreqEnd) >>
               kSoundCrashShift) == 256 - 32,
              "crash sweep constants no longer match the shift form");
static_assert((1u << kSoundCrashShift) == kSoundCrashFrames,
              "kSoundCrashShift must be log2(kSoundCrashFrames)");
static_assert((kSoundStallPw & 0xFF) == 0,
              "voice 3 PwLo is assumed zero from sound_silence()");
static_assert(kSoundStallFreq < 0x8000 && kSoundTouchdownFreq < 0x8000 &&
                  kSoundGearFreq < 0x8000 && kSoundFlapFreq < 0x8000 &&
                  kSoundGunFreq < 0x8000 && kSoundHitFreq < 0x8000,
              "sound_asm assumes kSoundV3FreqHi entries have bit 7 clear");

#ifdef __OSCAR64__

static_assert(kSoundRegV1 == 0 && kSoundRegV2 == 7 && kSoundRegV3 == 14 &&
                  kSoundVoiceFreqLo == 0 && kSoundVoiceFreqHi == 1 &&
                  kSoundVoicePwLo == 2 && kSoundVoicePwHi == 3 &&
                  kSoundVoiceCtrl == 4 && kSoundVoiceAttDec == 5 &&
                  kSoundVoiceSusRel == 6 && kSoundRegModeVol == 24,
              "sound_asm register offsets");
static_assert(kMaxThrottle == 24 && kSoundWindSteps == 16 &&
                  kSoundWindSpeedShift == 8 && kSoundWindMinSpeed == 0x0080 &&
                  kSoundEngineJitterShift == 5 && kSoundPwmMin == 0x0400 &&
                  kSoundPwmStep == 6 && kSoundPwmJitterMask == 0x7F &&
                  kSoundRngTaps == 0xB8,
              "sound_asm engine/wind/rng constants");
static_assert(kSoundEngineAttDec == 0x10 && kSoundEngineSusRel == 0xF0 &&
                  kSoundWindAttDec == 0x60 && kSoundWindSusRel == 0xA6 &&
                  kSoundStallPw == 0x0800 && kSoundStallOnFrames == 2 &&
                  kSoundStallPeriodFrames == 4 && kSoundCrashFrames == 16 &&
                  kSoundCrashFreqEnd == 0x0200 && kSoundVolumeSteps == 3 &&
                  kSoundVolumeDefault == 2,
              "sound_asm envelope/stall/crash/volume constants");
static_assert(SID_CTRL_RECT == 0x40 && SID_CTRL_NOISE == 0x80 &&
                  SID_CTRL_GATE == 0x01 && FLIGHT_CRASH_ROLL == 2 &&
                  FLIGHT_EV_CRASH == 0x08 && V3_NONE == 0 && V3_STALL == 1 &&
                  V3_CRASH == 2 && sizeof(flight_status) == 1,
              "sound_asm control/status constants");

// Per-frame SID shadow update in 6502 assembly. Reuses vec_asm.cc's main-line
// ZP scratch (never touched by raster IRQs):
//   vec_proj_mul_b + 0 : driver_live (0 or 1)
//   vec_proj_mul_b + 1 : flying (0 or 1)
//   vec_mul_res + 0    : events (0 if flight_gen == _sound_flight_gen_seen)
//   vec_mul_res + 1    : stall_gap (0 or 1)
//   vec_proj_mul_a     : engine base freq (lo/hi)
//   vec_proj_tmp1      : short scratch (tri hi / amp / pick end / crash hi)
void sound_update(void) {
  // clang-format off
  __asm {
        // 1) driver_live and flying flags.
        lda #$00;
        sta vec_proj_mul_b;             // driver_live = 0
        sta vec_proj_mul_b + 1;         // flying = 0
        lda sound_volume;
        beq L_live_done;
        lda flight_paused;
        bne L_live_done;
        inc vec_proj_mul_b;             // driver_live = 1
        lda flight_status;
        cmp #2;                         // FLIGHT_CRASH_ROLL
        bcs L_live_done;
        lda flight_fuel;
        ora flight_fuel + 1;
        ora flight_fuel + 2;
        ora flight_fuel + 3;
        beq L_live_done;
        inc vec_proj_mul_b + 1;         // flying = 1
    L_live_done:

        // 2) Two Galois LFSR draws (x^8 + x^6 + x^5 + x^4 + 1, taps $B8).
        // Y = r_pw, A = _sound_rng = r_pitch.
        lda _sound_rng;
        lsr;
        bcc L_rng1;
        eor #$b8;
    L_rng1:
        tay;                            // Y = r_pw
        lsr;
        bcc L_rng2;
        eor #$b8;
    L_rng2:
        sta _sound_rng;                 // A = r_pitch

        // vec_mul8_b = (r_pitch & 0x1F) << 3 == r_pitch << 3.
        asl;
        asl;
        asl;
        sta vec_mul8_b;

        // 3) Voice 1 (engine): PWM phase + triangle + jitter -> PwLo/PwHi.
        tya;                            // r_pw
        and #$03;
        clc;
        adc #6;                         // kSoundPwmStep
        adc _sound_pwm_phase;
        sta _sound_pwm_phase;
        asl;                            // C = old bit 7, A = phase << 1
        bcc L_tri_ok;
        eor #$fe;
    L_tri_ok:
        ldx #0;
        stx vec_proj_tmp1;
        asl;
        rol vec_proj_tmp1;
        asl;
        rol vec_proj_tmp1;
        asl;
        rol vec_proj_tmp1;              // A = (tri << 3) & $FF, tmp1 = tri >> 5, C = 0
        sta vec_proj_mul_a;             // temp lo
        tya;                            // r_pw
        and #$7f;                       // kSoundPwmJitterMask
        adc vec_proj_mul_a;             // C was 0
        sta sound_shadow + 2;           // V1 PwLo
        lda vec_proj_tmp1;
        adc #$04;                       // kSoundPwmMin >> 8 (result 4..12 <= $0F)
        sta sound_shadow + 3;           // V1 PwHi

        // Engine base freq + jitter: base + ((amp<<1) * ((r_pitch&31)<<3) >> 8) - amp.
        ldx flight_throttle;
        cpx #25;                        // kMaxThrottle + 1
        bcc L_thr_ok;
        ldx #24;                        // kMaxThrottle
    L_thr_ok:
        lda kSoundEngineFreqLo, x;
        sta vec_proj_mul_a;             // base_lo
        lda kSoundEngineFreqHi, x;
        sta vec_proj_mul_a + 1;         // base_hi (3..6)
        sta vec_proj_tmp1;
        lda vec_proj_mul_a;
        asl;
        rol vec_proj_tmp1;
        asl;
        rol vec_proj_tmp1;
        asl;
        rol vec_proj_tmp1;              // vec_proj_tmp1 = amp = base >> 5 (26..55)
        lda vec_proj_tmp1;
        asl;                            // A = amp << 1 (52..110)
        jsr vec_mul8;                   // A = (amp * (r_pitch & 31)) >> 4 (0..106)
        sec;
        sbc vec_proj_tmp1;              // A = delta_lo = prod - amp (-55..+51)
        sta vec_proj_tmp1;              // preserves C
        lda #0;
        sbc #0;                         // delta_hi ($00 if C=1, $FF if C=0)
        tax;
        lda vec_proj_tmp1;
        clc;
        adc vec_proj_mul_a;
        sta sound_shadow + 0;           // V1 FreqLo
        txa;
        adc vec_proj_mul_a + 1;
        sta sound_shadow + 1;           // V1 FreqHi
        lda #$10;                       // kSoundEngineAttDec
        sta sound_shadow + 5;
        lda #$f0;                       // kSoundEngineSusRel
        sta sound_shadow + 6;
        lda #$40;                       // SID_CTRL_RECT
        ora vec_proj_mul_b + 1;         // | SID_CTRL_GATE if flying
        sta sound_shadow + 4;           // V1 Ctrl LAST

        // 4) Voice 2 (wind): speed-clamped table + gate at speed >= 0x0080.
        ldx flight_speed + 1;
        bpl L_wind_pos;
        ldx #0;
        beq L_wind_idx;                 // always taken
    L_wind_pos:
        cpx #16;                        // kSoundWindSteps
        bcc L_wind_idx;
        ldx #15;
    L_wind_idx:
        lda kSoundWindFreqLo, x;
        sta sound_shadow + 7;           // V2 FreqLo
        lda kSoundWindFreqHi, x;
        sta sound_shadow + 8;           // V2 FreqHi
        lda #$60;                       // kSoundWindAttDec
        sta sound_shadow + 12;
        lda #$a6;                       // kSoundWindSusRel
        sta sound_shadow + 13;
        lda #$80;                       // SID_CTRL_NOISE
        ldx vec_proj_mul_b + 1;         // flying
        beq L_v2_ctrl;
        ldx flight_speed + 1;
        bmi L_v2_ctrl;                  // speed < 0
        bne L_v2_on;                    // speed >= 0x0100
        bit flight_speed;               // N = bit 7 of speed_lo
        bpl L_v2_ctrl;                  // speed < 0x0080
    L_v2_on:
        lda #$81;                       // SID_CTRL_NOISE | SID_CTRL_GATE
    L_v2_ctrl:
        sta sound_shadow + 11;          // V2 Ctrl LAST

        // 5) Voice 3 arbitration: consume events once per flight_gen.
        lda #0;
        sta vec_mul_res;                // events = 0
        sta vec_mul_res + 1;            // stall_gap = 0
        lda flight_gen;
        cmp _sound_flight_gen_seen;
        beq L_ev_done;
        sta _sound_flight_gen_seen;
        lda flight_events;
        sta vec_mul_res;                // events = flight_events
    L_ev_done:
        lda _sound_v3_frames;
        beq L_v3_f0;
        dec _sound_v3_frames;
    L_v3_f0:
        lda vec_proj_mul_b;             // driver_live
        beq L_v3_clear;

        // Crash event preempts everything when driver_live.
        lda vec_mul_res;                // events
        and #$08;                       // FLIGHT_EV_CRASH
        beq L_chk_crash_active;
        lda #2;                         // V3_CRASH
        sta _sound_v3_effect;
        lda #16;                        // kSoundCrashFrames
        sta _sound_v3_frames;
        bne L_v3_retrig;                // always taken

    L_v3_clear:
        lda #0;
        sta _sound_v3_effect;
        sta _sound_v3_frames;
        beq L_v3_emit;                  // always taken

    L_chk_crash_active:
        lda _sound_v3_effect;
        cmp #2;                         // V3_CRASH
        bne L_chk_flying;
        lda _sound_v3_frames;
        beq L_v3_clear;
        lda flight_status;
        cmp #2;                         // FLIGHT_CRASH_ROLL
        bcc L_v3_clear;                 // R restart cleared crash
        bcs L_v3_emit;                  // always taken: keep crash running

    L_chk_flying:
        lda vec_proj_mul_b + 1;         // flying
        beq L_v3_clear;

        // Touchdown and hit outrank stall and any running one-shot.
        lda vec_mul_res;
        beq L_chk_active_os;
        ldx #0;
        lda #2;
        jsr L_pick_v3;
        bcs L_v3_retrig;

    L_chk_active_os:
        // Running non-stall one-shot keeps the voice until frames == 0.
        lda _sound_v3_frames;
        beq L_chk_stall;
        lda _sound_v3_effect;
        cmp #1;                         // V3_STALL
        bne L_v3_emit;

    L_chk_stall:
#ifdef __FLIGHT_AOA__
        lda flight_stall_warn;
#else
        lda flight_stall;
#endif
        beq L_chk_low_ev;
        lda _sound_v3_effect;
        cmp #1;                         // V3_STALL
        beq L_stall_keep;
        lda #0;
        sta _sound_stall_phase;
        lda #1;                         // V3_STALL
        sta _sound_v3_effect;
    L_stall_keep:
        lda _sound_stall_phase;
        bne L_stall_no_trig;
        inc sound_gen;
    L_stall_no_trig:
        cmp #2;                         // kSoundStallOnFrames
        bcc L_stall_no_gap;
        inc vec_mul_res + 1;            // stall_gap = 1
    L_stall_no_gap:
        clc;
        adc #1;
        and #$03;                       // wrap mod kSoundStallPeriodFrames (4)
        sta _sound_stall_phase;
        jmp L_v3_emit;

    L_chk_low_ev:
        // Gun, gear, flap sit under stall.
        lda vec_mul_res;
        beq L_chk_v3_end;
        ldx #2;
        lda #5;
        jsr L_pick_v3;
        bcs L_v3_retrig;
    L_chk_v3_end:
        lda _sound_v3_frames;
        bne L_v3_emit;
        sta _sound_v3_effect;           // V3_NONE (0)
        beq L_v3_emit;                  // always taken

    L_v3_retrig:
        inc sound_gen;

        // 6) Voice 3 shadow writes (Ctrl LAST).
    L_v3_emit:
        ldx _sound_v3_effect;
        cpx #2;                         // V3_CRASH
        beq L_v3_crash;
        lda #0;
        ldy kSoundV3FreqHi, x;
        bpl L_v3_f_st;                  // always taken (all V3 FreqHi < $80)

    L_v3_crash:
        // m = 7 * _sound_v3_frames + 16 (16..128), freq = (m << 8) >> 3.
        lda _sound_v3_frames;
        asl;
        asl;
        asl;
        sec;
        sbc _sound_v3_frames;
        clc;
        adc #16;                        // kSoundCrashFreqEnd >> 5
        lsr;
        sta vec_proj_tmp1;
        lda #0;
        ror;
        lsr vec_proj_tmp1;
        ror;
        lsr vec_proj_tmp1;
        ror;
        ldy vec_proj_tmp1;
    L_v3_f_st:
        sta sound_shadow + 14;          // V3 FreqLo
        sty sound_shadow + 15;          // V3 FreqHi
        lda #0;
        cpx #1;                         // V3_STALL
        bne L_v3_pwhi;
        lda #$08;                       // kSoundStallPw >> 8
    L_v3_pwhi:
        sta sound_shadow + 17;          // V3 PwHi
        lda kSoundV3AttDec, x;
        sta sound_shadow + 19;          // V3 AttDec
        lda kSoundV3SusRel, x;
        sta sound_shadow + 20;          // V3 SusRel
        lda vec_mul_res + 1;            // stall_gap
        beq L_v3_ctrl_tbl;
        lda #$40;                       // SID_CTRL_RECT
        bne L_v3_ctrl_st;               // always taken
    L_v3_ctrl_tbl:
        lda kSoundV3Ctrl, x;
    L_v3_ctrl_st:
        sta sound_shadow + 18;          // V3 Ctrl LAST

        // 7) Master volume LAST, then mark shadow dirty.
        txa;                            // effect
        ora vec_proj_mul_b + 1;         // | flying
        beq L_vol_st;                   // both 0 -> A = 0
        ldx sound_volume;
        cpx #3;                         // kSoundVolumeSteps
        bcc L_vol_ok;
        ldx #2;                         // kSoundVolumeDefault
    L_vol_ok:
        lda kMasterVolume, x;
    L_vol_st:
        sta sound_shadow + 24;          // ModeVol LAST
        lda #1;
        sta sound_dirty;
        rts;

        // Local helper: scan kSoundV3Ev[X .. A-1] against non-zero vec_mul_res.
        // Returns C=1 on match (updating _sound_v3_effect/_frames), else C=0.
    L_pick_v3:
        sta vec_proj_tmp1;
    L_pick_lp:
        lda vec_mul_res;
        and kSoundV3Ev, x;
        bne L_pick_hit;
        inx;
        cpx vec_proj_tmp1;
        bcc L_pick_lp;
    L_pick_no:
        clc;
        rts;
    L_pick_hit:
        lda kSoundV3Id, x;
        sta _sound_v3_effect;
        lda kSoundV3Dur, x;
        sta _sound_v3_frames;
        sec;
        rts;
  }
  // clang-format on
}

#endif // __OSCAR64__

#pragma optimize(pop)
