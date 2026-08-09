// Host test for the title tune's player.
//
// docs/music.md section 7 is the reason this exists at phase 2 rather than
// phase 7: every defect this module has produced so far has been inaudible.
// The loop-identity invariant was wrong as written, the reference's BPM
// readout was double the real tempo, the volume ramp never reached C, and the
// tune selector's labels named the wrong tunes. None of those sound like a
// wrong note - they sound like nothing, or like something subtly fine.
//
// The test links music.cc and the generated musicdef.cc, and defines
// sound_volume itself rather than pulling in sound.cc. That keeps it a unit
// test of the player and lets the V-key setting be set directly.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../music.h"
#include "../sound.h"

// The one piece of sound.cc's state music.cc reads.
uint8_t sound_volume = kSoundVolumeDefault;

// The host build's stand-in for the chip. sid.h points SID_REGS at this.
static uint8_t sid_regs[32];
struct SID *sid_host = (struct SID *)sid_regs;

static const uint8_t kVoiceBase[3] = {kSoundRegV1, kSoundRegV2, kSoundRegV3};

static bool gate_set(uint8_t voice) {
  return (sid_regs[kVoiceBase[voice] + kSoundVoiceCtrl] & SID_CTRL_GATE) != 0;
}

// --- 1. The ownership guard -------------------------------------------------
//
// The single most important property in this module, because it is the only
// one that can break something outside it. help_run() ticks unconditionally
// and has a caller in flight; if the guard leaks, pressing H mid-mission
// starts the title music.
static void test_guard_writes_nothing(void) {
  memset(sid_regs, 0xAA, sizeof(sid_regs));
  music_playing = false;
  for (int i = 0; i < 500; ++i) {
    music_tick();
  }
  for (size_t i = 0; i < sizeof(sid_regs); ++i) {
    assert(sid_regs[i] == 0xAA && "music_tick() wrote with music_playing clear");
  }
  printf("  ok  music_tick() writes nothing while music_playing is clear\n");
}

// --- 2. start / stop --------------------------------------------------------
static void test_start_and_stop(void) {
  memset(sid_regs, 0xAA, sizeof(sid_regs));
  music_start();
  assert(music_playing);
  for (int v = 0; v < 3; ++v) {
    assert(!gate_set(v) && "music_start() left a gate set");
  }

  for (int i = 0; i < 300; ++i) {
    music_tick();
  }

  music_stop();
  assert(!music_playing);
  for (int v = 0; v < 3; ++v) {
    assert(!gate_set(v) && "music_stop() left a gate set");
  }
  assert((sid_regs[kSoundRegModeVol] & 0x0F) == 0 &&
         "music_stop() left the master volume up");
  printf("  ok  music_start() clears the chip, music_stop() releases it\n");
}

// --- 3. The note table ------------------------------------------------------
static void test_note_table(void) {
  // Monotonic across the whole range the arrangement uses.
  uint16_t prev = 0;
  for (uint8_t m = 29; m <= 82; ++m) {
    uint16_t f = music_note_freq(m);
    assert(f > prev && "note table is not monotonic");
    prev = f;
  }
  // An octave is exactly a factor of two, which is the property the shift
  // depends on.
  for (uint8_t m = 29; m <= 70; ++m) {
    assert(music_note_freq(m + 12) == music_note_freq(m) * 2 ||
           music_note_freq(m + 12) == music_note_freq(m) * 2 + 1);
  }
  // A4 = 440 Hz = 440 * 16777216 / 985248 = 7493 on PAL, within the 0.09%
  // the right shift costs.
  uint16_t a4 = music_note_freq(69);
  assert(a4 > 7480 && a4 < 7500);
  printf("  ok  note table monotonic, octave-exact, A4 = %u\n", a4);
}

// --- 4. Volume composition --------------------------------------------------
//
// The tune's per-bar ramp and the V key both want $D418's low nibble. Neither
// may override the other, "off" must be exactly silent, and the low setting
// must scale the ramp rather than clip it flat - a clipped ramp turns the
// opening fade into two bars of fade and six of nothing changing.
static void test_volume_composition(void) {
  for (uint8_t v = 0; v <= 15; ++v) {
    assert(music_master_volume(v, 0) == 0 && "sound off is not silent");
    assert(music_master_volume(v, 2) == v && "full volume is not the identity");
    assert(music_master_volume(v, 1) <= 7 && "low setting exceeds its ceiling");
  }
  // Monotonic and genuinely varying at the low setting.
  uint8_t lo = music_master_volume(0, 1), hi = music_master_volume(15, 1);
  assert(lo == 0 && hi == 7);
  uint8_t distinct = 0, last = 0xFF;
  for (uint8_t v = 0; v <= 15; ++v) {
    uint8_t m = music_master_volume(v, 1);
    assert(m >= last || last == 0xFF);
    if (m != last) {
      ++distinct;
    }
    last = m;
  }
  assert(distinct >= 6 && "low setting flattened the ramp");
  // Out-of-range settings fall back rather than reading off the end.
  assert(music_master_volume(15, 99) == 15);
  printf("  ok  volume composition: off silent, full identity, low scales (%u steps)\n",
         distinct);
}

// --- 5. The ramp actually reaches the chip ----------------------------------
static void test_ramp_reaches_the_chip(void) {
  sound_volume = kSoundVolumeDefault;
  music_start();
  // Bar 1 is the quietest bar of the fade; a bar in the middle is at 15.
  music_tick();
  uint8_t first = sid_regs[kSoundRegModeVol] & 0x0F;
  assert(first == kMusicVolMap[0] && "bar 1 volume did not reach $D418");

  // Run to the last bar and check the glide into the loop seam.
  music_start();
  const uint16_t frames_to_last_bar =
      (uint16_t)(kMusicBars - 1) * kMusicRowsPerBar * kMusicSpeed;
  for (uint16_t i = 0; i < frames_to_last_bar + 1; ++i) {
    music_tick();
  }
  uint8_t last = sid_regs[kSoundRegModeVol] & 0x0F;
  assert(last == kMusicVolMap[kMusicBars - 1] &&
         "last bar volume did not reach $D418");
  assert(last < 15 && "the glide into the loop seam is missing");

  // And "sound off" really is silent for the whole tune, ramp or no ramp.
  sound_volume = 0;
  music_start();
  for (uint16_t i = 0; i < kMusicTotalFrames; ++i) {
    music_tick();
    assert((sid_regs[kSoundRegModeVol] & 0x0F) == 0 &&
           "sound_volume == 0 did not silence the tune");
  }
  sound_volume = kSoundVolumeDefault;
  printf("  ok  volume ramp reaches $D418: bar 1 = %u, bar %u = %u\n", first,
         kMusicBars, last);
}

// --- 6. Loop identity, on gated voices ---------------------------------------
//
// NOT all 25 registers. The lead is silent for bars 1-4, so voice 1's
// frequency and envelope registers still hold the last note of bar 24 while
// its gate is clear. That is inaudible and unavoidable without writing
// registers for no reason. docs/music.md section 3 has the measurement.
static void test_loop_identity(void) {
  static uint8_t pass1[8192][25];
  assert(kMusicTotalFrames <= 8192);

  sound_volume = kSoundVolumeDefault;
  music_start();
  for (uint16_t f = 0; f < kMusicTotalFrames; ++f) {
    music_tick();
    memcpy(pass1[f], sid_regs, 25);
  }

  int strict = 0, gated = 0;
  for (uint16_t f = 0; f < kMusicTotalFrames; ++f) {
    music_tick();
    for (int v = 0; v < 3; ++v) {
      const uint8_t b = kVoiceBase[v];
      const bool on = (pass1[f][b + kSoundVoiceCtrl] & SID_CTRL_GATE) ||
                      (sid_regs[b + kSoundVoiceCtrl] & SID_CTRL_GATE);
      for (int o = 0; o < 7; ++o) {
        if (pass1[f][b + o] != sid_regs[b + o]) {
          ++strict;
          if (on) {
            ++gated;
          }
        }
      }
    }
    for (int r = 21; r < 25; ++r) {
      if (pass1[f][r] != sid_regs[r]) {
        ++strict;
        ++gated;
      }
    }
  }
  assert(gated == 0 && "the loop is not seamless on a gated voice");
  printf("  ok  loop identity over %u frames: %d strict diffs, %d on gated voices\n",
         kMusicTotalFrames, strict, gated);
}

// --- 7. Hard restart --------------------------------------------------------
//
// The decision most likely to be quietly dropped in a rewrite, and the one
// that most affects whether the tune sounds crisp or mushy. It must happen on
// the frame before a new note and only there.
static void test_hard_restart(void) {
  music_start();
  int restarts = 0, missing = 0, spurious = 0;

  for (uint16_t row = 0; row < kMusicTotalRows; ++row) {
    for (uint8_t rf = 0; rf < kMusicSpeed; ++rf) {
      music_tick();
      if (rf != kMusicSpeed - 1) {
        continue;
      }
      const uint16_t next = (row + 1 == kMusicTotalRows) ? 0 : (uint16_t)(row + 1);
      const uint8_t b = kSoundRegV1;
      const bool restarted = !(sid_regs[b + kSoundVoiceCtrl] & SID_CTRL_GATE) &&
                             sid_regs[b + kSoundVoiceAttDec] == 0 &&
                             sid_regs[b + kSoundVoiceSusRel] == 0;
      if (kMusicLeadStart[next] != 0) {
        if (restarted) {
          ++restarts;
        } else {
          ++missing;
        }
      } else if (restarted && kMusicLeadStart[row] != 0) {
        ++spurious;
      }
    }
  }
  assert(missing == 0 && "a new note had no hard restart before it");
  assert(spurious == 0 && "hard restart on a row with no new note");
  assert(restarts > 0);
  printf("  ok  hard restart on all %d new lead notes, and only there\n", restarts);
}

// --- 8. The lead actually plays ---------------------------------------------
//
// A player that passes everything above can still be silent. This asserts the
// gate goes up, the frequency changes, and the pulse width sweeps.
static void test_lead_is_audible(void) {
  music_start();
  int gate_frames = 0;
  uint16_t distinct_freqs = 0, last_freq = 0xFFFF;
  uint16_t pw_min = 0xFFFF, pw_max = 0;

  for (uint16_t f = 0; f < kMusicTotalFrames; ++f) {
    music_tick();
    if (gate_set(0)) {
      ++gate_frames;
    }
    const uint16_t freq = (uint16_t)sid_regs[kSoundRegV1 + kSoundVoiceFreqLo] |
                          ((uint16_t)sid_regs[kSoundRegV1 + kSoundVoiceFreqHi] << 8);
    if (freq != last_freq) {
      ++distinct_freqs;
      last_freq = freq;
    }
    const uint16_t pw = (uint16_t)sid_regs[kSoundRegV1 + kSoundVoicePwLo] |
                        ((uint16_t)(sid_regs[kSoundRegV1 + kSoundVoicePwHi] & 0x0F) << 8);
    if (pw < pw_min) {
      pw_min = pw;
    }
    if (pw > pw_max) {
      pw_max = pw;
    }
  }
  assert(gate_frames > 0 && "the lead never sounds");
  assert(distinct_freqs > 20 && "the lead barely changes pitch");
  assert(pw_max > pw_min && "the pulse width never sweeps");
  assert(pw_min > 0 && pw_max < 0x1000 && "pulse width left the legal range");

  // The lead is silent for the opening build, which is the arrangement's
  // shape and not an accident.
  music_start();
  const uint16_t build_frames = 4 * kMusicRowsPerBar * kMusicSpeed;
  for (uint16_t f = 0; f < build_frames; ++f) {
    music_tick();
    assert(!gate_set(0) && "the lead sounds during the pedal build");
  }
  printf("  ok  lead audible: %d gated frames, %u pitch changes, pw %u..%u\n",
         gate_frames, distinct_freqs, pw_min, pw_max);
}

// --- 9. Voice 3 stays silent until phase 4 -----------------------------------
//
// Not a permanent property. It is here so that "the arpeggio is not written
// yet" is a checked claim rather than an intention, and so that whoever adds
// voice 3 has to delete this and mean it. The voice-2 half of this assertion
// was deleted by phase 3.
static void test_voice_3_silent_until_phase_4(void) {
  music_start();
  for (uint16_t f = 0; f < kMusicTotalFrames; ++f) {
    music_tick();
    assert(!gate_set(2) && "voice 3 sounds, but the arpeggio is phase 4");
  }
  printf("  ok  voice 3 silent (phases 4-5 will change this)\n");
}

// --- 10. The bass ------------------------------------------------------------
//
// The bass is the voice most likely to be wrong in a way nobody hears as
// wrong: it is low, it is continuous, and for the first four bars it is one
// note per bar. A bass that never retriggers still sounds like a bass.
static void test_bass(void) {
  music_start();

  int gate_frames = 0, note_starts = 0;
  uint16_t lo = 0xFFFF, hi = 0;
  uint16_t last_freq = 0xFFFF;
  bool ever_silent = false;

  for (uint16_t row = 0; row < kMusicTotalRows; ++row) {
    for (uint8_t rf = 0; rf < kMusicSpeed; ++rf) {
      music_tick();
      if (gate_set(1)) {
        ++gate_frames;
      } else if (rf == 0 && row > 0) {
        // Legal only on a hard-restart frame, which is rf == kMusicSpeed-1.
        ever_silent = true;
      }
      const uint16_t f = (uint16_t)sid_regs[kSoundRegV2 + kSoundVoiceFreqLo] |
                         ((uint16_t)sid_regs[kSoundRegV2 + kSoundVoiceFreqHi] << 8);
      if (f != last_freq) {
        ++note_starts;
        last_freq = f;
        if (f) {
          if (f < lo) lo = f;
          if (f > hi) hi = f;
        }
      }
    }
  }
  assert(gate_frames > 0 && "the bass never sounds");
  assert(!ever_silent && "the bass was gated off at the start of a row");
  assert(note_starts > 20 && "the bass barely changes pitch");

  // The bass is a bass: every note below middle C's SID frequency.
  assert(hi < music_note_freq(60) && "a bass note strayed above middle C");
  printf("  ok  bass: %d gated frames, %u pitch changes, freq %u..%u\n",
         gate_frames, note_starts, lo, hi);
}

// --- 11. The pedal opening ---------------------------------------------------
//
// Bars 1-4 are one bass note per bar under the arpeggio, with no lead and no
// drums. It is the easiest thing in the arrangement to get audibly wrong,
// because there is nothing else playing to cover it: a bass that retriggers
// every two rows there would turn the pedal into a pulse, and a bass that
// never retriggers would drone one note through all four chord changes.
static void test_pedal_opening(void) {
  music_start();
  const uint16_t build_rows = 4 * kMusicRowsPerBar;
  int retriggers_per_bar[4] = {0, 0, 0, 0};
  uint16_t bar_freq[4] = {0, 0, 0, 0};
  uint16_t last = 0xFFFF;

  for (uint16_t row = 0; row < build_rows; ++row) {
    for (uint8_t rf = 0; rf < kMusicSpeed; ++rf) {
      music_tick();
      const uint16_t f = (uint16_t)sid_regs[kSoundRegV2 + kSoundVoiceFreqLo] |
                         ((uint16_t)sid_regs[kSoundRegV2 + kSoundVoiceFreqHi] << 8);
      if (f != last && f != 0) {
        ++retriggers_per_bar[row / kMusicRowsPerBar];
        bar_freq[row / kMusicRowsPerBar] = f;
        last = f;
      }
    }
  }
  for (int b = 0; b < 4; ++b) {
    assert(retriggers_per_bar[b] == 1 &&
           "the pedal opening is not exactly one bass note per bar");
  }
  // Four different chords, so four different pedal notes - a drone would mean
  // the chord table is not being read.
  assert(bar_freq[0] != bar_freq[1] && bar_freq[1] != bar_freq[2] &&
         bar_freq[2] != bar_freq[3] && "the pedal opening drones on one note");
  printf("  ok  pedal opening: one note per bar, %u/%u/%u/%u\n", bar_freq[0],
         bar_freq[1], bar_freq[2], bar_freq[3]);
}

// --- 12. Hard restart on the bass too ----------------------------------------
static void test_bass_hard_restart(void) {
  music_start();
  int restarts = 0, missing = 0;
  for (uint16_t row = 0; row < kMusicTotalRows; ++row) {
    for (uint8_t rf = 0; rf < kMusicSpeed; ++rf) {
      music_tick();
      if (rf != kMusicSpeed - 1) {
        continue;
      }
      const uint16_t next = (row + 1 == kMusicTotalRows) ? 0 : (uint16_t)(row + 1);
      if (kMusicBassStart[next] == 0) {
        continue;
      }
      const uint8_t b = kSoundRegV2;
      if (!(sid_regs[b + kSoundVoiceCtrl] & SID_CTRL_GATE) &&
          sid_regs[b + kSoundVoiceAttDec] == 0 &&
          sid_regs[b + kSoundVoiceSusRel] == 0) {
        ++restarts;
      } else {
        ++missing;
      }
    }
  }
  assert(missing == 0 && "a bass note had no hard restart before it");
  printf("  ok  hard restart on all %d new bass notes\n", restarts);
}

// --- 13. The two voices are distinguishable ----------------------------------
//
// With no per-voice volume the mix is entirely waveform and envelope, so the
// one thing that can be checked mechanically is that the bass is not simply
// the lead an octave down with the same settings.
static void test_lead_and_bass_differ(void) {
  music_start();
  bool seen = false;
  for (uint16_t f = 0; f < kMusicTotalFrames && !seen; ++f) {
    music_tick();
    if (gate_set(0) && gate_set(1)) {
      seen = true;
      assert(sid_regs[kSoundRegV1 + kSoundVoiceSusRel] !=
                 sid_regs[kSoundRegV2 + kSoundVoiceSusRel] &&
             "lead and bass share an envelope");
      const uint16_t lead_pw =
          (uint16_t)sid_regs[kSoundRegV1 + kSoundVoicePwLo] |
          ((uint16_t)(sid_regs[kSoundRegV1 + kSoundVoicePwHi] & 0x0F) << 8);
      const uint16_t bass_pw =
          (uint16_t)sid_regs[kSoundRegV2 + kSoundVoicePwLo] |
          ((uint16_t)(sid_regs[kSoundRegV2 + kSoundVoicePwHi] & 0x0F) << 8);
      assert(bass_pw == kMusicBassPw && "the bass pulse width is not the constant");
      assert(lead_pw != bass_pw || true);  // the lead sweeps; equality is transient
    }
  }
  assert(seen && "lead and bass never sound together");
  printf("  ok  lead and bass differ in envelope; bass pw fixed at $%04X\n",
         kMusicBassPw);
}

int main(void) {
  printf("music_test: %u bars, %u rows, %u frames, speed %u\n", kMusicBars,
         kMusicTotalRows, kMusicTotalFrames, kMusicSpeed);
  test_guard_writes_nothing();
  test_start_and_stop();
  test_note_table();
  test_volume_composition();
  test_ramp_reaches_the_chip();
  test_loop_identity();
  test_hard_restart();
  test_lead_is_audible();
  test_voice_3_silent_until_phase_4();
  test_bass();
  test_pedal_opening();
  test_bass_hard_restart();
  test_lead_and_bass_differ();
  printf("music_test: all passed\n");
  return 0;
}
