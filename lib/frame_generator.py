from typing import List, Tuple

from . import roll_angle

FRAME_WIDTH = 320
FRAME_HEIGHT = 200
CENTER_X = 160
CENTER_Y = 100

BAYER_2X2 = [
    [0, 2],
    [3, 1]
]

BAYER_4X4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5]
]

def get_bayer_offset_x2(x: int, y: int, matrix: List[List[int]]=BAYER_4X4) -> int:
    """
    Calculates the Bayer dither offset for a given pixel coordinate, scaled by 2.
    """
    h = len(matrix)
    if h == 4:
        # 4x4 matrix, values 0..15. Center 7.5. Scaled x2 -> val*2 - 15.
        val = matrix[y % 4][x % 4]
        return val * 2 - 15
    elif h == 2:
        val = matrix[y % 2][x % 2]
        # Scale 0..3 to match 4x4 range 0..15 (approx * 5)
        # val*5 -> 0, 5, 10, 15. Scaled x2 -> val*10 - 15.
        return val * 10 - 15
    return 0

def generate_frame_mcbm(colors: List[int], roll: roll_angle.RollAngle, grad_width_chars: int, soft_ground_horizon: bool=False, proportional_dither: bool=False, dither_type: str="bayer4x4", center_x: int=160, center_y: int=100) -> Tuple[int, bytes, bytes, bytes]:
    """
    Generates a full MCBM frame based on roll angle and colors.
    """
    
    ground_col, grad1_col, grad2_col, sky_col = colors
    
    total_width_px = grad_width_chars * 8
    # band_width_scaled = total_width_px / 2.0 * 1024 = total_width_px * 512
    band_width_scaled = total_width_px * 512
    
    nx, ny = roll.get_normal()
    import math
    length = math.hypot(nx, ny)
    # Fixed-point scale for distance (scaled by 1024)
    # dist_scaled = (rx_x2 * nx + ry_x2 * ny) * (1024 / (2 * length))
    # We use scale_fixed_x2 = 1024 / length
    scale_fixed_x2 = int(1024 / length) if length > 0 else 0
    
    # Prepare output buffers
    screen_ram = bytearray(1000)
    color_ram = bytearray(1000)
    bitmap = bytearray(8000)
    
    bg_color = sky_col 
    
    # Dither scale (fixed point 1024)
    # if proportional: width/4.0 * 1024 = width * 256
    # else: 0.5 * 1024 = 512
    if proportional_dither:
        dither_scale_fixed = grad_width_chars * 256
    else:
        dither_scale_fixed = 512

    for cy in range(25):
        for cx in range(40):
            char_idx = cy * 40 + cx
            block_colors = set()
            block_pixels = []
            
            y_base = cy * 8
            x_base = cx * 8
            
            for py in range(8):
                screen_y = y_base + py
                row_pixels = []
                ry_x2 = 2 * screen_y + 1 - 2 * center_y
                
                for px in range(0, 8, 2):
                    screen_x = x_base + px
                    rx_x2 = 2 * screen_x + 2 - 2 * center_x
                    
                    dist_unscaled_x2 = rx_x2 * nx + ry_x2 * ny
                    
                    # dist_scaled (1024 units) = (dist_unscaled_x2 / 2) * (1024 / length)
                    dist_scaled = (dist_unscaled_x2 * scale_fixed_x2) // 2
                    
                    if dither_type == "none":
                        dither_offset_scaled = 0
                    else:
                        matrix = BAYER_2X2 if dither_type == "bayer2x2" else BAYER_4X4
                        # get_bayer_offset_x2 is (2 * pixels)
                        # scaled_dither = (offset_x2 / 2) * (dither_scale_fixed)
                        dither_offset_scaled = (get_bayer_offset_x2(screen_x // 2, screen_y, matrix) * dither_scale_fixed) // 2
                    
                    eff_dist_scaled = dist_scaled + dither_offset_scaled
                    
                    c = sky_col
                    
                    if not soft_ground_horizon:
                        if dist_unscaled_x2 < 0:
                            c = ground_col
                        else:
                            # If strictly above horizon, ensure we don't fall back to ground due to dither
                            if grad_width_chars > 0:
                                if eff_dist_scaled < band_width_scaled:
                                    c = grad1_col
                                elif eff_dist_scaled < band_width_scaled * 2:
                                    c = grad2_col
                                else:
                                    c = sky_col
                            else:
                                c = sky_col
                    else:     
                        if eff_dist_scaled < 0:
                            c = ground_col
                        elif eff_dist_scaled < band_width_scaled:
                            c = grad1_col
                        elif eff_dist_scaled < band_width_scaled * 2:
                            c = grad2_col
                        else:
                            c = sky_col
                        
                    block_colors.add(c)
                    row_pixels.append(c) # One entry per multicolor pixel (2 screen pixels)
                block_pixels.append(row_pixels)
            
            # Constraint: No Grad1 + Sky pixels in the same block.
            if grad1_col in block_colors and sky_col in block_colors:
                grad1_count = 0
                sky_count = 0
                for r_p in block_pixels:
                    for p in r_p:
                        if p == grad1_col: grad1_count += 1
                        elif p == sky_col: sky_count += 1
                
                # Keep the more frequent one. Tie-break to sky.
                to_keep = sky_col if sky_count >= grad1_count else grad1_col
                to_replace = grad1_col if to_keep == sky_col else sky_col
                
                for r_idx in range(8):
                    row = block_pixels[r_idx]
                    for p_idx in range(4):
                        if row[p_idx] == to_replace:
                            row[p_idx] = to_keep
                
                # Update block_colors set
                block_colors.discard(to_replace)
                block_colors.add(to_keep)

            # Now mapping.
            # We have up to 4 colors in block_colors. 
            # bg_color is fixed globally.
            # Assign others to 01, 10, 11
            
            # Map: {color: bits}
            color_map = {bg_color: 0} # 00
            
            # Available slots
            slots = [1, 2, 3] # 01, 10, 11
            avail_colors = list(block_colors)
            if bg_color in avail_colors:
                avail_colors.remove(bg_color)
            
            assigned_slots = {} # slot_bits: color_idx
            
            for c in avail_colors:
                if slots:
                    s = slots.pop(0)
                    color_map[c] = s
                    assigned_slots[s] = c
                else:
                    # Too many colors! (Shouldn't happen with 4 solid bands logic unless dithering creates artifacts?)
                    # If it happens, map to nearest or something.
                    # Just map to 0 (bg) to avoid crash
                    color_map[c] = 0
            
            # Fill RAM values
            # default to 0 if unused
            c_01 = assigned_slots.get(1, 0)
            c_10 = assigned_slots.get(2, 0)
            c_11 = assigned_slots.get(3, 0)
            
            screen_byte = (c_01 << 4) | c_10
            screen_ram[char_idx] = screen_byte
            # Color RAM is only low nibble in MCBM typically, but prompt says "low 4 bits of a byte in the color ram"
            color_ram[char_idx] = c_11 
            
            # Generate Bitmap
            for r in range(8):
                byte_val = 0
                row_cols = block_pixels[r] # 4 pixel colors
                for i in range(4):
                    bits = color_map.get(row_cols[i], 0)
                    shift = 6 - (i * 2)
                    byte_val |= (bits << shift)
                bitmap[char_idx * 8 + r] = byte_val

    return bg_color, bytes(screen_ram), bytes(color_ram), bytes(bitmap)
