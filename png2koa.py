#!/usr/bin/env python3
"""
png2koa.py - PNG to C64 Koala Painter (.koa) file converter with Pepto palette.

Converts a 320x200 PNG image to Commodore 64 Koala Painter format (.koa)
and generates a palette-mapped PNG preview image using Pepto's C64 palette.

Usage:
    python png2koa.py input.png [output.koa] [output_pepto.png] [--bg-color BG]
"""

from __future__ import annotations
import os
import sys
import argparse
from PIL import Image

try:
    from lib import c64_colors
    PEPTO_PALETTE = [c64_colors.PALETTE_RGB[i] for i in range(16)]
except ImportError:
    # Fallback to standard C64 Pepto palette if lib module is not in path
    PEPTO_PALETTE = [
        (0x00, 0x00, 0x00),  # 0: Black
        (0xFF, 0xFF, 0xFF),  # 1: White
        (104, 55, 43),       # 2: Red
        (112, 164, 178),     # 3: Cyan
        (111, 61, 134),      # 4: Purple
        (88, 141, 67),       # 5: Green
        (53, 40, 121),       # 6: Blue
        (184, 199, 111),     # 7: Yellow
        (111, 79, 37),       # 8: Orange
        (67, 57, 0),         # 9: Brown
        (154, 103, 89),      # 10: Light Red
        (68, 68, 68),        # 11: Dark Grey
        (108, 108, 108),     # 12: Grey
        (154, 210, 132),     # 13: Light Green
        (108, 94, 181),      # 14: Light Blue
        (149, 149, 149),     # 15: Light Grey
    ]



def rgb_dist_sq(c1: tuple[int, int, int], c2: tuple[int, int, int]) -> float:
    """Perceptually weighted RGB Euclidean distance squared."""
    dr = c1[0] - c2[0]
    dg = c1[1] - c2[1]
    db = c1[2] - c2[2]
    return 2.0 * dr * dr + 4.0 * dg * dg + 3.0 * db * db


def get_closest_pepto_color(rgb: tuple[int, int, int]) -> int:
    """Returns the index (0..15) of the closest Pepto palette color."""
    best_idx = 0
    best_dist = float('inf')
    for i, p_rgb in enumerate(PEPTO_PALETTE):
        d = rgb_dist_sq(rgb, p_rgb)
        if d < best_dist:
            best_dist = d
            best_idx = i
    return best_idx


def downsample_to_multicolor_grid(img: Image.Image) -> list[list[tuple[int, int, int]]]:
    """
    Converts 320x200 Image into a 160x200 grid of multicolor pixels.
    Each pixel is the average RGB of 2 horizontal subpixels (x*2, y) and (x*2+1, y).
    If input image is an indexed PNG (mode 'P'), remaps palette entries 0..15 directly
    to Pepto palette colors to preserve C64 palette indices (e.g., VICE palette blue/green).
    """
    if img.size != (320, 200):
        print(f"Warning: Resizing input image from {img.size} to (320, 200)")
        img = img.resize((320, 200), Image.Resampling.LANCZOS)

    if img.mode == 'P':
        # Create a new palette using Pepto palette colors
        pepto_palette_bytes = []
        for r, g, b in PEPTO_PALETTE:
            pepto_palette_bytes.extend([r, g, b])
        # Fill remaining palette entries up to 256
        pepto_palette_bytes.extend([0] * (768 - len(pepto_palette_bytes)))
        img = img.copy()
        img.putpalette(pepto_palette_bytes)

    rgb_img = img.convert("RGB")
    pixels = rgb_img.load()

    grid = [[(0, 0, 0) for _ in range(160)] for _ in range(200)]
    for y in range(200):
        for x in range(160):
            r1, g1, b1 = pixels[x * 2, y]
            r2, g2, b2 = pixels[x * 2 + 1, y]
            grid[y][x] = ((r1 + r2) // 2, (g1 + g2) // 2, (b1 + b2) // 2)
    return grid


def optimize_cell(
    cell_pixels: list[tuple[int, int, int]],
    bg_color: int,
    prev_l1: int = 13,
    prev_l2: int = 13,
    prev_l3: int = 13,
) -> tuple[int, int, int, list[int]]:
    """
    Finds optimal 3 local colors (L1, L2, L3) and assigns bit pairs (0..3) for a 4x8 cell (32 pixels).
    Bits mapping: 00 -> bg_color, 01 -> L1, 10 -> L2, 11 -> L3.
    Propagates prev_l1, prev_l2, prev_l3 across unused color slots to maximize RLE / LZ77 compression ratio.
    """
    # Calculate distance of each pixel to all 16 Pepto colors
    pixel_dists = []
    pixel_best_colors = []
    color_freq = [0] * 16

    for rgb in cell_pixels:
        dists = [rgb_dist_sq(rgb, p_rgb) for p_rgb in PEPTO_PALETTE]
        pixel_dists.append(dists)
        best_c = min(range(16), key=lambda c: dists[c])
        pixel_best_colors.append(best_c)
        color_freq[best_c] += 1

    # Unique colors needed (excluding bg_color)
    needed_colors = [c for c in set(pixel_best_colors) if c != bg_color]

    # Rule 1: If needed_colors is empty or a subset of (prev_l1, prev_l2, prev_l3), keep prev_l1, prev_l2, prev_l3 completely
    if all(c in (prev_l1, prev_l2, prev_l3) for c in needed_colors):
        l1, l2, l3 = prev_l1, prev_l2, prev_l3
    elif len(needed_colors) <= 3:
        sorted_colors = sorted(needed_colors)
        cand_l = [prev_l1, prev_l2, prev_l3]
        missing = [c for c in sorted_colors if c not in cand_l]
        unused_slot_indices = [idx for idx, c in enumerate(cand_l) if c not in sorted_colors]

        for c, slot_idx in zip(missing, unused_slot_indices):
            cand_l[slot_idx] = c

        l1, l2, l3 = cand_l[0], cand_l[1], cand_l[2]
        if len(sorted_colors) == 3:
            l1, l2, l3 = sorted_colors[0], sorted_colors[1], sorted_colors[2]
    else:
        # Candidate local colors are primarily those present in the cell, sorted by frequency
        unique_cell_colors = sorted(set(needed_colors), key=lambda c: color_freq[c], reverse=True)
        if len(unique_cell_colors) < 6:
            # Supplement with most frequent colors in cell or palette if needed
            extra = [c for c in range(16) if c != bg_color and c not in unique_cell_colors]
            unique_cell_colors.extend(extra[: 6 - len(unique_cell_colors)])
        else:
            unique_cell_colors = unique_cell_colors[:6]

        best_error = float('inf')
        best_triple = (prev_l1, prev_l2, prev_l3)
        best_overlap = -1

        num_cand = len(unique_cell_colors)
        for i in range(num_cand):
            c1 = unique_cell_colors[i]
            for j in range(i + 1, num_cand):
                c2 = unique_cell_colors[j]
                for k in range(j + 1, num_cand):
                    c3 = unique_cell_colors[k]
                    # Total cell error with palette {bg_color, c1, c2, c3}
                    err = sum(
                        min(dists[bg_color], dists[c1], dists[c2], dists[c3])
                        for dists in pixel_dists
                    )
                    overlap = (1 if c1 == prev_l1 else 0) + (1 if c2 == prev_l2 else 0) + (1 if c3 == prev_l3 else 0)
                    if err < best_error or (err == best_error and overlap > best_overlap):
                        best_error = err
                        best_overlap = overlap
                        best_triple = (c1, c2, c3)

        l1, l2, l3 = sorted(best_triple)

    # Map each pixel to best bit pattern in {00: bg, 01: l1, 10: l2, 11: l3}
    allowed = [(bg_color, 0), (l1, 1), (l2, 2), (l3, 3)]
    bit_assignments = []
    for dists in pixel_dists:
        best_b = min(range(len(allowed)), key=lambda idx: dists[allowed[idx][0]])
        bit_assignments.append(allowed[best_b][1])

    return l1, l2, l3, bit_assignments


def auto_detect_bg_color(grid: list[list[tuple[int, int, int]]]) -> int:
    """Finds the global background color (0..15) that minimizes total quantization error."""
    color_counts = [0] * 16
    for y in range(200):
        for x in range(160):
            c = get_closest_pepto_color(grid[y][x])
            color_counts[c] += 1
    # Most frequent color is the best candidate for global background
    return max(range(16), key=lambda c: color_counts[c])


import zlib


def estimate_lzo_size(data: bytes) -> int:
    """Estimates the LZO1X / LZ77 compressed byte size of a binary buffer."""
    n = len(data)
    pos = 0
    literals = 0
    compressed_size = 0

    head: dict[tuple[int, int, int], int] = {}

    def flush_literals(count: int) -> None:
        nonlocal compressed_size
        if count == 0:
            return
        if count <= 3:
            compressed_size += count
        elif count <= 18:
            compressed_size += 1 + count
        else:
            compressed_size += 2 + count

    while pos < n:
        best_len = 0
        best_off = 0

        if pos + 2 < n:
            triple = (data[pos], data[pos + 1], data[pos + 2])
            prev_pos = head.get(triple, -1)
            head[triple] = pos
            if prev_pos != -1 and (pos - prev_pos) <= 4096:
                length = 0
                max_len = min(255, n - pos)
                while length < max_len and data[prev_pos + length] == data[pos + length]:
                    length += 1
                if length >= 3:
                    best_len = length
                    best_off = pos - prev_pos

        if best_len >= 3:
            flush_literals(literals)
            literals = 0

            if best_len <= 8 and best_off <= 2048:
                compressed_size += 2
            elif best_len <= 33 and best_off <= 16384:
                compressed_size += 2
            else:
                compressed_size += 3

            pos += best_len
        else:
            literals += 1
            pos += 1

    flush_literals(literals)
    return compressed_size


def convert_png_to_koala(
    input_path: str,
    koa_output_path: str,
    png_output_path: str | None = None,
    bg_color: int | None = None
) -> None:
    """
    Main conversion routine. Loads PNG, encodes to Koala Painter format,
    and optionally writes palette-mapped PNG.
    """
    img = Image.open(input_path)
    grid = downsample_to_multicolor_grid(img)

    if bg_color is None:
        bg_color = auto_detect_bg_color(grid)
        print(f"Auto-selected global background color: {bg_color} ({PEPTO_PALETTE[bg_color]})")
    else:
        bg_color &= 0x0F
        print(f"Using user background color: {bg_color}")

    # Prepare Koala 10,003 byte buffer
    koa_bytes = bytearray(10003)

    # Load address $6000 (0x00, 0x60)
    koa_bytes[0] = 0x00
    koa_bytes[1] = 0x60

    # Pointers to data sections in koa_bytes
    # Bitmap: 2..8001 (8000 bytes)
    # Screen RAM: 8002..9001 (1000 bytes)
    # Color RAM: 9002..10001 (1000 bytes)
    # Background: 10002 (1 byte)

    koa_bytes[10002] = bg_color

    # Preview image buffers if png_output_path is provided
    preview_img = Image.new("RGB", (320, 200))
    preview_pixels = preview_img.load()

    # Default initial prev colors set to 13 (Light Green border color)
    prev_l1, prev_l2, prev_l3 = 13, 13, 13

    # Process 40x25 character matrix cells
    for cy in range(25):
        for cx in range(40):
            cell_idx = cy * 40 + cx

            # Extract 4x8 cell pixels (row-major order inside cell)
            cell_pixels = []
            for ry in range(8):
                y = cy * 8 + ry
                for rx in range(4):
                    x = cx * 4 + rx
                    cell_pixels.append(grid[y][x])

            l1, l2, l3, bits = optimize_cell(cell_pixels, bg_color, prev_l1, prev_l2, prev_l3)
            prev_l1, prev_l2, prev_l3 = l1, l2, l3

            # Store Screen RAM: High nibble = L1, Low nibble = L2
            koa_bytes[8002 + cell_idx] = ((l1 & 0x0F) << 4) | (l2 & 0x0F)

            # Store Color RAM: Low nibble = L3
            koa_bytes[9002 + cell_idx] = l3 & 0x0F

            # Map bit pair to actual Pepto color index
            bit_to_color = {0: bg_color, 1: l1, 2: l2, 3: l3}

            # Store Bitmap bytes & preview pixels
            for ry in range(8):
                byte_val = 0
                for rx in range(4):
                    bit_pair = bits[ry * 4 + rx]
                    byte_val |= (bit_pair & 0x03) << (6 - rx * 2)

                    # Update preview PNG
                    c_idx = bit_to_color[bit_pair]
                    rgb = PEPTO_PALETTE[c_idx]
                    px = (cx * 4 + rx) * 2
                    py = cy * 8 + ry
                    preview_pixels[px, py] = rgb
                    preview_pixels[px + 1, py] = rgb

                koa_bytes[2 + cell_idx * 8 + ry] = byte_val

    # Write .koa binary file
    with open(koa_output_path, "wb") as f:
        f.write(koa_bytes)

    bm_lzo = estimate_lzo_size(bytes(koa_bytes[2:8002]))
    sc_lzo = estimate_lzo_size(bytes(koa_bytes[8002:9002]))
    cr_lzo = estimate_lzo_size(bytes(koa_bytes[9002:10002]))
    full_lzo = estimate_lzo_size(bytes(koa_bytes))
    zlib_size = len(zlib.compress(koa_bytes, 9))

    print(f"Saved Koala Painter file: {koa_output_path} ({len(koa_bytes)} bytes)")
    print(f"Estimated LZO compressed size: ~{full_lzo} bytes (Zlib: {zlib_size} bytes)")
    print(f"  - Bitmap RAM (8KB):    ~{bm_lzo} bytes")
    print(f"  - Screen RAM (1KB):    ~{sc_lzo} bytes")
    print(f"  - Color RAM  (1KB):    ~{cr_lzo} bytes")

    # Write preview PNG if requested
    if png_output_path:
        preview_img.save(png_output_path)
        print(f"Saved Pepto palette preview PNG: {png_output_path}")



def main():
    parser = argparse.ArgumentParser(
        description="Convert a 320x200 PNG image into a C64 Koala Painter (.koa) file and Pepto palette PNG preview."
    )
    parser.add_argument("input_png", help="Input 320x200 PNG image file path")
    parser.add_argument("output_koa", nargs="?", help="Output Koala Painter (.koa) file path (default: <input>.koa)")
    parser.add_argument("output_png", nargs="?", help="Output preview PNG file path (default: <input>_pepto.png)")
    parser.add_argument("--bg-color", type=int, choices=range(16), help="Force global background color (0..15)")

    args = parser.parse_args()

    base_name, _ = os.path.splitext(args.input_png)
    output_koa = args.output_koa or f"{base_name}.koa"
    output_png = args.output_png or f"{base_name}_pepto.png"

    convert_png_to_koala(
        input_path=args.input_png,
        koa_output_path=output_koa,
        png_output_path=output_png,
        bg_color=args.bg_color
    )


if __name__ == "__main__":
    main()
