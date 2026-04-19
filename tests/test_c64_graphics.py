import unittest
import os
import sys

from lib import c64_graphics
from lib import c64_colors

# Add lib to path
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

class TestC64Renderer(unittest.TestCase):
    def setUp(self):
        self.test_output_dir = os.path.join(os.path.dirname(__file__), 'test_frames')
        if not os.path.exists(self.test_output_dir):
            os.makedirs(self.test_output_dir)

    def test_mcbm_checkerboard(self):
        """Test MCBM rendering with a checkerboard pattern."""
        # Setup:
        # Background: Black (0)
        # Screen RAM: 01=White(1), 10=Red(2) -> byte 0x12
        # Color RAM: 11=Cyan(3)
        # Bitmap: alternating pixels
        
        bg_color = c64_colors.BLACK
        screen_ram = bytearray([0x12] * 1000)
        color_ram = bytearray([c64_colors.CYAN] * 1000)
        
        # Bitmap pattern to show all 4 colors in a block
        # 00, 01, 10, 11 -> 0, 1, 2, 3
        # 00010010 = 0x12
        # 10111011 = 0xBB
        bitmap = bytearray([
            0b00011011, # Row 0: 00(BG), 01(SR-hi), 10(SR-lo), 11(CR) -> Black, White, Red, Cyan
            0b11100100, # Row 1: Cyan, Red, White, Black
        ] * 4000) # repeat 4 times for 8 rows, then * 1000 chars

        img = c64_graphics.C64Screen.render_mcbm(bg_color, bytes(screen_ram), bytes(color_ram), bytes(bitmap))
        output_path = os.path.join(self.test_output_dir, 'test_mcbm.png')
        img.save(output_path)
        print(f"Saved MCBM test to {output_path}")

        # Verify pixel colors at (0,0), (2,0), (4,0), (6,0)
        # Remember they are double wide, so check x=0, x=2, x=4, x=6
        pixels = img.load()
        self.assertIsNotNone(pixels)
        expected_colors = [
            c64_colors.PALETTE_RGB[c64_colors.BLACK], # 00
            c64_colors.PALETTE_RGB[c64_colors.WHITE], # 01
            c64_colors.PALETTE_RGB[c64_colors.RED],   # 10
            c64_colors.PALETTE_RGB[c64_colors.CYAN]   # 11
        ]
        
        for i in range(4):
            self.assertEqual(pixels[i*2, 0], expected_colors[i], f"Pixel pair {i} mismatch")

    def test_mccm_bars(self):
        """Test MCCM rendering with simple bars."""
        # Globals: 00=Green, 01=Blue, 10=Yellow
        globals = [c64_colors.GREEN, c64_colors.BLUE, c64_colors.YELLOW]
        
        # Charset: Char 0 is all 00, Char 1 is all 01, Char 2 is all 10, Char 3 is all 11
        charset = bytearray(2048)
        # Char 0 (0x00) -> 00 00 00 00 (already 0)
        # Char 1 (0x55) -> 01 01 01 01 (0x55)
        for i in range(8): charset[1*8 + i] = 0x55
        # Char 2 (0xAA) -> 10 10 10 10 (0xAA)
        for i in range(8): charset[2*8 + i] = 0xAA
        # Char 3 (0xFF) -> 11 11 11 11 (0xFF)
        for i in range(8): charset[3*8 + i] = 0xFF
        
        screen_ram = bytearray(1000)
        color_ram = bytearray(1000)
        
        # Set first 4 characters to 0, 1, 2, 3
        for i in range(4):
            screen_ram[i] = i
            color_ram[i] = c64_colors.YELLOW # Set specific color to Yellow (7, valid for MCCM)

        img = c64_graphics.C64Screen.render_mccm(globals, bytes(screen_ram), bytes(color_ram), bytes(charset))
        output_path = os.path.join(self.test_output_dir, 'test_mccm.png')
        img.save(output_path)
        print(f"Saved MCCM test to {output_path}")

        pixels = img.load()
        self.assertIsNotNone(pixels)
        # Char 0: Should be Global 1 (Green)
        self.assertEqual(pixels[0,0], c64_colors.PALETTE_RGB[c64_colors.GREEN])
        # Char 1: Should be Global 2 (Blue)
        self.assertEqual(pixels[8,0], c64_colors.PALETTE_RGB[c64_colors.BLUE])
        # Char 2: Should be Global 3 (Yellow)
        self.assertEqual(pixels[16,0], c64_colors.PALETTE_RGB[c64_colors.YELLOW])
        # Char 3: Should be Global 4 (Orange - from Color RAM)
        self.assertEqual(pixels[24,0], c64_colors.PALETTE_RGB[c64_colors.YELLOW])

if __name__ == '__main__':
    unittest.main()
