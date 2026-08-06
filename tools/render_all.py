
import argparse
import os
import sys

# Repo root, so `lib` is importable and outputs land in the right place
# regardless of the current working directory.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

import lib.roll_angle
import lib.renderer_engine
import lib.c64_graphics


def main():
    parser = argparse.ArgumentParser(description="Render all roll angles for given centers.")
    parser.add_argument("--centers", type=str, required=True, help="Semicolon separated list of centers, e.g. '160,100;160,96'")
    parser.add_argument("--debug", action="store_true", help="Enable debug rendering")
    args = parser.parse_args()

    # Parse centers
    centers = []
    try:
        center_strs = args.centers.split(';')
        for cs in center_strs:
            if not cs.strip(): continue
            parts = cs.split(',')
            if len(parts) != 2:
                raise ValueError(f"Invalid center format: {cs}")
            centers.append((int(parts[0].strip()), int(parts[1].strip())))
    except ValueError as e:
        print(f"Error parsing centers: {e}")
        sys.exit(1)

    if not centers:
        print("No centers provided.")
        sys.exit(1)

    output_dir = os.path.join(REPO_ROOT, "out", "rendered_frames")
    os.makedirs(output_dir, exist_ok=True)

    print(f"Rendering for centers: {centers}")
    print(f"Debug mode: {args.debug}")

    # Standard colors: Green(5), Cyan(3), LtBlue(14), Blue(6)
    # Mapping: [Ground, Grad1, Grad2, Sky] = [5, 3, 14, 6]
    colors = [5, 3, 14, 6]
    
    # MCCM Globals mapping:
    # 00: Grad2 (colors[2])
    # 01: Ground (colors[0])
    # 10: Grad1 (colors[1])
    # 11: Sky (colors[3]) - dealt with via Color RAM
    global_colors = [colors[2], colors[0], colors[1]]

    # Buffers
    screen_ram = bytearray(lib.renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * lib.renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
    color_ram = bytearray(lib.renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * lib.renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
    charset = bytearray(2048)
    
    for cx, cy in centers:
        print(f"Processing Center ({cx}, {cy})...")
        for roll in lib.roll_angle.RollAngle.all_rolls():
            # Reset buffers
            # Fill with 0? init_screen_and_border fills what it needs.
            # But render_frame expects clean slate for dynamic allocation?
            # Actually render_frame re-initializes 'used_chars' but uses existing charset buffer.
            # We should clear charset to be safe or rely on overwrite.
            # init_solid_chars handles 0-23.
            # render_frame handles the rest.
            
            # Resetting helps debugging.
            for i in range(len(screen_ram)): screen_ram[i] = 0
            for i in range(len(color_ram)): color_ram[i] = 0
            for i in range(len(charset)): charset[i] = 0
            
            # Init base state
            lib.renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
            lib.renderer_engine.RendererEngine.init_solid_chars(charset)
            
            # Render
            try:
                debug_boxes = [] if args.debug else None
                lib.renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll,
                                            cx, cy, charset_start=0, debug_boxes=debug_boxes)
                
                # Render Image
                (cx_pulled, cy_pulled), (cx_snap, cy_snap) = lib.renderer_engine.RendererEngine.get_pulled_snapped_centers(roll, cx, cy)
                img = lib.c64_graphics.C64Screen.render_mccm(
                    global_colors, bytes(screen_ram), bytes(color_ram), bytes(charset), debug=args.debug,
                    debug_crosses=[(cx, cy), (cx_snap, cy_snap), (cx_pulled, cy_pulled)],
                    debug_boxes=debug_boxes)
                
                # Filename: rendered_frame_c160_96_00_r8
                filename = f"rendered_frame_c{cx}_{cy}_{roll.value:02d}_{roll.name.lower()}.png"
                filepath = os.path.join(output_dir, filename)
                
                img.save(filepath)
                # print(f"Saved {filepath}")
                
            except Exception as e:
                print(f"Error rendering {roll.name} at {cx},{cy}: {e}")
                import traceback
                traceback.print_exc()

    print("Done.")

if __name__ == "__main__":
    main()
