#!/usr/bin/env python3
import os
import sys
import tempfile
import unittest

# Add tools/ to path
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, 'tools'))

import analyze_ram


class TestAnalyzeRam(unittest.TestCase):

    def test_get_category_sound_and_music_separate(self):
        # Sound Effects symbols
        self.assertEqual(analyze_ram.get_category('sound_update'), 'Sound Effects')
        self.assertEqual(analyze_ram.get_category('sound_silence'), 'Sound Effects')
        self.assertEqual(analyze_ram.get_category('sound_volume'), 'Sound Effects')
        self.assertEqual(analyze_ram.get_category('kEngineFreq'), 'Sound Effects')
        self.assertEqual(analyze_ram.get_category('kWindFreq'), 'Sound Effects')
        self.assertEqual(analyze_ram.get_category('sound_shadow'), 'Sound Effects')

        # Music symbols
        self.assertEqual(analyze_ram.get_category('music_start'), 'Music')
        self.assertEqual(analyze_ram.get_category('music_stop'), 'Music')
        self.assertEqual(analyze_ram.get_category('music_tick'), 'Music')
        self.assertEqual(analyze_ram.get_category('kMusicLeadStart'), 'Music')
        self.assertEqual(analyze_ram.get_category('kMusicBassStart'), 'Music')
        self.assertEqual(analyze_ram.get_category('kMusicVolMap'), 'Music')
        self.assertEqual(analyze_ram.get_category('kVolumeMix'), 'Music')
        self.assertEqual(analyze_ram.get_category('kMusicLeadOnBits'), 'Music')

    def test_parse_map_with_mock(self):
        mock_map_content = """
header
objects
0100 - 0120 : sound_update, NATIVE_CODE:code
0120 - 0140 : music_tick, NATIVE_CODE:code
0140 - 0180 : kMusicLeadStart, DATA:data
0180 - 01A0 : kEngineFreq, DATA:data
"""
        with tempfile.NamedTemporaryFile('w', delete=False, suffix='.map') as tmp:
            tmp.write(mock_map_content)
            tmp_path = tmp.name

        try:
            cats = analyze_ram.parse_map(tmp_path)
            self.assertEqual(cats['Sound Effects']['Code'], 32)
            self.assertEqual(cats['Sound Effects']['Data'], 32)
            self.assertEqual(cats['Music']['Code'], 32)
            self.assertEqual(cats['Music']['Data'], 64)
        finally:
            if os.path.exists(tmp_path):
                os.remove(tmp_path)


if __name__ == '__main__':
    unittest.main()
