import re
import os
from typing import Dict, Any

def verify_chardefs_c(global_chars: Dict[bytes, Dict[str, Any]], c_file_path: str):
    """
    Parses chardefs.c and verifies it against global_chars.
    """
    if not os.path.exists(c_file_path):
        raise FileNotFoundError(f"Verification failed: {c_file_path} not found")

    with open(c_file_path, "r") as f:
        content = f.read()

    # Find chardefs array content
    # const uint8_t chardefs[TOTAL_CHARS][8] = { ... };
    match = re.search(r"(?:static\s+)?(?:const\s+)?uint8_t\s+chardefs\s*\[\s*kTotalChars\s*\]\s*\[\s*8\s*\]\s*=\s*\{(.*?)\};", content, re.DOTALL)
    if not match:
        raise ValueError("Verification failed: Could not find chardefs array in chardefs.c")

    array_body = match.group(1)
    # Match individual { 0x.., ... } rows
    rows = re.findall(r"\{(.*?)\}", array_body)
    
    if len(rows) != len(global_chars):
        raise ValueError(f"Verification failed: chardefs.c has {len(rows)} characters, expected {len(global_chars)}")

    sorted_chars = sorted(global_chars.items(), key=lambda item: item[1]['id'])
    
    for i, row_text in enumerate(rows):
        # Parse hex values
        byte_vals = [int(x.strip(), 16) for x in row_text.split(",") if x.strip()]
        if len(byte_vals) != 8:
             raise ValueError(f"Verification failed: Row {i} in chardefs.c has {len(byte_vals)} bytes, expected 8")
        
        expected_bytes = sorted_chars[i][0]
        if bytes(byte_vals) != expected_bytes:
             raise ValueError(f"Verification failed: Character index {i} mismatch.\n  Expected: {expected_bytes.hex()}\n  Got:      {bytes(byte_vals).hex()}")

    print(f"Successfully verified {c_file_path} against global character set.")


def verify_boxdefs_c(box_defs: Dict[str, Dict[str, Any]],
                     c_file_path: str,
                     total_chars: int):
    """
    Parses boxdefs.cc and verifies its contents against box_defs.

    Characters are stored as single bytes relative to the box's char_offset,
    so the check reconstructs (char_offset + char_idx[i]) % kTotalChars and
    compares it against the global chardefs id the box actually needs.
    """
    from . import find_boxes

    if not os.path.exists(c_file_path):
        raise FileNotFoundError(f"Verification failed: {c_file_path} not found")

    with open(c_file_path, "r") as f:
        content = f.read()

    for name, expected in box_defs.items():
        cname = name.lower()
        layout = find_boxes.compute_box_layout(name, expected, total_chars)

        # Verify boxdef_t struct fields
        # static const boxdef_t box_r8_def = { 1, 1, 1, 8, 0, 0, 0, 0, 0, r8_idx, r8_chars };
        # The fields are: w, h, total, step_x, step_y, rel_x, rel_y, char_count, char_offset, idx, chars
        struct_match = re.search(rf"static const boxdef_t {cname}_def = \{{(.*?)\}};", content, re.DOTALL)
        if not struct_match:
             raise ValueError(f"Verification failed: Could not find boxdef_t struct for {name}")
        body = struct_match.group(1)
        # Remove comments first
        body = re.sub(r"//.*", "", body)
        fields = body.split(",")
        # Clean up whitespace
        clean_fields = [f.strip() for f in fields if f.strip()]
            

        # Verify static arrays _idx and _chars
        chars_match = re.search(rf"(?:static\s+)?(?:const\s+)?uint8_t\s+{cname}_chars\s*\[\s*\]\s*=\s*\{{(.*?)\}};", content)
        if not chars_match:
             raise ValueError(f"Verification failed: Could not find chars array for {name} in boxdefs.c")
        actual_chars = [int(x.strip()) for x in chars_match.group(1).split(",") if x.strip()]

        # Expected fields: w, h, total, sx, sy, rx, ry, g1_start, cnt, off, idx_ptr, chars_ptr
        if len(clean_fields) != 12:
            raise ValueError(f"Verification failed: {name} struct has {len(clean_fields)} fields, expected 12")

        # Parse fields needed for array verification
        grad1_start = int(clean_fields[7])
        char_count = int(clean_fields[8])
        char_offset = int(clean_fields[9])
        idx_match = re.search(rf"(?:static\s+)?(?:const\s+)?uint8_t\s+{cname}_idx\s*\[\s*\]\s*=\s*\{{(.*?)\}};", content)
        if not idx_match:
            raise ValueError(f"Verification failed: Could not find idx array for {name} in boxdefs.c")
        raw_idx = [int(x.strip()) for x in idx_match.group(1).split(",") if x.strip()]
        if any(x > 255 for x in raw_idx):
            raise ValueError(f"Verification failed: {name} has a relative char index above 255")
        # The character each entry resolves to on the C64.
        actual_idx = [(x + char_offset) % total_chars for x in raw_idx]

        # Verify the resolved characters and the cell grid against the box.
        if char_offset != layout['char_offset']:
            raise ValueError(f"{name} char_offset mismatch: {char_offset} != {layout['char_offset']}")
        if actual_idx != layout['char_ids']:
            raise ValueError(f"Verification failed: {name} resolves to characters {actual_idx}, expected {layout['char_ids']}")
        if actual_chars != layout['grid']:
            raise ValueError(f"Verification failed: {name} box_chars mismatch")
        if grad1_start != layout['grad1_start']:
            raise ValueError(f"{name} grad1_color_start mismatch")
        if char_count != layout['char_count']:
            raise ValueError(f"{name} char_count mismatch")

        # Verify Struct Fields
        if int(clean_fields[0]) != expected['w']: raise ValueError(f"{name} w mismatch")
        if int(clean_fields[1]) != expected['h']: raise ValueError(f"{name} h mismatch")
        if int(clean_fields[2]) != expected['w'] * expected['h']: raise ValueError(f"{name} total_size mismatch")
        if int(clean_fields[3]) != expected['step_x']: raise ValueError(f"{name} step_x mismatch")
        if int(clean_fields[4]) != expected['step_y']: raise ValueError(f"{name} step_y mismatch")
        if int(clean_fields[5]) != expected['rel_x']: raise ValueError(f"{name} rel_x mismatch")
        if int(clean_fields[6]) != expected['rel_y']: raise ValueError(f"{name} rel_y mismatch")
        if char_count != len(actual_idx): raise ValueError(f"{name} char_count vs idx array length mismatch")

    print(f"Successfully verified {c_file_path} against box definitions.")
