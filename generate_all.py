import argparse
import sys
import os

# Ensure lib is in path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import lib.batch_generator
import lib.find_boxes
import lib.verify_defs

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
    parser = argparse.ArgumentParser(description="Render all C64 flight simulator frames and generate global charset.")
    parser.add_argument('--colors', type=str, default="5,3,14,6", help="Ground, Grad1, Grad2, Sky (indices 0-15)")
    parser.add_argument('--limit', type=int, default=None, help="Limit number of roll angles to render (for testing)")
    parser.add_argument('--gradient-width', type=int, default=4, help="Total width of gradient in characters (0, 2, or 4)")
    parser.add_argument('--soft-horizon', action='store_true', help="Enable dithering for the ground-to-gradient transition")
    parser.add_argument('--dither', type=str, choices=['none', 'bayer2x2', 'bayer4x4'], default='bayer4x4', help="Dithering matrix to use")
    parser.add_argument('--proportional-dither', action='store_true', help="Scale dither amount with gradient width")
    parser.add_argument('--include-alternates', action='store_true', help="Include frames with alternate center positions")
    parser.add_argument('--tolerance', type=int, default=2, help="Pixel match tolerance (default: 0)")
    parser.add_argument('--use-8bit-offsets', action='store_true', help="Use 8-bit offsets instead of 16-bit offsets")
    parser.add_argument('--debug', action='store_true', help="Enable debug output (writes to [output_dir]_debug)")
    parser.add_argument('--output-dir', type=str, default="all_frames", help="Directory to save rendered frames")
    parser.add_argument('--min-box-width', type=int, default=1, help="Minimum box width along major axis")
    
    args = parser.parse_args()
    
    colors = parse_colors(args.colors)
    
    print("Starting batch render...")
    print(f"  Colors: {colors}")
    print(f"  Output Dir: {args.output_dir}")
    if args.limit:
        print(f"  Limit: {args.limit} rolls")
    print(f"  Tolerance: {args.tolerance}")
        
    try:
        # returns (global_chars, box_defs)
        res = lib.batch_generator.render_batch(
            colors, 
            output_dir=args.output_dir,
            gradient_width=args.gradient_width,
            soft_horizon=args.soft_horizon,
            dither=args.dither,
            proportional_dither=args.proportional_dither,
            include_alternates=args.include_alternates,
            tolerance=args.tolerance,
            rolls_limit=args.limit,
            debug=args.debug,
            min_box_width=args.min_box_width,
        )
        
        # render_batch now returns a tuple
        # render_batch now returns a tuple (global_chars, box_defs, special_ids)
        global_chars, box_defs, special_ids = res
        
        print(f"Batch render complete.")
        print(f"Unique characters found: {len(global_chars)}")
        print(f"Box definitions generated: {len(box_defs)}")
        
        total_box_chars = sum(len(b['chars']) for b in box_defs.values())
        print(f"Total box characters: {total_box_chars}")
        
        box_stats = {}
        for key, data in box_defs.items():
            w, h = data['w'], data['h']
            box_stats[key] = {'w': w, 'h': h, 'area': w * h}
        
        # Sort by area
        sorted_boxes = sorted(box_stats.items(), key=lambda x: x[1]['area'], reverse=True)
        
        print("\nTop 8 MAXIMUM box sizes (w x h = area):")
        for key, stats in sorted_boxes[:8]:
            print(f"  {key}: {stats['w']}x{stats['h']} = {stats['area']}")
        
        # Calculate stats: Characters per box (excluding solid chars)
        solid_ids = {special_ids['CHAR_SOLID_GROUND'], special_ids['CHAR_SOLID_SKY'], special_ids['CHAR_SOLID_GRAD1']}
        
        box_char_counts = {}
        for box_name, data in box_defs.items():
            unique_chars = set(data['chars']) - solid_ids
            box_char_counts[box_name] = len(unique_chars)
            
        # Sort by unique character count
        sorted_box_counts = sorted(box_char_counts.items(), key=lambda x: x[1], reverse=True)
        
        print("\nTop 8 boxes with MOST unique chars (excluding solids):")
        for name, count in sorted_box_counts[:8]:
            print(f"  {name}: {count} chars")
            
        print("\nTop 8 boxes with LEAST unique chars (excluding solids):")
        for name, count in sorted_box_counts[-8:]:
            print(f"  {name}: {count} chars")
        
        sum_unique_chars = sum(box_char_counts.values())
        print(f"\nSum of unique chars for all boxdefs: {sum_unique_chars}")
        print("")
        
        # Generate chardefs.py
        chardefs_content = lib.batch_generator.generate_chardefs_content(global_chars, special_ids)
        
        chardefs_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib", "chardefs.py")
        with open(chardefs_path, "w") as f:
            f.write(chardefs_content)
            
        print(f"Generated {chardefs_path}")
        
        # Generate boxdefs.py
        boxdefs_content = lib.find_boxes.generate_boxdefs_content(box_defs)
        boxdefs_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib", "boxdefs.py")
        with open(boxdefs_path, "w") as f:
            f.write(boxdefs_content)
            
        print(f"Generated {boxdefs_path}")
        
        # --- C Export ---
        c64_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "c64o")
        if not os.path.exists(c64_dir):
            os.makedirs(c64_dir)
            
        total_chars = len(global_chars)
        
        # Calculate max stats for boxdefs.h
        max_box_total_size = sorted_boxes[0][1]['area'] if sorted_boxes else 0
        max_box_char_count = sorted_box_counts[0][1] if sorted_box_counts else 0
        
        # 1. Chardefs C and H
        chardefs_c = lib.batch_generator.generate_chardefs_c_content(global_chars)
        chardefs_c_path = os.path.join(c64_dir, "chardefs.cc")
        with open(chardefs_c_path, "w") as f:
            f.write(chardefs_c)
            
        print(f"Generated {chardefs_c_path}")

        chardefs_h = lib.batch_generator.generate_chardefs_h_content(global_chars, special_ids)
        chardefs_h_path = os.path.join(c64_dir, "chardefs.h")
        with open(chardefs_h_path, "w") as f:
            f.write(chardefs_h)
            
        print(f"Generated {chardefs_h_path}")
        
        # 2. Boxdefs C and H
        boxdefs_c = lib.find_boxes.generate_boxdefs_c_content(
            box_defs, total_chars, use_8bit_offsets=args.use_8bit_offsets)
        boxdefs_c_path = os.path.join(c64_dir, "boxdefs.cc")
        with open(boxdefs_c_path, "w") as f:
            f.write(boxdefs_c)
            
        print(f"Generated {boxdefs_c_path}")

        boxdefs_h = lib.find_boxes.generate_boxdefs_h_content(
            max_box_total_size, max_box_char_count)
        boxdefs_h_path = os.path.join(c64_dir, "boxdefs.h")
        with open(boxdefs_h_path, "w") as f:
            f.write(boxdefs_h)
            
        print(f"Generated {boxdefs_h_path}")
        
        # --- Verification ---
        print("\nVerifying C files...")
        lib.verify_defs.verify_chardefs_c(global_chars, chardefs_c_path)
        lib.verify_defs.verify_boxdefs_c(box_defs, boxdefs_c_path, total_chars, args.use_8bit_offsets)
        print("C Export Verification PASSED.")

    except Exception as e:
        print(f"Error during batch processing: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
