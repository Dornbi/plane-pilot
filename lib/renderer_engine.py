from typing import List, Dict, Tuple, Optional, Union

from . import boxdefs
from . import chardefs
from . import roll_angle

"""
The goal is to use this to translate the code to a c64.
The code should be as simple as possible to make it suitable to translate
to an 8-bit machine:
- Use fixed constants where possible
- Use byte, bytes, bytearray where possible
- Avoid using List, Dict, Tuple, Optional, Union if possible.
- Avoid modulo and division if possible.
"""

class RendererEngine:
    SCREEN_WIDTH_CHARS = 40
    SCREEN_HEIGHT_CHARS = 25
    
    VIEWPORT_WIDTH_CHARS = 32
    VIEWPORT_HEIGHT_CHARS = 15
    VIEWPORT_X_START_CHARS = 4  # (SCREEN_WIDTH - VIEWPORT_WIDTH) / 2
    VIEWPORT_X_END_CHARS = VIEWPORT_X_START_CHARS + VIEWPORT_WIDTH_CHARS
    VIEWPORT_Y_START_CHARS = 0
    VIEWPORT_Y_END_CHARS = VIEWPORT_Y_START_CHARS + VIEWPORT_HEIGHT_CHARS

    VIEWPORT_X_START_PIXELS = VIEWPORT_X_START_CHARS * 8
    VIEWPORT_X_END_PIXELS = VIEWPORT_X_END_CHARS * 8
    VIEWPORT_Y_START_PIXELS = VIEWPORT_Y_START_CHARS * 8
    VIEWPORT_Y_END_PIXELS = VIEWPORT_Y_END_CHARS * 8
    
    # Fixed Local Indices for Solid Characters
    LOCAL_IDX_GROUND = 0
    LOCAL_IDX_SKY = 1
    LOCAL_IDX_SOLID_11 = 2
    RESERVED_LOCAL_CHARS = 3
    
    @staticmethod
    def init_solid_chars(charset: bytearray) -> None:
        """
        Initializes the first 3 characters of the charset with the solid patterns.
        Index 0: Ground (CHAR_SOLID_GROUND)
        Index 1: Sky (CHAR_SOLID_SKY)
        Index 2: Solid 11 (CHAR_SOLID_11)
        """
        # Ensure charset is big enough
        if len(charset) < 24:
             raise ValueError("Charset buffer too small for solid chars")
             
        # Ground
        charset[0:8] = chardefs.ALL_CHARS[chardefs.CHAR_SOLID_GROUND]
        # Sky
        charset[8:16] = chardefs.ALL_CHARS[chardefs.CHAR_SOLID_SKY]
        # Solid 11
        charset[16:24] = chardefs.ALL_CHARS[chardefs.CHAR_SOLID_11]

    @staticmethod
    def init_screen_and_border(screen_ram: bytearray, color_ram: bytearray) -> None:
        """
        Initializes the screen/color RAM with the Solid 11 (Border) character.
        Only fills the area OUTSIDE the viewport.
        """
        # Dimensions
        s_w, s_h = RendererEngine.SCREEN_WIDTH_CHARS, RendererEngine.SCREEN_HEIGHT_CHARS
        
        # Viewport Bounds
        vx, vy = RendererEngine.VIEWPORT_X_START_CHARS, RendererEngine.VIEWPORT_Y_START_CHARS
        vw, vh = RendererEngine.VIEWPORT_WIDTH_CHARS, RendererEngine.VIEWPORT_HEIGHT_CHARS
        
        vx_end = RendererEngine.VIEWPORT_X_END_CHARS
        vy_end = RendererEngine.VIEWPORT_Y_END_CHARS
        
        # Solid 11 Char (Index 2)
        bg_char = RendererEngine.LOCAL_IDX_SOLID_11
        bg_color = 0 # Black? Or passed arg? Assuming 0 for border.
        
        # 1. Top Border (Rows 0 to vy)
        if vy > 0:
            top_size = s_w * vy
            screen_ram[0:top_size] = bytearray([bg_char]) * top_size
            color_ram[0:top_size] = bytearray([bg_color]) * top_size
            
        # 2. Bottom Border (Rows vy_end to s_h)
        if vy_end < s_h:
            bot_start = vy_end * s_w
            bot_size = s_w * (s_h - vy_end)
            screen_ram[bot_start : bot_start + bot_size] = bytearray([bg_char]) * bot_size
            color_ram[bot_start : bot_start + bot_size] = bytearray([bg_color]) * bot_size
            
        # 3. Side Borders (Rows vy to vy_end)
        # Iterate rows
        left_width = vx
        right_width = s_w - vx_end
        
        if left_width > 0 or right_width > 0:
            for y in range(vy, vy_end):
                row_start = y * s_w
                
                # Left Border
                if left_width > 0:
                    screen_ram[row_start : row_start + left_width] = bytearray([bg_char]) * left_width
                    color_ram[row_start : row_start + left_width] = bytearray([bg_color]) * left_width
                    
                # Right Border
                if right_width > 0:
                    r_start = row_start + vx_end
                    screen_ram[r_start : r_start + right_width] = bytearray([bg_char]) * right_width
                    color_ram[r_start : r_start + right_width] = bytearray([bg_color]) * right_width

    @staticmethod
    def _get_alt_shift(roll: roll_angle.RollAngle) -> Tuple[int, int]:
        """Returns the (dx, dy) shift for the ALT box lattice."""
        dx, dy = roll.get_vector()
        if abs(dx) >= abs(dy):
            return (0, 4)  # Horizontal major -> Vertical shift
        else:
            return (4, 0)  # Vertical major -> Horizontal shift

    @staticmethod
    def _pull_to_center(roll: roll_angle.RollAngle, cx: int, cy: int) -> Tuple[int, int]:
        """
        Returns a point (px, py) on the horizon line that is shifted by an integer number
        of major axis steps to be close to the viewport center.
        Uses bitshifts and no division.
        """
        dx, dy = roll.get_vector()
        shift = roll.get_shift() + 3
        half = 1 << (shift - 1)
        
        # Target center
        TARGET_X = 160
        TARGET_Y = 64

        if abs(dx) >= abs(dy):
            # X is major
            n = (TARGET_X - cx + half) >> shift
            px = cx + (n << shift)
            if dx < 0: n = -n
            py = cy + n * (dy << 3)
        else:
            # Y is major
            n = (TARGET_Y - cy + half) >> shift
            py = cy + (n << shift)
            if dy < 0: n = -n
            px = cx + n * (dx << 3)
        return px, py


    @staticmethod
    def _snap_center_chars(roll: roll_angle.RollAngle, cx: int, cy: int) -> Tuple[int, int, bool]:
        """
        Snaps (cx, cy) to the nearest supported center (Main or Alt lattice).
        Returns (cx_char, cy_char, use_alt).
        """
        n = roll.period()
        if (n > 8):
            n = 8

        # If the center is in the viewport, use it as is to avoid jitter.
        # Otherwise project it to the nearest supported center.
        #if (cx < RendererEngine.VIEWPORT_X_START_PIXELS or 
        #    cx >= RendererEngine.VIEWPORT_X_END_PIXELS or
        #    cy < RendererEngine.VIEWPORT_Y_START_PIXELS or
        #    cy >= RendererEngine.VIEWPORT_Y_END_PIXELS):
        px, py = RendererEngine._pull_to_center(roll, cx, cy)
        #else:
        #    px, py = cx, cy

        dx, dy = roll.get_vector()
        # C is derived from the starting point satisfying the equation:
        # -rolly * cx + rollx * cy + C = 0  =>  C = rolly * cx - rollx * cy
        C_val = dy * cx - dx * cy

        # Only consider Alt lattice for roll angles with period 1
        if n == 1:
            # 1. Main Lattice
            mx_char = (px + 4) >> 3
            my_char = (py + 4) >> 3
            dist_m = -dy * (mx_char << 3) + dx * (my_char << 3) + C_val
            if (dist_m < 0):
                dist_m = -dist_m

            # Alt shift for this roll
            asx, asy = RendererEngine._get_alt_shift(roll)

            # Find nearest char to (cx - asx)
            ax_char = (cx - asx + 4) >> 3
            ay_char = (cy - asy + 4) >> 3
            dist_a = -dy * ((ax_char << 3) + asx) + dx * ((ay_char << 3) + asy) + C_val
            if (dist_a < 0):
                dist_a = -dist_a
            
            if dist_m < dist_a:
              return mx_char, my_char, False  # best_use_alt
            else:
              return ax_char, ay_char, True   # best_use_alt
        

        best_cx_char = 0
        best_cy_char = 0
        min_dist = 10000
        
        # Iterate n points along horizon line passing through (cx, cy)
        for i in range(n):
            # 1. Main Lattice
            # char = (px + 4) >> 3  -> nearest char
            # dist = px - (char << 3)
            mx_char = (px + 4) >> 3
            my_char = (py + 4) >> 3
            dist_m = -dy * (mx_char << 3) + dx * (my_char << 3) + C_val

            if (dist_m < 0):
                dist_m = -dist_m

            if dist_m < min_dist:
                min_dist = dist_m
                best_cx_char = mx_char
                best_cy_char = my_char

            px += dx
            py += dy

        return best_cx_char, best_cy_char, False   # best_use_alt

    @staticmethod
    def get_pulled_snapped_centers(roll: roll_angle.RollAngle, cx: int, cy: int) -> Tuple[Tuple[int, int], Tuple[int, int]]:
        """Returns the pulled and snapped centers for visualization.."""
        cx_pulled, cy_pulled = RendererEngine._pull_to_center(roll, cx, cy)

        cx_char_s, cy_char_s, use_alt_s = RendererEngine._snap_center_chars(roll, cx, cy)
        asx_s, asy_s = RendererEngine._get_alt_shift(roll) if use_alt_s else (0, 0)
        cx_snap = (cx_char_s << 3) + asx_s
        cy_snap = (cy_char_s << 3) + asy_s
        return (cx_pulled, cy_pulled), (cx_snap, cy_snap)

    @staticmethod
    def render_frame(screen_ram: bytearray,
                     color_ram: bytearray,
                     charset: bytearray,
                     colors: List[int],
                     roll: roll_angle.RollAngle,
                     center_x: int,
                     center_y: int,
                     charset_start: int=0,
                     debug_boxes: Optional[List[Tuple[int, int, int, int]]]=None,
                     no_tiles: bool=False) -> Dict[int, int]:
        """
        Renders a full frame.
        Snaps center to supported lattice first.
        """
        # 1. Snap Center
        cx_char, cy_char, use_alt = RendererEngine._snap_center_chars(roll, center_x, center_y)
        asx, asy = RendererEngine._get_alt_shift(roll) if use_alt else (0, 0)
        snapped_cx = (cx_char << 3) + asx
        snapped_cy = (cy_char << 3) + asy
        
        # 2. Identify Used Characters (Global Indices)
        used_chars = set()
        
        # Function to extract chars from box def
        box = None
        if not no_tiles:
            box = RendererEngine._get_box_def(roll, use_alt)
        box_chars_list = []
        if box:
            char_idx = box[8]
            used_chars.update(char_idx)
            
        # 3. Create Mapping and Populate Charset
        global_to_local = {}
        local_to_global = {}
        
        # Pre-populate fixed indices
        global_to_local[chardefs.CHAR_SOLID_GROUND] = RendererEngine.LOCAL_IDX_GROUND
        global_to_local[chardefs.CHAR_SOLID_SKY] = RendererEngine.LOCAL_IDX_SKY
        global_to_local[chardefs.CHAR_SOLID_11] = RendererEngine.LOCAL_IDX_SOLID_11
        
        local_to_global[RendererEngine.LOCAL_IDX_GROUND] = chardefs.CHAR_SOLID_GROUND
        local_to_global[RendererEngine.LOCAL_IDX_SKY] = chardefs.CHAR_SOLID_SKY
        local_to_global[RendererEngine.LOCAL_IDX_SOLID_11] = chardefs.CHAR_SOLID_11
        
        current_local = charset_start
        if current_local < RendererEngine.RESERVED_LOCAL_CHARS:
            current_local = RendererEngine.RESERVED_LOCAL_CHARS
        
        sorted_used = sorted(list(used_chars))
        
        for char_code in sorted_used:
            if char_code in [chardefs.CHAR_SOLID_GROUND, chardefs.CHAR_SOLID_SKY, chardefs.CHAR_SOLID_11]:
                continue
            
            if current_local >= 256:
                raise ValueError(f"Charset overflow: too many unique characters (> {256 - RendererEngine.RESERVED_LOCAL_CHARS})")
            
            global_to_local[char_code] = current_local
            local_to_global[current_local] = char_code
            
            # Populate charset
            char_pattern = chardefs.ALL_CHARS[char_code]
            c_start = current_local * 8
            charset[c_start : c_start + 8] = char_pattern
            
            current_local += 1
            
        # 4. Fill Sky/Ground (Solids)
        RendererEngine._fill_sky_ground(screen_ram, color_ram, colors, roll, snapped_cx, snapped_cy)
        
        # 5. Fill Tiled Boxes
        if not no_tiles and box:
            # Map global chars to local indices
            ram_char_idx = [global_to_local[c] for c in char_idx]
            RendererEngine._tile_boxes(screen_ram, color_ram, box, roll, snapped_cx, snapped_cy, colors, 
                                      ram_char_idx, debug_boxes=debug_boxes)
            
        return local_to_global
    
    @staticmethod
    def _fill_sky_ground(screen_ram: bytearray, color_ram: bytearray, colors: List[int], roll: roll_angle.RollAngle, center_x: int, center_y: int) -> None:
        """
        Fills the viewport with Ground/Sky solid colors.
        Uses incremental arithmetic (Bresenham-like) to avoid multiplications in inner loops.
        """
        nx, ny = roll.get_normal()
        ground_c, grad1, grad2, sky_c = colors
        
        # Solid characters - fixed indices
        ground_char = RendererEngine.LOCAL_IDX_GROUND
        sky_char = RendererEngine.LOCAL_IDX_SKY
        
        # Color patterns
        color_to_bits = {
            colors[2]: 0,
            colors[0]: 1,
            colors[1]: 2,
            colors[3]: 3
        }
             
        ground_pattern = color_to_bits.get(ground_c, 0)
        sky_pattern = color_to_bits.get(sky_c, 2)
        
        sky_col_val = sky_c if sky_pattern == 3 else 0
        ground_col_val = ground_c if ground_pattern == 3 else 0
        
        # Calculate steps (change in dist per character step)
        # Original dist_x2 equation was: (16*x + 8 - 2*center_x)*nx + (16*y + 8 - 2*center_y)*ny
        # We divide by 2: dist = (8*x + 4 - center_x)*nx + (8*y + 4 - center_y)*ny
        
        step_x = 8 * nx
        step_y = 8 * ny
        
        # Viewport bounds
        start_y = RendererEngine.VIEWPORT_Y_START_CHARS
        end_y = RendererEngine.VIEWPORT_Y_END_CHARS
        start_x = RendererEngine.VIEWPORT_X_START_CHARS
        end_x = RendererEngine.VIEWPORT_X_END_CHARS
        
        if start_y < 0: start_y = 0
        if end_y > RendererEngine.SCREEN_HEIGHT_CHARS: end_y = RendererEngine.SCREEN_HEIGHT_CHARS
        if start_x < 0: start_x = 0
        if end_x > RendererEngine.SCREEN_WIDTH_CHARS: end_x = RendererEngine.SCREEN_WIDTH_CHARS

        # Calculate initial distance for the top-left corner (start_x, start_y)
        # dist_start = (8 * start_x + 4 - center_x) * nx + (8 * start_y + 4 - center_y) * ny
        dist_row_start = (8 * start_x + 4 - center_x) * nx + (8 * start_y + 4 - center_y) * ny
        row_idx_base = start_y * RendererEngine.SCREEN_WIDTH_CHARS
        
        for y in range(start_y, end_y):
            dist = dist_row_start
            idx = row_idx_base + start_x
            
            for x in range(start_x, end_x):
                if dist > 0:
                    # Sky
                    screen_ram[idx] = sky_char
                    color_ram[idx] = sky_col_val
                else:
                    # Ground
                    screen_ram[idx] = ground_char
                    color_ram[idx] = ground_col_val
                
                dist += step_x
                idx += 1
                
            dist_row_start += step_y
            row_idx_base += RendererEngine.SCREEN_WIDTH_CHARS


    @staticmethod
    def _get_box_def(roll: roll_angle.RollAngle, use_alt: bool) -> Optional[Union[Tuple, List]]:
        """
        Retrieves the appropriate box definition based on roll angle and lattice selection.
        """
        box_name_base = "BOX_" + roll.name
        
        if use_alt:
            box_alt = getattr(boxdefs, box_name_base + "_ALT", None)
            if box_alt:
                return box_alt
        
        return getattr(boxdefs, box_name_base, None)
             
    @staticmethod
    def _tile_boxes(screen_ram: bytearray, color_ram: bytearray, box: Union[Tuple, List], roll: roll_angle.RollAngle, center_x: int, center_y: int, colors: List[int], local_chars: Optional[List[int]]=None, debug_boxes: Optional[List[Tuple[int, int, int, int]]]=None) -> None:
        """
        Tiles the boxes across the viewport.

        Args:
            screen_ram (bytearray): The screen RAM buffer.
            color_ram (bytearray): The color RAM buffer.
            box (Union[Tuple, List]): The box definition.
            roll (RollAngle): The roll angle.
            center_x (int): X center coordinate.
            center_y (int): Y center coordinate.
            colors (List[int]): List of colors.
            local_chars (Optional[List[int]], optional): List of local char indices for the box. Defaults to None.
            debug_boxes (Optional[List[Tuple[int, int, int, int]]], optional): Destination for debug box coordinates.
        """
        # Unpack box definition
        w, h, sx, sy, rel_x, rel_y, grad1_start, char_count, char_idx, box_chars = box
        
        if local_chars:
            char_idx = local_chars
        
        start_char_x = center_x // 8
        start_char_y = center_y // 8
        
        # Color 11 value
        col_11_val = colors[3]
        col_grad1_val = colors[1]
        
        color_to_bits = {
            colors[2]: 0,
            colors[0]: 1,
            colors[1]: 2,
            colors[3]: 3
        }
        ground_pattern = color_to_bits.get(colors[0], 0)
        sky_pattern = color_to_bits.get(colors[3], 2)
        sky_col_val = colors[3] if sky_pattern == 3 else 0
        ground_col_val = colors[0] if ground_pattern == 3 else 0
             
        # Resolve K Range constraints
        # Box Top-Left at K:
        # bx(k) = start_char_x + k*sx + rel_x
        # by(k) = start_char_y + k*sy + rel_y
        
        # Viewport Range:
        # X: [vp_x_off, vp_x_off + vp_w)
        # Y: [vp_y_off, vp_y_off + vp_h)
        
        # Intersection non-empty if:
        # bx(k) < vp_max_x  AND  bx(k) + w > vp_min_x
        # by(k) < vp_max_y  AND  by(k) + h > vp_min_y
        
        vp_min_x = RendererEngine.VIEWPORT_X_START_CHARS
        vp_max_x = RendererEngine.VIEWPORT_X_END_CHARS
        vp_min_y = RendererEngine.VIEWPORT_Y_START_CHARS
        vp_max_y = RendererEngine.VIEWPORT_Y_END_CHARS
        
        # Base coords without K
        # C_x + k*sx
        C_x = start_char_x + rel_x
        C_y = start_char_y + rel_y
        
        # Constraints:
        # 1. k*sx < vp_max_x - C_x
        # 2. k*sx > vp_min_x - C_x - w
        
        # 3. k*sy < vp_max_y - C_y
        # 4. k*sy > vp_min_y - C_y - h
                
        # Initialize range to effectively infinite
        min_k = -1000000
        max_k = 1000000
        
        def update_range(step, lower_t, upper_t):
            # step * k > lower_t  =>  step * k >= lower_t + 1
            # step * k < upper_t  =>  step * k <= upper_t - 1
            
            nonlocal min_k, max_k
            
            target_min_val = lower_t + 1
            target_max_val = upper_t - 1
            
            if step > 0:
                # k >= ceil(target_min / step)
                # k <= floor(target_max / step)
                
                # Integer ceil(a/b) = (a + b - 1) // b
                curr_min = (target_min_val + step - 1) // step
                curr_max = target_max_val // step
                
                min_k = max(min_k, curr_min)
                max_k = min(max_k, curr_max)
                
            elif step < 0:
                # step is negative. Division flips inequality.
                # k * step >= target_min_val  =>  k <= target_min_val / step
                # k * step <= target_max_val  =>  k >= target_max_val / step
                
                # For negative divisor b:
                # floor(a/b) works normally in python (e.g. 10/-3 = -4).
                # But we want strict inequalities.
                
                # Let's multiply by -1 to keep sanity.
                # (-step) * k <= -target_min_val
                # (-step) * k >= -target_max_val
                
                pos_step = -step
                t_min = -target_max_val # New lower bound
                t_max = -target_min_val # New upper bound
                
                # Same logic as above
                curr_min = (t_min + pos_step - 1) // pos_step
                curr_max = t_max // pos_step
                
                min_k = max(min_k, curr_min)
                max_k = min(max_k, curr_max)
                
            else: # step == 0
                # Condition: 0 > lower_t AND 0 < upper_t
                # If not met, range is empty.
                if not (0 > lower_t and 0 < upper_t):
                    min_k = 1
                    max_k = 0 # Empty range
                    
        # Apply X constraints
        # k*sx > vp_min_x - C_x - w
        # k*sx < vp_max_x - C_x
        update_range(sx, vp_min_x - C_x - w, vp_max_x - C_x)
        
        # Apply Y constraints
        update_range(sy, vp_min_y - C_y - h, vp_max_y - C_y)
        
        # Iterate valid range
        if min_k > max_k:
            return # No intersection
            
        for k in range(min_k, max_k + 1):
            # Calculate box position
            bx = C_x + k * sx
            by = C_y + k * sy
            
            if debug_boxes is not None:
                debug_boxes.append((bx, by, w, h))

            # Draw box char by char
            # We can also optimize inner loop to clamp x,y range?
            # Yes. Intersect box [bx, bx+w) with [vp_min_x, vp_max_x)
            
            start_c = max(0, vp_min_x - bx)
            end_c = min(w, vp_max_x - bx)
            
            start_r = max(0, vp_min_y - by)
            end_r = min(h, vp_max_y - by)
            
            if start_c >= end_c or start_r >= end_r:
                continue
                
            for r in range(start_r, end_r):
                # Optimize: row constant
                row_char_idx_base = r * w
                dest_y = by + r
                screen_row_base = dest_y * RendererEngine.SCREEN_WIDTH_CHARS
                
                dest_x_start = bx + start_c
                # screen_idx start
                screen_idx = screen_row_base + dest_x_start
                
                for c in range(start_c, end_c):
                    local_val = box_chars[row_char_idx_base + c]
                    
                    if local_val == 0:
                        char_code = RendererEngine.LOCAL_IDX_GROUND
                        use_col = ground_col_val
                    elif local_val == 1:
                        char_code = RendererEngine.LOCAL_IDX_SKY
                        use_col = sky_col_val
                    elif local_val == 2:
                        # Solid Grad1
                        char_code = RendererEngine.LOCAL_IDX_SOLID_11 # we keep this index as it maps to id_11
                        use_col = col_grad1_val
                    else:
                        char_code = char_idx[local_val - 3]
                        is_grad1 = (local_val - 3) >= grad1_start
                        use_col = col_grad1_val if is_grad1 else col_11_val
                    
                    # No need to check viewport bounds check here, we clamped loops!
                    # Only check global RAM safety
                    if 0 <= screen_idx < (RendererEngine.SCREEN_WIDTH_CHARS * RendererEngine.SCREEN_HEIGHT_CHARS):
                         screen_ram[screen_idx] = char_code
                         color_ram[screen_idx] = use_col
                    
                    screen_idx += 1

