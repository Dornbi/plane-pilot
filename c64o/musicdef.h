#ifndef MUSICDEF_H
#define MUSICDEF_H

#include <stdint.h>

// The tune ships in ppilot.prg only. ppilotd.prg is the debug
// build and stays silent, so none of the tables in musicdef.cc
// should reach it. See ../docs/music.md section 4.
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

// Master volume per bar, low nibble of $D418. Composed with
// sound_volume through the 3 x 16 table in music.cc, never
// written straight to the chip. See docs/music.md section 3.
extern const uint8_t kMusicVolMap[24];

// Packed tables - docs/music.md section 4.
//
// Option B put the gate and drum lanes into one and two bits a row.
// This is the layer above it: the tune is 24 bars and most of them
// are repeats, so each lane is stored as its distinct bar patterns
// plus one index byte a bar. Every read costs the extra index
// lookup, which is why it is only done here - the player runs on the
// menu and help screens, where there is no frame to miss.
//
// There is deliberately no kMusicBassOn. The bass never rests, so
// the array it used to occupy held nothing but the value 1.
//
// Reach these through the macros below, never directly: the split
// into pattern and index is an encoding, not something the player
// should know about.
extern const music_chord_t kMusicChordPat[];
extern const uint8_t kMusicChordBar[24];
extern const uint8_t kMusicLeadStartPat[][16];
extern const uint8_t kMusicLeadStartBar[24];
extern const uint8_t kMusicBassStartPat[][16];
extern const uint8_t kMusicBassStartBar[24];
extern const uint8_t kMusicLeadOnBitsPat[][2];
extern const uint8_t kMusicLeadOnBitsBar[24];
extern const uint8_t kMusicDrumBitsPat[][4];
extern const uint8_t kMusicDrumBitsBar[24];

// The bar a row falls in, and the row within it.
#define MUSIC_BAR_OF(row)   ((row) >> 4)
#define MUSIC_IN_BAR(row)   ((row) & 15)

// The MIDI note a lead or bass note starts on, or 0 for no new note.
#define MUSIC_LEAD_START(row)  \
    (kMusicLeadStartPat[kMusicLeadStartBar[MUSIC_BAR_OF(row)]][MUSIC_IN_BAR(row)])
#define MUSIC_BASS_START(row)  \
    (kMusicBassStartPat[kMusicBassStartBar[MUSIC_BAR_OF(row)]][MUSIC_IN_BAR(row)])
#define MUSIC_CHORD(bar)    (kMusicChordPat[kMusicChordBar[bar]])
#define MUSIC_LEAD_ON(row)  \
    ((kMusicLeadOnBitsPat[kMusicLeadOnBitsBar[MUSIC_BAR_OF(row)]][MUSIC_IN_BAR(row) >> 3] \
      >> ((row) & 7)) & 1)
// 0 = none, 1 = kick, 2 = snare, 3 = hat.
#define MUSIC_DRUM_AT(row)  \
    ((kMusicDrumBitsPat[kMusicDrumBitsBar[MUSIC_BAR_OF(row)]][MUSIC_IN_BAR(row) >> 2] \
      >> (((row) & 3) << 1)) & 3)
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
