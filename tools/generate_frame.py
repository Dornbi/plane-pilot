import argparse
import sys
import os
import math

# Repo root, so `lib` is importable and outputs land in the right place
# regardless of the current working directory.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

import lib.frame_generator
import lib.c64_graphics
import lib.c64_converter
import lib.c64_colors
import lib.roll_angle
import lib.renderer_engine
import lib.find_boxes


def parse_colors(s):
    try:
        parts = [int(p.strip()) for p in s.split(',')]
        if len(parts) != 4:
            raise ValueError
        return parts
    except:
        print("Error: Colors must be 4 integers separated by commas (e.g. 5,3,14,6)")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Render a C64 flight simulator frame.")
    parser.add_argument('--colors', type=str, default="5,3,14,6", help="Ground, Grad1, Grad2, Sky (indices 0-15)")
    parser.add_argument('--roll', type=str, default="r8u1", help="Roll vector (e.g. r8u1)")
    parser.add_argument('--gradient-width', type=int, default=4, help="Total width of gradient in characters (0, 2, or 4)")
    parser.add_argument('--soft-horizon', action='store_true', help="Enable dithering for the ground-to-gradient transition")
    parser.add_argument('--dither', type=str, choices=['none', 'bayer2x2', 'bayer4x4'], default='bayer4x4', help="Dithering matrix to use")
    parser.add_argument('--proportional-dither', action='store_true', help="Scale dither amount with gradient width")
    parser.add_argument('--mccm', action='store_true', help="Convert and render as MCCM (Multicolor Character Mode)")
    parser.add_argument('--center-x', type=int, default=160, help="X coordinate of the center point")
    parser.add_argument('--center-y', type=int, default=96, help="Y coordinate of the center point")
    parser.add_argument('--tolerance', type=int, default=2, help="Pixel match tolerance for MCCM characters (default: 1)")
    parser.add_argument('--debug', action='store_true', help="Enable debug rendering (8x upscale, grid, char indices)")
    parser.add_argument('--output', type=str, default=os.path.join(REPO_ROOT, "out", "flight_frame.png"), help="Output PNG filename")
    parser.add_argument('--min-box-width', type=int, default=1, help="Minimum box width along major axis")
    
    args = parser.parse_args()
    
    colors = parse_colors(args.colors)
    print(f"Rendering frame with:")
    print(f"  Colors: {colors}")
    print(f"  Roll: {args.roll}")
    print(f"  Gradient Width: {args.gradient_width} chars")
    print(f"  Soft Horizon: {args.soft_horizon}")
    print(f"  Proportional Dither: {args.proportional_dither}")
    print(f"  Mode: {'MCCM' if args.mccm else 'MCBM'}")
    print(f"  Dither: {args.dither}")
    print(f"  Center: ({args.center_x}, {args.center_y})")
    if args.mccm:
        print(f"  Tolerance: {args.tolerance}")
    print(f"  Min Box Width: {args.min_box_width}")
    print(f"  Debug: {args.debug}")
    
    roll = lib.roll_angle.RollAngle.from_string(args.roll)
    
    bg_col, screen_ram, color_ram, bitmap = lib.frame_generator.generate_frame_mcbm(
        colors, roll, args.gradient_width, 
        soft_ground_horizon=args.soft_horizon, 
        proportional_dither=args.proportional_dither, 
        dither_type=args.dither, 
        center_x=args.center_x, center_y=args.center_y
    )

    if args.mccm:
        print("Converting to MCCM...")
        try:
            globals, c_sram, c_cram, charset = lib.c64_converter.convert_mcbm_to_mccm(
                bg_col, screen_ram, color_ram, bitmap, 
                colors=colors,
                ground_color_index=colors[0],
                tolerance=args.tolerance,
                ground_tolerance=(None if math.gcd(*roll.get_vector()) == 1 else 0)
            )
            img = lib.c64_graphics.C64Screen.render_mccm(globals, c_sram, c_cram, charset, debug=args.debug, debug_crosses=[(args.center_x, args.center_y)])
        except ValueError as e:
            print(f"Error converting to MCCM: {e}")
            sys.exit(1)
    else:
        img = lib.c64_graphics.C64Screen.render_mcbm(bg_col, screen_ram, color_ram, bitmap, debug=args.debug, debug_crosses=[(args.center_x, args.center_y)])
        
    out_dir = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(out_dir, exist_ok=True)
    img.save(args.output)
    print(f"Saved to {args.output}")

    if args.debug:
        print("\nFinding box definition for current frame...")
        # We need a dummy global_sram if we want to test find_box here.
        # But generate_frame doesn't have a global charset.
        # Actually, let's just use the sram from lib.frame_generator.
        box_def = lib.find_boxes.find_box(
            screen_ram, color_ram, roll, args.center_x, args.center_y, args.gradient_width, 
            grad1_color_val=colors[1],
            min_box_width=args.min_box_width)
        print(f"Box Definition: w={box_def['w']}, h={box_def['h']}, step_x={box_def['step_x']}, step_y={box_def['step_y']}")

if __name__ == '__main__':
    main()
