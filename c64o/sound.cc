#include "sound.h"

#include <string.h>

#include "flight.h"

uint8_t sound_shadow[kSoundRegCount];
uint8_t sound_gen;
uint8_t sound_gen_seen;

// Not reset by sound_init(): see the comment in sound.h. This is the only
// piece of driver state that outlives a flight.
uint8_t sound_volume = kSoundVolumeDefault;

// What each step of the V key puts in $D418. This is the only volume the SID
// has - it is global, not per voice - so the mix *between* voices is still set
// entirely by waveform and envelope, and this scales all of them together.
// See ../docs/sound.md section 2.
//
// 7 rather than 8 for the middle step: it is half amplitude, about -6 dB, and
// it stays clear of the bottom of the range where the 6581 in particular gets
// noisy. Step 0 never actually reaches the chip through here - the predicate
// below zeroes the whole shadow first - but it is in the table so the mapping
// from step to volume is complete in one place.
static const uint8_t kMasterVolume[kSoundVolumeSteps] = {0, 7, 15};

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

// --- Voice 2: wind ---------------------------------------------------------

// Wind intensity rises with airspeed, and section 2 established that the
// natural mechanism for "louder" is not available: $D418 is master only, and
// modulating a voice's sustain works downward but not upward without a
// retrigger. Of the three candidates in section 6 this is option 3, noise
// frequency as brightness. Higher noise frequency reads as brighter and more
// intense; it is not literally louder, but for "wind rises with speed" it
// sells, and it costs one register, no retrigger and no dependence on the
// filter - which section 3 spent a page arguing against relying on.
//
// The register means the same thing here as it would on any noise voice: it
// clocks the LFSR, which shifts at freq * 985248 / 2^20, or roughly freq
// hertz. This table spans 1443 to 5773 shifts per second - fast enough at
// every setting to be heard as a hiss rather than as the individual steps.
//
// Note that these values overlap the engine table numerically, and that this
// means nothing. The same register is a pitch on a pulse voice and an LFSR
// clock on a noise voice, a factor of 16 apart in the rate they produce, so
// comparing the two tables entry for entry is a category error. What does
// matter is the ratio the two sounds come out at, and sound_test.cc asserts
// that: the slowest wind is over ten times the fastest engine fundamental, so
// wind can never be mistaken for a chug.
//
// Indexed by flight_speed >> kWindSpeedShift, geometric between the endpoints
// for the same reason the engine table is - a linear ramp would spend most of
// its range in the bottom of the envelope, where an aircraft rarely is.
static const uint8_t kWindSpeedShift = 8;
static const uint8_t kWindSteps = (kMaxSpeed >> kWindSpeedShift) + 1;

static const uint16_t kWindFreq[kWindSteps] = {
    0x0600, 0x0695, 0x0738, 0x07EB,  //  0.. 3  1443 .. 1905 shifts/sec
    0x08AF, 0x0986, 0x0A72, 0x0B75,  //  4.. 7  2089 .. 2756
    0x0C91, 0x0DC9, 0x0F1E, 0x1095,  //  8..11  3023 .. 3989
    0x1230, 0x13F3, 0x15E2, 0x1800,  // 12..15  4375 .. 5773
};

// Below this there is no airspeed worth hearing, and voice 2 gates off. A
// stationary aircraft on the runway hissing at itself is the first thing
// anyone would notice, and brightness alone cannot fix it: option 3 changes
// the colour of the wind, never its level, so the bed at speed zero would be
// just as loud as the bed at cruise.
//
// 0x0080 is about 3% of the speed envelope, below any speed the aircraft can
// sustain in the air and below a brisk taxi.
static const uint16_t kWindMinSpeed = 0x0080;

// Attack 6 is about 68 ms, slow enough that crossing the threshold on the
// takeoff roll swells rather than clicks. Release 6 is about 200 ms, so it
// fades on the way back down too.
//
// Sustain is the mix. It is deliberately below the engine's 15, because $D418
// is global (section 2) and a static sustain difference is the only balance
// control the chip offers - wind has to sit under the engine as a bed, and
// noise reads louder than a pulse tone at equal envelope level. Static is also
// what keeps this safe: sustain can be lowered at will but only rises on a
// retrigger, and a value that never changes never needs one.
static const uint8_t kWindAttDec = 0x60;  // attack 6, decay 0
static const uint8_t kWindSustain = 10;
static const uint8_t kWindSusRel = (kWindSustain << 4) | 0x06;

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

#ifndef __OSCAR64__
void (*sound_shadow_observer)(void) = nullptr;
#define SHADOW_OBSERVE()                 \
  do {                                   \
    if (sound_shadow_observer)           \
      sound_shadow_observer();           \
  } while (0)
#else
#define SHADOW_OBSERVE() \
  do {                   \
  } while (0)
#endif

// Every write into the shadow goes through here. On the C64 build it is a
// plain store; on the host it additionally notifies the test observer, so a
// test can simulate the raster interrupt landing between any two stores.
//
// The store is through a volatile pointer, and that is load-bearing rather
// than decorative. The safety of a torn read (see _set_voice) depends on the
// order these stores actually happen in, and nothing else in the program can
// tell the difference - so an optimiser is entitled to reorder or coalesce
// them. Volatile stores may not be reordered against each other, which pins
// the order without making sound_shadow volatile outright: that would also
// make all 25 of sound_blit()'s reads volatile, and the blit's code generation
// is the one thing in this module that must not be disturbed.
static void _poke(uint8_t idx, uint8_t val) {
  ((volatile uint8_t *)sound_shadow)[idx] = val;
  SHADOW_OBSERVE();
}

// Writes the chip directly, bypassing the shadow. Only for the two callers
// below, both of which run when sound_blit() either has not started yet or is
// about to be masked - so nothing else is going to push the shadow out.
static void _write_through(void) {
  for (uint8_t i = 0; i < kSoundRegCount; ++i) {
    SID_REGS[i] = sound_shadow[i];
  }
}

// Writes one voice. The control register goes LAST, and that ordering is the
// whole reason this function exists rather than seven assignments at the call
// site.
//
// sound_blit() can fire between any two of these stores, so it can copy a
// half-written voice to the chip. Most of the registers do not care: a torn
// read that pairs a new frequency with an old pulse width is one frame of a
// slightly wrong timbre, corrected 20 ms later.
//
// Sustain is the exception, because it *latches*. Section 2: lowering sustain
// during the sustain phase drops the level, but raising it does nothing until
// the voice is retriggered. So a voice whose gate is set at the instant its
// sustain register reads 0 goes silent and stays silent - every later frame
// rewrites the correct sustain, and the chip ignores all of them, because
// nothing produces the gate edge it needs to act. The voice is dead until
// something unrelated cycles its gate, which during flight might be seconds.
//
// Writing the control register last means a torn read sees either the old
// gate with the new envelope, or the new gate with the new envelope. It can
// never see a gate turned on ahead of the sustain that gate is going to latch.
static void _set_voice(uint8_t base, uint16_t freq, uint16_t pw, uint8_t ctrl,
                       uint8_t attdec, uint8_t susrel) {
  _poke(base + kSoundVoiceFreqLo, (uint8_t)freq);
  _poke(base + kSoundVoiceFreqHi, (uint8_t)(freq >> 8));
  _poke(base + kSoundVoicePwLo, (uint8_t)pw);
  _poke(base + kSoundVoicePwHi, (uint8_t)(pw >> 8) & 0x0F);
  _poke(base + kSoundVoiceAttDec, attdec);
  _poke(base + kSoundVoiceSusRel, susrel);
  _poke(base + kSoundVoiceCtrl, ctrl);
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
  return sound_volume != 0 && !flight_paused && !flight_crashed() &&
         flight_fuel > 0;
}

// Cycling up and wrapping, so from the default of full the first press is
// silence.
//
// No write-through of its own is needed. sound_update() runs every frame and
// will rewrite the shadow on the next one, and the blit pushes that to the chip
// 20 ms later - so the change lands within about a frame, which at the ~10 Hz
// simulation rate is as immediate as any other control. The write-through in
// sound_silence() exists only because its callers are about to mask interrupts
// or bank I/O out, and neither is true here.
void sound_cycle_volume(void) {
  ++sound_volume;
  if (sound_volume >= kSoundVolumeSteps) {
    sound_volume = 0;
  }
}

uint16_t sound_engine_base_freq(uint8_t throttle) {
  if (throttle > kMaxThrottle) {
    throttle = kMaxThrottle;
  }
  return kEngineFreq[throttle];
}

uint16_t sound_wind_freq(int16_t speed) {
  if (speed < 0) {
    speed = 0;
  }
  uint8_t idx = (uint8_t)((uint16_t)speed >> kWindSpeedShift);
  if (idx >= kWindSteps) {
    idx = kWindSteps - 1;
  }
  return kWindFreq[idx];
}

bool sound_wind_audible(int16_t speed) {
  return speed >= (int16_t)kWindMinSpeed;
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

  // No zeroing pass. An earlier version wiped the whole shadow here and then
  // filled in whatever was audible, which read well but left the shadow
  // momentarily all zeros - and an interrupt landing in that window blitted
  // master volume 0 and every gate clear to the chip. That much was
  // self-correcting, but it also meant every voice was written from a zeroed
  // sustain, which is not (see _set_voice).
  //
  // So every register is written every frame with its final value, silent or
  // not, and silence is expressed as gates clear and master volume zero rather
  // than as an absence of writes. A torn read then mixes two valid register
  // sets instead of a valid one with a blank.
  const bool audible = _audible();

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

  // Voice 3 is unclaimed until the stall warning in phase 5. Written every
  // frame anyway, so it holds a defined gate-clear state rather than whatever
  // happened to be there.
  _set_voice(kSoundRegV3, 0, 0, 0, 0, 0);

  // Voice 1: engine. Gate clear when inaudible rather than the whole voice
  // blanked, so the envelope releases instead of being cut dead, and so the
  // sustain register is never zero underneath a gate that is about to be set.
  _set_voice(kSoundRegV1, freq, pw,
             audible ? (SID_CTRL_RECT | SID_CTRL_GATE) : SID_CTRL_RECT,
             kEngineAttDec, kEngineSusRel);

  // Voice 2: wind, which additionally gates off below the speed threshold.
  _set_voice(kSoundRegV2, sound_wind_freq(flight_speed), 0,
             (audible && sound_wind_audible(flight_speed))
                 ? (SID_CTRL_NOISE | SID_CTRL_GATE)
                 : SID_CTRL_NOISE,
             kWindAttDec, kWindSusRel);

  // The filter is deliberately unused - section 3 on why depending on it is a
  // portability trap - but the registers still have to be written, since
  // nothing zeroes them for us any more.
  _poke(kSoundRegCutoffLo, 0);
  _poke(kSoundRegCutoffHi, 0);
  _poke(kSoundRegResFilt, 0);

  // Master volume last. It is the one register with no latching behaviour, so
  // a torn read that catches the old value is a single frame at the wrong
  // level and nothing more - which makes it the safest thing to leave until
  // the end.
  //
  // The index is guarded rather than trusted: sound_volume is written from the
  // key handler, and an out-of-range value would index past the table and put
  // an arbitrary byte in $D418 - where the high nibble is the filter mode, so
  // the symptom would be a filter switching on rather than a wrong volume.
  _poke(kSoundRegModeVol,
        audible ? kMasterVolume[sound_volume < kSoundVolumeSteps
                                    ? sound_volume
                                    : kSoundVolumeDefault]
                : 0);
}
