#!/usr/bin/env python3
"""
Generates c64o/mapdefs.{cc,h} from gfx/ppilot_map_tiles.png.

The map view (docs/map.md) draws the 32 x 16 world as a multicolor bitmap by
stamping one pre-rendered 4 x 8 tile per cell. This tool turns the tile sheet
into the tables the C64 code compiles in, so the art stays in GIMP and the C64
reads kWorldMap[][] at runtime -- Python never parses world_map.cc.

Tile sheet format
-----------------
A single row of tiles, 8 screen pixels wide and 8 tall each, tile i at x=i*8.
A multicolor pixel is 2 screen pixels wide, so each tile is 4 x 8 in the C64's
terms. Colors must come from the C64 palette (lib/c64_colors.py).

Color to bit pair, per the map's palette:

    green   -> 00   the global background ($D021), free in every cell
    white   -> 01   the overlay layer, reserved for the flight path and the
                    navpoint digits; an error anywhere except a digit tile
    other   -> 10   first non-reserved color seen in the tile
    other   -> 11   second non-reserved color seen in the tile

Two free colors per tile is the whole budget: 00 and 01 are pinned screen-wide
so that the overlay can be drawn anywhere without a per-cell negotiation. A
tile needing a third color is a build error rather than a surprise on screen.

Digit tiles are different: they are stencils, not art. Only their white pixels
matter, and they are emitted as one mask byte per row for the overlay to punch
into whatever the cell already holds.

Output
------
    kMapTileRows[8][kMapTileCount]   transposed -- row outer, tile inner, so
                                     the compositor's inner loop is 8 indexed
                                     loads sharing one index register with no
                                     multiply and no pointer arithmetic
    kMapTileLo[kMapTileCount]        color for bit pair 10 (screen low nibble)
    kMapTileCol[kMapTileCount]       color for bit pair 11 (color RAM)
    kMapDigitMask[kMapDigitCount][8] overlay stencils for '1'..'4'
"""

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

from lib.c64_colors import PALETTE_RGB  # noqa: E402

TILE_W = 8   # screen pixels; 4 multicolor pixels
TILE_H = 8

# Bit pair 00 and 01 are pinned screen-wide. Everything else is per tile.
BACKGROUND = 5  # green   -> 00
OVERLAY = 1     # white   -> 01

DEFAULT_SHEET = os.path.join(REPO_ROOT, "gfx", "ppilot_map_tiles.png")
DEFAULT_OUT_CC = os.path.join(REPO_ROOT, "c64o", "mapdefs.cc")
DEFAULT_OUT_H = os.path.join(REPO_ROOT, "c64o", "mapdefs.h")

# Tile order. The C64 maps a WorldMapType to an index with
#   idx = type < kWorldMapObjStart ? kMapTileDot + (type - MAP_DOT_BLACK)
#                                  : kMapTileObj + (type - MAP_OBJ_RUNWAY)
# and empty ground (MAP_DOT_GROUND) picks one of the four grid variants by
# (row & 1) << 1 | (col & 1). Keeping that arithmetic shift-only is why the
# groups are contiguous and in world.h's order.
TILE_NAMES = [
    # Four gridline variants: the concept's dotted grid has a two-cell period,
    # so the tile depends on the parity of the cell's row and column.
    "GRID_EVEN_EVEN",
    "GRID_EVEN_ODD",
    "GRID_ODD_EVEN",
    "GRID_ODD_ODD",
    # Dot clusters, MAP_DOT_BLACK .. MAP_DOT_YELLOW (world.h 2..6).
    "DOT_BLACK",
    "DOT_WHITE",
    "DOT_CYAN",
    "DOT_BLUE",
    "DOT_YELLOW",
    # Polygon objects, MAP_OBJ_RUNWAY .. MAP_OBJ_CITY (world.h 16..24).
    "RUNWAY",
    "FIELD",
    "FIELD_SPARSE",
    "FIELD_YELLOW",
    "FIELD_YELLOW_SPARSE",
    "POND",
    "LAKE",
    "TOWN",
    "CITY",
]
DIGIT_NAMES = ["DIGIT_1", "DIGIT_2", "DIGIT_3", "DIGIT_4"]

RGB_TO_INDEX = {rgb: i for i, rgb in PALETTE_RGB.items()}


def _read_sheet(path):
    """Returns the sheet as a list of tiles, each an 8x4 grid of color indices."""
    try:
        from PIL import Image
    except ImportError:
        sys.exit("generate_map_tiles: Pillow is required (pip install pillow)")

    if not os.path.exists(path):
        sys.exit(
            f"generate_map_tiles: {path} not found.\n"
            "Run tools/make_map_tiles_draft.py to lay down a starting sheet."
        )

    im = Image.open(path).convert("RGB")
    width, height = im.size
    if height != TILE_H:
        sys.exit(
            f"generate_map_tiles: sheet must be {TILE_H} pixels tall, got {height}"
        )
    expected = len(TILE_NAMES) + len(DIGIT_NAMES)
    if width != expected * TILE_W:
        sys.exit(
            f"generate_map_tiles: sheet must be {expected} tiles "
            f"({expected * TILE_W} px) wide, got {width} px "
            f"({width / TILE_W:g} tiles)"
        )

    px = im.load()
    tiles = []
    for t in range(expected):
        rows = []
        for y in range(TILE_H):
            row = []
            # A multicolor pixel is two screen pixels wide. Read the left one
            # and require the right to match, so a sheet edited at 1x without
            # noticing gets caught here instead of silently losing half the art.
            for mx in range(TILE_W // 2):
                x = t * TILE_W + mx * 2
                left, right = px[x, y], px[x + 1, y]
                if left != right:
                    sys.exit(
                        f"generate_map_tiles: tile {t} "
                        f"({_tile_label(t)}) row {y}: multicolor pixel {mx} is "
                        f"two different colors ({left} and {right}). Every "
                        "pixel must be drawn 2 screen pixels wide."
                    )
                if left not in RGB_TO_INDEX:
                    sys.exit(
                        f"generate_map_tiles: tile {t} ({_tile_label(t)}) "
                        f"row {y} pixel {mx}: {left} is not a C64 palette color"
                    )
                row.append(RGB_TO_INDEX[left])
            rows.append(row)
        tiles.append(rows)
    return tiles


def _tile_label(index):
    names = TILE_NAMES + DIGIT_NAMES
    return names[index] if index < len(names) else f"#{index}"


def _encode_tile(rows, label):
    """Art tile -> (8 packed bytes, lo color, colram color)."""
    extra = []
    for y, row in enumerate(rows):
        for x, color in enumerate(row):
            if color == BACKGROUND:
                continue
            if color == OVERLAY:
                sys.exit(
                    f"generate_map_tiles: {label} row {y} pixel {x} uses white, "
                    "which is reserved for the overlay layer (flight path and "
                    "navpoint digits). Pick another color."
                )
            if color not in extra:
                extra.append(color)
    if len(extra) > 2:
        names = ", ".join(str(c) for c in extra)
        sys.exit(
            f"generate_map_tiles: {label} uses {len(extra)} colors besides "
            f"green and white ({names}); the budget is 2 per tile "
            "(bit pairs 10 and 11)."
        )

    pair_of = {BACKGROUND: 0}
    for i, color in enumerate(extra):
        pair_of[color] = 2 + i  # 10 then 11

    packed = []
    for row in rows:
        byte = 0
        for x, color in enumerate(row):
            byte |= pair_of[color] << (2 * (3 - x))
        packed.append(byte)

    lo = extra[0] if len(extra) > 0 else 0
    col = extra[1] if len(extra) > 1 else 0
    return packed, lo, col


def _encode_digit(rows, label):
    """Digit stencil -> 8 mask bytes with 11 in each ink pair."""
    masks = []
    for y, row in enumerate(rows):
        mask = 0
        for x, color in enumerate(row):
            if color == OVERLAY:
                mask |= 3 << (2 * (3 - x))
            elif color != BACKGROUND:
                sys.exit(
                    f"generate_map_tiles: {label} row {y} pixel {x} is color "
                    f"{color}; digit stencils may only use white (ink) and "
                    "green (background)."
                )
        masks.append(mask)
    return masks


def _hex_row(values):
    return ", ".join(f"0x{v:02X}" for v in values)


def _write_header(path, tiles, digits):
    with open(path, "w") as f:
        f.write(
            "// Generated by tools/generate_map_tiles.py -- do not edit.\n"
            "// Source art: gfx/ppilot_map_tiles.png\n"
            "\n"
            "#ifndef MAPDEFS_H\n"
            "#define MAPDEFS_H\n"
            "\n"
            "#include <stdint.h>\n"
            "\n"
            f"static const uint8_t kMapTileCount = {len(tiles)};\n"
            f"static const uint8_t kMapDigitCount = {len(digits)};\n"
            "\n"
            "// Group bases, so a WorldMapType becomes a tile index with\n"
            "// shifts and adds only. Empty ground (MAP_DOT_GROUND) selects a\n"
            "// grid variant by ((row & 1) << 1) | (col & 1).\n"
        )
        f.write(
            "static const uint8_t kMapTileGrid = 0;\n"
            f"static const uint8_t kMapTileDot = {TILE_NAMES.index('DOT_BLACK')};\n"
            f"static const uint8_t kMapTileObj = {TILE_NAMES.index('RUNWAY')};\n"
            "\n"
        )
        for i, name in enumerate(TILE_NAMES):
            f.write(f"static const uint8_t kMapTile{_camel(name)} = {i};\n")
        f.write(
            "\n"
            "// Transposed: row index outer, tile index inner, so the\n"
            "// compositor does eight indexed loads off one index register.\n"
            "extern const uint8_t kMapTileRows[8][kMapTileCount];\n"
            "// Bit pair 10, written to the screen RAM low nibble.\n"
            "extern const uint8_t kMapTileLo[kMapTileCount];\n"
            "// Bit pair 11, written to color RAM.\n"
            "extern const uint8_t kMapTileCol[kMapTileCount];\n"
            "\n"
            "// Overlay stencils for navpoints 1..4. Each byte holds 11 in\n"
            "// every ink pair; the draw is\n"
            "//   dst[r] = (dst[r] & ~mask[r]) | (mask[r] & 0x55)\n"
            "// which deposits 01 into the ink pairs and leaves the object\n"
            "// art in every other pair untouched.\n"
            "extern const uint8_t kMapDigitMask[kMapDigitCount][8];\n"
            "\n"
            '#pragma compile("mapdefs.cc")\n'
            "\n"
            "#endif // MAPDEFS_H\n"
        )


def _camel(name):
    return "".join(part.capitalize() for part in name.split("_"))


def _write_source(path, tiles, digits):
    packed = [t[0] for t in tiles]
    los = [t[1] for t in tiles]
    cols = [t[2] for t in tiles]

    with open(path, "w") as f:
        f.write(
            "// Generated by tools/generate_map_tiles.py -- do not edit.\n"
            "// Source art: gfx/ppilot_map_tiles.png\n"
            "\n"
            '#include "mapdefs.h"\n'
            "\n"
            "// clang-format off\n"
            "const uint8_t kMapTileRows[8][kMapTileCount] = {\n"
        )
        for row in range(8):
            f.write(f"    {{{_hex_row([p[row] for p in packed])}}}, // row {row}\n")
        f.write("};\n\n")

        f.write("const uint8_t kMapTileLo[kMapTileCount] = {\n")
        for name, value in zip(TILE_NAMES, los):
            f.write(f"    {value:2d}, // {name}\n")
        f.write("};\n\n")

        f.write("const uint8_t kMapTileCol[kMapTileCount] = {\n")
        for name, value in zip(TILE_NAMES, cols):
            f.write(f"    {value:2d}, // {name}\n")
        f.write("};\n\n")

        f.write("const uint8_t kMapDigitMask[kMapDigitCount][8] = {\n")
        for name, mask in zip(DIGIT_NAMES, digits):
            f.write(f"    {{{_hex_row(mask)}}}, // {name}\n")
        f.write("};\n// clang-format on\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sheet", default=DEFAULT_SHEET)
    ap.add_argument("--out-cc", default=DEFAULT_OUT_CC)
    ap.add_argument("--out-h", default=DEFAULT_OUT_H)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    raw = _read_sheet(args.sheet)
    art = [
        _encode_tile(raw[i], TILE_NAMES[i]) for i in range(len(TILE_NAMES))
    ]
    digits = [
        _encode_digit(raw[len(TILE_NAMES) + i], DIGIT_NAMES[i])
        for i in range(len(DIGIT_NAMES))
    ]

    _write_header(args.out_h, art, digits)
    _write_source(args.out_cc, art, digits)

    if not args.quiet:
        total = len(art) * 10 + len(digits) * 8
        print(f"generate_map_tiles: {len(art)} tiles, {len(digits)} digits")
        print(f"  {os.path.relpath(args.out_h, REPO_ROOT)}")
        print(f"  {os.path.relpath(args.out_cc, REPO_ROOT)}")
        print(f"  {total} bytes of C64 data")


if __name__ == "__main__":
    main()
