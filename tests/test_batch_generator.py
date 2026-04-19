import unittest
import os
import re
import sys
import shutil

# Add project root to path
sys.path.append(os.path.dirname(os.path.abspath(__file__)) + '/..')

from lib import batch_generator

class TestBatchGenerator(unittest.TestCase):
    def test_batch_execution(self):
        # Test with limit=2 to be fast
        out_dir = "tests/temp_renders"
        if os.path.exists(out_dir):
            shutil.rmtree(out_dir)
            
        colors = [5, 3, 14, 6]
        rolls_Limit = 2
        
        # Test 1: Default (No alternate center) -> 2 images
        global_chars, box_defs, special_ids = batch_generator.render_batch(colors, rolls_limit=rolls_Limit, output_dir=out_dir)
        files = [f for f in os.listdir(out_dir) if f.endswith(".png")]
        self.assertEqual(len(files), 2)
        shutil.rmtree(out_dir)
        
        # Test 2: With alternate center -> 3 images (Only R8 has period 1, R16U1 has period 16)
        global_chars, box_defs, special_ids = batch_generator.render_batch(colors, rolls_limit=rolls_Limit, output_dir=out_dir, include_alternates=True)
        files = [f for f in os.listdir(out_dir) if f.endswith(".png")]
        self.assertEqual(len(files), 3)
        
        # Verify content format: flight_frame_c160_100_00_r8.png ...
        # (Assuming 'r8' is first in VALID_ROLLS)
        # Check if at least one file matches the expected pattern
        found_match = False
        # Pattern: flight_frame_c(\d+)_(\d+)_(\d+)_(\w+).png
        for f in files:
            if re.match(r"flight_frame_c\d+_\d+_\d+_\w+\.png", f):
                found_match = True
                break
        self.assertTrue(found_match, f"No file matched expected pattern in {files}")

        # Check global_chars
        self.assertGreater(len(global_chars), 0)
        self.assertGreater(len(box_defs), 0)
        
        code = batch_generator.generate_chardefs_content(global_chars)
        self.assertIn("ALL_CHARS = [", code)
        self.assertIn("# Used in", code)
        
        # Clean up
        shutil.rmtree(out_dir)

if __name__ == '__main__':
    unittest.main()
