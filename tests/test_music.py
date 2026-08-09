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
            lead_start, lead_on = music.get_flattened_lead(t['melody'], t['total_rows'])
            bass_start, bass_on = music.get_flattened_bass(t['chords'], t['bars'], t['total_rows'])
            drum_at = music.get_flattened_drums(t['bars'], t['total_rows'])

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

        # Verify HTML contains TUNES array
        self.assertIn("const TUNES = [", html_content)
        self.assertIn("32-Bar Atmospheric Theme (125 BPM Build/Fade)", html_content)

if __name__ == '__main__':
    unittest.main()
