#ifndef SOUND_ASM_H
#define SOUND_ASM_H

#include <stdint.h>

#include "bool.h"
#include "sid.h"
#include "vec.h"
#ifndef __TARGET_TEST__
#include "flight.h"
#include "sound.h"
#else
static const uint8_t kSoundRegCount = 25;
static const uint8_t kSoundRegV1 = 0;
static const uint8_t kSoundRegV2 = 7;
static const uint8_t kSoundRegV3 = 14;
static const uint8_t kSoundVoiceFreqLo = 0;
static const uint8_t kSoundVoiceFreqHi = 1;
static const uint8_t kSoundVoicePwLo = 2;
static const uint8_t kSoundVoicePwHi = 3;
static const uint8_t kSoundVoiceCtrl = 4;
static const uint8_t kSoundVoiceAttDec = 5;
static const uint8_t kSoundVoiceSusRel = 6;
static const uint8_t kSoundRegModeVol = 24;
static const uint8_t kSoundEngineJitterShift = 5;
static const uint8_t kSoundPwmJitterMask = 0x7F;
static const uint8_t kSoundVolumeSteps = 3;
static const uint8_t kSoundVolumeDefault = 2;
static const uint8_t kMaxThrottle = 0x18;
#ifndef FLIGHT_ASM_H
static const uint16_t kMaxSpeed = 0x0F00;
#endif
#define FLIGHT_EV_TOUCHDOWN 0x01
#define FLIGHT_EV_GEAR 0x02
#define FLIGHT_EV_FLAP 0x04
#define FLIGHT_EV_CRASH 0x08
#define FLIGHT_EV_GUN 0x10
#define FLIGHT_EV_HIT 0x20
enum FlightStatus {
  FLIGHT_ONGOING = 0,
  FLIGHT_MISSION_COMPLETED,
  FLIGHT_CRASH_ROLL,
};
extern bool flight_paused;
extern enum FlightStatus flight_status;
extern int16_t flight_speed;
extern uint8_t flight_throttle;
extern uint32_t flight_fuel;
extern uint8_t flight_stall;
extern uint8_t flight_events;
extern uint8_t flight_gen;
extern uint8_t sound_volume;
#endif

// The assembly half of sound.cc, in sound_asm.cc: shared SID register lookup
// tables, shadow/voice state, and the per-frame 6502 sound_update() driver
// (Galois LFSR roughness, PWM triangle sweep, quarter-square engine pitch
// jitter via vec_mul8, wind frequency/gating, Voice 3 priority arbitration &
// crash sweep, and torn-read-safe shadow writes with Ctrl last per voice and
// ModeVol + sound_dirty last of all). sound.cc keeps the pure C counterpart
// under #ifndef __OSCAR64__ for host sound_test.cc (with SHADOW_OBSERVE()),
// and test/target_test.cc (in target_planes_test.prg) verifies 6510
// equivalence against a pure C reference.

// Voice 1 (engine) parameters.
static const uint8_t kSoundEngineAttDec = 0x10; // attack 1, decay 0
static const uint8_t kSoundEngineSusRel = 0xF0; // sustain 15, release 0
static const uint16_t kSoundPwmMin = 0x0400;
static const uint8_t kSoundPwmStep = 6;

// Voice 2 (wind) parameters.
static const uint8_t kSoundWindSpeedShift = 8;
static const uint8_t kSoundWindSteps = (kMaxSpeed >> kSoundWindSpeedShift) + 1;
static const uint16_t kSoundWindMinSpeed = 0x0080;
static const uint8_t kSoundWindAttDec = 0x60; // attack 6, decay 0
static const uint8_t kSoundWindSustain = 10;
static const uint8_t kSoundWindSusRel = (kSoundWindSustain << 4) | 0x06;

// Voice 3 (stall warning, crash, one-shots) parameters.
static const uint16_t kSoundStallFreq = 0x3800;
static const uint16_t kSoundStallPw = 0x0800;
static const uint8_t kSoundStallAttDec = 0x00;
static const uint8_t kSoundStallSusRel = 0xF3;
static const uint8_t kSoundStallOnFrames = 2;
static const uint8_t kSoundStallPeriodFrames = 4;

static const uint8_t kSoundOneShotAttDec = 0x00;
static const uint16_t kSoundTouchdownFreq = 0x1800;
static const uint16_t kSoundGearFreq = 0x0900;
static const uint16_t kSoundFlapFreq = 0x0C00;
static const uint8_t kSoundTouchdownSusRel = 0xF5;
static const uint8_t kSoundGearSusRel = 0xF6;
static const uint8_t kSoundFlapSusRel = 0xF4;
static const uint8_t kSoundTouchdownFrames = 3;
static const uint8_t kSoundGearFrames = 5;
static const uint8_t kSoundFlapFrames = 4;

static const uint16_t kSoundGunFreq = 0x0800;
static const uint16_t kSoundHitFreq = 0x2400;
static const uint8_t kSoundGunAttDec = 0x05;
static const uint8_t kSoundGunSusRel = 0x00;
static const uint8_t kSoundHitAttDec = 0x26;
static const uint8_t kSoundHitSusRel = 0x10;
static const uint8_t kSoundGunFrames = 1;
static const uint8_t kSoundHitFrames = 2;

static const uint8_t kSoundCrashFrames = 16;
static const uint8_t kSoundCrashShift = 4;
static const uint16_t kSoundCrashFreqStart = 0x1000;
static const uint16_t kSoundCrashFreqEnd = 0x0200;
static const uint8_t kSoundCrashAttDec = 0x00;
static const uint8_t kSoundCrashSusRel = 0xF9;

static const uint8_t kSoundRngTaps = 0xB8;
static const uint8_t kSoundRngSeed = 0xA5;

// Which effect owns voice 3 right now.
enum Voice3Effect {
  V3_NONE = 0,
  V3_STALL,
  V3_CRASH,
  V3_TOUCHDOWN,
  V3_GEAR,
  V3_FLAP,
  V3_GUN,
  V3_HIT,
};

// Shared state (defined in sound_asm.cc).
extern uint8_t sound_shadow[kSoundRegCount];
extern uint8_t sound_gen;
extern uint8_t sound_gen_seen;
extern volatile uint8_t sound_dirty;
extern uint8_t _sound_pwm_phase;
extern uint8_t _sound_rng;
extern uint8_t _sound_v3_effect;
extern uint8_t _sound_v3_frames;
extern uint8_t _sound_stall_phase;
extern uint8_t _sound_flight_gen_seen;

// Shared lookup tables (defined in sound_asm.cc).
extern const uint8_t kMasterVolume[kSoundVolumeSteps];
extern const uint8_t kSoundEngineFreqLo[kMaxThrottle + 1];
extern const uint8_t kSoundEngineFreqHi[kMaxThrottle + 1];
extern const uint8_t kSoundWindFreqLo[kSoundWindSteps];
extern const uint8_t kSoundWindFreqHi[kSoundWindSteps];
extern const uint8_t kSoundV3FreqHi[V3_HIT + 1];
extern const uint8_t kSoundV3Ctrl[V3_HIT + 1];
extern const uint8_t kSoundV3SusRel[V3_HIT + 1];
extern const uint8_t kSoundV3AttDec[V3_HIT + 1];
extern const uint8_t kSoundV3Ev[5];
extern const uint8_t kSoundV3Id[5];
extern const uint8_t kSoundV3Dur[5];

#ifdef __OSCAR64__
void sound_update(void);

#pragma compile("sound_asm.cc")
#endif

#endif
