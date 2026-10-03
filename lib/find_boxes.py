from typing import List, Dict, Any, Union, Optional

from . import banner
from . import roll_angle

def find_box(screen_ram: Union[List[int], bytes],
             color_ram: Union[List[int], bytes],
             roll: roll_angle.RollAngle,
             center_x: int,
             center_y: int,
             gradient_width: int=4,
             sky_char: Optional[int]=None,
             gnd_char: Optional[int]=None,
             grad1_color_val: Optional[int]=None,
             min_box_width: int=1) -> Dict[str, Any]:
    """
    Calculates the box definition for a given frame.
    
    Assumes standard 40x25 screen (320x200 pixels).

    Args:
        screen_ram (List[int]): The screen RAM indices.
        color_ram (List[int]): The color RAM values.
        roll (roll_angle.RollAngle): The roll angle object.
        center_x (int): Center X coordinate (pixels).
        center_y (int): Center Y coordinate (pixels).
        gradient_width (int, optional): Gradient width. Defaults to 4.
        sky_char (int, optional): Explicit Sky character ID.
        gnd_char (int, optional): Explicit Ground character ID.
        grad1_color_val (int, optional): Color value for Grad1 in color RAM.
        min_box_width (int, optional): Minimum box width along major axis. Defaults to 1.

    Returns:
        Dict[str, Any]: Box definition dictionary (w, h, step_x, step_y, rel_x, rel_y, chars).
    """
    dx, dy = roll.get_vector()
    
    period = roll.period()
    major = max(abs(dx), abs(dy))
    common = major // period
    if common == 0: common = 1
    
    is_horz = abs(dx) >= abs(dy)
    
    if is_horz:
        box_w = period
        step_x = dx // common
        step_y = dy // common

        # Superbox logic: expand BEFORE scan
        if box_w > 0 and box_w < min_box_width:
            k = (min_box_width + box_w - 1) // box_w
            box_w *= k
            step_x *= k
            step_y *= k

        cx_char = center_x // 8
        cy_char = center_y // 8
        
        # Direction signs
        sx = 1 if dx >= 0 else -1
        sy = 1 if dy >= 0 else -1 
        
        # X Scan Range
        x_range = range(cx_char, cx_char + box_w) if sx == 1 else range(cx_char - box_w + 1, cx_char + 1)
        
        # Identify Sky/Ground chars
        if sky_char is None:
            sky_char = screen_ram[0]
        if gnd_char is None:
            gnd_char = screen_ram[24 * 40]
        
        y_start = 25
        y_end = 0
        
        raw_cols = []
        raw_col_colors = []
        
        for tx in x_range:
            # Check bounds
            if tx < 0 or tx >= 40:
                raw_cols.append([]) 
                raw_col_colors.append([])
                continue
                
            col_vals = []
            col_colors = []
            for ty in range(25):
                idx = ty * 40 + tx
                char = screen_ram[idx]
                col_vals.append(char)
                col_colors.append(color_ram[idx])
            raw_cols.append(col_vals)
            raw_col_colors.append(col_colors)
            
        for i, col in enumerate(raw_cols):
            if not col: continue
            
            c_start = 0
            while c_start < 25:
                is_sky = (col[c_start] == sky_char and raw_col_colors[i][c_start] != grad1_color_val)
                is_gnd = (col[c_start] == gnd_char)
                if not (is_sky or is_gnd):
                    break
                c_start += 1
                
            c_end = 24
            while c_end >= 0:
                is_sky = (col[c_end] == sky_char and raw_col_colors[i][c_end] != grad1_color_val)
                is_gnd = (col[c_end] == gnd_char)
                if not (is_sky or is_gnd):
                    break
                c_end -= 1
            
            if c_start <= c_end:
                 y_start = min(y_start, c_start)
                 y_end = max(y_end, c_end)
                 
        if y_start > y_end: 
            if gradient_width == 0:
                box_h = 0
            else:
                box_h = 1 
        else:
            box_h = y_end - y_start + 1
            
        chars = []
        for y in range(y_start, y_end + 1):
            for i, col in enumerate(raw_cols):
                if y < len(col):
                    is_grad1 = (raw_col_colors[i][y] == grad1_color_val)
                    chars.append((col[y], is_grad1))
                else:
                    chars.append((0, False)) 
                    
        box_x_start = x_range.start
        rel_x = box_x_start - cx_char
        rel_y = y_start - cy_char
        
        return {
            'w': box_w,
            'h': box_h,
            'step_x': step_x, 
            'step_y': step_y, 
            'rel_x': rel_x,
            'rel_y': rel_y,
            'chars': chars
        }
        
    else:
        # Vertical Major
        box_h = period
        step_x = dx // common
        step_y = dy // common
        
        # Superbox logic: expand BEFORE scan
        if box_h > 0 and box_h < min_box_width:
            k = (min_box_width + box_h - 1) // box_h
            box_h *= k
            step_x *= k
            step_y *= k

        cx_char = center_x // 8
        cy_char = center_y // 8
        
        sy = 1 if dy >= 0 else -1 
        y_range = range(cy_char, cy_char + box_h) if sy == 1 else range(cy_char - box_h + 1, cy_char + 1)
        
        raw_rows = []
        raw_row_colors = []
        for ty in y_range:
            if ty < 0 or ty >= 25:
                raw_rows.append([])
                raw_row_colors.append([])
                continue
            row_vals = []
            row_colors = []
            for tx in range(40): # Full width scan
                idx = ty * 40 + tx
                row_vals.append(screen_ram[idx])
                row_colors.append(color_ram[idx])
            raw_rows.append(row_vals)
            raw_row_colors.append(row_colors)
            
        # Crop X
        start_x = 40
        end_x = -1
        
        if sky_char is None:
            sky_char = screen_ram[0]
        if gnd_char is None:
            gnd_char = screen_ram[999]
        
        for ri, row in enumerate(raw_rows):
            if not row: continue
            cs = 0
            while cs < 40:
                is_sky = (row[cs] == sky_char and raw_row_colors[ri][cs] != grad1_color_val)
                is_gnd = (row[cs] == gnd_char)
                if not (is_sky or is_gnd):
                    break
                cs += 1
                
            ce = 39
            while ce >= 0:
                is_sky = (row[ce] == sky_char and raw_row_colors[ri][ce] != grad1_color_val)
                is_gnd = (row[ce] == gnd_char)
                if not (is_sky or is_gnd):
                    break
                ce -= 1
            
            if cs <= ce:
                start_x = min(start_x, cs)
                end_x = max(end_x, ce)
                
        if start_x > end_x:
            box_w = 0 
        else:
            box_w = end_x - start_x + 1
            
        chars = []
        for ri, row in enumerate(raw_rows):
            if not row:
                chars.extend([(0, False)]*box_w)
                continue
            if start_x < len(row):
                 for xi in range(start_x, end_x + 1):
                     is_grad1 = (raw_row_colors[ri][xi] == grad1_color_val)
                     chars.append((row[xi], is_grad1))
            else:
                 chars.extend([(0, False)]*box_w)
                 
        box_y_start = y_range.start
        rel_y = box_y_start - cy_char
        
        return {
            'w': box_w,
            'h': box_h,
            'step_x': step_x,
            'step_y': step_y,
            'rel_x': start_x - cx_char,
            'rel_y': rel_y,
            'chars': chars
        }


def generate_boxdefs_content(box_defs: Dict[str, Dict[str, Any]]) -> str:
    """
    Generates the python code for boxdefs.py.

    Args:
        box_defs (Dict[str, Dict[str, Any]]): Map of box names to definitions.

    Returns:
        str: The generated python code content.
    """
    content = banner.py_banner("lib/find_boxes.py")
    content += "# Generated Box Definitions\n\n"
    for key, data in box_defs.items():
        raw_entries = data['chars']
        
        dynamic_entries = []
        seen_entries = {}
        grid = []
        for entry in raw_entries:
            char_id, is_grad1 = entry if isinstance(entry, tuple) else (entry, False)
            if char_id == 0:
                grid.append(0)  # Ground
            elif char_id == 1 and not is_grad1:
                grid.append(1)  # Sky
            elif char_id == 1 and is_grad1:
                grid.append(2)  # Solid Grad1
            else:
                if (char_id, is_grad1) not in seen_entries:
                    seen_entries[(char_id, is_grad1)] = len(dynamic_entries)
                    dynamic_entries.append((char_id, is_grad1))
                grid.append(3 + seen_entries[(char_id, is_grad1)])

        sorted_dynamic = sorted(dynamic_entries, key=lambda x: x[1])
        grad1_start = 0
        while grad1_start < len(sorted_dynamic) and not sorted_dynamic[grad1_start][1]:
            grad1_start += 1
            
        old_to_new = { seen_entries[entry]: new_idx for new_idx, entry in enumerate(sorted_dynamic) }
        
        mapped_grid = []
        for val in grid:
            if val < 3:
                mapped_grid.append(val)
            else:
                mapped_grid.append(3 + old_to_new[val - 3])
                
        char_idx = [e[0] for e in sorted_dynamic]

        content += f"{key} = (\n"
        content += f"  {data['w']}, # width\n"
        content += f"  {data['h']}, # height\n"
        content += f"  {data['step_x']}, # step_x\n"
        content += f"  {data['step_y']}, # step_y\n"
        content += f"  {data.get('rel_x', 0)}, # rel_x\n"
        content += f"  {data.get('rel_y', 0)}, # rel_y\n"
        content += f"  {grad1_start}, # grad1_color_start\n"
        content += f"  {len(sorted_dynamic)}, # char_count\n"
        content += f"  {char_idx}, # char_idx\n"
        content += f"  {mapped_grid} # box_chars\n"
        content += ")\n\n"
    return content


def _box_entries(data: Dict[str, Any]):
    """(char_id, is_grad1) for every cell of a box, in cell order."""
    for entry in data['chars']:
        yield entry if isinstance(entry, tuple) else (entry, False)


def build_c_charset(global_chars: Dict[bytes, Dict[str, Any]],
                    box_defs: Dict[str, Dict[str, Any]]):
    """
    The characters chardefs.cc holds, each one once per vertical flip.

    A multicolour character turned upside down is its eight bytes in reverse
    order, and box_prepare() copies characters into character RAM one at a
    time anyway, so it can copy a stored character backwards for no more than
    it costs to copy it forwards. About a third of the global character set
    turns out to be another member's vertical flip - 107 pairs of 333 - so each
    such pair is stored once and referenced as stored or flipped.

    Only the characters a box references dynamically are stored: 0 and 1 are
    the solid ground and sky, which box_prepare() never copies.

    The member of a pair with the lower global id is the stored one, and the
    stored characters are in global id order, so chardefs.cc still reads in
    the order the generator found them.

    Returns (stored, char_map):
        stored    [(bytes, [global ids it serves])], in chardefs.cc order
        char_map  global id -> (index into stored, flipped)
    """
    by_id = {info['id']: char_bytes for char_bytes, info in global_chars.items()}
    referenced = sorted({char_id
                         for data in box_defs.values()
                         for char_id, _ in _box_entries(data)
                         if char_id not in (0, 1)})
    stored = []
    slot_of = {}
    char_map = {}
    for char_id in referenced:
        char_bytes = by_id[char_id]
        flipped = bytes(reversed(char_bytes))
        if flipped in slot_of:
            slot = slot_of[flipped]
            char_map[char_id] = (slot, True)
        else:
            slot = len(stored)
            slot_of[char_bytes] = slot
            stored.append((char_bytes, []))
            char_map[char_id] = (slot, False)
        stored[slot][1].append(char_id)
    if len(stored) > 256:
        raise ValueError(
            f"{len(stored)} characters to store: box_prepare() indexes "
            f"chardefs with a byte, so at most 256 fit.")
    return stored, char_map


def compute_box_layout(name: str,
                       data: Dict[str, Any],
                       char_map: Dict[int, Any]) -> Dict[str, Any]:
    """
    Reduces one box definition to the form boxdefs.cc needs.

    The box's unique characters are collected in the order the C code copies
    them into character RAM: sky-coloured ones first, then Grad1-coloured, as
    box_prepare() colours them by that split. Each is stored as a single byte,
    its index into chardefs.cc (see build_c_charset()), and some are that
    entry upside down.

    Within each colour the order is otherwise free, so it is chosen to make the
    flipped characters one run: sky unflipped, sky flipped, Grad1 flipped,
    Grad1 unflipped. The run always touches grad1_start, so one byte per box
    says which characters box_prepare() copies backwards - how many of the
    run lie below grad1_start in its low nibble, how many from it up in its
    high one - and the index bytes stay plain indices. One boundary would not
    do: some boxes have unflipped characters of both colours either side of a
    run that has both colours in it.

    Returns a dict with:
        grid          local char index per cell (0..2 are the solid chars)
        char_idx      chardefs.cc index per unique character
        char_ids      the corresponding global character ids
        flip_start    first local index that is copied upside down
        flip_end      one past the last; equal to flip_start if none is
        flip_around   the same run as boxdefs.cc stores it: the number of
                      its characters below grad1_start, plus 16 times the
                      number from grad1_start up
        char_count    number of unique characters
        grad1_start   first local index that uses the Grad1 colour
    """
    # unique_entries: (char_id, is_grad1) -> local_index
    dynamic_entries = [] # List of (char_id, is_grad1)
    seen_entries = {}

    # Build raw grid using (global_id, is_grad1)
    grid = []
    for char_id, is_grad1 in _box_entries(data):
        if char_id == 0:
            grid.append(0)  # Ground
        elif char_id == 1 and not is_grad1:
            grid.append(1)  # Sky
        elif char_id == 1 and is_grad1:
            grid.append(2)  # Solid Grad1
        else:
             if (char_id, is_grad1) not in seen_entries:
                 seen_entries[(char_id, is_grad1)] = len(dynamic_entries)
                 dynamic_entries.append((char_id, is_grad1))
             grid.append(3 + seen_entries[(char_id, is_grad1)])

    # Sky (False) first, and within each colour the flipped characters toward
    # the boundary between the two, which makes them one run. sorted() is
    # stable, so the order the cells first used them survives otherwise.
    def order(entry):
        char_id, is_grad1 = entry
        flipped = char_map[char_id][1]
        return (is_grad1, flipped != is_grad1)
    sorted_dynamic = sorted(dynamic_entries, key=order)
    grad1_start = 0
    while grad1_start < len(sorted_dynamic) and not sorted_dynamic[grad1_start][1]:
        grad1_start += 1

    # Mapping from old unsorted local index to new sorted local index
    old_to_new = { seen_entries[entry]: new_idx for new_idx, entry in enumerate(sorted_dynamic) }

    # Remap grid
    mapped_grid = []
    for val in grid:
        if val < 3:
            mapped_grid.append(val)
        else:
            mapped_grid.append(3 + old_to_new[val - 3])

    char_count = len(sorted_dynamic)
    if char_count > 254:
        raise ValueError(f"Box {name} uses {char_count} dynamic characters, which exceeds limit of 254.")

    dynamic_ids = [e[0] for e in sorted_dynamic]
    char_idx = [char_map[cid][0] for cid in dynamic_ids]
    flips = [char_map[cid][1] for cid in dynamic_ids]
    if any(flips):
        flip_start = flips.index(True)
        flip_end = len(flips) - flips[::-1].index(True)
    else:
        flip_start = flip_end = grad1_start
    if flips != [flip_start <= i < flip_end for i in range(char_count)]:
        raise ValueError(f"Box {name}: the flipped characters are not one run.")
    below, above = grad1_start - flip_start, flip_end - grad1_start
    if not (0 <= below <= 15 and 0 <= above <= 15):
        raise ValueError(
            f"Box {name}: the flipped run reaches {below} below and {above} "
            f"above grad1_color_start; flip_around holds at most 15 each way.")

    return {
        'grid': mapped_grid,
        'char_idx': char_idx,
        'char_ids': dynamic_ids,
        'flip_start': flip_start,
        'flip_end': flip_end,
        'flip_around': below | (above << 4),
        'char_count': char_count,
        'grad1_start': grad1_start,
    }


def generate_boxdefs_c_content(box_defs: Dict[str, Dict[str, Any]],
                               char_map: Dict[int, Any]) -> str:
    """
    Generates boxdefs.cc content with preprocessed boxdef_t structures.
    """
    content = banner.c_banner("lib/find_boxes.py")
    content += '#include "boxdefs.h"\n\n'
    content += '#include <stddef.h>\n'
    content += '#include <string.h>\n\n'
    content += '#include "roll.h"\n\n'
    content += '#pragma data(data_box)\n\n'

    box_names = sorted(box_defs.keys())

    # helper to clean name for C identifier
    def clean_name(n):
        return n.lower()

    # Pre-process each box to generate static arrays
    for name in box_names:
        data = box_defs[name]
        layout = compute_box_layout(name, data, char_map)

        # Write static arrays
        cname = clean_name(name)

        char_idx = layout['char_idx'] or [0]
        content += f"static const uint8_t {cname}_idx[] = {{ {', '.join(map(str, char_idx))} }};\n"
        content += f"static const uint8_t {cname}_chars[] = {{ {', '.join(map(str, layout['grid']))} }};\n"

        # Write boxdef_t struct
        content += f"static const boxdef_t {cname}_def = {{\n"
        content += f"    {data['w']}, // w\n"
        content += f"    {data['h']}, // h\n"
        content += f"    {data['w'] * data['h']}, // total_size\n"
        content += f"    {data['step_x']}, // step_x\n"
        content += f"    {data['step_y']}, // step_y\n"
        content += f"    {data['rel_x']}, // rel_x\n"
        content += f"    {data['rel_y']}, // rel_y\n"
        content += f"    {layout['grad1_start']}, // grad1_color_start\n"
        content += f"    {layout['char_count']}, // char_count\n"
        if layout['flip_start'] == layout['flip_end']:
            flipped = "none flipped"
        else:
            flipped = (f"chars {layout['flip_start']}..{layout['flip_end'] - 1} "
                       f"flipped")
        content += (f"    0x{layout['flip_around']:02x}, // flip_around: "
                    f"{flipped}\n")
        content += f"    {cname}_idx, // char_idx\n"
        content += f"    {cname}_chars // box_chars\n"
        content += "};\n\n"

    # Generate Lookup Tables
    # Need to map RollAngle index to boxdef_t*
    # RollAngle is 0..59 (calculated in roll_angle.py)
    # We should generate tables main_boxes[60] and alt_boxes[60]
    
    # Not __striped: oscar64 emits an &object initializer in a striped table as
    # a plain word at the low-byte slot, so main_boxes linked with every high
    # byte 0. See bugs/striped-pointer-table.
    content += "const boxdef_t* const main_boxes[60] = {\n"
    for r_idx in range(60):
        # Find box name for this roll index (main)
        # Roll names are like R8, R16U1...
        # We need to map index back to name. 
        # Actually generate_all.py knows the rolls.
        # find_boxes doesn't know the roll list order unless we import it.
        from . import roll_angle
        roll = roll_angle.RollAngle(r_idx)
        name = f"BOX_{roll.name.upper()}"
        if name in box_defs:
            content += f"    &{clean_name(name)}_def, // {r_idx}: {name}\n"
        else:
            content += "    NULL,\n"
    content += "};\n\n"

    content += "const boxdef_t* const alt_boxes[60] = {\n"
    for r_idx in range(60):
        from . import roll_angle
        roll = roll_angle.RollAngle(r_idx)
        name = f"BOX_{roll.name.upper()}_ALT"
        if name in box_defs:
            content += f"    &{clean_name(name)}_def, // {r_idx}: {name}\n"
        else:
            content += "    NULL,\n"
    content += "};\n\n"
    content += '#pragma data(data)\n\n'

    content += "boxdef_t boxdef;\n\n"

    content += "const boxdef_t *boxdef_set_main() {\n"
    content += "  if (roll_angle >= kRollMax) {\n"
    content += "    return NULL;\n"
    content += "  }\n"
    content += "  const boxdef_t *src = main_boxes[roll_angle];\n"
    content += "  memcpy(&boxdef, src, sizeof(boxdef_t));\n"
    content += "  return src;\n"
    content += "}\n\n"

    content += "const boxdef_t *boxdef_set_alt() {\n"
    content += "  if (roll_angle >= kRollMax) {\n"
    content += "    return NULL;\n"
    content += "  }\n"
    content += "  const boxdef_t *src = alt_boxes[roll_angle];\n"
    content += "  if (src == NULL) {\n"
    content += "    return boxdef_set_main();\n"
    content += "  }\n"
    content += "  memcpy(&boxdef, src, sizeof(boxdef_t));\n"
    content += "  return src;\n"
    content += "}\n\n"

    return content


def generate_boxdefs_h_content(max_total_size: int,
                               max_char_count: int) -> str:
    """
    Generates boxdefs.h content with the specified constants.
    """
    content = banner.c_banner("lib/find_boxes.py")
    content += "#ifndef BOXDEFS_H\n"
    content += "#define BOXDEFS_H\n\n"
    content += "#include <stdint.h>\n\n"
    content += f"static const uint8_t kMaxBoxTotalSize = {max_total_size};\n"
    content += f"static const uint8_t kMaxBoxCharCount = {max_char_count};\n\n"
    content += "struct boxdef_t {\n"
    content += "  // Width of the box in chars.\n"
    content += "  uint8_t w;\n"
    content += "  // Height of the box in chars.\n"
    content += "  uint8_t h;\n"
    content += "  // Total number of chars in the box = w * h.\n"
    content += "  uint8_t total_size;\n"
    content += "  // Step in x direction to draw the next adjacent box.\n"
    content += "  int8_t step_x;\n"
    content += "  // Step in y direction to draw the next adjacent box.\n"
    content += "  int8_t step_y;\n"
    content += "  // X position of the starting box, relative to the center.\n"
    content += "  int8_t rel_x;\n"
    content += "  // Y position of the starting box, relative to the center.\n"
    content += "  int8_t rel_y;\n"
    content += "  // Local character index where Grad1 color usage starts.\n"
    content += "  uint8_t grad1_color_start;\n"
    content += "  // Number of unique characters used by this box (excluding solid 0,1)\n"
    content += "  uint8_t char_count;\n"
    content += "  // Which local characters are their chardefs entry upside down, for\n"
    content += "  // box_prepare to copy backwards. The generator orders each box so\n"
    content += "  // they are one run that touches grad1_color_start: the low nibble\n"
    content += "  // is how many of them lie below it, the high nibble how many from it\n"
    content += "  // up, so the run is grad1_color_start - low .. grad1_color_start +\n"
    content += "  // high - 1, and zero means none.\n"
    content += "  uint8_t flip_around;\n"
    content += "  // chardefs index of each character.\n"
    content += "  const uint8_t *char_idx;\n"
    content += "  // Index of each character in the local char_idx array.\n"
    content += "  const uint8_t *box_chars;\n"
    content += "};\n\n"
    content += "extern boxdef_t boxdef;\n\n"
    content += "// Updates boxdef based on roll_angle.\n"
    content += "// Returns the definition that was copied into boxdef, which box_prepare\n"
    content += "// uses as a cache identity. Returns NULL if roll_angle is out of range\n"
    content += "// (boxdef is left unchanged).\n"
    content += "// @param roll_angle\n"
    content += "// @result boxdef\n"
    content += "const boxdef_t *boxdef_set_main();\n\n"
    content += "// Same as boxdef_set_main but prefers the alternative (shifted) box.\n"
    content += "// @param roll_angle\n"
    content += "// @result boxdef\n"
    content += "const boxdef_t *boxdef_set_alt();\n\n"
    content += '#pragma compile("boxdefs.cc")\n\n'
    content += "#endif\n"
    return content
