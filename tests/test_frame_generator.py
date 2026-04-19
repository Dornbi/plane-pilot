import unittest
import os
import sys

from lib import frame_generator
from lib import roll_angle

# Add project root to path
sys.path.append(os.path.dirname(os.path.abspath(__file__)) + '/..')

class TestFrameGenerator(unittest.TestCase):

        
    def test_get_bayer_offset_2x2(self):
        # BAYER_2X2 = [[0, 2], [3, 1]]
        # (0,0) -> 0 -> scaled to val*10 - 15 = -15
        val = frame_generator.get_bayer_offset_x2(0, 0, frame_generator.BAYER_2X2)
        self.assertEqual(val, -15)
        
        # (1,0) -> 2 -> scaled to 2*10 - 15 = 5
        val = frame_generator.get_bayer_offset_x2(1, 0, frame_generator.BAYER_2X2)
        self.assertEqual(val, 5)

    def test_generation_runs(self):
        # Just ensure it doesn't crash and returns correct sizes
        colors = [5, 3, 14, 6]
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8u1"), 2)
        
        self.assertEqual(len(sram), 1000)
        self.assertEqual(len(cram), 1000)
        self.assertEqual(len(bmp), 8000)

    def test_generation_runs_2x2(self):
        # Ensure 2x2 mode runs
        colors = [5, 3, 14, 6]
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8u1"), 2, dither_type="bayer2x2")
        
        self.assertEqual(len(sram), 1000)
        self.assertEqual(len(cram), 1000)
        self.assertEqual(len(bmp), 8000)

    def test_generation_runs_no_dither(self):
        # Ensure no dither mode runs
        colors = [5, 3, 14, 6]
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8u1"), 2, dither_type="none")
        
        self.assertEqual(len(sram), 1000)
        self.assertEqual(len(cram), 1000)
        self.assertEqual(len(bmp), 8000)

    def test_soft_horizon(self):
        # Ensure it accepts the soft_ground_horizon parameter
        colors = [5, 3, 14, 6]
        # Run with soft horizon enabled
        frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8u1"), 2, soft_ground_horizon=True)
        # Run with soft horizon disabled (default is False now)
        frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8u1"), 2, soft_ground_horizon=False)

    def test_custom_center(self):
        # Ensure it accepts custom center coordinates
        colors = [5, 3, 14, 6]
        # Run with shifted center
        frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8u1"), 2, center_x=100, center_y=50)
        # Assuming no assertion on output correctness here, just crash checking

    def _get_pixel_color(self, x, y, bg, sram, cram, bmp):
        """Helper to sample pixel color from C64 memory buffers."""
        cx = x // 8
        cy = y // 8
        char_idx = cy * 40 + cx
        local_x = x % 8
        local_y = y % 8
        
        sr = sram[char_idx]
        cr = cram[char_idx]
        c00 = bg
        c01 = (sr >> 4) & 0x0F
        c10 = sr & 0x0F
        c11 = cr & 0x0F
        
        # Get bitmap bits
        # local_x in [0..7]. Pairs: 0,1->0; 2,3->1; 4,5->2; 6,7->3
        pair_idx = local_x // 2
        bm_byte = bmp[char_idx * 8 + local_y]
        shift = 6 - (pair_idx * 2)
        bits = (bm_byte >> shift) & 0x03
        
        if bits == 0: return c00
        if bits == 1: return c01
        if bits == 2: return c10
        if bits == 3: return c11
        return 0

    def test_horizon_r8_l8(self):
        # r8 and l8 should produce a flat horizontal horizon at centerline (y=100 default)
        # Sky/Grad above (y<100), Ground below (y>=100) or vice versa depending on roll
        colors = [5, 3, 14, 6] # Ground=5, Sky=6
        ground = 5
        sky = 6 # or grad1 etc.
        
        # r8: Normal (0, -1). Pointing Up.
        # dist = ry * -1 = -(y - 100 + 1) = 99 - y.
        # Ground if dist < 0 => 99 < y => y > 99 => y >= 100.
        # So y=0..99 is Sky/Grad (dist >= 0), y=100..199 is Ground.
        
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("r8"), 2, soft_ground_horizon=False)
        
        # Check r8
        col_99 = self._get_pixel_color(160, 99, bg, sram, cram, bmp)
        col_100 = self._get_pixel_color(160, 100, bg, sram, cram, bmp)
        
        # 5 is ground.
        self.assertNotEqual(col_99, ground, "y=99 should be sky/grad")
        self.assertEqual(col_100, ground, "y=100 should be ground")
        
        # l8: Inverted horizon. Ground at Top, Sky at Bottom.
        # Normal (0, 1) (Down).
        # ry = y - 100 + 0.5 = y - 99.5.
        # dist = ry * 1.
        # Ground if dist < 0 => y < 99.5. 
        # So y=0..99 is Ground. y=100..199 is Sky.
        
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("l8"), 2, soft_ground_horizon=False)
        col_99 = self._get_pixel_color(160, 99, bg, sram, cram, bmp)
        col_100 = self._get_pixel_color(160, 100, bg, sram, cram, bmp)
        
        self.assertEqual(col_99, ground, "l8: y=99 should be ground")
        self.assertNotEqual(col_100, ground, "l8: y=100 should be sky/grad")

    def test_horizon_u8_d8(self):
        # CENTER_X = 160.
        # u8: Normal (-1, 0). Pointing Left.
        # dist = rx * -1 = -((x + 1) - 160) = 159 - x.
        # Ground if dist < 0 => 159 < x => x > 159 => x >= 160.
        # So x=0..159 is Sky, x=160..319 is Ground.
        
        colors = [5, 3, 14, 6]
        ground = 5
        
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("u8"), 2, soft_ground_horizon=False)
        
        # Check x=159 vs 160 at y=100
        c_159 = self._get_pixel_color(158, 100, bg, sram, cram, bmp) # x=158 (pixel pair 158/159)
        c_160 = self._get_pixel_color(160, 100, bg, sram, cram, bmp) # x=160
        
        self.assertNotEqual(c_159, ground, "x=158/159 should be sky")
        self.assertEqual(c_160, ground, "x=160/161 should be ground")
        
        # d8: Normal (1, 0). Pointing Right.
        # dist = rx * 1 = x + 1 - 160.
        # Ground if dist < 0 => x + 1 < 160 => x < 159.
        # x=158: dist = -1. Ground.
        # x=160: dist = 1. Sky.
        
        # x=160: dist = 1. Sky.
        
        bg, sram, cram, bmp = frame_generator.generate_frame_mcbm(colors, roll_angle.RollAngle.from_string("d8"), 2, soft_ground_horizon=False)
        c_158 = self._get_pixel_color(158, 100, bg, sram, cram, bmp)
        c_160 = self._get_pixel_color(160, 100, bg, sram, cram, bmp)
        
        self.assertEqual(c_158, ground, "x=158 should be ground")
        self.assertNotEqual(c_160, ground, "x=160 should be sky")


if __name__ == '__main__':
    unittest.main()
