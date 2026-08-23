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

    # A minimal map. The addresses are inside no linker region and clear of
    # every FIXED range, so the walk classifies the rest as orphan free space
    # and the categories are the only thing under test here.
    MOCK_MAP = """
sections
0100 - 0200 : DATA, code

regions
0100 - 0200 : 0200, 0080, main

objects
0100 - 0120 : sound_update, NATIVE_CODE:code
0120 - 0140 : music_tick, NATIVE_CODE:code
0140 - 0180 : kMusicLeadStart, DATA:data
0180 - 01A0 : kEngineFreq, DATA:data
"""

    def _parse_mock(self):
        with tempfile.NamedTemporaryFile('w', delete=False, suffix='.map') as tmp:
            tmp.write(self.MOCK_MAP)
            tmp_path = tmp.name
        try:
            return analyze_ram.parse_map(tmp_path)
        finally:
            if os.path.exists(tmp_path):
                os.remove(tmp_path)

    def test_parse_map_with_mock(self):
        cats, _walk = self._parse_mock()
        self.assertEqual(cats['Sound Effects']['Code'], 32)
        self.assertEqual(cats['Sound Effects']['Data'], 32)
        self.assertEqual(cats['Music']['Code'], 32)
        self.assertEqual(cats['Music']['Data'], 64)

    def test_walk_covers_the_whole_address_space(self):
        """The walk is the only source of the free figure, so it has to account
        for all 65,536 bytes. parse_map() asserts this internally; this pins it
        as a property rather than an implementation detail."""
        _cats, walk = self._parse_mock()
        self.assertEqual(sum(walk['totals'].values()), 0x10000)

    def test_feature_table_bridges_to_the_walk(self):
        """Symbol sizes plus machine-owned ranges minus double-counted
        addresses must equal what the walk calls used. If these two ever
        disagree the report is back to inventing free space."""
        cats, walk = self._parse_mock()
        cols = ('Code', 'Data', 'BSS', 'ZP', 'VRAM')
        feature_total = sum(cat[c] for cat in cats.values() for c in cols)
        overlap = (walk['sum_object_sizes'] + walk['sum_fixed_sizes']
                   - walk['totals'][walk['USED']])
        self.assertEqual(feature_total + walk['unowned_fixed'] - overlap,
                         walk['totals'][walk['USED']])

    def test_fixed_ranges_do_not_overlap_each_other(self):
        """FIXED is maintained by hand against mem.h and friends. Two entries
        claiming the same byte would be double-counted in the feature table."""
        spans = sorted((s, e) for s, e, _o, _l in analyze_ram.FIXED)
        for (s1, e1), (s2, _e2) in zip(spans, spans[1:]):
            self.assertLessEqual(e1, s2, f"FIXED ranges overlap at ${s2:04X}")

    def test_real_map_reconciles(self):
        """The shipped map, if it has been built. Guards against a layout
        change that FIXED has not been told about."""
        map_path = os.path.join(REPO_ROOT, 'c64o', 'ppilot.map')
        if not os.path.exists(map_path):
            self.skipTest('c64o/ppilot.map not built')
        _cats, walk = analyze_ram.parse_map(map_path)
        self.assertEqual(sum(walk['totals'].values()), 0x10000)
        self.assertEqual(walk['clashes'], [],
                         'a linker object landed inside a hand-placed range')


if __name__ == '__main__':
    unittest.main()
