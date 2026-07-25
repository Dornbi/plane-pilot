import json
import os

from typing import List, Dict, Any, Tuple, Optional

from . import banner
from . import c64_converter
from . import c64_graphics
from . import find_boxes
from . import frame_generator
from . import renderer_engine
from . import roll_angle

def _to_k_camel_case(text):
    words = text.lower().split('_')
    return 'k' + ''.join(word.capitalize() for word in words)


def render_batch(colors: List[int],
                 output_dir: str,
                 gradient_width: int=4,
                 soft_horizon: bool=False,
                 dither: str="bayer4x4",
                 proportional_dither: bool=False,
                 include_alternates: bool=False,
                 tolerance: int=0,
                 rolls_limit: Optional[int]=None,
                 debug: bool=False,
                 min_box_width: int=1) -> Tuple[Dict[bytes, Any], Dict[str, Any], Dict[str, int]]:
    """
    Renders frames for all valid roll angles.

    Args:
        colors (List[int]): List of 4 color indices [ground, grad1, grad2, sky].
        output_dir (str, optional): Output directory. Defaults to "test_frames".
        gradient_width (int, optional): Width of gradient bands. Defaults to 4.
        soft_horizon (bool, optional): Soft horizon enable. Defaults to False.
        dither (str, optional): Dither type. Defaults to "bayer4x4".
        proportional_dither (bool, optional): Proportional dither enable. Defaults to False.
        include_alternates (bool, optional): Include alternate centers. Defaults to False.
        tolerance (int, optional): Pixel match tolerance. Defaults to 0.
        rolls_limit (Optional[int], optional): limit number of rolls to render. Defaults to None.
        debug (bool, optional): Debug output enable. Defaults to False.
        min_box_width (int, optional): Minimum box width along major axis. Defaults to 1.

    Returns:
        Tuple[Dict[bytes, Dict[str, Any]], Dict[str, Dict[str, Any]], Dict[str, int]]:
            - global_chars: Map of byte patterns to info.
            - box_defs: Map of box names to definitions.
            - special_ids: Map of special constant names to IDs.
    """
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)
        
    rolls_to_render = roll_angle.RollAngle.all_rolls()
    if rolls_limit:
        rolls_to_render = rolls_to_render[:rolls_limit]
        
    global_chars: Dict[bytes, Dict[str, Any]] = {} # bytes -> {'id': int, 'rolls': set(roll_key)}
    box_defs = {}
    # roll_key format: "r8u1_c160_100"

    color_to_bits = {
        colors[0]: 1, # Ground -> 01
        colors[1]: 3, # Grad1 -> 11
    }
    
    # Global colors validation not strictly needed if we assume inputs are valid
    # But for rendering, we might need them?
    # batch_generator doesn't use globals_list until render_mcbm? 
    # generate_frame_mcbm uses colors list directly.
    # convert_mcbm_to_mccm will return globals.
    
    ground_c = colors[0]
    
    # Helper to find bit pattern for a color
    def get_solid_bytes(color_idx: int, fallback_bits: int) -> bytes:
        bits = color_to_bits.get(color_idx, fallback_bits)
        # Construct 8 bytes of this pattern
        byte_val = (bits << 6) | (bits << 4) | (bits << 2) | bits
        return bytes([byte_val] * 8)

    bytes_ground = get_solid_bytes(ground_c, 1) 
    bytes_11 = bytes([0xFF] * 8)                # Pattern 11 (3)
    
    # Ensure these are in global_chars EARLY so they get IDs in specific order
    # 0: Ground (01)
    # 1: Sky/Grad1 (11)
    def ensure_in_global(char_bytes, char_key):
        if char_bytes not in global_chars:
            global_chars[char_bytes] = {
                'id': len(global_chars),
                'rolls': set([char_key])
            }

    ensure_in_global(bytes_ground, 'CHAR_SOLID_GROUND')
    ensure_in_global(bytes_11, 'CHAR_SOLID_11')

    # Identify IDs for constants
    id_ground = global_chars[bytes_ground]['id']
    id_11 = global_chars[bytes_11]['id']

    # Main center: Middle of screen, 160, 100
    main_cx = 160
    main_cy = 96
    
    for idx, roll in enumerate(rolls_to_render):
        centers = [(main_cx, main_cy)]
        
        if include_alternates and roll.period() == 1:
            # Alternate center: shifted 4 pixels down or right depending on major axis
            shift_x, shift_y = renderer_engine.RendererEngine._get_alt_shift(roll)
            centers.append((main_cx + shift_x, main_cy + shift_y))
        
        for cx, cy in centers:
            # Format: c160_100_01_r16u1
            # idx is 0-based index in the processed list.
            # Use 02d for index.
            
            flat_center = f"c{cx}_{cy}"
            roll_str = roll.to_string()
            roll_key = f"{flat_center}_{idx:02d}_{roll_str}"
            
            # 1. Generate MCBM
            bg, sram, cram, bitmap = frame_generator.generate_frame_mcbm(
                colors, roll, gradient_width, 
                soft_ground_horizon=soft_horizon, 
                proportional_dither=proportional_dither, 
                dither_type=dither,
                center_x=cx, center_y=cy
            )
            
            # 2. Convert to MCCM (to extract chars)
            # We don't strictly need to render the image if we just want chars, 
            # but we need to run conversion.
            try:
                # Prepare known chars (keys from global)
                known_keys = list(global_chars.keys())
                ground_tol = 0 if roll.period() < 8 else 2

                # convert_mcbm_to_mccm returns (globals, sram, cram, charset)
                g_cols, c_sram, c_cram, charset = c64_converter.convert_mcbm_to_mccm(
                    bg, sram, cram, bitmap, 
                    ground_color_index=colors[0],
                    tolerance=tolerance, 
                    ground_tolerance=ground_tol,
                    known_chars=known_keys,
                    colors=colors
                )
                
                # Create output directory
                if not os.path.exists(output_dir):
                    os.makedirs(output_dir)
                    
                debug_dir = None
                if debug:
                    debug_dir = output_dir + "_debug"
                    if not os.path.exists(debug_dir):
                        os.makedirs(debug_dir)
                
                # 2. Collect Characters & Build Global Map
                # We do this BEFORE saving images so we can use global indices for debug output.
                
                # Safest: Scan SRAM to find max index used.
                max_char_idx = -1
                for b in c_sram:
                    if b > max_char_idx: max_char_idx = b
                    
                local_to_global = {}
                
                # Now extract chars 0..max_char_idx
                for char_idx in range(max_char_idx + 1):
                    start = char_idx * 8
                    end = start + 8
                    char_bytes = charset[start:end]
                    
                    if char_bytes not in global_chars:
                        global_chars[char_bytes] = {
                            'id': len(global_chars),
                            'rolls': set()
                        }
                    
                    global_chars[char_bytes]['rolls'].add(roll_key)
                    local_to_global[char_idx] = global_chars[char_bytes]['id']
                    
                # Convert sram to use global indices
                global_sram : List[int] = [local_to_global[x] for x in c_sram]

                # Save Reference JSON
                # Create reference_frames directory
                ref_dir = os.path.join(os.path.dirname(output_dir), "reference_frames")
                if not os.path.exists(ref_dir):
                    os.makedirs(ref_dir)
                    
                ref_path = os.path.join(ref_dir, f"ref_{roll_key}.json")
                with open(ref_path, "w") as f:
                    json.dump({
                        'roll_key': roll_key,
                        'roll': roll,
                        'cx': cx,
                        'cy': cy,
                        'screen_ram': global_sram
                    }, f)

                # 3. Render and Save Images
                img = c64_graphics.C64Screen.render_mccm(g_cols, c_sram, c_cram, charset)
                
                # Save normal
                filename = f"flight_frame_{roll_key}.png"
                path = os.path.join(output_dir, filename)
                img.save(path)
                
                # Render and save debug image if requested
                if debug and debug_dir:
                    # Pass global_sram as debug_indices to show global IDs
                    img_debug = c64_graphics.C64Screen.render_mccm(g_cols, c_sram, c_cram, charset, debug=True, debug_indices=global_sram, debug_crosses=[(cx, cy)])
                    
                    path_debug = os.path.join(debug_dir, filename)
                    img_debug.save(path_debug)
                    
                # 4. Find Box Definition
                # Store as BOX_<ROLL> e.g. BOX_R8U1_ALT or BOX_R8U1
                # Format key: derived from roll_key but simplified?
                # User request: "BOX_R8U5 (or BOX_R8U1_ALT for alt center)"
                # My roll_key: `c160_100_01_r16u1` etc.
                # I should construct a clean name.
                # If cx,cy is "Main" (160,96), use simple name e.g. "R8U1".
                # If cx,cy is "Alt", use "R8U1_ALT".
                
                # Check if it is main or alt center
                # Main center is (main_cx, main_cy) - hardcoded 160, 96 now.
                is_main = (cx == 160 and cy == 96)
                box_name = roll.name
                if not is_main:
                    box_name += "_ALT"
                    
                box_def = find_boxes.find_box(
                    global_sram, c_cram, roll, cx, cy, gradient_width, 
                    sky_char=id_11, gnd_char=id_ground, grad1_color_val=colors[1],
                    min_box_width=min_box_width)
                box_defs[f"BOX_{box_name}"] = box_def
                    
            except ValueError as e:
                print(f"Skipping {roll_key}: {e}")
                
    # Identify IDs for constants
    id_ground = global_chars[bytes_ground]['id']
    id_11 = global_chars[bytes_11]['id']
    
    special_ids = {
        'CHAR_SOLID_GROUND': id_ground,
        'CHAR_SOLID_SKY': id_11,
        'CHAR_SOLID_GRAD1': id_11,
        'CHAR_SOLID_11': id_11
    }
    
    return global_chars, box_defs, special_ids

def generate_chardefs_content(global_chars: Dict[bytes, Dict[str, Any]], special_ids: Optional[Dict[str, int]]=None) -> str:
    """
    Generates the python code for chardefs.py.

    Args:
        global_chars (Dict[bytes, Dict[str, Any]]): Global character map.
        special_ids (Optional[Dict[str, int]], optional): Special IDs map. Defaults to None.

    Returns:
        str: The generated python code content.
    """
    content = banner.py_banner("lib/batch_generator.py")
    content += "# Generated Global Character Set\n\n"
    
    # Sort by ID for stability? 
    # Or just iterate.
    # We assigned IDs sequentially as we found them.
    # Let's sort by ID to be nice.
    
    sorted_chars = sorted(global_chars.items(), key=lambda item: item[1]['id'])
    
    char_names = []
    
    for char_bytes, info in sorted_chars:
        cid = info['id']
        name = f"CHAR_{cid}"
        char_names.append(name)
        
        rolls = sorted(list(info['rolls']))
        rolls_comment = ", ".join(rolls)
        # Limits comment length?
        if len(rolls_comment) > 100:
            rolls_comment = rolls_comment[:97] + "..."
            
        content += f"# Used in ({len(rolls)}): {rolls_comment}\n"
        
        # Format bytes
        byte_str = "b'" + "".join([f"\\x{b:02x}" for b in char_bytes]) + "'"
        content += f"{name} = {byte_str}\n"
        
    content += "ALL_CHARS = [\n"
    for name in char_names:
        content += f"    {name},\n"
    content += "]\n"
    


    if special_ids:
        content += "\n# Special Constants\n"
        for name, pid in special_ids.items():
            content += f"{name} = {pid}\n"
            
    return content


def generate_chardefs_c_content(global_chars: Dict[bytes, Dict[str, Any]]) -> str:
    """
    Generates the C source code for chardefs.c.
    """
    sorted_chars = sorted(global_chars.items(), key=lambda item: item[1]['id'])
    
    content = banner.c_banner("lib/batch_generator.py")
    content += '#include "chardefs.h"\n\n'
    content += f"const uint8_t chardefs[kTotalChars][8] = {{\n"
    
    items = sorted(global_chars.items(), key=lambda item: item[1]['id'])
    for char_bytes, info in items:
        cid = info['id']
        byte_vals = [f"0x{b:02x}" for b in char_bytes]
        content += f"    {{ {', '.join(byte_vals)} }}, // {cid}\n"
        
    content += "};\n"
    return content

def generate_chardefs_h_content(global_chars: Dict[bytes, Dict[str, Any]], special_ids: Dict[str, int]) -> str:
    """
    Generates the C header code for chardefs.h.
    """
    content = banner.c_banner("lib/batch_generator.py")
    content += "#ifndef CHARDEFS_H\n"
    content += "#define CHARDEFS_H\n\n"
    content += "#include <stdint.h>\n\n"
    content += f"static const uint16_t kTotalChars = {len(global_chars)};\n\n"
    
    content += "static const uint8_t kCharSolidGround = 128;\n"
    content += "static const uint8_t kCharSolidSky = 0;\n"
    content += "static const uint8_t kCharSolidGrad1 = 0;\n"
    content += "static const uint8_t kCharSolid11 = 0;\n"

    #for name, cid in special_ids.items():
    #    content += f"static const uint8_t {_to_k_camel_case(name)} = {cid};\n"
        
    content += "\nextern const uint8_t chardefs[kTotalChars][8];\n\n"
    content += "#pragma compile(\"chardefs.cc\")\n\n"
    content += "#endif\n"
    return content
