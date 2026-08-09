#!/usr/bin/env python3
"""
Analyzes an oscar64 .map file and reports RAM usage broken down by feature and segment.

Segment breakdown includes:
  - Code : Executable CPU instructions (code, startup)
  - Data : Read-only tables, compressed assets, data constants
  - BSS  : Uninitialized dynamic variables and buffers
  - ZP   : Zero Page memory variables ($0060-$00FC)
  - VRAM : Fixed VIC-II Video RAM allocations (Char RAM, Screen RAMs, Color RAM, Sprite RAM)

Usage:
  python3 tools/analyze_ram.py [path/to/ppilot.map] [--markdown] [--verbose]
"""

import argparse
import os
import re
import sys

DEFAULT_MAP = 'c64o/ppilot.map'

def get_category(name):
    n = name.lower()
    
    # Benchmarks & Timing
    if any(n.startswith(p) for p in ['bm_', '_benchmark']) or 'benchmark' in n:
        return 'Benchmarks & Timing'

    # Debug Messages & Overlays
    if any(n.startswith(p) for p in ['panel_maybe_print_debug', 'mem_debug_enabled', 'mem_switch_debug', 'print_labeled_']):
        return 'Debug Messages & Overlay'

    # Message System (in-game HUD messages)
    if n.startswith('msg_') or n.startswith('_status_text') or n == 'msg.cc':
        return 'Message System'

    # Music (must be before Menu & Missions so kMusicVolMap is not matched by 'map')
    if any(n.startswith(p) for p in ['music_', 'kmusic', '_music', 'kvolumemix']) or 'music' in n:
        return 'Music'

    # Sound Effects
    if any(n.startswith(p) for p in ['sound_', '_set_voice', 'sound_wind_freq', 'sound_gen', '_pwm_phase', '_rng', '_v3_effect', '_v3_frames', '_stall_phase', 'kenginefreq', 'kwindfreq', 'ksoundvolumenames']) or 'sound' in n or 'sid' in n:
        return 'Sound Effects'

    # Menu & Missions
    if any(n.startswith(p) for p in ['menu_', 'help_', 'mission_', 'map_', '_render_menu_items', '_enter_menu', '_draw_mission_cursor', '_tile_index', '_draw_object_layer', '_draw_path', '_draw_stencil', '_draw_navpoints', '_draw_compass', '_draw_screen_layer', '_map_poll_exit']) or any(k in n for k in ['menu', 'help', 'mission', 'map']):
        if 'world_map' in n or 'mapdefs' in n:
            pass # handle elsewhere
        else:
            return 'Menu & Missions'

    # Instrument Panel (incl Sprites)
    if any(n.startswith(p) for p in ['panel_', 'view_', 'sprites_', 'spritedef_', 'mapdefs_', '_set_instrument_sprite', '_switch_to_panel_top', '_switch_to_panel_bottom', '_switch_to_terrain', '_sprite_instrument_idx', '_sprite_instrument_xy', '_rirq_panel_top', '_rirq_panel_bottom', '_rirq_terrain', 'g_panel_koa_lzo', 'g_spritedef_bin', 'g_mapdefs', '_char_lut', '_color_lut']) or any(k in n for k in ['panel', 'view', 'sprite', 'mapdefs', 'koa']):
        return 'Instrument Panel'

    # World Model
    if any(n.startswith(p) for p in ['world_', 'sim_', 'flight_', '_world_', '_flight_', 'flight_eye_', 'flight_speed', 'flight_throttle', 'flight_fuel', 'flight_flap', 'flight_gear', 'flight_stall', 'flight_events', 'model_', 'flight_waypoint_nav', 'flight_vspeed', 'world_cam', 'flight_cam', 'flight_nav_point_', 'flight_path_', '_world_grid_radius', '_num_points_per_radius', '_world_dx_vec', '_world_dy_vec', '_world_p_start', '_world_step_x', '_world_start_cx', '_world_step_y', '_world_start_cy', '_world_vec_v', '_world_dx4', '_world_dy4', '_mitch_x', '_mitch_y', '_mitch_z']) or any(k in n for k in ['world', 'flight', 'sim']):
        return 'World Model'

    # Polygon Graphics & 3D Math
    if any(n.startswith(p) for p in ['poly_', 'vec_', 'fmath_', '_get_msb', '_split_vec', '_clip_2d', '_project_vertices', '_trace_edge_bresenham', '_scan_lines2', '_limit_vec', 'g_sin_table', 'g_cos_table', 'g_inv_z_table', 'poly_verts', 'clip3_buf', 'proj_buf', '_min_x', '_max_x', 'vec_v', 'project_mul_a', 'project_mul_b', 'vec_sx', 'vec_sy', 'mul_res', 'tmp1', 'tmp2', 'tmp3', 'tmp4']) or any(k in n for k in ['poly', 'vec', 'fmath']):
        return 'Polygon Graphics'

    # Horizon Graphics
    if any(n.startswith(p) for p in ['render_', 'box_', 'boxdef_', 'chardef_', 'roll_', '_fill_line', '_pull_to_center', '_call_roll_mul_dy', '_call_roll_mul_dx', '_get_roll_angle', '_roll_mul_', '_slot_def', '_cur_box_chars', '_cur_box_colors', '_box_chars', '_box_colors', 'g_boxdefs', 'g_alt_boxdefs', 'g_num_boxdefs', 'g_chardefs', 'g_alt_chardefs', 'g_num_chardefs', 'g_roll_mul_table', 'g_roll_slopes', 'roll_dx', 'roll_dy', 'roll_period', 'roll_shift_2chars', 'roll_x_is_major', 'roll_angle', 'roll_dx_div_dy', 'roll_mul_dy', 'roll_mul_dx', 'roll_mul_tmp_val', 'roll_get_dist_res', 'render_cx_pixels', 'render_px_pixels', 'render_cy_pixels', 'render_py_pixels', 'render_alt_shift_x', 'render_alt_shift_y', 'render_cx_chars', 'render_cy_chars', 'render_alt_box']) or any(k in n for k in ['chardef', 'boxdef', 'render', 'box', 'roll']):
        return 'Horizon Graphics'

    return 'Core System & Drivers'


def parse_map(map_path):
    if not os.path.exists(map_path):
        sys.exit(f"Error: Map file not found at '{map_path}'. Build project first.")

    with open(map_path, 'r') as f:
        lines = f.readlines()

    in_objects = False
    objects = []

    for line in lines:
        line = line.strip()
        if line == 'objects':
            in_objects = True
            continue
        if not in_objects:
            continue
        
        m = re.match(r'^([0-9a-fA-F]{4})\s*-\s*([0-9a-fA-F]{4})\s*:\s*([^,]+),\s*(.*)$', line)
        if m:
            start = int(m.group(1), 16)
            end = int(m.group(2), 16)
            name = m.group(3).strip()
            sec_type = m.group(4).strip()
            size = end - start
            if size > 0:
                objects.append({
                    'start': start,
                    'end': end,
                    'size': size,
                    'name': name,
                    'type': sec_type
                })

    categories = {
        'Horizon Graphics': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Polygon Graphics': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'World Model': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Instrument Panel': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Menu & Missions': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Message System': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Sound Effects': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Music': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Debug Messages & Overlay': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Benchmarks & Timing': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
        'Core System & Drivers': {'Code': 0, 'Data': 0, 'BSS': 0, 'ZP': 0, 'VRAM': 0, 'items': []},
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

    # Add fixed Video RAM allocations:
    # - Character RAM: $E000-$E6FF (1792 bytes) -> Horizon Graphics (box chars)
    # - Main Screen RAM: $E800-$EBFF (1000 bytes) -> Horizon Graphics
    # - Alt Screen RAM: $EC00-$EFF7 (1000 bytes) -> Horizon Graphics
    # - Color RAM: $D800-$DBE7 (1000 bytes) -> Core System
    # - Sprite Data RAM: $E700-$E7FF (256 bytes) -> Instrument Panel
    categories['Horizon Graphics']['VRAM'] += 1792 + 1000 + 1000
    categories['Instrument Panel']['VRAM'] += 256
    categories['Core System & Drivers']['VRAM'] += 1000

    return categories


def main():
    parser = argparse.ArgumentParser(description='Analyze Plane Pilot RAM map by segment')
    parser.add_argument('map_file', nargs='?', default=DEFAULT_MAP,
                        help=f'Path to .map file (default: {DEFAULT_MAP})')
    parser.add_argument('-m', '--markdown', action='store_true',
                        help='Output in Markdown table format')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show detailed symbol listing for each category')
    args = parser.parse_args()

    categories = parse_map(args.map_file)

    total_code = 0
    total_data = 0
    total_bss = 0
    total_zp = 0
    total_vram = 0
    total_all = 0

    if args.markdown:
        print(f"| Feature Area | Code | Data | BSS | ZP | VRAM | **Total Footprint** |")
        print(f"| :--- | :---: | :---: | :---: | :---: | :---: | :---: |")

        idx = 1
        for cname, cat in categories.items():
            code = cat['Code']
            data = cat['Data']
            bss = cat['BSS']
            zp = cat['ZP']
            vram = cat['VRAM']
            tot = code + data + bss + zp + vram
            
            total_code += code
            total_data += data
            total_bss += bss
            total_zp += zp
            total_vram += vram
            total_all += tot

            kb = tot / 1024.0
            print(f"| **{idx}. {cname}** | {code:,} B | {data:,} B | {bss:,} B | {zp:,} B | {vram:,} B | **{tot:,} B ({kb:.1f} KB)** |")
            idx += 1

        print(f"| **TOTAL** | **{total_code:,} B** | **{total_data:,} B** | **{total_bss:,} B** | **{total_zp:,} B** | **{total_vram:,} B** | **{total_all:,} B ({total_all/1024.0:.1f} KB)** |")

    else:
        print(f"\nRAM Analysis for: {args.map_file}\n")
        print(f"{'Category':<28} | {'Code (B)':<9} | {'Data (B)':<9} | {'BSS (B)':<8} | {'ZP (B)':<7} | {'VRAM (B)':<9} | {'Total (B)':<9}")
        print("-" * 95)

        for cname, cat in categories.items():
            code = cat['Code']
            data = cat['Data']
            bss = cat['BSS']
            zp = cat['ZP']
            vram = cat['VRAM']
            tot = code + data + bss + zp + vram
            
            total_code += code
            total_data += data
            total_bss += bss
            total_zp += zp
            total_vram += vram
            total_all += tot
            
            print(f"{cname:<28} | {code:<9} | {data:<9} | {bss:<8} | {zp:<7} | {vram:<9} | {tot:<9}")

        print("-" * 95)
        print(f"{'TOTAL':<28} | {total_code:<9} | {total_data:<9} | {total_bss:<8} | {total_zp:<7} | {total_vram:<9} | {total_all:<9}")
        print(f"\nOverall RAM footprint: {total_all} bytes ({total_all / 1024:.1f} KB / 64 KB C64 RAM)\n")

    if args.verbose:
        print("\n--- DETAILED SYMBOLS BY CATEGORY ---")
        for cname, cat in categories.items():
            print(f"\n=== {cname} ===")
            for item in sorted(cat['items'], key=lambda x: x[1], reverse=True):
                print(f"  {item[0]:<35} {item[1]:>6} B  [{item[2]}] ({item[3]})")


if __name__ == '__main__':
    main()
