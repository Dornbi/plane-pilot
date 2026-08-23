#!/usr/bin/env python3
"""
Analyzes an oscar64 .map file and reports RAM usage broken down by feature and
segment, plus a walk of the whole 64 KB address space.

Segment breakdown includes:
  - Code : Executable CPU instructions (code, startup)
  - Data : Read-only tables, compressed assets, data constants
  - BSS  : Uninitialized dynamic variables and buffers
  - ZP   : Zero Page memory variables ($0060-$00FF)
  - VRAM : Fixed allocations the linker never sees, placed by hand at absolute
           addresses in mem.h / view.cc / gfx.cc / map.cc (see FIXED below)

Two things this reports, and the difference between them matters:

  * The feature table adds up what each area of the program costs. It is built
    from the symbols in the .map plus the FIXED table, so it is only ever as
    complete as those two.

  * The address space walk covers all 65,536 bytes and reconciles to exactly
    that. Free space comes out of the walk, never out of 65536 minus the
    feature total - a symbol nobody wrote down is then missing memory rather
    than free memory, which is how this tool used to report 18 KB free when
    the real figure was a third of that.

Usage:
  python3 tools/analyze_ram.py [path/to/ppilot.map] [--markdown] [--verbose]
"""

import argparse
import os
import re
import sys

DEFAULT_MAP = 'c64o/ppilot.map'

# Allocations the linker knows nothing about, because the program writes them
# at absolute addresses. Every one of these must stay in sync with the source
# that names it; the address is quoted so a grep finds both ends.
#
#   (start, end_exclusive, owning feature or None, label)
#
# Not here, deliberately: color RAM at $D800. It is a separate 1000 x 4 bit
# array inside the I/O block, not part of the 64 KB of DRAM - the DRAM behind
# those addresses is the sprite bitmap block below. Counting it in the walk
# would book the same addresses twice. See OFF_BUDGET.
FIXED = [
    (0x0000, 0x0002, None, '6510 processor port'),
    (0x0002, 0x0060, None,
     'oscar64 runtime zero page (reserved; headroom checked by check_zeropage.py)'),
    (0x0100, 0x0200, None, '6502 hardware stack'),
    (0x0800, 0x0801, None, 'BASIC link byte'),
    (0xD000, 0xD400, 'Menu & Missions',
     'map view screen RAM, under I/O (map.cc kMapScreenRam)'),
    (0xD400, 0xE000, 'Instrument Panel',
     'sprite bitmaps, 48 blocks (mem.cc kSpriteData)'),
    (0xE000, 0xE800, 'Horizon Graphics',
     'character RAM, 256 chars (mem.h kCharRam)'),
    (0xE800, 0xEBE8, 'Horizon Graphics',
     'main screen RAM (mem.h kScreenRamMain)'),
    (0xEBF8, 0xEC00, 'Instrument Panel', 'main screen sprite pointers'),
    (0xEC00, 0xEFE8, 'Horizon Graphics',
     'alt screen RAM (mem.h kScreenRamAlt)'),
    (0xEFF8, 0xF000, 'Instrument Panel', 'alt screen sprite pointers'),
    (0xF000, 0xFF40, 'Instrument Panel',
     'panel bitmap, incl. the four heading strips at $F000 (view.cc, gfx.cc)'),
    (0xFFFA, 0x10000, None, 'NMI / RESET / IRQ vectors'),
]

# Real hardware the program uses that is not part of the 64 KB budget, so it is
# reported on its own rather than in the walk.
OFF_BUDGET = [
    (1000, 'Core System & Drivers',
     'color RAM $D800-$DBE7: 1000 x 4 bits inside the I/O block, not DRAM'),
]

# What each feature area is, per segment, for the detail sections of
# docs/memory_map.md. Prose only - every byte count in that document is filled
# in from the map at generation time, because the hand-written ones went stale
# within a release and nobody noticed.
DESCRIPTIONS = {
    'Horizon Graphics': {
        'Code': 'viewport horizon rendering, cell filling, box generation, '
                'slot drawing (`render.cc`, `box.cc`, `roll.cc`, `roll_asm.cc`)',
        'Data': 'box definitions (`boxdefs`), character definitions '
                '(`chardefs`), roll multiply and slope tables, compressed '
                'charset (`kGfxCharsCompressed`)',
        'BSS': 'per-slot frame arrays (`_box_chars`, `_box_colors`)',
        'ZP': 'roll and render registers (`roll_dx`, `roll_dy`, `roll_period`, '
              '`render_cx_pixels`, ...)',
        'VRAM': 'character RAM `$E000-$E7FF`, main screen `$E800`, '
                'alt screen `$EC00` - the two double-buffered VIC screens',
    },
    'Polygon Graphics': {
        'Code': 'polygon pipeline, edge scan conversion, near and screen '
                'clipping, fixed-point vector math (`poly.cc`, `vec.cc`, '
                '`vec_asm.cc`, `fmath.cc`)',
        'Data': 'sine and cosine tables, inverse-Z LUT, the quarter-square '
                'multiply tables (`vec_sqr_lo`, `vec_sqr_hi`)',
        'BSS': 'scratch vertex buffers (`poly_verts`, `clip3_buf`, `proj_buf`, '
               '`clip2_buf1/2`, `final_verts`)',
        'ZP': 'vector registers (`vec_v`, `vec_sx`, `vec_sy`)',
        'VRAM': '',
    },
    'World Model': {
        'Code': 'flight dynamics, physics integration, waypoint checking, '
                'terrain grid rendering (`flight.cc`, `world.cc`, `sim.cc`, '
                '`world_map.cc`, `clouds.cc`)',
        'Data': 'orientation matrices (`mat3_rot`, `kHeadingLut`), the world '
                'map, cloud hash and ladder tables',
        'BSS': 'flight path history (`flight_path_px/py`), delta transform '
               'vectors (`_world_dx4`, `_world_dy4`), camera state',
        'ZP': 'flight state (`flight_eye_x/y/z`, `flight_speed`, '
              '`flight_throttle`, `flight_fuel`, `flight_vspeed`)',
        'VRAM': '',
    },
    'Instrument Panel': {
        'Code': 'viewport split raster handlers, gauge updates, the sprite '
                'stack and hardware controller (`view.cc`, `panel.cc`, '
                '`sprites.cc`, `spritedef.cc`)',
        'Data': 'compressed panel image and sprite bitmaps, character and '
                'color LUTs',
        'BSS': 'raster IRQ split structures, the sprite candidate stack and '
               'the two committed sprite frames',
        'ZP': 'sprite index and pointers',
        'VRAM': 'sprite bitmaps `$D400-$DFFF`, panel bitmap `$F000-$FF3F` '
                '(the four heading strips live in its off-screen head at '
                '`$F000-$F17F`), and both screens\' sprite pointers',
    },
    'Menu & Missions': {
        'Code': 'menu loop, mission cursor, help screen, map view '
                '(`menu.cc`, `mission.cc`, `help.cc`, `map.cc`)',
        'Data': 'menu and mission text, mission definitions, help text, '
                'map tiles',
        'BSS': '',
        'ZP': '',
        'VRAM': 'map view screen RAM at `$D000-$D3FF`, RAM under I/O, live '
                'only while the map is open',
    },
    'Message System': {
        'Code': 'status message timer, line formatter, clear and restore '
                '(`msg.cc`, `screen.cc` notices)',
        'Data': 'format strings and delays',
        'BSS': 'the active message buffer',
        'ZP': 'notice countdown and length',
        'VRAM': '',
    },
    'Sound Effects': {
        'Code': 'SID driver, engine generator, stall alarm, wind noise, '
                'volume control (`sound.cc`)',
        'Data': 'engine pitch table, wind frequency table, volume names',
        'BSS': 'the SID register shadow',
        'ZP': 'PWM phase, generation counters, RNG, voice-3 arbitration, '
              'stall phase',
        'VRAM': '',
    },
    'Music': {
        'Code': 'playback driver, tick handler, note-to-frequency conversion '
                '(`music.cc`)',
        'Data': 'note table, per-row lead and bass streams, chord table, '
                'volume map and mix matrix, bit-packed gate and drum masks',
        'BSS': 'voice-3 sweep step',
        'ZP': 'row, bar and frame counters, arpeggio index, voice-3 ownership',
        'VRAM': '',
    },
    'Debug Messages & Overlay': {
        'Code': 'compiled out of `ppilot.prg`; present only in `ppilotd.prg`',
        'Data': '', 'BSS': '', 'ZP': '', 'VRAM': '',
    },
    'Benchmarks & Timing': {
        'Code': 'compiled out of `ppilot.prg`; present only in `ppilotd.prg`',
        'Data': '', 'BSS': '', 'ZP': '', 'VRAM': '',
    },
    'Core System & Drivers': {
        'Code': 'entry point, VIC setup, raster IRQ core, LZO decompressor, '
                'keyboard, CPU speed probe, oscar64 runtime (`ppilot.cc`, '
                '`mem.cc`, `gfx.cc`, `screen.cc`, `keys.cc`, `cpu.cc`, '
                '`bcd.cc`, `print.cc`)',
        'Data': 'startup header, screen row pointer tables, fill patterns',
        'BSS': 'raster IRQ lists, keyboard matrix, CPU probe results',
        'ZP': 'compiler temporaries and kernel flags',
        'VRAM': '',
    },
}

# The map view redecorates memory that is already allocated - it puts its
# bitmap over char RAM, both screen buffers and the panel bitmap - so only the
# $D000 window above is extra. Recorded here because the overlap is deliberate
# and someone will otherwise "find" 8 KB that is not there.
MAP_VIEW_NOTE = ('The map view also borrows $E000-$FF3F for its bitmap, on top '
                 'of char RAM, both screens and the panel. That is reuse, not '
                 'extra memory, and only $D000-$D3FF is charged to it here.')

def get_category(name):
    n = name.lower()
    
    # Benchmarks & Timing
    if any(n.startswith(p) for p in ['bm_', '_benchmark']) or 'benchmark' in n:
        return 'Benchmarks & Timing'

    # Debug Messages & Overlays
    if any(n.startswith(p) for p in ['panel_maybe_print_debug', 'mem_debug_enabled', 'mem_switch_debug', 'print_labeled_', 'bcd_convert32', 'bcd_result', 'print_bcd']):
        return 'Debug Messages & Overlay'

    # Message System (in-game HUD messages)
    if n.startswith('msg_') or n.startswith('_status_text') or n.startswith('screen_notice') or n.startswith('screen_begin_text_page') or n in ['msg.cc', '_notice_frames', '_notice_len']:
        return 'Message System'

    # Music (must be before Menu & Missions so kMusicVolMap is not matched by 'map')
    if any(n.startswith(p) for p in ['music_', '_music_', 'kmusic', '_music', 'kvolumemix', 'kmusicvolumemix', '_row', '_bar', '_arp_idx', '_hard_restart', '_music_hard_restart']) or 'music' in n:
        return 'Music'

    # Sound Effects
    if any(n.startswith(p) for p in ['sound_', '_sound_', 'ksound', '_set_voice', '_sound_set_voice', 'sound_wind_freq', 'sound_gen', '_pwm_phase', '_sound_pwm_phase', '_rng', '_sound_rng', '_v3_', '_sound_v3_', '_stall_phase', '_sound_stall_phase', 'kenginefreq', 'kwindfreq', 'ksoundvolumenames', 'kmastervolume', '_next_rand', '_sound_next_rand', '_ctrl']) or 'sound' in n or 'sid' in n:
        return 'Sound Effects'

    # Menu & Missions
    if any(n.startswith(p) for p in ['menu_', '_menu_', 'kmenu', 'help_', '_help_', 'khelp', 'mission_', 'map_', '_map_', 'kmap', '_render_menu_items', '_enter_menu', '_draw_mission_cursor', '_tile_index', '_draw_object_layer', '_draw_path', '_draw_stencil', '_draw_navpoints', '_draw_compass', '_draw_screen_layer', '_map_poll_exit']) or any(k in n for k in ['menu', 'help', 'mission', 'map']):
        if 'world_map' in n or 'mapdefs' in n:
            pass # handle elsewhere
        else:
            return 'Menu & Missions'

    # Instrument Panel (incl Sprites)
    if any(n.startswith(p) for p in ['panel_', '_panel_', 'kpanel', 'view_', '_view_', 'kview', 'sprites_', '_sprites_', 'ksprite', 'spritedef_', 'mapdefs_', '_set_instrument_sprite', '_switch_to_panel_top', '_switch_to_panel_bottom', '_switch_to_terrain', '_gfx_switch_to_panel_top', '_gfx_switch_to_terrain', '_sprite_instrument_idx', '_sprite_instrument_xy', '_rirq_panel_top', '_rirq_panel_bottom', '_rirq_terrain', 'g_panel_koa_lzo', 'g_spritedef_bin', 'g_mapdefs', '_char_lut', '_color_lut', '_copy_color_ram', '_sun_x', '_sun_y', '_sun_msbx', '_sprites_sun_', 'kgfxheading']) or any(k in n for k in ['panel', 'view', 'sprite', 'mapdefs', 'koa']):
        return 'Instrument Panel'

    # World Model
    if any(n.startswith(p) for p in ['world_', '_world_', 'kworld', 'sim_', '_sim_', 'flight_', '_flight_', 'kflight', 'flight_eye_', 'flight_speed', 'flight_throttle', 'flight_fuel', 'flight_flap', 'flight_gear', 'flight_stall', 'flight_events', 'model_', 'flight_waypoint_nav', 'flight_vspeed', 'world_cam', 'flight_cam', 'flight_nav_point_', 'flight_path_', '_world_grid_radius', '_num_points_per_radius', '_world_dx_vec', '_world_dy_vec', '_world_p_start', '_world_step_x', '_world_start_cx', '_world_step_y', '_world_start_cy', '_world_vec_v', '_world_dx4', '_world_dy4', '_mitch_x', '_mitch_y', '_mitch_z', '_get_heading', '_get_ratio', 'kheadinglut', 'kmitchellpoints', 'knumpoints', 'mat3_rot', 'kwpminalthi', 'nav_msg_buf', 'nav_reached_buf']) or any(k in n for k in ['world', 'flight', 'sim']):
        return 'World Model'

    # Polygon Graphics & 3D Math
    if any(n.startswith(p) for p in ['poly_', '_poly_', 'kpoly', 'vec_', '_vec_', 'kvec', 'fmath_', '_get_msb', '_split_vec', '_clip_2d', '_project_vertices', '_trace_edge_bresenham', '_scan_lines2', '_limit_vec', 'g_sin_table', 'g_cos_table', 'g_inv_z_table', 'poly_verts', 'clip3_buf', 'clip2_buf', 'final_verts', 'proj_buf', '_min_x', '_max_x', 'vec_v', 'project_mul_a', 'project_mul_b', 'vec_sx', 'vec_sy', 'mul_res', 'tmp1', 'tmp2', 'tmp3', 'tmp4']) or any(k in n for k in ['poly', 'vec', 'fmath']):
        return 'Polygon Graphics'

    # Horizon Graphics
    if any(n.startswith(p) for p in ['render_', '_render_', 'krender', 'box_', '_box_', 'kbox', 'boxdef_', 'chardef_', '_chardef_', 'roll_', '_roll_', 'kroll', '_fill_line', '_pull_to_center', '_call_roll_mul_dy', '_call_roll_mul_dx', '_get_roll_angle', '_roll_mul_', '_slot_def', '_cur_box_chars', '_cur_box_colors', '_box_chars', '_box_colors', 'g_boxdefs', 'g_alt_boxdefs', 'g_num_boxdefs', 'g_chardefs', 'g_alt_chardefs', 'g_num_chardefs', 'g_roll_mul_table', 'g_roll_slopes', 'roll_dx', 'roll_dy', 'roll_period', 'roll_shift_2chars', 'roll_x_is_major', 'roll_angle', 'roll_dx_div_dy', 'roll_mul_dy', 'roll_mul_dx', 'roll_mul_tmp_val', 'roll_get_dist_res', 'render_cx_pixels', 'render_px_pixels', 'render_cy_pixels', 'render_py_pixels', 'render_alt_shift_x', 'render_alt_shift_y', 'render_cx_chars', 'render_cy_chars', 'render_alt_box', 'kgfxcharscompressed', 'gfx_init_chars', 'gfx_init_raster_irqs']) or any(k in n for k in ['chardef', 'boxdef', 'render', 'box', 'roll']):
        return 'Horizon Graphics'

    return 'Core System & Drivers'


def read_map(map_path):
    """Pulls the three blocks this tool needs out of an oscar64 .map.

    Returns (objects, regions, stack_free). `regions` is the linker's own
    allocatable areas, which is what tells free bytes the compiler can still
    use apart from free bytes only reachable by hand-placing something.

    `stack_free` is the odd one. oscar64's software stack grows *down* from the
    end of its region, and the `sections` line for it reports the part that
    was never reached - so `0200 - 0251` on a $0200..$0280 region means 47
    bytes of stack in use and 81 spare, not the other way round.
    """
    if not os.path.exists(map_path):
        sys.exit(f"Error: Map file not found at '{map_path}'. Build project first.")

    with open(map_path, 'r') as f:
        lines = f.readlines()

    block = None
    objects = []
    regions = []
    stack_free = None

    for line in lines:
        line = line.strip()
        if line in ('sections', 'regions', 'objects'):
            block = line
            continue
        if not line:
            continue

        if block == 'sections':
            m = re.match(r'^([0-9a-fA-F]{4})\s*-\s*([0-9a-fA-F]{4})\s*:\s*STACK,\s*stack$',
                         line)
            if m:
                stack_free = (int(m.group(1), 16), int(m.group(2), 16))
            continue

        if block == 'regions':
            # start - end : highwater, used, name
            m = re.match(r'^([0-9a-fA-F]{4})\s*-\s*([0-9a-fA-F]{4})\s*:\s*'
                         r'[0-9a-fA-F]+,\s*[0-9a-fA-F]+,\s*(\S+)$', line)
            if m:
                start, end = int(m.group(1), 16), int(m.group(2), 16)
                if end > start:
                    regions.append((start, end, m.group(3)))
            continue

        if block == 'objects':
            m = re.match(r'^([0-9a-fA-F]{4})\s*-\s*([0-9a-fA-F]{4})\s*:\s*([^,]+),\s*(.*)$',
                         line)
            if m:
                start = int(m.group(1), 16)
                end = int(m.group(2), 16)
                if end > start:
                    objects.append({
                        'start': start,
                        'end': end,
                        'size': end - start,
                        'name': m.group(3).strip(),
                        'type': m.group(4).strip(),
                    })

    return objects, regions, stack_free


def walk_address_space(objects, regions, stack_free):
    """Marks every one of the 65,536 bytes, and reconciles.

    Used bytes are the union of the .map's objects and the FIXED table, so an
    object the linker tucked into a gap between two data sections is counted
    once and in the right place. Whatever is left over is free, classified by
    whether the linker could still reach it.
    """
    USED, FREE_ALLOC, FREE_STACK, FREE_ORPHAN = 1, 2, 3, 4

    kind = bytearray(0x10000)  # 0 = not yet decided
    # Which FIXED entry owns each byte, 1-based; 0 means the linker placed it.
    # Carried per byte so that adjacent hand-placed ranges keep their own
    # labels in the walk instead of collapsing into one anonymous run.
    owner = bytearray(0x10000)

    for o in objects:
        for a in range(o['start'], o['end']):
            kind[a] = USED
    for i, (start, end, _owner, _label) in enumerate(FIXED, start=1):
        for a in range(start, end):
            kind[a] = USED
            owner[a] = i

    # An object landing inside a hand-placed range would mean the two are
    # fighting over the same bytes; the union above would hide it, so say so.
    clashes = []
    for o in objects:
        for start, end, _owner, label in FIXED:
            if o['start'] < end and start < o['end']:
                clashes.append((o['name'], label))

    in_region = bytearray(0x10000)
    for start, end, _name in regions:
        for a in range(start, end):
            in_region[a] = 1

    if stack_free:
        for a in range(stack_free[0], stack_free[1]):
            if kind[a] == 0:
                kind[a] = FREE_STACK

    for a in range(0x10000):
        if kind[a] == 0:
            kind[a] = FREE_ALLOC if in_region[a] else FREE_ORPHAN

    # Collapse into runs, splitting wherever either the state or the owner
    # changes.
    runs = []
    a = 0
    while a < 0x10000:
        b = a
        while b < 0x10000 and kind[b] == kind[a] and owner[b] == owner[a]:
            b += 1
        runs.append((a, b, kind[a], owner[a]))
        a = b

    totals = {USED: 0, FREE_ALLOC: 0, FREE_STACK: 0, FREE_ORPHAN: 0}
    for start, end, k, _o in runs:
        totals[k] += end - start
    assert sum(totals.values()) == 0x10000, 'address space walk does not reconcile'

    return {
        'runs': runs,
        'totals': totals,
        'clashes': clashes,
        # For the reconciliation: the feature table adds symbol sizes up, and
        # oscar64 overlays the hoisted call frames of functions that cannot be
        # live at the same time, so those bytes are counted more than once
        # there and exactly once here.
        'sum_object_sizes': sum(o['size'] for o in objects),
        'sum_fixed_sizes': sum(e - s for s, e, _o, _l in FIXED),
        'unowned_fixed': sum(e - s for s, e, o, _l in FIXED if o is None),
        'names': {USED: 'used', FREE_ALLOC: 'free', FREE_STACK: 'stack headroom',
                  FREE_ORPHAN: 'free (orphan)'},
        'USED': USED, 'FREE_ALLOC': FREE_ALLOC,
        'FREE_STACK': FREE_STACK, 'FREE_ORPHAN': FREE_ORPHAN,
    }


def parse_map(map_path):
    objects, regions, stack_free = read_map(map_path)

    # FIXED describes the ppilot / ppilotd layout, which is the -D__MAX_RAM__
    # one from mem.h. Pointed at vecdemo or vectest - built without it, and
    # without a VIC to speak of - every address above $D000 below would be
    # invented. Say so rather than printing a confident wrong number.
    if not any(start == 0x0860 and end == 0xD000 for start, end, _n in regions):
        print(f"warning: {map_path} is not a __MAX_RAM__ build "
              f"(no $0860-$D000 main region); the fixed allocations below "
              f"describe ppilot and do not apply.\n", file=sys.stderr)


    categories = {
        'Horizon Graphics': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Polygon Graphics': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'World Model': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Instrument Panel': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Menu & Missions': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Message System': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Sound Effects': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Music': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Debug Messages & Overlay': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Benchmarks & Timing': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
        'Core System & Drivers': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'OFF': 0, 'items': []},
    }

    for o in objects:
        cat = get_category(o['name'])
        sec = o['type'].lower()
        sz = o['size']
        
        seg = 'Data'
        if 'code' in sec or 'startup' in sec:
            seg = 'Code'
        elif 'zeropage' in sec:
            seg = 'ZP'
        elif 'bss' in sec or 'stack' in sec:
            seg = 'BSS'
        elif 'data' in sec:
            seg = 'Data'
        
        categories[cat][seg] += sz
        categories[cat]['items'].append((o['name'], sz, o['type'], seg))

    # The hand-placed allocations, charged to whoever uses them. Ranges with no
    # owner (the vectors, the hardware stack, the runtime zero page) belong to
    # the machine rather than to a feature and only appear in the walk.
    for start, end, owner, _label in FIXED:
        if owner:
            categories[owner]['VRAM'] += end - start
    for size, owner, _label in OFF_BUDGET:
        categories[owner]['OFF'] += size

    return categories, walk_address_space(objects, regions, stack_free)


def _fmt_span(start, end):
    return f"${start:04X}-${end - 1:04X}"


def print_walk(walk, markdown=False):
    U, FA, FS, FO = walk['USED'], walk['FREE_ALLOC'], walk['FREE_STACK'], walk['FREE_ORPHAN']
    names = walk['names']

    rows = []
    for start, end, kind, owner in walk['runs']:
        if owner:
            text = FIXED[owner - 1][3]
        elif kind == U:
            text = 'linker-allocated: code, data, bss, zero page, stack frames'
        elif kind == FA:
            text = 'free, inside a linker region'
        elif kind == FS:
            text = 'software stack headroom (see #pragma stacksize in mem.h)'
        else:
            text = 'free, but outside every linker region'
        rows.append((start, end, names[kind], text))

    if markdown:
        print('| Range | Bytes | State | Contents |')
        print('| :--- | ---: | :--- | :--- |')
        for start, end, kind, text in rows:
            print(f"| `{_fmt_span(start, end)}` | {end - start:,} | {kind} | {text} |")
    else:
        for start, end, kind, text in rows:
            print(f"  {_fmt_span(start, end):>11}  {end - start:>6}  {kind:<14}  {text}")

    free = walk['totals'][FA] + walk['totals'][FS] + walk['totals'][FO]
    used = walk['totals'][U]
    biggest = max((e - s, s) for s, e, k, _o in walk["runs"] if k == FA)

    lines = [
        f"Used                       {used:>6,} B   {used / 655.36:.1f}%",
        f"Free, allocatable          {walk['totals'][FA]:>6,} B   "
        f"largest run {biggest[0]:,} B at ${biggest[1]:04X}",
        f"Free, stack headroom       {walk['totals'][FS]:>6,} B   "
        f"reachable by lowering #pragma stacksize",
        f"Free, orphan fragments     {walk['totals'][FO]:>6,} B   "
        f"only reachable by hand-placing",
        f"Free, total                {free:>6,} B   {free / 655.36:.1f}%",
    ]
    if markdown:
        print()
        print('```')
        for l in lines:
            print(l)
        print('```')
    else:
        print()
        for l in lines:
            print('  ' + l)

    if walk['clashes']:
        print()
        for name, text in walk['clashes']:
            print(f"  WARNING: linker placed '{name}' inside hand-placed range: {text}")


def print_reconciliation(walk, feature_total, markdown=False):
    """Ties the feature table to the walk. They count different things and
    will not match on their own; printing the bridge is what stops the gap
    from being read as slack."""
    used = walk['totals'][walk['USED']]
    overlap = (walk['sum_object_sizes'] + walk['sum_fixed_sizes']) - used
    lines = [
        f"Feature table total        {feature_total:>6,} B",
        f"+ machine-owned ranges     {walk['unowned_fixed']:>6,} B   "
        f"processor port, runtime ZP, hardware stack, BASIC link",
        f"- addresses counted twice  {overlap:>6,} B   "
        f"oscar64 overlays call frames that cannot be live together",
        f"= address space, used      {used:>6,} B",
    ]
    assert feature_total + walk['unowned_fixed'] - overlap == used, \
        'feature table and address space walk do not reconcile'
    if markdown:
        print()
        print('```')
        for l in lines:
            print(l)
        print('```')
    else:
        print()
        for l in lines:
            print('  ' + l)


def print_details(categories):
    """The per-feature detail sections of docs/memory_map.md. Prose from
    DESCRIPTIONS, every number from the map."""
    cols = ('Code', 'Data', 'BSS', 'ZP', 'VRAM')
    for idx, (cname, cat) in enumerate(categories.items(), start=1):
        tot = sum(cat[c] for c in cols)
        print(f"### {idx}. {cname} ({tot:,} B)")
        print()
        desc = DESCRIPTIONS.get(cname, {})
        for c in cols:
            text = desc.get(c, '')
            if cat[c] == 0 and not text:
                continue
            suffix = f": {text}" if text else ''
            print(f"* **{c} ({cat[c]:,} B)**{suffix}.")
        if cat['OFF']:
            for size, owner, text in OFF_BUDGET:
                if owner == cname:
                    print(f"* **Off budget ({size:,} B)**: {text}.")
        print()


def main():
    parser = argparse.ArgumentParser(description='Analyze Plane Pilot RAM map by segment')
    parser.add_argument('map_file', nargs='?', default=DEFAULT_MAP,
                        help=f'Path to .map file (default: {DEFAULT_MAP})')
    parser.add_argument('-m', '--markdown', action='store_true',
                        help='Output in Markdown table format')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show detailed symbol listing for each category')
    args = parser.parse_args()

    categories, walk = parse_map(args.map_file)

    cols = ('Code', 'Data', 'BSS', 'ZP', 'VRAM')
    totals = {c: 0 for c in cols}
    total_off = 0
    total_all = 0

    if args.markdown:
        print('| Feature Area | Code | Data | BSS | ZP | VRAM | **Total Footprint** |')
        print('| :--- | :---: | :---: | :---: | :---: | :---: | :---: |')

        for idx, (cname, cat) in enumerate(categories.items(), start=1):
            tot = sum(cat[c] for c in cols)
            for c in cols:
                totals[c] += cat[c]
            total_off += cat['OFF']
            total_all += tot
            cells = ' | '.join(f"{cat[c]:,} B" for c in cols)
            print(f"| **{idx}. {cname}** | {cells} | **{tot:,} B ({tot / 1024.0:.1f} KB)** |")

        cells = ' | '.join(f"**{totals[c]:,} B**" for c in cols)
        print(f"| **TOTAL** | {cells} | **{total_all:,} B ({total_all / 1024.0:.1f} KB)** |")

        print()
        print('### Address space walk')
        print()
        print_walk(walk, markdown=True)
        print_reconciliation(walk, total_all, markdown=True)
        print()
        print('### By feature area')
        print()
        print_details(categories)
    else:
        print(f"\nRAM Analysis for: {args.map_file}\n")
        header = (f"{'Category':<28} | {'Code (B)':<9} | {'Data (B)':<9} | "
                  f"{'BSS (B)':<8} | {'ZP (B)':<7} | {'VRAM (B)':<9} | {'Total (B)':<9}")
        print(header)
        print('-' * len(header))

        for cname, cat in categories.items():
            tot = sum(cat[c] for c in cols)
            for c in cols:
                totals[c] += cat[c]
            total_off += cat['OFF']
            total_all += tot
            print(f"{cname:<28} | {cat['Code']:<9} | {cat['Data']:<9} | {cat['BSS']:<8} | "
                  f"{cat['ZP']:<7} | {cat['VRAM']:<9} | {tot:<9}")

        print('-' * len(header))
        print(f"{'TOTAL':<28} | {totals['Code']:<9} | {totals['Data']:<9} | "
              f"{totals['BSS']:<8} | {totals['ZP']:<7} | {totals['VRAM']:<9} | {total_all:<9}")

        print(f"\nAddress space walk ({args.map_file}):\n")
        print_walk(walk)
        print_reconciliation(walk, total_all)

    print()
    if args.markdown:
        for size, _owner, text in OFF_BUDGET:
            print(f"- **Off budget, {size:,} B** — {text}")
        print(f"- **Note** — {MAP_VIEW_NOTE}")
    else:
        for size, _owner, text in OFF_BUDGET:
            print(f"  Off budget: {size:,} B  {text}")
        print(f"  Note: {MAP_VIEW_NOTE}")
    print()

    if args.verbose:
        print("\n--- DETAILED SYMBOLS BY CATEGORY ---")
        for cname, cat in categories.items():
            print(f"\n=== {cname} ===")
            for item in sorted(cat['items'], key=lambda x: x[1], reverse=True):
                print(f"  {item[0]:<35} {item[1]:>6} B  [{item[2]}] ({item[3]})")


if __name__ == '__main__':
    main()
