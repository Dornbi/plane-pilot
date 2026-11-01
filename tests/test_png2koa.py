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
    parse_slot_pin,
    oscar64_lzo_size,
    lzo_encode,
    lzo_decode,
    _copy_cell_pattern,
)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# ppilot's and fpilot's, at the same layout (view.cc, `make panel`).
PANEL_KOAS = [os.path.join(REPO_ROOT, "c64o", name)
              for name in ("ppanel.koa", "fpanel.koa")]
GFX_CC = os.path.join(REPO_ROOT, "c64o", "gfx.cc")
VIEW_CC = os.path.join(REPO_ROOT, "c64o", "view.cc")
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


def lzo_outputs_from_makefile() -> dict[str, list[tuple[str, int, int]]]:
    """
    {panel: [(.lzo file name, .koa offset, length), ...]}: the --lzo-out files
    each panel rule writes, paired in order with the --embed ranges in
    PANEL_FLAGS, as png2koa.py pairs them.
    """
    with open(MAKEFILE) as f:
        makefile = f.read()
    flags = re.search(r"^PANEL_FLAGS\s*=(.*?)(?<!\\)\n", makefile, re.M | re.S)
    ranges = [(int(off), int(length))
              for off, length in re.findall(r"--embed\s+(\d+):(\d+)", flags.group(1))]
    out = {}
    for panel in ("ppanel", "fpanel"):
        rule = re.search(rf"^{panel}:\n((?:\t.*\n)+)", makefile, re.M).group(1)
        names = re.findall(r"--lzo-out\s+c64o/(\S+)", rule)
        assert len(names) == len(ranges), f"{panel}: {names} for {ranges}"
        out[panel] = [(name, off, length) for name, (off, length) in zip(names, ranges)]
    return out


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


class TestPinSlots(unittest.TestCase):
    """
    --pin-slots fixes a cell's whole slot layout, for cells whose bitmap bytes
    the program copies into other cells: the copy carries bit pairs, so both
    ends have to give them the same colors.
    """

    LAYOUT = (1, 12, 15)  # white, grey, light grey: the panel's glyph layout
    CELL = (12, 22)

    def _image_with_glyph(self, path: str) -> None:
        """A white and light grey "0" with no grey in it, like the panel's."""
        img = Image.new("RGB", (320, 200), (0, 0, 0))
        px = img.load()
        row, col = self.CELL
        for y in range(row * 8 + 1, row * 8 + 7):
            for x in (col * 8, col * 8 + 1):
                px[x, y] = PEPTO_PALETTE[1]
            for x in (col * 8 + 2, col * 8 + 3):
                px[x, y] = PEPTO_PALETTE[15]
        img.save(path)

    def test_pin_sets_the_layout_and_changes_no_pixel(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            src = os.path.join(tmp_dir, "glyph.png")
            self._image_with_glyph(src)
            row, col = self.CELL
            idx = row * 40 + col
            outs = {}
            for name, slot_pins in (("free", None), ("pinned", {idx: self.LAYOUT})):
                koa = os.path.join(tmp_dir, f"{name}.koa")
                png = os.path.join(tmp_dir, f"{name}.png")
                convert_png_to_koala(input_path=src, koa_output_path=koa,
                                     png_output_path=png, bg_color=0,
                                     slot_pins=slot_pins)
                outs[name] = (read_koa(koa), png)

            # Unpinned, light grey takes the lowest free slot, which is grey's.
            (_, screen, _, _), _ = outs["free"]
            self.assertEqual(screen[idx] & 0x0F, 15)

            (_, screen, color, _), png = outs["pinned"]
            self.assertEqual((screen[idx] >> 4, screen[idx] & 0x0F, color[idx] & 0x0F),
                             self.LAYOUT)
            self.assertEqual(Image.open(outs["free"][1]).convert("RGB").tobytes(),
                             Image.open(png).convert("RGB").tobytes())

    def test_a_color_outside_the_layout_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            src = os.path.join(tmp_dir, "glyph.png")
            self._image_with_glyph(src)
            row, col = self.CELL
            with self.assertRaises(ValueError):
                convert_png_to_koala(
                    input_path=src,
                    koa_output_path=os.path.join(tmp_dir, "out.koa"),
                    bg_color=0,
                    slot_pins={row * 40 + col: (1, 12, 11)},  # no light grey
                )

    def test_parse_slot_pin(self):
        self.assertEqual(parse_slot_pin("1,12,15@18,5"), [(18 * 40 + 5, (1, 12, 15))])
        self.assertEqual(parse_slot_pin("1,12,15@18,5-8"),
                         [(18 * 40 + c, (1, 12, 15)) for c in range(5, 9)])
        for bad in ("1,12@0,0", "1,1,15@0,0", "1,12,16@0,0", "1,12,15@25,0",
                    "1,12,15@0,8-5", "1,12,15@0,39-40"):
            with self.assertRaises(Exception, msg=bad):
                parse_slot_pin(bad)


class TestLzo(unittest.TestCase):
    """lzo_encode() against oscar_expand_lzo(), as lzo_decode() mirrors it."""

    SAMPLES = [
        b"",
        b"x",
        b"ab",
        bytes(300),                                  # one long run
        bytes(range(256)) * 3,                       # matches 256 back only
        b"abcd" * 50 + bytes(range(200)),            # runs into literals
        bytes((i * 37) & 0xFF for i in range(1000)),
        bytes((i * i) >> 3 & 0x0F for i in range(2000)),
    ]

    @staticmethod
    def _tokens(stream: bytes):
        """(kind, length, offset) for each command, as the decoder sees them."""
        sp = 0
        while stream[sp]:
            cmd = stream[sp]
            if cmd & 0x80:
                yield "match", cmd & 0x7F, stream[sp + 1]
                sp += 2
            else:
                yield "literal", cmd, None
                sp += 1 + cmd
        assert sp == len(stream) - 1, "bytes after the terminating 0"

    def test_round_trip(self):
        for data in self.SAMPLES:
            self.assertEqual(lzo_decode(lzo_encode(data)), data)

    def test_never_bigger_than_oscar64(self):
        for data in self.SAMPLES:
            self.assertLessEqual(len(lzo_encode(data)), oscar64_lzo_size(data))

    def test_commands_stay_within_the_decoder(self):
        # oscar_expand_lzo() counts in a char and reads the offset from one
        # byte: a match needs offset 1..255, and no command may be 0 long.
        for data in self.SAMPLES:
            for kind, length, offset in self._tokens(lzo_encode(data)):
                self.assertTrue(1 <= length <= 127, (kind, length))
                if kind == "match":
                    self.assertTrue(1 <= offset <= 255, offset)


class TestCopyCellPattern(unittest.TestCase):
    """The slot optimizer's other freedom: a spare slot holding the background."""

    BG = 0

    def test_a_spare_slot_lets_background_copy_a_pattern(self):
        # White on black in slot 1, slots 2 and 3 unused. The target draws a
        # 10 where this cell has background: slot 2 becomes black.
        bits = [1, 0, 0, 0] * 8
        target = [1, 2, 0, 0] * 8
        cand = _copy_cell_pattern(1, 5, 6, bits, target, self.BG, None)
        self.assertIsNotNone(cand)
        l1, l2, l3, new_bits = cand
        self.assertEqual(new_bits, target)
        self.assertEqual((l1, l2), (1, self.BG))
        colors = (self.BG, l1, l2, l3)
        self.assertEqual([colors[b] for b in new_bits],
                         [(self.BG, 1, 5, 6)[b] for b in bits])

    def test_a_drawn_pixel_cannot_become_background(self):
        bits = [1, 2, 0, 0] * 8
        target = [1, 0, 0, 0] * 8
        self.assertIsNone(_copy_cell_pattern(1, 5, 6, bits, target, self.BG, None))

    def test_two_colors_cannot_share_a_slot(self):
        bits = [1, 2, 0, 0] * 8
        target = [1, 1, 0, 0] * 8
        self.assertIsNone(_copy_cell_pattern(1, 5, 6, bits, target, self.BG, None))

    def test_a_slot_cannot_be_both_background_and_a_color(self):
        bits = [1, 0, 0, 0] * 8
        target = [1, 1, 0, 0] * 8
        self.assertIsNone(_copy_cell_pattern(1, 5, 6, bits, target, self.BG, None))


class TestShippedPanel(unittest.TestCase):
    """
    The checked-in panel image against the code that lights it. This is the
    test that would have caught the nav lamps going dead: the encoder had moved
    light red from one screen RAM nibble to the other, and gfx.cc was writing
    the nibble it used to be in.
    """

    def test_lamp_cells_carry_the_lamp_color_in_color_ram(self):
        for path in PANEL_KOAS:
            name = os.path.basename(path)
            _, screen, color, _ = read_koa(path)
            for row, col in lamp_cells_from_gfx_cc():
                idx = row * 40 + col
                self.assertEqual(color[idx] & 0x0F, LAMP_COLOR,
                                 f"{name}: lamp cell at row {row} col {col} does "
                                 f"not have the lamp color in color RAM - gfx.cc "
                                 f"would switch color {color[idx] & 0x0F} instead")
                self.assertNotEqual(screen[idx] >> 4, LAMP_COLOR, name)
                self.assertNotEqual(screen[idx] & 0x0F, LAMP_COLOR, name)

    def test_makefile_pins_exactly_the_lamp_cells(self):
        # Two hand-written lists of the same four cells, one in C and one in
        # make. Regenerating the panel with the wrong list is silent: the image
        # looks right and the lamps do not work.
        self.assertEqual(
            pinned_cells_from_makefile(),
            {cell: LAMP_COLOR for cell in lamp_cells_from_gfx_cc()},
            "PANEL_FLAGS in the Makefile and the lamps in gfx.cc disagree")

    # Where gfx.cc copies glyphs from and to: the ammo digits and the heading
    # strips in the off-screen head of the bitmap, the HDG window and fpilot's
    # AMMO window on the panel. A copy moves bit pairs, so all of them have to
    # give the pairs the same colors (--pin-slots in the root Makefile).
    GLYPH_CELLS = ([(12, c) for c in range(22, 40)] + [(13, c) for c in range(40)]
                   + [(17, c) for c in range(17, 23)])
    FPILOT_GLYPH_CELLS = [(18, c) for c in range(6, 9)]

    def test_glyph_cells_share_one_slot_layout(self):
        for path in PANEL_KOAS:
            name = os.path.basename(path)
            _, screen, color, _ = read_koa(path)
            cells = self.GLYPH_CELLS + (
                self.FPILOT_GLYPH_CELLS if name == "fpanel.koa" else [])
            for row, col in cells:
                idx = row * 40 + col
                self.assertEqual(
                    (screen[idx] >> 4, screen[idx] & 0x0F, color[idx] & 0x0F),
                    TestPinSlots.LAYOUT,
                    f"{name}: row {row} col {col} has another slot layout, so "
                    "glyphs copied to or from it would change color")

    def test_lzo_streams_expand_to_the_koa(self):
        # view.cc embeds the .lzo files as they are, and gfx.cc embeds part of
        # the .koa too: both have to come from the same `make panel`.
        for panel, outputs in lzo_outputs_from_makefile().items():
            with open(os.path.join(REPO_ROOT, "c64o", f"{panel}.koa"), "rb") as f:
                koa = f.read()
            for name, off, length in outputs:
                with open(os.path.join(REPO_ROOT, "c64o", name), "rb") as f:
                    stream = f.read()
                self.assertEqual(lzo_decode(stream), koa[off:off + length],
                                 f"{name} does not expand to {panel}.koa bytes "
                                 f"{off}-{off + length - 1}; rerun `make {panel}`")

    def test_view_cc_embeds_the_lzo_streams(self):
        with open(VIEW_CC) as f:
            embedded = set(re.findall(r'#embed\s+"(\w+\.lzo)"', f.read()))
        written = {name for outputs in lzo_outputs_from_makefile().values()
                   for name, _, _ in outputs}
        self.assertEqual(embedded, written)


if __name__ == "__main__":
    unittest.main()
