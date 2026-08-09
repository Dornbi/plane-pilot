import unittest
import sys
import os
import re

# Adjust path to import lib and tools
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from lib import music
from tools import generate_music

class TestMusicData(unittest.TestCase):

    def test_song_validation(self):
        """Verify that lib/music.py passes validation checks."""
        self.assertTrue(music.validate_song_data())

    def test_bar_durations(self):
        """Verify that every bar in MELODY sums to exactly 16 rows."""
        for idx, bar_str in enumerate(music.MELODY):
            row_sum = 0
            tokens = bar_str.strip().split()
            for tok in tokens:
                _, length_str = tok.split(':')
                row_sum += int(length_str, 10)
            self.assertEqual(row_sum, 16, f"Bar {idx+1} does not sum to 16 rows")

    def test_note_ranges(self):
        """Verify that all melody and chord notes fall in MIDI 28-83."""
        for idx, bar_str in enumerate(music.MELODY):
            tokens = bar_str.strip().split()
            for tok in tokens:
                note_name, _ = tok.split(':')
                if note_name != '-':
                    midi = music.name_to_midi(note_name)
                    self.assertGreaterEqual(midi, 28, f"Melody note {note_name} below MIDI 28")
                    self.assertLessEqual(midi, 83, f"Melody note {note_name} above MIDI 83")

        for idx, (name, root, triad) in enumerate(music.CHORDS):
            self.assertGreaterEqual(root, 28, f"Chord {name} root below 28")
            self.assertLessEqual(root, 83, f"Chord {name} root above 83")
            for tm in triad:
                self.assertGreaterEqual(tm, 28, f"Chord {name} triad note below 28")
                self.assertLessEqual(tm, 83, f"Chord {name} triad note above 83")

    def test_chord_table_structure(self):
        """Verify chord table has 16 entries and valid triads."""
        self.assertEqual(len(music.CHORDS), 16)
        for name, root, triad in music.CHORDS:
            self.assertEqual(len(triad), 3)

    def test_flattened_row_counts(self):
        """Verify flattened tables have length 256."""
        lead_start, lead_on = music.get_flattened_lead()
        bass_start, bass_on = music.get_flattened_bass()
        drum_at = music.get_flattened_drums()

        self.assertEqual(len(lead_start), 256)
        self.assertEqual(len(lead_on), 256)
        self.assertEqual(len(bass_start), 256)
        self.assertEqual(len(bass_on), 256)
        self.assertEqual(len(drum_at), 256)

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

        # Verify HTML contains speed & bars definitions matching music.py
        self.assertIn(f"const SPEED = {music.SPEED};", html_content)
        self.assertIn(f"const ROWS_PER_BAR = {music.ROWS_PER_BAR}, BARS = {music.BARS};", html_content)

if __name__ == '__main__':
    unittest.main()
