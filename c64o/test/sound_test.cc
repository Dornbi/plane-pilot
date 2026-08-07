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
  memset(sid_regs, 0xAA, sizeof(sid_regs));
  sound_volume = kSoundVolumeDefault;
  sound_init();
}

static uint8_t master_volume(void) {
  return sound_shadow[kSoundRegModeVol] & 0x0F;
}

static uint16_t voice_freq(uint8_t base) {
  return (uint16_t)sound_shadow[base + kSoundVoiceFreqLo] |
         ((uint16_t)sound_shadow[base + kSoundVoiceFreqHi] << 8);
}

static uint16_t voice_pw(uint8_t base) {
  return (uint16_t)sound_shadow[base + kSoundVoicePwLo] |
         ((uint16_t)sound_shadow[base + kSoundVoicePwHi] << 8);
}

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
  assert(shadow_all_zero());

  reset_state();
  flight_throttle = kMaxThrottle;
  flight_status = FLIGHT_CRASH_VSPEED;
  sound_update();
  assert(shadow_all_zero());

  // A completed mission is not a crash: the simulation keeps running and so
  // does the engine. This is the case a plain truth test on flight_status
  // would get wrong.
  reset_state();
  flight_throttle = kMaxThrottle;
  flight_status = FLIGHT_MISSION_COMPLETED;
  sound_update();
  assert(!shadow_all_zero());

  reset_state();
  flight_throttle = kMaxThrottle;
  flight_fuel = 0;
  sound_update();
  assert(shadow_all_zero());

  // Fuel of 1 is still fuel. The boundary matters because the flight model
  // drains toward zero rather than jumping to it.
  reset_state();
  flight_throttle = kMaxThrottle;
  flight_fuel = 1;
  sound_update();
  assert(!shadow_all_zero());

  // --- The V key -----------------------------------------------------------

  // The cycle order. Default is full, and V wraps upward, so the first press
  // from a fresh start silences the game - which is what a player reaching for
  // an unfamiliar key most likely wants.
  reset_state();
  assert(sound_volume == kSoundVolumeDefault);
  assert(kSoundVolumeDefault == kSoundVolumeSteps - 1);
  for (uint8_t i = 0; i < kSoundVolumeSteps; ++i) {
    sound_cycle_volume();
    assert(sound_volume == i);
  }
  // Cycling all the way round returns to where it started, rather than
  // sticking at the top or running off the end of the volume table.
  assert(sound_volume == kSoundVolumeDefault);

  // Step 0 goes through the same predicate as pause and crash, so it silences
  // the whole driver rather than one voice - which is what has to stay true as
  // phases 3 to 6 add voices that the volume control knows nothing about.
  reset_state();
  flight_throttle = kMaxThrottle;
  sound_volume = 0;
  sound_update();
  assert(shadow_all_zero());

  // The two audible steps have to be audible, ordered, and distinct. A middle
  // step equal to full is a control that does nothing on two of its three
  // settings; a middle step of 0 is a second, silent "off".
  uint8_t vol_at_step[kSoundVolumeSteps];
  for (uint8_t step = 1; step < kSoundVolumeSteps; ++step) {
    reset_state();
    flight_throttle = kMaxThrottle;
    sound_volume = step;
    sound_update();
    assert(!shadow_all_zero());
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
      assert(shadow_all_zero());
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

  // Voice 3 is still unclaimed - it arrives with the stall warning in phase 5 -
  // and until then it must be gated off, not left holding whatever the
  // previous frame put there.
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
  assert(shadow_all_zero());

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
  assert(!shadow_all_zero());

  sound_silence();
  assert(shadow_all_zero());
  for (uint8_t i = 0; i < kSoundRegCount; ++i) {
    assert(sid_regs[i] == 0);
  }

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
