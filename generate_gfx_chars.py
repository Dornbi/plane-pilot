#!/usr/bin/env python3
"""
Python module to generate binary character data equivalent to
_init_single_point_chars() and _init_quad_chars() in c64o/gfx.cc,
and save the result to c64o/gfx_chars.bin.
"""

import os
import sys

def generate_single_point_chars() -> bytearray:
    """
    Generates binary data for the 16 single point characters.
    Equivalent to _init_single_point_chars() in c64o/gfx.cc.
    """
    alt_lines = [0x95, 0x65, 0x59, 0x56]
    data = bytearray()
    
    # 4 groups in Y
    for y in range(4):
        # 4 groups in X
        for x in range(4):
            # Start with solid background 0x55
            char_bytes = bytearray([0x55] * 8)
            # Set the two lines for y * 2 and y * 2 + 1
            char_bytes[y * 2] = alt_lines[x]
            char_bytes[y * 2 + 1] = alt_lines[x]
            data.extend(char_bytes)
            
    return data

def generate_quad_chars() -> bytearray:
    """
    Generates binary data for the 16 quad characters.
    Equivalent to _init_quad_chars() in c64o/gfx.cc.
    """
    full_quad_char = [
        0x99, 0x66, 0x9A, 0xA6,
        0x99, 0x66, 0xA9, 0x66
    ]
    data = bytearray()
    
    for ch_idx in range(16):
        for line_idx in range(8):
            line_val = full_quad_char[line_idx]
            
            # Mask the left 4 bits to 0101 (ground, 0x50) if bit not set in ch_idx
            # Equivalent to: !(ch_idx & (line_idx < 4 ? 0x01 : 0x04))
            left_bit_mask = 0x01 if line_idx < 4 else 0x04
            if not (ch_idx & left_bit_mask):
                line_val &= 0x0f
                line_val |= 0x50
                
            # Mask the right 4 bits to 0101 (ground, 0x05) if bit not set in ch_idx
            # Equivalent to: !(ch_idx & (line_idx < 4 ? 0x02 : 0x08))
            right_bit_mask = 0x02 if line_idx < 4 else 0x08
            if not (ch_idx & right_bit_mask):
                line_val &= 0xf0
                line_val |= 0x05
                
            data.append(line_val)
            
    return data

def main():
    # Paths relative to the script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    output_dir = os.path.join(script_dir, "c64o")
    output_file = os.path.join(output_dir, "gfx_chars.bin")
    
    # Generate character data
    print("Generating single point character data...")
    single_point_data = generate_single_point_chars()
    print(f"Generated {len(single_point_data)} bytes for 16 single point characters.")
    
    print("Generating quad character data...")
    quad_data = generate_quad_chars()
    print(f"Generated {len(quad_data)} bytes for 16 quad characters.")
    
    # Combine data
    combined_data = single_point_data + quad_data
    total_bytes = len(combined_data)
    print(f"Total combined binary size: {total_bytes} bytes (32 characters).")
    
    # Ensure directory exists
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created directory: {output_dir}")
        
    # Write to bin file
    try:
        with open(output_file, "wb") as f:
            f.write(combined_data)
        print(f"Successfully wrote binary character data to: {output_file}")
    except Exception as e:
        print(f"Error writing to {output_file}: {e}", file=sys.stderr)
        sys.exit(1)

    # Print out detailed hex layout for verification
    print("\n--- Verification Hex Dump ---")
    print("Single Point Characters (128 bytes):")
    for i in range(0, len(single_point_data), 8):
        char_num = 128 + (i // 8)
        hex_str = " ".join(f"{b:02X}" for b in single_point_data[i:i+8])
        print(f"  Char {char_num:03d} (0x{char_num:02X}): {hex_str}")
        
    print("\nQuad Characters (128 bytes):")
    for i in range(0, len(quad_data), 8):
        char_num = 144 + (i // 8)
        hex_str = " ".join(f"{b:02X}" for b in quad_data[i:i+8])
        print(f"  Char {char_num:03d} (0x{char_num:02X}): {hex_str}")

if __name__ == "__main__":
    main()
