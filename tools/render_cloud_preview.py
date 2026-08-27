#!/usr/bin/env python3
"""Renders the cloud layout and measures how often you would see one.

docs/clouds.md §7: a verification tool, not part of the build. Changing
the density in generate_clouds.py should be answerable in a second, and
"fly around for a while and see how it feels" is not that.

Two outputs:

  out/cloud_preview.png   the whole world from above - every cloud group at its
                          hashed position, the cell grid it came from, and a
                          sample viewport wedge for scale.
  stdout                  the density table of §2.4, sampled the way the
                          simulation would actually meet it: random positions,
                          random headings, several altitudes.

The numbers are the point. §2.4 asks for "you see a group about 40% of the time
and two at once rarely", and the only honest way to know is to sample it.
"""

import argparse
import math
import os
import random
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

from lib import clouddef  # noqa: E402
from lib import spritedef  # noqa: E402

# The viewport, from mem.h and the projection in vec.cc. vec_project() scales
# by 256 and gfx.cc uses the result as a pixel offset directly, so the
# half-angles are atan(160/256) and atan(56/256). See docs/clouds.md §2.6.
PROJECT_SCALE = 256
HALF_W_PX = 160
HALF_H_PX = 56


def visible_groups(eye_x, eye_y, eye_z, heading):
    """The groups whose centre is in the viewport, as (dx, dy, depth, rung).

    Mirrors what clouds.cc will do: scan the 5 x 5 cells around the eye, reject
    on depth, project, reject off screen.
    """
    cf, sf = math.cos(heading), math.sin(heading)
    ccx = int(math.floor(eye_x / clouddef.CELL_U))
    ccy = int(math.floor(eye_y / clouddef.CELL_U))
    out = []
    r = clouddef.SCAN_RADIUS
    for dx in range(-r, r + 1):
        for dy in range(-r, r + 1):
            g = clouddef.group_at(ccx + dx, ccy + dy)
            if g is None:
                continue
            rx = g["x"] - eye_x
            ry = g["y"] - eye_y
            rz = clouddef.DECK_U - eye_z
            fwd = cf * rx + sf * ry          # camera forward
            left = -sf * rx + cf * ry        # camera left
            if fwd <= 8:
                continue
            rung = clouddef.rung_for_depth(fwd)
            if rung is None:
                continue
            sx = PROJECT_SCALE * left / fwd
            sy = PROJECT_SCALE * rz / fwd
            # Half a blob of slack, so a group half on screen still counts.
            if abs(sx) > HALF_W_PX + 24 or abs(sy) > HALF_H_PX + 21:
                continue
            out.append((g, fwd, rung))
    return out


def density_table(samples, altitudes_m, seed=1):
    rng = random.Random(seed)
    world_x = clouddef.CELLS_X * clouddef.CELL_U
    world_y = clouddef.CELLS_Y * clouddef.CELL_U
    rows = []
    for alt_m in altitudes_m:
        counts = []
        slots = []
        for _ in range(samples):
            vis = visible_groups(
                rng.uniform(0, world_x),
                rng.uniform(0, world_y),
                alt_m / 2.0,
                rng.uniform(0, 2 * math.pi),
            )
            counts.append(len(vis))
            slots.append(
                sum(
                    clouddef.BLOBS_PER_GROUP
                    * (2 if rung >= clouddef.RUNG_STACKED else 1)
                    for _, _, rung in vis
                )
            )
        n = len(counts)
        rows.append({
            "alt": alt_m,
            "mean": sum(counts) / n,
            "p0": counts.count(0) / n,
            "p1": counts.count(1) / n,
            "p2": sum(1 for c in counts if c >= 2) / n,
            "slots_mean": sum(slots) / n,
            "slots_over": sum(1 for s in slots if s > 6) / n,
            "slots_max": max(slots),
        })
    return rows


def print_report(rows, samples):
    cells = clouddef.CELLS_X * clouddef.CELLS_Y
    present = sum(
        1
        for cx in range(clouddef.CELLS_X)
        for cy in range(clouddef.CELLS_Y)
        if clouddef.group_at(cx, cy)
    )
    print("Cloud layout")
    print("  cell        %5d m   (%d x %d cells, %d in the world)"
          % (clouddef.CELL_U * 2, clouddef.CELLS_X, clouddef.CELLS_Y, cells))
    print("  gate      %2d/32      %d of %d cells carry a group (%.0f%%)"
          % (clouddef.GATE_LIMIT, present, cells, 100 * present / cells))
    print("  blob        %5d m   deck %d m, cull %d m"
          % (clouddef.BLOB_U * 2, clouddef.DECK_U * 2, clouddef.CULL_U * 2))
    print()
    print("Groups in the viewport, over %d random positions and headings each"
          % samples)
    print("  %-9s %7s %7s %7s %7s" % ("eye alt", "E[n]", "P(0)", "P(1)", "P(>=2)"))
    for r in rows:
        print("  %6d m  %7.2f %7.2f %7.2f %7.2f"
              % (r["alt"], r["mean"], r["p0"], r["p1"], r["p2"]))
    print()
    print("Sprite slots the clouds ask for (6 available; the sun takes the 7th,")
    print("and index 7 is reserved for the vertical-speed needle - clouds.md §1.9)")
    print("  %-9s %7s %9s %6s" % ("eye alt", "mean", "P(> 6)", "max"))
    for r in rows:
        print("  %6d m  %7.2f %9.3f %6d"
              % (r["alt"], r["slots_mean"], r["slots_over"], r["slots_max"]))
    print()
    over = max(r["slots_over"] for r in rows)
    print("  The stack drops the farthest entry when it runs out, so P(> 6) is")
    print("  how often that happens: %.1f%% of frames at worst." % (100 * over))


# --- The picture ------------------------------------------------------------

def render(out_path, eye=None, heading=0.0, scale=3):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        sys.exit("render_cloud_preview: needs Pillow (pip install pillow)")

    # One pixel per 256 m at scale 1, which puts the whole 33 x 66 km world in
    # a sensible picture.
    world_x = clouddef.CELLS_X * clouddef.CELL_U
    world_y = clouddef.CELLS_Y * clouddef.CELL_U
    px_per_u = scale / 128.0
    w = int(world_y * px_per_u)   # world Y is west, drawn across
    h = int(world_x * px_per_u)   # world X is north, drawn down

    im = Image.new("RGB", (w, h), (24, 34, 58))
    d = ImageDraw.Draw(im, "RGBA")

    def to_px(wx, wy):
        return (wy * px_per_u, wx * px_per_u)

    # The map tiles the terrain uses, for scale: 2048 m, half a cloud cell.
    tile_u = clouddef.CELL_U // 2
    for i in range(0, world_y // tile_u + 1):
        x = i * tile_u * px_per_u
        d.line([(x, 0), (x, h)], fill=(255, 255, 255, 18))
    for i in range(0, world_x // tile_u + 1):
        y = i * tile_u * px_per_u
        d.line([(0, y), (w, y)], fill=(255, 255, 255, 18))
    # The cloud cell grid on top of it.
    for i in range(clouddef.CELLS_Y + 1):
        x = i * clouddef.CELL_U * px_per_u
        d.line([(x, 0), (x, h)], fill=(255, 255, 255, 90))
    for i in range(clouddef.CELLS_X + 1):
        y = i * clouddef.CELL_U * px_per_u
        d.line([(0, y), (w, y)], fill=(255, 255, 255, 90))

    # Every group, with its three blobs at the pattern's offsets so the shape
    # of a group is visible, not just where it is.
    blob_r = max(1.5, clouddef.BLOB_U * px_per_u / 2)
    half = clouddef.OFFSET_U / 2.0
    for cx in range(clouddef.CELLS_X):
        for cy in range(clouddef.CELLS_Y):
            g = clouddef.group_at(cx, cy)
            if g is None:
                continue
            for ax, ay, _az in clouddef.GROUP_OFFSET[g["pattern"]]:
                bx = g["x"] + ax * half
                by = g["y"] + ay * half
                px, py = to_px(bx, by)
                d.ellipse([px - blob_r, py - blob_r, px + blob_r, py + blob_r],
                          fill=(235, 240, 255, 220))
            px, py = to_px(g["x"], g["y"])
            d.ellipse([px - 1, py - 1, px + 1, py + 1], fill=(120, 170, 255, 255))

    # A sample viewport: the 64 degree wedge out to the cull, and the 5 x 5
    # scan block it draws its candidates from.
    if eye is None:
        eye = (world_x * 0.5, world_y * 0.42)
    ex, ey = eye
    ccx = int(math.floor(ex / clouddef.CELL_U))
    ccy = int(math.floor(ey / clouddef.CELL_U))
    r = clouddef.SCAN_RADIUS
    x0, y0 = to_px((ccx - r) * clouddef.CELL_U, (ccy - r) * clouddef.CELL_U)
    x1, y1 = to_px((ccx + r + 1) * clouddef.CELL_U, (ccy + r + 1) * clouddef.CELL_U)
    d.rectangle([x0, y0, x1, y1], outline=(255, 210, 90, 160))

    half_fov = math.atan(HALF_W_PX / PROJECT_SCALE)
    cull = clouddef.CULL_U
    pts = [to_px(ex, ey)]
    steps = 24
    for i in range(steps + 1):
        a = heading - half_fov + 2 * half_fov * i / steps
        pts.append(to_px(ex + cull * math.cos(a), ey + cull * math.sin(a)))
    d.polygon(pts, fill=(255, 210, 90, 40), outline=(255, 210, 90, 200))

    # A caption, so the picture still says what it is once it has been pasted
    # somewhere else.
    present = sum(
        1
        for cx in range(clouddef.CELLS_X)
        for cy in range(clouddef.CELLS_Y)
        if clouddef.group_at(cx, cy)
    )
    d.rectangle([0, 0, w, 16], fill=(12, 18, 32, 210))
    d.text(
        (6, 4),
        "gate 0x%02X - %d of %d cells - cell %d m - blob %d m - deck %d m - "
        "cull %d m   |   box: the 5 x 5 scan, wedge: the 64 deg viewport"
        % (clouddef.GATE_LIMIT, present, clouddef.CELLS_X * clouddef.CELLS_Y,
           clouddef.CELL_U * 2, clouddef.BLOB_U * 2, clouddef.DECK_U * 2,
           clouddef.CULL_U * 2),
        fill=(200, 214, 240),
    )

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    im.save(out_path)
    return out_path, im.size


# --- What a group looks like from the cockpit -------------------------------
#
# The map above answers "where are the clouds"; this answers the question §4
# actually cares about, which is whether three overlapping blobs land on one
# dither lattice or turn into a white lump. It is a bit-for-bit model of the
# runtime path - integer multiplies with the same truncation, the same pivot
# arithmetic, the same snap - so a seam here is a seam in the emulator.

def _fastmul8p8(a, b):
    """vec.h's vec_fastmul8p8: trunc(a * b / 256), rounding toward zero."""
    p = a * b
    mag = (-p if p < 0 else p) >> 8
    return -mag if p < 0 else mag


def _half_basis(heading):
    """clouds.cc's _clouds_build_basis() for a pure yaw of `heading`.

    Camera space is x forward, y left, z up, and vec_transform_inv() reads the
    matrix by rows, so the camera-space image of world axis i is column i:
    (front[i], left[i], up[i]).
    """
    c = int(round(math.cos(heading) * 256))
    s = int(round(math.sin(heading) * 256))
    front = (c, s, 0)
    left = (-s, c, 0)
    up = (0, 0, 256)
    k = clouddef.OFFSET_U >> 1          # the table counts in half steps
    return [
        (_fastmul8p8(front[i], k), _fastmul8p8(left[i], k),
         _fastmul8p8(up[i], k))
        for i in range(3)
    ]


def draw_rung(rung):
    """The ladder row a group at `rung` actually draws from (§3.5).

    Below RUNG_COLLAPSED the whole group is a single blob taken from the next
    rung up, which is about what the three of them covered. At and above it the
    group draws three blobs from its own row.
    """
    return rung + 1 if rung < clouddef.RUNG_COLLAPSED else rung


def cloud1_row(rung):
    """The META_CLOUD1 / PATTERNS_CLOUD1 index for a single-sprite ladder rung.

    One less than the rung. The flat table clouds.cc indexes keeps a dead row 0
    - the rung a collapsed group can never ask for - so that a rung number is a
    row number there, but the block that row used to own is the orientation
    indicator's now and META_CLOUD1 holds only the four real single-sprite
    rungs. This is the whole of the difference.
    """
    return rung - 1


def rung_meta(rung):
    """(pivot_x, pivot_y, [bitmap, ...]) for a rung, either half of the ladder.

    Above RUNG_STACKED a blob is two sprites: the second placed 21 lines below
    the first by sprites_stack_commit(), and drawn from the block whose dither
    phase was built with that same 21 in it (§4.4).
    """
    if rung < clouddef.RUNG_STACKED:
        i = cloud1_row(rung)
        m = spritedef.META_CLOUD1[i]
        return m["pivot_x"], m["pivot_y"], [spritedef.PATTERNS_CLOUD1[i]]
    m = spritedef.META_CLOUD2[rung - clouddef.RUNG_STACKED]
    j = 2 * (rung - clouddef.RUNG_STACKED)
    return m["pivot_x"], m["pivot_y"], [spritedef.PATTERNS_CLOUD2[j],
                                        spritedef.PATTERNS_CLOUD2[j + 1]]


def group_blobs_on_screen(depth, rel_z, pattern, heading, rung):
    """The three blobs of a group as (sprite_x, sprite_y) in viewport pixels.

    Mirrors clouds.cc and sprites_stack_add() exactly, including the
    X-expansion doubling the horizontal pivot and the §4.2 dither snap.
    Returns them nearest-first, which is the order the stack draws them in.
    """
    basis = _half_basis(heading)
    pivot_x, pivot_y, _ = rung_meta(draw_rung(rung))
    meta = {"pivot_x": pivot_x, "pivot_y": pivot_y}
    # A collapsed group is one blob at the group centre - the same single row of
    # zero coefficients clouds.cc feeds _clouds_add_step() (§3.5).
    coeff_rows = ([(0, 0, 0)] if rung < clouddef.RUNG_COLLAPSED
                  else clouddef.GROUP_OFFSET[pattern])
    out = []
    for coeffs in coeff_rows:
        v = [depth, 0, rel_z]
        for axis, coeff in enumerate(coeffs):
            if coeff == 0:
                continue
            h = basis[axis]
            sign = -1 if coeff < 0 else 1
            step = 2 if abs(coeff) > 1 else 1
            for i in range(3):
                v[i] += sign * h[i] * step
        if v[0] <= 8:
            continue
        sx = PROJECT_SCALE * v[1] // v[0]
        sy = PROJECT_SCALE * v[2] // v[0]
        x = 320 // 2 - sx - meta["pivot_x"] * 2      # expanded: pivot doubles
        y = 112 // 2 - sy - meta["pivot_y"]
        x &= ~1                                       # §4.2, sprites.cc
        y = (y & ~1) | ((x >> 1) & 1)
        out.append((v[0], x, y))
    out.sort(key=lambda b: b[0])
    return [(x, y) for _, x, y in out]


def _blit_expanded(canvas, bitmap, x, y, w, h):
    """One X-expanded hires sprite: a bitmap column is two screen pixels."""
    for r, row in enumerate(bitmap):
        py = y + r
        if py < 0 or py >= h:
            continue
        for c, ch in enumerate(row):
            if ch == ".":
                continue
            px = x + 2 * c
            if 0 <= px < w:
                canvas[py][px] = 1
            if 0 <= px + 1 < w:
                canvas[py][px + 1] = 1


def render_groups(out_path, rungs=None, headings=None, zoom=4):
    """A contact sheet of every pattern at every single-sprite rung."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        sys.exit("render_cloud_preview: needs Pillow (pip install pillow)")

    if rungs is None:
        rungs = list(range(clouddef.RUNG_COUNT))
    if headings is None:
        # Straight on, and off the cardinal axes where the offsets round worst.
        headings = [0.0, math.pi / 8, math.pi / 4]

    # A cell big enough for the widest sprite at the coarsest rung, plus the
    # offsets: 2 * 24 expanded pixels and change.
    cw, ch = 128, 76
    pad = 4
    cols = clouddef.PATTERN_COUNT * len(headings)
    rows_n = len(rungs)
    label_h = 16
    w = cols * (cw + pad) + pad
    h = rows_n * (ch + pad + label_h) + pad + label_h

    im = Image.new("RGB", (w * zoom // 2, h * zoom // 2), (10, 14, 26))
    d = ImageDraw.Draw(im)
    z = zoom / 2.0

    d.text((pad * z, 2 * z),
           "A group at each rung (rows) x pattern and heading (columns). "
           "White = sprite ink, on the shared dither lattice.",
           fill=(190, 205, 235))

    for ri, rung in enumerate(rungs):
        meta = clouddef.RUNG_DEPTH
        # Mid-rung depth, so the rung under test is the one selected.
        far = meta[rung]
        near = meta[rung + 1] if rung + 1 < len(meta) else meta[rung] * 3 // 4
        depth = (far + near) // 2
        top = label_h + pad + ri * (ch + pad + label_h)
        dr = draw_rung(rung)
        sm = (spritedef.META_CLOUD1[cloud1_row(dr)]
              if dr < clouddef.RUNG_STACKED
              else spritedef.META_CLOUD2[dr - clouddef.RUNG_STACKED])
        d.text((pad * z, top * z),
               "rung %d  -  depth %d units (%d m), sprite %d x %d, %d slot%s%s"
               % (rung, depth, depth * 2, sm["width"], sm["height"],
                  len(rung_meta(dr)[2]),
                  "" if dr < clouddef.RUNG_STACKED else "s each",
                  "  COLLAPSED: one blob from rung %d" % dr if dr != rung
                  else ""),
               fill=(150, 170, 205))
        for pi in range(clouddef.PATTERN_COUNT):
            for hi, heading in enumerate(headings):
                ci = pi * len(headings) + hi
                canvas = [[0] * cw for _ in range(ch)]
                blobs = group_blobs_on_screen(depth, 0, pi, heading, rung)
                ox = oy = 0
                # Recentre the group in the cell: the absolute screen position
                # is not what is being looked at here, the overlap is.
                if blobs:
                    ox = cw // 2 - (min(b[0] for b in blobs)
                                    + max(b[0] for b in blobs) + 48) // 2
                    tall = 21 * len(rung_meta(dr)[2])
                    oy = ch // 2 - (min(b[1] for b in blobs)
                                    + max(b[1] for b in blobs) + tall) // 2
                    ox &= ~1          # keep the lattice intact while shifting
                    oy &= ~1
                _, _, blocks = rung_meta(dr)
                for bx, by in blobs:
                    for bi, block in enumerate(blocks):
                        _blit_expanded(canvas, block, bx + ox,
                                       by + oy + 21 * bi, cw, ch)
                cx0 = pad + ci * (cw + pad)
                cy0 = top + label_h
                d.rectangle([cx0 * z, cy0 * z,
                             (cx0 + cw) * z - 1, (cy0 + ch) * z - 1],
                            fill=(18, 24, 40))
                for yy in range(ch):
                    for xx in range(cw):
                        if canvas[yy][xx]:
                            d.rectangle([(cx0 + xx) * z, (cy0 + yy) * z,
                                         (cx0 + xx + 1) * z - 1,
                                         (cy0 + yy + 1) * z - 1],
                                        fill=(255, 255, 255))
                if ri == 0:
                    d.text(((cx0 + 2) * z, (label_h + pad - 12) * z),
                           "p%d %d deg" % (pi, int(round(math.degrees(heading)))),
                           fill=(120, 145, 185))

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    im.save(out_path)
    return out_path, im.size


def lattice_report(rungs=None, headings=None):
    """Checks the thing §4 exists for, in numbers rather than by eye.

    Every set pixel of every blob of every group must satisfy the same lattice
    parity. If two blobs of a group disagree, their overlap fills solid white
    instead of staying a checkerboard, and that is the failure the snap and
    the procedural dither were both built to prevent.
    """
    if rungs is None:
        rungs = list(range(clouddef.RUNG_COUNT))
    if headings is None:
        headings = [i * math.pi / 16 for i in range(32)]
    bad = []
    overlaps = 0
    for rung in rungs:
        blocks = rung_meta(draw_rung(rung))[2]
        far = clouddef.RUNG_DEPTH[rung]
        near = (clouddef.RUNG_DEPTH[rung + 1]
                if rung + 1 < len(clouddef.RUNG_DEPTH)
                else clouddef.RUNG_DEPTH[rung] * 3 // 4)
        for depth in (near + 1, (far + near) // 2, far):
            for pattern in range(clouddef.PATTERN_COUNT):
                for heading in headings:
                    blobs = group_blobs_on_screen(
                        depth, 0, pattern, heading, rung)
                    parities = set()
                    seen = {}
                    for bx, by in blobs:
                      for bi, bmp in enumerate(blocks):
                        for r0, row in enumerate(bmp):
                            r = r0 + 21 * bi
                            for c, chx in enumerate(row):
                                if chx == ".":
                                    continue
                                # Both halves of the expanded pixel, not just
                                # the left one: an odd sprite X splits a single
                                # bitmap pixel across two lattice cells, and
                                # sampling one half would never see it.
                                for px in (bx + 2 * c, bx + 2 * c + 1):
                                    py = by + r
                                    parities.add(((px >> 1) + py) & 1)
                                    key = (px, py)
                                    if key in seen:
                                        overlaps += 1
                                    seen[key] = True
                    if len(parities) > 1:
                        bad.append((rung, pattern, depth,
                                    round(math.degrees(heading))))
    return bad, overlaps


def compare_gates(samples):
    """Search tables for each candidate gate and measure all of them.

    The tuning loop of §7 in one command. The tables are searched per gate -
    presence comes out of them - so comparing masks honestly means re-searching,
    which is why this cannot just re-mask the committed tables.
    """
    sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
    import generate_clouds as gc

    original = gc.GATE_LIMIT
    print("%-6s %-9s %6s %6s %6s %7s %9s"
          % ("gate", "cells", "E[n]", "P(0)", "P(1)", "P(>=2)", "slots P>7"))
    try:
        for mask in (0x01, 0x03, 0x07):
            gc.GATE_LIMIT = mask
            try:
                seed, tables, stats = gc.search_tables()
            except SystemExit:
                print("%-6s  no usable tables" % hex(mask))
                continue
            rows = _measure_with(gc, tables, samples)
            print("%-6s %3d/%-5d %6.2f %6.2f %6.2f %7.2f %9.3f"
                  % (hex(mask), stats["groups"], gc.CELLS_X * gc.CELLS_Y,
                     rows["mean"], rows["p0"], rows["p1"], rows["p2"],
                     rows["slots_over"]))
    finally:
        gc.GATE_LIMIT = original
    print()
    print("  To adopt one, set GATE_LIMIT / TARGET_CELLS in generate_clouds.py and rerun")
    print("  `make clouds` - the tables are searched for the gate, not reused.")


def _measure_with(gc, tables, samples, alt_m=700):
    rng = random.Random(1)
    depths = gc.rung_depths()

    def rung(d):
        if d > depths[0]:
            return None
        r = 0
        while r < len(depths) - 1 and d <= depths[r + 1]:
            r += 1
        return r

    wx, wy = gc.CELLS_X * gc.CELL_U, gc.CELLS_Y * gc.CELL_U
    counts, slots = [], []
    for _ in range(samples):
        ex, ey = rng.uniform(0, wx), rng.uniform(0, wy)
        ez, hd = alt_m / 2.0, rng.uniform(0, 2 * math.pi)
        cf, sf = math.cos(hd), math.sin(hd)
        ccx, ccy = int(ex // gc.CELL_U), int(ey // gc.CELL_U)
        n = s = 0
        for dx in range(-gc.SCAN_RADIUS, gc.SCAN_RADIUS + 1):
            for dy in range(-gc.SCAN_RADIUS, gc.SCAN_RADIUS + 1):
                g = gc.group_at(tables, ccx + dx, ccy + dy)
                if g is None:
                    continue
                rx = (ccx + dx) * gc.CELL_U + g["jx"] - ex
                ry = (ccy + dy) * gc.CELL_U + g["jy"] - ey
                rz = gc.DECK_U - ez
                fwd = cf * rx + sf * ry
                if fwd <= 8:
                    continue
                rr = rung(fwd)
                if rr is None:
                    continue
                if abs(PROJECT_SCALE * (-sf * rx + cf * ry) / fwd) > HALF_W_PX + 24:
                    continue
                if abs(PROJECT_SCALE * rz / fwd) > HALF_H_PX + 21:
                    continue
                n += 1
                s += gc.BLOBS_PER_GROUP * (2 if rr >= gc.RUNG_STACKED else 1)
        counts.append(n)
        slots.append(s)
    n = len(counts)
    return {
        "mean": sum(counts) / n,
        "p0": counts.count(0) / n,
        "p1": counts.count(1) / n,
        "p2": sum(1 for c in counts if c >= 2) / n,
        "slots_over": sum(1 for x in slots if x > 6) / n,
    }


def main():
    p = argparse.ArgumentParser(
        description="Preview the cloud layout and its density (clouds.md §7)."
    )
    p.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "out"))
    p.add_argument("--samples", type=int, default=4000)
    p.add_argument("--scale", type=int, default=3)
    p.add_argument("--no-image", action="store_true")
    p.add_argument("--compare-gates", action="store_true",
                   help="search and measure each candidate gate mask")
    p.add_argument("--groups", action="store_true",
                   help="also render out/cloud_groups.png, a group at every "
                        "rung, pattern and heading, and check the lattice")
    args = p.parse_args()

    if args.compare_gates:
        compare_gates(args.samples)
        return

    # The altitudes the aircraft actually flies at: mission starts are
    # kMissionStartZ * 256 m (0, 512, 1024 m), and the air-density penalty
    # in flight.cc begins at 2048 m. The deck sits inside that band.
    rows = density_table(args.samples, [100, 512, 1024, 1536, 2048])
    print_report(rows, args.samples)

    if not args.no_image:
        path, size = render(
            os.path.join(args.out_dir, "cloud_preview.png"), scale=args.scale
        )
        print()
        print("Wrote %s (%d x %d)" % (path, size[0], size[1]))

    if args.groups:
        bad, overlaps = lattice_report()
        print()
        print("Dither lattice across a group (docs/clouds.md §4)")
        print("  %d overlapping ink pixels sampled over every rung, pattern"
              % overlaps)
        print("  and 32 headings")
        if bad:
            print("  BROKEN: %d group(s) mix both lattice phases, e.g. %s"
                  % (len(bad), bad[:4]))
        else:
            print("  ok: every blob of every group lands on one phase")
        if not args.no_image:
            path, size = render_groups(
                os.path.join(args.out_dir, "cloud_groups.png"))
            print()
            print("Wrote %s (%d x %d)" % (path, size[0], size[1]))


if __name__ == "__main__":
    main()
