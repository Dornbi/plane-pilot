#!/usr/bin/env python3
"""
Renders what the map view will look like, to out/map_preview.png.

This is a verification tool, not part of the build. It composites the
generated tile tables over kWorldMap exactly the way map_enter() will --
including the grid parity, the 180 degree rotation and the navpoint digit
stencils -- so the tile sheet can be judged as a whole map rather than as
nineteen 8x8 thumbnails.

It parses c64o/world_map.cc to get the world, which the *build* pipeline
deliberately never does: the C64 reads kWorldMap[][] out of RAM at runtime,
so there is nothing to keep in sync. A preview is allowed to cheat because
being wrong here costs a wrong picture, not a wrong ROM.

Also writes out/map_tiles_preview.png, the tile sheet at 8x with labels.
"""

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

from lib.c64_colors import PALETTE_RGB, to_indexed  # noqa: E402
from tools.generate_map_tiles import TILE_NAMES, DIGIT_NAMES  # noqa: E402

BACKGROUND = 5   # green, bit pair 00
OVERLAY = 1      # white, bit pair 01

MAP_W, MAP_H = 32, 16
ORIGIN_COL, ORIGIN_ROW = 4, 4     # character cell the map starts at
SCREEN_W, SCREEN_H = 320, 200

# world_map.cc's local abbreviations -> tile index within its group.
DOT_ORDER = ["DK_", "DW_", "DC_", "DB_", "DY_"]
OBJ_ORDER = ["RWY", "FLD", "FLS", "FYW", "FYS", "PND", "LAK", "TWN", "CTY"]


def _parse_world_map(path):
    body = open(path).read().split("const WorldMapType kWorldMap")[1]
    rows = [
        [t.strip() for t in r.split(",") if t.strip()]
        for r in re.findall(r"\{([^{}]*)\}", body)
    ]
    rows = [r for r in rows if len(r) == MAP_W]
    if len(rows) != MAP_H:
        sys.exit(f"render_map_preview: parsed {len(rows)} rows, expected {MAP_H}")
    return rows


def _parse_tables(path):
    src = open(path).read()

    def block(marker):
        rest = src.split(marker)[1].split("{", 1)[1]
        depth, out = 1, ""
        for ch in rest:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    break
            out += ch
        return out

    rows_txt = block("kMapTileRows[8][kMapTileCount] =")
    rows = [
        [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", line)]
        for line in re.findall(r"\{([^{}]*)\}", rows_txt)
    ]
    lo = [int(x) for x in re.findall(r"^\s*(\d+),", block("kMapTileLo[kMapTileCount] ="), re.M)]
    col = [int(x) for x in re.findall(r"^\s*(\d+),", block("kMapTileCol[kMapTileCount] ="), re.M)]
    digits_txt = block("kMapDigitMask[kMapDigitCount][8] =")
    digits = [
        [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", line)]
        for line in re.findall(r"\{([^{}]*)\}", digits_txt)
    ]
    return rows, lo, col, digits


def _tile_index(cell, row, col_):
    if cell == "D__":
        # The dotted grid has a two-cell period; the C64 does the same
        # ((row & 1) << 1) | (col & 1).
        return ((row & 1) << 1) | (col_ & 1)
    if cell in DOT_ORDER:
        return TILE_NAMES.index("DOT_BLACK") + DOT_ORDER.index(cell)
    if cell in OBJ_ORDER:
        return TILE_NAMES.index("RUNWAY") + OBJ_ORDER.index(cell)
    sys.exit(f"render_map_preview: unknown cell type {cell!r}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--navpoints", default="4,8;12,24",
                    help="semicolon separated row,col map cells to number 1..4")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "out"))
    args = ap.parse_args()

    try:
        from PIL import Image
    except ImportError:
        sys.exit("render_map_preview: Pillow is required (pip install pillow)")

    world = _parse_world_map(os.path.join(REPO_ROOT, "c64o", "world_map.cc"))
    rows, lo, col, digits = _parse_tables(os.path.join(REPO_ROOT, "c64o", "mapdefs.cc"))

    # Bitmap in the C64's own layout, so the address arithmetic gets exercised.
    bitmap = bytearray(8000)
    tile_of = {}
    for r in range(MAP_H):
        for c in range(MAP_W):
            idx = _tile_index(world[r][c], r, c)
            sr, sc = (MAP_H - 1) - r, (MAP_W - 1) - c      # 180 degree rotation
            base = ((ORIGIN_ROW + sr) * 40 + (ORIGIN_COL + sc)) * 8
            tile_of[(ORIGIN_ROW + sr, ORIGIN_COL + sc)] = idx
            for y in range(8):
                bitmap[base + y] = rows[y][idx]

    # Navpoint digits, drawn into the overlay layer the way map_enter() will:
    # clear the cell's overlay pairs, then punch the stencil in as 01.
    for n, spec in enumerate(p for p in args.navpoints.split(";") if p):
        if n >= len(digits):
            break
        r, c = (int(v) for v in spec.split(","))
        sr, sc = (MAP_H - 1) - r, (MAP_W - 1) - c
        base = ((ORIGIN_ROW + sr) * 40 + (ORIGIN_COL + sc)) * 8
        for y in range(8):
            mask = digits[n][y]
            bitmap[base + y] = (bitmap[base + y] & ~mask) | (mask & 0x55)

    im = Image.new("RGB", (SCREEN_W, SCREEN_H), PALETTE_RGB[0])
    px = im.load()
    for cr in range(ORIGIN_ROW, ORIGIN_ROW + MAP_H):
        for cc in range(ORIGIN_COL, ORIGIN_COL + MAP_W):
            idx = tile_of[(cr, cc)]
            base = (cr * 40 + cc) * 8
            for y in range(8):
                b = bitmap[base + y]
                for mx in range(4):
                    pair = (b >> (2 * (3 - mx))) & 3
                    ci = {0: BACKGROUND, 1: OVERLAY, 2: lo[idx], 3: col[idx]}[pair]
                    rgb = PALETTE_RGB[ci]
                    x, y0 = cc * 8 + mx * 2, cr * 8 + y
                    px[x, y0] = rgb
                    px[x + 1, y0] = rgb

    os.makedirs(args.out_dir, exist_ok=True)
    out_map = os.path.join(args.out_dir, "map_preview.png")
    # Indexed with the C64 palette, like the source art: nearest-neighbour
    # scaling introduces no new colors, so the preview stays a picture of
    # exactly the 16 the hardware has.
    to_indexed(im.resize((SCREEN_W * args.scale, SCREEN_H * args.scale),
                         Image.NEAREST)).save(out_map)

    # Tile sheet, blown up, in table order.
    names = TILE_NAMES + DIGIT_NAMES
    zoom, pad = 8, 2
    sheet = Image.open(os.path.join(REPO_ROOT, "gfx", "ppilot_map_tiles.png"))
    sheet = sheet.convert("RGB")
    cols = 11
    rows_n = (len(names) + cols - 1) // cols
    cell = 8 * zoom + pad
    # Dark gray rather than an arbitrary near-black: the sheet is saved
    # indexed on the C64 palette, so the padding has to be a C64 color too.
    grid = Image.new("RGB", (cols * cell + pad, rows_n * cell + pad),
                     PALETTE_RGB[11])
    for i in range(len(names)):
        tile = sheet.crop((i * 8, 0, i * 8 + 8, 8)).resize((8 * zoom, 8 * zoom),
                                                           Image.NEAREST)
        gx, gy = i % cols, i // cols
        grid.paste(tile, (pad + gx * cell, pad + gy * cell))
    out_tiles = os.path.join(args.out_dir, "map_tiles_preview.png")
    to_indexed(grid).save(out_tiles)

    print(f"render_map_preview: {os.path.relpath(out_map, REPO_ROOT)}")
    print(f"render_map_preview: {os.path.relpath(out_tiles, REPO_ROOT)}")
    print("  tile order: " + ", ".join(f"{i}:{n}" for i, n in enumerate(names)))


if __name__ == "__main__":
    main()
