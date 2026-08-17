#!/usr/bin/env python3
"""Generates the cloud placement constants, for the C64 build and for Python.

docs/clouds.md §7: the hash tables, the rung thresholds and the group patterns
all follow from four numbers - the cell size, the gate mask, the blob diameter
and the deck altitude - and none of them should be typed by hand into clouds.cc.
This emits c64o/clouddef.h, c64o/clouddef.cc and lib/clouddef.py from one place,
so the preview, the tests and the C64 build cannot drift.

Run it through `make clouds`, or `make data` with everything else.

The hash tables are *searched*, not seeded and hoped over: the loop below walks
seeds until it finds a set whose density, spread and independence all pass, then
stops. That is deterministic - the same four numbers always give the same tables
- while still letting the density be retuned by changing GATE_LIMIT / TARGET_CELLS and
re-running, rather than by hand-editing 88 bytes.
"""

import argparse
import os
import random
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- The four numbers, and what follows from them ---------------------------
#
# Units are the terrain grid's: 1 unit = 2 m, because world.cc down-shifts
# flight_eye_* by 9. See docs/clouds.md §2.1.

CELL_U = 2048           # §2.2  cloud cell, 4096 m on a side
CELL_SHIFT = 11         #       cell index = position >> 11
CELLS_X = 8             #       the world is 16 map tiles across in X ...
CELLS_Y = 16            #       ... and 32 in Y, and a cell is two tiles
SCAN_RADIUS = 2         # §2.2  a 5 x 5 scan
GATE_BITS = 0x1F        # §2.4  the five gate bits of the hash byte
GATE_LIMIT = 10         #       a cell carries a group when (ha & GATE_BITS) < this.
                        #       Density is GATE_LIMIT/32, so it is a real dial rather
                        #       than the power-of-two steps a mask gives.
TARGET_CELLS = 38       # §2.4  how many of the 128 cells should carry a group.
                        #       Twice the 19 the first tuning pass settled on.
BLOB_U = 96             # §3.1  blob diameter, 192 m
OFFSET_U = 32           # §2.5  group offset step, 64 m
OVERLAP_MAX = 0.75      # §2.5  furthest two blobs, as a share of BLOB_U
DECK_U = 700            # §2.6  cloud deck altitude, 1400 m
JITTER_SHIFT = 7        # §2.3  a 4-bit jitter nibble scaled to the cell

RUNG_COUNT = 10         # §3.2  ladder widths 3, 5, 7 .. 21 world pixels
RUNG_STACKED = 5        #       rungs from here up are a 1 x 2 sprite stack
BLOBS_PER_GROUP = 3     # §2.5
PATTERN_COUNT = 4

# The projection scales by 256 and a world pixel is two screen pixels, so an
# object of size S at depth D spans 128 * S / D world pixels. See §3.1.
PROJECTION_SCALE = 128


def rung_depths():
    """Largest camera-space depth at which each rung still applies (§3.2)."""
    return [
        int(round(PROJECTION_SCALE * BLOB_U / (3 + 2 * i)))
        for i in range(RUNG_COUNT)
    ]


def cull_u():
    """The draw distance, which is also the scan's guaranteed radius (§2.2)."""
    return SCAN_RADIUS * CELL_U


def check_the_numbers_still_agree():
    """The two couplings between the four numbers, spelled out.

    Both of these are easy to break by retuning one number in isolation, and
    both fail on screen in ways that are hard to attribute, so they fail here
    instead.
    """
    depths = rung_depths()

    # §2.2. The cull radius and the scan's *guaranteed* radius have to be the
    # same number. A group found beyond the guarantee appears and disappears as
    # the eye crosses a cell boundary rather than as it moves - a flicker keyed
    # to nothing the pilot can see.
    if depths[0] != cull_u():
        raise SystemExit(
            "generate_clouds: the ladder and the scan disagree about the draw "
            "distance.\n"
            "  rung 0 reaches %d units, the %d x %d scan guarantees %d.\n"
            "  docs/clouds.md §2.2 and §3.1: the smallest rung must land "
            "exactly at the cull,\n"
            "  or clouds either pop in at a visible size or flicker at cell "
            "boundaries. Retune\n"
            "  BLOB_U (currently %d) to %d, or change SCAN_RADIUS / CELL_U."
            % (depths[0], 2 * SCAN_RADIUS + 1, 2 * SCAN_RADIUS + 1, cull_u(),
               BLOB_U, cull_u() * 3 // PROJECTION_SCALE)
        )

    # The ladder has to be strictly descending, or rung selection - a walk down
    # the table until the depth fits - would skip a rung.
    for i in range(1, RUNG_COUNT):
        if depths[i] >= depths[i - 1]:
            raise SystemExit(
                "generate_clouds: rung %d (%d) does not sit below rung %d (%d)."
                % (i, depths[i], i - 1, depths[i - 1])
            )

    # The jitter must span the cell exactly: 16 steps of 2^JITTER_SHIFT.
    if (16 << JITTER_SHIFT) != CELL_U:
        raise SystemExit(
            "generate_clouds: a 4-bit jitter of 2^%d units spans %d, not the "
            "%d-unit cell." % (JITTER_SHIFT, 16 << JITTER_SHIFT, CELL_U)
        )
    if (1 << CELL_SHIFT) != CELL_U:
        raise SystemExit(
            "generate_clouds: CELL_SHIFT %d does not match CELL_U %d."
            % (CELL_SHIFT, CELL_U)
        )


# --- The hash ---------------------------------------------------------------
#
# §2.3. Two scramble tables mix the cell coordinates into one byte; that byte
# indexes two more, one carrying presence and pattern, the other the jitter.
#
# Presence and jitter come out of *different* tables on purpose. Gating on
# `h < threshold` and then taking `h >> 3` for a jitter correlates the two -
# every cell that has a cloud also has a small jitter - and the layout picks up
# a visible diagonal grain. Two tables indexed by the same scrambled byte cost
# 32 extra bytes and remove the problem, but only if the tables themselves are
# independent, which is what the search below is for.


def cell_hash(tables, cx, cy):
    hx, hy, ha, hb = tables
    a = hx[cx % CELLS_X] ^ hy[cy % CELLS_Y]
    return ha[a & 31], hb[a & 31]


def group_at(tables, cx, cy):
    """The group in this cell, or None. Positions are in world units."""
    ha, hb = cell_hash(tables, cx, cy)
    if (ha & GATE_BITS) >= GATE_LIMIT:
        return None
    return {
        "pattern": (ha >> 6) & (PATTERN_COUNT - 1),
        "jx": (hb & 0x0F) << JITTER_SHIFT,
        "jy": (hb >> 4) << JITTER_SHIFT,
    }


# How even the layout has to look. docs/clouds.md §2.4 asks for sparse, but
# sparse and *clumpy* are different things, and random is clumpy: a Poisson
# scatter puts pairs of clouds almost on top of each other and leaves holes
# between them, which reads as an uneven sky rather than a quiet one.
#
# world.cc already solves this problem for the terrain dots, with "Mitchell's
# Best-Candidate algorithm to maximize distance between points while
# maintaining an organic, non-linear distribution". The same idea applies here,
# reached from the other end: the layout is fixed by the hash, so instead of
# placing points well we search for tables whose points happen to be placed
# well. Two measures, and both matter:
#
#   minimum separation   the closest pair. Low means clumps.
#   covering radius      the furthest you can be from any cloud. High means
#                        holes.
#
# Maximising their ratio is what blue noise is; a perfectly even lattice scores
# highest, pure randomness scores about 0.25. The tables below reach 0.58.

# Cells may hold at most this many groups, and each half and quadrant of the
# world must carry roughly its share. Coverage alone is not enough - the first
# version of this checked only how many rows and columns had *any* group, which
# a layout with everything bunched on one side passes comfortably, and one did.
#
# Measured note: at the current settings these are belt and braces. Drop all
# four and the evenness score still picks the same seed out of six times as many
# candidates - balance turns out to be a *consequence* of even spacing rather
# than an independent requirement. They are kept because that implication is not
# guaranteed to survive a change of density or world size, and because they make
# the intent legible; they are not what is doing the work.
# Per-column and per-row caps, as a multiple of the even share rather than as
# absolute counts. They were 3 and 4, which are the right numbers for 19 groups
# and reject *every* seed at twice that density - the caps, not the gate, were
# what made the density dial look stuck.
COLUMN_CAP = 1.9        # of (groups / CELLS_Y)
ROW_CAP = 1.9           # of (groups / CELLS_X)
HALF_BALANCE = (0.40, 0.60)
QUADRANT_BALANCE = (0.15, 0.35)

# Seeds to consider. Deterministic, and wide enough that the score plateaus;
# the winner is recorded in the generated files so nothing downstream has to
# repeat the search.
SEARCH_SEEDS = 40000


def _layout(tables):
    """Every group's world position, plus the counts the balance checks use."""
    hx, hy, ha_t, hb_t = tables
    points = []
    columns = [0] * CELLS_Y
    rows = [0] * CELLS_X
    quadrants = [0, 0, 0, 0]
    patterns = set()
    jitter_quadrants = [0, 0, 0, 0]
    for cx in range(CELLS_X):
        ux = hx[cx]
        for cy in range(CELLS_Y):
            idx = (ux ^ hy[cy]) & 31
            ha = ha_t[idx]
            if (ha & GATE_BITS) >= GATE_LIMIT:
                continue
            hb = hb_t[idx]
            jx = (hb & 0x0F) << JITTER_SHIFT
            jy = (hb >> 4) << JITTER_SHIFT
            points.append((cx * CELL_U + jx, cy * CELL_U + jy))
            columns[cy] += 1
            rows[cx] += 1
            quadrants[(1 if cy >= CELLS_Y // 2 else 0)
                      + (2 if cx >= CELLS_X // 2 else 0)] += 1
            patterns.add((ha >> 6) & (PATTERN_COUNT - 1))
            jitter_quadrants[(1 if (hb & 0x0F) >= 8 else 0)
                             + (2 if (hb >> 4) >= 8 else 0)] += 1
    return points, columns, rows, quadrants, patterns, jitter_quadrants


def _torus_sq(a, b):
    """Squared distance on the wrapping world."""
    dx = abs(a[0] - b[0])
    dx = min(dx, CELLS_X * CELL_U - dx)
    dy = abs(a[1] - b[1])
    dy = min(dy, CELLS_Y * CELL_U - dy)
    return dx * dx + dy * dy


def min_separation(points):
    """The closest pair. Low means clumps."""
    return min(
        _torus_sq(points[i], points[j])
        for i in range(len(points))
        for j in range(i + 1, len(points))
    ) ** 0.5


def covering_radius(points, nx=16, ny=32):
    """The furthest a point in the world can be from any group. High means
    holes."""
    step_x = CELLS_X * CELL_U / float(nx)
    step_y = CELLS_Y * CELL_U / float(ny)
    worst = 0
    for i in range(nx):
        for j in range(ny):
            s = (i * step_x, j * step_y)
            d = min(_torus_sq(s, p) for p in points)
            if d > worst:
                worst = d
    return worst ** 0.5


def evenness(points):
    """Blue-noise score: separation against holes. Higher is more even."""
    return min_separation(points) / covering_radius(points)


def _balance_ok(counts):
    """The hard constraints. Judged before the expensive measures."""
    points, columns, rows, quadrants, patterns, jitter_q = counts
    n = len(points)
    if n < 2:
        return False
    # The realised count, not the gate's nominal: 128 cells share 32 hash
    # entries, so what a given GATE_LIMIT actually yields varies widely by
    # seed. Aiming at the count directly is what makes density a dial.
    if not (TARGET_CELLS * 0.9 <= n <= TARGET_CELLS * 1.1):
        return False
    # Every one of the four orientations of §2.5 has to be reachable.
    if len(patterns) != PATTERN_COUNT:
        return False
    # No cell column or row may hoard groups.
    if (max(columns) > max(2, COLUMN_CAP * n / CELLS_Y)
            or max(rows) > max(3, ROW_CAP * n / CELLS_X)):
        return False
    # Each half and quadrant of the world carries roughly its share.
    if not (HALF_BALANCE[0] <= sum(columns[:CELLS_Y // 2]) / n
            <= HALF_BALANCE[1]):
        return False
    if not (HALF_BALANCE[0] <= sum(rows[:CELLS_X // 2]) / n <= HALF_BALANCE[1]):
        return False
    if min(quadrants) < QUADRANT_BALANCE[0] * n:
        return False
    if max(quadrants) > QUADRANT_BALANCE[1] * n:
        return False
    # §2.3 worried that presence and jitter might correlate, and this used to
    # test for it directly - every group in the same corner of its cell. That
    # test was a *proxy* for "the layout looks even", and now that evenness is
    # measured for real it was rejecting the best layouts to enforce a
    # statistic nobody looks at. What is left is the degenerate case the score
    # cannot see: if every group sat at the same offset the positions would
    # form a lattice, which scores well and looks manufactured.
    if sum(1 for q in jitter_q if q) < 2 or max(jitter_q) > 0.75 * n:
        return False
    return True


def search_tables(max_seeds=SEARCH_SEEDS):
    """The most even layout among the seeds, not merely the first passable one.

    Two passes, because the covering radius is the expensive measure: rank the
    seeds that clear the hard constraints by their minimum separation, then
    score only the best of those properly.
    """
    passing = []
    for seed in range(max_seeds):
        rng = random.Random(seed)
        tables = (
            [rng.randrange(256) for _ in range(CELLS_X)],
            [rng.randrange(256) for _ in range(CELLS_Y)],
            [rng.randrange(256) for _ in range(32)],
            [rng.randrange(256) for _ in range(32)],
        )
        counts = _layout(tables)
        if not _balance_ok(counts):
            continue
        passing.append((min_separation(counts[0]), seed, tables, counts))

    if not passing:
        raise SystemExit(
            "generate_clouds: no usable hash tables in %d seeds. If GATE_LIMIT "
            "was just changed,\n  the density band or the balance constraints "
            "may need to move with it." % max_seeds
        )

    passing.sort(key=lambda p: -p[0])
    best = None
    for _, seed, tables, counts in passing[:150]:
        score = evenness(counts[0])
        if best is None or score > best[0]:
            best = (score, seed, tables, counts)

    score, seed, tables, counts = best
    points, columns, rows, quadrants, patterns, _ = counts
    return seed, tables, {
        "density": len(points) / float(CELLS_X * CELLS_Y),
        "groups": len(points),
        "quadrants": quadrants,
        "columns": columns,
        "evenness": score,
        "min_separation": min_separation(points),
        "covering_radius": covering_radius(points),
        "candidates": len(passing),
    }


# --- The group layout -------------------------------------------------------
#
# §2.5. Three blobs whose spacing is about half a blob diameter, so they overlap
# from any angle. Offsets are built in *world* axes, so a group keeps its shape
# as the camera turns, and they are given in halves of the offset basis - the
# runtime has ex, ey, ez and their halves precomputed once per frame, so a
# coefficient of +/-2 or +/-1 is an add, never a multiply.
#
# The four patterns are the same triangle at four orientations. The third blob's
# half-step in z is a deliberate small departure from "all at the same
# altitude": a flat triple reads as a row of dots from the side.

GROUP_OFFSETS = [
    # pattern 0
    [(-2, -1, 0), (+2, -1, 0), (0, +2, +1)],
    # pattern 1
    [(-1, -2, 0), (-1, +2, 0), (+2, 0, +1)],
    # pattern 2
    [(-2, +1, 0), (+2, +1, 0), (0, -2, +1)],
    # pattern 3
    [(+1, -2, 0), (+1, +2, 0), (-2, 0, +1)],
]


def check_group_offsets():
    if len(GROUP_OFFSETS) != PATTERN_COUNT:
        raise SystemExit("generate_clouds: expected %d patterns" % PATTERN_COUNT)
    for i, pat in enumerate(GROUP_OFFSETS):
        if len(pat) != BLOBS_PER_GROUP:
            raise SystemExit(
                "generate_clouds: pattern %d has %d blobs, expected %d"
                % (i, len(pat), BLOBS_PER_GROUP)
            )
        for a in pat:
            if any(abs(v) > 2 for v in a):
                raise SystemExit(
                    "generate_clouds: pattern %d has a coefficient outside "
                    "+/-2, which would stop being an add" % i
                )
        # The blobs must actually be apart, or the group is one blob drawn
        # three times.
        if len(set(pat)) != BLOBS_PER_GROUP:
            raise SystemExit(
                "generate_clouds: pattern %d places two blobs at the same "
                "offset" % i
            )
        # ... and close enough to overlap, which is the whole reason a group
        # is three blobs rather than three clouds (§2.5). The measure is the
        # *furthest* pair, not each blob's nearest neighbour: a group is drawn
        # from every direction, and the pair that decides whether it reads as
        # one cloud is the one the current heading happens to spread widest.
        # The first version of this check asked only that every blob had *a*
        # neighbour inside a diameter, which the shipped table passed while
        # its two outer blobs sat exactly one diameter apart - tangent circles,
        # touching at a point, and visibly three lumps on screen.
        half = OFFSET_U / 2.0
        limit = OVERLAP_MAX * BLOB_U
        for a in range(BLOBS_PER_GROUP):
            for b in range(a + 1, BLOBS_PER_GROUP):
                d = sum(
                    ((pat[a][k] - pat[b][k]) * half) ** 2 for k in range(3)
                ) ** 0.5
                if d > limit:
                    raise SystemExit(
                        "generate_clouds: pattern %d blobs %d and %d are %.0f "
                        "units apart,\n  past the %.0f-unit limit (%.0f%% of "
                        "the %d-unit blob diameter). They would stop\n"
                        "  overlapping from some headings (docs/clouds.md "
                        "§2.5)." % (i, a, b, d, limit, 100 * OVERLAP_MAX,
                                    BLOB_U)
                    )


# --- Emitting ---------------------------------------------------------------

BANNER = ("// Generated by tools/generate_clouds.py -- do not edit.\n"
          "// Run `make clouds` after changing the constants at the top of "
          "that script.\n")


def _rows(values, per_line, fmt="%d"):
    out = []
    for i in range(0, len(values), per_line):
        out.append("    " + ", ".join(fmt % v for v in values[i:i + per_line]) + ",")
    return "\n".join(out)


def write_header(path, seed):
    with open(path, "w") as f:
        f.write("#ifndef CLOUDDEF_H\n#define CLOUDDEF_H\n\n")
        f.write(BANNER)
        f.write("//\n// See docs/clouds.md: §2 for the placement, §3 for the "
                "size ladder.\n// Hash tables from search seed %d.\n\n" % seed)
        f.write("#include <stdint.h>\n\n")

        f.write("// The cloud cell grid (§2.2). Cell index is the world "
                "position shifted down\n// by kCloudCellShift and masked; the "
                "masks are the world's wrap.\n")
        f.write("static const uint16_t kCloudCellU = %d;\n" % CELL_U)
        f.write("static const uint8_t kCloudCellShift = %d;\n" % CELL_SHIFT)
        f.write("static const uint8_t kCloudCellMaskX = %d;\n" % (CELLS_X - 1))
        f.write("static const uint8_t kCloudCellMaskY = %d;\n" % (CELLS_Y - 1))
        f.write("static const uint8_t kCloudScanRadius = %d;\n\n" % SCAN_RADIUS)

        f.write("// A cell carries a group when (ha & kCloudGateBits) < kCloudGateLimit "
                "(§2.4).\n")
        f.write("static const uint8_t kCloudGateBits = 0x%02X;\n" % GATE_BITS)
        f.write("static const uint8_t kCloudGateLimit = %d;\n" % GATE_LIMIT)
        f.write("static const uint8_t kCloudJitterShift = %d;\n\n"
                % JITTER_SHIFT)

        f.write("// Blob diameter (§3.1), deck altitude (§2.6) and the "
                "half-basis the group\n// offsets are counted in (§2.5), all "
                "in 2 m units.\n")
        f.write("static const uint8_t kCloudBlobU = %d;\n" % BLOB_U)
        f.write("static const int16_t kCloudDeckU = %d;\n" % DECK_U)
        f.write("static const uint8_t kCloudOffsetU = %d;\n\n" % OFFSET_U)

        f.write("// The draw distance. Equal to the scan's guaranteed radius "
                "by construction -\n// generate_clouds.py fails the build if "
                "they ever disagree (§2.2).\n")
        f.write("static const int16_t kCloudCullU = %d;\n\n" % cull_u())

        f.write("// The size ladder (§3.2). Rungs 0..%d are one sprite and "
                "index\n// kSpriteDefCloud1Sprite; %d..%d are a 1 x 2 stack "
                "and index\n// kSpriteDefCloud2Sprite[rung - %d].\n"
                % (RUNG_STACKED - 1, RUNG_STACKED, RUNG_COUNT - 1,
                   RUNG_STACKED))
        f.write("static const uint8_t kCloudRungCount = %d;\n" % RUNG_COUNT)
        f.write("static const uint8_t kCloudRungStacked = %d;\n\n"
                % RUNG_STACKED)

        f.write("static const uint8_t kCloudPatternCount = %d;\n"
                % PATTERN_COUNT)
        f.write("static const uint8_t kCloudBlobsPerGroup = %d;\n\n"
                % BLOBS_PER_GROUP)

        f.write("extern const uint8_t kCloudHashX[%d];\n" % CELLS_X)
        f.write("extern const uint8_t kCloudHashY[%d];\n" % CELLS_Y)
        f.write("extern const uint8_t kCloudHashA[32];\n")
        f.write("extern const uint8_t kCloudHashB[32];\n")
        f.write("extern const int16_t kCloudRungDepth[kCloudRungCount];\n")
        f.write("extern const int8_t "
                "kCloudGroupOffset[kCloudPatternCount][kCloudBlobsPerGroup][3];"
                "\n\n")
        f.write('#pragma compile("clouddef.cc")\n\n#endif\n')


def write_source(path, tables):
    hx, hy, ha, hb = tables
    depths = rung_depths()
    with open(path, "w") as f:
        f.write(BANNER)
        f.write('\n#include "clouddef.h"\n\n')
        f.write("const uint8_t kCloudHashX[%d] = {\n%s\n};\n\n"
                % (CELLS_X, _rows(hx, 8)))
        f.write("const uint8_t kCloudHashY[%d] = {\n%s\n};\n\n"
                % (CELLS_Y, _rows(hy, 8)))
        f.write("// Presence in the low bits, pattern in the top two.\n")
        f.write("const uint8_t kCloudHashA[32] = {\n%s\n};\n\n" % _rows(ha, 8))
        f.write("// Jitter x in the low nibble, jitter y in the high one.\n")
        f.write("const uint8_t kCloudHashB[32] = {\n%s\n};\n\n" % _rows(hb, 8))

        f.write("// Largest depth at which each rung still applies, so rung\n"
                "// selection is a compare chain and never a divide (§3.2):\n")
        for i, d in enumerate(depths):
            w = 3 + 2 * i
            f.write("//   rung %d: %2d x2 world px, out to %5d units (%5d m)\n"
                    % (i, w, d, d * 2))
        f.write("const int16_t kCloudRungDepth[kCloudRungCount] = {\n%s\n};\n\n"
                % _rows(depths, 5))

        f.write("// Group layout (§2.5), in halves of kCloudOffsetU along the\n"
                "// world x, y and z axes.\n")
        f.write("const int8_t "
                "kCloudGroupOffset[kCloudPatternCount][kCloudBlobsPerGroup]"
                "[3] = {\n")
        for i, pat in enumerate(GROUP_OFFSETS):
            f.write("    { %s }, // pattern %d\n"
                    % (", ".join("{ %2d, %2d, %2d }" % a for a in pat), i))
        f.write("};\n")


def write_python(path, tables, seed, stats):
    hx, hy, ha, hb = tables
    with open(path, "w") as f:
        f.write("# Generated by tools/generate_clouds.py -- do not edit.\n")
        f.write("# The same constants c64o/clouddef.h carries, for the "
                "preview and the tests.\n")
        f.write("# Hash tables from search seed %d; %d of %d cells carry a "
                "group.\n" % (seed, stats["groups"], CELLS_X * CELLS_Y))
        f.write("# Evenness %.3f = separation %.0f / covering radius %.0f "
                "units (see the\n# generator's blue-noise notes). Recorded so "
                "the tests can rebuild these\n# tables and re-check the layout "
                "without repeating the search.\n\n"
                % (stats["evenness"], stats["min_separation"],
                   stats["covering_radius"]))
        f.write("SEARCH_SEED = %d\n" % seed)
        f.write("EVENNESS = %.4f\n" % stats["evenness"])
        f.write("MIN_SEPARATION = %.1f\n" % stats["min_separation"])
        f.write("COVERING_RADIUS = %.1f\n" % stats["covering_radius"])
        for name, value in (
            ("CELL_U", CELL_U), ("CELL_SHIFT", CELL_SHIFT),
            ("CELLS_X", CELLS_X), ("CELLS_Y", CELLS_Y),
            ("SCAN_RADIUS", SCAN_RADIUS), ("GATE_BITS", GATE_BITS),
            ("GATE_LIMIT", GATE_LIMIT),
            ("BLOB_U", BLOB_U), ("DECK_U", DECK_U),
            ("OFFSET_U", OFFSET_U), ("JITTER_SHIFT", JITTER_SHIFT),
            ("CULL_U", cull_u()), ("RUNG_COUNT", RUNG_COUNT),
            ("RUNG_STACKED", RUNG_STACKED),
            ("BLOBS_PER_GROUP", BLOBS_PER_GROUP),
            ("PATTERN_COUNT", PATTERN_COUNT),
            ("PROJECTION_SCALE", PROJECTION_SCALE),
        ):
            f.write("%s = %d\n" % (name, value))
        f.write("\nHASH_X = %r\n" % (hx,))
        f.write("HASH_Y = %r\n" % (hy,))
        f.write("HASH_A = %r\n" % (ha,))
        f.write("HASH_B = %r\n" % (hb,))
        f.write("RUNG_DEPTH = %r\n" % (rung_depths(),))
        f.write("GROUP_OFFSET = %r\n" % (GROUP_OFFSETS,))
        f.write('''

def cell_hash(cx, cy):
    """The two hash bytes for a cloud cell. Mirrors _cloud_at() in clouds.cc."""
    a = HASH_X[cx % CELLS_X] ^ HASH_Y[cy % CELLS_Y]
    return HASH_A[a & 31], HASH_B[a & 31]


def group_at(cx, cy):
    """The group in this cell as {pattern, x, y} in world units, or None.

    cx and cy are unmasked cell coordinates, so the position is absolute and
    the hash wraps - exactly how world.cc treats the map.
    """
    ha, hb = cell_hash(cx, cy)
    if (ha & GATE_BITS) >= GATE_LIMIT:
        return None
    return {
        "pattern": (ha >> 6) & (PATTERN_COUNT - 1),
        "x": cx * CELL_U + ((hb & 0x0F) << JITTER_SHIFT),
        "y": cy * CELL_U + ((hb >> 4) << JITTER_SHIFT),
    }


def rung_for_depth(depth):
    """Ladder rung for a camera-space depth, or None beyond the cull (§3.2)."""
    if depth > RUNG_DEPTH[0]:
        return None
    rung = 0
    while rung < RUNG_COUNT - 1 and depth <= RUNG_DEPTH[rung + 1]:
        rung += 1
    return rung
''')


def main():
    parser = argparse.ArgumentParser(
        description="Generate cloud placement constants (docs/clouds.md §7)."
    )
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    check_the_numbers_still_agree()
    check_group_offsets()
    seed, tables, stats = search_tables()

    h_path = os.path.join(REPO_ROOT, "c64o", "clouddef.h")
    cc_path = os.path.join(REPO_ROOT, "c64o", "clouddef.cc")
    py_path = os.path.join(REPO_ROOT, "lib", "clouddef.py")
    write_header(h_path, seed)
    write_source(cc_path, tables)
    write_python(py_path, tables, seed, stats)

    if not args.quiet:
        print("Generated %s" % h_path)
        print("Generated %s" % cc_path)
        print("Generated %s" % py_path)
        print("  hash seed %d of %d candidates, %d of %d cells carry a group "
              "(%.0f%%)"
              % (seed, stats["candidates"], stats["groups"],
                 CELLS_X * CELLS_Y, 100 * stats["density"]))
        print("  evenness %.2f (separation %.0f / covering %.0f units); "
              "quadrants %s"
              % (stats["evenness"], stats["min_separation"],
                 stats["covering_radius"], stats["quadrants"]))
        print("  blob %d m, deck %d m, cell %d m, cull %d m"
              % (BLOB_U * 2, DECK_U * 2, CELL_U * 2, cull_u() * 2))
        print("  run tools/render_cloud_preview.py to see the layout and the "
              "density table")


if __name__ == "__main__":
    main()
