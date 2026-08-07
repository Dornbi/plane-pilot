#include "sound.h"

#include <string.h>

#include "flight.h"

uint8_t sound_shadow[kSoundRegCount];
uint8_t sound_gen;
uint8_t sound_gen_seen;

// Master volume. $D418 is the only volume the SID has - it is global, not per
// voice - so this is a constant and the mix between voices is set entirely by
// waveform and envelope. See ../docs/sound.md section 2.
static const uint8_t kMasterVolume = 15;

// --- Voice 1: engine -------------------------------------------------------

// Voice 1 frequency for each throttle step, as a SID frequency register value.
//
// A propeller's fundamental is blade passing, and the harmonic stack above it
// is what makes a pulse wave read as an engine rather than as a tone. The span
// here is 50 Hz at idle to 105 Hz at full, which is the roughly 2:1 ratio a
// light aircraft covers between idle and takeoff power.
//
// The steps are geometric, not linear: 105/50 spread evenly across 25 steps is
// 0.54 semitones each. A linear ramp over the same span would put more than
// half its frequency change in the bottom third of the throttle and leave the
// last five or six steps sounding identical - which is the audible half of why
// section 3 chose a table. The other half is that a table is also cheaper than
// the multiply a linear map needs.
//
// Values are f * 16777216 / 985248, the PAL conversion. On NTSC the same
// numbers come out about 3.8% sharp, which for an engine drone with no
// reference pitch is not perceptible and does not justify a second table.
static const uint16_t kEngineFreq[kMaxThrottle + 1] = {
    0x0353, 0x036E, 0x038A, 0x03A6, 0x03C3,  //  0.. 4   50.0 ..  56.6 Hz
    0x03E2, 0x0401, 0x0421, 0x0442, 0x0465,  //  5.. 9   58.4 ..  66.0 Hz
    0x0488, 0x04AC, 0x04D2, 0x04F9, 0x0521,  // 10..14   68.1 ..  77.1 Hz
    0x054A, 0x0574, 0x05A0, 0x05CD, 0x05FC,  // 15..19   79.5 ..  90.0 Hz
    0x062C, 0x065E, 0x0691, 0x06C6, 0x06FC,  // 20..24   92.8 .. 105.0 Hz
};

// A held drone, so the envelope only ever plays its attack and then sits on
// sustain. Attack 1 is about 8 ms - long enough not to click when the engine
// comes back after a pause, short enough not to be heard as a fade. Decay 0
// and sustain 15 mean the level never moves once it is up; release 0 makes the
// engine stop with the gate rather than trailing after a screen change.
static const uint8_t kEngineAttDec = 0x10;  // attack 1, decay 0
static const uint8_t kEngineSusRel = 0xF0;  // sustain 15, release 0

// Pulse width sweep. Swept independently of RPM, which is the entire reason a
// constant-throttle cruise - most of any flight - does not degenerate into a
// dead drone. Duty cycle is what a pulse wave's harmonic content is made of,
// so moving it moves the timbre without moving the pitch.
//
// The sweep is a triangle over kPwmMin .. kPwmMin + 0x7F8, staying well clear
// of both ends of the 12-bit range: at 0 and at 0xFFF the pulse degenerates to
// DC and the voice goes silent.
//
// kPwmStep is per frame, and the phase is 8 bits, so a full cycle is around 40
// frames, about 4 seconds at the ~10 Hz frame rate.
static const uint16_t kPwmMin = 0x0400;
static const uint8_t kPwmStep = 6;

static uint8_t _pwm_phase;

// --- Roughness -------------------------------------------------------------
//
// A clean pitch table plus a clean triangle sweep is audibly a synthesizer
// holding a note: too steady to be a machine with moving parts. Both are
// therefore perturbed every frame from a pseudo-random source.
//
// What this can and cannot be is set by where it runs. sound_update() is on
// the main line at ~10 Hz, so this is a *flutter* - an engine running rough -
// and not a texture. Real per-cycle grit would have to happen at audio rate,
// which means in the blit, and section 3 requires the blit to stay a flat
// store sequence with no state. The two alternatives that would have given
// genuine noise were both rejected: RECT|NOISE as a combined waveform zeroes
// the 6581's noise LFSR and silences the voice until TEST is toggled, which is
// exactly the chip-revision dependency section 3 spent a page avoiding; and
// putting the engine on a noise waveform outright collides with voice 2, which
// is noise already (wind, phase 3).
//
// An 8-bit Galois LFSR with the taps of x^8 + x^6 + x^5 + x^4 + 1. Maximal
// length, so it cycles through all 255 non-zero states before repeating -
// about 25 seconds at the frame rate, long enough that the engine never
// audibly loops. Zero is the one state it cannot enter and cannot leave, which
// is why sound_init() seeds it non-zero.
static const uint8_t kRngTaps = 0xB8;
static const uint8_t kRngSeed = 0xA5;

static uint8_t _rng;

static uint8_t _next_rand(void) {
  bool lsb = _rng & 1;
  _rng >>= 1;
  if (lsb) {
    _rng ^= kRngTaps;
  }
  return _rng;
}

// --- Driver ----------------------------------------------------------------

// Writes the chip directly, bypassing the shadow. Only for the two callers
// below, both of which run when sound_blit() either has not started yet or is
// about to be masked - so nothing else is going to push the shadow out.
static void _write_through(void) {
  for (uint8_t i = 0; i < kSoundRegCount; ++i) {
    SID_REGS[i] = sound_shadow[i];
  }
}

static void _set_voice(uint8_t base, uint16_t freq, uint16_t pw, uint8_t ctrl,
                       uint8_t attdec, uint8_t susrel) {
  sound_shadow[base + kSoundVoiceFreqLo] = (uint8_t)freq;
  sound_shadow[base + kSoundVoiceFreqHi] = (uint8_t)(freq >> 8);
  sound_shadow[base + kSoundVoicePwLo] = (uint8_t)pw;
  sound_shadow[base + kSoundVoicePwHi] = (uint8_t)(pw >> 8) & 0x0F;
  sound_shadow[base + kSoundVoiceCtrl] = ctrl;
  sound_shadow[base + kSoundVoiceAttDec] = attdec;
  sound_shadow[base + kSoundVoiceSusRel] = susrel;
}

// The derived half of the silence rule. The other half - menu, help and map -
// is sound_silence() below, called from gfx_stop_raster_irqs(). These three
// need no call at all, because the simulation loop keeps running and this is
// re-evaluated every frame.
//
// Crashed is checked here rather than trusted to the flight state going quiet
// on its own: flight_advance() returns early once wrecked, so every input to
// this function holds whatever value it had at the moment of the crash - a
// stalled aircraft that hits the ground would otherwise leave its engine and,
// from phase 5, its stall warning running forever.
static bool _audible(void) {
  return !flight_paused && !flight_crashed() && flight_fuel > 0;
}

uint16_t sound_engine_base_freq(uint8_t throttle) {
  if (throttle > kMaxThrottle) {
    throttle = kMaxThrottle;
  }
  return kEngineFreq[throttle];
}

void sound_init(void) {
  sound_gen = 0;
  sound_gen_seen = 0;
  _pwm_phase = 0;
  _rng = kRngSeed;
  sound_silence();
}

void sound_silence(void) {
  memset(sound_shadow, 0, kSoundRegCount);
  // The chip has to be written here and not left to the next blit: the
  // callers are on their way to an sei, and map_enter() additionally banks
  // I/O out, so $D400 is about to stop being the SID for a while.
  _write_through();
}

void sound_update(void) {
  // Both of these run whether or not anything is audible. The sweep, so that
  // unpausing does not restart the timbre from the same point every time; the
  // LFSR, so that a pause does not freeze the engine's roughness into the same
  // few states each flight.
  //
  // Two draws rather than one, because a single byte does not have enough
  // independent bits for both jobs and reusing them would tie the timbre to
  // the pitch - the two would move together every frame, which is a pattern
  // the ear picks out as machinery of the wrong kind. Within the pulse width
  // the same byte drives both terms, and that correlation is immaterial: they
  // are the same parameter.
  uint8_t r_pw = _next_rand();
  uint8_t r_pitch = _next_rand();

  // Stepping the phase by a varying amount is half the grit: a fixed step
  // makes the sweep exactly periodic, and at four seconds a period that is
  // slow enough to be heard as a repeating figure rather than as drift.
  _pwm_phase += kPwmStep + (r_pw & 3);

  // Zeroing first means every register this function does not set is silent by
  // default, rather than holding the previous frame's value. Phases 3 to 6 add
  // voices 2 and 3 on top; until then they are gated off by this memset and
  // not by anything anyone has to remember to write.
  memset(sound_shadow, 0, kSoundRegCount);
  if (!_audible()) {
    return;
  }

  // Triangle from the 8-bit phase: count up over the bottom half, back down
  // over the top. Scaled by 8 into the 12-bit pulse width register, then
  // knocked off it by a small random offset - the other half of the grit.
  uint8_t tri = (_pwm_phase & 0x80) ? (uint8_t)(0xFE - ((_pwm_phase & 0x7F) << 1))
                                    : (uint8_t)((_pwm_phase & 0x7F) << 1);
  uint16_t pw = kPwmMin + ((uint16_t)tri << 3) + (r_pw & kPwmJitterMask);

  // Pitch jitter, proportional to the pitch so idle and full power are equally
  // unsteady. amp is the full-scale deviation and n scales it to a signed
  // fraction of itself, so the result is uniform over -amp .. +amp.
  //
  // Both fit comfortably in 16 bits: amp is at most 1788 >> 5 = 55 and n is
  // -16 .. 15, so the product never leaves a signed byte's worth of headroom.
  // A different jitter shift changes amp, not the arithmetic.
  uint16_t base = sound_engine_base_freq(flight_throttle);
  int16_t amp = (int16_t)(base >> kEngineJitterShift);
  int16_t n = (int16_t)(r_pitch & 0x1F) - 16;
  uint16_t freq = (uint16_t)((int16_t)base + ((amp * n) >> 4));

  _set_voice(kSoundRegV1, freq, pw, SID_CTRL_RECT | SID_CTRL_GATE,
             kEngineAttDec, kEngineSusRel);

  sound_shadow[kSoundRegModeVol] = kMasterVolume;
}
