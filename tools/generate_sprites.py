import argparse
import math
import os

# Repo root, so outputs land in the right place regardless of the
# current working directory.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def generate_arm_set(arm_length, bitmap_offset, angles_tot):
    centered_x = 12.0
    centered_y = 10.0
    angles_step = 360.0 / angles_tot
    num_unique_bitmaps = angles_tot // 2
    
    sprites_data = []
    sprites_bits = []
    
    # Generate 32 unique bitmaps for angles -90 to 84.375
    for i in range(num_unique_bitmaps):
        angle_deg = (i * angles_step) - 90
        angle_rad = math.radians(angle_deg)
        
        dx = math.cos(angle_rad)
        dy = math.sin(angle_rad)
        nx = -dy
        ny = dx
        
        p_x = centered_x + 0.5
        p_y = centered_y + 0.5
        
        grid = [[0 for _ in range(24)] for _ in range(21)]
        
        for y in range(21):
            for x in range(24):
                vx = x - p_x
                vy = y - p_y
                proj = vx * dx + vy * dy
                dist = abs(vx * nx + vy * ny)
                
                # Use the logic from the user's latest edit
                if abs(proj) <= arm_length / 2 + 0.5 and dist <= 1.0:
                    grid[y][x] = 1
                    
        sprite_bytes = bytearray(64)
        for y in range(21):
            row_bits = grid[y]
            b0 = b1 = b2 = 0
            for k in range(8):
                if row_bits[k]: b0 |= (1 << (7-k))
                if row_bits[k+8]: b1 |= (1 << (7-k))
                if row_bits[k+16]: b2 |= (1 << (7-k))
            sprite_bytes[y*3] = b0
            sprite_bytes[y*3+1] = b1
            sprite_bytes[y*3+2] = b2
            
        sprites_data.append(sprite_bytes)
        sprites_bits.append(grid)

    sprites_meta = []
    for i in range(angles_tot):
        angle_deg = i * angles_step
        base_angle_deg = (angle_deg % 180.0) - 90.0
        # Correctly apply the bitmap offset for different sets
        bitmap_idx = (i % num_unique_bitmaps) + bitmap_offset
        
        angle_rad = math.radians(base_angle_deg)
        dx = math.cos(angle_rad)
        dy = math.sin(angle_rad)
        
        # Consistent pivot logic using arm_length / 2
        if i < num_unique_bitmaps:
            pv_x = centered_x - (arm_length / 2) * dx
            pv_y = centered_y - (arm_length / 2) * dy
        else:
            pv_x = centered_x + (arm_length / 2) * dx
            pv_y = centered_y + (arm_length / 2) * dy
            
        sprites_meta.append({
            'pivot_x': int(round(pv_x)),
            'pivot_y': int(round(pv_y)),
            'bitmap_idx': bitmap_idx,
            'label': f"{angle_deg} deg"
        })
        
    return sprites_data, sprites_meta, sprites_bits

def generate_sun_sprite(bitmap_offset):
    grid = [[0 for _ in range(24)] for _ in range(21)]
    center_x = 12.0
    center_y = 10.0
    radius_sq = 10.4 * 10.4
    for y in range(21):
        for x in range(24):
            dx = x - center_x
            dy = y - center_y
            if dx*dx + dy*dy <= radius_sq:
                grid[y][x] = 1
                
    sprite_bytes = bytearray(64)
    for y in range(21):
        row_bits = grid[y]
        b0 = b1 = b2 = 0
        for k in range(8):
            if row_bits[k]: b0 |= (1 << (7-k))
            if row_bits[k+8]: b1 |= (1 << (7-k))
            if row_bits[k+16]: b2 |= (1 << (7-k))
        sprite_bytes[y*3] = b0
        sprite_bytes[y*3+1] = b1
        sprite_bytes[y*3+2] = b2
        
    meta = {
        'pivot_x': 12,
        'pivot_y': 10,
        'bitmap_idx': bitmap_offset,
        'label': "Sun"
    }
    return sprite_bytes, meta, grid

def main():
    parser = argparse.ArgumentParser(description='Generate sprite data for C64 instrument needles.')
    parser.add_argument('--angles_tot', type=int, default=32, help='Number of angles (default: 32)')
    parser.add_argument('--base_offset', type=int, default=96, help='Base offset for bitmap indicators (default: 96)')
    args = parser.parse_args()

    angles_tot = args.angles_tot
    base_offset = args.base_offset
    
    # Generate Long Arm (14 pixels)
    data14, meta14, bits14 = generate_arm_set(14, base_offset, angles_tot)
    # Generate Short Arm (10 pixels)
    data10, meta10, bits10 = generate_arm_set(10, base_offset + angles_tot // 2, angles_tot)
    # Generate Sun
    data_sun, meta_sun, bits_sun = generate_sun_sprite(base_offset - 1)
    
    all_data = [data_sun] + data14 + data10
    total_bitmaps = len(all_data)

    # 1. Output BIN
    bin_path = os.path.join(REPO_ROOT, "c64o", "spritedef.bin")
    os.makedirs(os.path.dirname(bin_path), exist_ok=True)
    with open(bin_path, "wb") as f:
        for data in all_data:
            f.write(data)
    print(f"Generated {bin_path} ({len(all_data) * 64} bytes)")

    # 2. Output H and CC
    h_path = os.path.join(REPO_ROOT, "c64o", "spritedef.h")
    with open(h_path, "w") as f:
        f.write("#ifndef SPRITEDEF_H\n")
        f.write("#define SPRITEDEF_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"static const uint16_t kSpriteDefMetaCount = {angles_tot};\n")
        f.write(f"static const uint16_t kSpriteDefBitmapCount = {total_bitmaps};\n\n")
        f.write("struct sprite_meta_t {\n")
        f.write("    int8_t pivot_x;\n")
        f.write("    int8_t pivot_y;\n")
        f.write("    uint8_t bitmap_idx;\n")
        f.write("};\n\n")
        f.write("extern const sprite_meta_t kSpriteDefMetaLongArm[kSpriteDefMetaCount];\n")
        f.write("extern const sprite_meta_t kSpriteDefMetaShortArm[kSpriteDefMetaCount];\n")
        f.write("extern const sprite_meta_t kSpriteDefSun;\n\n")
        f.write("#pragma compile(\"spritedef.cc\")\n\n")
        f.write("#endif\n")
    print(f"Generated {h_path}")

    cc_path = os.path.join(REPO_ROOT, "c64o", "spritedef.cc")
    with open(cc_path, "w") as f:
        f.write('#include "spritedef.h"\n\n')
        f.write("const sprite_meta_t kSpriteDefMetaLongArm[kSpriteDefMetaCount] = {\n")
        for m in meta14:
            f.write(f"    {{ {m['pivot_x']}, {m['pivot_y']}, {m['bitmap_idx']} }}, // {m['label']}\n")
        f.write("};\n\n")
        f.write("const sprite_meta_t kSpriteDefMetaShortArm[kSpriteDefMetaCount] = {\n")
        for m in meta10:
            f.write(f"    {{ {m['pivot_x']}, {m['pivot_y']}, {m['bitmap_idx']} }}, // {m['label']}\n")
        f.write("};\n\n")
        f.write(f"const sprite_meta_t kSpriteDefSun = {{ {meta_sun['pivot_x']}, {meta_sun['pivot_y']}, {meta_sun['bitmap_idx']} }};\n")
    print(f"Generated {cc_path}")

    # 3. Output Python
    lib_path = os.path.join(REPO_ROOT, "lib", "spritedef.py")
    with open(lib_path, "w") as f:
        f.write("# Generated Sprite Definitions (Long Arm 14, Short Arm 10, and Sun)\n\n")
        f.write(f"NUM_BITMAPS_TOTAL = {total_bitmaps}\n")
        f.write(f"NUM_ANGLES = {angles_tot}\n\n")
        
        f.write(f"META_SUN = {{'pivot_x': {meta_sun['pivot_x']}, 'pivot_y': {meta_sun['pivot_y']}, 'bitmap_idx': {meta_sun['bitmap_idx']}, 'label': '{meta_sun['label']}'}}\n\n")

        f.write("META_LONG_ARM = [\n")
        for m in meta14:
            f.write(f"    {{'pivot_x': {m['pivot_x']}, 'pivot_y': {m['pivot_y']}, 'bitmap_idx': {m['bitmap_idx']}, 'label': '{m['label']}'}},\n")
        f.write("]\n\n")
        
        f.write("META_SHORT_ARM = [\n")
        for m in meta10:
            f.write(f"    {{'pivot_x': {m['pivot_x']}, 'pivot_y': {m['pivot_y']}, 'bitmap_idx': {m['bitmap_idx']}, 'label': '{m['label']}'}},\n")
        f.write("]\n\n")
        
        f.write("PATTERN_SUN = [\n")
        for row in bits_sun:
            row_str = "".join(["#" if b else "." for b in row])
            f.write(f"    \"{row_str}\",\n")
        f.write("]\n\n")

        f.write("PATTERNS_LONG_ARM = [\n")
        for i, grid in enumerate(bits14):
            f.write(f"    # Bitmap {i} (Long, { (i*(360.0/angles_tot))-90 } deg)\n")
            f.write("    [\n")
            for row in grid:
                row_str = "".join(["#" if b else "." for b in row])
                f.write(f"        \"{row_str}\",\n")
            f.write("    ],\n")
        f.write("]\n\n")
        
        f.write("PATTERNS_SHORT_ARM = [\n")
        for i, grid in enumerate(bits10):
            f.write(f"    # Bitmap {i + angles_tot//2} (Short, { (i*(360.0/angles_tot))-90 } deg)\n")
            f.write("    [\n")
            for row in grid:
                row_str = "".join(["#" if b else "." for b in row])
                f.write(f"        \"{row_str}\",\n")
            f.write("    ],\n")
        f.write("]\n")
    print(f"Generated {lib_path}")

if __name__ == "__main__":
    main()
