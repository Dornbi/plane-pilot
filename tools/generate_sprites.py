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


def generate_cloud_sprites(cloud_base_offset):
    png_path = os.path.join(REPO_ROOT, "gfx", "ppilot_clouds_concept.png")
    img, w, h = _read_concept_png(png_path)

    # 1-sprite cloud configurations from upper set in concept PNG
    one_sprite_defs = [
        ("3x5", (24, 29), (66, 70), 3, 5),
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

    cloud2_base_offset = cloud_base_offset + len(one_sprite_defs)  # 80 + 5 = 85

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
        help="Base offset for cloud bitmaps (default: 80)",
    )
    parser.add_argument(
        "--base_offset",
        type=int,
        default=96,
        help="Base offset for bitmap indicators (default: 96)",
    )
    args = parser.parse_args()

    angles_tot = args.angles_tot
    cloud_base = args.cloud_base_offset
    base_offset = args.base_offset

    # 1. Generate Clouds (15 bitmaps: pointers 80-94)
    (
        cloud1_data,
        cloud1_meta,
        cloud1_bits,
        cloud2_data,
        cloud2_meta,
        cloud2_bits,
    ) = generate_cloud_sprites(cloud_base)

    # 2. Generate Sun (pointer 95)
    data_sun, meta_sun, bits_sun = generate_sun_sprite(base_offset - 1)

    # 3. Generate Long Arm (14 pixels, 16 bitmaps: pointers 96-111)
    data14, meta14, bits14 = generate_arm_set(14, base_offset, angles_tot)

    # 4. Generate Short Arm (10 pixels, 16 bitmaps: pointers 112-127)
    data10, meta10, bits10 = generate_arm_set(
        10, base_offset + angles_tot // 2, angles_tot
    )

    all_data = cloud1_data + cloud2_data + [data_sun] + data14 + data10
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
        f.write(f"static const uint8_t kSpriteDefCloudRungCount = "
                f"{len(cloud1_meta) + len(cloud2_meta)};\n\n")
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
        f.write("extern const sprite_meta_t kSpriteDefSun;\n\n")
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
    print(f"Generated {cc_path}")

    # 3. Output Python
    lib_path = os.path.join(REPO_ROOT, "lib", "spritedef.py")
    with open(lib_path, "w") as f:
        f.write(
            "# Generated Sprite Definitions (Clouds, Sun, Long Arm 14, Short Arm 10)\n\n"
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


if __name__ == "__main__":
    main()
