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

# ==========================================================================
#  TUNE 1: 16-Bar Rock Intro (150 BPM)
# ==========================================================================
SPEED = 5                 # frames per row -> 150 BPM at 50 Hz
ROWS_PER_BAR = 16
BARS = 16
TOTAL_ROWS = BARS * ROWS_PER_BAR     # 256
TOTAL_FRAMES = TOTAL_ROWS * SPEED    # 1280

CHORDS = [
    ['Am', 33, [57, 60, 64]], ['F', 29, [53, 57, 60]], ['G', 31, [55, 59, 62]], ['Am', 33, [57, 60, 64]],
    ['Am', 33, [57, 60, 64]], ['F', 29, [53, 57, 60]], ['G', 31, [55, 59, 62]], ['E', 28, [52, 56, 59]],
    ['C', 36, [48, 52, 55]],  ['G', 31, [55, 59, 62]], ['Am', 33, [57, 60, 64]], ['F', 29, [53, 57, 60]],
    ['F', 29, [53, 57, 60]],  ['G', 31, [55, 59, 62]], ['Am', 33, [57, 60, 64]], ['G', 31, [55, 59, 62]]
]

MELODY = [
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
#  TUNE 2: 32-Bar Atmospheric Theme (125 BPM Build & Cadential Turnaround Outro)
# ==========================================================================
SPEED_TUNE2 = 6           # frames per row -> 125 BPM at 50 Hz (~130 BPM)
BARS_TUNE2 = 32
TOTAL_ROWS_TUNE2 = BARS_TUNE2 * ROWS_PER_BAR  # 512
TOTAL_FRAMES_TUNE2 = TOTAL_ROWS_TUNE2 * SPEED_TUNE2  # 3072

CHORDS_TUNE2 = [
    # Bars 1-8: Intro & Motif
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['Am', 33, [45, 48, 52]],
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['A7', 33, [45, 49, 52]],
    # Bars 9-16: Main Theme A
    ['Dm', 38, [50, 53, 57]], ['F',  29, [53, 57, 60]], ['C', 36, [48, 52, 55]], ['Gm', 31, [50, 55, 58]],
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['A7', 33, [45, 49, 52]],
    # Bars 17-24: Climax Theme B
    ['Dm', 38, [50, 53, 57]], ['A#', 34, [46, 50, 53]], ['C', 36, [48, 52, 55]], ['Am', 33, [45, 48, 52]],
    ['Dm', 38, [50, 53, 57]], ['F',  29, [53, 57, 60]], ['Gm', 31, [50, 55, 58]], ['A7', 33, [45, 49, 52]],
    # Bars 25-32: Musical Outro & Grand Turnaround Cadence
    ['Gm', 31, [50, 55, 58]], ['C7', 36, [48, 52, 58]], ['F',  29, [53, 57, 60]], ['A#', 34, [46, 50, 53]],
    ['Gm', 31, [50, 55, 58]], ['A7', 33, [45, 49, 52]], ['A7', 33, [45, 49, 52]], ['Dm', 38, [50, 53, 57]]
]

MELODY_TUNE2 = [
    # Bars 1-4: Soft Intro Build (Melody silent)
    "-:16", "-:16", "-:16", "-:16",
    # Bars 5-8: Motif Introduction
    "D4:8 F4:4 A4:4",
    "G4:8 F4:4 E4:4",
    "F4:8 E4:4 D4:4",
    "E4:8 -:8",
    # Bars 9-16: Main Theme A
    "D4:4 F4:4 A4:4 D5:4",
    "C5:4 A4:4 F4:4 A4:4",
    "G4:4 E4:4 C4:4 E4:4",
    "F4:4 D4:4 A#3:4 D4:4",
    "D4:4 F4:4 A4:4 D5:4",
    "F5:4 D5:4 A4:4 D5:4",
    "E5:4 C5:4 G4:4 C5:4",
    "C#5:8 -:8",
    # Bars 17-24: Climax Theme B (Sixteenth runs & high octaves)
    "F5:2 E5:2 D5:2 C5:2 D5:4 A4:4",
    "D5:2 C5:2 A#4:2 A4:2 A#4:4 F4:4",
    "C5:2 A#4:2 A4:2 G4:2 A4:4 E4:4",
    "E4:2 F4:2 G4:2 A4:2 A#4:2 C5:2 C#5:4",
    "D5:4 F5:4 A5:4 F5:4",
    "C5:4 E5:4 G5:4 E5:4",
    "A#4:4 D5:4 F5:4 D5:4",
    "A4:4 C#5:4 E5:4 -:4",
    # Bars 25-32: Musical Outro & Grand Turnaround Cadence
    "G5:4 F5:4 D5:4 A#4:4",             # Bar 25: Gm descent
    "E5:4 C5:4 A#4:4 G4:4",             # Bar 26: C7 step-down
    "F5:4 C5:4 A4:4 F4:4",              # Bar 27: F major resolution
    "D5:4 A#4:4 F4:4 D4:4",             # Bar 28: A# major falling arpeggio
    "G4:4 A4:4 A#4:4 C5:4",             # Bar 29: Gm scale ascent
    "C#5:4 D5:4 E5:4 F5:4",             # Bar 30: A7 rising tension
    "E5:2 F5:2 G5:4 F5:2 E5:2 C#5:4",   # Bar 31: A7 classical turnaround cadence
    "D5:8 -:8"                          # Bar 32: D minor tonic resolution & rest
]

# Volume Map: Soft intro build (bars 1-8), then full volume (15) through out the entire song and outro!
VOL_MAP_TUNE2 = [
    4, 6, 8, 10,       # Bars 1-4: Soft build-up
    12, 13, 14, 15,    # Bars 5-8: Drums/Motif entering
    15, 15, 15, 15,    # Bars 9-12: Main Theme A
    15, 15, 15, 15,    # Bars 13-16
    15, 15, 15, 15,    # Bars 17-20: Climax
    15, 15, 15, 15,    # Bars 21-24
    15, 15, 15, 15,    # Bars 25-28: Outro Cadence (Full volume!)
    15, 15, 15, 15     # Bars 29-32: Grand Turnaround (Full volume!)
]

# ---------- Tunes Catalogue ----------
TUNES = [
    {
        'id': 'tune1',
        'name': '16-Bar Rock Intro (150 BPM)',
        'speed': SPEED,
        'rows_per_bar': ROWS_PER_BAR,
        'bars': BARS,
        'total_rows': TOTAL_ROWS,
        'total_frames': TOTAL_FRAMES,
        'chords': CHORDS,
        'melody': MELODY,
        'vol_map': VOL_MAP_TUNE1
    },
    {
        'id': 'tune2',
        'name': '32-Bar Atmospheric Theme (125 BPM Turnaround Outro)',
        'speed': SPEED_TUNE2,
        'rows_per_bar': ROWS_PER_BAR,
        'bars': BARS_TUNE2,
        'total_rows': TOTAL_ROWS_TUNE2,
        'total_frames': TOTAL_FRAMES_TUNE2,
        'chords': CHORDS_TUNE2,
        'melody': MELODY_TUNE2,
        'vol_map': VOL_MAP_TUNE2
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

def get_flattened_bass(chords=CHORDS, bars=BARS, total_rows=TOTAL_ROWS):
    bass_start = [0] * total_rows
    bass_on = [False] * total_rows
    for bar in range(bars):
        root = chords[bar][1]
        pattern = BASS_B if is_push(bar) else BASS_A
        # In Tune 2 bars 0-3, use simple pedal bass
        if bars == 32 and bar < 4:
            r = bar * ROWS_PER_BAR
            bass_start[r] = root
            for i in range(16):
                bass_on[r + i] = True
            continue
        for row, off, length in pattern:
            r = bar * ROWS_PER_BAR + row
            bass_start[r] = root + off
            for i in range(length):
                bass_on[r + i] = True
    return bass_start, bass_on

def get_flattened_drums(bars=BARS, total_rows=TOTAL_ROWS):
    drum_at = [None] * total_rows
    for bar in range(bars):
        if bars == 32:
            if bar < 4:
                continue  # No drums during intro build (1-4)
            elif bar < 8:
                # Light hat only
                for s in HAT:
                    drum_at[bar * ROWS_PER_BAR + s] = 'hat'
                continue
            elif bar == 31:  # Bar 32: single crash on row 0, then rest for turnaround
                drum_at[bar * ROWS_PER_BAR + 0] = 'kick'
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
