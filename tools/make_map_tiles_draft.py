#!/usr/bin/env python3
"""
Writes a starting gfx/ppilot_map_tiles.png for the map view.

The PNG is the source of truth once it exists -- edit it in GIMP and run
`make map-tiles` to regenerate the C tables. This tool only lays down the
first version, and is here so the sheet can be recreated from scratch if it
is ever lost. It will not overwrite an existing sheet without --force.

The four gridline variants are transcribed from gfx/ppilot_map2.koa, so the
dotted grid keeps the two-cell period of the concept image. Every other shape
is a first pass meant to be redrawn.

Legend for the ASCII art below:
    .  green, the global background (bit pair 00)
    A  the tile's first color  (bit pair 10, screen RAM low nibble)
    B  the tile's second color (bit pair 11, color RAM)
    #  white, the overlay layer -- digit stencils only

White is reserved screen-wide for the flight path and the navpoint digits, so
no art tile may use it. That is why the white dot clusters (MAP_DOT_WHITE) are
drawn in light gray here.
"""

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

from lib.c64_colors import PALETTE_RGB  # noqa: E402

BLACK, WHITE, CYAN, GREEN, BLUE, YELLOW = 0, 1, 3, 5, 6, 7
MEDIUM_GRAY, LIGHT_GREEN, LIGHT_GRAY = 12, 13, 15

# (name, art, color A, color B)
TILES = [
    # --- Gridlines, transcribed from the concept ---------------------------
    # The grid has a two-cell period, so the tile depends on cell parity.
    ("GRID_EVEN_EVEN", [
        "....",
        "....",
        "....",
        "....",
        "....",
        "....",
        "....",
        "....",
    ], MEDIUM_GRAY, BLACK),
    ("GRID_EVEN_ODD", [
        "..A.",
        "....",
        "....",
        "....",
        "..A.",
        "....",
        "....",
        "....",
    ], MEDIUM_GRAY, BLACK),
    ("GRID_ODD_EVEN", [
        "....",
        "....",
        "....",
        "....",
        "A.A.",
        "....",
        "....",
        "....",
    ], MEDIUM_GRAY, BLACK),
    ("GRID_ODD_ODD", [
        "..A.",
        "....",
        "....",
        "....",
        "A.A.",
        "....",
        "....",
        "....",
    ], MEDIUM_GRAY, BLACK),

    # --- Dot clusters ------------------------------------------------------
    # Rough or built-up ground: a dense stipple.
    ("DOT_BLACK", [
        "....",
        ".A.A",
        "....",
        "A.A.",
        "....",
        ".A.A",
        "....",
        "A.A.",
    ], BLACK, BLACK),
    # These run in lines through the world (approach paths), so they read as a
    # dashed track rather than a texture. Light gray, not white.
    ("DOT_WHITE", [
        ".A..",
        ".A..",
        "....",
        ".A..",
        ".A..",
        "....",
        ".A..",
        ".A..",
    ], LIGHT_GRAY, BLACK),
    # Shallows: a light ripple around the blue water.
    ("DOT_CYAN", [
        "....",
        "A.A.",
        "....",
        ".A.A",
        "....",
        "A.A.",
        "....",
        ".A.A",
    ], CYAN, BLACK),
    # Open water, one step in from the shallows.
    ("DOT_BLUE", [
        ".A.A",
        "AAAA",
        "A.AA",
        "AAAA",
        "AAA.",
        "AAAA",
        "A.AA",
        "AAAA",
    ], BLUE, BLACK),
    # Cropland: ploughed rows.
    ("DOT_YELLOW", [
        "....",
        "AAAA",
        "....",
        "AAAA",
        "....",
        "AAAA",
        "....",
        "AAAA",
    ], YELLOW, BLACK),

    # --- Polygon objects ---------------------------------------------------
    # Runway: a horizontal strip with a centreline.
    ("RUNWAY", [
        "....",
        "....",
        "AAAA",
        "BBBB",
        "AAAA",
        "....",
        "....",
        "....",
    ], BLACK, MEDIUM_GRAY),
    # Field: a diagonal hatch, dense.
    ("FIELD", [
        ".AAA",
        "AAA.",
        ".AAA",
        "AAA.",
        ".AAA",
        "AAA.",
        ".AAA",
        "AAA.",
    ], LIGHT_GREEN, BLACK),
    # Same crop, thinner stand.
    ("FIELD_SPARSE", [
        ".A.A",
        "....",
        "A.A.",
        "....",
        ".A.A",
        "....",
        "A.A.",
        "....",
    ], LIGHT_GREEN, BLACK),
    # Ripe crop: the same two densities in yellow.
    ("FIELD_YELLOW", [
        "AAAA",
        ".AA.",
        "AAAA",
        ".AA.",
        "AAAA",
        ".AA.",
        "AAAA",
        ".AA.",
    ], YELLOW, BLACK),
    ("FIELD_YELLOW_SPARSE", [
        "A.A.",
        "....",
        ".A.A",
        "....",
        "A.A.",
        "....",
        ".A.A",
        "....",
    ], YELLOW, BLACK),
    # Pond: a small blob, no shoreline.
    ("POND", [
        "....",
        "....",
        ".AA.",
        "AAAA",
        "AAAA",
        ".AA.",
        "....",
        "....",
    ], BLUE, BLACK),
    # Lake: fills the cell, with a cyan shore.
    ("LAKE", [
        ".BB.",
        "BAAB",
        "AAAA",
        "AAAA",
        "AAAA",
        "AAAA",
        "BAAB",
        ".BB.",
    ], BLUE, CYAN),
    # Town: a handful of buildings.
    ("TOWN", [
        "....",
        ".A..",
        ".AA.",
        "....",
        "....",
        "A.A.",
        "AAA.",
        "....",
    ], BLACK, BLACK),
    # City: dense blocks with taller gray towers.
    ("CITY", [
        "AA.B",
        "AA.B",
        "....",
        "B.AA",
        "B.AA",
        "....",
        "AA.B",
        "AA.B",
    ], BLACK, MEDIUM_GRAY),
]

# Navpoint digits. Transcribed from the multicolor font, taking the light blue
# stroke as ink and discarding the brown edge shading, so these match the
# game's text. Drawn in white because they belong to the overlay layer.
DIGITS = [
    ("DIGIT_1", [
        "....",
        ".#..",
        "##..",
        ".#..",
        ".#..",
        ".#..",
        ".#..",
        "....",
    ]),
    ("DIGIT_2", [
        "....",
        "##..",
        "..#.",
        "..#.",
        ".#..",
        "#...",
        "###.",
        "....",
    ]),
    ("DIGIT_3", [
        "....",
        "##..",
        "..#.",
        "..#.",
        ".#..",
        "..#.",
        "##..",
        "....",
    ]),
    ("DIGIT_4", [
        "....",
        "..#.",
        ".##.",
        "#.#.",
        "###.",
        "..#.",
        "..#.",
        "....",
    ]),
]

TILE_W, TILE_H = 8, 8
DEFAULT_OUT = os.path.join(REPO_ROOT, "gfx", "ppilot_map_tiles.png")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--force", action="store_true",
                    help="overwrite an existing sheet")
    args = ap.parse_args()

    try:
        from PIL import Image
    except ImportError:
        sys.exit("make_map_tiles_draft: Pillow is required (pip install pillow)")

    if os.path.exists(args.out) and not args.force:
        sys.exit(
            f"make_map_tiles_draft: {args.out} already exists.\n"
            "The sheet is the source of truth once drawn; pass --force to "
            "discard it and start over."
        )

    cells = TILES + [(name, art, None, None) for name, art in DIGITS]
    im = Image.new("RGB", (len(cells) * TILE_W, TILE_H), PALETTE_RGB[GREEN])
    px = im.load()

    for i, (name, art, color_a, color_b) in enumerate(cells):
        if len(art) != TILE_H:
            sys.exit(f"make_map_tiles_draft: {name} has {len(art)} rows, need {TILE_H}")
        for y, row in enumerate(art):
            if len(row) != TILE_W // 2:
                sys.exit(
                    f"make_map_tiles_draft: {name} row {y} is {len(row)} wide, "
                    f"need {TILE_W // 2}"
                )
            for mx, ch in enumerate(row):
                color = {
                    ".": GREEN,
                    "A": color_a,
                    "B": color_b,
                    "#": WHITE,
                }[ch]
                rgb = PALETTE_RGB[color]
                # A multicolor pixel is two screen pixels wide.
                px[i * TILE_W + mx * 2, y] = rgb
                px[i * TILE_W + mx * 2 + 1, y] = rgb

    im.save(args.out)
    print(f"make_map_tiles_draft: wrote {os.path.relpath(args.out, REPO_ROOT)} "
          f"({len(cells)} tiles, {im.size[0]}x{im.size[1]})")


if __name__ == "__main__":
    main()
