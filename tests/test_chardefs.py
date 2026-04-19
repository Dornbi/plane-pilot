import unittest
import sys
import os

# Adjust path to import lib
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

from lib import chardefs

class TestChardefs(unittest.TestCase):

    def get_ground_bits(self):
        # Deduce ground bits from CHAR_SOLID_GROUND
        # Use existing logic from chardefs or inspect ALL_CHARS
        try:
            ground_idx = chardefs.CHAR_SOLID_GROUND
            ground_char = chardefs.ALL_CHARS[ground_idx]
            # Assuming uniform color, take last 2 bits of first byte
            # Multicolored pixels are 2 bits wide.
            # Byte: 76543210. 10 is pair 0.
            bits = ground_char[0] & 3
            return bits
        except:
            return None

    def count_pixel_diff(self, char1, char2, ground_bits=None):
        """
        Counts number of differing pixels between two 8-byte characters.
        If ground_bits is set, any difference involving that color 
        counts as a 'major' difference (effectively infinite/exceeds limit).
        """
        diff_count = 0
        limit_break = 9999 
        
        for b1, b2 in zip(char1, char2):
            if b1 == b2:
                continue
                
            # Compare 4 pixels
            for shift in (0, 2, 4, 6):
                p1 = (b1 >> shift) & 0x03
                p2 = (b2 >> shift) & 0x03
                
                if p1 != p2:
                    if ground_bits is not None:
                        if p1 == ground_bits or p2 == ground_bits:
                            return limit_break
                    diff_count += 1
                    
        return diff_count

    def test_no_near_duplicates(self):
        """
        Verify that no two characters in ALL_CHARS are closer than pixel_limit.
        Difference on Ground Color counts as exceeding limit (safe).
        """
        chars = chardefs.ALL_CHARS
        limit = 2
        ground_bits = self.get_ground_bits()
        
        # We can optimize by only checking unique content if ALL_CHARS has duplicates 
        # (but we want to find duplicates in ALL_CHARS itself if they are distinct entries)
        # Actually, pure duplicates (diff=0) are also "closer than limit".
        # So this checks for duplicates too.
        
        failures = []
        
        for i in range(len(chars)):
            for j in range(i + 1, len(chars)):
                c1 = chars[i]
                c2 = chars[j]
                
                diff = self.count_pixel_diff(c1, c2, ground_bits)
                
                if diff < limit:
                    msg = f"Chars {i} and {j} are too similar! Diff: {diff}"
                    failures.append(msg)
                    # Fail early or collect all? Collecting is better for debugging.
                    if len(failures) > 10: break
            if len(failures) > 10: break
            
        if failures:
            self.fail("\n".join(failures))

if __name__ == '__main__':
    unittest.main()
