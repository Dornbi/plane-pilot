#!/usr/bin/env python3
"""
Unit tests for tools/png2koa.py converter tool.
"""

from __future__ import annotations
import os
import re
import tempfile
import unittest
from PIL import Image

from tools.png2koa import (
    PEPTO_PALETTE,
    get_closest_pepto_color,
    downsample_to_multicolor_grid,
    convert_png_to_koala,
    parse_pin,
)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL_KOA = os.path.join(REPO_ROOT, "c64o", "panel.koa")
GFX_CC = os.path.join(REPO_ROOT, "c64o", "gfx.cc")
MAKEFILE = os.path.join(REPO_ROOT, "Makefile")

# The indicator lamp color. gfx.cc switches the lamps by storing it into color
# RAM, so `make panel` pins it there in those four cells with --pin-color-ram;
# see the lamp functions in gfx.cc and PANEL_FLAGS in the root Makefile.
LAMP_COLOR = 10  # light red


def pinned_cells_from_makefile() -> dict[tuple[int, int], int]:
    """The --pin-color-ram cells in PANEL_FLAGS, as {(row, col): color}."""
    with open(MAKEFILE) as f:
        makefile = f.read()
    pins = {}
    for spec in re.findall(r"--pin-color-ram\s+(\d+@\d+,\d+)", makefile):
        idx, color = parse_pin(spec)
        pins[(idx // 40, idx % 40)] = color
    return pins


def read_koa(path: str) -> tuple[bytes, bytes, bytes, int]:
    """Splits a .koa into (bitmap, screen RAM, color RAM, background color)."""
    with open(path, "rb") as f:
        data = f.read()
    assert len(data) == 10003, f"{path}: expected 10003 bytes, got {len(data)}"
    return data[2:8002], data[8002:9002], data[9002:10002], data[10002]


def lamp_cells_from_gfx_cc() -> list[tuple[int, int]]:
    """
    The panel cells gfx.cc pokes, read out of the source rather than repeated
    here: the point of the test is that the code and the image agree, and a
    second copy of the coordinates could drift from both.

    Recognises the pointer definitions

        static uint8_t *const kFlapPtr = kColorRam + 16 * kScreenWidth + 13;

    and every `_set_lamp(kFlapPtr, ...)` or `_set_lamp(kFlapPtr + 1, ...)` that
    uses one.
    """
    with open(GFX_CC) as f:
        source = f.read()
    bases = {
        name: (int(row), int(col))
        for name, row, col in re.findall(
            r"(\w+)\s*=\s*kColorRam \+ (\d+) \* kScreenWidth \+ (\d+)\s*;", source)
    }
    cells = []
    for name, offset in re.findall(r"_set_lamp\((\w+)(?:\s*\+\s*(\d+))?\s*,", source):
        assert name in bases, f"_set_lamp() on unknown pointer {name}"
        row, col = bases[name]
        cells.append((row, col + int(offset or 0)))
    assert cells, "found no lamps in gfx.cc"
    return cells


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


class TestPinColorRam(unittest.TestCase):
    """
    A pinned cell has to keep its color in color RAM. Which of the two screen
    RAM colors a cell's colors land in is the encoder's choice and it re-labels
    them for compression, so anything the C64 side pokes at run time has to be
    somewhere that choice cannot reach - color RAM is a whole byte per cell and
    is the only such place. Only the named cells are constrained; the same color
    elsewhere in the image is placed for compression as usual.
    """

    LAMP_CELL = (16, 10)  # row, col of the lamp _image_with_lamp() draws

    def _image_with_lamp(self, path: str) -> None:
        """A gray dial with a light red lamp on it, in one 4x8 multicolor cell."""
        img = Image.new("RGB", (320, 200), (0, 0, 0))
        px = img.load()
        for y in range(120, 160):
            for x in range(40, 200):
                px[x, y] = PEPTO_PALETTE[15]
            for x in range(200, 240):
                px[x, y] = PEPTO_PALETTE[11]
        # Smaller than a cell, so the lamp shares its cell with the dial.
        for y in range(130, 134):
            for x in range(82, 86):
                px[x, y] = PEPTO_PALETTE[LAMP_COLOR]
        img.save(path)

    def test_pin_moves_the_color_and_changes_no_pixel(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            src = os.path.join(tmp_dir, "lamp.png")
            self._image_with_lamp(src)

            free_koa = os.path.join(tmp_dir, "free.koa")
            free_png = os.path.join(tmp_dir, "free.png")
            pinned_koa = os.path.join(tmp_dir, "pinned.koa")
            pinned_png = os.path.join(tmp_dir, "pinned.png")
            row, col = self.LAMP_CELL
            convert_png_to_koala(input_path=src, koa_output_path=free_koa,
                                 png_output_path=free_png, bg_color=0)
            convert_png_to_koala(input_path=src, koa_output_path=pinned_koa,
                                 png_output_path=pinned_png, bg_color=0,
                                 pins={row * 40 + col: LAMP_COLOR})

            # Unpinned, the encoder puts the lamp in a screen RAM slot, which is
            # what makes the pin worth having.
            _, free_screen, free_color, _ = read_koa(free_koa)
            idx = row * 40 + col
            self.assertNotEqual(free_color[idx] & 0x0F, LAMP_COLOR)

            _, screen, color, _ = read_koa(pinned_koa)
            self.assertEqual(color[idx] & 0x0F, LAMP_COLOR)
            self.assertNotEqual(screen[idx] >> 4, LAMP_COLOR)
            self.assertNotEqual(screen[idx] & 0x0F, LAMP_COLOR)

            # Pinning only relabels slots, so the picture must be untouched.
            self.assertEqual(Image.open(free_png).convert("RGB").tobytes(),
                             Image.open(pinned_png).convert("RGB").tobytes())

    def test_pinning_a_cell_that_does_not_use_the_color_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            src = os.path.join(tmp_dir, "lamp.png")
            self._image_with_lamp(src)
            row, col = self.LAMP_CELL
            with self.assertRaises(ValueError):
                convert_png_to_koala(
                    input_path=src,
                    koa_output_path=os.path.join(tmp_dir, "out.koa"),
                    bg_color=0,
                    # One cell to the right: dial, no lamp. Pinning there would
                    # give the C64 side a cell with nothing to light up.
                    pins={row * 40 + col + 1: LAMP_COLOR},
                )

    def test_parse_pin(self):
        self.assertEqual(parse_pin("10@15,21"), (15 * 40 + 21, 10))
        for bad in ("10", "10@15", "16@0,0", "10@25,0", "10@0,40", "@1,2"):
            with self.assertRaises(Exception, msg=bad):
                parse_pin(bad)


class TestShippedPanel(unittest.TestCase):
    """
    The checked-in panel image against the code that lights it. This is the
    test that would have caught the nav lamps going dead: the encoder had moved
    light red from one screen RAM nibble to the other, and gfx.cc was writing
    the nibble it used to be in.
    """

    def test_lamp_cells_carry_the_lamp_color_in_color_ram(self):
        _, screen, color, _ = read_koa(PANEL_KOA)
        for row, col in lamp_cells_from_gfx_cc():
            idx = row * 40 + col
            self.assertEqual(color[idx] & 0x0F, LAMP_COLOR,
                             f"lamp cell at row {row} col {col} does not have the "
                             f"lamp color in color RAM - gfx.cc would switch "
                             f"color {color[idx] & 0x0F} instead")
            self.assertNotEqual(screen[idx] >> 4, LAMP_COLOR)
            self.assertNotEqual(screen[idx] & 0x0F, LAMP_COLOR)

    def test_makefile_pins_exactly_the_lamp_cells(self):
        # Two hand-written lists of the same four cells, one in C and one in
        # make. Regenerating the panel with the wrong list is silent: the image
        # looks right and the lamps do not work.
        self.assertEqual(
            pinned_cells_from_makefile(),
            {cell: LAMP_COLOR for cell in lamp_cells_from_gfx_cc()},
            "PANEL_FLAGS in the Makefile and the lamps in gfx.cc disagree")


if __name__ == "__main__":
    unittest.main()
