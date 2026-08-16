#include "music.h"

#ifdef __ENABLE_SOUND__

#include "sound.h"

// Phases 2 to 5 of ../docs/music.md: the player skeleton, the ownership guard,
// and all three voices.

bool music_playing = false;

// --- The row clock ---------------------------------------------------------
//
// One tick is one video frame. kMusicSpeed frames make a row, kMusicRowsPerBar
// rows make a bar, and the whole tune is kMusicTotalRows rows.
//
// Counted rather than derived: the C64 has no divide, and this is the
// innermost thing the player does. There is deliberately no frame counter -
// nothing reads one, and unused state is what section 4 warns about.
static uint16_t _music_row;      // 0 .. kMusicTotalRows-1
static uint8_t _music_row_frame; // 0 .. kMusicSpeed-1
static uint8_t _music_bar;       // 0 .. kMusicBars-1

// The pulse width sweep, a triangle over 0x800. The step is 8 because that
// makes the cycle 256 frames, which divides the 2304-frame loop exactly nine
// times - a step that did not divide the loop would leave the pulse width
// somewhere different on every pass. See ../docs/music.md section 6.
static const uint16_t kMusicPwmStep = 8;
static const uint16_t kMusicPwmRange = 0x800;
static const uint16_t kMusicPwmBase = 0x0300;
static uint16_t _music_pwm_phase;

// --- Voice 3 ----------------------------------------------------------------
//
// Shared between the arpeggio and the drums, which is how every SID tune got
// four parts out of three voices. The arpeggio is a texture and loses 40 to
// 100 ms without anyone noticing; a kick that is not on the beat is not a kick.
//
// _music_v3_owner is kMusicV3Arp, kMusicV3Restart, or MUSIC_DRUM_AT()'s code (1
// kick, 2 snare, 3 hat) while a drum holds the voice. There is no priority
// logic between the drums themselves - get_flattened_drums() resolves that when
// it builds the table, so a row carries at most one hit.
//
// kMusicV3Restart is the state between a hit ending and the arpeggio coming
// back, and it exists because of the SID's ADSR delay bug. Gating a voice on
// while its envelope counter is somewhere awkward does not restart the
// envelope; the counter has to wrap its full 15-bit range first, which can take
// the best part of a second. The cure every SID player uses is the *hard
// restart*: hold the gate low with the attack/decay and sustain/release
// registers at zero for a couple of frames, which forces the counter down, and
// only then gate on.
//
// Handing straight back from a hit to the arpeggio skipped that. The gate went
// low for one frame with the drum's sustain still in the register, and the
// arpeggio's envelope never climbed. On hardware the symptom was an arpeggio
// audible only in bars 1 to 4 - the one stretch preceded by a real hard
// restart, because music_start() zeroes every envelope register before the
// first gate rises - and, after a loop, silent for two more bars while the
// counter wrapped.
static const uint8_t kMusicV3Arp = 0;
static const uint8_t kMusicV3Restart = 0xFF;

static uint8_t _music_v3_owner;
static uint8_t
    _music_v3_timer; // frames left in the current hit, or in the restart
static uint16_t
    _music_v3_freq; // swept down by _music_v3_step while a drum holds it
static uint16_t _music_v3_step;

// Which chord tone the arpeggio is on, 0..2. Advanced once per frame, but only
// while the arpeggio actually owns the voice - a hit does not move it, so the
// shimmer resumes where it left off instead of jumping.
static uint8_t _music_arp_idx;

// --- Volume ----------------------------------------------------------------
//
// The tune's per-bar ramp and the player's V-key setting both want $D418's low
// nibble, and neither may override the other. A table rather than a multiply:
// no divide, no rounding question, and row 0 being all zeros means "sound off"
// is silent by construction rather than by a predicate someone has to remember
// to check. Row 1 scales the ramp to a ceiling of 7 instead of clipping it,
// which is what keeps the opening fade a fade at the low setting.
//
// 48 bytes. See ../docs/music.md section 3.
static const uint8_t kMusicVolumeMix[kSoundVolumeSteps][16] = {
    // off
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    // low: round(v * 7 / 15)
    {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7},
    // full: identity
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
};

uint8_t music_master_volume(uint8_t bar_volume, uint8_t setting) {
  if (setting >= kSoundVolumeSteps) {
    setting = kSoundVolumeDefault;
  }
  return kMusicVolumeMix[setting][bar_volume & 0x0F];
}

// --- Pitch -----------------------------------------------------------------
//
// A SID frequency halves exactly per octave, so twelve entries for octave 6
// and a right shift cover the whole range. Worst error over the notes this
// tune uses is 0.090%, against 5.95% for a semitone.
//
// MIDI 84 is C6, so the shift is (6 - octave) where octave = midi/12 - 1,
// i.e. 12 - midi/12. Notes above octave 6 would need a left shift and would
// overflow; the arrangement does not have any and the exporter rejects them,
// so this clamps instead of shifting the wrong way.
uint16_t music_note_freq(uint8_t midi) {
  if (midi < 12) {
    return 0;
  }
  uint8_t octave = 0;
  while (midi >= 24) {
    midi -= 12;
    octave++;
  }
  uint8_t pc = midi - 12;
  uint16_t f = kMusicNoteTable[pc];
  if (octave >= 6) {
    return f;
  }
  return f >> (6 - octave);
}

// --- Voice writes ----------------------------------------------------------
//
// **$D400-$D418 are WRITE-ONLY.** Reading them on real hardware returns
// whatever was last on the data bus, not what was written there. So the player
// may never read a SID register back, and it needs the one piece of register
// state it makes decisions from - the control byte, for its gate bit - kept in
// RAM instead.
//
// This is the same reason sound.cc keeps sound_shadow[]: that module's blit is
// a pure store sequence and it decides everything from the shadow. This one
// needs only three bytes, because the gate bit is the only thing it ever asks
// the chip about.
//
// The failure mode when this is got wrong is not a crash and not silence: it
// is a voice that sounds in some bars and not others, depending on what the
// VIC-II happened to leave on the bus. It is also invisible to every host
// test, because on the host SID_REGS points at ordinary RAM and reads back
// exactly what was written.
static uint8_t _ctrl[3];

// Voice indices, deliberately distinct from the kSoundRegV* register offsets:
// _ctrl is indexed by voice, the chip by register.
static const uint8_t kVoice1 = 0;
static const uint8_t kVoice2 = 1;
static const uint8_t kVoice3 = 2;
static const uint8_t kVoiceRegs = 7;

// The control register, to the chip and to the shadow, always together.
static void _write_ctrl(uint8_t voice, uint8_t ctrl) {
  _ctrl[voice] = ctrl;
  SID_REGS[voice * kVoiceRegs + kSoundVoiceCtrl] = ctrl;
}

// The control register goes last. Not for sound.cc's torn-read reason - that
// cannot happen here - but because the SID latches sustain on the gate edge,
// so a gate raised before its envelope registers are in place latches whatever
// was there before.
static void _set_voice(uint8_t voice, uint16_t freq, uint16_t pw, uint8_t ctrl,
                       uint8_t attdec, uint8_t susrel) {
  const uint8_t base = voice * kVoiceRegs;
  SID_REGS[base + kSoundVoiceFreqLo] = (uint8_t)freq;
  SID_REGS[base + kSoundVoiceFreqHi] = (uint8_t)(freq >> 8);
  SID_REGS[base + kSoundVoicePwLo] = (uint8_t)pw;
  SID_REGS[base + kSoundVoicePwHi] = (uint8_t)((pw >> 8) & 0x0F);
  SID_REGS[base + kSoundVoiceAttDec] = attdec;
  SID_REGS[base + kSoundVoiceSusRel] = susrel;
  _write_ctrl(voice, ctrl);
}

static void _set_freq(uint8_t voice, uint16_t freq) {
  const uint8_t base = voice * kVoiceRegs;
  SID_REGS[base + kSoundVoiceFreqLo] = (uint8_t)freq;
  SID_REGS[base + kSoundVoiceFreqHi] = (uint8_t)(freq >> 8);
}

static void _gate_off(uint8_t voice) {
  _write_ctrl(voice, (uint8_t)(_ctrl[voice] & ~SID_CTRL_GATE));
}

static bool _gated(uint8_t voice) {
  return (_ctrl[voice] & SID_CTRL_GATE) != 0;
}

// Hard restart. On the frame before a new note, drop the gate and zero the
// envelope registers so the counter is forced down and the next gate edge
// starts from silence. Without it a new note begins wherever the previous
// envelope happened to be, and the SID does not reliably retrigger on a gate
// that goes low and high within a few cycles.
//
// Applied only where the *next* row actually starts a note, so held notes
// never pay for it. See ../docs/music.md section 3.
static void _music_hard_restart_restart(uint8_t voice) {
  const uint8_t base = voice * kVoiceRegs;
  _gate_off(voice);
  SID_REGS[base + kSoundVoiceAttDec] = 0;
  SID_REGS[base + kSoundVoiceSusRel] = 0;
}

// --- Public interface ------------------------------------------------------

void music_start(void) {
  _music_row = 0;
  _music_row_frame = 0;
  _music_bar = 0;
  _music_pwm_phase = 0;
  // Voice 3 starts in the restart state, not straight into the arpeggio - the
  // same state the loop point puts it in. That is what makes "the loop is
  // identical to a fresh start" true rather than nearly true, and it is the
  // property test_loop_identity checks.
  _music_v3_owner = kMusicV3Restart;
  _music_v3_timer = kV3LoopRestartFrames;
  _music_v3_freq = 0;
  _music_v3_step = 0;
  _music_arp_idx = 0;

  // The chip already arrives silent - gfx_stop_raster_irqs() calls
  // sound_silence(), which write-throughs zeros to $D400 on its way to the
  // sei - so this is belt and braces. It is kept because the dependency runs
  // through two modules and a screen transition, and the failure it guards
  // against is a voice from the previous flight droning under the menu.
  for (uint8_t v = 0; v < 3; ++v) {
    _set_voice(v, 0, 0, 0, 0, 0);
  }

  // The filter, explicitly. music_tick() never touches $D415-$D417, so
  // whatever is in them when the tune starts stays there for its whole run -
  // and the low nibble of $D417 routes voices *into* the filter. A voice
  // routed into a filter whose cutoff is 0 is silent, which sounds exactly
  // like that voice not being written at all.
  //
  // Until now this worked only because sound.cc happens to zero the same three
  // registers on every frame of every flight, and sound_silence() zeroes them
  // on the way to the menu. That is a dependency on another module's
  // housekeeping, running through a screen transition, for a register this one
  // never writes - which is the shape of a bug that appears years later when
  // something else starts using the filter. ../docs/music.md section 3 says
  // the design does not depend on the filter; this is what makes that true
  // rather than merely intended.
  SID_REGS[kSoundRegCutoffLo] = 0;
  SID_REGS[kSoundRegCutoffHi] = 0;
  SID_REGS[kSoundRegResFilt] = 0;

  music_playing = true;
}

void music_stop(void) {
  music_playing = false;
  for (uint8_t v = 0; v < 3; ++v) {
    _set_voice(v, 0, 0, 0, 0, 0);
  }
  SID_REGS[kSoundRegModeVol] = 0;
}

void music_tick(void) {
  // The guard that keeps the in-flight help screen silent. See music.h.
  if (!music_playing) {
    return;
  }

  const bool last_frame_of_row = (_music_row_frame == kMusicSpeed - 1);
  const uint16_t next_row =
      (_music_row + 1 == kMusicTotalRows) ? 0 : (_music_row + 1);

  // --- voice 1: lead ---
  //
  // Two independent conditions, not an if/else chain. They are mutually
  // exclusive only because kMusicSpeed is 6; writing them as alternatives
  // would quietly stop being correct at speed 1.
  if (last_frame_of_row && kMusicLeadStart[next_row] != 0) {
    _music_hard_restart_restart(kVoice1);
  }
  if (_music_row_frame == 0) {
    const uint8_t note = kMusicLeadStart[_music_row];
    if (note != 0) {
      _set_voice(kVoice1, music_note_freq(note), kMusicPwmBase,
                 kMusicInsLead.wave | SID_CTRL_GATE, kMusicInsLead.ad,
                 kMusicInsLead.sr);
    } else if (!MUSIC_LEAD_ON(_music_row)) {
      _gate_off(kVoice1);
    }
  }

  // The pulse width moves every frame, independently of the note, so a held
  // note keeps changing timbre. It is the only movement the design allows
  // itself - the SID filter is chip-dependent and off limits.
  _music_pwm_phase += kMusicPwmStep;
  if (_music_pwm_phase >= kMusicPwmRange) {
    _music_pwm_phase -= kMusicPwmRange;
  }
  {
    const uint16_t tri = (_music_pwm_phase < (kMusicPwmRange / 2))
                             ? _music_pwm_phase
                             : (uint16_t)(kMusicPwmRange - _music_pwm_phase);
    const uint16_t pw = (uint16_t)(kMusicPwmBase + (tri >> 1));
    SID_REGS[kSoundRegV1 + kSoundVoicePwLo] = (uint8_t)pw;
    SID_REGS[kSoundRegV1 + kSoundVoicePwHi] = (uint8_t)((pw >> 8) & 0x0F);
  }

  // --- voice 2: bass ---
  //
  // Same shape as the lead, and deliberately so: one row clock, one hard
  // restart rule, one note-start rule. The differences are the instrument, a
  // fixed pulse width instead of a swept one, and that the bass never rests -
  // MUSIC_BASS_ON(row) is the constant 1, because the 384-byte array it
  // replaced held nothing else.
  //
  // This voice is the one the hard restart was kept for. Its rhythm patterns
  // drop to single rows in the push bars, and a bass pickup whose envelope
  // starts halfway up is a note with no front edge - on the one voice whose
  // front edge *is* the rhythm.
  if (last_frame_of_row && kMusicBassStart[next_row] != 0) {
    _music_hard_restart_restart(kVoice2);
  }
  if (_music_row_frame == 0) {
    const uint8_t note = kMusicBassStart[_music_row];
    if (note != 0) {
      _set_voice(kVoice2, music_note_freq(note), kMusicBassPw,
                 kMusicInsBass.wave | SID_CTRL_GATE, kMusicInsBass.ad,
                 kMusicInsBass.sr);
    }
    // No gate-off branch. There is no row at which the bass is silent, so a
    // held note simply runs until the next one restarts it. If that ever stops
    // being true, MUSIC_BASS_ON(row) stops being 1 and this needs the same
    // else-branch the lead has - the generator asserts the premise and
    // music_test.cc re-checks it.
  }

  // --- voice 3: arpeggio, stolen by drums ---
  //
  // The hard restart has to be remembered, not just performed. It lands on the
  // LAST frame of a row and the hand-back below can fire on any frame, so
  // without the flag a hit arriving next row would be un-gated by the same
  // tick that just prepared it. That was a real bug in the reference player.
  // kMusicV3RestartFrames of hard restart before an incoming hit, not one.
  //
  // This used to fire only on the last frame of the row, which gave a drum the
  // same single frame of gate-low that left the arpeggio unable to climb. The
  // drums had the identical bug and the identical symptom: the percussion was
  // inaudible. A hit is 2 to 5 frames of noise at full sustain, so an envelope
  // that takes longer than that to start never produces anything at all.
  bool v3_restarted = false;
  if (_music_row_frame >= kMusicSpeed - kMusicV3RestartFrames &&
      MUSIC_DRUM_AT(next_row) != 0) {
    _music_hard_restart_restart(kVoice3);
    v3_restarted = true;
  }

  const uint8_t hit = MUSIC_DRUM_AT(_music_row);
  if (_music_row_frame == 0 && hit != 0) {
    const music_instrument_t *d = &kMusicDrumIns[hit - 1];
    _music_v3_owner = hit;
    _music_v3_timer = d->frames;
    _music_v3_freq = d->freq_from;
    _music_v3_step = d->freq_step;
    _set_voice(kVoice3, _music_v3_freq, 0, d->wave | SID_CTRL_GATE, d->ad,
               d->sr);
  } else if (_music_v3_owner == kMusicV3Restart && _music_v3_timer == 0 &&
             !v3_restarted) {
    // The hard restart has run its course. Now the gate may rise, and the
    // envelope starts from a counter that has actually been forced down.
    _music_v3_owner = kMusicV3Arp;
    SID_REGS[kSoundRegV3 + kSoundVoiceAttDec] = kMusicInsArp.ad;
    SID_REGS[kSoundRegV3 + kSoundVoiceSusRel] = kMusicInsArp.sr;
    _write_ctrl(kVoice3, kMusicInsArp.wave | SID_CTRL_GATE);
  }

  if (_music_v3_owner == kMusicV3Arp) {
    // One chord tone per frame with the gate HELD. Rewriting the frequency
    // does not retrigger the envelope, and that is the whole trick: at 50 Hz
    // it reads as a chord shimmering rather than as three notes being played
    // very fast. Re-gating here would turn it into a machine gun.
    _set_freq(kVoice3,
              music_note_freq(kMusicChords[_music_bar].triad[_music_arp_idx]));
    if (++_music_arp_idx == 3) {
      _music_arp_idx = 0;
    }
    // The one exception: a row boundary is allowed to re-gate, because that is
    // where a hard restart from the previous frame has to be undone if no hit
    // actually arrived. Restricting it to _music_row_frame == 0 is what stops
    // it cancelling the restart on the frame the restart happened.
    if (_music_row_frame == 0 && !_gated(kVoice3)) {
      SID_REGS[kSoundRegV3 + kSoundVoiceAttDec] = kMusicInsArp.ad;
      SID_REGS[kSoundRegV3 + kSoundVoiceSusRel] = kMusicInsArp.sr;
      _write_ctrl(kVoice3, kMusicInsArp.wave | SID_CTRL_GATE);
    }
  } else if (_music_v3_owner == kMusicV3Restart) {
    // Holding the gate low with the envelope registers at zero. Nothing to
    // write - _music_hard_restart_restart() already put the chip in this state
    // - just count the frames out.
    //
    // Guarded, because the hand-back above can be blocked by v3_restarted: a
    // restart that expires on the same frame a pre-drum restart fires stays in
    // this state with the counter already at zero, and an unguarded decrement
    // would wrap it to 255 and hold voice 3 silent for five seconds.
    if (_music_v3_timer != 0) {
      --_music_v3_timer;
    }
  } else {
    // A drum holds the voice. Write the current sweep value, then step it: the
    // first frame therefore emits freq_from, which is what makes the kick's
    // descent start where the instrument says.
    _set_freq(kVoice3, _music_v3_freq);
    _music_v3_freq -= _music_v3_step; // zero for the flat drums
    if (--_music_v3_timer == 0) {
      // Not just a gate-off: a full hard restart, which zeroes the envelope
      // registers too. Then kMusicV3RestartFrames frames before the arpeggio is
      // allowed back. See the ADSR delay bug note above.
      _music_hard_restart_restart(kVoice3);
      _music_v3_owner = kMusicV3Restart;
      _music_v3_timer = kMusicV3RestartFrames;
    }
  }

  // --- master volume ---
  SID_REGS[kSoundRegModeVol] =
      music_master_volume(kMusicVolMap[_music_bar], sound_volume);

  // --- advance the clock ---
  if (++_music_row_frame == kMusicSpeed) {
    _music_row_frame = 0;
    if (++_music_row == kMusicTotalRows) {
      _music_row = 0;
      _music_bar = 0;
      // Reset with the row counter so the loop point is identical. 2304 is a
      // multiple of 3, so a free-running index would in fact land back on 0 -
      // but that is a coincidence of the bar count, and the loop-identity test
      // would start failing the next time the arrangement is resized.
      _music_arp_idx = 0;

      // Make the loop point a real start for voice 3, the way music_start() is.
      // The other two voices get this for free from the note-ahead rule; voice
      // 3 does not, because bars 1-4 have no drums to ask for it. See
      // kV3LoopRestartFrames in music.h.
      _music_hard_restart_restart(kVoice3);
      _music_v3_owner = kMusicV3Restart;
      _music_v3_timer = kV3LoopRestartFrames;
      // _music_pwm_phase is deliberately not reset: kMusicPwmStep divides the
      // loop, so it is already back where it started. The test asserts that
      // rather than trusting it.
    } else if ((_music_row % kMusicRowsPerBar) == 0) {
      ++_music_bar;
    }
  }
}

#endif // __ENABLE_SOUND__
