# Song Data and Arrangement for Title Music
# Generated/referenced by tools/generate_music.py and docs/music.md

import re

# ---------- SID constants & note tables ----------
PAL_CLK = 985248
SID_K = 16777216 / PAL_CLK  # ~17.02841

# Octave-6 note table (12 entries), per docs/music.md section 5
NOTE6 = [17820, 18880, 20003, 21192, 22452, 23787, 25202, 26700, 28288, 29970, 31752, 33640]

NI = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}

def name_to_midi(n):
    m = re.match(r'^([A-G]#?)(-?\d)$', n)
    if not m:
        raise ValueError(f"Invalid note format: {n}")
    note_name, oct_str = m.group(1), m.group(2)
    return NI[note_name] + (int(oct_str, 10) + 1) * 12

def midi_to_reg(m):
    pc = ((m % 12) + 12) % 12
    octave = (m // 12) - 1
    sh = 6 - octave
    if sh >= 0:
        return NOTE6[pc] >> sh
    else:
        return NOTE6[pc] << (-sh)

# ---------- Shared Rhythm Constants & Instruments ----------
CTRL_GATE = 0x01
CTRL_TRI = 0x10
CTRL_SAW = 0x20
CTRL_PULSE = 0x40
CTRL_NOISE = 0x80

INS = {
    'lead':  {'ad': 0x05, 'sr': 0xC5, 'wave': CTRL_PULSE},
    'bass':  {'ad': 0x06, 'sr': 0x84, 'wave': CTRL_PULSE},
    'arp':   {'ad': 0x00, 'sr': 0xF3, 'wave': CTRL_SAW},
    'kick':  {'ad': 0x00, 'sr': 0xF0, 'wave': CTRL_NOISE, 'frames': 5, 'from': 0x1000, 'to': 0x0300},
    'snare': {'ad': 0x00, 'sr': 0xF0, 'wave': CTRL_NOISE, 'frames': 4, 'from': 0x3800, 'to': 0x3800},
    'hat':   {'ad': 0x00, 'sr': 0xF0, 'wave': CTRL_NOISE, 'frames': 2, 'from': 0x5000, 'to': 0x5000}
}

# The arpeggio's register.
#
# One arpeggio tone lasts exactly one frame - 20 ms - and a note needs roughly
# four waveform cycles before the ear reads it as a pitch rather than a click.
# The triads as voiced below sit at 110-220 Hz, which is 2.2 to 4.4 cycles per
# tone: audible, but as a buzz rather than as a chord. That is what "thin"
# sounds like. One octave up is 4.4 to 8.8 cycles, which is where SID arpeggios
# conventionally sit.
#
# Applied to the triads only - the bass root in each chord entry is untouched.
# One number, so it is trivial to revert or to try +24. See docs/music.md
# section 6.
ARP_OCTAVE_SHIFT = 12

def shift_triads(chords, semitones):
    return [[name, root, [t + semitones for t in triad]]
            for name, root, triad in chords]

BASS_PW = 0x0500

BASS_A = [[0, 0, 2], [2, 0, 2], [4, 0, 2], [6, 12, 2], [8, 0, 2], [10, 0, 2], [12, 7, 2], [14, 12, 2]]
BASS_B = [[0, 0, 2], [2, 0, 2], [4, 0, 1], [5, 0, 1], [6, 12, 2], [8, 0, 2], [10, 7, 2], [12, 12, 2], [14, 10, 1], [15, 12, 1]]

KICK_A = [0, 6, 8]
KICK_B = [0, 6, 8, 10, 14]
SNR_A = [4, 12]
SNR_B = [4, 12, 13, 14, 15]
HAT = [0, 2, 4, 6, 8, 10, 12, 14]

def is_fill(b):
    return b == 7 or b == 15 or b == 23 or b == 30 or b == 31

def is_push(b):
    return b == 3 or b == 11 or b == 19 or b == 27 or is_fill(b)

# Whether a tune opens with the four-bar pedal-bass build and four bars of hats
# only. This is a property of the arrangement, so it travels in the tune dict
# rather than being inferred from the bar count - both arrangements are 16 bars
# now, and the atmospheric one needs the intro while the rock one must not have
# it.
SOFT_INTRO_BARS = 4      # pedal bass, arpeggio, no lead, no drums
SOFT_HAT_BARS = 8        # ...then hats only until this bar

# ==========================================================================
#  PRIMARY TUNE: 24-Bar Atmospheric Theme (125 BPM, lift turnaround)
# ==========================================================================
# A bar is 96 frames at speed 6 and the player's pulse-width sweep cycles every
# 256, so bars * 96 must be a multiple of 256: only 8, 16, 24 and 32 keep the
# loop bit-clean.
#
# This started at 32 bars, lost its eight-bar cadential outro for musical
# reasons - it walked out to F major and closed with a V7-i, which is the one
# thing a loop must not do - and then briefly dropped to 16 for RAM, which cost
# it the climax. Packing the flat tables (docs/music.md section 4, option B)
# bought that back and more: 24 bars packed is smaller than 16 bars was
# unpacked. The layout is 4 bars of pedal build, 4 of motif entry, 8 of theme,
# 6 of climax and a 2-bar turnaround that is also the melodic peak. Gm -> A7 is
# iv - V7 into the Dm of bar 1, so the loop point needs no bars of its own.
#
# See docs/music.md sections 3, 4 and 5.
SPEED = 6           # frames per row -> 125 BPM at 50 Hz
ROWS_PER_BAR = 16
BARS = 24
TOTAL_ROWS = BARS * ROWS_PER_BAR        # 384
TOTAL_FRAMES = TOTAL_ROWS * SPEED       # 2304 = 46.08 s PAL

CHORDS = [
    # Bars 1-8: Intro & Motif
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['Am', 33, [45, 48, 52]],
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['A7', 33, [45, 49, 52]],
    # Bars 9-16: Main Theme
    ['Dm', 38, [50, 53, 57]], ['F',  29, [53, 57, 60]], ['C', 36, [48, 52, 55]], ['Gm', 31, [50, 55, 58]],
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['A7', 33, [45, 49, 52]],
    # Bars 17-22: Climax. Six bars rather than the original eight - the last two
    # were Gm and A7, which the turnaround below now says better.
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['Am', 33, [45, 48, 52]],
    ['Dm', 38, [50, 53, 57]], ['F',  29, [53, 57, 60]],
    # Bars 23-24: lift turnaround. iv - V7 into bar 1's Dm.
    ['Gm', 31, [50, 55, 58]], ['A7', 33, [45, 49, 52]]
]

MELODY = [
    # Bars 1-4: Soft Intro Build (Melody silent)
    "-:16", "-:16", "-:16", "-:16",
    # Bars 5-8: Motif Introduction
    "D4:8 F4:4 A4:4",
    "G4:8 F4:4 E4:4",
    "F4:8 E4:4 D4:4",
    "E4:8 -:8",
    # Bars 9-16: Main Theme, in quarters throughout.
    "D4:4 F4:4 A4:4 D5:4",
    "C5:4 A4:4 F4:4 A4:4",
    "G4:4 E4:4 C4:4 E4:4",
    "F4:4 D4:4 A#3:4 D4:4",
    "D4:4 F4:4 A4:4 D5:4",
    "F5:4 D5:4 A4:4 D5:4",
    "E5:4 C5:4 G4:4 C5:4",
    "C#5:8 -:8",
    # Bars 17-22: Climax. The rhythmic contrast the theme does not have - four
    # bars of sixteenth-note runs, then two of high-octave arpeggiated figures
    # that hand the tune to the turnaround already near the top of its range.
    "F5:2 E5:2 D5:2 C5:2 D5:4 A4:4",
    "D5:2 C5:2 A#4:2 A4:2 A#4:4 F4:4",
    "C5:2 A#4:2 A4:2 G4:2 A4:4 E4:4",
    "E4:2 F4:2 G4:2 A4:2 A#4:2 C5:2 C#5:4",
    "D5:4 F5:4 A5:4 F5:4",
    "C5:4 E5:4 G5:4 E5:4",
    # Bars 23-24: the lift turnaround, and the highest notes in the tune.
    #
    # The old outro fell for four bars and then closed with a Baroque cadential
    # figure over A7 - E F G F E C# - resolving C# -> D. That is a full stop,
    # and a loop point is the one place a full stop is wrong. These two bars
    # climb instead, and deliberately never play C#: with the leading tone
    # withheld the A7 does not announce itself as a dominant, so returning to
    # bar 1's Dm reads as the phrase carrying on rather than starting again.
    # F5 over A7 is the flat 13th, which is also the third of D minor - it
    # keeps the home key in earshot through the turn.
    "D5:4 F5:4 G5:4 A#5:4",             # Bar 23 (Gm): opens upward
    "A5:4 G5:4 F5:4 E5:4"               # Bar 24 (A7): peak, then down onto the 5th
]

VOL_MAP = [
    # Bars 1-8: the opening fade. The floor is 8 rather than 4 so that the glide
    # at the end of bar 16 lands next to it - see the tail of this table. On a
    # first play a deeper fade would be nicer; on every loop after that it is a
    # hole. A deeper first-play fade costs one entry if it is ever wanted back.
    8, 9, 10, 11,      # Bars 1-4: pedal bass and arpeggio only
    12, 13, 14, 15,    # Bars 5-8: motif and hats entering, full by bar 8
    15, 15, 15, 15,    # Bars 9-12: main theme
    15, 15, 15, 15,    # Bars 13-16
    15, 15, 15, 15,    # Bars 17-20: climax
    15, 15,            # Bars 21-22
    12, 10             # Bars 23-24: glide into the loop seam (10 -> bar 1's 8)
]

# ==========================================================================
#  TUNE 2: 16-Bar Rock Intro (150 BPM)
# ==========================================================================
SPEED_TUNE1 = 5
BARS_TUNE1 = 16
TOTAL_ROWS_TUNE1 = 256
TOTAL_FRAMES_TUNE1 = 1280

CHORDS_TUNE1 = [
    ['Am', 33, [57, 60, 64]], ['F', 29, [53, 57, 60]], ['G', 31, [55, 59, 62]], ['Am', 33, [57, 60, 64]],
    ['Am', 33, [57, 60, 64]], ['F', 29, [53, 57, 60]], ['G', 31, [55, 59, 62]], ['E', 28, [52, 56, 59]],
    ['C', 36, [48, 52, 55]],  ['G', 31, [55, 59, 62]], ['Am', 33, [57, 60, 64]], ['F', 29, [53, 57, 60]],
    ['F', 29, [53, 57, 60]],  ['G', 31, [55, 59, 62]], ['Am', 33, [57, 60, 64]], ['G', 31, [55, 59, 62]]
]

MELODY_TUNE1 = [
    "E4:2 A4:2 C5:2 B4:2 A4:4 -:2 G4:2",
    "A4:2 F4:2 A4:2 C5:2 D5:4 C5:2 A4:2",
    "B4:2 G4:2 B4:2 D5:2 G5:4 D5:2 B4:2",
    "C5:2 B4:2 A4:2 G4:2 A4:6 -:2",
    "E5:2 A4:2 C5:2 B4:2 A4:4 E5:2 C5:2",
    "A4:2 F4:2 A4:2 C5:2 F5:4 E5:2 C5:2",
    "D5:2 B4:2 D5:2 G5:2 F5:2 E5:2 D5:2 B4:2",
    "E5:2 B4:2 G#4:2 B4:2 E5:4 D5:2 C5:2",
    "G5:4 E5:4 C5:4 E5:2 G5:2",
    "D5:4 B4:4 G4:4 B4:2 D5:2",
    "E5:4 C5:4 A4:4 C5:2 E5:2",
    "F5:4 C5:4 A4:4 C5:2 F5:2",
    "A5:2 G5:2 F5:2 E5:2 F5:4 C5:4",
    "B5:2 A5:2 G5:2 F5:2 G5:4 D5:4",
    "A5:2 E5:2 C5:2 A4:2 C5:2 E5:2 A5:4",
    "G5:2 F5:2 E5:2 D5:2 C5:2 B4:2 -:2 E4:2"
]

VOL_MAP_TUNE1 = [15] * 16

# ==========================================================================
#  TUNE 3: "Afterburner" - 24 bars, E minor, driving rock
# ==========================================================================
# Written from scratch rather than as a variation. The brief was Last Ninja
# catchiness in something a flight sim could open with, which pulled two ways:
# Last Ninja hooks are modal, syncopated and rhythmically stubborn, while a
# flight sim wants lift - fourths and fifths, ascending lines, open intervals.
#
# The answer is E minor with an i - VI - III - VII loop (Em C G D). That is the
# most forward-leaning progression in minor rock: every chord is major except
# the tonic, so the harmony keeps opening outward while the key stays dark.
#
# The hook is one rhythmic cell repeated over all four chords - a long note, a
# pickup, two quarters (6:2:4:4). A cell that survives transposition is what
# makes a tune hummable after one listen; Ben Daglish built most of Last Ninja
# on exactly that.
#
# Triads are voiced in octave 4 directly, at 260-590 Hz, so they need no
# ARP_OCTAVE_SHIFT: one arpeggio tone is 5 to 12 waveform cycles, well clear of
# the threshold that made the D minor tune sound thin.
CHORDS_ROCK = [
    # bars 1-4   intro: bass and arpeggio only
    ['Em', 40, [64, 67, 71]], ['C', 36, [60, 64, 67]], ['G', 43, [67, 71, 74]], ['D', 38, [62, 66, 69]],
    # bars 5-8   motif enters, hats
    ['Em', 40, [64, 67, 71]], ['C', 36, [60, 64, 67]], ['G', 43, [67, 71, 74]], ['D', 38, [62, 66, 69]],
    # bars 9-16  main theme, full kit
    ['Em', 40, [64, 67, 71]], ['C', 36, [60, 64, 67]], ['G', 43, [67, 71, 74]], ['D', 38, [62, 66, 69]],
    ['Em', 40, [64, 67, 71]], ['C', 36, [60, 64, 67]], ['G', 43, [67, 71, 74]], ['B', 35, [71, 75, 78]],
    # bars 17-20 climax: starts on VI so the section lifts off its own downbeat
    ['C', 36, [60, 64, 67]], ['G', 43, [67, 71, 74]], ['Em', 40, [64, 67, 71]], ['D', 38, [62, 66, 69]],
    # bars 21-24 outro, closing on V for the turn back to Em
    ['C', 36, [60, 64, 67]], ['D', 38, [62, 66, 69]], ['Em', 40, [64, 67, 71]], ['B', 35, [71, 75, 78]],
]

MELODY_ROCK = [
    "-:16", "-:16", "-:16", "-:16",
    # 5-8: the motif, low and plain - long tone then a rising third and fifth
    "E4:8 G4:4 B4:4",
    "C5:8 B4:4 G4:4",
    "D5:8 B4:4 G4:4",
    "A4:8 F#4:4 D4:4",
    # 9-16: the hook. 6:2:4:4 on every chord - long, pickup, two quarters.
    "E5:6 D5:2 B4:4 G4:4",
    "G4:6 A4:2 C5:4 B4:4",
    "D5:6 B4:2 G4:4 A4:4",
    "F#4:6 A4:2 D5:4 F#4:4",
    "B4:6 A4:2 G4:4 E4:4",
    "C5:6 B4:2 A4:4 G4:4",
    "B4:6 D5:2 G5:4 D5:4",
    "F#5:6 E5:2 D#5:4 B4:4",   # D# is B major's third - the one leading tone
    # 17-20: climax. Sixteenth pairs, top of the range, same cell underneath.
    "E5:2 G5:2 E5:2 C5:2 G4:4 C5:4",
    "D5:2 G5:2 D5:2 B4:2 G4:4 D5:4",
    "E5:2 B4:2 G4:2 B4:2 E5:4 G5:4",
    "F#5:2 D5:2 A4:2 D5:2 F#5:4 A5:4",
    # 21-24: outro. Quarters, thinning out, then a scalar descent onto F# -
    # the fifth of B - which pulls back to E without ever stating it.
    "G5:4 E5:4 C5:4 E5:4",
    "F#5:4 D5:4 A4:4 D5:4",
    "E5:8 D5:4 B4:4",
    "B4:2 A4:2 G4:2 F#4:2 E4:4 F#4:4",
]

VOL_MAP_ROCK = [
    8, 10, 11, 12,      # 1-4   intro build
    13, 14, 15, 15,     # 5-8   motif and hats
    15, 15, 15, 15,     # 9-12  theme
    15, 15, 15, 15,     # 13-16
    15, 15, 15, 15,     # 17-20 climax
    14, 13, 12, 10,     # 21-24 outro, landing next to bar 1's 8
]

# Straight eighths alternating root and octave - the engine of the tune, and
# the thing that most separates it from the atmospheric arrangement's pedal.
RHYTHM_ROCK = {
    'bass': [[0, 0, 2], [2, 12, 2], [4, 0, 2], [6, 12, 2],
             [8, 0, 2], [10, 12, 2], [12, 0, 2], [14, 12, 2]],
    'bass_push': [[0, 0, 2], [2, 12, 2], [4, 0, 1], [5, 7, 1], [6, 12, 2],
                  [8, 0, 2], [10, 7, 2], [12, 12, 2], [14, 10, 1], [15, 12, 1]],
    'kick': [0, 6, 8, 14],          # busier than the default: pushes the beat
    'kick_push': [0, 3, 6, 8, 10, 14],
    'snare': [4, 12],
    'snare_fill': [4, 12, 13, 14, 15],
    'hat': [0, 2, 4, 6, 8, 10, 12, 14],
}

# ==========================================================================
#  TUNE 4: "High Country" - 24 bars, A Dorian, anthemic rock
# ==========================================================================
# The other half of the brief. Where Afterburner is busy and syncopated, this
# is wide: half-time drums, a bass with air in it, and a melody built out of
# rising fourths and fifths.
#
# A Dorian rather than natural minor - the raised sixth (F#) is the difference
# between "dark" and "high up". The Am - G - D turn is the mode's signature
# and does not exist in Aeolian at all, which is what keeps this from sounding
# like the same key as the other two tunes.
#
# The hook is an interval rather than a rhythm: root, leap up a fifth, hold,
# fall back. It is the most flight-shaped gesture available - the same one
# behind most fanfares - and it survives being played over four different
# chords, which is what makes it a hook rather than a phrase.
CHORDS_WIDE = [
    # bars 1-4   intro
    ['Am', 33, [69, 72, 76]], ['G', 31, [67, 71, 74]], ['D', 38, [62, 66, 69]], ['Am', 33, [69, 72, 76]],
    # bars 5-8   motif
    ['Am', 33, [69, 72, 76]], ['G', 31, [67, 71, 74]], ['D', 38, [62, 66, 69]], ['Am', 33, [69, 72, 76]],
    # bars 9-16  theme
    ['Am', 33, [69, 72, 76]], ['G', 31, [67, 71, 74]], ['C', 36, [60, 64, 67]], ['D', 38, [62, 66, 69]],
    ['Am', 33, [69, 72, 76]], ['G', 31, [67, 71, 74]], ['D', 38, [62, 66, 69]], ['Am', 33, [69, 72, 76]],
    # bars 17-20 climax
    ['C', 36, [60, 64, 67]], ['G', 31, [67, 71, 74]], ['Am', 33, [69, 72, 76]], ['D', 38, [62, 66, 69]],
    # bars 21-24 outro, closing on E major - borrowed V, the one chord that
    # pulls hard enough to restart a Dorian loop
    ['G', 31, [67, 71, 74]], ['D', 38, [62, 66, 69]], ['Am', 33, [69, 72, 76]], ['E', 40, [64, 68, 71]],
]

MELODY_WIDE = [
    "-:16", "-:16", "-:16", "-:16",
    # 5-8: motif, one long tone and two answering notes
    "A4:8 C5:4 E5:4",
    "B4:8 D5:4 G4:4",
    "F#4:8 A4:4 D5:4",
    "E5:8 C5:4 A4:4",
    # 9-16: the hook - step, leap up, hold, fall back. Same shape, four chords.
    "A4:4 D5:4 E5:6 C5:2",
    "G4:4 D5:4 B4:6 D5:2",
    "C5:4 G5:4 E5:6 C5:2",
    "D5:4 A5:4 F#5:6 D5:2",
    "E5:4 A5:4 G5:6 E5:2",
    "D5:4 G5:4 F#5:6 D5:2",
    "F#5:4 D5:4 A4:6 D5:2",
    "C5:4 E5:4 A4:8",
    # 17-20: climax. The hook broken into sixteenths and pushed to the top.
    "G5:2 A5:2 G5:2 E5:2 C5:4 G5:4",
    "F#5:2 G5:2 F#5:2 D5:2 B4:4 F#5:4",
    "E5:2 G5:2 A5:2 G5:2 E5:4 A5:4",
    "F#5:2 A5:2 F#5:2 D5:2 A4:4 D5:4",
    # 21-24: outro. Quarters, then the highest note in the tune, then an
    # E major arpeggio walking down into the loop.
    "G5:4 D5:4 B4:4 D5:4",
    "F#5:4 D5:4 A4:4 D5:4",
    "A5:8 E5:4 C5:4",
    "B4:4 G#4:4 E4:4 -:4",
]

VOL_MAP_WIDE = [
    8, 9, 11, 12,
    13, 14, 15, 15,
    15, 15, 15, 15,
    15, 15, 15, 15,
    15, 15, 15, 15,
    14, 13, 11, 9,
]

# Half-time kit and a bass with space in it. Quarter-note hats rather than
# eighths: the tune's motion comes from the melody's leaps, and an eighth-note
# hat under it just fills the gaps the leaps depend on.
RHYTHM_WIDE = {
    'bass': [[0, 0, 4], [4, 0, 2], [6, 7, 2], [8, 0, 4], [12, 7, 2], [14, 12, 2]],
    'bass_push': [[0, 0, 4], [4, 0, 2], [6, 7, 2], [8, 0, 2], [10, 12, 2],
                  [12, 7, 2], [14, 12, 2]],
    'kick': [0, 8],
    'kick_push': [0, 8, 14],
    'snare': [4, 12],
    'snare_fill': [4, 12, 14],
    'hat': [0, 4, 8, 12],
}

# ---------- Tunes Catalogue ----------
TUNES = [
    {
        'id': 'tune2',
        'name': '24-Bar Atmospheric Theme (125 BPM, lift turnaround)',
        'speed': SPEED,
        'rows_per_bar': ROWS_PER_BAR,
        'bars': BARS,
        'total_rows': TOTAL_ROWS,
        'total_frames': TOTAL_FRAMES,
        'chords': shift_triads(CHORDS, ARP_OCTAVE_SHIFT),
        'melody': MELODY,
        'vol_map': VOL_MAP,
        'soft_intro': True
    },
    {
        'id': 'rock',
        'name': '24-Bar "Afterburner" - E minor driving rock (125 BPM)',
        'speed': SPEED, 'rows_per_bar': ROWS_PER_BAR, 'bars': BARS,
        'total_rows': TOTAL_ROWS, 'total_frames': TOTAL_FRAMES,
        # No shift_triads(): these are already voiced in octave 4.
        'chords': CHORDS_ROCK,
        'melody': MELODY_ROCK,
        'vol_map': VOL_MAP_ROCK,
        'rhythm': RHYTHM_ROCK,
        'soft_intro': True,
    },
    {
        'id': 'wide',
        'name': '24-Bar "High Country" - A Dorian anthem (125 BPM)',
        'speed': SPEED, 'rows_per_bar': ROWS_PER_BAR, 'bars': BARS,
        'total_rows': TOTAL_ROWS, 'total_frames': TOTAL_FRAMES,
        'chords': CHORDS_WIDE,
        'melody': MELODY_WIDE,
        'vol_map': VOL_MAP_WIDE,
        'rhythm': RHYTHM_WIDE,
        'soft_intro': True,
    },
    {
        'id': 'tune1',
        'name': '16-Bar Rock Intro (150 BPM)',
        'speed': SPEED_TUNE1,
        'rows_per_bar': ROWS_PER_BAR,
        'bars': BARS_TUNE1,
        'total_rows': TOTAL_ROWS_TUNE1,
        'total_frames': TOTAL_FRAMES_TUNE1,
        'chords': CHORDS_TUNE1,
        'melody': MELODY_TUNE1,
        'vol_map': VOL_MAP_TUNE1,
        'soft_intro': False
    }
]

# ---------- Flattened Table Builders ----------
def get_flattened_lead(melody=MELODY, total_rows=TOTAL_ROWS):
    lead_start = [0] * total_rows
    lead_on = [False] * total_rows
    for bar, s in enumerate(melody):
        r = bar * ROWS_PER_BAR
        tokens = s.strip().split()
        for tok in tokens:
            note_name, length_str = tok.split(':')
            duration = int(length_str, 10)
            if note_name != '-':
                midi = name_to_midi(note_name)
                lead_start[r] = midi
                for i in range(duration):
                    lead_on[r + i] = True
            r += duration
    return lead_start, lead_on

# Default rhythm, used by any tune that does not carry its own. A tune's
# groove is part of what makes it that tune, so a new arrangement that inherits
# these ends up sounding like a variation of the old one however different its
# notes are.
DEFAULT_RHYTHM = {
    'bass': BASS_A, 'bass_push': BASS_B,
    'kick': KICK_A, 'kick_push': KICK_B,
    'snare': SNR_A, 'snare_fill': SNR_B,
    'hat': HAT,
}

def rhythm_of(tune):
    r = dict(DEFAULT_RHYTHM)
    r.update(tune.get('rhythm', {}))
    return r


def get_flattened_bass(chords=CHORDS, bars=BARS, total_rows=TOTAL_ROWS,
                       soft_intro=True, rhythm=None):
    bass_start = [0] * total_rows
    bass_on = [False] * total_rows
    for bar in range(bars):
        root = chords[bar][1]
        r = rhythm or DEFAULT_RHYTHM
        pattern = r['bass_push'] if is_push(bar) else r['bass']
        # One pedal note per bar through the opening build.
        if soft_intro and bar < SOFT_INTRO_BARS:
            r = bar * ROWS_PER_BAR
            bass_start[r] = root
            for i in range(ROWS_PER_BAR):
                bass_on[r + i] = True
            continue
        for row, off, length in pattern:
            r = bar * ROWS_PER_BAR + row
            bass_start[r] = root + off
            for i in range(length):
                bass_on[r + i] = True
    return bass_start, bass_on

def get_flattened_drums(bars=BARS, total_rows=TOTAL_ROWS, soft_intro=True,
                        rhythm=None):
    r = rhythm or DEFAULT_RHYTHM
    drum_at = [None] * total_rows
    for bar in range(bars):
        if soft_intro:
            if bar < SOFT_INTRO_BARS:
                continue                      # no drums at all under the build
            elif bar < SOFT_HAT_BARS:
                for s in r['hat']:            # hats only under the motif
                    drum_at[bar * ROWS_PER_BAR + s] = 'hat'
                continue

        kick_p = r['kick_push'] if is_push(bar) else r['kick']
        snr_p = r['snare_fill'] if is_fill(bar) else r['snare']
        for s in r['hat']:
            drum_at[bar * ROWS_PER_BAR + s] = 'hat'
        for s in snr_p:
            drum_at[bar * ROWS_PER_BAR + s] = 'snare'
        for s in kick_p:
            drum_at[bar * ROWS_PER_BAR + s] = 'kick'
    return drum_at

def validate_song_data(melody=MELODY, chords=CHORDS, bars=BARS):
    # 1. Bar row sum check
    for bar_idx, bar_str in enumerate(melody):
        total_rows_bar = 0
        tokens = bar_str.strip().split()
        for tok in tokens:
            note_name, length_str = tok.split(':')
            duration = int(length_str, 10)
            total_rows_bar += duration
            if note_name != '-':
                m = name_to_midi(note_name)
                if not (28 <= m <= 83):
                    raise ValueError(f"Bar {bar_idx+1} note {note_name} (MIDI {m}) out of range [28..83]")
        if total_rows_bar != ROWS_PER_BAR:
            raise ValueError(f"Bar {bar_idx+1} melody sums to {total_rows_bar} rows, expected {ROWS_PER_BAR}")

    # 2. Chord table validation
    if len(chords) != bars:
        raise ValueError(f"Expected {bars} chords, got {len(chords)}")
    for i, chord in enumerate(chords):
        name, root, triad = chord
        if not (28 <= root <= 83):
            raise ValueError(f"Chord {i} root {root} out of range [28..83]")
        if len(triad) != 3:
            raise ValueError(f"Chord {i} triad must have 3 elements, got {triad}")
        for tm in triad:
            if not (28 <= tm <= 83):
                raise ValueError(f"Chord {i} triad note {tm} out of range [28..83]")

    return True
