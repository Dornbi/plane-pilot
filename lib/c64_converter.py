from typing import List, Tuple, Dict, Optional

def to_pixels(byte_val: int) -> List[int]:
    """
    Unpacks a byte into a list of 4 2-bit values.

    Args:
        byte_val (int): The byte to unpack.

    Returns:
        List[int]: List of 4 integers (0-3).
    """
    return [
        (byte_val >> 6) & 3,
        (byte_val >> 4) & 3,
        (byte_val >> 2) & 3,
        byte_val & 3
    ]

def count_pixel_diff(bytes1: bytes,
                     bytes2: bytes,
                     ground_bits: Optional[int]=None) -> Tuple[int, int]:
    """
    Counts the number of differing pixels between two 8-byte character definitions.

    If ground_bits is specified, any difference involving a ground pixel returns 64 (high penalty).

    Args:
        bytes1 (bytes): First character definition (8 bytes).
        bytes2 (bytes): Second character definition (8 bytes).
        ground_bits (Optional[int], optional): The 2-bit value representing ground color. Defaults to None.

    Returns:
        Tuple[int, int]: (other_diffs, ground_diffs)
    """
    diffs = 0
    ground_diffs = 0
    for r in range(8):
        b1 = bytes1[r]
        b2 = bytes2[r]
        if b1 == b2: continue
        
        p1 = to_pixels(b1)
        p2 = to_pixels(b2)
        
        for k in range(4):
            val1 = p1[k]
            val2 = p2[k]
            if val1 != val2:
                # If either pixel is ground color, strictly enforce no tolerance
                # at the border pixels, to avoid double steps.
                if ground_bits is not None:
                    if val1 == ground_bits or val2 == ground_bits:
                        if r == 0 or r == 7 or k == 0 or k == 3:
                            ground_diffs = 64
                        else:
                            ground_diffs += 1
                diffs += 1
    return (diffs, ground_diffs)

def pack_row(pixels: List[int], color_to_bits: Dict[int, int]) -> int:
    """
    Packs a row of 4 pixels into a single byte based on color mapping.

    Args:
        pixels (List[int]): List of 4 color indices.
        color_to_bits (Dict[int, int]): Mapping from color index to 2-bit value.

    Returns:
        int: The packed byte.

    Raises:
        ValueError: If a color is not found in the map.
    """
    b = 0
    for i in range(4):
        # pixels[i] is color index
        c = pixels[i]
        if c not in color_to_bits:
             # Should not happen if logic above is correct
             raise ValueError(f"Color {c} not in map {color_to_bits}")
        bits = color_to_bits[c]
        b |= (bits << (6 - i*2))
    return b


def convert_mcbm_to_mccm(bg_color_index: int,
                         screen_ram: bytes,
                         color_ram: bytes,
                         bitmap: bytes,
                         colors: List[int],
                         tolerance:    int,
                         ground_color_index: Optional[int] = None,
                         ground_tolerance: Optional[int] = None,
                         known_chars: Optional[List[bytes]]=None) -> Tuple[List[int], bytes, bytes, bytes]:
    """
    Convert a rendered MCBM frame to MCCM format.

    Analyzes input MCBM data and compresses it into a Character Set + Screen/Color RAM representation.

    Args:
        bg_color_index (int): Background color index (00).
        screen_ram (bytes): 1000 bytes. Upper nibble -> 01, Lower nibble -> 10.
        color_ram (bytes): 1000 bytes. Lower nibble -> 11.
        bitmap (bytes): 8000 bytes.
        colors (List[int]): Fixed list of 4 colors [Ground(01), Grad1(10), Grad2(00), Sky(11)].
        ground_color_index (int): Index of ground color for strict tolerance.
        tolerance (int): Pixel match tolerance for reuse.
        ground_tolerance (int): Pixel match tolerance for reuse.
        known_chars (Optional[List[bytes]], optional): List of known character bytes to prefer. Defaults to None.

    Returns:
        Tuple[List[int], bytes, bytes, bytes]:
            - global_colors (List[int]): Colors for bits 00, 01, 10.
            - screen_ram (bytes): 1000 bytes, indices into charset.
            - color_ram (bytes): 1000 bytes, lower 3 bits are color 11.
            - charset (bytes): 2048 bytes (256 * 8).

    Raises:
        ValueError: If constraints are violated.
    """
    
    # 1. Decode the full image to determine unique colors used
    # We need to scan the bitmap and map back to colors based on the RAMs.
    unique_colors = set()
    
    num_blocks = len(screen_ram)
    decoded_blocks = [] 
    
    for i in range(num_blocks):
        # Decode one 8x8 block (MCBM)
        sr = screen_ram[i]
        c00 = bg_color_index
        c01 = (sr >> 4) & 0x0F
        c10 = sr & 0x0F
        c11 = color_ram[i] & 0x0F
        
        # Bitmap data for this char
        base_bm = i * 8
        
        used_colors_in_block = set()

        block_rows = []
        for r in range(8):
            bm_byte = bitmap[base_bm + r]
            row_pix = []
            for pair in range(4):
                shift = 6 - (pair * 2)
                bits = (bm_byte >> shift) & 0x03
                
                col = c00
                if bits == 1: col = c01
                elif bits == 2: col = c10
                elif bits == 3: col = c11
                
                used_colors_in_block.add(col)
                row_pix.append(col)
            block_rows.append(row_pix)
            
        decoded_blocks.append(block_rows)
        unique_colors.update(used_colors_in_block)

    # 2. Assign Global Colors (00, 01, 10) and verify Color RAM constraint.
    if len(colors) != 4:
        raise ValueError(f"colors must have exactly 4 values. (01, 10, 00, 11). Got: {colors}")
    
    # User defined mapping:
    # 01: Ground (Index 0 in list)
    # 10: Grad1 (Index 1 in list)
    # 00: Grad2 (Index 2 in list)
    # 11: Sky (Index 3 in list)
    
    color_01 = colors[0] # Ground
    color_11a = colors[1] # Grad1 -> ALSO 11
    color_00 = colors[2] # Grad2
    color_11b = colors[3] # Sky -> 11
    
    globals = [color_00, color_01, 10] # 00, 01, (10 is unused)
    col_11_global = None # We'll set this per-block in the output color_ram
    
    color_to_bits = {
        color_00: 0,
        color_01: 1,
        color_11a: 3, # Grad1 now 11
        color_11b: 3  # Sky now 11
    }
    
    # Determine Ground Bits
    ground_bits = 1 # colors[0] is ground
    
    # 3. Generate Charset and RAMs
    unique_chars = {} # bytes(8) -> char_index
    charset_bytes = bytearray()
    
    # Pre-seed unique_chars with Solid blocks for each present color
    # This ensures they are always available for tolerance snapping and consistent across frames
    # Sort to ensure deterministic order (e.g. by color index)
    # Pre-seed unique_chars with Solid blocks for Ground, Sky, and Solid11
    # This ensures they are always at indices 0, 1, 2 respectively.
    # Note: Solid11 uses bits '11' (3).
    
    fixed_patterns = []
    # 0: Ground (colors[0]) - bits 01
    fixed_patterns.append((1 << 6) | (1 << 4) | (1 << 2) | 1)
    
    # 1: Sky/Grad1 (colors[3]/colors[1]) - bits 11
    fixed_patterns.append(0xFF) # 11 11 11 11
    
    for i, row_byte in enumerate(fixed_patterns):
        char_bytes = bytes([row_byte] * 8)
        
        # We append to charset to reserve the INDEX (0, 1)
        charset_bytes.extend(char_bytes)
        
        if char_bytes not in unique_chars:
             unique_chars[char_bytes] = i
    
    out_screen_ram = bytearray(num_blocks)
    out_color_ram = bytearray(num_blocks)
    
    for i in range(num_blocks):
        # Determine the color to use for bits 11 in this block
        # We look at the first pixel that uses bits 11
        block = decoded_blocks[i]
        c11_block_val = colors[3] # Default to Sky
        for row in block:
            for c in row:
                if c == colors[1]: # Grad1
                    c11_block_val = colors[1]
                    break
                if c == colors[3]: # Sky
                    c11_block_val = colors[3]
                    break
            else: continue
            break
        # We have the decoded pixels in decoded_blocks[i] (8 rows of 4 colors)
        block = decoded_blocks[i]
        
        char_def = bytearray()
        for row_indices in block:
            char_def.append(pack_row(row_indices, color_to_bits))
            
        char_def_immutable = bytes(char_def)
        
        found_idx = -1
        
        # 1. Exact match in Local
        if char_def_immutable in unique_chars:
            found_idx = unique_chars[char_def_immutable]
        
        else:
             # Strategy: Find BEST match in (Local + Known).
             best_candidate = None
             min_diff = tolerance + 1
             
             # Check Local
             if tolerance > 0:
                for existing_char_bytes, idx in unique_chars.items():
                    diffs, ground_diffs = count_pixel_diff(char_def_immutable, existing_char_bytes, ground_bits)
                    if diffs <= tolerance and (ground_tolerance is None or ground_diffs <= ground_tolerance) and diffs < min_diff:
                        min_diff = diffs
                        found_idx = idx
            
             # Check Known (Global)
             if known_chars is not None:
                 for kc in known_chars:
                     diffs, ground_diffs = count_pixel_diff(char_def_immutable, kc, ground_bits)
                     if diffs <= tolerance and (ground_tolerance is None or ground_diffs <= ground_tolerance) and diffs < min_diff:
                          # We found a better (or equal) match in global set.
                          min_diff = diffs
                          best_candidate = kc
                          found_idx = -2 # Found in Known
             
             if found_idx == -2:
                 # Check if this Global char is already in Local?
                 if best_candidate in unique_chars:
                     found_idx = unique_chars[best_candidate]
                 else:
                     # Add Global char to Local
                     if len(unique_chars) >= 256:
                        print(f"Warning: Too many unique characters (>256) for MCCM. Index will overflow: {len(unique_chars)}")
                        # raise ValueError("Too many unique characters (>256) for MCCM.")
                     new_idx = len(charset_bytes) // 8
                     unique_chars[best_candidate] = new_idx
                     found_idx = new_idx
                     if best_candidate is None:
                         raise ValueError("best_candidate is None")
                     charset_bytes.extend(best_candidate)
                     
             elif found_idx == -1:
                 # No match found within tolerance. Create new.
                 if len(unique_chars) >= 256:
                    print(f"Warning: Too many unique characters (>256) for MCCM. Index will overflow: {len(unique_chars)}")
                    # raise ValueError("Too many unique characters (>256) for MCCM.")
                 new_idx = len(charset_bytes) // 8
                 unique_chars[char_def_immutable] = new_idx
                 found_idx = new_idx
                 charset_bytes.extend(char_def_immutable)
            
        out_screen_ram[i] = found_idx
        
        # Color RAM (11).
        out_color_ram[i] = c11_block_val
        
    # Pad charset to 2048 bytes
    while len(charset_bytes) < 2048:
        charset_bytes.append(0)
        
    print(f"MCCM conversion: {len(unique_chars)} unique chars, {len(unique_colors)} unique colors")
    return globals, bytes(out_screen_ram), bytes(out_color_ram), bytes(charset_bytes)
