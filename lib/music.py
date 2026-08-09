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
#  PRIMARY TUNE: 16-Bar Atmospheric Theme (125 BPM, lift turnaround)
# ==========================================================================
# A bar is 96 frames at speed 6 and the player's pulse-width sweep cycles every
# 256, so bars * 96 must be a multiple of 256: only 8, 16, 24 and 32 keep the
# loop bit-clean.
#
# This started at 32 bars and came down twice. The eight-bar cadential outro
# went first - it walked out to F major and closed with a V7-i, which is the one
# thing a loop must not do - and the climax section went with the second cut,
# for RAM. What survives is the shape that made the arrangement worth having:
# four bars of pedal build, four of motif entry, six of theme, and a two-bar
# turnaround that is also the tune's melodic peak. Gm -> A7 is iv - V7 into the
# Dm of bar 1, so the loop point needed no bars of its own.
#
# See docs/music.md sections 3 and 4.
SPEED = 6           # frames per row -> 125 BPM at 50 Hz
ROWS_PER_BAR = 16
BARS = 16
TOTAL_ROWS = BARS * ROWS_PER_BAR        # 256
TOTAL_FRAMES = TOTAL_ROWS * SPEED       # 1536 = 30.72 s PAL

CHORDS = [
    # Bars 1-8: Intro & Motif
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['Am', 33, [45, 48, 52]],
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['A7', 33, [45, 49, 52]],
    # Bars 9-14: Main Theme
    ['Dm', 38, [50, 53, 57]], ['F',  29, [53, 57, 60]], ['C', 36, [48, 52, 55]], ['Gm', 31, [50, 55, 58]],
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]],
    # Bars 15-16: lift turnaround. iv - V7 into bar 1's Dm.
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
    # Bars 9-14: Main Theme. Rises across the six bars so the turnaround that
    # follows is the peak rather than an afterthought.
    "D4:4 F4:4 A4:4 D5:4",
    "C5:4 A4:4 F4:4 A4:4",
    "G4:4 E4:4 C4:4 E4:4",
    "F4:4 D4:4 A#3:4 D4:4",
    "D4:4 F4:4 A4:4 D5:4",
    "F5:4 D5:4 A4:4 D5:4",
    # Bars 15-16: the lift turnaround, and the highest notes in the tune.
    #
    # The old outro fell for four bars and then closed with a Baroque cadential
    # figure over A7 - E F G F E C# - resolving C# -> D. That is a full stop,
    # and a loop point is the one place a full stop is wrong. These two bars
    # climb instead, and deliberately never play C#: with the leading tone
    # withheld the A7 does not announce itself as a dominant, so returning to
    # bar 1's Dm reads as the phrase carrying on rather than starting again.
    # F5 over A7 is the flat 13th, which is also the third of D minor - it
    # keeps the home key in earshot through the turn.
    "D5:4 F5:4 G5:4 A#5:4",             # Bar 15 (Gm): opens upward
    "A5:4 G5:4 F5:4 E5:4"               # Bar 16 (A7): peak, then down onto the 5th
]

VOL_MAP = [
    # Bars 1-8: the opening fade. The floor is 8 rather than 4 so that the glide
    # at the end of bar 16 lands next to it - see the tail of this table. On a
    # first play a deeper fade would be nicer; on every loop after that it is a
    # hole. A deeper first-play fade costs one entry if it is ever wanted back.
    8, 9, 10, 11,      # Bars 1-4: pedal bass and arpeggio only
    12, 13, 14, 15,    # Bars 5-8: motif and hats entering, full by bar 8
    15, 15, 15, 15,    # Bars 9-12: main theme
    15, 15,            # Bars 13-14
    12, 10             # Bars 15-16: glide into the loop seam (10 -> bar 1's 8)
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

# ---------- Tunes Catalogue ----------
TUNES = [
    {
        'id': 'tune2',
        'name': '16-Bar Atmospheric Theme (125 BPM, lift turnaround)',
        'speed': SPEED,
        'rows_per_bar': ROWS_PER_BAR,
        'bars': BARS,
        'total_rows': TOTAL_ROWS,
        'total_frames': TOTAL_FRAMES,
        'chords': CHORDS,
        'melody': MELODY,
        'vol_map': VOL_MAP,
        'soft_intro': True
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

def get_flattened_bass(chords=CHORDS, bars=BARS, total_rows=TOTAL_ROWS,
                       soft_intro=True):
    bass_start = [0] * total_rows
    bass_on = [False] * total_rows
    for bar in range(bars):
        root = chords[bar][1]
        pattern = BASS_B if is_push(bar) else BASS_A
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

def get_flattened_drums(bars=BARS, total_rows=TOTAL_ROWS, soft_intro=True):
    drum_at = [None] * total_rows
    for bar in range(bars):
        if soft_intro:
            if bar < SOFT_INTRO_BARS:
                continue                      # no drums at all under the build
            elif bar < SOFT_HAT_BARS:
                for s in HAT:                 # hats only under the motif
                    drum_at[bar * ROWS_PER_BAR + s] = 'hat'
                continue

        kick_p = KICK_B if is_push(bar) else KICK_A
        snr_p = SNR_B if is_fill(bar) else SNR_A
        for s in HAT:
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
