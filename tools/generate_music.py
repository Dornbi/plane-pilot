import os
import sys

# Ensure repository root is on sys.path
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from lib import music

# ---------------------------------------------------------------------------
# Packing, per docs/music.md section 4 option B.
#
# The browser reference keeps these unpacked, because JS has no reason to care
# and the player there reads them directly. The C64 pays for every byte, so the
# same data is emitted packed and tests/test_music.py checks that unpacking the
# C reproduces the arrays the HTML got. Same data, two encodings, verified
# equivalent - which is the only arrangement under which two copies are safe.
# ---------------------------------------------------------------------------
DRUM_CODE = {None: 0, 'kick': 1, 'snare': 2, 'hat': 3}

def pack_bits1(values):
    """One bit per row, LSB first: row r is bit r&7 of byte r>>3."""
    out = bytearray((len(values) + 7) // 8)
    for r, v in enumerate(values):
        if v:
            out[r >> 3] |= 1 << (r & 7)
    return list(out)

def pack_bits2(values):
    """Two bits per row, low pair first: row r is bits (r&3)*2 of byte r>>2."""
    out = bytearray((len(values) + 3) // 4)
    for r, v in enumerate(values):
        out[r >> 2] |= (v & 3) << ((r & 3) * 2)
    return list(out)

def dedup_bars(values, per_bar):
    """Splits a per-row table into distinct bar patterns plus a bar index.

    A 24 bar tune that repeats is stored 24 times over otherwise. Returns
    (patterns, index) where patterns is a list of per_bar-long lists in first
    appearance order and index[b] says which one bar b plays, so
    values[b * per_bar + k] == patterns[index[b]][k] for every b and k.

    Worth it only where bars actually repeat: the index costs one byte a bar,
    so a table whose bars are all distinct comes out larger. generate() prints
    the before and after for each so that stays visible.
    """
    bars = len(values) // per_bar
    assert bars * per_bar == len(values), "table is not a whole number of bars"
    patterns = []
    seen = {}
    index = []
    for b in range(bars):
        pat = tuple(values[b * per_bar:(b + 1) * per_bar])
        if pat not in seen:
            seen[pat] = len(patterns)
            patterns.append(list(pat))
        index.append(seen[pat])
    assert len(patterns) <= 256, "bar index no longer fits in a byte"
    return patterns, index


def _emit_bar_table(f, name, patterns, index, per_bar, per_line, what):
    """Emits the pattern array and its bar index, with the saving in a
    comment so the next person can see whether it is still earning its keep."""
    was = len(index) * per_bar
    now = len(patterns) * per_bar + len(index)
    f.write(f"// {what}: {len(patterns)} distinct bars of {len(index)}, "
            f"{now} bytes instead of {was}.\n")
    f.write(f"const uint8_t {name}Pat[{len(patterns)}][{per_bar}] = {{\n")
    for i, pat in enumerate(patterns):
        body = ", ".join(f"{v:3d}" for v in pat)
        f.write(f"    {{ {body} }}, // pattern {i}\n")
    f.write("};\n\n")
    f.write(f"const uint8_t {name}Bar[{len(index)}] = {{\n")
    _emit_rows(f, index, per_line)
    f.write("};\n\n")


def _emit_rows(f, values, per_line, comment_every=None):
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        line = "    " + ", ".join(f"{v:3d}" for v in chunk) + ","
        if comment_every:
            line += f" // {comment_every} {i // per_line}"
        f.write(line + "\n")

def generate_c64_headers():
    h_path = os.path.join(REPO_ROOT, "c64o", "musicdef.h")
    os.makedirs(os.path.dirname(h_path), exist_ok=True)
    t1 = music.TUNES[0]
    with open(h_path, "w") as f:
        f.write("#ifndef MUSICDEF_H\n")
        f.write("#define MUSICDEF_H\n\n")
        # No <stdbool.h>. This header declares no bool, and pulling it in was
        # actively harmful: bool.h defines `bool` as a *macro* on the host, and
        # clang's <stdbool.h> #undefs that macro in C++ mode while gcc's leaves
        # it alone. Including it here changed the meaning of `bool` partway
        # through sound.cc - which includes music.h - so sound_wind_audible()
        # was declared returning `unsigned char` and defined returning `bool`.
        # That built on Linux/gcc and failed on macOS/clang.
        f.write("#include <stdint.h>\n\n")
        f.write("// The tune ships in ppilot.prg only. ppilotd.prg is the debug\n")
        f.write("// build and stays silent, so none of the tables in musicdef.cc\n")
        f.write("// should reach it. See ../docs/music.md section 4.\n")
        f.write("#ifdef __ENABLE_SOUND__\n\n")
        f.write(f"static const uint8_t kMusicSpeed = {t1['speed']};\n")
        f.write(f"static const uint8_t kMusicRowsPerBar = {t1['rows_per_bar']};\n")
        f.write(f"static const uint8_t kMusicBars = {t1['bars']};\n")
        f.write(f"static const uint16_t kMusicTotalRows = {t1['total_rows']};\n")
        f.write(f"static const uint16_t kMusicTotalFrames = {t1['total_frames']};\n\n")
        f.write("struct music_chord_t {\n")
        f.write("    uint8_t root;\n")
        f.write("    uint8_t triad[3];\n")
        f.write("};\n\n")
        f.write("struct music_instrument_t {\n")
        f.write("    uint8_t ad;\n")
        f.write("    uint8_t sr;\n")
        f.write("    uint8_t wave;\n")
        f.write("    uint8_t frames;\n")
        f.write("    uint16_t freq_from;\n")
        f.write("    // Per-frame subtraction, not a target frequency. The kick sweeps\n")
        f.write("    // down over its frames; the reference computes\n")
        f.write("    //   from + (to - from) * t / frames\n")
        f.write("    // which is one divide per frame, and a 6510 has none. The\n")
        f.write("    // division happens in the exporter instead and the player\n")
        f.write("    // subtracts. Worst divergence from the reference sequence is 3\n")
        f.write("    // parts in 2700, on a noise voice.\n")
        f.write("    uint16_t freq_step;\n")
        f.write("};\n\n")
        rows = t1['total_rows']
        f.write("extern const uint16_t kMusicNoteTable[12];\n\n")

        f.write("// Master volume per bar, low nibble of $D418. Composed with\n")
        f.write("// sound_volume through the 3 x 16 table in music.cc, never\n")
        f.write("// written straight to the chip. See docs/music.md section 3.\n")
        f.write(f"extern const uint8_t kMusicVolMap[{t1['bars']}];\n\n")

        rpb = t1['rows_per_bar']
        bar_shift = rpb.bit_length() - 1
        assert 1 << bar_shift == rpb, "rows per bar must be a power of two"
        row_mask = rpb - 1

        f.write("// Packed tables - docs/music.md section 4.\n")
        f.write("//\n")
        f.write("// Option B put the gate and drum lanes into one and two bits a row.\n")
        f.write("// This is the layer above it: the tune is 24 bars and most of them\n")
        f.write("// are repeats, so each lane is stored as its distinct bar patterns\n")
        f.write("// plus one index byte a bar. Every read costs the extra index\n")
        f.write("// lookup, which is why it is only done here - the player runs on the\n")
        f.write("// menu and help screens, where there is no frame to miss.\n")
        f.write("//\n")
        f.write("// There is deliberately no kMusicBassOn. The bass never rests, so\n")
        f.write("// the array it used to occupy held nothing but the value 1.\n")
        f.write("//\n")
        f.write("// Reach these through the macros below, never directly: the split\n")
        f.write("// into pattern and index is an encoding, not something the player\n")
        f.write("// should know about.\n")
        f.write(f"extern const music_chord_t kMusicChordPat[];\n")
        f.write(f"extern const uint8_t kMusicChordBar[{t1['bars']}];\n")
        f.write(f"extern const uint8_t kMusicLeadStartPat[][{rpb}];\n")
        f.write(f"extern const uint8_t kMusicLeadStartBar[{t1['bars']}];\n")
        f.write(f"extern const uint8_t kMusicBassStartPat[][{rpb}];\n")
        f.write(f"extern const uint8_t kMusicBassStartBar[{t1['bars']}];\n")
        f.write(f"extern const uint8_t kMusicLeadOnBitsPat[][{rpb // 8}];\n")
        f.write(f"extern const uint8_t kMusicLeadOnBitsBar[{t1['bars']}];\n")
        f.write(f"extern const uint8_t kMusicDrumBitsPat[][{rpb // 4}];\n")
        f.write(f"extern const uint8_t kMusicDrumBitsBar[{t1['bars']}];\n\n")

        f.write("// The bar a row falls in, and the row within it.\n")
        f.write(f"#define MUSIC_BAR_OF(row)   ((row) >> {bar_shift})\n")
        f.write(f"#define MUSIC_IN_BAR(row)   ((row) & {row_mask})\n\n")

        f.write("// The MIDI note a lead or bass note starts on, or 0 for no new note.\n")
        f.write("#define MUSIC_LEAD_START(row)  \\\n")
        f.write("    (kMusicLeadStartPat[kMusicLeadStartBar[MUSIC_BAR_OF(row)]]"
                "[MUSIC_IN_BAR(row)])\n")
        f.write("#define MUSIC_BASS_START(row)  \\\n")
        f.write("    (kMusicBassStartPat[kMusicBassStartBar[MUSIC_BAR_OF(row)]]"
                "[MUSIC_IN_BAR(row)])\n")
        f.write("#define MUSIC_CHORD(bar)    (kMusicChordPat[kMusicChordBar[bar]])\n")
        f.write("#define MUSIC_LEAD_ON(row)  \\\n")
        f.write("    ((kMusicLeadOnBitsPat[kMusicLeadOnBitsBar[MUSIC_BAR_OF(row)]]"
                "[MUSIC_IN_BAR(row) >> 3] \\\n")
        f.write("      >> ((row) & 7)) & 1)\n")
        f.write("// 0 = none, 1 = kick, 2 = snare, 3 = hat.\n")
        f.write("#define MUSIC_DRUM_AT(row)  \\\n")
        f.write("    ((kMusicDrumBitsPat[kMusicDrumBitsBar[MUSIC_BAR_OF(row)]]"
                "[MUSIC_IN_BAR(row) >> 2] \\\n")
        f.write("      >> (((row) & 3) << 1)) & 3)\n")
        f.write("#define MUSIC_BASS_ON(row)  (1)\n\n")

        f.write("// The bass voice's pulse width. A per-instrument constant that does\n")
        f.write("// not fit music_instrument_t, which has no pw field because only one\n")
        f.write("// voice needs a fixed one - the lead sweeps and the rest are noise.\n")
        f.write(f"static const uint16_t kMusicBassPw = 0x{music.BASS_PW:04X};\n\n")
        f.write("extern const music_instrument_t kMusicInsLead;\n")
        f.write("extern const music_instrument_t kMusicInsBass;\n")
        f.write("extern const music_instrument_t kMusicInsArp;\n")
        f.write("\n")
        f.write("// Kick, snare, hat - an array rather than three names, because the\n")
        f.write("// player reaches them through MUSIC_DRUM_AT(row), which is already\n")
        f.write("// 1, 2 or 3. Indexing beats a switch: sound.md section 10 measured\n")
        f.write("// the equivalent comparison chain at 101 bytes.\n")
        f.write("extern const music_instrument_t kMusicDrumIns[3];\n\n")
        f.write("#endif // __ENABLE_SOUND__\n\n")
        f.write("#pragma compile(\"musicdef.cc\")\n\n")
        f.write("#endif // MUSICDEF_H\n")
    print(f"Generated {h_path}")

def generate_c64_source():
    cc_path = os.path.join(REPO_ROOT, "c64o", "musicdef.cc")
    os.makedirs(os.path.dirname(cc_path), exist_ok=True)

    t1 = music.TUNES[0]
    tot_rows = t1['total_rows']
    bars = t1['bars']

    lead_start, lead_on = music.get_flattened_lead(t1['melody'], tot_rows)
    soft = t1.get('soft_intro', False)
    rhythm = music.rhythm_of(t1)
    bass_start, bass_on = music.get_flattened_bass(t1['chords'], bars, tot_rows,
                                                   soft, rhythm)
    drum_at = music.get_flattened_drums(bars, tot_rows, soft, rhythm)

    assert all(bass_on), "kMusicBassOn was dropped on the grounds that the bass never rests"

    lead_on_bits = pack_bits1(lead_on)
    drum_bits = pack_bits2([DRUM_CODE[v] for v in drum_at])

    rows_per_bar = t1['rows_per_bar']
    lead_pat = dedup_bars(lead_start, rows_per_bar)
    bass_pat = dedup_bars(bass_start, rows_per_bar)
    leadon_pat = dedup_bars(lead_on_bits, rows_per_bar // 8)
    drum_pat = dedup_bars(drum_bits, rows_per_bar // 4)

    with open(cc_path, "w") as f:
        f.write('#include "musicdef.h"\n\n')
        f.write("// Generated by tools/generate_music.py from lib/music.py.\n")
        f.write("// Do not edit; run `make music`.\n\n")
        f.write("#ifdef __ENABLE_SOUND__\n\n")
        f.write("// Octave-6 note table (12 entries)\n")
        f.write("const uint16_t kMusicNoteTable[12] = {\n    ")
        f.write(", ".join(str(v) for v in music.NOTE6))
        f.write("\n};\n\n")

        # Chords are already one entry a bar, so the dedup is over whole
        # entries rather than over rows; four bytes each against a one byte
        # index makes it the best ratio of the five.
        chord_pats = []
        chord_index = []
        chord_names = []
        for name, root, triad in t1['chords']:
            key = (root, tuple(triad))
            if key not in chord_pats:
                chord_pats.append(key)
                chord_names.append(name)
            chord_index.append(chord_pats.index(key))
        f.write(f"// Chord table: {len(chord_pats)} distinct of {bars} bars, "
                f"{len(chord_pats) * 4 + bars} bytes instead of {bars * 4}.\n")
        f.write(f"const music_chord_t kMusicChordPat[{len(chord_pats)}] = {{\n")
        for (root, triad), name in zip(chord_pats, chord_names):
            f.write(f"    {{ {root}, {{ {triad[0]}, {triad[1]}, {triad[2]} }} }}, // {name}\n")
        f.write("};\n\n")
        f.write(f"const uint8_t kMusicChordBar[{bars}] = {{\n")
        _emit_rows(f, chord_index, 12)
        f.write("};\n\n")

        f.write(f"// Master volume per bar ({bars} entries, 0..15)\n")
        f.write(f"const uint8_t kMusicVolMap[{bars}] = {{\n")
        _emit_rows(f, t1['vol_map'], 8)
        f.write("};\n\n")

        _emit_bar_table(f, "kMusicLeadStart", *lead_pat, rows_per_bar, 12,
                        "Lead start midi notes, 0 = rest")
        _emit_bar_table(f, "kMusicBassStart", *bass_pat, rows_per_bar, 12,
                        "Bass start midi notes, 0 = rest")

        _emit_bar_table(f, "kMusicLeadOnBits", *leadon_pat, rows_per_bar // 8, 12,
                        "Lead gate, 1 bit per row LSB first. MUSIC_LEAD_ON(row)")
        _emit_bar_table(f, "kMusicDrumBits", *drum_pat, rows_per_bar // 4, 12,
                        "Drum hits, 2 bits per row. MUSIC_DRUM_AT(row)")

        ins = music.INS

        def _tone(name):
            d = ins[name]
            return f"{{ {d['ad']}, {d['sr']}, {d['wave']}, 0, 0, 0 }}"

        def _drum(name):
            d = ins[name]
            # freq_step, not freq_to: see the struct comment in musicdef.h.
            step = (d['from'] - d['to']) // d['frames']
            return f"{{ {d['ad']}, {d['sr']}, {d['wave']}, {d['frames']}, {d['from']}, {step} }}"

        f.write(f"const music_instrument_t kMusicInsLead = {_tone('lead')};\n")
        f.write(f"const music_instrument_t kMusicInsBass = {_tone('bass')};\n")
        f.write(f"const music_instrument_t kMusicInsArp  = {_tone('arp')};\n\n")
        f.write("// Indexed by MUSIC_DRUM_AT(row) - 1.\n")
        f.write("const music_instrument_t kMusicDrumIns[3] = {\n")
        for _name in ("kick", "snare", "hat"):
            _d = ins[_name]
            f.write(f"    {_drum(_name)}, // {_name}: {_d['from']} -> {_d['to']}"
                    f" over {_d['frames']} frames\n")
        f.write("};\n")

        f.write("\n#endif // __ENABLE_SOUND__\n")

    print(f"Generated {cc_path}")

def update_html_data_block():
    html_path = os.path.join(REPO_ROOT, "docs", "sid-intro-theme.html")
    if not os.path.exists(html_path):
        print(f"Warning: {html_path} does not exist.")
        return

    with open(html_path, "r", encoding="utf-8") as f:
        content = f.read()

    start_marker = "// ==========================================================================\n//  SONG DATA"
    end_marker = "// ==========================================================================\n//  THE PLAYER"

    start_pos = content.find(start_marker)
    end_pos = content.find(end_marker)

    if start_pos == -1 or end_pos == -1:
        print("Warning: Could not find song data section markers in sid-intro-theme.html")
        return

    js_block = []
    js_block.append("// ==========================================================================")
    js_block.append("//  SONG DATA  - generated from lib/music.py via tools/generate_music.py.")
    js_block.append("// ==========================================================================")

    # Process all tunes in music.TUNES
    js_block.append("const TUNES = [")
    for idx, tune in enumerate(music.TUNES):
        lead_start, lead_on = music.get_flattened_lead(tune['melody'], tune['total_rows'])
        soft = tune.get('soft_intro', False)
        rhythm = music.rhythm_of(tune)
        bass_start, bass_on = music.get_flattened_bass(
            tune['chords'], tune['bars'], tune['total_rows'], soft, rhythm)
        drum_at = music.get_flattened_drums(tune['bars'], tune['total_rows'],
                                            soft, rhythm)

        js_block.append("  {")
        js_block.append(f"    id: '{tune['id']}',")
        js_block.append(f"    name: '{tune['name']}',")
        js_block.append(f"    speed: {tune['speed']},")
        js_block.append(f"    rows_per_bar: {tune['rows_per_bar']},")
        js_block.append(f"    bars: {tune['bars']},")
        js_block.append(f"    total_rows: {tune['total_rows']},")
        js_block.append(f"    total_frames: {tune['total_frames']},")
        js_block.append("    chords: [")
        for cname, root, triad in tune['chords']:
            js_block.append(f"      ['{cname}',{root},{triad}],")
        js_block.append("    ],")
        js_block.append(f"    leadStart: {lead_start},")
        js_block.append(f"    leadOn: {[1 if v else 0 for v in lead_on]},")
        js_block.append(f"    bassStart: {bass_start},")
        js_block.append(f"    bassOn: {[1 if v else 0 for v in bass_on]},")
        formatted_drums = "[" + ", ".join(f"'{v}'" if v else "null" for v in drum_at) + "]"
        js_block.append(f"    drumAt: {formatted_drums},")
        js_block.append(f"    volMap: {tune['vol_map']}")
        comma = "," if idx < len(music.TUNES) - 1 else ""
        js_block.append(f"  }}{comma}")
    js_block.append("];\n")

    js_block.append("// Default active tune pointer")
    js_block.append("let activeTuneIdx = 0;")
    js_block.append("let currentTune = TUNES[activeTuneIdx];\n")

    js_block.append("// Active parameters shortcut getters")
    js_block.append("let SPEED = currentTune.speed;")
    js_block.append("let ROWS_PER_BAR = currentTune.rows_per_bar;")
    js_block.append("let BARS = currentTune.bars;")
    js_block.append("let TOTAL_ROWS = currentTune.total_rows;")
    js_block.append("let TOTAL_FRAMES = currentTune.total_frames;")
    js_block.append("let CHORDS = currentTune.chords;")
    js_block.append("let leadStart = currentTune.leadStart;")
    js_block.append("let leadOn = currentTune.leadOn;")
    js_block.append("let bassStart = currentTune.bassStart;")
    js_block.append("let bassOn = currentTune.bassOn;")
    js_block.append("let drumAt = currentTune.drumAt;")
    js_block.append("let volMap = currentTune.volMap;\n")

    js_block.append("function setTune(idx){")
    js_block.append("  activeTuneIdx = idx;")
    js_block.append("  currentTune = TUNES[activeTuneIdx];")
    js_block.append("  SPEED = currentTune.speed;")
    js_block.append("  ROWS_PER_BAR = currentTune.rows_per_bar;")
    js_block.append("  BARS = currentTune.bars;")
    js_block.append("  TOTAL_ROWS = currentTune.total_rows;")
    js_block.append("  TOTAL_FRAMES = currentTune.total_frames;")
    js_block.append("  CHORDS = currentTune.chords;")
    js_block.append("  leadStart = currentTune.leadStart;")
    js_block.append("  leadOn = currentTune.leadOn;")
    js_block.append("  bassStart = currentTune.bassStart;")
    js_block.append("  bassOn = currentTune.bassOn;")
    js_block.append("  drumAt = currentTune.drumAt;")
    js_block.append("  volMap = currentTune.volMap;")
    js_block.append("  if(typeof updateTuneUI === 'function') updateTuneUI();")
    js_block.append("}\n")

    js_block.append("// ---------- instruments: [attack, decay, sustain, release] nibbles ----------")
    # Emitted from music.INS, not written out by hand. These used to be
    # hardcoded literals here while the C side read music.INS, so editing an
    # envelope in Python would have changed the C64 and not the browser. They
    # happened to agree, which is why it went unnoticed.
    wave_name = {music.CTRL_PULSE: "CTRL_PULSE", music.CTRL_SAW: "CTRL_SAW",
                 music.CTRL_NOISE: "CTRL_NOISE", music.CTRL_TRI: "CTRL_TRI"}
    js_block.append("const INS = {")
    for i, (name, d) in enumerate(music.INS.items()):
        parts = [f"ad:0x{d['ad']:02X}", f"sr:0x{d['sr']:02X}", f"wave:{wave_name[d['wave']]}"]
        if 'frames' in d:
            parts += [f"frames:{d['frames']}", f"from:0x{d['from']:04X}", f"to:0x{d['to']:04X}"]
        comma = "," if i < len(music.INS) - 1 else ""
        js_block.append(f"  {name:<5}: {{{', '.join(parts)}}}{comma}")
    js_block.append("};")
    js_block.append(f"const BASS_PW = 0x{music.BASS_PW:04X};\n")

    new_section = "\n".join(js_block)
    updated_content = content[:start_pos] + new_section + "\n\n" + content[end_pos:]

    with open(html_path, "w", encoding="utf-8") as f:
        f.write(updated_content)
    print(f"Updated data block in {html_path}")

def main():
    print("Validating song data...")
    for t in music.TUNES:
        music.validate_song_data(t['melody'], t['chords'], t['bars'])
    print("All song data valid.")

    generate_c64_headers()
    generate_c64_source()
    update_html_data_block()

if __name__ == "__main__":
    main()
