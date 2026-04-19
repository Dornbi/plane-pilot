
import unittest
import os
import sys

from lib import c64_converter
from lib import c64_graphics
from lib import frame_generator
from lib import roll_angle

# Add project root to path
sys.path.append(os.path.dirname(os.path.abspath(__file__)) + '/..')

class TestC64Converter(unittest.TestCase):
    def setUp(self):
        # Create output directory
        self.out_dir = os.path.join(os.path.dirname(__file__), 'test_frames')
        if not os.path.exists(self.out_dir):
            os.makedirs(self.out_dir)

    def test_convert_simple(self):
        # Create a synthetic MCBM setup
        # 1. Colors
        bg = 0 # Black
        # SR: 0x02 -> 01=0(Black), 10=2(Red)
        # CR: 0x05 -> 11=5(Green)
        # Total colors: 0, 2, 5.
        
        screen_ram = bytearray([0x02] * 1000)
        color_ram = bytearray([0x05] * 1000)
        
        # Bitmap: alternating patterns
        # 00 01 10 11 -> 00011011 bin -> 0x1B
        bitmap = bytearray([0x1B] * 8000)
        
        globals, sram, cram, charset = c64_converter.convert_mcbm_to_mccm(bg, bytes(screen_ram), bytes(color_ram), bytes(bitmap), colors=[0, 1, 2, 5], tolerance=0)
        
        self.assertEqual(len(globals), 3)
        self.assertEqual(len(sram), 1000)
        self.assertEqual(len(cram), 1000)
        self.assertEqual(len(charset), 2048)
        
        # Since input is uniform, should produce 1 unique char (plus padding 0s)
        # Actually, charset might have garbage if we didn't zero-init properly? 
        # No, we append. So first 8 bytes = pattern. Rest 0.
        
        # Verify unique chars count
        # Scan sram, should all be 4 (since 0-3 are solid chars for the 4 colors)
        for b in sram:
            self.assertEqual(b, 2)
            
        # Verify color RAM consistency
        # Since we used colors (0,2,5), check if they are in result.
        
        used_cols = [0, 2, 5]
        # Check globals + cram
        # One color in cram[0]
        c11 = cram[0] & 0x07
        all_res_cols = set(globals)
        all_res_cols.add(c11)
        
        # 0s in globals might be unused padding, so filter 0
        # Wait, 0 is Black, a valid color.
        # We know specific colors were in input.
        for c in used_cols:
            self.assertIn(c, all_res_cols)

    def test_too_many_colors(self):
        # 5 distinct colors
        bg = 0
        # Block 1 uses 1,2,3
        # Block 2 uses 0(bg),4
        # Total 0,1,2,3,4 = 5 colors.
        
        screen_ram = bytearray(1000)
        color_ram = bytearray(1000)
        bitmap = bytearray(8000)
        
        # Block 0: colors 1,2,3
        screen_ram[0] = (1 << 4) | 2
        color_ram[0] = 3
        # use all bits
        bitmap[0] = 0b01101100 # 01, 10, 11, 00(is bg=0) -- wait, we want to force usage
        # Actually, simply putting them in RAM doesn't mean they are used if pixels don't reference them.
        # The converter checks *used* pixels.
        # So we need pixels for 1,2,3 in block 0.
        bitmap[0] = 0b01101100 # uses 1, 2, 3, 0.
        
        # Block 1: color 4
        screen_ram[1] = (4 << 4) | 4 # just put 4s
        color_ram[1] = 4
        bitmap[8] = 0b01010101 # all 01 -> color 4.
        
        # Set BG=0.
        # Colors found: 0, 1, 2, 3, 4.
        
        with self.assertRaises(ValueError):
            c64_converter.convert_mcbm_to_mccm(bg, bytes(screen_ram), bytes(color_ram), bytes(bitmap), colors=[0, 1, 2, 3], tolerance=0)

    def test_visual_verification(self):
        # Render a gradient-like thing and convert it
        
        # r8u1 -> some rotation
        # Colors: Black, White, Red, Cyan (0, 1, 2, 3) -> 4 colors.
        colors = [0, 1, 2, 3]
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8u1"), 2)
        
        # Render Original
        img_mcbm = c64_graphics.C64Screen.render_mcbm(bg, sram, cram, bmp)
        img_mcbm.save(os.path.join(self.out_dir, "test_converter_original.png"))
        # Convert
        g_cols, c_sram, c_cram, charset = c64_converter.convert_mcbm_to_mccm(bg, sram, cram, bmp, colors=colors, tolerance=0)
        
        # Render Converted
        img_mccm = c64_graphics.C64Screen.render_mccm(g_cols, c_sram, c_cram, charset)
        img_mccm.save(os.path.join(self.out_dir, "test_converter_mccm.png"))
        
        # Compare pixels (exact match expected since < 256 unique chars likely for simple pattern)
        # Note: generate_frame_mcbm might produce many unique tiles if pattern is complex.
        # r8u1 might have many shifts.
        # But limited check: assert images same size
        self.assertEqual(img_mcbm.size, img_mccm.size)
        
        # Check a few pixels? Or just rely on visual file generation as requested.

    def test_count_pixel_diff(self):
        # Helper to make 8 bytes from single row byte
        def make_char(b):
            return bytes([b] * 8)
            
        # 1. Exact match
        # 00 hex = 00000000 binary = pixels 0,0,0,0
        # diff should be 0
        c1 = make_char(0x00)
        c2 = make_char(0x00)
        self.assertEqual(c64_converter.count_pixel_diff(c1, c2), (0, 0))
        
        # 2. Single pixel diff
        # 0x00 = 00 00 00 00
        # 0x40 = 01 00 00 00 (Pixel 0 is 1)
        # diff should be 1 (for first row) * 8 = 8 total? 
        # Wait, if I make_char repeats it 8 times.
        # Let's simple use single byte array
        c1 = bytes([0x00] * 8)
        c2 = bytes([0x00] * 8)
        # Change only first byte of c2
        c2_mutable = bytearray(c2)
        c2_mutable[0] = 0x40 # 01 00 00 00
        c2 = bytes(c2_mutable)
        
        # Diff is 1, ground_diffs is 0 (neither is ground)
        self.assertEqual(c64_converter.count_pixel_diff(c1, c2), (1, 0))
        
        # 3. Ground bits
        # If ground_bits = 0 (pixels 00)
        # c1 has pixels 00. c2 has pixels 01.
        # Diff is Space(0) vs Color1(1).
        # Since one side is Ground(0), strict ground check should fail (return 9999).
        
        ground = 0 # 00
        # Diff is 1. If ground involved, ground_diffs=1.
        # c1 has 00 (Ground). c2 has 01 (Color 1).
        
        diff, g_diff = c64_converter.count_pixel_diff(c1, c2, ground_bits=ground)
        self.assertEqual(diff, 1)
        self.assertEqual(g_diff, 64) # Border pixel diff returns 64
        
        # 3b. Non-border ground diff
        # Change row 4, pixel pair 1 (not border)
        c1_mid = bytes([0x00] * 8)
        c2_mid_mutable = bytearray([0x00] * 8)
        c2_mid_mutable[4] = 0x10 # 00 01 00 00 (row 4, k=1)
        c2_mid = bytes(c2_mid_mutable)
        diff, g_diff = c64_converter.count_pixel_diff(c1_mid, c2_mid, ground_bits=ground)
        self.assertEqual(diff, 1)
        self.assertEqual(g_diff, 1) # Non-border diff increments normally
        
        # 4. Non-ground diff with ground bits set
        # Compare 01 (0x40) vs 10 (0x80)
        # Neither is 00 (Ground).
        # Diff should be normal.
        c3 = bytearray(8)
        c3[0] = 0x40 # 01 00 00 00
        c4 = bytearray(8)
        c4[0] = 0x80 # 10 00 00 00
        
        # Diff is 1 pixel (c3 and c4 are bytearrays, cast to bytes)
        diff, g_diff = c64_converter.count_pixel_diff(bytes(c3), bytes(c4), ground_bits=ground)
        self.assertEqual(diff, 1)
        
        # 5. Diff involves ground, but ground_bits NOT set
        diff, g_diff = c64_converter.count_pixel_diff(c1, c2, ground_bits=None)
        self.assertEqual(diff, 1)

if __name__ == '__main__':
    unittest.main()
