#include "music.h"

#ifdef __ENABLE_SOUND__

#include "sound.h"

// Phases 2 and 3 of ../docs/music.md: the player skeleton, the ownership
// guard, and voices 1 and 2. Voice 3 - the arpeggio and the drums stealing it -
// is phases 4 and 5.

bool music_playing = false;

// --- The row clock ---------------------------------------------------------
//
// One tick is one video frame. kMusicSpeed frames make a row, kMusicRowsPerBar
// rows make a bar, and the whole tune is kMusicTotalRows rows.
//
// Counted rather than derived: the C64 has no divide, and this is the
// innermost thing the player does. There is deliberately no frame counter -
// nothing reads one, and unused state is what section 4 warns about.
static uint16_t _row;          // 0 .. kMusicTotalRows-1
static uint8_t _row_frame;     // 0 .. kMusicSpeed-1
static uint8_t _bar;           // 0 .. kMusicBars-1

// The pulse width sweep, a triangle over 0x800. The step is 8 because that
// makes the cycle 256 frames, which divides the 2304-frame loop exactly nine
// times - a step that did not divide the loop would leave the pulse width
// somewhere different on every pass. See ../docs/music.md section 6.
static const uint16_t kPwmStep = 8;
static const uint16_t kPwmRange = 0x800;
static const uint16_t kPwmBase = 0x0300;
static uint16_t _pwm_phase;

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
static const uint8_t kVolumeMix[kSoundVolumeSteps][16] = {
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
  return kVolumeMix[setting][bar_volume & 0x0F];
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
  uint8_t octave = (uint8_t)(midi / 12) - 1;
  uint8_t pc = midi % 12;
  uint16_t f = kMusicNoteTable[pc];
  if (octave >= 6) {
    return f;
  }
  return f >> (6 - octave);
}

// --- Voice writes ----------------------------------------------------------
//
// Direct to the chip. There is no shadow and no blit: nothing can interrupt
// this, because the only screens that run it have already masked interrupts.
//
// The control register still goes last. Not for sound.cc's torn-read reason -
// that cannot happen here - but because the SID latches sustain on the gate
// edge, so a gate raised before its envelope registers are in place latches
// whatever was there before.
static void _set_voice(uint8_t base, uint16_t freq, uint16_t pw, uint8_t ctrl,
                       uint8_t attdec, uint8_t susrel) {
  SID_REGS[base + kSoundVoiceFreqLo] = (uint8_t)freq;
  SID_REGS[base + kSoundVoiceFreqHi] = (uint8_t)(freq >> 8);
  SID_REGS[base + kSoundVoicePwLo] = (uint8_t)pw;
  SID_REGS[base + kSoundVoicePwHi] = (uint8_t)((pw >> 8) & 0x0F);
  SID_REGS[base + kSoundVoiceAttDec] = attdec;
  SID_REGS[base + kSoundVoiceSusRel] = susrel;
  SID_REGS[base + kSoundVoiceCtrl] = ctrl;
}

// Hard restart. On the frame before a new note, drop the gate and zero the
// envelope registers so the counter is forced down and the next gate edge
// starts from silence. Without it a new note begins wherever the previous
// envelope happened to be, and the SID does not reliably retrigger on a gate
// that goes low and high within a few cycles.
//
// Applied only where the *next* row actually starts a note, so held notes
// never pay for it. See ../docs/music.md section 3.
static void _hard_restart(uint8_t base) {
  SID_REGS[base + kSoundVoiceCtrl] &= (uint8_t)~SID_CTRL_GATE;
  SID_REGS[base + kSoundVoiceAttDec] = 0;
  SID_REGS[base + kSoundVoiceSusRel] = 0;
}

// --- Public interface ------------------------------------------------------

void music_start(void) {
  _row = 0;
  _row_frame = 0;
  _bar = 0;
  _pwm_phase = 0;

  // The chip already arrives silent - gfx_stop_raster_irqs() calls
  // sound_silence(), which write-throughs zeros to $D400 on its way to the
  // sei - so this is belt and braces. It is kept because the dependency runs
  // through two modules and a screen transition, and the failure it guards
  // against is a voice from the previous flight droning under the menu.
  for (uint8_t v = 0; v < 3; ++v) {
    _set_voice((uint8_t)(v * 7), 0, 0, 0, 0, 0);
  }
  music_playing = true;
}

void music_stop(void) {
  music_playing = false;
  for (uint8_t v = 0; v < 3; ++v) {
    _set_voice((uint8_t)(v * 7), 0, 0, 0, 0, 0);
  }
  SID_REGS[kSoundRegModeVol] = 0;
}

void music_tick(void) {
  // The guard that keeps the in-flight help screen silent. See music.h.
  if (!music_playing) {
    return;
  }

  const bool last_frame_of_row = (_row_frame == kMusicSpeed - 1);
  const uint16_t next_row = (_row + 1 == kMusicTotalRows) ? 0 : (_row + 1);

  // --- voice 1: lead ---
  //
  // Two independent conditions, not an if/else chain. They are mutually
  // exclusive only because kMusicSpeed is 6; writing them as alternatives
  // would quietly stop being correct at speed 1.
  if (last_frame_of_row && kMusicLeadStart[next_row] != 0) {
    _hard_restart(kSoundRegV1);
  }
  if (_row_frame == 0) {
    const uint8_t note = kMusicLeadStart[_row];
    if (note != 0) {
      _set_voice(kSoundRegV1, music_note_freq(note), kPwmBase,
                 kMusicInsLead.wave | SID_CTRL_GATE, kMusicInsLead.ad,
                 kMusicInsLead.sr);
    } else if (!MUSIC_LEAD_ON(_row)) {
      SID_REGS[kSoundRegV1 + kSoundVoiceCtrl] &= (uint8_t)~SID_CTRL_GATE;
    }
  }

  // The pulse width moves every frame, independently of the note, so a held
  // note keeps changing timbre. It is the only movement the design allows
  // itself - the SID filter is chip-dependent and off limits.
  _pwm_phase += kPwmStep;
  if (_pwm_phase >= kPwmRange) {
    _pwm_phase -= kPwmRange;
  }
  {
    const uint16_t tri = (_pwm_phase < (kPwmRange / 2))
                             ? _pwm_phase
                             : (uint16_t)(kPwmRange - _pwm_phase);
    const uint16_t pw = (uint16_t)(kPwmBase + (tri >> 1));
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
    _hard_restart(kSoundRegV2);
  }
  if (_row_frame == 0) {
    const uint8_t note = kMusicBassStart[_row];
    if (note != 0) {
      _set_voice(kSoundRegV2, music_note_freq(note), kMusicBassPw,
                 kMusicInsBass.wave | SID_CTRL_GATE, kMusicInsBass.ad,
                 kMusicInsBass.sr);
    }
    // No gate-off branch. There is no row at which the bass is silent, so a
    // held note simply runs until the next one restarts it. If that ever stops
    // being true, MUSIC_BASS_ON(row) stops being 1 and this needs the same
    // else-branch the lead has - the generator asserts the premise and
    // music_test.cc re-checks it.
  }

  // Voice 3 is phases 4 and 5.

  // --- master volume ---
  SID_REGS[kSoundRegModeVol] =
      music_master_volume(kMusicVolMap[_bar], sound_volume);

  // --- advance the clock ---
  if (++_row_frame == kMusicSpeed) {
    _row_frame = 0;
    if (++_row == kMusicTotalRows) {
      _row = 0;
      _bar = 0;
      // _pwm_phase is deliberately not reset: kPwmStep divides the loop, so it
      // is already back where it started. The test asserts that rather than
      // trusting it.
    } else if ((_row % kMusicRowsPerBar) == 0) {
      ++_bar;
    }
  }
}

#endif // __ENABLE_SOUND__
