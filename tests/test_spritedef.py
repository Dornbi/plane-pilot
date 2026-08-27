#!/usr/bin/env python3
"""The cloud bitmaps' dither phase, and that the generator still reproduces the
bitmaps that are checked in.

docs/clouds.md §4 is what this guards. Clouds are a white-and-transparent
checkerboard and §2.5 deliberately overlaps three of them, so the phase of that
checkerboard has to be the same for every cloud sprite on screen. In phase, the
rear sprite's lit pixels sit under the front one's and the overlap stays a
half-tone; out of phase, the rear fills the front's holes exactly and the group
turns into a solid white lump with a hard edge where the overlap stops.

The failure is silent on this side of the build - the generator would emit
perfectly reasonable-looking bitmaps - and unmistakable on screen. It is also
the kind of thing that breaks by accident: redraw the concept art at 1-pixel
resolution and the OR in the extraction turns the checkerboard solid; move a
crop rectangle by one pixel and a single rung inverts.
"""

import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import generate_sprites
from lib import spritedef

ROWS = generate_sprites.KSPRITE_ROWS  # 21


def lit(grid):
    """The set pixels of a pattern, as {(column, row)}."""
    return {
        (c, r)
        for r, row in enumerate(grid)
        for c, ch in enumerate(row)
        if ch == "#"
    }


def bbox(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


class TestDitherPhase(unittest.TestCase):
    """Every cloud pixel sits on one global checkerboard (§4.2)."""

    def test_single_sprite_rungs(self):
        # Four, not five: the ladder's smallest rung is unreachable and its
        # block is the orientation indicator's, which is not a cloud and is not
        # on the lattice. TestGeneratorReproducesCheckedInData covers its shape.
        self.assertEqual(len(spritedef.PATTERNS_CLOUD1), 4)
        for i, grid in enumerate(spritedef.PATTERNS_CLOUD1):
            label = spritedef.META_CLOUD1[i]["label"]
            for c, r in lit(grid):
                self.assertTrue(
                    generate_sprites.on_dither_lattice(c, r),
                    "%s: set pixel off the lattice at column %d, row %d"
                    % (label, c, r),
                )

    def test_stacked_rungs_run_unbroken_across_the_seam(self):
        # The lower block is placed 21 raster lines below the upper one, and 21
        # is odd, so its bitmap has to carry the opposite phase for the 42-row
        # stack to read as one checkerboard. Assembling the stack and checking
        # it as a single 42-row bitmap is the same statement, said in the form
        # the eye would see.
        for i in range(0, len(spritedef.PATTERNS_CLOUD2), 2):
            label = spritedef.META_CLOUD2[i // 2]["label"]
            top = spritedef.PATTERNS_CLOUD2[i]
            bot = spritedef.PATTERNS_CLOUD2[i + 1]
            stack = list(top) + list(bot)
            self.assertEqual(len(stack), 2 * ROWS)
            for c, r in lit(stack):
                self.assertTrue(
                    generate_sprites.on_dither_lattice(c, r),
                    "%s: set pixel off the lattice at column %d, row %d of the "
                    "assembled 42-row stack" % (label, c, r),
                )

    def test_every_rung_shares_one_phase(self):
        # The runtime snap in sprites_stack_add() has no per-bitmap correction
        # term, so it is only correct if all fifteen blocks agree on which
        # sublattice they live on. Stated directly rather than inferred from the
        # two cases above.
        phases = set()
        for grid in spritedef.PATTERNS_CLOUD1:
            phases |= {(c + r) % 2 for c, r in lit(grid)}
        for i in range(0, len(spritedef.PATTERNS_CLOUD2), 2):
            stack = list(spritedef.PATTERNS_CLOUD2[i]) + list(
                spritedef.PATTERNS_CLOUD2[i + 1]
            )
            phases |= {(c + r) % 2 for c, r in lit(stack)}
        self.assertEqual(phases, {0}, "the fifteen blocks do not share a phase")


class TestOverlap(unittest.TestCase):
    """What the phase actually buys, measured on the real bitmaps (§4.1)."""

    def test_misaligned_offsets_interlock_exactly(self):
        # This is the whole argument for §4, and it is exact rather than
        # statistical: every lit pixel has an even coordinate sum, so shifting a
        # blob by an odd (dx + dy) puts every one of its pixels on the odd
        # sublattice - the holes. The two lit sets are then disjoint, which is
        # precisely "the rear sprite fills the front one's holes".
        blob = lit(spritedef.PATTERNS_CLOUD1[-1])  # the largest single-sprite rung
        checked = 0
        for dx in range(-24, 25):
            for dy in range(-ROWS, ROWS + 1):
                shifted = {(c + dx, r + dy) for c, r in blob}
                if (dx + dy) % 2:
                    self.assertEqual(
                        blob & shifted,
                        set(),
                        "offset (%d, %d) should interlock, not overlap"
                        % (dx, dy),
                    )
                    checked += 1
        self.assertGreater(checked, 500)

    def test_aligned_offsets_overlap(self):
        # The converse: an even offset keeps both copies on the same sublattice,
        # so wherever the shapes overlap the pixels do too, and the overlap
        # stays a half-tone instead of filling in.
        blob = lit(spritedef.PATTERNS_CLOUD1[-1])
        for dx, dy in ((2, 0), (4, 0), (0, 2), (3, 1), (-3, 1), (2, 2)):
            shifted = {(c + dx, r + dy) for c, r in blob}
            self.assertTrue(
                blob & shifted,
                "offset (%d, %d) shares a sublattice and should overlap"
                % (dx, dy),
            )

    def test_coverage_in_the_overlap(self):
        # The number docs/clouds.md §4.1 quotes. Bounds are loose - the point is
        # the gap between the two, not the exact figures - but if the art is
        # ever redrawn at a different density this is what notices.
        blob = lit(spritedef.PATTERNS_CLOUD1[-1])

        def coverage(dx, dy):
            shifted = {(c + dx, r + dy) for c, r in blob}
            ax0, ay0, ax1, ay1 = bbox(blob)
            bx0, by0, bx1, by1 = bbox(shifted)
            x0, y0 = max(ax0, bx0), max(ay0, by0)
            x1, y1 = min(ax1, bx1), min(ay1, by1)
            area = (x1 - x0 + 1) * (y1 - y0 + 1)
            both = blob | shifted
            n = sum(
                1
                for x in range(x0, x1 + 1)
                for y in range(y0, y1 + 1)
                if (x, y) in both
            )
            return n / area

        aligned = coverage(4, 0)
        misaligned = coverage(3, 0)
        self.assertLess(aligned, 0.60, "aligned overlap should stay a half-tone")
        self.assertGreater(
            misaligned, 0.70, "misaligned overlap should fill in"
        )
        self.assertGreater(misaligned - aligned, 0.25)


class TestPivots(unittest.TestCase):
    def test_each_ladder_shares_one_pivot(self):
        # Not a dither constraint: the lattice is anchored to the sprite's top
        # left, which sprites_stack_add() snaps after subtracting the pivot, so
        # the pivot does not enter it. What this guards is the size ladder - a
        # rung whose pivot disagreed with its neighbours would make a cloud jump
        # sideways when it stepped size instead of just growing.
        for label, metas in (
            ("cloud1", spritedef.META_CLOUD1),
            ("cloud2", spritedef.META_CLOUD2),
        ):
            pivots = {(m["pivot_x"], m["pivot_y"]) for m in metas}
            self.assertEqual(
                len(pivots), 1, "%s rungs disagree on the pivot: %s"
                % (label, sorted(pivots))
            )


class TestGeneratorReproducesCheckedInData(unittest.TestCase):
    """Applying the dither in the generator was meant to change nothing."""

    def test_bitmaps_match_spritedef_bin(self):
        # docs/clouds.md §4.3: moving the checkerboard out of the concept art
        # and into the generator is a no-op on the art as it stands, and that is
        # what makes it safe to land on its own. Checked against the file the
        # C64 build actually embeds.
        #
        # Everything is computed in memory - the generator's main() writes four
        # files into the repo and a test has no business doing that.
        angles_tot = 32
        cloud_base = 80
        base_offset = 96

        orient_data, _, _ = generate_sprites.generate_orient_sprite(cloud_base)
        c1_data, _, _, c2_data, _, _ = generate_sprites.generate_cloud_sprites(
            cloud_base + 1
        )
        sun_data, _, _ = generate_sprites.generate_sun_sprite(base_offset - 1)
        arm14, _, _ = generate_sprites.generate_arm_set(
            14, base_offset, angles_tot
        )
        arm10, _, _ = generate_sprites.generate_arm_set(
            10, base_offset + angles_tot // 2, angles_tot
        )

        produced = b"".join(
            bytes(b)
            for b in [orient_data] + c1_data + c2_data + [sun_data] + arm14
            + arm10
        )
        with open(os.path.join(REPO_ROOT, "c64o", "spritedef.bin"), "rb") as f:
            checked_in = f.read()

        self.assertEqual(len(produced), len(checked_in))
        self.assertEqual(
            produced,
            checked_in,
            "the generator no longer reproduces c64o/spritedef.bin - if that is "
            "deliberate, rerun `make sprites` and commit the result",
        )

    def test_metadata_matches_lib_spritedef(self):
        # spritedef.bin is bitmaps only, so the byte-for-byte test above
        # structurally cannot see the pivots, the sizes or the block indices -
        # those reach the C64 through spritedef.cc and the Python preview
        # through lib/spritedef.py. Without this, a changed pivot regenerates
        # cleanly and silently disagrees with what is committed.
        c1_meta = generate_sprites.generate_cloud_sprites(81)[1]
        c2_meta = generate_sprites.generate_cloud_sprites(81)[4]

        self.assertEqual(len(c1_meta), len(spritedef.META_CLOUD1))
        for produced, committed in zip(c1_meta, spritedef.META_CLOUD1):
            for key in ("width", "height", "bitmap_idx", "pivot_x", "pivot_y"):
                self.assertEqual(
                    produced[key], committed[key],
                    "cloud1 %s disagrees on %s" % (produced["label"], key)
                )
        self.assertEqual(len(c2_meta), len(spritedef.META_CLOUD2))
        for produced, committed in zip(c2_meta, spritedef.META_CLOUD2):
            for key in ("width", "height", "top_bitmap_idx", "bot_bitmap_idx",
                        "pivot_x", "pivot_y"):
                self.assertEqual(
                    produced[key], committed[key],
                    "cloud2 %s disagrees on %s" % (produced["label"], key)
                )

    def test_flat_rung_table_agrees_with_the_two_it_replaces(self):
        # docs/clouds.md §3.4. clouds.cc indexes one flat row per rung rather
        # than branching between sprite_cloud1_meta_t and sprite_cloud2_meta_t,
        # because that branch miscompiles under oscar64 - it folded a constant
        # from one arm into two fields of the other and dropped a third
        # assignment, which put a blank sprite under every near cloud. The flat
        # table has no branch to get wrong, but it does have a copy to drift,
        # so check it against the two tables it is derived from.
        import re

        path = os.path.join(REPO_ROOT, "c64o", "spritedef.cc")
        with open(path) as f:
            src = f.read()
        body = re.search(
            r"kSpriteDefCloudRung\[[^\]]*\]\s*=\s*\{(.*?)\n\};",
            src, re.S,
        )
        self.assertIsNotNone(body, "kSpriteDefCloudRung is missing from "
                                   "spritedef.cc - rerun `make sprites`")
        rows = []
        for line in body.group(1).splitlines():
            line = line.split("//")[0].strip()
            if not line.startswith("{"):
                continue
            rows.append([int(v.strip(), 0)
                         for v in line.strip("{},").split(",")])

        # Row 0 is the rung clouds.cc can never select - a collapsed group
        # draws one rung *larger* than its own, so the smallest row it can index
        # is 1 (docs/clouds.md §3.5). The row is kept so that a rung number is
        # still a row number, and it names the smallest real cloud rather than
        # the orientation indicator that took its bitmap slot, so that even a
        # rung 0 that somehow got indexed would draw a cloud.
        expected = [
            [
                spritedef.META_CLOUD1[0]["bitmap_idx"], 0xFF,
                spritedef.META_CLOUD1[0]["pivot_x"],
                spritedef.META_CLOUD1[0]["pivot_y"],
            ]
        ] + [
            [m["bitmap_idx"], 0xFF, m["pivot_x"], m["pivot_y"]]
            for m in spritedef.META_CLOUD1
        ] + [
            [m["top_bitmap_idx"], m["bot_bitmap_idx"], m["pivot_x"],
             m["pivot_y"]]
            for m in spritedef.META_CLOUD2
        ]
        self.assertEqual(rows, expected)

        # The property clouds.cc leans on, spelled out: the sentinel appears on
        # exactly the single-sprite rungs, because sprites_stack_add() reads it
        # as "this entry is one hardware sprite, not two".
        for i, row in enumerate(rows):
            self.assertEqual(
                row[1] == 0xFF, i < 1 + len(spritedef.META_CLOUD1),
                "rung %d disagrees with the ladder about being one sprite" % i,
            )
        # And no row of the ladder names the orientation indicator's block: it
        # is a mark, not weather, and a retuned kCloudRungCollapsed must not be
        # able to put it in the sky.
        for i, row in enumerate(rows):
            self.assertNotIn(
                spritedef.META_ORIENT["bitmap_idx"], row[:2],
                "rung %d names the orientation indicator's bitmap" % i,
            )

    def test_the_phase_survives_art_with_no_checkerboard_in_it(self):
        # The point of moving the dither into the generator, stated as a test.
        # Before this, the extraction ORed two adjacent source pixels and took
        # whatever phase the art happened to have - so a concept redrawn as a
        # plain silhouette, or at 1-pixel resolution, would have come out solid
        # white. Feed it exactly that and the output is still a checkerboard on
        # the right lattice.
        original = generate_sprites._read_concept_png
        generate_sprites._read_concept_png = lambda path: (
            [[1] * 300 for _ in range(140)],
            300,
            140,
        )
        try:
            _, _, c1_bits, _, _, c2_bits = (
                generate_sprites.generate_cloud_sprites(80)
            )
        finally:
            generate_sprites._read_concept_png = original

        total = 0
        for grid in c1_bits:
            for r, row in enumerate(grid):
                for c, bit in enumerate(row):
                    if bit:
                        total += 1
                        self.assertTrue(
                            generate_sprites.on_dither_lattice(c, r)
                        )
        for top, bot in c2_bits:
            for r, row in enumerate(top):
                for c, bit in enumerate(row):
                    if bit:
                        total += 1
                        self.assertTrue(
                            generate_sprites.on_dither_lattice(c, r)
                        )
            for r, row in enumerate(bot):
                for c, bit in enumerate(row):
                    if bit:
                        total += 1
                        self.assertTrue(
                            generate_sprites.on_dither_lattice(c, r, ROWS)
                        )
        # Solid input, so every rung is filled to its own extent: about half of
        # each block's pixels, which is the dither doing the work.
        self.assertGreater(total, 1000)

    def test_the_mark_and_the_clouds_are_the_first_fifteen(self):
        # The sprite pointers 80..94 in docs/clouds.md §6.1 are only right if
        # these bitmaps come first in the file, which mem_init() expands to
        # $D400 in order. Fifteen blocks still, but the first is no longer a
        # cloud: it is the orientation indicator, in the block the ladder's
        # unreachable rung 0 used to hold.
        orient_data, orient_meta, _ = generate_sprites.generate_orient_sprite(80)
        c1_data, c1_meta, _, c2_data, c2_meta, _ = (
            generate_sprites.generate_cloud_sprites(81)
        )
        self.assertEqual(1 + len(c1_data) + len(c2_data), 15)
        self.assertEqual(orient_meta["bitmap_idx"], 80)
        self.assertEqual(c1_meta[0]["bitmap_idx"], 81)
        self.assertEqual(c2_meta[-1]["bot_bitmap_idx"], 94)
        self.assertEqual(len(orient_data), 64)

    def test_the_mark_is_two_wings_and_a_gap(self):
        # xxxxxxxx--------xxxxxxxx on the pivot row, and nothing anywhere else:
        # a bar the horizon can be read against, with a gap to centre by. The
        # dither lattice does not apply - it is not a cloud - which is exactly
        # why it needs a shape test of its own.
        _, meta, grid = generate_sprites.generate_orient_sprite(80)
        wing = generate_sprites.KSPRITE_ORIENT_WING
        cols = generate_sprites.KSPRITE_COLS
        for r, row in enumerate(grid):
            for c, bit in enumerate(row):
                on_the_bar = r == meta["pivot_y"] and (
                    c < wing or c >= cols - wing
                )
                self.assertEqual(
                    bool(bit), on_the_bar,
                    "pixel (%d, %d) should be %s"
                    % (c, r, "set" if on_the_bar else "clear"),
                )
        # Symmetric about the pivot column, or the mark does not sit where
        # sprites.cc puts it.
        self.assertEqual(2 * wing + (cols - 2 * wing), cols)
        self.assertEqual(meta["pivot_x"], cols // 2)


class TestTheGuardItself(unittest.TestCase):
    """A check that cannot fail is not a check."""

    def test_assert_dither_phase_rejects_an_off_lattice_pixel(self):
        grid = [[0] * generate_sprites.KSPRITE_COLS for _ in range(ROWS)]
        grid[0][1] = 1  # (c + r) odd
        with self.assertRaises(SystemExit):
            generate_sprites.assert_dither_phase("synthetic", grid)

    def test_assert_dither_phase_accepts_the_lower_block_phase(self):
        grid = [[0] * generate_sprites.KSPRITE_COLS for _ in range(ROWS)]
        grid[0][1] = 1
        # The same pixel is correct for a lower block, where the 21-line seam
        # has already flipped the phase.
        generate_sprites.assert_dither_phase("synthetic", grid, ROWS)
        with self.assertRaises(SystemExit):
            grid[0][1] = 0
            grid[0][2] = 1
            generate_sprites.assert_dither_phase("synthetic", grid, ROWS)

    def test_assert_pivots_consistent_rejects_a_disagreeing_rung(self):
        metas = [
            {"pivot_x": 12, "pivot_y": 10},
            {"pivot_x": 12, "pivot_y": 11},
        ]
        with self.assertRaises(SystemExit):
            generate_sprites.assert_pivots_consistent("synthetic", metas)


if __name__ == "__main__":
    unittest.main()
