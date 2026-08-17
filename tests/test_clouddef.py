#!/usr/bin/env python3
"""The generated cloud placement constants.

docs/clouds.md §7: the tables are emitted three times over - c64o/clouddef.h,
c64o/clouddef.cc and lib/clouddef.py - so that the C64 build, the preview and
these tests all see one set of numbers. Most of what follows checks properties
of the layout; the last class checks the three copies still agree, which is the
only thing that makes the other two copies trustworthy.
"""

import os
import re
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import generate_clouds  # noqa: E402
from lib import clouddef  # noqa: E402

H_PATH = os.path.join(REPO_ROOT, "c64o", "clouddef.h")
CC_PATH = os.path.join(REPO_ROOT, "c64o", "clouddef.cc")


def _c_scalars(path):
    """`static const T kName = value;` from a header, as {name: int}."""
    src = open(path).read()
    return {
        m.group(1): int(m.group(2), 0)
        for m in re.finditer(
            r"static const \w+ (k\w+)\s*=\s*(-?(?:0[xX][0-9a-fA-F]+|\d+));", src
        )
    }


def _c_array(path, name):
    """The integers of a `const T kName[...] = { ... };` initialiser.

    Comments are stripped first: the generated tables carry `// pattern 0`
    labels inside the braces, and those digits are not data.
    """
    src = re.sub(r"//[^\n]*", "", open(path).read())
    body = src.split(name, 1)[1].split("{", 1)[1]
    depth, out = 1, ""
    for ch in body:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                break
        out += ch
    return [int(t) for t in re.findall(r"-?\d+", out)]


class TestTheLadder(unittest.TestCase):
    def test_depths_match_the_formula(self):
        # §3.2: an object of size S at depth D spans 128 * S / D world pixels,
        # so the rung boundaries are that inverted. Computed rather than typed,
        # and checked here so a hand edit to clouddef.cc cannot survive.
        for i, d in enumerate(clouddef.RUNG_DEPTH):
            expected = round(
                clouddef.PROJECTION_SCALE * clouddef.BLOB_U / (3 + 2 * i)
            )
            self.assertEqual(d, expected, "rung %d" % i)

    def test_strictly_descending(self):
        # Rung selection walks down the table until the depth fits, so a
        # non-monotone table would skip a rung.
        for i in range(1, clouddef.RUNG_COUNT):
            self.assertLess(clouddef.RUNG_DEPTH[i], clouddef.RUNG_DEPTH[i - 1])

    def test_the_cull_is_the_scans_guaranteed_radius(self):
        # docs/clouds.md §2.2. This is the assertion that fails if someone
        # retunes the cell size: a 5 x 5 scan of N-unit cells guarantees only
        # 2N of reach, and a group found beyond that flickers as the eye crosses
        # a cell boundary rather than as it moves.
        guaranteed = clouddef.SCAN_RADIUS * clouddef.CELL_U
        self.assertEqual(
            clouddef.CULL_U, guaranteed,
            "cull %d != the %d x %d scan's guaranteed radius %d - see "
            "docs/clouds.md §2.2"
            % (clouddef.CULL_U, 2 * clouddef.SCAN_RADIUS + 1,
               2 * clouddef.SCAN_RADIUS + 1, guaranteed),
        )
        # And §3.1: the smallest rung has to land exactly there, so a cloud
        # fades in by stepping up the ladder instead of popping.
        self.assertEqual(clouddef.RUNG_DEPTH[0], clouddef.CULL_U)

    def test_rung_for_depth(self):
        self.assertIsNone(clouddef.rung_for_depth(clouddef.CULL_U + 1))
        self.assertEqual(clouddef.rung_for_depth(clouddef.CULL_U), 0)
        self.assertEqual(clouddef.rung_for_depth(1), clouddef.RUNG_COUNT - 1)
        previous = clouddef.RUNG_COUNT
        for depth in range(1, clouddef.CULL_U + 1):
            r = clouddef.rung_for_depth(depth)
            self.assertIsNotNone(r)
            self.assertTrue(0 <= r < clouddef.RUNG_COUNT)
            # Monotone: further away is never a bigger rung.
            self.assertLessEqual(r, previous)
            previous = r


class TestTheHash(unittest.TestCase):
    def test_it_wraps_with_the_world(self):
        # world.cc indexes kWorldMap with the cell coordinate masked, so the
        # map repeats every 16 tiles in X and 32 in Y. The clouds have to repeat
        # on the same period or they would drift off the terrain.
        for cx in range(-3, 12):
            for cy in range(-3, 20):
                a = clouddef.group_at(cx, cy)
                b = clouddef.group_at(cx + clouddef.CELLS_X,
                                      cy + clouddef.CELLS_Y)
                self.assertEqual(a is None, b is None)
                if a is None:
                    continue
                self.assertEqual(a["pattern"], b["pattern"])
                # Same offset inside the cell; the absolute position differs by
                # exactly one world.
                self.assertEqual(b["x"] - a["x"],
                                 clouddef.CELLS_X * clouddef.CELL_U)
                self.assertEqual(b["y"] - a["y"],
                                 clouddef.CELLS_Y * clouddef.CELL_U)

    def test_density_is_in_the_band_the_generator_accepted(self):
        present = sum(
            1
            for cx in range(clouddef.CELLS_X)
            for cy in range(clouddef.CELLS_Y)
            if clouddef.group_at(cx, cy)
        )
        cells = clouddef.CELLS_X * clouddef.CELLS_Y
        nominal = clouddef.GATE_LIMIT / 32.0
        self.assertGreaterEqual(present / cells, nominal * 0.75)
        self.assertLessEqual(present / cells, nominal * 1.25)

    def test_the_jitter_is_not_degenerate(self):
        # §2.3 worried that presence and jitter might correlate, and this used
        # to test for it directly - every group in the same corner of its cell.
        # That was a *proxy* for "the layout looks even", and once evenness was
        # measured for real the proxy started rejecting the best layouts to
        # enforce a statistic nobody looks at. See
        # test_the_layout_is_evenly_spaced_not_merely_sparse, which is the real
        # test now.
        #
        # What is left here is the one degenerate case evenness cannot see: if
        # every group sat at the same offset in its cell the positions would
        # form a lattice, which scores *well* on evenness and looks
        # manufactured.
        quadrants = [0, 0, 0, 0]
        for cx in range(clouddef.CELLS_X):
            for cy in range(clouddef.CELLS_Y):
                g = clouddef.group_at(cx, cy)
                if g is None:
                    continue
                jx = (g["x"] - cx * clouddef.CELL_U) >> clouddef.JITTER_SHIFT
                jy = (g["y"] - cy * clouddef.CELL_U) >> clouddef.JITTER_SHIFT
                quadrants[(1 if jx >= 8 else 0) + (2 if jy >= 8 else 0)] += 1
        total = sum(quadrants)
        self.assertGreaterEqual(
            sum(1 for q in quadrants if q), 2,
            "every group sits in one corner of its cell - the layout would be a "
            "lattice",
        )
        self.assertLessEqual(max(quadrants), 0.75 * total)

    def test_groups_are_balanced_over_the_world(self):
        # The first version of this only counted how many rows and columns had
        # *any* group, which a layout with everything bunched on one side passes
        # comfortably - and one did, visibly, in the first preview. These are
        # the counts, not the coverage.
        points, columns, rows, quadrants, _, _ = generate_clouds._layout(
            (clouddef.HASH_X, clouddef.HASH_Y, clouddef.HASH_A, clouddef.HASH_B)
        )
        n = len(points)
        # The thresholds are spelled out here rather than imported from
        # generate_clouds. A test that reads its expected values out of the code
        # it is testing cannot fail: weaken the generator, regenerate, and the
        # test weakens with it. These are the numbers the layout is required to
        # meet, and changing them has to be a deliberate edit in two places.
        # 37 groups over 16 columns and 8 rows is an even share of 2.3 and 4.6;
        # these caps are a shade under twice that. They were 3 and 4 when the
        # layout carried 19 groups - if the density moves again these move with
        # it, deliberately, in both places.
        self.assertLessEqual(max(columns), 4, "a cell column hoards groups")
        self.assertLessEqual(max(rows), 8, "a cell row hoards groups")
        self.assertTrue(
            0.40 <= sum(columns[:clouddef.CELLS_Y // 2]) / n <= 0.60,
            "left/right split is %d/%d"
            % (sum(columns[:clouddef.CELLS_Y // 2]),
               sum(columns[clouddef.CELLS_Y // 2:])),
        )
        self.assertTrue(
            0.40 <= sum(rows[:clouddef.CELLS_X // 2]) / n <= 0.60,
            "top/bottom split is %d/%d"
            % (sum(rows[:clouddef.CELLS_X // 2]),
               sum(rows[clouddef.CELLS_X // 2:])),
        )
        self.assertGreaterEqual(min(quadrants), 0.15 * n,
                                "quadrants %s" % (quadrants,))
        self.assertLessEqual(max(quadrants), 0.35 * n,
                             "quadrants %s" % (quadrants,))

    def test_the_layout_is_evenly_spaced_not_merely_sparse(self):
        # Sparse and clumpy are different things, and a random scatter is
        # clumpy: it puts pairs almost on top of each other and leaves holes
        # between them. world.cc reaches for Mitchell's best-candidate for the
        # same reason on the terrain dots. Random scores about 0.25 here.
        points, _, _, _, _, _ = generate_clouds._layout(
            (clouddef.HASH_X, clouddef.HASH_Y, clouddef.HASH_A, clouddef.HASH_B)
        )
        separation = generate_clouds.min_separation(points)
        covering = generate_clouds.covering_radius(points)
        self.assertAlmostEqual(separation, clouddef.MIN_SEPARATION, places=0)
        self.assertAlmostEqual(covering, clouddef.COVERING_RADIUS, places=0)
        self.assertGreater(
            separation / covering, 0.45,
            "the layout is clumpier than the search accepted - rerun "
            "`make clouds`",
        )

    def test_the_jitter_spans_the_cell(self):
        self.assertEqual(16 << clouddef.JITTER_SHIFT, clouddef.CELL_U)
        self.assertEqual(1 << clouddef.CELL_SHIFT, clouddef.CELL_U)


class TestTheGroupLayout(unittest.TestCase):
    def test_shape(self):
        self.assertEqual(len(clouddef.GROUP_OFFSET), clouddef.PATTERN_COUNT)
        for i, pattern in enumerate(clouddef.GROUP_OFFSET):
            self.assertEqual(len(pattern), clouddef.BLOBS_PER_GROUP)
            self.assertEqual(len(set(map(tuple, pattern))),
                             clouddef.BLOBS_PER_GROUP,
                             "pattern %d stacks two blobs in one place" % i)
            for blob in pattern:
                self.assertEqual(len(blob), 3)
                for coeff in blob:
                    # §2.5: coefficients count halves of the offset basis, and
                    # the runtime has the halves precomputed, so anything past
                    # +/-2 would stop being an add.
                    self.assertLessEqual(abs(coeff), 2, "pattern %d" % i)

    def test_every_pattern_is_reachable(self):
        used = {clouddef.group_at(cx, cy)["pattern"]
                for cx in range(clouddef.CELLS_X)
                for cy in range(clouddef.CELLS_Y)
                if clouddef.group_at(cx, cy)}
        self.assertEqual(used, set(range(clouddef.PATTERN_COUNT)))

    def test_the_blobs_actually_overlap(self):
        # The whole point of a group (§2.5): the blobs are about half a diameter
        # apart, so they overlap from any angle instead of reading as three
        # separate clouds. Offsets are in halves of kCloudOffsetU, itself half a
        # blob, so a coefficient of 2 is one kCloudOffsetU = half a diameter.
        half = clouddef.OFFSET_U / 2.0
        for i, pattern in enumerate(clouddef.GROUP_OFFSET):
            for a in range(len(pattern)):
                nearest = min(
                    ((pattern[a][0] - pattern[b][0]) * half) ** 2
                    + ((pattern[a][1] - pattern[b][1]) * half) ** 2
                    + ((pattern[a][2] - pattern[b][2]) * half) ** 2
                    for b in range(len(pattern)) if b != a
                ) ** 0.5
                self.assertLess(
                    nearest, clouddef.BLOB_U,
                    "pattern %d blob %d is more than a diameter from its "
                    "nearest neighbour, so the group would not overlap"
                    % (i, a),
                )


class TestTheThreeCopiesAgree(unittest.TestCase):
    """§7's actual claim: the C64 build and Python cannot drift apart."""

    def test_header_scalars_match_python(self):
        h = _c_scalars(H_PATH)
        for c_name, py_name in (
            ("kCloudCellU", "CELL_U"),
            ("kCloudCellShift", "CELL_SHIFT"),
            ("kCloudScanRadius", "SCAN_RADIUS"),
            ("kCloudGateBits", "GATE_BITS"),
            ("kCloudGateLimit", "GATE_LIMIT"),
            ("kCloudJitterShift", "JITTER_SHIFT"),
            ("kCloudBlobU", "BLOB_U"),
            ("kCloudDeckU", "DECK_U"),
            ("kCloudOffsetU", "OFFSET_U"),
            ("kCloudCullU", "CULL_U"),
            ("kCloudRungCount", "RUNG_COUNT"),
            ("kCloudRungStacked", "RUNG_STACKED"),
            ("kCloudPatternCount", "PATTERN_COUNT"),
            ("kCloudBlobsPerGroup", "BLOBS_PER_GROUP"),
        ):
            self.assertIn(c_name, h, "%s missing from clouddef.h" % c_name)
            self.assertEqual(h[c_name], getattr(clouddef, py_name), c_name)
        # The cell masks are the wrap, expressed as a mask on the C side.
        self.assertEqual(h["kCloudCellMaskX"] + 1, clouddef.CELLS_X)
        self.assertEqual(h["kCloudCellMaskY"] + 1, clouddef.CELLS_Y)

    def test_tables_match_python(self):
        self.assertEqual(_c_array(CC_PATH, "kCloudHashX"), clouddef.HASH_X)
        self.assertEqual(_c_array(CC_PATH, "kCloudHashY"), clouddef.HASH_Y)
        self.assertEqual(_c_array(CC_PATH, "kCloudHashA"), clouddef.HASH_A)
        self.assertEqual(_c_array(CC_PATH, "kCloudHashB"), clouddef.HASH_B)
        self.assertEqual(
            _c_array(CC_PATH, "kCloudRungDepth[kCloudRungCount]"),
            clouddef.RUNG_DEPTH,
        )
        flat = [v for pattern in clouddef.GROUP_OFFSET
                for blob in pattern for v in blob]
        self.assertEqual(_c_array(CC_PATH, "kCloudGroupOffset"), flat)

    def test_regenerating_would_not_change_anything(self):
        # The committed files are what the generator produces from the constants
        # at the top of it. If this fails, someone edited a generated file or
        # changed a constant without rerunning `make clouds`.
        #
        # Rebuilt from the seed the search recorded rather than by repeating the
        # search, which takes a few seconds. What the search *chose* is covered
        # by the layout tests above: if a different seed would now win, its
        # tables would not match these.
        import random
        rng = random.Random(clouddef.SEARCH_SEED)
        tables = (
            [rng.randrange(256) for _ in range(clouddef.CELLS_X)],
            [rng.randrange(256) for _ in range(clouddef.CELLS_Y)],
            [rng.randrange(256) for _ in range(32)],
            [rng.randrange(256) for _ in range(32)],
        )
        hx, hy, ha, hb = tables
        self.assertEqual(hx, clouddef.HASH_X)
        self.assertEqual(hy, clouddef.HASH_Y)
        self.assertEqual(ha, clouddef.HASH_A)
        self.assertEqual(hb, clouddef.HASH_B)
        self.assertEqual(generate_clouds.rung_depths(), clouddef.RUNG_DEPTH)
        self.assertEqual(
            [[list(b) for b in p] for p in generate_clouds.GROUP_OFFSETS],
            [[list(b) for b in p] for p in clouddef.GROUP_OFFSET],
        )
        self.assertEqual(generate_clouds.GATE_BITS, clouddef.GATE_BITS)
        self.assertEqual(generate_clouds.GATE_LIMIT, clouddef.GATE_LIMIT)
        self.assertEqual(generate_clouds.BLOB_U, clouddef.BLOB_U)
        self.assertEqual(generate_clouds.DECK_U, clouddef.DECK_U)


class TestTheGeneratorsGuards(unittest.TestCase):
    def test_a_mismatched_cull_fails_the_build(self):
        original = generate_clouds.BLOB_U
        try:
            generate_clouds.BLOB_U = original + 8  # ladder no longer reaches
            with self.assertRaises(SystemExit):
                generate_clouds.check_the_numbers_still_agree()
        finally:
            generate_clouds.BLOB_U = original
        generate_clouds.check_the_numbers_still_agree()

    def test_a_bad_group_pattern_fails_the_build(self):
        original = generate_clouds.GROUP_OFFSETS
        try:
            generate_clouds.GROUP_OFFSETS = [
                [(0, 0, 0), (0, 0, 0), (0, 0, 0)]
            ] * generate_clouds.PATTERN_COUNT
            with self.assertRaises(SystemExit):
                generate_clouds.check_group_offsets()
        finally:
            generate_clouds.GROUP_OFFSETS = original
        generate_clouds.check_group_offsets()


if __name__ == "__main__":
    unittest.main()
