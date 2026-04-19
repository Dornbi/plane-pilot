import unittest
import os
import sys

from lib import c64_converter

sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

class TestTolerance(unittest.TestCase):
    def test_tolerance(self):
        # Create two 8x8 blocks that differ by 1 pixel
        # Row 0, pair 0: 0 vs 1
        
        # We need mock RAM/BMP
        screen_ram = bytearray(1000)
        color_ram = bytearray(1000)
        bitmap = bytearray(8000)
        bg = 0
        
        # Char 0: All 0
        # Char 1: Row 0 has 1 pixel different
        
        # Bitmap byte: (00 00 00 00) vs (01 00 00 00) -> 0x00 vs 0x40
        # Wait, pixel values 0, 1, 2, 3.
        # 01 is 1 (Color 1). 00 is 0 (Color 0).
        # We need to ensure Color 1 is used correctly.
        # Let's set Screen RAM to use colors 1, 2.
        
        # Block 0: Use colors 0, 1, 2, 3.
        screen_ram[0] = 0x12
        color_ram[0] = 0x03
        
        # Block 1: Same colors
        screen_ram[1] = 0x12
        color_ram[1] = 0x03
        
        # Block 0 bitmap: All 0
        # Block 1 bitmap: Byte 0 is 0x40 (Bits 01 00 00 00).
        # Pixel[0] is 1. Original was 0.
        # Diff = 1 pixel.
        
        bitmap[8] = 0x40 # Char 1, Row 0
        
        # Test with tolerance 0 -> Should have 2 chars used (Solid 0 and New Char)
        # Actually, since we pre-seed Solid 0 and Solid 1 (we use 2 colors),
        # Block 0 -> Index 0.
        # Block 1 -> Index 2 (New char).
        colors = [0, 1, 2, 3]
        g, s, c, ch = c64_converter.convert_mcbm_to_mccm(bg, bytes(screen_ram), bytes(color_ram), bytes(bitmap), tolerance=0, colors=colors)
        # Check num chars used. By checking max index in sram.
        max_idx = max(s[0], s[1])
        self.assertEqual(max_idx, 2) # Indices used: matches fixed or first dynamic
        
        # Test with tolerance 1 -> Should have 1 char (mapped to 0)
        # BUT WAIT! If one of the pixels is Ground, and we enforce strict ground tolerance?
        # In this test setup, bg = 0 (Black).
        # RAM[0] used colors 0, 1.
        # So Ground (bg=0) is used.
        # Char 0 bitmap: All 0 (Color 0 = Ground).
        # Char 1 bitmap: Has 1 pixel turned to 01 (Color 1).
        # Diff is Ground vs Color 1.
        # Strict Ground Tolerance says: If diff involves Ground, return 9999.
        # So d = 9999. > tolerance=1.
        # So Char 1 should NOT match Char 0.
        # So we should have 2 chars (Indices 0 and 2).
        
        g, s, c, ch = c64_converter.convert_mcbm_to_mccm(bg, bytes(screen_ram), bytes(color_ram), bytes(bitmap), tolerance=1, colors=colors, ground_tolerance=0)
        max_idx = max(s[0], s[1])
        self.assertEqual(max_idx, 2) # Strict ground check prevented matching!
        
        # Test Non-Ground Diff
        # Let's say bg=5 (Green). But RAM uses colors 0 and 1 (Black and White).
        # Char 0: All 0 (Black).
        # Char 1: One pixel 1 (White).
        # Diff is Black vs White. Neither is Ground (Green).
        # Should match with tolerance=1.
        
        bg_alt = 5
        # We need to make sure Screen RAM uses colors distinct from 5.
        # 0x12 -> Colors 1 and 2.
        # But we need color map.
        screen_ram[2] = 0x12 # Block 2
        bg_col_index = 5
        # Ensure 5 is not 1 or 2.
        
        # We need a new test function or just modify variables here?
        # Let's just create a quick check here.
        
    def test_strict_ground(self):
         # Explicit test for strict ground behavior vs non-ground behavior
         pass # To be implemented if easier logic needed.
         # But the above check covers the "Ground" case.
         
         # Let's check the "Non-Ground" case to ensure normal tolerance still works.
         # Setup: BG=5. Block 2 and 3 use colors 1 and 2.
         # Block 2: All Color 1.
         # Block 3: All Color 1 except one pixel Color 2.
         # Diff is Color 1 vs Color 2. neither is ground.
         
         bg = 5
         sram = bytearray(1000)
         cram = bytearray(1000)
         bmp = bytearray(8000)
         
         # Use colors 1 and 2.
         # 0x12 -> 01=1, 10=2.
         
         sram[0] = 0x12
         cram[0] = 0x00 # 11=0 (unused)
         sram[1] = 0x12
         cram[1] = 0x00
         
         # Block 0: All Color 1 (bits 01).
         # Byte 0xFF? No, 01010101 = 0x55.
         for i in range(8):
             bmp[i] = 0x55
             
         # Block 1: All Color 1 except one pixel Color 2 (bits 10).
         for i in range(8):
             bmp[8+i] = 0x55
             
         # Change first pixel of Block 1 to Color 2 (10)
         # 0x55 = 01 01 01 01
         # Change to 10 01 01 01 = 0x95
         bmp[8] = 0x95
         
         # Diff is 1 pixel. Not involving Ground (5).
         # Should match with tolerance=1.
         
         colors_alt = [5, 1, 2, 3] # Ground=5, Others=1,2,3
         g, s, c, ch = c64_converter.convert_mcbm_to_mccm(bg, bytes(sram), bytes(cram), bytes(bmp), tolerance=1, colors=colors_alt)
         
         # Should reuse.
         # Pre-seeded: Solid 1, Solid 2, Solid 5 (BG).
         # Block 0 matches Solid 1 (Index X).
         # Block 1 matches Solid 1 (Index X) because diff=1 <= tolerance.
         
         self.assertEqual(s[0], s[1])

if __name__ == '__main__':
    unittest.main()
