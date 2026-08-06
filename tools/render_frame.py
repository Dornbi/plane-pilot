
import argparse
import sys
import os

# Repo root, so `lib` is importable and outputs land in the right place
# regardless of the current working directory.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

import lib.renderer_engine
import lib.chardefs
import lib.c64_converter
import lib.c64_graphics
import lib.roll_angle

from typing import List

def parse_colors(s: str) -> List[int]:
    try:
        parts = [int(p.strip()) for p in s.split(',')]
        if len(parts) != 4:
            raise ValueError
        return parts
    except:
        print("Error: Colors must be 4 integers separated by commas (e.g. 5,3,14,6)")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Stage 4: Render MCCM Frame using Chardefs/Boxdefs.")
    parser.add_argument('--colors', type=str, default="5,3,14,6", help="Ground, Grad1, Grad2, Sky")
    parser.add_argument('--roll', type=str, default="r8u1", help="Roll vector (e.g. r8u1)")
    parser.add_argument('--center-x', type=int, default=160, help="Horizon Center X")
    parser.add_argument('--center-y', type=int, default=64, help="Horizon Center Y")
    parser.add_argument('--output', type=str, default=os.path.join(REPO_ROOT, "out", "render_output.png"), help="Output PNG filename")
    parser.add_argument('--debug', action='store_true', help="Enable debug overlay")
    parser.add_argument('--no-tiles', action='store_true', help="Disable tiled gradient boxes")
    
    args = parser.parse_args()
    
    colors = parse_colors(args.colors)
    
    # Parse RollAngle
    try:
        roll = lib.roll_angle.RollAngle.from_string(args.roll)
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)
    
    # Calculate viewport params
    # Horizontal Center, Vertical Top
    vp_w = lib.renderer_engine.RendererEngine.VIEWPORT_WIDTH_CHARS
    vp_h = lib.renderer_engine.RendererEngine.VIEWPORT_HEIGHT_CHARS
    vp_x = lib.renderer_engine.RendererEngine.VIEWPORT_X_START_CHARS
    vp_y = lib.renderer_engine.RendererEngine.VIEWPORT_Y_START_CHARS
    
    print(f"Rendering Stage 4 Frame:")
    print(f"  Colors: {colors}")
    print(f"  Roll: {args.roll} -> {roll}")
    print(f"  Center: ({args.center_x}, {args.center_y})")
    print(f"  Viewport: {vp_w}x{vp_h} at ({vp_x}, {vp_y})")
    
    try:
        # Initialize RAMs
        # Initialize RAMs
        screen_ram = bytearray(lib.renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * lib.renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        color_ram = bytearray(lib.renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * lib.renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        
        # 1. Initialize Screen and Border
        lib.renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
        
        # Initialize Charset
        charset = bytearray(2048)
        
        # 2. Initialize Solid Chars in Charset (Indices 0, 1, 2)
        lib.renderer_engine.RendererEngine.init_solid_chars(charset)
        
        # Render to RAM and populate Charset
        # We start populating charset at index 0
        # Render to RAM and populate Charset
        # We start populating charset at index 0 (But 0,1,2 are reserved now)
        # If we say start=0, the engine will skip 0,1,2 and start at 3.
        debug_boxes = [] if args.debug else None
        lib.renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll,
                                    args.center_x, args.center_y,
                                    charset_start=0, debug_boxes=debug_boxes,
                                    no_tiles=args.no_tiles)
        
        # No Manual Background Fill Needed!
        # Handled internally by render_frame using fixed indices.
        
        # We don't need local_to_global_map for rendering, as the RAM already contains local indices
        # and the charset is populated with valid data at those indices.
        
        # Calculate min/max char usage in viewport
        min_char = 256
        max_char = -1
        
        for r in range(vp_y, vp_y + vp_h):
            row_offset = r * lib.renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS
            for c in range(vp_x, vp_x + vp_w):
                idx = row_offset + c
                char_code = screen_ram[idx]
                if char_code < min_char: min_char = char_code
                if char_code > max_char: max_char = char_code
        
        if max_char >= 0:
            print(f"  Character Range Used in Viewport: [{min_char}, {max_char}] (Count: {max_char - min_char + 1})")
        else:
            print("  Character Range Used in Viewport: None")
        
        # Generate Image from RAM
        
        # 1. Charset is now populated in `charset`
        
        # 2. Color Mapping
        # 2. Color Mapping
        # Fixed: 00=Grad2, 01=Ground, 10=Grad1
        globals_list = [colors[2], colors[0], colors[1]]
        
        # 3. Render
        (cx_pulled, cy_pulled), (cx_snap, cy_snap) = lib.renderer_engine.RendererEngine.get_pulled_snapped_centers(
            roll, args.center_x, args.center_y)
        img = lib.c64_graphics.C64Screen.render_mccm(
            globals_list, bytes(screen_ram), bytes(color_ram), bytes(charset), debug=args.debug,
            debug_crosses=[(args.center_x, args.center_y), (cx_snap, cy_snap), (cx_pulled, cy_pulled)],
            debug_boxes=debug_boxes)

        print(f"Center: ({args.center_x}, {args.center_y})")
        print(f"Pulled: ({cx_pulled}, {cy_pulled})")
        print(f"Snapped: ({cx_snap}, {cy_snap})")
        
        out_dir = os.path.dirname(os.path.abspath(args.output))
        os.makedirs(out_dir, exist_ok=True)
        img.save(args.output)
        print(f"Saved to {args.output}")
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
