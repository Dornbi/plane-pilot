import re
import unittest
import sys
import os

# Adjust path to import lib and tools
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from lib import music
from tools import generate_music

class TestMusicData(unittest.TestCase):

    def test_primary_tune_constants(self):
        """Pin the shipping tune's shape, so a resize is a deliberate edit."""
        self.assertEqual(music.BARS, 24)
        self.assertEqual(music.SPEED, 6)
        self.assertEqual(music.TOTAL_ROWS, music.BARS * music.ROWS_PER_BAR)
        self.assertEqual(music.TOTAL_FRAMES, music.TOTAL_ROWS * music.SPEED)
        self.assertEqual(music.TOTAL_FRAMES, 2304)          # 46.08 s at 50 Hz
        self.assertEqual(750 / music.SPEED, 125.0)          # BPM

    def test_loop_is_a_whole_number_of_pwm_cycles(self):
        """The player's pulse-width sweep cycles every 256 frames.

        A loop that is not a multiple of that leaves the pulse width somewhere
        different on every pass, and the loop-identity check in music_test.cc
        fails for a reason that has nothing to do with the notes. At 16 rows per
        bar this constrains the bar count to a multiple of 8. See
        docs/music.md section 3.
        """
        PWM_CYCLE_FRAMES = 256
        for t in music.TUNES:
            self.assertEqual(
                t['total_frames'] % PWM_CYCLE_FRAMES, 0,
                f"Tune {t['id']}: {t['bars']} bars gives {t['total_frames']} frames, "
                f"not a multiple of {PWM_CYCLE_FRAMES}")

    def test_volume_map(self):
        """One entry per bar, in range, and no cliff at the loop seam."""
        for t in music.TUNES:
            vm = t['vol_map']
            self.assertEqual(len(vm), t['bars'], f"Tune {t['id']} vol_map is not one per bar")
            for i, v in enumerate(vm):
                self.assertTrue(0 <= v <= 15, f"Tune {t['id']} bar {i+1} volume {v} outside 0..15")
            # The last bar hands over to the first on every loop.
            self.assertLessEqual(
                abs(vm[-1] - vm[0]), 4,
                f"Tune {t['id']}: volume steps {vm[-1]} -> {vm[0]} at the loop seam")

    def test_soft_intro_structure(self):
        """A soft-intro tune opens with no lead and no drums, then hats only.

        This used to be inferred from `bars == 32` inside the flatteners, which
        broke as soon as both arrangements were 16 bars. It is a flag now, and
        this is the test that says so.
        """
        for t in music.TUNES:
            if not t.get('soft_intro'):
                continue
            lead_start, _ = music.get_flattened_lead(t['melody'], t['total_rows'])
            drum_at = music.get_flattened_drums(t['bars'], t['total_rows'], True)
            build = music.SOFT_INTRO_BARS * music.ROWS_PER_BAR
            hats = music.SOFT_HAT_BARS * music.ROWS_PER_BAR
            self.assertTrue(all(v == 0 for v in lead_start[:build]),
                            f"Tune {t['id']}: lead sounds during the pedal build")
            self.assertTrue(all(v is None for v in drum_at[:build]),
                            f"Tune {t['id']}: drums present during the pedal build")
            self.assertTrue(all(v in (None, 'hat') for v in drum_at[build:hats]),
                            f"Tune {t['id']}: more than hats under the motif")

    def test_all_tunes_validation(self):
        """Verify that all tunes in lib/music.py pass validation checks."""
        for t in music.TUNES:
            self.assertTrue(music.validate_song_data(t['melody'], t['chords'], t['bars']))

    def test_bar_durations(self):
        """Verify that every bar in every tune's melody sums to exactly 16 rows."""
        for t in music.TUNES:
            for idx, bar_str in enumerate(t['melody']):
                row_sum = 0
                tokens = bar_str.strip().split()
                for tok in tokens:
                    _, length_str = tok.split(':')
                    row_sum += int(length_str, 10)
                self.assertEqual(row_sum, 16, f"Tune {t['id']} bar {idx+1} does not sum to 16 rows")

    def test_note_ranges(self):
        """Verify that all melody and chord notes fall in MIDI 28-83."""
        for t in music.TUNES:
            for idx, bar_str in enumerate(t['melody']):
                tokens = bar_str.strip().split()
                for tok in tokens:
                    note_name, _ = tok.split(':')
                    if note_name != '-':
                        midi = music.name_to_midi(note_name)
                        self.assertGreaterEqual(midi, 28, f"Tune {t['id']} melody note {note_name} below MIDI 28")
                        self.assertLessEqual(midi, 83, f"Tune {t['id']} melody note {note_name} above MIDI 83")

            for idx, (name, root, triad) in enumerate(t['chords']):
                self.assertGreaterEqual(root, 28, f"Tune {t['id']} chord {name} root below 28")
                self.assertLessEqual(root, 83, f"Tune {t['id']} chord {name} root above 83")
                for tm in triad:
                    self.assertGreaterEqual(tm, 28, f"Tune {t['id']} chord {name} triad note below 28")
                    self.assertLessEqual(tm, 83, f"Tune {t['id']} chord {name} triad note above 83")

    def test_chord_table_structure(self):
        """Verify chord tables have valid lengths and triads."""
        for t in music.TUNES:
            self.assertEqual(len(t['chords']), t['bars'])
            for name, root, triad in t['chords']:
                self.assertEqual(len(triad), 3)

    def test_flattened_row_counts(self):
        """Verify flattened tables match total_rows for each tune."""
        for t in music.TUNES:
            soft = t.get('soft_intro', False)
            lead_start, lead_on = music.get_flattened_lead(t['melody'], t['total_rows'])
            bass_start, bass_on = music.get_flattened_bass(
                t['chords'], t['bars'], t['total_rows'], soft)
            drum_at = music.get_flattened_drums(t['bars'], t['total_rows'], soft)

            self.assertEqual(len(lead_start), t['total_rows'])
            self.assertEqual(len(lead_on), t['total_rows'])
            self.assertEqual(len(bass_start), t['total_rows'])
            self.assertEqual(len(bass_on), t['total_rows'])
            self.assertEqual(len(drum_at), t['total_rows'])

    def test_generation_and_html_sync(self):
        """Generate files and verify that sid-intro-theme.html and musicdef.cc agree."""
        generate_music.main()

        cc_path = os.path.join(REPO_ROOT, "c64o", "musicdef.cc")
        html_path = os.path.join(REPO_ROOT, "docs", "sid-intro-theme.html")

        self.assertTrue(os.path.exists(cc_path))
        self.assertTrue(os.path.exists(html_path))

        with open(cc_path, "r", encoding="utf-8") as f:
            cc_content = f.read()
        with open(html_path, "r", encoding="utf-8") as f:
            html_content = f.read()

        # Verify Note Table in .cc matches music.NOTE6
        for note in music.NOTE6:
            self.assertIn(str(note), cc_content)

        # Verify HTML contains TUNES array, with every tune named
        self.assertIn("const TUNES = [", html_content)
        for t in music.TUNES:
            self.assertIn(t['name'], html_content,
                          f"Tune {t['id']} is missing from the reference page")

        # The generated header must describe the tune the generator selected.
        h_path = os.path.join(REPO_ROOT, "c64o", "musicdef.h")
        with open(h_path, "r", encoding="utf-8") as f:
            h_content = f.read()
        shipped = music.TUNES[0]
        self.assertIn(f"kMusicBars = {shipped['bars']};", h_content)
        self.assertIn(f"kMusicSpeed = {shipped['speed']};", h_content)
        self.assertIn(f"kMusicTotalFrames = {shipped['total_frames']};", h_content)
        # The lanes are bar-indexed, so the header declares a pattern array and
        # a one-byte-per-bar index rather than one flat row array.
        self.assertIn(f"kMusicLeadStartPat[][{shipped['rows_per_bar']}]", h_content)
        self.assertIn(f"kMusicLeadStartBar[{shipped['bars']}]", h_content)

    def test_volume_map_reaches_the_c64(self):
        """The fade has to exist in all three copies, not just two.

        It shipped in Python and in the browser and not in C for a whole phase,
        which would have meant a build with no opening fade and a hard edge at
        the loop point. docs/music.md section 3.
        """
        generate_music.main()
        cc, h = self._read_generated()
        self.assertIn("kMusicVolMap", h)
        self.assertEqual(self._c_array(cc, "kMusicVolMap", music.BARS),
                         music.TUNES[0]['vol_map'])

    # ---- packed tables: docs/music.md section 4, option B -------------------

    def _read_generated(self):
        cc_path = os.path.join(REPO_ROOT, "c64o", "musicdef.cc")
        h_path = os.path.join(REPO_ROOT, "c64o", "musicdef.h")
        with open(cc_path, encoding="utf-8") as f:
            cc = re.sub(r'//[^\n]*', '', f.read())   # comments hold bar numbers
        with open(h_path, encoding="utf-8") as f:
            h = f.read()
        return cc, h

    def _c_array(self, cc, name, size):
        m = re.search(r'const uint8_t ' + name + r'\[' + str(size) + r'\] = \{(.*?)\};',
                      cc, re.S)
        self.assertIsNotNone(m, f"{name}[{size}] not found in musicdef.cc")
        vals = [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', m.group(1))]
        self.assertEqual(len(vals), size, f"{name} has {len(vals)} entries, expected {size}")
        return vals

    def _bar_table(self, cc, name, per_bar):
        """Rebuilds one flat per-row lane from its pattern array and bar index.

        This is MUSIC_BAR_OF / MUSIC_IN_BAR applied by hand. If the split in
        musicdef.h ever changes shape, this is what notices.
        """
        m = re.search(r'const uint8_t ' + name + r'Pat\[(\d+)\]\[' + str(per_bar)
                      + r'\] = \{(.*?)\n\};', cc, re.S)
        self.assertIsNotNone(m, f"{name}Pat[][{per_bar}] not found in musicdef.cc")
        pats = [[int(x) for x in row.split(',') if x.strip()]
                for row in re.findall(r'\{([^}]*)\}', m.group(2))]
        self.assertEqual(len(pats), int(m.group(1)))
        for pat in pats:
            self.assertEqual(len(pat), per_bar, f"{name}Pat row is not {per_bar} long")
        index = self._c_array(cc, name + "Bar", music.BARS)
        for b in index:
            self.assertLess(b, len(pats), f"{name}Bar points past {name}Pat")
        return [v for b in index for v in pats[b]]

    def test_packed_tables_round_trip(self):
        """Unpacking the C must reproduce exactly what the browser reference got.

        The two consumers use different encodings on purpose - JS has no reason
        to pay for packing - so this is the test that keeps them the same data.
        Two layers are undone here: the bar index, and then option B's bit
        packing. If either changes, this fails.
        """
        generate_music.main()
        cc, _ = self._read_generated()

        t = music.TUNES[0]
        rows = t['total_rows']
        rpb = t['rows_per_bar']
        soft = t.get('soft_intro', False)
        lead_start, lead_on = music.get_flattened_lead(t['melody'], rows)
        drum_at = music.get_flattened_drums(t['bars'], rows, soft)
        rhythm = music.rhythm_of(t)
        bass_start, _ = music.get_flattened_bass(t['chords'], t['bars'], rows,
                                                 soft, rhythm)

        # Lane 1 and 2: one byte a row, bar indexed only.
        self.assertEqual(self._bar_table(cc, "kMusicLeadStart", rpb), lead_start)
        self.assertEqual(self._bar_table(cc, "kMusicBassStart", rpb), bass_start)

        # Lane 3 and 4: bar indexed *and* bit packed. MUSIC_LEAD_ON(row) and
        # MUSIC_DRUM_AT(row), spelled out.
        lead_bits = self._bar_table(cc, "kMusicLeadOnBits", rpb // 8)
        drum_bits = self._bar_table(cc, "kMusicDrumBits", rpb // 4)
        unpacked_lead = [(lead_bits[r >> 3] >> (r & 7)) & 1 for r in range(rows)]
        unpacked_drum = [(drum_bits[r >> 2] >> ((r & 3) * 2)) & 3 for r in range(rows)]

        self.assertEqual(unpacked_lead, [1 if v else 0 for v in lead_on])
        self.assertEqual(unpacked_drum, [generate_music.DRUM_CODE[v] for v in drum_at])

    def test_chords_round_trip(self):
        """The chord lane is bar indexed too - 7 distinct of 24."""
        generate_music.main()
        cc, _ = self._read_generated()

        m = re.search(r'const music_chord_t kMusicChordPat\[(\d+)\] = \{(.*?)\n\};',
                      cc, re.S)
        self.assertIsNotNone(m, "kMusicChordPat not found in musicdef.cc")
        pats = [(int(a), [int(b), int(c), int(d)]) for a, b, c, d in
                re.findall(r'\{ (\d+), \{ (\d+), (\d+), (\d+) \} \}', m.group(2))]
        self.assertEqual(len(pats), int(m.group(1)))
        index = self._c_array(cc, "kMusicChordBar", music.BARS)

        want = [(root, list(triad)) for _name, root, triad in music.TUNES[0]['chords']]
        self.assertEqual([pats[b] for b in index], want)

    def test_bar_dedup_still_pays(self):
        """Every bar-indexed lane must be smaller than the flat array it
        replaced. The index costs a byte a bar, so a lane whose bars stopped
        repeating would silently grow - this is the tripwire for that."""
        t = music.TUNES[0]
        rpb = t['rows_per_bar']
        rows = t['total_rows']
        soft = t.get('soft_intro', False)
        lead_start, lead_on = music.get_flattened_lead(t['melody'], rows)
        rhythm = music.rhythm_of(t)
        bass_start, _ = music.get_flattened_bass(t['chords'], t['bars'], rows,
                                                 soft, rhythm)
        drum_at = music.get_flattened_drums(t['bars'], rows, soft)

        lanes = {
            'lead': (lead_start, rpb),
            'bass': (bass_start, rpb),
            'leadOn': (generate_music.pack_bits1(lead_on), rpb // 8),
            'drum': (generate_music.pack_bits2(
                [generate_music.DRUM_CODE[v] for v in drum_at]), rpb // 4),
        }
        for name, (values, per_bar) in lanes.items():
            pats, index = generate_music.dedup_bars(values, per_bar)
            flat = len(values)
            packed = len(pats) * per_bar + len(index)
            self.assertLess(packed, flat,
                            f"{name}: bar dedup now costs {packed} against {flat} flat")

    def test_player_never_reads_a_sid_register(self):
        """c64o/music.cc must only ever STORE to SID_REGS, never read it.

        $D400-$D418 are write-only on real hardware: a read returns whatever
        was last on the data bus, not what was written. The player used to read
        the control register back to test its own gate bit, which on a C64 made
        voice 3 sound in some bars and not others depending on what the VIC-II
        left behind.

        No host test can catch this. On the host, SID_REGS points at ordinary
        RAM and reads back exactly what was written, so every assertion passes
        while the real machine misbehaves. That is why the check is here, on the
        source, rather than in music_test.cc.

        The rule: SID_REGS[...] may appear only as the target of a plain `=`.
        A compound assignment (&=, |=) is a read-modify-write, and any other
        appearance is a read.
        """
        src_path = os.path.join(REPO_ROOT, "c64o", "music.cc")
        with open(src_path, encoding="utf-8") as f:
            src = re.sub(r'//[^\n]*', '', f.read())      # strip line comments

        offenders = []
        for m in re.finditer(r'SID_REGS\s*\[', src):
            # Find the matching ']' and look at what follows.
            i, depth = m.end(), 1
            while i < len(src) and depth:
                if src[i] == '[':
                    depth += 1
                elif src[i] == ']':
                    depth -= 1
                i += 1
            after = src[i:i + 3].lstrip()
            line = src.count('\n', 0, m.start()) + 1
            if not after.startswith('=') or after.startswith('=='):
                offenders.append(f"line ~{line}: SID_REGS[...] {after[:2]!r} is a read")

        self.assertEqual(offenders, [],
                         "music.cc reads a write-only SID register:\n  " +
                         "\n  ".join(offenders))

    def test_generated_header_does_not_include_stdbool(self):
        """musicdef.h must not pull in <stdbool.h>.

        bool.h defines `bool` as a *macro* on the host build. clang's
        <stdbool.h> #undefs that macro in C++ mode; gcc's leaves it alone. So
        including it here changed the meaning of `bool` partway through any
        file that includes music.h - and sound.cc does - which made
        sound_wind_audible() declared as returning `unsigned char` and defined
        as returning `bool`. It compiled on Linux and failed on macOS.

        The header declares no bool, so the include was pure cost. This test
        exists because the file is generated: the fix lives in the exporter and
        would come back silently if someone re-added the line.
        """
        generate_music.main()
        _, h = self._read_generated()
        self.assertNotIn("stdbool", h)

    def test_bass_on_was_dropped_and_stays_dropped(self):
        """kMusicBassOn was 256 bytes of the constant 1. It must not come back,
        and the premise it was deleted on must keep holding."""
        cc, h = self._read_generated()
        # Match declarations, not the comment in musicdef.h that explains why
        # the array is gone - that comment is worth keeping.
        h_code = re.sub(r'//[^\n]*', '', h)
        self.assertNotRegex(cc, r'\bkMusicBassOn\s*\[')
        self.assertNotRegex(h_code, r'\bkMusicBassOn\s*\[')
        self.assertIn("MUSIC_BASS_ON", h_code)
        for t in music.TUNES:
            _, bass_on = music.get_flattened_bass(
                t['chords'], t['bars'], t['total_rows'], t.get('soft_intro', False))
            self.assertTrue(all(bass_on),
                            f"Tune {t['id']} has a bass rest; MUSIC_BASS_ON(row) is a lie")

if __name__ == '__main__':
    unittest.main()
