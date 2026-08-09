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
        self.assertEqual(music.BARS, 16)
        self.assertEqual(music.SPEED, 6)
        self.assertEqual(music.TOTAL_ROWS, music.BARS * music.ROWS_PER_BAR)
        self.assertEqual(music.TOTAL_FRAMES, music.TOTAL_ROWS * music.SPEED)
        self.assertEqual(music.TOTAL_FRAMES, 1536)          # 30.72 s at 50 Hz
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
        self.assertIn(f"kMusicLeadStart[{shipped['total_rows']}]", h_content)

    @unittest.expectedFailure
    def test_volume_map_reaches_the_c64(self):
        """The fade exists in Python and in the browser, but not in C.

        docs/music.md section 8, phase 1b. Marked expected-failure rather than
        omitted so that it starts passing - loudly - the moment the export is
        added, instead of being remembered.
        """
        generate_music.main()
        h_path = os.path.join(REPO_ROOT, "c64o", "musicdef.h")
        with open(h_path, "r", encoding="utf-8") as f:
            self.assertIn("kMusicVolMap", f.read())

if __name__ == '__main__':
    unittest.main()
