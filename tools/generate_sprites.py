import argparse
import math
import os
import struct
import zlib

# Repo root, so outputs land in the right place regardless of the
# current working directory.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _read_concept_png(png_path):
    with open(png_path, "rb") as f:
        data = f.read()

    assert data[:8] == b"\x89PNG\r\n\x1a\n", "Invalid PNG header"
    idx = 8
    idat = bytearray()
    w, h = 0, 0
    while idx < len(data):
        length, ctype = struct.unpack(">I4s", data[idx : idx + 8])
        cdata = data[idx + 8 : idx + 8 + length]
        idx += 8 + length + 4
        if ctype == b"IHDR":
            w, h, bitd, colort, comp, filt, inter = struct.unpack(
                ">IIBBBBB", cdata
            )
        elif ctype == b"IDAT":
            idat.extend(cdata)
        elif ctype == b"IEND":
            break

    decomp = zlib.decompress(idat)
    pos = 0
    stride = w
    img = []
    for y in range(h):
        filter_type = decomp[pos]
        pos += 1
        line = bytearray(decomp[pos : pos + stride])
        pos += stride
        if filter_type == 0:
            pass
        elif filter_type == 1:
            for x in range(1, stride):
                line[x] = (line[x] + line[x - 1]) & 0xFF
        elif filter_type == 2:
            if y > 0:
                for x in range(stride):
                    line[x] = (line[x] + img[y - 1][x]) & 0xFF
        elif filter_type == 3:
            for x in range(stride):
                left = line[x - 1] if x >= 1 else 0
                up = img[y - 1][x] if y > 0 else 0
                line[x] = (line[x] + (left + up) // 2) & 0xFF
        elif filter_type == 4:
            for x in range(stride):
                a = line[x - 1] if x >= 1 else 0
                b = img[y - 1][x] if y > 0 else 0
                c = img[y - 1][x - 1] if (y > 0 and x >= 1) else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        img.append(line)
    return img, w, h


def _grid_to_sprite_bytes(grid):
    sprite_bytes = bytearray(64)
    for y in range(21):
        row_bits = grid[y]
        b0 = b1 = b2 = 0
        for k in range(8):
            if row_bits[k]:
                b0 |= 1 << (7 - k)
            if row_bits[k + 8]:
                b1 |= 1 << (7 - k)
            if row_bits[k + 16]:
                b2 |= 1 << (7 - k)
        sprite_bytes[y * 3] = b0
        sprite_bytes[y * 3 + 1] = b1
        sprite_bytes[y * 3 + 2] = b2
    return sprite_bytes


# --- The cloud dither lattice ------------------------------------------------
#
# Clouds are drawn as a white-and-transparent checkerboard, and docs/clouds.md
# §4 is the reason the phase of that checkerboard is a property of this script
# rather than of the concept art. Overlapping cloud sprites only read as one
# cloud if their transparent pixels line up; out of phase, the rear sprite fills
# the front one's holes and the group turns into a solid white lump with a hard
# edge where the overlap stops. Measured on these bitmaps: 48% coverage in the
# overlap when aligned, 82% when not.
#
# The rule is that a set pixel at bitmap column c, row r satisfies
#
#     (c + r + row_offset) % 2 == 0
#
# where row_offset is 0 for a single sprite and for the upper block of a
# stacked pair, and KSPRITE_ROWS for the lower block - which carries the phase
# across the 21-line seam, since 21 is odd and would otherwise flip it.
#
# The concept art already satisfies this, so ANDing it in changes nothing today
# (tests/test_spritedef.py checks the output still matches the checked-in
# spritedef.bin byte for byte). What it buys is that the art no longer has to:
# it can be redrawn as a plain silhouette, or at a different resolution, and the
# phase survives. Before this, extracting one pixel further left would have
# inverted a single rung's phase silently.

KSPRITE_ROWS = 21
KSPRITE_COLS = 24


def on_dither_lattice(c, r, row_offset=0):
    """True if a set pixel is allowed at this position. See above."""
    return (c + r + row_offset) % 2 == 0


def assert_dither_phase(label, grid, row_offset=0):
    """Fails the build if any set pixel is off the lattice."""
    for r, row in enumerate(grid):
        for c, bit in enumerate(row):
            if bit and not on_dither_lattice(c, r, row_offset):
                raise SystemExit(
                    "generate_sprites: %s has a set pixel off the dither "
                    "lattice at column %d, row %d (row_offset %d).\n"
                    "  Every cloud pixel must satisfy (c + r + row_offset) %% 2 "
                    "== 0, or overlapping\n"
                    "  clouds fill each other in - see docs/clouds.md §4. If "
                    "the concept art moved,\n"
                    "  the AND in generate_cloud_sprites() should have handled "
                    "it, so reaching this\n"
                    "  means the lattice itself was bypassed."
                    % (label, c, r, row_offset)
                )


def assert_pivots_consistent(label, metas):
    """Every rung of a ladder must share one pivot.

    Not a dither constraint - the lattice is anchored to the sprite's top left,
    which sprites_stack_add() snaps, so the pivot does not enter it. This guards
    something else: the rungs are a size ladder the renderer steps up and down
    as a cloud approaches, and a rung whose pivot disagreed with its neighbours
    would make the blob jump sideways at the step instead of just growing.
    """
    pivots = {(m["pivot_x"], m["pivot_y"]) for m in metas}
    if len(pivots) != 1:
        raise SystemExit(
            "generate_sprites: %s rungs disagree on the pivot: %s.\n"
            "  All rungs of one ladder must share it, or a cloud jumps when it "
            "changes size." % (label, sorted(pivots))
        )


# --- The orientation indicator ----------------------------------------------
#
# The fixed reference mark in the middle of the viewport (c64o/sprites.h). It
# takes the first block of the blob, bitmap 80, which is the slot the cloud size
# ladder's rung 0 used to hold and which no cloud can reach: clouds.cc draws a
# distant group as one blob a rung *larger* than its own, so the smallest row it
# can index is rung 1's (docs/clouds.md §3.5). There is no other slot to take -
# the blob's 48 blocks are full from the cloud base to the last needle - so
# claiming the dead one is the only way this mark gets drawn at all.
#
# Two wings and a gap on the pivot row, which is what the horizon is read
# against: level flight puts the horizon along the bar and through the gap.
KSPRITE_ORIENT_WING = 8
KSPRITE_ORIENT_ROW = 10


def generate_orient_sprite(bitmap_offset):
    """The orientation indicator: xxxxxxxx--------xxxxxxxx on the pivot row."""
    grid = [[0 for _ in range(KSPRITE_COLS)] for _ in range(KSPRITE_ROWS)]
    for x in range(KSPRITE_ORIENT_WING):
        grid[KSPRITE_ORIENT_ROW][x] = 1
        grid[KSPRITE_ORIENT_ROW][KSPRITE_COLS - 1 - x] = 1

    # The gap is what is left over, and it has to stay a gap: a wing wide enough
    # to close it would leave a solid bar with nothing to centre by.
    gap = KSPRITE_COLS - 2 * KSPRITE_ORIENT_WING
    assert gap > 0, "the orientation indicator's wings meet in the middle"

    meta = {
        "pivot_x": 12,
        "pivot_y": KSPRITE_ORIENT_ROW,
        "bitmap_idx": bitmap_offset,
        "label": "Orientation indicator",
    }
    return _grid_to_sprite_bytes(grid), meta, grid


def generate_cloud_sprites(cloud_base_offset):
    png_path = os.path.join(REPO_ROOT, "gfx", "ppilot_clouds_concept.png")
    img, w, h = _read_concept_png(png_path)

    # 1-sprite cloud configurations from upper set in concept PNG.
    #
    # The ladder starts at 5x9, not at the 3x5 the concept art also carries.
    # That rung is unreachable - a collapsed group draws one rung larger than
    # its own, so rung 0 is never indexed (docs/clouds.md §3.5) - and its block
    # is the orientation indicator's. kSpriteDefCloudRung keeps a dead row 0
    # anyway, so a rung number is still a row number in clouds.cc.
    one_sprite_defs = [
        ("5x9", (46, 55), (64, 72), 5, 9),
        ("7x13", (68, 81), (62, 74), 7, 13),
        ("9x17", (90, 107), (60, 76), 9, 17),
        ("11x21", (112, 133), (58, 78), 11, 21),
    ]

    cloud1_data = []
    cloud1_meta = []
    cloud1_bits = []

    for i, (name, (minx, maxx), (miny, maxy), w_world, h_lines) in enumerate(
        one_sprite_defs
    ):
        grid = [[0 for _ in range(24)] for _ in range(21)]
        start_sx = 12 - w_world // 2
        start_sy = 10 - h_lines // 2
        for dy in range(h_lines):
            img_y = miny + dy
            for dx in range(w_world):
                img_x = minx + dx * 2
                # Coverage from the art, phase from the lattice.
                covered = img[img_y][img_x] == 1 or img[img_y][img_x + 1] == 1
                sx = start_sx + dx
                sy = start_sy + dy
                grid[sy][sx] = 1 if covered and on_dither_lattice(sx, sy) else 0

        bidx = cloud_base_offset + i
        cloud1_data.append(_grid_to_sprite_bytes(grid))
        cloud1_bits.append(grid)
        cloud1_meta.append(
            {
                "width": w_world,
                "height": h_lines,
                "bitmap_idx": bidx,
                "pivot_x": 12,
                "pivot_y": 10,
                "label": f"Cloud 1-Sprite {name}",
            }
        )

    # 2-sprite cloud configurations from upper set in concept PNG
    two_sprite_defs = [
        ("13x25", (26, 51), (87, 111), 13, 25),
        ("15x29", (72, 101), (85, 113), 15, 29),
        ("17x33", (118, 151), (83, 115), 17, 33),
        ("19x37", (164, 201), (81, 117), 19, 37),
        ("21x41", (210, 251), (79, 119), 21, 41),
    ]

    cloud2_data = []
    cloud2_meta = []
    cloud2_bits = []

    cloud2_base_offset = cloud_base_offset + len(one_sprite_defs)  # 81 + 4 = 85

    for i, (name, (minx, maxx), (miny, maxy), w_world, h_lines) in enumerate(
        two_sprite_defs
    ):
        grid_top = [[0 for _ in range(24)] for _ in range(21)]
        grid_bot = [[0 for _ in range(24)] for _ in range(21)]
        start_sx = 12 - w_world // 2

        # 42-line stack: top sprite = concept Y 79..99, bot sprite = concept Y 100..120
        # The upper block carries no row offset; the lower one carries the
        # 21 lines between them, so the checkerboard runs unbroken across the
        # seam of a 42-row stack.
        for sy in range(KSPRITE_ROWS):
            cy = 79 + sy
            if miny <= cy <= maxy:
                for dx in range(w_world):
                    img_x = minx + dx * 2
                    covered = img[cy][img_x] == 1 or img[cy][img_x + 1] == 1
                    sx = start_sx + dx
                    grid_top[sy][sx] = (
                        1 if covered and on_dither_lattice(sx, sy) else 0
                    )

        for sy in range(KSPRITE_ROWS):
            cy = 100 + sy
            if miny <= cy <= maxy:
                for dx in range(w_world):
                    img_x = minx + dx * 2
                    covered = img[cy][img_x] == 1 or img[cy][img_x + 1] == 1
                    sx = start_sx + dx
                    grid_bot[sy][sx] = (
                        1
                        if covered and on_dither_lattice(sx, sy, KSPRITE_ROWS)
                        else 0
                    )

        top_idx = cloud2_base_offset + 2 * i
        bot_idx = cloud2_base_offset + 2 * i + 1

        cloud2_data.append(_grid_to_sprite_bytes(grid_top))
        cloud2_data.append(_grid_to_sprite_bytes(grid_bot))
        cloud2_bits.append((grid_top, grid_bot))
        cloud2_meta.append(
            {
                "width": w_world,
                "height": h_lines,
                "top_bitmap_idx": top_idx,
                "bot_bitmap_idx": bot_idx,
                "pivot_x": 12,
                "pivot_y": 20,
                "label": f"Cloud 2-Sprite {name}",
            }
        )

    # Belt and braces: the AND above is what puts the pixels on the lattice,
    # so this can only fire if someone edits around it. It costs nothing and it
    # is the difference between a silent regression and a failed build.
    for i, grid in enumerate(cloud1_bits):
        assert_dither_phase(cloud1_meta[i]["label"], grid)
    for i, (g_top, g_bot) in enumerate(cloud2_bits):
        assert_dither_phase(cloud2_meta[i]["label"] + " (top)", g_top)
        assert_dither_phase(
            cloud2_meta[i]["label"] + " (bottom)", g_bot, KSPRITE_ROWS
        )
    assert_pivots_consistent("Cloud 1-sprite", cloud1_meta)
    assert_pivots_consistent("Cloud 2-sprite", cloud2_meta)

    return (
        cloud1_data,
        cloud1_meta,
        cloud1_bits,
        cloud2_data,
        cloud2_meta,
        cloud2_bits,
    )


def generate_arm_set(arm_length, bitmap_offset, angles_tot):
    centered_x = 12.0
    centered_y = 10.0
    angles_step = 360.0 / angles_tot
    num_unique_bitmaps = angles_tot // 2

    sprites_data = []
    sprites_bits = []

    # Generate 32 unique bitmaps for angles -90 to 84.375
    for i in range(num_unique_bitmaps):
        angle_deg = (i * angles_step) - 90
        angle_rad = math.radians(angle_deg)

        dx = math.cos(angle_rad)
        dy = math.sin(angle_rad)
        nx = -dy
        ny = dx

        p_x = centered_x + 0.5
        p_y = centered_y + 0.5

        grid = [[0 for _ in range(24)] for _ in range(21)]

        for y in range(21):
            for x in range(24):
                vx = x - p_x
                vy = y - p_y
                proj = vx * dx + vy * dy
                dist = abs(vx * nx + vy * ny)

                if abs(proj) <= arm_length / 2 + 0.5 and dist <= 1.0:
                    grid[y][x] = 1

        sprites_data.append(_grid_to_sprite_bytes(grid))
        sprites_bits.append(grid)

    sprites_meta = []
    for i in range(angles_tot):
        angle_deg = i * angles_step
        base_angle_deg = (angle_deg % 180.0) - 90.0
        bitmap_idx = (i % num_unique_bitmaps) + bitmap_offset

        angle_rad = math.radians(base_angle_deg)
        dx = math.cos(angle_rad)
        dy = math.sin(angle_rad)

        if i < num_unique_bitmaps:
            pv_x = centered_x - (arm_length / 2) * dx
            pv_y = centered_y - (arm_length / 2) * dy
        else:
            pv_x = centered_x + (arm_length / 2) * dx
            pv_y = centered_y + (arm_length / 2) * dy

        sprites_meta.append(
            {
                "pivot_x": int(round(pv_x)),
                "pivot_y": int(round(pv_y)),
                "bitmap_idx": bitmap_idx,
                "label": f"{angle_deg} deg",
            }
        )

    return sprites_data, sprites_meta, sprites_bits


def generate_sun_sprite(bitmap_offset):
    grid = [[0 for _ in range(24)] for _ in range(21)]
    center_x = 12.0
    center_y = 10.0
    radius_sq = 10.4 * 10.4
    for y in range(21):
        for x in range(24):
            dx = x - center_x
            dy = y - center_y
            if dx * dx + dy * dy <= radius_sq:
                grid[y][x] = 1

    sprite_bytes = _grid_to_sprite_bytes(grid)
    meta = {
        "pivot_x": 12,
        "pivot_y": 10,
        "bitmap_idx": bitmap_offset,
        "label": "Sun",
    }
    return sprite_bytes, meta, grid


# --- The title screen aircraft ----------------------------------------------
#
# The one sprite set in this program that is multicolour, and the only one that
# never appears during flight. docs/sprite_objects.md §0 rules multicolour out
# for world objects, and for good reasons - a cloud's checkerboard dither
# collapses into 2-pixel blocks, and an aircraft silhouette at that scale is
# three straight lines that need the full horizontal resolution. Neither
# argument reaches the title screen: there is one aircraft, it is 48 x 42
# screen pixels rather than a few, and it is standing art rather than something
# drawn per frame. Three colours over four blocks buys a readable aeroplane
# where hires would buy a grey cross.
#
# The crop is the second aircraft in gfx/ppilot_sprites_concept.png, the one
# flying towards the bottom left, and it was drawn to land on exactly 2 x 2
# sprites: 48 screen pixels wide is two multicolour sprites (12 double-wide
# pixels each) and 42 rows is two sprites tall.
KTITLE_PNG = "ppilot_sprites_concept.png"
KTITLE_CROP_X = 88
KTITLE_CROP_Y = 16
KTITLE_COLS = 2
KTITLE_ROWS = 2

# A multicolour sprite is still 24 screen pixels wide; the pixels are just
# twice as wide and half as many.
KSPRITE_MC_COLS = KSPRITE_COLS // 2

# The art's palette indices, and where each one has to come from on the VIC.
# Only one of the four is per sprite, so the choice is which colour the four
# blocks are free to disagree on - and since they are four quarters of one
# aeroplane, they never do. The mapping is therefore arbitrary and only has to
# be written down once; it is the main body tone that gets the per-sprite slot.
#
#   00  transparent          - black in the art
#   01  $D025, shared        - the dark shading
#   10  $D027+n, per sprite  - the main body tone
#   11  $D026, shared        - the light highlight
KTITLE_COLOR_MC0 = 2  # red
KTITLE_COLOR_MAIN = 10  # light red
KTITLE_COLOR_MC1 = 15  # light grey
KTITLE_BITPAIR = {
    0: 0,
    KTITLE_COLOR_MC0: 1,
    KTITLE_COLOR_MAIN: 2,
    KTITLE_COLOR_MC1: 3,
}


def _title_pair_color(img, x, y):
    """The colour of one multicolour pixel, from the two art pixels under it.

    The art is drawn on the multicolour grid, so the pair is almost always two
    of the same. The exception is a single stray transparent pixel inside a
    solid run at (126, 27), which is a one-pixel hole in the drawing rather
    than a misalignment: resolving a mixed pair in favour of the colour fills
    it. Two *different* colours in one pair would mean the crop had slipped by
    one, and that is a build failure rather than something to round off.
    """
    a, b = img[y][x], img[y][x + 1]
    if a == b:
        return a
    if a == 0:
        return b
    if b == 0:
        return a
    raise SystemExit(
        "generate_sprites: the title aircraft has two different colours in one "
        "multicolour pixel at (%d, %d): %d and %d.\n"
        "  The crop is off the multicolour grid - check KTITLE_CROP_X, which "
        "must stay even." % (x, y, a, b)
    )


def _mc_grid_to_sprite_bytes(grid):
    """21 rows of 12 bit pairs into the VIC's 64-byte block."""
    sprite_bytes = bytearray(64)
    for y in range(KSPRITE_ROWS):
        row = grid[y]
        for b in range(3):
            v = 0
            for k in range(4):
                v |= row[b * 4 + k] << (6 - 2 * k)
            sprite_bytes[y * 3 + b] = v
    return sprite_bytes


def generate_title_sprites(bitmap_offset):
    """The four blocks of the title aircraft, in reading order.

    Block order is top-left, top-right, bottom-left, bottom-right, which is
    also the order title.cc hands them to hardware sprites 0..3, so a block
    index is a sprite index plus the base and neither side needs a table.
    """
    png_path = os.path.join(REPO_ROOT, "gfx", KTITLE_PNG)
    img, _w, _h = _read_concept_png(png_path)

    assert KTITLE_CROP_X % 2 == 0, "the crop must start on a multicolour pixel"

    data = []
    meta = []
    grids = []
    for row in range(KTITLE_ROWS):
        for col in range(KTITLE_COLS):
            grid = [
                [0 for _ in range(KSPRITE_MC_COLS)] for _ in range(KSPRITE_ROWS)
            ]
            for sy in range(KSPRITE_ROWS):
                iy = KTITLE_CROP_Y + row * KSPRITE_ROWS + sy
                for sx in range(KSPRITE_MC_COLS):
                    ix = KTITLE_CROP_X + col * KSPRITE_COLS + sx * 2
                    c = _title_pair_color(img, ix, iy)
                    if c not in KTITLE_BITPAIR:
                        raise SystemExit(
                            "generate_sprites: the title aircraft uses palette "
                            "index %d at (%d, %d), which has no bit pair.\n"
                            "  A multicolour sprite has room for three colours "
                            "and transparent; see KTITLE_BITPAIR." % (c, ix, iy)
                        )
                    grid[sy][sx] = KTITLE_BITPAIR[c]

            data.append(_mc_grid_to_sprite_bytes(grid))
            grids.append(grid)
            meta.append(
                {
                    "bitmap_idx": bitmap_offset + row * KTITLE_COLS + col,
                    "label": "Title aircraft %s %s"
                    % ("top" if row == 0 else "bottom",
                       "left" if col == 0 else "right"),
                }
            )
    return data, meta, grids


def main():
    parser = argparse.ArgumentParser(
        description="Generate sprite data for C64 instrument needles and clouds."
    )
    parser.add_argument(
        "--angles_tot",
        type=int,
        default=32,
        help="Number of needle angles (default: 32)",
    )
    parser.add_argument(
        "--cloud_base_offset",
        type=int,
        default=80,
        help="Base offset for the blob: the orientation indicator, then the "
             "clouds (default: 80)",
    )
    parser.add_argument(
        "--base_offset",
        type=int,
        default=96,
        help="Base offset for bitmap indicators (default: 96)",
    )
    parser.add_argument(
        "--title_base_offset",
        type=int,
        default=60,
        help="First VIC sprite block of the title aircraft, which lives in "
             "its own 256 bytes at $CF00 rather than in the blob at $D400 "
             "(default: 60)",
    )
    args = parser.parse_args()

    angles_tot = args.angles_tot
    cloud_base = args.cloud_base_offset
    base_offset = args.base_offset
    title_base = args.title_base_offset

    # 1. Generate the orientation indicator (pointer 80) and the clouds
    # (14 bitmaps: pointers 81-94). Fifteen blocks between them, which is what
    # docs/clouds.md §6.1 and mem_init()'s expansion to $D400 assume; the mark
    # holds the block the cloud ladder's unreachable rung 0 used to.
    data_orient, meta_orient, bits_orient = generate_orient_sprite(cloud_base)
    (
        cloud1_data,
        cloud1_meta,
        cloud1_bits,
        cloud2_data,
        cloud2_meta,
        cloud2_bits,
    ) = generate_cloud_sprites(cloud_base + 1)

    # 2. Generate Sun (pointer 95)
    data_sun, meta_sun, bits_sun = generate_sun_sprite(base_offset - 1)

    # 3. Generate Long Arm (14 pixels, 16 bitmaps: pointers 96-111)
    data14, meta14, bits14 = generate_arm_set(14, base_offset, angles_tot)

    # 4. Generate Short Arm (10 pixels, 16 bitmaps: pointers 112-127)
    data10, meta10, bits10 = generate_arm_set(
        10, base_offset + angles_tot // 2, angles_tot
    )

    all_data = (
        [data_orient] + cloud1_data + cloud2_data + [data_sun] + data14 + data10
    )
    total_bitmaps = len(all_data)

    # 1. Output BIN
    bin_path = os.path.join(REPO_ROOT, "c64o", "spritedef.bin")
    os.makedirs(os.path.dirname(bin_path), exist_ok=True)
    with open(bin_path, "wb") as f:
        for data in all_data:
            f.write(data)
    print(f"Generated {bin_path} ({len(all_data) * 64} bytes)")

    # 2. Output H and CC
    h_path = os.path.join(REPO_ROOT, "c64o", "spritedef.h")
    with open(h_path, "w") as f:
        f.write("#ifndef SPRITEDEF_H\n")
        f.write("#define SPRITEDEF_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"static const uint16_t kSpriteDefMetaCount = {angles_tot};\n")
        f.write(
            f"static const uint16_t kSpriteDefBitmapCount = {total_bitmaps};\n"
        )
        f.write(
            f"static const uint8_t kSpriteDefCloud1Count = {len(cloud1_meta)};\n"
        )
        f.write(
            f"static const uint8_t kSpriteDefCloud2Count = {len(cloud2_meta)};\n\n"
        )
        f.write("struct sprite_meta_t {\n")
        f.write("    int8_t pivot_x;\n")
        f.write("    int8_t pivot_y;\n")
        f.write("    uint8_t bitmap_idx;\n")
        f.write("};\n\n")
        f.write("struct sprite_cloud1_meta_t {\n")
        f.write("    uint8_t width;\n")
        f.write("    uint8_t height;\n")
        f.write("    uint8_t bitmap_idx;\n")
        f.write("    int8_t pivot_x;\n")
        f.write("    int8_t pivot_y;\n")
        f.write("};\n\n")
        f.write("struct sprite_cloud2_meta_t {\n")
        f.write("    uint8_t width;\n")
        f.write("    uint8_t height;\n")
        f.write("    uint8_t top_bitmap_idx;\n")
        f.write("    uint8_t bot_bitmap_idx;\n")
        f.write("    int8_t pivot_x;\n")
        f.write("    int8_t pivot_y;\n")
        f.write("};\n\n")
        # The two tables above are how the sprites are *described*; this one is
        # how the simulation *uses* them - one flat row per ladder rung, so a
        # caller indexes by rung and never has to know which half of the ladder
        # it is on. bitmap2 is 0xFF (kSpriteNoBitmap) for the single-sprite
        # rungs, which is exactly what sprites_stack_add() wants.
        #
        # This is not sugar. Selecting between the two structs in C - two
        # differently-typed pointers of the same shape, four parallel field
        # assignments - miscompiles under oscar64: it folds a constant from one
        # branch into two fields of the other and drops a third assignment
        # entirely. See docs/clouds.md §3.4. A flat table has no branch to get
        # wrong.
        # One row per rung *including* rung 0, which clouds.cc can never
        # select and whose bitmap slot the orientation indicator now holds. The
        # row is kept so that a rung number is still a row number - the whole
        # point of a flat table - and it names the smallest real cloud, so even
        # a rung 0 that somehow got indexed would draw a cloud and not a mark.
        f.write(f"static const uint8_t kSpriteDefCloudRungCount = "
                f"{1 + len(cloud1_meta) + len(cloud2_meta)};\n\n")
        f.write("struct sprite_cloud_rung_t {\n")
        f.write("    uint8_t bitmap;\n")
        f.write("    uint8_t bitmap2;   // 0xFF when the rung is one sprite\n")
        f.write("    int8_t pivot_x;\n")
        f.write("    int8_t pivot_y;\n")
        f.write("};\n\n")
        f.write("extern const sprite_cloud_rung_t "
                "kSpriteDefCloudRung[kSpriteDefCloudRungCount];\n")
        f.write(
            "extern const sprite_cloud1_meta_t kSpriteDefCloud1Sprite[kSpriteDefCloud1Count];\n"
        )
        f.write(
            "extern const sprite_cloud2_meta_t kSpriteDefCloud2Sprite[kSpriteDefCloud2Count];\n"
        )
        f.write(
            "extern const sprite_meta_t kSpriteDefMetaLongArm[kSpriteDefMetaCount];\n"
        )
        f.write(
            "extern const sprite_meta_t kSpriteDefMetaShortArm[kSpriteDefMetaCount];\n"
        )
        f.write("extern const sprite_meta_t kSpriteDefSun;\n")
        f.write("extern const sprite_meta_t kSpriteDefOrient;\n\n")
        f.write('#pragma compile("spritedef.cc")\n\n')
        f.write("#endif\n")
    print(f"Generated {h_path}")

    cc_path = os.path.join(REPO_ROOT, "c64o", "spritedef.cc")
    with open(cc_path, "w") as f:
        f.write('#include "spritedef.h"\n\n')
        f.write(
            "const sprite_cloud1_meta_t kSpriteDefCloud1Sprite[kSpriteDefCloud1Count] = {\n"
        )
        for m in cloud1_meta:
            f.write(
                f"    {{ {m['width']}, {m['height']}, {m['bitmap_idx']}, {m['pivot_x']}, {m['pivot_y']} }}, // {m['label']}\n"
            )
        f.write("};\n\n")
        f.write(
            "const sprite_cloud2_meta_t kSpriteDefCloud2Sprite[kSpriteDefCloud2Count] = {\n"
        )
        for m in cloud2_meta:
            f.write(
                f"    {{ {m['width']}, {m['height']}, {m['top_bitmap_idx']}, {m['bot_bitmap_idx']}, {m['pivot_x']}, {m['pivot_y']} }}, // {m['label']}\n"
            )
        f.write("};\n\n")
        f.write(
            "const sprite_cloud_rung_t kSpriteDefCloudRung[kSpriteDefCloudRungCount] = {\n"
        )
        f.write(
            f"    {{ {cloud1_meta[0]['bitmap_idx']}, 0xFF, "
            f"{cloud1_meta[0]['pivot_x']}, {cloud1_meta[0]['pivot_y']} }}, "
            f"// rung 0: never selected; its block is the orientation indicator\n"
        )
        for m in cloud1_meta:
            f.write(
                f"    {{ {m['bitmap_idx']}, 0xFF, {m['pivot_x']}, {m['pivot_y']} }}, // {m['label']}\n"
            )
        for m in cloud2_meta:
            f.write(
                f"    {{ {m['top_bitmap_idx']}, {m['bot_bitmap_idx']}, {m['pivot_x']}, {m['pivot_y']} }}, // {m['label']}\n"
            )
        f.write("};\n\n")
        f.write(
            "const sprite_meta_t kSpriteDefMetaLongArm[kSpriteDefMetaCount] = {\n"
        )
        for m in meta14:
            f.write(
                f"    {{ {m['pivot_x']}, {m['pivot_y']}, {m['bitmap_idx']} }}, // {m['label']}\n"
            )
        f.write("};\n\n")
        f.write(
            "const sprite_meta_t kSpriteDefMetaShortArm[kSpriteDefMetaCount] = {\n"
        )
        for m in meta10:
            f.write(
                f"    {{ {m['pivot_x']}, {m['pivot_y']}, {m['bitmap_idx']} }}, // {m['label']}\n"
            )
        f.write("};\n\n")
        f.write(
            f"const sprite_meta_t kSpriteDefSun = {{ {meta_sun['pivot_x']}, {meta_sun['pivot_y']}, {meta_sun['bitmap_idx']} }};\n"
        )
        f.write(
            f"const sprite_meta_t kSpriteDefOrient = {{ {meta_orient['pivot_x']}, {meta_orient['pivot_y']}, {meta_orient['bitmap_idx']} }};\n"
        )
    print(f"Generated {cc_path}")

    # 3. Output Python
    lib_path = os.path.join(REPO_ROOT, "lib", "spritedef.py")
    with open(lib_path, "w") as f:
        f.write(
            "# Generated Sprite Definitions (Orientation indicator, Clouds, "
            "Sun, Long Arm 14, Short Arm 10)\n\n"
        )
        f.write(f"NUM_BITMAPS_TOTAL = {total_bitmaps}\n")
        f.write(f"NUM_ANGLES = {angles_tot}\n\n")

        f.write("META_CLOUD1 = [\n")
        for m in cloud1_meta:
            f.write(
                f"    {{'width': {m['width']}, 'height': {m['height']}, 'bitmap_idx': {m['bitmap_idx']}, 'pivot_x': {m['pivot_x']}, 'pivot_y': {m['pivot_y']}, 'label': '{m['label']}'}},\n"
            )
        f.write("]\n\n")

        f.write("META_CLOUD2 = [\n")
        for m in cloud2_meta:
            f.write(
                f"    {{'width': {m['width']}, 'height': {m['height']}, 'top_bitmap_idx': {m['top_bitmap_idx']}, 'bot_bitmap_idx': {m['bot_bitmap_idx']}, 'pivot_x': {m['pivot_x']}, 'pivot_y': {m['pivot_y']}, 'label': '{m['label']}'}},\n"
            )
        f.write("]\n\n")

        f.write(
            f"META_SUN = {{'pivot_x': {meta_sun['pivot_x']}, 'pivot_y': {meta_sun['pivot_y']}, 'bitmap_idx': {meta_sun['bitmap_idx']}, 'label': '{meta_sun['label']}'}}\n\n"
        )

        f.write(
            f"META_ORIENT = {{'pivot_x': {meta_orient['pivot_x']}, 'pivot_y': {meta_orient['pivot_y']}, 'bitmap_idx': {meta_orient['bitmap_idx']}, 'label': '{meta_orient['label']}'}}\n\n"
        )

        f.write("META_LONG_ARM = [\n")
        for m in meta14:
            f.write(
                f"    {{'pivot_x': {m['pivot_x']}, 'pivot_y': {m['pivot_y']}, 'bitmap_idx': {m['bitmap_idx']}, 'label': '{m['label']}'}},\n"
            )
        f.write("]\n\n")

        f.write("META_SHORT_ARM = [\n")
        for m in meta10:
            f.write(
                f"    {{'pivot_x': {m['pivot_x']}, 'pivot_y': {m['pivot_y']}, 'bitmap_idx': {m['bitmap_idx']}, 'label': '{m['label']}'}},\n"
            )
        f.write("]\n\n")

        f.write("PATTERNS_CLOUD1 = [\n")
        for i, grid in enumerate(cloud1_bits):
            f.write(f"    # {cloud1_meta[i]['label']}\n")
            f.write("    [\n")
            for row in grid:
                row_str = "".join(["#" if b else "." for b in row])
                f.write(f'        "{row_str}",\n')
            f.write("    ],\n")
        f.write("]\n\n")

        f.write("PATTERNS_CLOUD2 = [\n")
        for i, (g_top, g_bot) in enumerate(cloud2_bits):
            f.write(f"    # {cloud2_meta[i]['label']} (Top)\n")
            f.write("    [\n")
            for row in g_top:
                row_str = "".join(["#" if b else "." for b in row])
                f.write(f'        "{row_str}",\n')
            f.write("    ],\n")
            f.write(f"    # {cloud2_meta[i]['label']} (Bottom)\n")
            f.write("    [\n")
            for row in g_bot:
                row_str = "".join(["#" if b else "." for b in row])
                f.write(f'        "{row_str}",\n')
            f.write("    ],\n")
        f.write("]\n\n")

        f.write("PATTERN_SUN = [\n")
        for row in bits_sun:
            row_str = "".join(["#" if b else "." for b in row])
            f.write(f'    "{row_str}",\n')
        f.write("]\n\n")

        f.write("PATTERN_ORIENT = [\n")
        for row in bits_orient:
            row_str = "".join(["#" if b else "." for b in row])
            f.write(f'    "{row_str}",\n')
        f.write("]\n\n")

        f.write("PATTERNS_LONG_ARM = [\n")
        for i, grid in enumerate(bits14):
            f.write(
                f"    # Bitmap {i + base_offset} (Long, { (i*(360.0/angles_tot))-90 } deg)\n"
            )
            f.write("    [\n")
            for row in grid:
                row_str = "".join(["#" if b else "." for b in row])
                f.write(f'        "{row_str}",\n')
            f.write("    ],\n")
        f.write("]\n\n")

        f.write("PATTERNS_SHORT_ARM = [\n")
        for i, grid in enumerate(bits10):
            f.write(
                f"    # Bitmap {i + base_offset + angles_tot//2} (Short, { (i*(360.0/angles_tot))-90 } deg)\n"
            )
            f.write("    [\n")
            for row in grid:
                row_str = "".join(["#" if b else "." for b in row])
                f.write(f'        "{row_str}",\n')
            f.write("    ],\n")
        f.write("]\n")
    print(f"Generated {lib_path}")

    # --- The title aircraft, which is its own blob -------------------------
    #
    # Separate from spritedef.bin because it is expanded somewhere else: the
    # blob above goes to $D400 as one contiguous 3072 bytes, and there is no
    # room in front of it for four more blocks - $D000..$D3FF is the map view's
    # screen RAM. These four live at $CF00, just below I/O, and are expanded by
    # title.cc when the menu opens. See c64o/mem.h.
    title_data, title_meta, title_bits = generate_title_sprites(title_base)

    title_bin_path = os.path.join(REPO_ROOT, "c64o", "titledef.bin")
    with open(title_bin_path, "wb") as f:
        for data in title_data:
            f.write(data)
    print(f"Generated {title_bin_path} ({len(title_data) * 64} bytes)")

    title_h_path = os.path.join(REPO_ROOT, "c64o", "titledef.h")
    with open(title_h_path, "w") as f:
        f.write("// Generated by tools/generate_sprites.py from\n")
        f.write(f"// gfx/{KTITLE_PNG}. Do not edit; run `make sprites`.\n\n")
        f.write("#ifndef TITLEDEF_H\n")
        f.write("#define TITLEDEF_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("// The title screen aircraft: one drawing across a 2 x 2 "
                "block of\n")
        f.write("// multicolour sprites, in reading order - top left, top "
                "right, bottom\n")
        f.write("// left, bottom right. 48 x 42 screen pixels.\n")
        f.write(f"static const uint8_t kTitleDefCols = {KTITLE_COLS};\n")
        f.write(f"static const uint8_t kTitleDefRows = {KTITLE_ROWS};\n")
        f.write("static const uint8_t kTitleDefBitmapCount = "
                f"{len(title_data)};\n")
        f.write("// First VIC sprite block; the four are consecutive. "
                "c64o/mem.h places\n")
        f.write("// the bitmaps and asserts that its address agrees with "
                "this.\n")
        f.write("static const uint8_t kTitleDefBitmapBase = "
                f"{title_base};\n\n")
        f.write("// Bit pair 01 and 11 are the two screen-wide sprite "
                "multicolour\n")
        f.write("// registers; bit pair 10 is each sprite's own colour, and "
                "all four\n")
        f.write("// blocks are given the same one.\n")
        f.write("static const uint8_t kTitleDefColorMc0 = "
                f"{KTITLE_COLOR_MC0};\n")
        f.write("static const uint8_t kTitleDefColorMain = "
                f"{KTITLE_COLOR_MAIN};\n")
        f.write("static const uint8_t kTitleDefColorMc1 = "
                f"{KTITLE_COLOR_MC1};\n\n")
        f.write("#endif\n")
    print(f"Generated {title_h_path}")

    title_lib_path = os.path.join(REPO_ROOT, "lib", "titledef.py")
    with open(title_lib_path, "w") as f:
        f.write("# Generated Title Screen Aircraft Sprite Definitions\n\n")
        f.write(f"NUM_BITMAPS = {len(title_data)}\n")
        f.write(f"COLS = {KTITLE_COLS}\n")
        f.write(f"ROWS = {KTITLE_ROWS}\n")
        f.write(f"BITMAP_BASE = {title_base}\n")
        f.write(f"COLOR_MC0 = {KTITLE_COLOR_MC0}\n")
        f.write(f"COLOR_MAIN = {KTITLE_COLOR_MAIN}\n")
        f.write(f"COLOR_MC1 = {KTITLE_COLOR_MC1}\n\n")
        f.write("META = [\n")
        for m in title_meta:
            f.write(f"    {{'bitmap_idx': {m['bitmap_idx']}, "
                    f"'label': '{m['label']}'}},\n")
        f.write("]\n\n")
        # One character per multicolour pixel: '.' transparent, then the bit
        # pair as a digit, so a pattern can be read back as art or as data.
        f.write("PATTERNS = [\n")
        for i, grid in enumerate(title_bits):
            f.write(f"    # {title_meta[i]['label']}\n")
            f.write("    [\n")
            for row in grid:
                row_str = "".join("." if v == 0 else str(v) for v in row)
                f.write(f'        "{row_str}",\n')
            f.write("    ],\n")
        f.write("]\n")
    print(f"Generated {title_lib_path}")


if __name__ == "__main__":
    main()
