#!/usr/bin/env python3
"""
png2koa.py - PNG to C64 Koala Painter (.koa) file converter with Pepto palette.

Converts a 320x200 PNG image to Commodore 64 Koala Painter format (.koa)
and generates a palette-mapped PNG preview image using Pepto's C64 palette.

The encoder is tuned for oscar64's `#embed ... lzo` compressor, which is a very
weak LZ77 variant (see oscar64/Compression.cpp):

    literal run : 1 + n bytes, n <= 127
    match       : 2 bytes, length 4..127, offset 1..255 ONLY
    greedy, longest match, smallest offset

The 255-byte window is the dominant constraint. A bitmap row is 320 bytes, so
row-to-row redundancy is structurally unreachable; only horizontal reuse within
~31 char cells ever pays off. Screen/colour RAM rows are 40 bytes apart, so
those streams do see ~6 rows back.

Usage:
    python png2koa.py input.png [output.koa] [output_pepto.png]
                      [--bg-color BG] [--bg-search [N]]
                      [--snap-tolerance N] [--snap-window N]
"""

from __future__ import annotations
import os
import sys
import zlib
import argparse
from itertools import combinations
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

# oscar64 LZO match window, in bytes and in bitmap char cells (8 bytes/cell).
LZO_WINDOW = 255
LZO_MIN_MATCH = 4
LZO_MAX_MATCH = 127
CELL_WINDOW = LZO_WINDOW // 8  # 31 cells


# ---------------------------------------------------------------------------
# oscar64 LZO
# ---------------------------------------------------------------------------

def oscar64_lzo_size(data: bytes) -> int:
    """
    Exact size of the stream produced by oscar64's CompressLZO()
    (oscar64/Compression.cpp), including the terminating 0 byte.

    Faithful to the reference implementation:
      - candidate offsets 1..255 (clamped to the bytes emitted so far)
      - matches may overlap the current position (offset-1 RLE)
      - minimum match length 4, maximum 127
      - literal runs are flushed at 127 bytes
    """
    if not isinstance(data, (bytes, bytearray)):
        data = bytes(data)
    data = bytes(data)

    n = len(data)
    pos = 0
    csize = 0

    while pos < n:
        pending = 0
        while pending < LZO_MAX_MATCH and pos < n:
            best = 0
            if n - pos >= LZO_MIN_MATCH:
                lo = pos - LZO_WINDOW
                if lo < 0:
                    lo = 0
                pat = data[pos:pos + LZO_MIN_MATCH]
                max_len = LZO_MAX_MATCH if n - pos > LZO_MAX_MATCH else n - pos
                # Candidate starts must lie in [lo, pos-1]; the pattern may run
                # past `pos`, which is how offset-1 run-length coding happens.
                p = data.find(pat, lo, pos + LZO_MIN_MATCH - 1)
                while p != -1:
                    j = LZO_MIN_MATCH
                    while j < max_len and data[p + j] == data[pos + j]:
                        j += 1
                    if j > best:
                        best = j
                        if best >= max_len:
                            break
                    p = data.find(pat, p + 1, pos + LZO_MIN_MATCH - 1)

            if best >= LZO_MIN_MATCH:
                if pending > 0:
                    csize += 1 + pending
                    pending = 0
                csize += 2
                pos += best
            else:
                pos += 1
                pending += 1

        if pending > 0:
            csize += 1 + pending

    return csize + 1


# ---------------------------------------------------------------------------
# Colour handling
# ---------------------------------------------------------------------------

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


def build_cell_dists(grid: list[list[tuple[int, int, int]]]) -> list[list[list[float]]]:
    """
    Precomputes, for every one of the 1000 char cells, the distance of each of its
    32 pixels to all 16 Pepto colours. Independent of the background colour, so it
    is computed once and reused across the whole background-colour search.

    Returns cell_dists[cell_idx][pixel 0..31][colour 0..15], pixels in row-major
    order inside the 4x8 cell.
    """
    cell_dists: list[list[list[float]]] = []
    for cy in range(25):
        for cx in range(40):
            dists_for_cell = []
            for ry in range(8):
                row = grid[cy * 8 + ry]
                for rx in range(4):
                    rgb = row[cx * 4 + rx]
                    dists_for_cell.append([rgb_dist_sq(rgb, p) for p in PEPTO_PALETTE])
            cell_dists.append(dists_for_cell)
    return cell_dists


def auto_detect_bg_color(cell_dists: list[list[list[float]]]) -> int:
    """Finds the background colour (0..15) most frequently nearest to a pixel."""
    counts = [0] * 16
    for dists_for_cell in cell_dists:
        for dists in dists_for_cell:
            counts[min(range(16), key=lambda c: dists[c])] += 1
    return max(range(16), key=lambda c: counts[c])


# ---------------------------------------------------------------------------
# Per-cell encoding
# ---------------------------------------------------------------------------

def _pin_to_color_ram(l1: int, l2: int, l3: int, bits: list[int],
                      pinned: frozenset[int]) -> tuple[int, int, int, list[int]]:
    """
    Moves a pinned colour into L3 (colour RAM, bit pair 11), swapping whatever
    was there into the slot it came from. Purely a relabeling: every pixel keeps
    its exact colour, so this may be applied to any finished cell.

    At most one pinned colour can be honoured per cell, since there is only one
    colour RAM nibble. A cell needing two of them is a conflict the caller has
    to hear about, so it is reported rather than silently half-applied.

    Slots no pixel references are cleared out too. They are invisible, but
    leaving a pinned colour in one would break the flat rule the C64 side relies
    on - "this colour lives in colour RAM, always" - and make it untestable.
    """
    slots = [l1, l2, l3]
    used = set(bits)
    live = [s for s in (1, 2, 3) if s in used and slots[s - 1] in pinned]
    if len(live) > 1:
        raise ValueError(
            f"cell draws in {len(live)} pinned colours "
            f"({', '.join(str(slots[s - 1]) for s in live)}) but there is "
            "only one colour RAM nibble to pin them to")
    if live and live[0] != 3:
        src = live[0]
        slots[src - 1], slots[2] = slots[2], slots[src - 1]
        swap = {src: 3, 3: src}
        bits = [swap.get(b, b) for b in bits]
        used = set(bits)
    # Whatever pinned colours are left sit in slots no pixel reads, so they can
    # be anything at all; they are cleared rather than kept so that "pinned
    # colour in screen RAM" stays a flat error, with no exceptions to check.
    filler = next(c for c in range(16) if c not in pinned)
    for s in (1, 2):
        if slots[s - 1] in pinned and s not in used:
            slots[s - 1] = filler
    return slots[0], slots[1], slots[2], bits


def optimize_cell(
    pixel_dists: list[list[float]],
    bg_color: int,
    prev_l1: int = 13,
    prev_l2: int = 13,
    prev_l3: int = 13,
    pin_color_ram: frozenset[int] = frozenset(),
) -> tuple[int, int, int, list[int]]:
    """
    Finds optimal 3 local colors (L1, L2, L3) and assigns bit pairs (0..3) for a 4x8 cell.
    Bits mapping: 00 -> bg_color, 01 -> L1, 10 -> L2, 11 -> L3.

    Slot assignment policy, chosen for oscar64 LZO:
      * If the cell needs nothing outside (prev_l1, prev_l2, prev_l3), reuse that
        triple verbatim so the screen and colour RAM bytes repeat exactly.
      * Otherwise the triple is stored in ascending order. This canonicalisation
        is what makes screen/colour RAM compress to a few dozen bytes: two cells
        using the same colour set always emit the same screen/colour bytes, and
        the same pixel pattern then emits the same bitmap bytes too.
      * Slots not actually used by the cell inherit the previous cell's value,
        which is free (those bit patterns never occur) and keeps the screen and
        colour RAM bytes stable.
      * A colour in `pin_color_ram` overrides all of the above and is placed in
        L3, so the program can find it at a known address at run time.
    """
    color_freq = [0] * 16
    pixel_best_colors = []
    for dists in pixel_dists:
        best_c = min(range(16), key=lambda c: dists[c])
        pixel_best_colors.append(best_c)
        color_freq[best_c] += 1

    needed_colors = sorted({c for c in pixel_best_colors if c != bg_color})
    prev = (prev_l1, prev_l2, prev_l3)

    if all(c in prev for c in needed_colors):
        # Full reuse: identical screen + colour RAM bytes as the previous cell.
        l1, l2, l3 = prev
    elif len(needed_colors) == 3:
        l1, l2, l3 = needed_colors
    elif len(needed_colors) < 3:
        # Place the needed colours in ascending order into the lowest slots that
        # are not already carrying one of them; leave the rest inheriting `prev`.
        slots = list(prev)
        free = [i for i, c in enumerate(slots) if c not in needed_colors]
        for c, i in zip([c for c in needed_colors if c not in slots], free):
            slots[i] = c
        l1, l2, l3 = slots
    else:
        # More than three colours needed: search triples, canonically sorted.
        candidates = sorted(needed_colors, key=lambda c: color_freq[c], reverse=True)
        if len(candidates) < 6:
            extra = [c for c in range(16) if c != bg_color and c not in candidates]
            candidates.extend(extra[: 6 - len(candidates)])
        else:
            candidates = candidates[:6]

        best_error = float('inf')
        best_overlap = -1
        best_triple = tuple(sorted(prev))

        for triple in combinations(sorted(set(candidates)), 3):
            c1, c2, c3 = triple
            err = 0.0
            for dists in pixel_dists:
                d = dists[bg_color]
                if dists[c1] < d:
                    d = dists[c1]
                if dists[c2] < d:
                    d = dists[c2]
                if dists[c3] < d:
                    d = dists[c3]
                err += d
            # Tie-break towards the previous cell's slot layout: identical
            # screen/colour bytes cost nothing extra in the compressed stream.
            overlap = sum(1 for a, b in zip(triple, prev) if a == b)
            if err < best_error or (err == best_error and overlap > best_overlap):
                best_error = err
                best_overlap = overlap
                best_triple = triple

        l1, l2, l3 = best_triple

    allowed = ((bg_color, 0), (l1, 1), (l2, 2), (l3, 3))
    bit_assignments = []
    for dists in pixel_dists:
        best_b = 0
        best_d = dists[bg_color]
        for color, code in allowed[1:]:
            d = dists[color]
            if d < best_d:
                best_d = d
                best_b = code
        bit_assignments.append(best_b)

    if pin_color_ram:
        l1, l2, l3, bit_assignments = _pin_to_color_ram(
            l1, l2, l3, bit_assignments, pin_color_ram)

    return l1, l2, l3, bit_assignments


def encode_cells(cell_dists, bg_color: int,
                 pin_color_ram: frozenset[int] = frozenset()
                 ) -> list[tuple[int, int, int, list[int]]]:
    """Runs optimize_cell over the whole 40x25 matrix in char order."""
    cells = []
    prev_l1, prev_l2, prev_l3 = 13, 13, 13
    for cell_idx in range(1000):
        try:
            l1, l2, l3, bits = optimize_cell(
                cell_dists[cell_idx], bg_color, prev_l1, prev_l2, prev_l3,
                pin_color_ram
            )
        except ValueError as e:
            raise ValueError(f"cell {cell_idx} "
                             f"(row {cell_idx // 40}, col {cell_idx % 40}): {e}") from None
        prev_l1, prev_l2, prev_l3 = l1, l2, l3
        cells.append((l1, l2, l3, bits))
    return cells


# ---------------------------------------------------------------------------
# Cell snapping (lossy compression pass)
# ---------------------------------------------------------------------------

def snap_cells(
    cells: list[tuple[int, int, int, list[int]]],
    cell_dists,
    bg_color: int,
    tolerance: int,
    max_error: float,
    window: int = CELL_WINDOW,
) -> int:
    """
    Lossy pass: force a cell's 8 bitmap bytes to be byte-identical to an earlier
    cell within `window` cells, when the earlier cell uses the same colour slots
    and the substitution is cheap enough.

    An exact 8-byte repeat inside the 255-byte window turns 8 literal bytes into
    a 2-byte match (and often extends an existing match instead of costing
    anything at all). Candidates are compared against already-snapped cells, so
    runs of similar cells collapse onto a single template.

    Two gates, both of which must pass:
      `tolerance` - at most this many of the 32 pixels may change. Bounds how
                    much *structure* is lost, and is the master on/off switch.
      `max_error` - no single pixel may get worse by more than this much
                    weighted RGB distance. Bounds how much *contrast* is lost.
                    A mean or total budget is the wrong statistic here: it lets
                    one catastrophic flip hide behind 31 harmless ones, which is
                    exactly how text turns to mush. Swapping between two similar
                    shades costs ~15k-30k, a black<->white flip costs 585k, and
                    only the first kind is safe.

    Returns the number of cells snapped.
    """
    if tolerance <= 0:
        return 0

    snapped = 0
    for idx in range(1, 1000):
        l1, l2, l3, bits = cells[idx]
        dists_for_cell = cell_dists[idx]
        colors = (bg_color, l1, l2, l3)

        best_cand = None
        best_error = float('inf')
        lo = idx - window
        if lo < 0:
            lo = 0
        for j in range(lo, idx):
            cl1, cl2, cl3, cbits = cells[j]
            if cl1 != l1 or cl2 != l2 or cl3 != l3:
                continue

            diff = 0
            err = 0.0
            ok = True
            for p in range(32):
                dists = dists_for_cell[p]
                own = dists[colors[bits[p]]]
                if cbits[p] != bits[p]:
                    diff += 1
                    cand = dists[colors[cbits[p]]]
                    if diff > tolerance or cand - own > max_error:
                        ok = False
                        break
                    err += cand
                else:
                    err += own
            if not ok or diff == 0:
                continue

            if err < best_error:
                best_error = err
                best_cand = cbits

        if best_cand is not None:
            cells[idx] = (l1, l2, l3, list(best_cand))
            snapped += 1

    return snapped


# ---------------------------------------------------------------------------
# Embed plan / lossless slot optimization
# ---------------------------------------------------------------------------

BITMAP_BASE, SCREEN_BASE, COLOR_BASE = 2, 8002, 9002


class EmbedPlan:
    """
    The byte ranges of the .koa that actually end up in the binary, as file
    offsets matching oscar64's `#embed LEN OFFSET lzo "file.koa"`.

    Each range is compressed as its own independent LZO stream, so only these
    ranges determine the size contribution, and each one starts with a cold
    255-byte window. A weight of 0 marks a range that lands in linker slack and
    therefore costs nothing (see --embed).
    """

    def __init__(self, ranges: list[tuple[int, int, float]] | None = None):
        if ranges:
            self.ranges = list(ranges)
        else:
            self.ranges = [(BITMAP_BASE, 8000, 1.0),
                           (SCREEN_BASE, 1000, 1.0),
                           (COLOR_BASE, 1000, 1.0)]

    @staticmethod
    def parse(spec: str) -> tuple[int, int, float]:
        parts = spec.split(":")
        if len(parts) not in (2, 3):
            raise argparse.ArgumentTypeError(
                f"--embed expects OFFSET:LENGTH[:WEIGHT], got {spec!r}")
        off, length = int(parts[0], 0), int(parts[1], 0)
        weight = float(parts[2]) if len(parts) == 3 else 1.0
        return off, length, weight

    def covers(self, lo: int, hi: int) -> bool:
        """True if the whole byte range [lo, hi) sits inside one embedded range."""
        return any(off <= lo and hi <= off + length for off, length, _ in self.ranges)

    def objective(self, koa) -> float:
        return sum(w * oscar64_lzo_size(bytes(koa[off:off + length]))
                   for off, length, w in self.ranges if w)

    def report(self, koa) -> list[tuple[int, int, float, int]]:
        return [(off, length, w, oscar64_lzo_size(bytes(koa[off:off + length])))
                for off, length, w in self.ranges]

    def permutable_cells(self, ) -> list[int]:
        """
        A cell may only be re-slotted if its bitmap, screen and colour bytes are
        all embedded together. If the bitmap is embedded but the screen/colour
        bytes are not, those come from somewhere else at run time and re-labeling
        the bit patterns would change what is displayed.
        """
        out = []
        for c in range(1000):
            bm = self.covers(BITMAP_BASE + c * 8, BITMAP_BASE + c * 8 + 8)
            sc = self.covers(SCREEN_BASE + c, SCREEN_BASE + c + 1)
            cr = self.covers(COLOR_BASE + c, COLOR_BASE + c + 1)
            if bm and sc and cr:
                out.append(c)
        return out


# The six ways of relabeling the three local-colour slots. Index by old code.
_SLOT_PERMUTATIONS = ((0, 1, 2, 3), (0, 1, 3, 2), (0, 2, 1, 3),
                      (0, 2, 3, 1), (0, 3, 1, 2), (0, 3, 2, 1))


def _permute_cell(l1: int, l2: int, l3: int, bits: list[int], perm,
                  free_fill: tuple[int, int, int] | None):
    """
    Applies a slot relabeling to one cell. Every pixel keeps its exact colour:
    a pixel whose code was c is rewritten to perm[c], and the colour that lived
    in slot c moves to slot perm[c]. Slots no pixel references are free and take
    their value from `free_fill` (normally the previous cell, so the screen and
    colour RAM bytes stay repetitive).
    """
    old = (None, l1, l2, l3)
    new_bits = [perm[b] for b in bits]
    used = set(bits)
    slots = [None, None, None, None]
    for c in (1, 2, 3):
        if c in used:
            slots[perm[c]] = old[c]
    for s in (1, 2, 3):
        if slots[s] is None:
            slots[s] = free_fill[s - 1] if free_fill else old[s]
    return slots[1], slots[2], slots[3], new_bits


def _write_cell(koa, idx: int, l1: int, l2: int, l3: int, bits: list[int]) -> None:
    koa[SCREEN_BASE + idx] = ((l1 & 0x0F) << 4) | (l2 & 0x0F)
    koa[COLOR_BASE + idx] = l3 & 0x0F
    base = BITMAP_BASE + idx * 8
    for ry in range(8):
        v = 0
        for rx in range(4):
            v |= (bits[ry * 4 + rx] & 0x03) << (6 - rx * 2)
        koa[base + ry] = v


def optimize_slots(cells, plan: EmbedPlan, passes: int = 8, verbose: bool = True,
                   pin_color_ram: frozenset[int] = frozenset()):
    """
    Lossless post-pass. Within a cell, which of L1/L2/L3 holds which colour is
    arbitrary, and slots no pixel uses may hold anything at all. Both choices
    change the emitted bytes without changing a single pixel, so they are free
    to pick for compression.

    Free to pick for *pixels*, that is. A colour in `pin_color_ram` is one the
    program pokes at run time, so where it sits is part of the interface and
    candidates that move it out of colour RAM are dropped.

    This matters because the bitmap stream does not encode colours at all, only
    2-bit patterns: two cells that look different can still be made to emit the
    same eight bytes, which inside the 255-byte window turns them into a 2-byte
    match. The per-cell encoder cannot see this because it decides each cell in
    isolation.

    Greedy, cell by cell, repeated until no cell improves. Candidates are
    screened on a 263-byte window (the LZO history plus the cell) and only the
    survivors are scored on the real objective, which is the weighted sum of the
    embedded ranges' compressed sizes.
    """
    koa = build_koa(cells, 0)
    targets = plan.permutable_cells()
    if not targets:
        return 0, 0.0, 0.0

    start_obj = plan.objective(koa)
    current = start_obj
    total_changed = 0

    for _ in range(passes):
        changed = 0
        for idx in targets:
            l1, l2, l3, bits = cells[idx]
            if idx > 0:
                p1, p2, p3, _ = cells[idx - 1]
                fills = ((p1, p2, p3), None)
            else:
                fills = (None,)

            base = BITMAP_BASE + idx * 8
            lo = max(0, base - LZO_WINDOW)
            slo = max(0, SCREEN_BASE + idx - LZO_WINDOW)
            clo = max(0, COLOR_BASE + idx - LZO_WINDOW)
            best = None
            seen = set()
            scored = []
            for perm in _SLOT_PERMUTATIONS:
                for fill in fills:
                    cand = _permute_cell(l1, l2, l3, bits, perm, fill)
                    if pin_color_ram and (cand[0] in pin_color_ram
                                          or cand[1] in pin_color_ram):
                        continue
                    key = (cand[0], cand[1], cand[2], tuple(cand[3]))
                    if key in seen:
                        continue
                    seen.add(key)
                    _write_cell(koa, idx, *cand)
                    # Screen on the local LZO window only: the full objective is
                    # ~500x more expensive, so it is spent on the survivors.
                    est = (oscar64_lzo_size(bytes(koa[lo:base + 8]))
                           + oscar64_lzo_size(bytes(koa[slo:SCREEN_BASE + idx + 1]))
                           + oscar64_lzo_size(bytes(koa[clo:COLOR_BASE + idx + 1])))
                    scored.append((est, cand))
            _write_cell(koa, idx, l1, l2, l3, bits)

            scored.sort(key=lambda t: t[0])
            for _, cand in scored[:4]:
                _write_cell(koa, idx, *cand)
                value = plan.objective(koa)
                if value < current:
                    current = value
                    best = cand
                _write_cell(koa, idx, l1, l2, l3, bits)

            if best is not None:
                _write_cell(koa, idx, *best)
                cells[idx] = (best[0], best[1], best[2], best[3])
                changed += 1

        total_changed += changed
        if verbose:
            print(f"  slot optimization pass: {changed} cells re-slotted, "
                  f"objective {current:.0f}")
        if not changed:
            break

    return total_changed, start_obj, current


def verify_pins(cells, pin_color_ram: frozenset[int]) -> list[int]:
    """
    Returns the cells where a pinned colour did not end up in colour RAM. Must
    be empty: the whole point of a pin is that the C64 side can name one address
    per cell and know what it is writing.
    """
    return [idx for idx, (l1, l2, _l3, _bits) in enumerate(cells)
            if l1 in pin_color_ram or l2 in pin_color_ram]


def verify_lossless(cells_before, cells_after, bg_color: int) -> int:
    """Counts multicolor pixels whose rendered colour changed. Must be zero."""
    diff = 0
    for (a1, a2, a3, abits), (b1, b2, b3, bbits) in zip(cells_before, cells_after):
        ac, bc = (bg_color, a1, a2, a3), (bg_color, b1, b2, b3)
        for p in range(32):
            if ac[abits[p]] != bc[bbits[p]]:
                diff += 1
    return diff


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def build_koa(cells, bg_color: int) -> bytearray:
    """Assembles the 10003 byte Koala Painter buffer."""
    koa = bytearray(10003)
    koa[0] = 0x00           # load address $6000
    koa[1] = 0x60
    koa[10002] = bg_color

    for cell_idx, (l1, l2, l3, bits) in enumerate(cells):
        koa[8002 + cell_idx] = ((l1 & 0x0F) << 4) | (l2 & 0x0F)
        koa[9002 + cell_idx] = l3 & 0x0F
        base = 2 + cell_idx * 8
        for ry in range(8):
            byte_val = 0
            for rx in range(4):
                byte_val |= (bits[ry * 4 + rx] & 0x03) << (6 - rx * 2)
            koa[base + ry] = byte_val
    return koa


def render_preview(cells, bg_color: int) -> Image.Image:
    """Renders the Pepto-palette 320x200 preview of the encoded cells."""
    img = Image.new("RGB", (320, 200))
    px = img.load()
    for cell_idx, (l1, l2, l3, bits) in enumerate(cells):
        cy, cx = divmod(cell_idx, 40)
        bit_to_color = (bg_color, l1, l2, l3)
        for ry in range(8):
            y = cy * 8 + ry
            for rx in range(4):
                rgb = PEPTO_PALETTE[bit_to_color[bits[ry * 4 + rx]]]
                x = (cx * 4 + rx) * 2
                px[x, y] = rgb
                px[x + 1, y] = rgb
    return img


def stream_sizes(koa: bytearray) -> tuple[int, int, int]:
    """LZO sizes of the three streams as oscar64 embeds them separately."""
    return (
        oscar64_lzo_size(bytes(koa[2:8002])),
        oscar64_lzo_size(bytes(koa[8002:9002])),
        oscar64_lzo_size(bytes(koa[9002:10002])),
    )


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def convert_png_to_koala(
    input_path: str,
    koa_output_path: str,
    png_output_path: str | None = None,
    bg_color: int | None = None,
    bg_search: int = 0,
    snap_tolerance: int = 0,
    snap_max_error: float = 40000.0,
    snap_window: int = CELL_WINDOW,
    plan: EmbedPlan | None = None,
    optimize_slot_order: bool = False,
    pin_color_ram: frozenset[int] = frozenset(),
) -> None:
    """
    Loads a PNG, encodes it to Koala Painter format, optionally searching for the
    background colour that minimises the oscar64 LZO size, and optionally running
    the lossy cell-snapping pass.
    """
    img = Image.open(input_path)
    grid = downsample_to_multicolor_grid(img)
    cell_dists = build_cell_dists(grid)
    plan = plan or EmbedPlan()
    pin_color_ram = frozenset(pin_color_ram)

    def encode(bg: int):
        cells = encode_cells(cell_dists, bg, pin_color_ram)
        snapped = snap_cells(cells, cell_dists, bg, snap_tolerance,
                             snap_max_error, snap_window)
        return cells, snapped

    def mean_error(cells, bg: int) -> float:
        total = 0.0
        for idx, (l1, l2, l3, bits) in enumerate(cells):
            colors = (bg, l1, l2, l3)
            dists_for_cell = cell_dists[idx]
            total += sum(dists_for_cell[p][colors[bits[p]]] for p in range(32))
        return total / 32000.0

    if bg_search:
        counts = [0] * 16
        for dists_for_cell in cell_dists:
            for dists in dists_for_cell:
                counts[min(range(16), key=lambda c: dists[c])] += 1
        candidates = sorted(range(16), key=lambda c: counts[c], reverse=True)[:bg_search]
        if bg_color is not None and (bg_color & 0x0F) not in candidates:
            candidates.append(bg_color & 0x0F)

        print(f"Searching {len(candidates)} background colors for smallest LZO size...")
        best = None
        for bg in candidates:
            cells, snapped = encode(bg)
            koa = build_koa(cells, bg)
            bm, sc, cr = stream_sizes(koa)
            total = bm + sc + cr
            print(f"  bg {bg:2d}: bitmap {bm:5d}  screen {sc:4d}  color {cr:4d}"
                  f"  total {total:5d}  err/px {mean_error(cells, bg):8.1f}")
            if best is None or total < best[0]:
                best = (total, bg, cells, snapped)
        _, bg_color, cells, snapped = best
        print(f"Selected background color: {bg_color} ({PEPTO_PALETTE[bg_color]})"
              f" -- check err/px above, the smallest stream is not always the best picture")
    else:
        if bg_color is None:
            bg_color = auto_detect_bg_color(cell_dists)
            print(f"Auto-selected global background color: {bg_color} ({PEPTO_PALETTE[bg_color]})")
        else:
            bg_color &= 0x0F
            print(f"Using user background color: {bg_color}")
        cells, snapped = encode(bg_color)

    if optimize_slot_order:
        targets = plan.permutable_cells()
        partial = sum(
            1 for c in range(1000)
            if plan.covers(BITMAP_BASE + c * 8, BITMAP_BASE + c * 8 + 8)
            and not (plan.covers(SCREEN_BASE + c, SCREEN_BASE + c + 1)
                     and plan.covers(COLOR_BASE + c, COLOR_BASE + c + 1))
        )
        print(f"Lossless slot optimization over {len(targets)} of 1000 cells"
              + (f" ({partial} skipped: bitmap embedded but screen/color are not)"
                 if partial else ""))
        before = [(a, b, c, list(d)) for a, b, c, d in cells]
        changed, obj0, obj1 = optimize_slots(cells, plan,
                                             pin_color_ram=pin_color_ram)
        moved = verify_lossless(before, cells, bg_color)
        if moved:
            raise AssertionError(
                f"slot optimization changed {moved} pixels - this is a bug, "
                "the pass must be lossless")
        print(f"  {changed} cells re-slotted, objective {obj0:.0f} -> {obj1:.0f} "
              f"({obj1 - obj0:+.0f}), 0 of 32000 pixels changed")

    if pin_color_ram:
        loose = verify_pins(cells, pin_color_ram)
        if loose:
            raise AssertionError(
                f"{len(loose)} cells hold a pinned colour outside colour RAM, "
                f"first at row {loose[0] // 40} col {loose[0] % 40} - this is a bug")
        held = sum(1 for l1, l2, l3, _ in cells if l3 in pin_color_ram)
        print(f"Pinned to colour RAM: {', '.join(str(c) for c in sorted(pin_color_ram))}"
              f" ({held} cells carry one)")

    koa_bytes = build_koa(cells, bg_color)

    with open(koa_output_path, "wb") as f:
        f.write(koa_bytes)

    bm, sc, cr = stream_sizes(koa_bytes)
    zlib_size = len(zlib.compress(bytes(koa_bytes), 9))

    print(f"Saved Koala Painter file: {koa_output_path} ({len(koa_bytes)} bytes)")
    print(f"Mean weighted quantization error: {mean_error(cells, bg_color):.1f} per pixel")
    if snap_tolerance > 0:
        print(f"Cell snapping: tolerance {snap_tolerance} px, max added error "
              f"{snap_max_error:g}/px, window {snap_window} cells, "
              f"{snapped} of 1000 cells snapped")
    print("oscar64 LZO sizes (each #embed range is compressed as its own stream):")
    print(f"  - Bitmap RAM (8000 B): {bm:5d}  ({100.0 * bm / 8000:.1f}%)")
    print(f"  - Screen RAM (1000 B): {sc:5d}  ({100.0 * sc / 1000:.1f}%)")
    print(f"  - Color RAM  (1000 B): {cr:5d}  ({100.0 * cr / 1000:.1f}%)")
    print(f"  - Total:               {bm + sc + cr:5d}   (zlib -9 reference: {zlib_size})")

    if plan.ranges != EmbedPlan().ranges:
        print("Embedded ranges (what actually reaches the binary):")
        billed = 0.0
        for off, length, w, size in plan.report(koa_bytes):
            note = "" if w else "   [weight 0, lands in linker slack]"
            billed += w * size
            print(f"  - #embed {length} {off}: {size:5d} bytes"
                  f"{'' if w == 1.0 else f'  x{w:g}'}{note}")
        print(f"  - Billed total:        {billed:.0f}")

    if png_output_path:
        render_preview(cells, bg_color).save(png_output_path)
        print(f"Saved Pepto palette preview PNG: {png_output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Convert a 320x200 PNG image into a C64 Koala Painter (.koa) file and Pepto palette PNG preview.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "The encoder targets oscar64's `#embed ... lzo` compressor: 255-byte\n"
            "window, 4..127 byte matches at 2 bytes each, literal runs at 1+n bytes.\n"
            "Because the window is only ~31 char cells wide, only horizontal reuse\n"
            "compresses; use --snap-tolerance to trade a little accuracy for it."
        ),
    )
    parser.add_argument("input_png", help="Input 320x200 PNG image file path")
    parser.add_argument("output_koa", nargs="?", help="Output Koala Painter (.koa) file path (default: <input>.koa)")
    parser.add_argument("output_png", nargs="?", help="Output preview PNG file path (default: <input>_pepto.png)")
    parser.add_argument("--bg-color", type=int, choices=range(16), metavar="0..15",
                        help="Force global background color (0..15)")
    parser.add_argument("--bg-search", type=int, nargs="?", const=6, default=0, metavar="N",
                        help="Try the N most common colors as background and keep the one with "
                             "the smallest total LZO size (default N=6). Prints the quantization "
                             "error of each candidate so a size win can be sanity-checked.")
    parser.add_argument("--snap-tolerance", type=int, default=0, metavar="PX",
                        help="Lossy: snap a cell's bitmap onto an identical-color cell within the "
                             "LZO window when they differ in at most PX of 32 pixels. 0 disables "
                             "(default). 2 is near-invisible, 4-8 trades more for size.")
    parser.add_argument("--snap-max-error", type=float, default=40000.0, metavar="E",
                        help="Cap on how much worse any single pixel may get when snapping, in "
                             "weighted RGB distance squared (default 40000; two adjacent C64 "
                             "shades are ~15000-30000 apart, black to white is 585225). This is "
                             "what keeps high-contrast detail such as text intact.")
    parser.add_argument("--embed", action="append", default=None, metavar="OFF:LEN[:W]",
                        help="A byte range of the .koa that the program actually embeds, "
                             "matching `#embed LEN OFF lzo`. Repeatable. Optimization and "
                             "reporting then target exactly these ranges. Give a weight of 0 for "
                             "a range that lands in linker padding and so costs nothing.")
    parser.add_argument("--optimize-slots", action="store_true",
                        help="Lossless: re-label each cell's L1/L2/L3 slots, and fill unused "
                             "slots, to minimize the embedded ranges. Every pixel keeps its exact "
                             "color; the result is verified pixel-by-pixel before writing.")
    parser.add_argument("--pin-color-ram", type=int, action="append", default=None,
                        choices=range(16), metavar="0..15",
                        help="Keep this color in color RAM (bit pair 11) in every cell that "
                             "uses it, and never let the slot optimizer move it. Repeatable. "
                             "For colors the program pokes at run time: color RAM is one whole "
                             "byte per cell, so the C64 side can just store to it, while the "
                             "two screen RAM colors share a byte and can swap nibbles whenever "
                             "the image is re-encoded.")
    parser.add_argument("--snap-window", type=int, default=CELL_WINDOW, metavar="CELLS",
                        help=f"How many cells back snapping may reference (default {CELL_WINDOW}, "
                             "which is the 255-byte LZO window)")

    args = parser.parse_args()

    base_name, _ = os.path.splitext(args.input_png)
    output_koa = args.output_koa or f"{base_name}.koa"
    output_png = args.output_png or f"{base_name}_pepto.png"

    convert_png_to_koala(
        input_path=args.input_png,
        koa_output_path=output_koa,
        png_output_path=output_png,
        bg_color=args.bg_color,
        bg_search=args.bg_search,
        snap_tolerance=args.snap_tolerance,
        snap_max_error=args.snap_max_error,
        snap_window=args.snap_window,
        plan=EmbedPlan([EmbedPlan.parse(s) for s in args.embed]) if args.embed else None,
        optimize_slot_order=args.optimize_slots,
        pin_color_ram=frozenset(args.pin_color_ram or ()),
    )


if __name__ == "__main__":
    main()
