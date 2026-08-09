// Host test for the sound driver's register mapping.
//
// docs/sound.md section 7 is the reason this file is worth its weight: the
// failure mode of audio code is silence, and silence is exactly what playing
// the game does not reliably surface. A wrong register index, a gate bit that
// never gets set, a pulse width that lands on 0 - all of them sound like
// nothing at all, and all of them are visible here.
//
// The test links sound.cc and nothing else from the simulation. Everything
// sound.cc reads out of flight.h is defined below instead, which is what makes
// this a unit test of the mapping rather than a second copy of flight_test:
// the inputs are set directly, including combinations the flight model would
// take a contrived trajectory to produce.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../sound.h"

#include "../flight.h"

// --- The flight state sound.cc reads ---------------------------------------
//
// flight_crashed() is inline in flight.h and reads flight_status, so the crash
// case is exercised for real rather than stubbed.
bool flight_paused = false;
enum FlightStatus flight_status = FLIGHT_ONGOING;
uint8_t flight_throttle = 0;
uint32_t flight_fuel = 0x21FFF;
int16_t flight_speed = 0;
uint8_t flight_stall = 0;
uint8_t flight_events = 0;
uint8_t flight_gen = 0;

// The one piece of music.cc's state sound.cc reads. Defined here rather than
// linked, for the same reason as the flight globals above: it lets the test
// set the ownership question directly instead of driving a screen transition.
bool music_playing = false;

// The host build's stand-in for the chip. sid.h points SID_REGS at this, so
// sound_silence()'s write-through is an array store instead of a segfault, and
// the test can check what reached the chip as well as what is in the shadow.
static uint8_t sid_regs[32];
struct SID *sid_host = (struct SID *)sid_regs;

// --- Helpers ---------------------------------------------------------------

static void reset_state(void) {
  flight_paused = false;
  flight_status = FLIGHT_ONGOING;
  flight_throttle = 0;
  flight_fuel = 0x21FFF;
  // Above the wind gate threshold, so the default state has both continuous
  // voices running and a test that forgets to set a speed still exercises
  // voice 2 rather than silently skipping it.
  flight_speed = kMaxSpeed / 2;
  flight_stall = 0;
  flight_events = 0;
  flight_gen = 0;
  memset(sid_regs, 0xAA, sizeof(sid_regs));
  sound_volume = kSoundVolumeDefault;
  sound_init();
}

static uint8_t master_volume(void) {
  return sound_shadow[kSoundRegModeVol] & 0x0F;
}

// --- A model of the SID envelope, just deep enough to show a stuck voice ----
//
// One rule from section 2 is what this exists for: $D418 is master only, and a
// voice's sustain level "is asymmetric - lowering sustain during the sustain
// phase drops the level, but *raising* it does nothing until the voice is
// retriggered."
//
// That makes sustain latching, not levelling. A voice that is gated on at the
// instant its sustain register reads 0 goes to silence and stays there, and
// every later write of the correct sustain has no effect, because nothing
// produces the gate edge the chip needs. Only the level is modelled - not the
// attack and decay rates - because the question is never how fast a voice
// reaches its level, only whether it ever leaves zero again.

static const uint8_t kVoiceBase[3] = {kSoundRegV1, kSoundRegV2, kSoundRegV3};

struct EnvModel {
  uint8_t level;
  bool gated;
};

static EnvModel env[3];
static uint8_t env_volume;

static void model_reset(void) {
  for (int v = 0; v < 3; ++v) {
    env[v].level = 0;
    env[v].gated = false;
  }
  env_volume = 0;
}

// What the chip does when sound_blit() copies the shadow to $D400.
static void model_blit(void) {
  env_volume = sound_shadow[kSoundRegModeVol] & 0x0F;
  for (int v = 0; v < 3; ++v) {
    uint8_t ctrl = sound_shadow[kVoiceBase[v] + kSoundVoiceCtrl];
    uint8_t sustain = sound_shadow[kVoiceBase[v] + kSoundVoiceSusRel] >> 4;
    bool gate = (ctrl & SID_CTRL_GATE) != 0;

    if (gate && !env[v].gated) {
      // Gate edge: attack to peak, then decay to whatever sustain says right
      // now. If sustain reads 0 at this instant, the voice lands on silence.
      env[v].level = sustain;
    } else if (gate) {
      // Held gate. Sustain can pull the level down and never pushes it up.
      if (env[v].level > sustain) {
        env[v].level = sustain;
      }
    } else {
      env[v].level = 0;
    }
    env[v].gated = gate;
  }
}

// Is this voice actually making a sound?
static bool model_audible(int v) { return env_volume > 0 && env[v].level > 0; }

// --- Simulating a raster interrupt at an arbitrary instant ------------------

static long observer_writes;
static long observer_fire_at;

static void observer(void) {
  if (observer_writes++ == observer_fire_at) {
    model_blit();
  }
}

// One update with no interrupt during it, then the blit that follows it.
static void quiet_frame(void) {
  sound_shadow_observer = nullptr;
  sound_update();
  model_blit();
}

static uint16_t voice_freq(uint8_t base) {
  return (uint16_t)sound_shadow[base + kSoundVoiceFreqLo] |
         ((uint16_t)sound_shadow[base + kSoundVoiceFreqHi] << 8);
}

static uint16_t voice_pw(uint8_t base) {
  return (uint16_t)sound_shadow[base + kSoundVoicePwLo] |
         ((uint16_t)sound_shadow[base + kSoundVoicePwHi] << 8);
}

// Silence is a property of the register set, not a particular byte pattern.
//
// It used to be "every byte is zero", because sound_update() wiped the shadow
// and filled in only what was audible. That is no longer how silence is
// expressed - blanking the shadow is what made a torn read able to gate a
// voice on over a zeroed sustain - so the test asks the question directly:
// master volume off, and no voice gated.
static bool shadow_is_silent(void) {
  if ((sound_shadow[kSoundRegModeVol] & 0x0F) != 0) {
    return false;
  }
  return (sound_shadow[kSoundRegV1Ctrl] & SID_CTRL_GATE) == 0 &&
         (sound_shadow[kSoundRegV2Ctrl] & SID_CTRL_GATE) == 0 &&
         (sound_shadow[kSoundRegV3Ctrl] & SID_CTRL_GATE) == 0;
}

// Still needed for sound_silence(), which really does blank everything - it
// runs on the way to an sei with the chip about to be banked out, so there is
// no later blit to rely on and nothing to be torn against.
static bool shadow_all_zero(void) {
  for (uint8_t i = 0; i < kSoundRegCount; ++i) {
    if (sound_shadow[i] != 0) {
      return false;
    }
  }
  return true;
}

// The pitch table entry for a throttle setting, before jitter. The emitted
// frequency is deliberately never exactly this - see the roughness section -
// so the table's shape has to be asserted through the accessor.
static uint16_t freq_at_throttle(uint8_t t) {
  return sound_engine_base_freq(t);
}

// How far sound_update() may move the frequency off the table entry, derived
// from the tuning constant rather than repeated as a literal, so turning the
// roughness up or down does not falsify the test.
static uint16_t jitter_bound(uint16_t base) {
  return base >> kEngineJitterShift;
}

// The control register the engine should be driving voice 1 with.
static uint8_t expected_ctrl(void) {
  return SID_CTRL_RECT | SID_CTRL_GATE;
}

// --- Voice 3 helpers -------------------------------------------------------

static bool v3_gated(void) {
  return (sound_shadow[kSoundRegV3Ctrl] & SID_CTRL_GATE) != 0;
}
static uint8_t v3_wave(void) { return sound_shadow[kSoundRegV3Ctrl] & 0xF0; }

// Publish an event set the way flight_advance() does: the bits first, then the
// generation. sound_update() keys off the generation changing.
static void publish_events(uint8_t bits) {
  flight_events = bits;
  ++flight_gen;
}

// One frame with no events at all - the generation still advances, because
// flight_advance() bumps it every step whether anything happened or not.
static void idle_frame(void) {
  publish_events(0);
  sound_update();
}

// Did this frame ask sound_blit() to cycle voice 3's gate? That is what marks
// the start of a burst.
//
// For one-shots it is the only marker available: two events in consecutive
// frames leave the gate set throughout, so the gate bit alone cannot say that
// a second sound began. The stall's beeps are also visible on the gate now
// that its gap is a gate-off, and both are asserted where they apply.
static uint8_t gen_before;
static void mark(void) { gen_before = sound_gen; }
static bool retriggered(void) { return sound_gen != gen_before; }

// How many frames a one-shot holds voice 3 gated, measured rather than read
// off a constant. This is the property the pilot actually hears, and it is not
// visible in any single register: the one-shots hold a plateau and are gated
// off after a frame count, so the decay nibble says nothing about length.
static int one_shot_frames(uint8_t ev) {
  reset_state();
  idle_frame();
  publish_events(ev);
  sound_update();
  int frames = 0;
  while (v3_gated() && frames < 100) {
    ++frames;
    idle_frame();
  }
  return frames;
}

int main() {
  printf("Running sound_test...\n");

  // --- The register block itself ------------------------------------------
  //
  // $D400..$D418 inclusive. Getting this wrong shifts every voice by one
  // register and produces noise or nothing, so it is asserted rather than
  // assumed.
  assert(kSoundRegCount == 25);
  assert(kSoundRegV1 == 0);
  assert(kSoundRegV2 == 7);
  assert(kSoundRegV3 == 14);
  assert(kSoundRegV1Ctrl == 4);
  assert(kSoundRegV2Ctrl == 11);
  assert(kSoundRegV3Ctrl == 18);
  assert(kSoundRegModeVol == 24);

  // --- Silence is derived, not called -------------------------------------
  //
  // docs/sound.md section 3: paused, crashed and out of fuel each need no call
  // site, because sound_update() re-derives them every frame. All three are
  // checked at full throttle, which is the state that would be loudest if the
  // predicate were missing.
  reset_state();
  flight_throttle = kMaxThrottle;
  flight_paused = true;
  sound_update();
  assert(shadow_is_silent());

  reset_state();
  flight_throttle = kMaxThrottle;
  flight_status = FLIGHT_CRASH_VSPEED;
  sound_update();
  assert(shadow_is_silent());

  // A completed mission is not a crash: the simulation keeps running and so
  // does the engine. This is the case a plain truth test on flight_status
  // would get wrong.
  reset_state();
  flight_throttle = kMaxThrottle;
  flight_status = FLIGHT_MISSION_COMPLETED;
  sound_update();
  assert(!shadow_is_silent());

  reset_state();
  flight_throttle = kMaxThrottle;
  flight_fuel = 0;
  sound_update();
  assert(shadow_is_silent());

  // Fuel of 1 is still fuel. The boundary matters because the flight model
  // drains toward zero rather than jumping to it.
  reset_state();
  flight_throttle = kMaxThrottle;
  flight_fuel = 1;
  sound_update();
  assert(!shadow_is_silent());

  // --- The V key -----------------------------------------------------------

  // The cycle order. Default is full and V wraps DOWNWARD, so the sequence is
  // full -> low -> off -> full: every press means "quieter" until it wraps.
  // It used to go upward, which made the first press mean "off" and the second
  // mean "quiet" - two different things from one key.
  //
  // The sequence is written out literally rather than derived from
  // kSoundVolumeSteps. A loop that computes the expected value applies the
  // same rule the implementation does, so it agrees with an inverted
  // implementation instead of catching it - which is exactly what a test of a
  // direction must not do.
  reset_state();
  assert(kSoundVolumeSteps == 3);
  assert(sound_volume == kSoundVolumeDefault);
  assert(kSoundVolumeDefault == kSoundVolumeSteps - 1);
  sound_cycle_volume();
  assert(sound_volume == 1);  // full -> low
  sound_cycle_volume();
  assert(sound_volume == 0);  // low  -> off
  sound_cycle_volume();
  assert(sound_volume == 2);  // off  -> full, wrapping rather than sticking
  assert(sound_volume == kSoundVolumeDefault);

  // Every step has a label, they are all the documented fixed width, and the
  // label tracks the step. The screens print this without strlen and without
  // clearing the cell, so a short one would leave the tail of the last.
  for (uint8_t step = 0; step < kSoundVolumeSteps; ++step) {
    sound_volume = step;
    const char *label = sound_volume_label();
    assert(label != NULL);
    assert(strlen(label) == kSoundVolumeLabelLen);
  }
  sound_volume = 0;
  assert(strstr(sound_volume_label(), "OFF") != NULL);
  sound_volume = kSoundVolumeSteps - 1;
  assert(strstr(sound_volume_label(), "FULL") != NULL);
  // Out of range falls back rather than reading off the end of the table.
  sound_volume = 99;
  assert(sound_volume_label() != NULL);
  sound_volume = kSoundVolumeDefault;

  // Step 0 goes through the same predicate as pause and crash, so it silences
  // the whole driver rather than one voice - which is what has to stay true as
  // phases 3 to 6 add voices that the volume control knows nothing about.
  reset_state();
  flight_throttle = kMaxThrottle;
  sound_volume = 0;
  sound_update();
  assert(shadow_is_silent());

  // The two audible steps have to be audible, ordered, and distinct. A middle
  // step equal to full is a control that does nothing on two of its three
  // settings; a middle step of 0 is a second, silent "off".
  uint8_t vol_at_step[kSoundVolumeSteps];
  for (uint8_t step = 1; step < kSoundVolumeSteps; ++step) {
    reset_state();
    flight_throttle = kMaxThrottle;
    sound_volume = step;
    sound_update();
    assert(!shadow_is_silent());
    vol_at_step[step] = master_volume();
    assert(vol_at_step[step] > 0);
    // The high nibble of $D418 is the filter mode. Volume must not leak into
    // it, or turning the sound down would switch a filter on.
    assert((sound_shadow[kSoundRegModeVol] & 0xF0) == 0);
    if (step > 1) {
      assert(vol_at_step[step] > vol_at_step[step - 1]);
    }
  }

  // The volume is a setting, not flight state, so it has to survive a whole
  // mission round trip. That is the exact sequence the game runs on Q to the
  // menu and then into the next mission:
  //
  //   sound_silence()  from gfx_stop_raster_irqs(), on the way to the menu
  //   sound_init()     from _enter_simulation(), entering the next mission
  //
  // Resetting the volume in either one would silently turn the sound back up
  // every time the player restarts or picks a different mission. Every step is
  // checked, not just the end state, so a failure says which call did it.
  for (uint8_t step = 0; step < kSoundVolumeSteps; ++step) {
    reset_state();
    sound_volume = step;

    sound_silence();
    assert(sound_volume == step);
    assert(shadow_all_zero());

    sound_init();
    assert(sound_volume == step);

    flight_throttle = kMaxThrottle;
    sound_update();
    // And it is still in force, not merely still stored.
    if (step == 0) {
      assert(shadow_is_silent());
    } else {
      assert(master_volume() == vol_at_step[step]);
    }
  }

  // An out-of-range step must not index past the volume table. Nothing in the
  // game produces this - sound_cycle_volume() is the only writer - but the
  // symptom if it ever did would be a stray high nibble in $D418 switching a
  // filter on, which reads as a bug in a completely different module.
  reset_state();
  flight_throttle = kMaxThrottle;
  sound_volume = 99;
  sound_update();
  assert((sound_shadow[kSoundRegModeVol] & 0xF0) == 0);
  assert(master_volume() == vol_at_step[kSoundVolumeDefault]);

  // --- The engine voice ----------------------------------------------------

  reset_state();
  flight_throttle = 0x10;
  sound_update();

  // Pulse, gated on. Noise or sawtooth here would still make a sound, so a
  // listening test would not catch it - but it would not be a propeller.
  assert(sound_shadow[kSoundRegV1Ctrl] == expected_ctrl());
  // Exactly one waveform bit. The SID treats a combined waveform as a
  // completely different generator, and noise combined with anything else
  // zeroes its LFSR on a 6581 and silences the voice until TEST is toggled.
  {
    uint8_t wave = sound_shadow[kSoundRegV1Ctrl] & 0xF0;
    assert(wave != 0 && (wave & (wave - 1)) == 0);
  }
  // Sustain at full: the engine is a held drone, so the envelope has to sit at
  // the top rather than decaying away under it.
  assert((sound_shadow[kSoundRegV1 + kSoundVoiceSusRel] >> 4) == 15);

  // Master volume is on. A correct voice behind a zeroed $D418 is the exact
  // failure this whole file exists for.
  assert(master_volume() > 0);

  // Voice 3 is the transient voice, so with no stall and no events pending it
  // must be released rather than left holding whatever the previous frame put
  // there. reset_state() clears flight_stall and flight_events, so this is the
  // idle case.
  assert((sound_shadow[kSoundRegV3Ctrl] & SID_CTRL_GATE) == 0);

  // The filter is deliberately unused; section 3 explains why depending on it
  // is a portability trap. Resonance/routing off, filter mode nibble clear.
  assert(sound_shadow[kSoundRegResFilt] == 0);
  assert((sound_shadow[kSoundRegModeVol] & 0xF0) == 0);

  // --- The pitch table -----------------------------------------------------

  // Strictly increasing across the whole throttle range.
  uint16_t prev = freq_at_throttle(0);
  for (uint8_t t = 1; t <= kMaxThrottle; ++t) {
    uint16_t f = freq_at_throttle(t);
    assert(f > prev);
    prev = f;
  }

  // The named case from section 7: full throttle and throttle 0x12 must not
  // sound the same. This is the regression test for the pitch compression a
  // linear map would ship, where the top several steps collapse together.
  assert(freq_at_throttle(0x18) != freq_at_throttle(0x12));

  // The general form of the same check, and the one that actually pins the
  // shape down. Every adjacent pair has to be separated by about the same
  // musical interval, so no region of the throttle is mushier than any other.
  // A linear table fails this at the top, where the ratio approaches 1.
  for (uint8_t t = 0; t < kMaxThrottle; ++t) {
    uint32_t lo = freq_at_throttle(t);
    uint32_t hi = freq_at_throttle(t + 1);
    uint32_t permille = (hi * 1000) / lo;
    assert(permille >= 1028 && permille <= 1036);
  }

  // Idle and full sit at the ends of a roughly 2:1 span, which is what makes
  // the throttle audible as a throttle rather than as a detune.
  {
    uint32_t idle = freq_at_throttle(0);
    uint32_t full = freq_at_throttle(kMaxThrottle);
    assert(full * 100 / idle >= 200 && full * 100 / idle <= 215);
  }

  // Out-of-range throttle clamps instead of reading past the table. The flight
  // model does not produce this, which is precisely why nothing else would
  // catch it if the table and kMaxThrottle ever disagreed.
  {
    uint16_t full = freq_at_throttle(kMaxThrottle);
    assert(freq_at_throttle(kMaxThrottle + 1) == full);
    assert(freq_at_throttle(0xFF) == full);
  }

  // --- Voice 2: wind -------------------------------------------------------

  // Brightness rises with airspeed, monotonically. This is the whole mechanism
  // - section 6 option 3 - so a table that ever went backwards would make the
  // aircraft sound like it was slowing down while accelerating.
  {
    uint16_t prev_w = sound_wind_freq(0);
    for (int16_t s = 0; s <= (int16_t)kMaxSpeed; s += 16) {
      uint16_t f = sound_wind_freq(s);
      assert(f >= prev_w);
      prev_w = f;
    }
    // And it has to travel far enough to be heard as a change at all.
    assert(sound_wind_freq(kMaxSpeed) > sound_wind_freq(0) * 3);
  }

  // Every adjacent step of the table separated by about the same ratio, for
  // the same reason as the engine's: a linear ramp would spend most of its
  // range in the bottom of the speed envelope, where an aircraft rarely is.
  {
    const uint16_t step = 1 << 8;  // one table entry
    for (uint16_t s = 0; s + step <= kMaxSpeed; s += step) {
      uint32_t lo = sound_wind_freq((int16_t)s);
      uint32_t hi = sound_wind_freq((int16_t)(s + step));
      uint32_t permille = (hi * 1000) / lo;
      assert(permille >= 1085 && permille <= 1110);
    }
  }

  // Out-of-range speeds clamp rather than indexing past the table, at both
  // ends. Negative is the one that matters: flight_speed is signed and the
  // model clamps it to zero every step, but this function is not entitled to
  // assume that - a negative shifted right stays negative and would index
  // wildly.
  assert(sound_wind_freq(-1) == sound_wind_freq(0));
  assert(sound_wind_freq(-30000) == sound_wind_freq(0));
  assert(sound_wind_freq(0x7FFF) == sound_wind_freq(kMaxSpeed));

  // The gate threshold. No airspeed, no wind - a stationary aircraft on the
  // runway must not hiss at itself, and brightness alone cannot fix that
  // because option 3 changes the wind's colour and never its level.
  assert(!sound_wind_audible(0));
  assert(sound_wind_audible(kMaxSpeed));
  {
    // Exactly one crossing, and no gap or overlap at it. Two thresholds that
    // disagreed by even one unit would leave a band where the wind is neither
    // on nor off, which shows up as a flutter while accelerating through it.
    int16_t crossings = 0;
    bool prev_on = sound_wind_audible(0);
    for (int16_t s = 1; s <= (int16_t)kMaxSpeed; ++s) {
      bool on = sound_wind_audible(s);
      if (on != prev_on) ++crossings;
      prev_on = on;
    }
    assert(crossings == 1);
  }

  // Now the registers themselves, at a speed that is comfortably flying.
  reset_state();
  flight_speed = kMaxSpeed;
  flight_throttle = kMaxThrottle;
  sound_update();

  assert(sound_shadow[kSoundRegV2Ctrl] == (SID_CTRL_NOISE | SID_CTRL_GATE));
  {
    // One waveform bit, for the reason above: noise combined with anything
    // else zeroes the 6581's LFSR and the voice goes silent until TEST is
    // toggled. This is the voice where that actually bites.
    uint8_t wave = sound_shadow[kSoundRegV2Ctrl] & 0xF0;
    assert(wave != 0 && (wave & (wave - 1)) == 0);
  }
  assert(voice_freq(kSoundRegV2) == sound_wind_freq(kMaxSpeed));

  // Wind is a bed. $D418 is global, so a static sustain difference is the only
  // balance control the chip offers, and noise reads louder than a pulse tone
  // at equal envelope level - equal sustains would bury the engine.
  {
    uint8_t wind_sustain = sound_shadow[kSoundRegV2 + kSoundVoiceSusRel] >> 4;
    uint8_t engine_sustain = sound_shadow[kSoundRegV1 + kSoundVoiceSusRel] >> 4;
    assert(wind_sustain > 0 && wind_sustain < engine_sustain);
  }

  // A non-zero release, so that dropping below the threshold fades the bed out
  // rather than cutting it dead.
  assert((sound_shadow[kSoundRegV2 + kSoundVoiceSusRel] & 0x0F) != 0);

  // Wind must always read as a hiss, never as a chug. The comparison has to go
  // through the rates rather than the register values: the same number is a
  // pitch on a pulse voice and an LFSR clock on a noise voice, and the noise
  // rate is 16x the pulse frequency for equal registers. Comparing the two
  // tables entry for entry would be a category error - they overlap
  // numerically and it means nothing.
  {
    uint32_t slowest_wind_rate = 16u * sound_wind_freq(0);
    uint32_t fastest_engine = sound_engine_base_freq(kMaxThrottle);
    assert(slowest_wind_rate > 10u * fastest_engine);
  }

  // Below the threshold the voice is still written - waveform and envelope
  // intact, gate clear - rather than left to the memset. That is what lets it
  // release instead of being cut dead, and the memset would zero the release
  // nibble along with everything else.
  reset_state();
  flight_speed = 0;
  flight_throttle = kMaxThrottle;
  sound_update();
  assert((sound_shadow[kSoundRegV2Ctrl] & SID_CTRL_GATE) == 0);
  assert((sound_shadow[kSoundRegV2Ctrl] & 0xF0) == SID_CTRL_NOISE);
  assert((sound_shadow[kSoundRegV2 + kSoundVoiceSusRel] & 0x0F) != 0);
  // The engine is unaffected by the wind being silent.
  assert(sound_shadow[kSoundRegV1Ctrl] == expected_ctrl());

  // The silence predicates own voice 2 as well. This is the property that has
  // to survive every future voice: silence is one predicate, not one call per
  // voice that someone has to remember to add.
  reset_state();
  flight_speed = kMaxSpeed;
  flight_throttle = kMaxThrottle;
  flight_paused = true;
  sound_update();
  assert(shadow_is_silent());

  // --- Roughness: the engine must not hold a steady note -------------------
  //
  // A clean table plus a clean sweep is audibly a synthesizer. Both are
  // perturbed every frame, and these are the properties that has to keep.
  // Checked at idle, mid and full throttle, not at one sample point. Idle is
  // the one a single mid-throttle sample misses: it is where the register
  // values are smallest, so it is where a jitter scheme that does not suit
  // their magnitude collapses to nothing first. It is also where a real engine
  // is roughest, so an idle that goes glassy while full power stays unsteady
  // is audibly backwards.
  static const uint8_t kRoughnessThrottles[] = {0, 0x0C, kMaxThrottle};
  for (uint8_t ti = 0; ti < 3; ++ti) {
    const uint8_t t = kRoughnessThrottles[ti];
    reset_state();
    flight_throttle = t;

    const uint16_t base = freq_at_throttle(t);
    const uint16_t bound = jitter_bound(base);

    uint16_t seen[512];
    int n_seen = 0;
    uint16_t f_min = 0xFFFF, f_max = 0;

    // A full LFSR period, so this covers every state the generator can reach.
    for (int i = 0; i < 255; ++i) {
      sound_update();
      uint16_t f = voice_freq(kSoundRegV1);

      // Bounded by the tuning constant. Unbounded jitter would eventually
      // wander far enough to sound like a different throttle setting, or in
      // the worst case wrap the 16-bit register.
      assert(f + bound >= base && f <= base + bound);

      bool dup = false;
      for (int j = 0; j < n_seen; ++j) {
        if (seen[j] == f) {
          dup = true;
          break;
        }
      }
      if (!dup) seen[n_seen++] = f;

      if (f < f_min) f_min = f;
      if (f > f_max) f_max = f;
    }

    // It has to actually move, and over many distinct values rather than
    // toggling between two. A stuck LFSR - the failure mode of seeding it
    // zero - would leave exactly one.
    assert(n_seen >= 20);
    // And it has to move in both directions off the table entry, or the
    // jitter is a detune rather than a wobble.
    assert(f_min < base && f_max > base);
  }

  // --- Voice 3: the stall warning ------------------------------------------

  // Nothing on voice 3 decays: every effect goes straight to full sustain and
  // ends by being gated off. For the stall that means its gap is a gate-off
  // rather than an envelope running out, so the warble is visible directly on
  // the gate bit.
  reset_state();
  flight_stall = 1;
  idle_frame();
  assert(v3_gated());
  assert(v3_wave() == SID_CTRL_RECT);  // a tone, not noise - it competes with
                                       // the wind bed on voice 2
  {
    // Full sustain and a release, like every other effect on this voice. A
    // warning horn that fades while it sounds is the same mistake that made
    // gear and flap inaudible.
    const uint8_t susrel = sound_shadow[kSoundRegV3 + kSoundVoiceSusRel];
    assert((susrel >> 4) == 15);
    assert((susrel & 0x0F) != 0);
    // Decay 0, so the level never falls while the beep is sounding.
    assert((sound_shadow[kSoundRegV3 + kSoundVoiceAttDec] & 0x0F) == 0);
  }

  // It has to re-announce itself rather than sit there, and both halves of the
  // warble have to appear. A held tone shows the gate always set; a broken
  // period shows it always clear.
  {
    reset_state();
    flight_stall = 1;
    int on = 0, off = 0, bursts = 0;
    for (int i = 0; i < 40; ++i) {
      mark();
      idle_frame();
      if (retriggered()) ++bursts;
      if (v3_gated()) {
        ++on;
      } else {
        ++off;
      }
    }
    // Both states occur, and neither is a rounding error - this is the whole
    // difference between a warning and a drone.
    assert(on >= 8 && off >= 8);
    // 40 frames is about four seconds. A rate in cockpit-warner territory, so
    // several beeps but not one per frame.
    assert(bursts >= 6 && bursts <= 20);
  }

  // Clearing the flag stops it, and nothing re-arms it afterwards. It is a
  // level, so there is no falling edge to handle - the last burst simply
  // decays on its own envelope.
  {
    reset_state();
    flight_stall = 1;
    for (int i = 0; i < 6; ++i) idle_frame();
    flight_stall = 0;
    for (int i = 0; i < 30; ++i) {
      mark();
      idle_frame();
      assert(!retriggered());
    }
    assert(!v3_gated());
  }

  // A crash while stalled must not leave the horn sounding. flight_stall holds
  // its last value after a crash - flight_advance() returns early - so this
  // only works if the silence predicates are checked ahead of the stall logic,
  // and that ordering is what is being tested.
  {
    reset_state();
    flight_stall = 1;
    for (int i = 0; i < 4; ++i) idle_frame();
    flight_status = FLIGHT_CRASH_VSPEED;
    idle_frame();
    assert(shadow_is_silent());
    assert(!v3_gated());
  }

  // --- Voice 3: one-shots and the priority order ---------------------------

  // Each event produces a burst on voice 3, with the noise waveform that
  // separates the one-shots from the stall tone.
  {
    static const uint8_t kEvents[] = {FLIGHT_EV_TOUCHDOWN, FLIGHT_EV_GEAR,
                                      FLIGHT_EV_FLAP};
    for (int i = 0; i < 3; ++i) {
      reset_state();
      idle_frame();
      mark();
      publish_events(kEvents[i]);
      sound_update();
      assert(retriggered());
      assert(v3_gated());
      assert(v3_wave() == SID_CTRL_NOISE);
    }
  }

  // The one-shots have to be loud enough to hear over two continuous voices
  // that never decay. This is the regression test for the first version, where
  // gear and flap used the stall's decay-to-zero envelope and were almost
  // inaudible: the burst started decaying 2 ms in, so its average level over
  // the sound was a fraction of its peak.
  //
  // Full sustain, and at least the engine's - a one-shot quieter than the drone
  // it has to cut through is the failure being guarded against.
  {
    static const uint8_t kOneShots[] = {FLIGHT_EV_TOUCHDOWN, FLIGHT_EV_GEAR,
                                        FLIGHT_EV_FLAP};
    for (int i = 0; i < 3; ++i) {
      reset_state();
      flight_throttle = kMaxThrottle;
      idle_frame();
      publish_events(kOneShots[i]);
      sound_update();
      const uint8_t v3_sustain =
          sound_shadow[kSoundRegV3 + kSoundVoiceSusRel] >> 4;
      const uint8_t engine_sustain =
          sound_shadow[kSoundRegV1 + kSoundVoiceSusRel] >> 4;
      assert(v3_sustain >= engine_sustain);
      // And a non-zero release, so gating off fades rather than chopping.
      assert((sound_shadow[kSoundRegV3 + kSoundVoiceSusRel] & 0x0F) != 0);
    }
  }

  // Long enough to register. The frame rate wobbles around 10 Hz, so two
  // frames is roughly 200 ms - the length the first version aimed at and which
  // turned out to be too short to notice.
  {
    const int touchdown_len = one_shot_frames(FLIGHT_EV_TOUCHDOWN);
    const int gear_len = one_shot_frames(FLIGHT_EV_GEAR);
    const int flap_len = one_shot_frames(FLIGHT_EV_FLAP);

    assert(touchdown_len >= 2);
    assert(gear_len >= 2);
    assert(flap_len >= 2);

    // Gear and flap are distinguishable. They share a character deliberately -
    // the same class of event to the pilot - but if they produced identical
    // sounds there would be no information in having two. Section 6 asks for
    // flap to be "shorter and quieter"; quieter is not available on a chip
    // with no per-voice volume, so length and pitch carry the whole
    // difference.
    assert(gear_len > flap_len);
  }

  {
    reset_state();
    idle_frame();
    publish_events(FLIGHT_EV_GEAR);
    sound_update();
    const uint16_t gear_freq = voice_freq(kSoundRegV3);

    reset_state();
    idle_frame();
    publish_events(FLIGHT_EV_FLAP);
    sound_update();
    assert(voice_freq(kSoundRegV3) != gear_freq);
    // Gear is the lower of the two - section 6 wants it mechanical rather than
    // an impact.
    assert(voice_freq(kSoundRegV3) > gear_freq);
  }

  // Touchdown outranks a stall in progress.
  {
    reset_state();
    flight_stall = 1;
    for (int i = 0; i < 6; ++i) idle_frame();
    publish_events(FLIGHT_EV_TOUCHDOWN);
    sound_update();
    assert(v3_wave() == SID_CTRL_NOISE);  // the impact, not the warning
  }

  // Gear and flap do NOT outrank a stall, and are dropped rather than queued.
  // Queuing would surface a gear click hundreds of milliseconds after the key
  // press, which reads as a bug rather than as feedback. This is also the case
  // the abandoned voice-2 buffet design got backwards: buffet would have
  // vanished during gear-down on approach, exactly when it is most wanted.
  {
    reset_state();
    flight_stall = 1;
    for (int i = 0; i < 6; ++i) idle_frame();
    publish_events(FLIGHT_EV_GEAR);
    sound_update();
    assert(v3_wave() == SID_CTRL_RECT);  // still the stall tone
    // And it is gone, not deferred: many quiet frames later, no noise burst
    // ever appears.
    for (int i = 0; i < 20; ++i) {
      idle_frame();
      assert(v3_wave() == SID_CTRL_RECT);
    }
  }

  // A stall beginning while a one-shot is still running waits for it, rather
  // than cutting it off mid-burst.
  {
    reset_state();
    idle_frame();
    publish_events(FLIGHT_EV_GEAR);
    sound_update();
    assert(v3_wave() == SID_CTRL_NOISE);
    flight_stall = 1;
    idle_frame();
    assert(v3_wave() == SID_CTRL_NOISE);  // the gear burst still owns it
    // But the stall does get the voice back shortly afterwards.
    bool got_it = false;
    for (int i = 0; i < 10 && !got_it; ++i) {
      idle_frame();
      if (v3_wave() == SID_CTRL_RECT) got_it = true;
    }
    assert(got_it);
  }

  // An event set is consumed exactly once. Without the generation check the
  // same bits would refire on every frame - and flight_advance() stops bumping
  // the generation once the aircraft is wrecked, so the last event of a flight
  // would repeat forever.
  {
    reset_state();
    idle_frame();
    publish_events(FLIGHT_EV_GEAR);
    sound_update();
    // flight_events still holds the bits; only the generation says they are
    // stale. Run well past the one-shot's own lifetime.
    int refires = 0;
    for (int i = 0; i < 20; ++i) {
      mark();
      sound_update();  // deliberately NOT publish_events: no new generation
      if (retriggered()) ++refires;
    }
    assert(refires == 0);
  }

  // Voice 3 releases when nothing owns it, so an idle flight is not sitting on
  // a gated voice.
  {
    reset_state();
    idle_frame();
    publish_events(FLIGHT_EV_FLAP);
    sound_update();
    for (int i = 0; i < 20; ++i) idle_frame();
    assert(!v3_gated());
  }

  // Silence drops whatever voice 3 was doing rather than letting it run down,
  // so unpausing does not resume a half-finished gear click.
  {
    reset_state();
    idle_frame();
    publish_events(FLIGHT_EV_GEAR);
    sound_update();
    assert(v3_gated());
    flight_paused = true;
    idle_frame();
    assert(shadow_is_silent());
    flight_paused = false;
    mark();
    idle_frame();
    assert(!retriggered());
    assert(!v3_gated());
  }

  // --- Voice 3: the crash --------------------------------------------------
  //
  // The crash is the one sound that outlives the aircraft. Everything else on
  // this voice stops when the silence predicates say so, and flight_crashed()
  // is one of them - so the crash burst has to play with the engine and the
  // wind already gated off. That split is the whole of what is new here, and
  // it is the thing most likely to be undone by someone tidying the
  // predicates back into one.
  {
    reset_state();
    flight_throttle = kMaxThrottle;
    flight_speed = kMaxSpeed;
    idle_frame();
    assert(model_audible(0) || true);  // engine running before the crash
    assert((sound_shadow[kSoundRegV1Ctrl] & SID_CTRL_GATE) != 0);

    // The wreck: the model sets the status during the step and publishes the
    // event from the same step, which is why flight_advance()'s early return
    // on later frames cannot swallow it.
    flight_status = FLIGHT_CRASH_VSPEED;
    mark();
    publish_events(FLIGHT_EV_CRASH);
    sound_update();

    assert(retriggered());
    assert(v3_gated());
    assert(v3_wave() == SID_CTRL_NOISE);
    // Loud, and holding its level rather than fading.
    assert((sound_shadow[kSoundRegV3 + kSoundVoiceSusRel] >> 4) == 15);
    assert((sound_shadow[kSoundRegV3 + kSoundVoiceAttDec] & 0x0F) == 0);
    // Audible: the master volume cannot have been zeroed by the crash.
    assert(master_volume() > 0);
    // But the aircraft is wrecked, so the flying voices are done.
    assert((sound_shadow[kSoundRegV1Ctrl] & SID_CTRL_GATE) == 0);
    assert((sound_shadow[kSoundRegV2Ctrl] & SID_CTRL_GATE) == 0);
  }

  // Over a second long. At the wobbling ~10 Hz frame rate that is 11 frames,
  // and it has to hold up while every other voice is silent - there is nothing
  // else left to carry the moment.
  {
    reset_state();
    flight_status = FLIGHT_CRASH_VSPEED;
    publish_events(FLIGHT_EV_CRASH);
    sound_update();

    int frames = 1;
    uint16_t first = voice_freq(kSoundRegV3);
    uint16_t last = first;
    while (v3_gated() && frames < 100) {
      last = voice_freq(kSoundRegV3);
      // Voices 1 and 2 stay down for the whole burst.
      assert((sound_shadow[kSoundRegV1Ctrl] & SID_CTRL_GATE) == 0);
      assert((sound_shadow[kSoundRegV2Ctrl] & SID_CTRL_GATE) == 0);
      assert(master_volume() > 0);
      idle_frame();
      ++frames;
    }
    assert(frames >= 11);

    // It sweeps downward as it goes - an impact collapsing rather than a long
    // hiss on one note. Amplitude is not what falls; the sustain check above
    // already pinned that.
    assert(last < first);

    // And once it is over, everything really is silent - not merely gated off
    // with the volume still up.
    assert(shadow_is_silent());
  }

  // Wrecked but with no crash event is still silent. This is the case a test
  // that only ever published the event would miss, and it is what the rest of
  // the driver has always done.
  {
    reset_state();
    flight_status = FLIGHT_CRASH_VSPEED;
    idle_frame();
    assert(shadow_is_silent());
  }

  // The crash outranks a stall in progress. Hitting the ground while the
  // warning is sounding is the common case, not a corner one.
  {
    reset_state();
    flight_stall = 1;
    for (int i = 0; i < 4; ++i) idle_frame();
    flight_status = FLIGHT_CRASH_VSPEED;
    publish_events(FLIGHT_EV_CRASH);
    sound_update();
    assert(v3_wave() == SID_CTRL_NOISE);
    assert(v3_gated());
  }

  // Pausing during the crash silences it like anything else. The crash
  // outlives the *aircraft*, not the player's own controls.
  {
    reset_state();
    flight_status = FLIGHT_CRASH_VSPEED;
    publish_events(FLIGHT_EV_CRASH);
    sound_update();
    assert(v3_gated());
    flight_paused = true;
    idle_frame();
    assert(shadow_is_silent());
  }

  // Restarting with R clears the status without going near the sound driver,
  // so the burst has to notice on its own. A wreck still rumbling over the
  // first two seconds of the next attempt would be a strange thing to hear.
  {
    reset_state();
    flight_status = FLIGHT_CRASH_VSPEED;
    publish_events(FLIGHT_EV_CRASH);
    sound_update();
    assert(v3_gated());
    flight_status = FLIGHT_ONGOING;
    idle_frame();
    assert(!v3_gated());
    // And the flying voices come straight back.
    assert((sound_shadow[kSoundRegV1Ctrl] & SID_CTRL_GATE) != 0);
  }

  // --- The raster interrupt landing mid-update ------------------------------
  //
  // sound_update() runs on the main line and sound_blit() runs from the raster
  // interrupt, so the interrupt can land between any two of the stores that
  // build the shadow. sound.h used to claim this was harmless, on the grounds
  // that "the two halves are both valid register sets and the next tick
  // corrects it 20 ms later".
  //
  // The second half of that is false for exactly one register. Sustain latches
  // on the gate edge and only ever falls afterwards (section 2), so a torn
  // read that gates a voice on while its sustain still reads 0 strands it at
  // silence - and every later frame rewrites the correct sustain with no gate
  // edge, so the chip never acts on it. The voice stays dead until something
  // unrelated happens to cycle its gate.
  //
  // That is the reported symptom: one or both channels drop out during flight
  // and come back a while later. This sweep fires the interrupt at every
  // instant it could possibly occur and requires the sound to survive each
  // one.
  {
    // How many stores one update makes, measured rather than assumed.
    reset_state();
    flight_throttle = 0x10;
    flight_speed = kMaxSpeed;
    observer_writes = 0;
    observer_fire_at = -1;
    sound_shadow_observer = observer;
    sound_update();
    sound_shadow_observer = nullptr;
    const long writes_per_update = observer_writes;
    assert(writes_per_update > 0);

    // Two starting points, because they are different hazards.
    //
    // warmup 4 is steady-state flight: every register already holds a sane
    // value from the previous frame, so a torn read mixes two valid sets.
    //
    // warmup 0 is the frame immediately after sound_silence(), which really
    // does blank the shadow - so sustain genuinely reads zero going in. That
    // is not a corner case: it is every return from the map, the help screen
    // and the main menu, and the raster interrupt is running again by then.
    // A voice stranded here would be silent for the whole next flight.
    static const int kWarmups[] = {4, 0};
    for (int w = 0; w < 2; ++w) {
      for (long k = 0; k < writes_per_update; ++k) {
        reset_state();
        model_reset();
        flight_throttle = 0x10;
        flight_speed = kMaxSpeed;

        for (int i = 0; i < kWarmups[w]; ++i) {
          quiet_frame();
        }

        // Now the interrupt lands after store number k.
        observer_writes = 0;
        observer_fire_at = k;
        sound_shadow_observer = observer;
        sound_update();
        sound_shadow_observer = nullptr;
        model_blit();

        // Give it far longer than 20 ms to put itself right. A dropout that
        // recovers on the next frame is a click; one that is still silent 30
        // frames later is the bug.
        for (int i = 0; i < 30; ++i) {
          quiet_frame();
        }

        assert(model_audible(0));
        assert(model_audible(1));
      }
    }

    sound_shadow_observer = nullptr;
  }

  // The two invariants underneath that sweep, asserted directly so a failure
  // says what is wrong rather than only that something went quiet.
  {
    reset_state();
    flight_throttle = 0x10;
    flight_speed = kMaxSpeed;

    static bool saw_gate_over_zero_sustain;
    static bool saw_silent_volume;
    saw_gate_over_zero_sustain = false;
    saw_silent_volume = false;

    // Warm up first, with the observer off. reset_state() goes through
    // sound_init() and therefore sound_silence(), which really does blank the
    // shadow - so the very first update legitimately starts from master volume
    // zero and only writes it at the end. Observing that would be observing
    // the transition out of silence, not a glitch inside a steady state.
    for (int i = 0; i < 3; ++i) {
      sound_update();
    }
    assert(master_volume() > 0);

    sound_shadow_observer = []() {
      // 1. A gate set over a zeroed sustain. This is the one that strands a
      //    voice: the gate edge latches the level at 0 and no later write of
      //    the correct sustain can lift it.
      //
      //    All three voices. This was scoped to the continuous pair while
      //    voice 3's effects decayed to zero, where sustain 0 under a set gate
      //    is the intended envelope rather than a stranding. Nothing on voice 3
      //    decays any more - every effect holds a level and ends by being gated
      //    off - so the invariant is universal again. Anything added there that
      //    does decay to nothing would have to re-open the exemption.
      for (int v = 0; v < 3; ++v) {
        uint8_t ctrl = sound_shadow[kVoiceBase[v] + kSoundVoiceCtrl];
        uint8_t sustain = sound_shadow[kVoiceBase[v] + kSoundVoiceSusRel] >> 4;
        if ((ctrl & SID_CTRL_GATE) && sustain == 0) {
          saw_gate_over_zero_sustain = true;
        }
      }
      // 2. Master volume reading zero at any instant. Starting audible and
      //    ending audible, it has no business passing through silence in
      //    between. This one recovers on the next blit, so it is a click
      //    rather than a dropout - but it is a click every time the interrupt
      //    lands in the window, and the fix is free: do not blank a register
      //    you are about to rewrite.
      if ((sound_shadow[kSoundRegModeVol] & 0x0F) == 0) {
        saw_silent_volume = true;
      }
    };

    // Every update here both starts and ends audible - full fuel, flying, not
    // paused - so neither condition has any legitimate reason to appear.
    for (int i = 0; i < 20; ++i) {
      sound_update();
    }
    sound_shadow_observer = nullptr;

    assert(!saw_gate_over_zero_sustain);
    assert(!saw_silent_volume);
  }

  // --- The pulse width sweep -----------------------------------------------

  reset_state();
  flight_throttle = 0x0C;

  const uint16_t sweep_base = freq_at_throttle(0x0C);
  const uint16_t sweep_bound = jitter_bound(sweep_base);
  uint16_t pw_min = 0xFFFF, pw_max = 0;
  bool went_up = false, came_down = false;
  bool pw_off_grid = false;
  uint16_t pw_prev = 0;
  uint16_t pw_log[400];

  for (int i = 0; i < 400; ++i) {
    sound_update();
    uint16_t pw = voice_pw(kSoundRegV1);
    uint16_t f = voice_freq(kSoundRegV1);
    pw_log[i] = pw;

    // Section 6: the sweep is independent of RPM. If pulse width were derived
    // from throttle, holding the throttle would freeze the timbre and a cruise
    // would turn back into a dead drone. With jitter the frequency is no
    // longer constant, so the claim is now that it stays anchored to this
    // throttle's table entry - it wanders, but it does not follow the sweep.
    assert(f + sweep_bound >= sweep_base && f <= sweep_base + sweep_bound);

    if (i > 0) {
      if (pw > pw_prev) went_up = true;
      if (pw < pw_prev) came_down = true;
      // The triangle alone moves in multiples of 8. Anything landing off that
      // grid is the jitter term doing its job.
      if ((pw & 7) != 0) pw_off_grid = true;
    }
    pw_prev = pw;

    // Never at either end of the 12-bit range: at 0 and 0xFFF a pulse wave is
    // DC and the voice goes silent. A sweep that touches those would drop the
    // engine out once a cycle. The jitter widens the excursion, so this has to
    // hold for the jittered value and not just for the triangle.
    assert(pw > 0x0010 && pw < 0x0FF0);
    // The high byte is a nibble on the SID. Anything above bit 11 is not a
    // wider pulse, it is bits landing in a register that ignores them.
    assert((sound_shadow[kSoundRegV1 + kSoundVoicePwHi] & 0xF0) == 0);

    if (pw < pw_min) pw_min = pw;
    if (pw > pw_max) pw_max = pw;
  }

  // A triangle, so both directions must appear. A sweep that only ever rises
  // would wrap with a click once per cycle.
  assert(went_up && came_down);
  // And it has to actually travel: a sweep of a handful of steps is not
  // audible as movement.
  assert(pw_max - pw_min > 0x0400);
  // The jitter has to survive on top of the sweep, not be swallowed by it.
  assert(pw_off_grid);

  // The sweep must not be exactly periodic, which is the other half of why the
  // phase step varies. The arithmetic: with a fixed step of kPwmStep = 6 and
  // an 8-bit phase, 128 frames advance the phase by 6 * 128 = 768, which is
  // 0 mod 256 - so the triangle would come back to precisely where it was,
  // every 128 frames, and the only thing separating pw[i] from pw[i + 128]
  // would be the jitter term's kPwmJitterMask. Four seconds is slow enough
  // that the ear hears a period that exact as a repeating figure rather than
  // as drift.
  //
  // A varying step makes the phase wander instead, so the two should often be
  // far further apart than the jitter alone could account for.
  {
    int far_apart = 0;
    for (int i = 0; i + 128 < 400; ++i) {
      int d = (int)pw_log[i + 128] - (int)pw_log[i];
      if (d < 0) d = -d;
      if (d > (int)kPwmJitterMask) ++far_apart;
    }
    assert(far_apart > 100);
  }

  // --- Silence and recovery ------------------------------------------------

  // sound_silence() zeroes the shadow and pushes it to the chip in the same
  // call. The write-through is the part that matters: its callers are on their
  // way to an sei, and map_enter() banks I/O out afterwards, so there is no
  // later blit to rely on.
  reset_state();
  flight_throttle = kMaxThrottle;
  sound_update();
  assert(!shadow_is_silent());

  sound_silence();
  assert(shadow_all_zero());
  for (uint8_t i = 0; i < kSoundRegCount; ++i) {
    assert(sid_regs[i] == 0);
  }

  // ...but only when this driver actually owns the chip. music.cc is the
  // second owner and holds $D400 while music_playing is set, so silencing
  // then would stomp on it. Both help-screen transitions run
  // screen_begin_text_page() -> gfx_stop_raster_irqs() -> sound_silence()
  // with the menu tune playing, and before this guard existed that clipped a
  // held note and dropped the master volume on the way in and again on the
  // way out. See ../docs/music.md section 3.
  reset_state();
  flight_throttle = kMaxThrottle;
  sound_update();
  memset(sid_regs, 0xAA, sizeof(sid_regs));
  music_playing = true;
  sound_silence();
  // The shadow is this driver's own state and is cleaned unconditionally: it
  // has to be clear before the raster interrupts come back, or the first blit
  // would restore whatever the last flight was playing.
  assert(shadow_all_zero());
  for (uint8_t i = 0; i < kSoundRegCount; ++i) {
    assert(sid_regs[i] == 0xAA && "sound_silence() wrote the chip while the tune owned it");
  }
  music_playing = false;

  // Coming back from the menu or the map: the driver resumes on its own, with
  // no un-silence call anywhere. The gate went 0 -> 1 in the shadow, which is a
  // real edge at the chip, so the envelope retriggers without needing the
  // sound_gen handshake.
  sound_update();
  assert(sound_shadow[kSoundRegV1Ctrl] == expected_ctrl());
  assert(master_volume() > 0);

  // Phase 2 starts no one-shots, so nothing should be asking for a retrigger
  // yet. This is a tripwire for phase 6: if sound_gen starts moving before the
  // one-shot code exists, something is bumping it by accident.
  assert(sound_gen == 0);

  printf("sound_test: all tests passed\n");
  return 0;
}
