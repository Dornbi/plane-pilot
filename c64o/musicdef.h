#ifndef MUSICDEF_H
#define MUSICDEF_H

#include <stdint.h>

// The tune ships in ppilot.prg only. ppilotd.prg is the debug
// build and stays silent, so none of this - including the
// ~1.1 KB of tables in musicdef.cc - should reach it.
// See ../docs/music.md section 4.
#ifdef __ENABLE_SOUND__

static const uint8_t kMusicSpeed = 6;
static const uint8_t kMusicRowsPerBar = 16;
static const uint8_t kMusicBars = 24;
static const uint16_t kMusicTotalRows = 384;
static const uint16_t kMusicTotalFrames = 2304;

struct music_chord_t {
    uint8_t root;
    uint8_t triad[3];
};

struct music_instrument_t {
    uint8_t ad;
    uint8_t sr;
    uint8_t wave;
    uint8_t frames;
    uint16_t freq_from;
    // Per-frame subtraction, not a target frequency. The kick sweeps
    // down over its frames; the reference computes
    //   from + (to - from) * t / frames
    // which is one divide per frame, and a 6510 has none. The
    // division happens in the exporter instead and the player
    // subtracts. Worst divergence from the reference sequence is 3
    // parts in 2700, on a noise voice.
    uint16_t freq_step;
};

extern const uint16_t kMusicNoteTable[12];
extern const music_chord_t kMusicChords[24];

// Master volume per bar, low nibble of $D418. Composed with
// sound_volume through the 3 x 16 table in music.cc, never
// written straight to the chip. See docs/music.md section 3.
extern const uint8_t kMusicVolMap[24];

// One byte per row: the MIDI note a lead or bass note starts on,
// or 0 for no new note.
extern const uint8_t kMusicLeadStart[384];
extern const uint8_t kMusicBassStart[384];

// Packed tables - docs/music.md section 4, option B. These were
// one byte per row and are now one and two bits; together with
// dropping kMusicBassOn that is 672 bytes for ~40 bytes of code.
//
// There is deliberately no kMusicBassOn. The bass never rests, so
// the array it used to occupy held nothing but the value 1.
extern const uint8_t kMusicLeadOnBits[48];
extern const uint8_t kMusicDrumBits[96];

#define MUSIC_LEAD_ON(row)  \
    ((kMusicLeadOnBits[(row) >> 3] >> ((row) & 7)) & 1)
// 0 = none, 1 = kick, 2 = snare, 3 = hat.
#define MUSIC_DRUM_AT(row)  \
    ((kMusicDrumBits[(row) >> 2] >> (((row) & 3) << 1)) & 3)
#define MUSIC_BASS_ON(row)  (1)

// The bass voice's pulse width. A per-instrument constant that does
// not fit music_instrument_t, which has no pw field because only one
// voice needs a fixed one - the lead sweeps and the rest are noise.
static const uint16_t kMusicBassPw = 0x0500;

extern const music_instrument_t kMusicInsLead;
extern const music_instrument_t kMusicInsBass;
extern const music_instrument_t kMusicInsArp;

// Kick, snare, hat - an array rather than three names, because the
// player reaches them through MUSIC_DRUM_AT(row), which is already
// 1, 2 or 3. Indexing beats a switch: sound.md section 10 measured
// the equivalent comparison chain at 101 bytes.
extern const music_instrument_t kMusicDrumIns[3];

#endif // __ENABLE_SOUND__

#pragma compile("musicdef.cc")

#endif // MUSICDEF_H
