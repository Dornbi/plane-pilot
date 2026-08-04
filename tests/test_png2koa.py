#!/usr/bin/env python3
"""
Unit tests for png2koa.py converter tool.
"""

from __future__ import annotations
import os
import tempfile
import unittest
from PIL import Image

from png2koa import (
    PEPTO_PALETTE,
    get_closest_pepto_color,
    downsample_to_multicolor_grid,
    convert_png_to_koala,
)


class TestPng2Koa(unittest.TestCase):

    def test_pepto_palette_properties(self):
        """Verify Pepto palette has 16 RGB color tuples."""
        self.assertEqual(len(PEPTO_PALETTE), 16)
        for color in PEPTO_PALETTE:
            self.assertEqual(len(color), 3)
            for val in color:
                self.assertTrue(0 <= val <= 255)

    def test_get_closest_pepto_color(self):
        """Test exact RGB matching for Pepto palette colors."""
        # Exact black
        self.assertEqual(get_closest_pepto_color((0, 0, 0)), 0)
        # Exact white
        self.assertEqual(get_closest_pepto_color((255, 255, 255)), 1)
        # Exact red
        self.assertEqual(get_closest_pepto_color((136, 0, 0)), 2)

    def test_conversion_and_file_formats(self):
        """Test converting a test PNG image to Koala Painter and preview PNG."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            input_png_path = os.path.join(tmp_dir, "input_test.png")
            output_koa_path = os.path.join(tmp_dir, "output.koa")
            output_png_path = os.path.join(tmp_dir, "output.png")

            # Create a 320x200 synthetic image with color blocks
            img = Image.new("RGB", (320, 200), (0, 0, 0))
            # Draw blue block in top-left
            for y in range(50):
                for x in range(80):
                    img.putpixel((x, y), (0, 0, 170))
            # Draw yellow block in bottom-right
            for y in range(150, 200):
                for x in range(240, 320):
                    img.putpixel((x, y), (238, 238, 119))

            img.save(input_png_path)

            # Convert to Koala
            convert_png_to_koala(
                input_path=input_png_path,
                koa_output_path=output_koa_path,
                png_output_path=output_png_path,
                bg_color=0,
            )

            # Verify .koa file existence, size, and header
            self.assertTrue(os.path.exists(output_koa_path))
            file_size = os.path.getsize(output_koa_path)
            self.assertEqual(file_size, 10003)

            with open(output_koa_path, "rb") as f:
                koa_data = f.read()

            # Check load address $6000 (\x00\x60)
            load_addr = koa_data[0] | (koa_data[1] << 8)
            self.assertEqual(load_addr, 0x6000)

            # Check background color byte at index 10002
            self.assertEqual(koa_data[10002], 0)

            # Verify preview PNG image existence and properties
            self.assertTrue(os.path.exists(output_png_path))
            preview_img = Image.open(output_png_path)
            self.assertEqual(preview_img.size, (320, 200))

            # Verify preview PNG colors are strictly from Pepto palette
            preview_pixels = preview_img.convert("RGB").load()
            pepto_set = set(PEPTO_PALETTE)
            for y in range(200):
                for x in range(320):
                    self.assertIn(preview_pixels[x, y], pepto_set)

    def test_cell_color_constraints(self):
        """Verify each 8x8 cell in preview PNG contains at most 4 unique Pepto colors."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            input_png_path = os.path.join(tmp_dir, "input_rainbow.png")
            output_koa_path = os.path.join(tmp_dir, "output_rainbow.koa")
            output_png_path = os.path.join(tmp_dir, "output_rainbow.png")

            # Create an image with complex gradient to test constraint enforcement
            img = Image.new("RGB", (320, 200))
            pixels = img.load()
            for y in range(200):
                for x in range(320):
                    pixels[x, y] = (x % 256, y % 256, (x + y) % 256)
            img.save(input_png_path)

            convert_png_to_koala(
                input_path=input_png_path,
                koa_output_path=output_koa_path,
                png_output_path=output_png_path,
            )

            preview_img = Image.open(output_png_path).convert("RGB")
            preview_pixels = preview_img.load()

            # Check all 40x25 character matrix cells
            for cy in range(25):
                for cx in range(40):
                    cell_colors = set()
                    for ry in range(8):
                        for rx in range(8):
                            cell_colors.add(preview_pixels[cx * 8 + rx, cy * 8 + ry])
                    # Max 4 unique colors per 8x8 cell in VIC-II multicolor mode
                    self.assertLessEqual(len(cell_colors), 4)


if __name__ == "__main__":
    unittest.main()
