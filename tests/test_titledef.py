#!/usr/bin/env python3
"""The title screen aircraft: that the art still fits four multicolour sprites,
and that the generator still reproduces the bitmaps that are checked in.

Three things can silently go wrong here, and all three are invisible until the
menu is on screen for the two seconds a flyby lasts.

The crop can slip. It is a hand-written rectangle into a 320 x 200 concept
image and nothing in the PNG says where the second aeroplane starts. One pixel
left or right and every multicolour pixel straddles two of the artist's, which
the generator turns into a build failure only when the two happen to be
different colours - so this checks the alignment directly, on the whole crop.

The palette can grow. A multicolour sprite has room for three colours and
transparent; a fourth in the art has nowhere to go. The generator raises on
one, and this says the same thing about the file that is actually embedded.

And the block order can invert. The four bitmaps are laid out in reading order
because title.cc hands block N to hardware sprite N and places sprite N by
arithmetic rather than by table. Reassembling the four into one 48 x 42 picture
and comparing it against the concept art is the only check that the order and
the bit packing agree with each other.
"""

import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import generate_sprites
from lib import titledef

ROWS = generate_sprites.KSPRITE_ROWS  # 21
MC_COLS = generate_sprites.KSPRITE_MC_COLS  # 12
BASE = titledef.BITMAP_BASE


def concept_art():
    img, _w, _h = generate_sprites._read_concept_png(
        os.path.join(REPO_ROOT, "gfx", generate_sprites.KTITLE_PNG)
    )
    return img


class TestCropFitsTheSprites(unittest.TestCase):
    def test_the_crop_is_exactly_two_by_two_sprites(self):
        # 48 x 42 screen pixels. Not a size to round to: a multicolour sprite is
        # 24 screen pixels wide however few pixels that is, and 21 lines tall.
        self.assertEqual(titledef.COLS * generate_sprites.KSPRITE_COLS, 48)
        self.assertEqual(titledef.ROWS * ROWS, 42)
        self.assertEqual(titledef.NUM_BITMAPS, titledef.COLS * titledef.ROWS)

    def test_the_art_is_on_the_multicolour_grid(self):
        # Every pair of art pixels under one multicolour pixel is the same
        # colour, or one of the two is transparent - a single-pixel hole in the
        # drawing, which _title_pair_color() fills. Two different colours would
        # mean the crop had slipped off the grid by one.
        img = concept_art()
        mixed = 0
        for y in range(
            generate_sprites.KTITLE_CROP_Y, generate_sprites.KTITLE_CROP_Y + 42
        ):
            for x in range(
                generate_sprites.KTITLE_CROP_X,
                generate_sprites.KTITLE_CROP_X + 48,
                2,
            ):
                a, b = img[y][x], img[y][x + 1]
                if a == b:
                    continue
                self.assertIn(
                    0, (a, b),
                    "two different colours in one multicolour pixel at "
                    "(%d, %d): %d and %d - the crop is off the grid"
                    % (x, y, a, b),
                )
                mixed += 1
        # As drawn today there is exactly one, at (126, 27). Pinned so that a
        # redraw which introduces a spray of them has to say so here.
        self.assertEqual(mixed, 1)

    def test_three_colours_and_transparent(self):
        # A multicolour sprite has four states and one of them is transparent,
        # so a fourth colour in the art has nowhere to go. lib/titledef.py
        # writes the bit pair as a digit and transparent as a dot.
        used = {v for grid in titledef.PATTERNS for row in grid for v in row}
        self.assertEqual(used, {".", "1", "2", "3"})

    def test_the_colours_are_the_ones_the_c64_is_told_about(self):
        # The bit pair a colour lands on is what decides which VIC register has
        # to hold it, and title.cc reads all three straight out of titledef.h.
        self.assertEqual(
            generate_sprites.KTITLE_BITPAIR[titledef.COLOR_MC0], 1
        )
        self.assertEqual(
            generate_sprites.KTITLE_BITPAIR[titledef.COLOR_MAIN], 2
        )
        self.assertEqual(
            generate_sprites.KTITLE_BITPAIR[titledef.COLOR_MC1], 3
        )


class TestReadingOrder(unittest.TestCase):
    def test_reassembling_the_four_blocks_gives_back_the_art(self):
        img = concept_art()
        bitpair = generate_sprites.KTITLE_BITPAIR

        for row in range(titledef.ROWS):
            for col in range(titledef.COLS):
                grid = titledef.PATTERNS[row * titledef.COLS + col]
                self.assertEqual(len(grid), ROWS)
                for sy in range(ROWS):
                    line = grid[sy]
                    self.assertEqual(len(line), MC_COLS)
                    iy = generate_sprites.KTITLE_CROP_Y + row * ROWS + sy
                    for sx in range(MC_COLS):
                        ix = (
                            generate_sprites.KTITLE_CROP_X
                            + col * generate_sprites.KSPRITE_COLS
                            + sx * 2
                        )
                        want = bitpair[
                            generate_sprites._title_pair_color(img, ix, iy)
                        ]
                        got = 0 if line[sx] == "." else int(line[sx])
                        self.assertEqual(
                            got, want,
                            "block %d, row %d, column %d"
                            % (row * titledef.COLS + col, sy, sx),
                        )

    def test_bitmap_indices_are_consecutive_from_the_base(self):
        # title.cc writes sprite pointer i as kTitleSpriteBlock + i and places
        # sprite i at (i & 1, i >> 1) of the block. Neither has a table to get
        # out of step with, so long as the four stay consecutive and in order.
        self.assertEqual(
            [m["bitmap_idx"] for m in titledef.META],
            [BASE, BASE + 1, BASE + 2, BASE + 3],
        )

    def test_the_base_block_is_the_page_mem_h_places(self):
        # ($CF00 - $C000) / 64. Spelled out rather than imported, so that this
        # fails if either end moves; c64o/title.cc has the same assertion in C.
        self.assertEqual(BASE, (0xCF00 - 0xC000) // 64)


class TestGeneratorReproducesCheckedInData(unittest.TestCase):
    def test_bitmaps_match_titledef_bin(self):
        # Everything is computed in memory - the generator's main() writes
        # files into the repo and a test has no business doing that.
        data, _meta, _bits = generate_sprites.generate_title_sprites(BASE)
        produced = b"".join(bytes(b) for b in data)

        with open(os.path.join(REPO_ROOT, "c64o", "titledef.bin"), "rb") as f:
            checked_in = f.read()

        self.assertEqual(len(produced), 4 * 64)
        self.assertEqual(
            produced,
            checked_in,
            "the generator no longer reproduces c64o/titledef.bin - if that is "
            "deliberate, rerun `make sprites` and commit the result",
        )


if __name__ == "__main__":
    unittest.main()
