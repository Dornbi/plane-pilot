#include "sound.h"

#ifdef __ENABLE_SOUND__

#include <string.h>

#include "flight.h"
#include "music.h"
#include "sound_asm.h"
#include "vec.h"

// Not reset by sound_init(): see the comment in sound.h. This is the only
// piece of driver state that outlives a flight.
uint8_t sound_volume = kSoundVolumeDefault;

// Shared shadow/state variables and SID lookup tables (kMasterVolume,
// kSoundEngineFreqLo/Hi, kSoundWindFreqLo/Hi, kSoundV3*) live in sound_asm.cc
// so both the 6502 sound_update() (sound_asm.cc) and the host C counterpart
// below share one copy. See sound_asm.h for the parameter constants and
// ../docs/sound.md for the audio design rationale.

// --- Driver ----------------------------------------------------------------

#ifndef __OSCAR64__
static uint8_t _sound_next_rand(void) {
  bool lsb = _sound_rng & 1;
  _sound_rng >>= 1;
  if (lsb) {
    _sound_rng ^= kSoundRngTaps;
  }
  return _sound_rng;
}

void (*sound_shadow_observer)(void) = nullptr;
#define SHADOW_OBSERVE()                                                       \
  do {                                                                         \
    if (sound_shadow_observer)                                                 \
      sound_shadow_observer();                                                 \
  } while (0)

static void _sound_poke(uint8_t idx, uint8_t val) {
  ((volatile uint8_t *)sound_shadow)[idx] = val;
  SHADOW_OBSERVE();
}
#endif

// Writes the chip directly, bypassing the shadow. Only for sound_silence()
// below, which runs when sound_blit() either has not started yet or is about
// to be masked - so nothing else is going to push the shadow out. The shadow
// has just been zeroed, so this stores zeros rather than copying it.
//
// oscar64 miscompiled the rolled copy of the shadow, the ascending form of
// sound_blit()'s loop, into
//
//     LDA sound_shadow,y
//     STA $D400          ; index dropped - absolute, not $D400,y
//     INY
//
// so all 25 bytes landed on voice 1's frequency low byte and every other
// register kept whatever it held. That silenced nothing: the gates at $D404,
// $D40B and $D412 and the master volume at $D418 all survived
// sound_silence(), and since its callers go on to mask the raster IRQs, the
// engine drone was left running for as long as the map or the help screen was
// up. The copy was then fully unrolled, 25 absolute stores (75 B). This
// counted-down store of a constant compiles to DEX / STA $D400,x / BNE, the
// index kept.
//
// The host build compiles either form correctly, so sound_test.cc cannot see
// any of this; it is a target-only defect and the .asm is the only place it
// shows. Worth re-reading sound_silence in fpilot.asm after an oscar64
// upgrade.
static inline void _sound_write_through(void) {
  uint8_t i = kSoundRegCount;
  do {
    SID_REGS[--i] = 0;
  } while (i != 0);
}

// Note on voice register write order in sound_update():
// The control register on each voice goes LAST.
//
// sound_blit() can fire between any two shadow stores, so it can copy a
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

// The derived half of the silence rule. The other half - menu, help and map -
// is sound_silence() below, called from gfx_stop_raster_irqs(). These three
// need no call at all, because the simulation loop keeps running and this is
// re-evaluated every frame.
//
// Crashed is checked here rather than trusted to the flight state going quiet
// on its own: flight_advance() returns early once wrecked, so every input to
// this function holds whatever value it had at the moment of the crash - a
// stalled aircraft that hits the ground would otherwise leave its engine and,
// its stall warning running forever.
// Hard silence: nothing plays at all, not even the crash. Both terms are the
// player's own doing - the volume key and the pause key - which is what makes
// this the level at which "no sound" means no sound.
#ifndef __OSCAR64__
static bool _sound_driver_live(void) {
  return sound_volume != 0 && !flight_paused;
}
#endif

// The aircraft is in a state that makes flying noises. The continuous voices
// key off this; voice 3 does not, because the crash sound has to outlive it.
//
// Crashed is checked here rather than trusted to the flight state going quiet
// on its own: flight_advance() returns early once wrecked, so every input to
// this function holds whatever value it had at the moment of the crash - a
// stalled aircraft that hits the ground would otherwise leave its engine and
// its stall warning running forever.
#ifndef __OSCAR64__
static bool _sound_flying(bool driver_live) {
  return driver_live && !flight_crashed() && flight_fuel != 0;
}
#endif

// Cycling up and wrapping, so from the default of full the first press is
// silence.
//
// No write-through of its own is needed. sound_update() runs every frame and
// will rewrite the shadow on the next one, and the blit pushes that to the chip
// 20 ms later - so the change lands within about a frame, which at the ~10 Hz
// simulation rate is as immediate as any other control. The write-through in
// sound_silence() exists only because its callers are about to mask interrupts
// or bank I/O out, and neither is true here.
// Indexed by sound_volume, which sound_cycle_volume() keeps in range.
static const char *const kSoundVolumeLabels[kSoundVolumeSteps] = {
    "SOUND OFF", "SOUND LOW", "SOUND FULL"};

const char *sound_volume_label(void) {
  return kSoundVolumeLabels[sound_volume < kSoundVolumeSteps
                                ? sound_volume
                                : kSoundVolumeDefault];
}

// Downward: full -> low -> off -> full. See the argument in sound.h; the short
// version is that a volume key's presses should all mean the same thing, and
// upward made the first press mean "off" and the second mean "quiet".
void sound_cycle_volume(void) {
  if (sound_volume == 0) {
    sound_volume = kSoundVolumeSteps - 1;
  } else {
    --sound_volume;
  }
}

#ifndef __OSCAR64__
uint16_t sound_engine_base_freq(uint8_t throttle) {
  if (throttle > kMaxThrottle) {
    throttle = kMaxThrottle;
  }
  return (uint16_t)((uint16_t)kSoundEngineFreqHi[throttle] << 8) |
         kSoundEngineFreqLo[throttle];
}

__forceinline uint16_t sound_wind_freq(int16_t speed) {
  if (speed < 0) {
    speed = 0;
  }
  uint8_t idx = (uint8_t)((uint16_t)speed >> kSoundWindSpeedShift);
  if (idx >= kSoundWindSteps) {
    idx = kSoundWindSteps - 1;
  }
  return (uint16_t)((uint16_t)kSoundWindFreqHi[idx] << 8) |
         kSoundWindFreqLo[idx];
}

bool sound_wind_audible(int16_t speed) {
  return speed >= (int16_t)kSoundWindMinSpeed;
}
#endif

void sound_init(void) {
  sound_gen = 0;
  sound_gen_seen = 0;
  _sound_pwm_phase = 0;
  _sound_rng = kSoundRngSeed;
  _sound_v3_effect = V3_NONE;
  _sound_v3_frames = 0;
  _sound_stall_phase = 0;
  // Deliberately synchronised with whatever the model has published so far,
  // rather than zeroed. Starting from a stale value would make the first frame
  // of a flight look like a new generation and fire whatever event happened to
  // be left in flight_events from the previous one.
  _sound_flight_gen_seen = flight_gen;
  sound_silence();
}

void sound_silence(void) {
  memset(sound_shadow, 0, kSoundRegCount);

  // "Release the SID" - which is a no-op if this driver is not the one holding
  // it. music.cc is the second owner (../docs/music.md section 3), and it owns
  // $D400 exactly while music_playing is set.
  //
  // Without this guard the menu tune is silenced every time the player opens
  // or closes the help screen, because both transitions run
  // screen_begin_text_page() -> gfx_stop_raster_irqs() -> here. That clipped a
  // held note and dropped the master volume until the next music_tick()
  // restored it, for a gap on the way in and another on the way out.
  //
  // The shadow is still zeroed above, and unconditionally: it belongs to this
  // driver rather than to the chip, and it has to be clean before the raster
  // interrupts come back or the first blit would restore whatever the last
  // flight was playing.
  if (music_playing) {
    return;
  }

  // The chip has to be written here and not left to the next blit: the
  // callers are on their way to an sei, and map_enter() additionally banks
  // I/O out, so $D400 is about to stop being the SID for a while.
  _sound_write_through();
}

#ifndef __OSCAR64__
static bool _sound_pick_v3(uint8_t events, uint8_t from, uint8_t to) {
  if (events == 0) {
    return false;
  }
  for (uint8_t i = from; i < to; ++i) {
    if (events & kSoundV3Ev[i]) {
      _sound_v3_effect = kSoundV3Id[i];
      _sound_v3_frames = kSoundV3Dur[i];
      return true;
    }
  }
  return false;
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
  uint8_t r_pw = _sound_next_rand();
  uint8_t r_pitch = _sound_next_rand();

  // Stepping the phase by a varying amount is half the grit: a fixed step
  // makes the sweep exactly periodic, and at four seconds a period that is
  // slow enough to be heard as a repeating figure rather than as drift.
  _sound_pwm_phase += kSoundPwmStep + (r_pw & 3);

  // No zeroing pass. An earlier version wiped the whole shadow here and then
  // filled in whatever was audible, which read well but left the shadow
  // momentarily all zeros - and an interrupt landing in that window blitted
  // master volume 0 and every gate clear to the chip. That much was
  // self-correcting, but it also meant every voice was written from a zeroed
  // sustain, which is not (see _sound_set_voice).
  //
  // So every register is written every frame with its final value, silent or
  // not, and silence is expressed as gates clear and master volume zero rather
  // than as an absence of writes. A torn read then mixes two valid register
  // sets instead of a valid one with a blank.
  const bool driver_live = _sound_driver_live();
  const bool flying = _sound_flying(driver_live);

  // Triangle from the 8-bit phase: count up over the bottom half, back down
  // over the top. Scaled by 8 into the 12-bit pulse width register, then
  // knocked off it by a small random offset - the other half of the grit.
  uint8_t tri = (uint8_t)(_sound_pwm_phase << 1);
  if (_sound_pwm_phase & 0x80) {
    tri ^= 0xFE;
  }
  uint16_t pw =
      kSoundPwmMin + ((uint16_t)tri << 3) + (r_pw & kSoundPwmJitterMask);

  // Pitch jitter, proportional to the pitch so idle and full power are equally
  // unsteady. amp is the full-scale deviation and n scales it to a signed
  // fraction of itself, so the result is uniform over -amp .. +amp.
  //
  // Both fit comfortably in 16 bits: amp is at most 1788 >> 5 = 55 and n is
  // -16 .. 15, so the product never leaves a signed byte's worth of headroom.
  // A different jitter shift changes amp, not the arithmetic.
  //
  // n is data, not a constant, so there is no shift chain that computes this;
  // the product goes through vec_fastmul8p8 instead of oscar64's mul16, which
  // is what lets mul16 drop out of the link entirely (docs/sound.md section 9
  // costed this as a shift rewrite, but that would have to change the jitter's
  // distribution to get rid of the variable multiplier). Passing (amp << 1)
  // and ((r_pitch & 0x1F) << 3) as unsigned bytes hits vec_fastmul8p8's
  // single-byte fast path to compute (amp * (r_pitch & 0x1F)) >> 4 directly,
  // and subtracting amp yields floor(amp * ((r_pitch & 0x1F) - 16) / 16).
  //
  // vec_fastmul8p8 clobbers the shared zero-page scratch in vec_asm.cc. That
  // is safe here for the same reason the rest of this function is main-line
  // only: sound_update() is called from sim_frame(), never from a raster IRQ.
  uint16_t base = sound_engine_base_freq(flight_throttle);
  uint8_t amp = (uint8_t)(base >> kSoundEngineJitterShift);
  uint16_t freq =
      (uint16_t)((int16_t)base +
                 vec_fastmul8p8((uint8_t)(amp << 1),
                                (uint8_t)((r_pitch & 0x1F) << 3)) -
                 (int16_t)amp);

  // --- Voice 3 arbitration -------------------------------------------------
  //
  // Priority, from section 6:  crash > touchdown > stall > gear > flap
  //
  // The crash sits above all of it and is the only effect that plays while
  // _flying() is false. Nothing preempts it once started - there is nothing
  // left that could, since flight_advance() stops publishing events the moment
  // the aircraft is wrecked.
  //
  // A one-shot already in progress keeps the voice until it expires; only
  // touchdown preempts. A gear or flap event that arrives while something
  // outranks it is DROPPED, not queued - queuing would surface a flap click
  // hundreds of milliseconds after the key press, which reads as a bug rather
  // than as feedback.
  //
  // The stall is not dropped in any meaningful sense, because it is a level
  // rather than an edge: it simply takes the voice back on the first frame
  // nothing outranks it.

  // Consume this frame's events, at most once. Reading the generation first is
  // what makes that true - flight_advance() bumps it after writing the set, so
  // a new generation guarantees a complete one, and a wrecked aircraft stops
  // bumping it and therefore stops retriggering its last event forever.
  uint8_t events = 0;
  if (flight_gen != _sound_flight_gen_seen) {
    _sound_flight_gen_seen = flight_gen;
    events = flight_events;
  }

  bool retrigger = false;
  // Only set while the stall owns the voice during the off-half of its warble.
  bool stall_gap = false;

  if (_sound_v3_frames != 0) {
    --_sound_v3_frames;
  }

  if (!driver_live) {
    // Nothing survives a pause or the volume key. Dropping the effect here
    // rather than letting it run down means unpausing does not resume a
    // half-finished gear click.
    _sound_v3_effect = V3_NONE;
    _sound_v3_frames = 0;
  } else if (events & FLIGHT_EV_CRASH) {
    _sound_v3_effect = V3_CRASH;
    _sound_v3_frames = kSoundCrashFrames;
    retrigger = true;
  } else if (_sound_v3_effect == V3_CRASH) {
    // The crash burst runs to the end, uninterrupted, unless the burst is over
    // or an `R` restart cleared the status without going near the sound driver.
    // Dropped explicitly rather than left to fall through: the "a one-shot is
    // still running" branch below would otherwise adopt it and keep it going.
    if (_sound_v3_frames == 0 || !flight_crashed()) {
      _sound_v3_effect = V3_NONE;
      _sound_v3_frames = 0;
    }
  } else if (!flying) {
    // Out of fuel, or wrecked with the crash burst already finished.
    _sound_v3_effect = V3_NONE;
    _sound_v3_frames = 0;
  } else if (_sound_pick_v3(events, 0, 2)) {
    // Touchdown outranks stall/one-shots; hit outranks any active one-shot.
    retrigger = true;
  } else if (_sound_v3_frames != 0 && _sound_v3_effect != V3_STALL) {
    // A one-shot still running. Left alone.
  } else if (flight_stall_warning()) {
    // The warble: gated on for the first kSoundStallOnFrames of each period,
    // off for the rest. Both halves are stated here rather than one being left
    // to an envelope running out.
    if (_sound_v3_effect != V3_STALL) {
      _sound_stall_phase = 0;
    }
    _sound_v3_effect = V3_STALL;
    if (_sound_stall_phase == 0) {
      retrigger = true;
    }
    stall_gap = _sound_stall_phase >= kSoundStallOnFrames;
    if (++_sound_stall_phase >= kSoundStallPeriodFrames) {
      _sound_stall_phase = 0;
    }
  } else if (_sound_pick_v3(events, 2, 5)) {
    // Gun, gear, flap sit under stall.
    retrigger = true;
  } else if (_sound_v3_frames == 0) {
    _sound_v3_effect = V3_NONE;
  }

  // Only the main line writes sound_gen; only sound_blit() writes
  // sound_gen_seen. One writer per byte, so neither side needs to mask
  // interrupts around it.
  if (retrigger) {
    ++sound_gen;
  }

  // Registers that never move off zero (V2 PwLo/PwHi, V3 PwLo, and the
  // three unused filter registers) keep the zero sound_silence() wrote at init
  // and on every screen transition. On each voice, Ctrl still goes last so a
  // torn read by sound_blit() can never see a gate edge over a zeroed sustain.
  const uint8_t effect = _sound_v3_effect;
  {
    uint16_t v3_freq = (uint16_t)((uint16_t)kSoundV3FreqHi[effect] << 8);
    if (effect == V3_CRASH) {
      // Sweeps down as the burst runs. _sound_v3_frames counts from
      // kSoundCrashFrames (16) to zero, and the span per frame is 224 = 7 * 32,
      // so 0x0200 + 224 * n == ((7 * n + 16) << 8) >> 3 with 7 * n + 16 <= 128.
      uint8_t m = (uint8_t)((_sound_v3_frames << 3) - _sound_v3_frames +
                            (uint8_t)(kSoundCrashFreqEnd >> 5));
      v3_freq = ((uint16_t)((uint16_t)m << 8)) >> 3;
    }
    _sound_poke(kSoundRegV3 + kSoundVoiceFreqLo, (uint8_t)v3_freq);
    _sound_poke(kSoundRegV3 + kSoundVoiceFreqHi, (uint8_t)(v3_freq >> 8));
    _sound_poke(kSoundRegV3 + kSoundVoicePwHi,
                effect == V3_STALL ? (uint8_t)(kSoundStallPw >> 8) : 0);
    _sound_poke(kSoundRegV3 + kSoundVoiceAttDec, kSoundV3AttDec[effect]);
    _sound_poke(kSoundRegV3 + kSoundVoiceSusRel, kSoundV3SusRel[effect]);
    _sound_poke(kSoundRegV3 + kSoundVoiceCtrl,
                stall_gap ? SID_CTRL_RECT : kSoundV3Ctrl[effect]);
  }

  // Voice 1: engine. Gate clear when inaudible rather than the whole voice
  // blanked, so the envelope releases instead of being cut dead, and so the
  // sustain register is never zero underneath a gate that is about to be set.
  _sound_poke(kSoundRegV1 + kSoundVoiceFreqLo, (uint8_t)freq);
  _sound_poke(kSoundRegV1 + kSoundVoiceFreqHi, (uint8_t)(freq >> 8));
  _sound_poke(kSoundRegV1 + kSoundVoicePwLo, (uint8_t)pw);
  _sound_poke(kSoundRegV1 + kSoundVoicePwHi, (uint8_t)(pw >> 8) & 0x0F);
  _sound_poke(kSoundRegV1 + kSoundVoiceAttDec, kSoundEngineAttDec);
  _sound_poke(kSoundRegV1 + kSoundVoiceSusRel, kSoundEngineSusRel);
  _sound_poke(kSoundRegV1 + kSoundVoiceCtrl,
              flying ? (SID_CTRL_RECT | SID_CTRL_GATE) : SID_CTRL_RECT);

  // Voice 2: wind, which additionally gates off below the speed threshold.
  {
    uint16_t wind_freq = sound_wind_freq(flight_speed);
    _sound_poke(kSoundRegV2 + kSoundVoiceFreqLo, (uint8_t)wind_freq);
    _sound_poke(kSoundRegV2 + kSoundVoiceFreqHi, (uint8_t)(wind_freq >> 8));
    _sound_poke(kSoundRegV2 + kSoundVoiceAttDec, kSoundWindAttDec);
    _sound_poke(kSoundRegV2 + kSoundVoiceSusRel, kSoundWindSusRel);
    _sound_poke(kSoundRegV2 + kSoundVoiceCtrl,
                (flying && sound_wind_audible(flight_speed))
                    ? (SID_CTRL_NOISE | SID_CTRL_GATE)
                    : SID_CTRL_NOISE);
  }

  // Master volume last. It is the one register with no latching behaviour, so
  // a torn read that catches the old value is a single frame at the wrong
  // level and nothing more - which makes it the safest thing to leave until
  // the end.
  //
  // The index is guarded rather than trusted: sound_volume is written from the
  // key handler, and an out-of-range value would index past the table and put
  // an arbitrary byte in $D418 - where the high nibble is the filter mode, so
  // the symptom would be a filter switching on rather than a wrong volume.
  //
  // On whenever something could be sounding, which is no longer the same as
  // "flying": the crash burst plays with the continuous voices already gated
  // off. When !driver_live, both flying and effect are already false/V3_NONE.
  _sound_poke(kSoundRegModeVol,
              (flying || effect != V3_NONE)
                  ? kMasterVolume[sound_volume < kSoundVolumeSteps
                                      ? sound_volume
                                      : kSoundVolumeDefault]
                  : 0);

  // The shadow is whole again: the next sound_blit() takes it (sound.h).
  sound_mark_dirty();
}
#endif // !__OSCAR64__

#endif // __ENABLE_SOUND__
