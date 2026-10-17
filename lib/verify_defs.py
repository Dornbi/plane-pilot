import re
import os
from typing import Dict, Any, List


def parse_chardefs_c(c_file_path: str) -> List[bytes]:
    """
    The stored characters of chardefs.cc, decoded the way box_prepare() does:
    eight bytes of chardef_bytes from chardef_offset_lo[i], plus 256 from
    kCharDefPage1 on and 256 more from kCharDefPage2 on (chardefs.h).
    """
    h_file_path = os.path.splitext(c_file_path)[0] + ".h"
    for path in (c_file_path, h_file_path):
        if not os.path.exists(path):
            raise FileNotFoundError(f"Verification failed: {path} not found")

    with open(c_file_path, "r") as f:
        content = re.sub(r"//.*", "", f.read())
    with open(h_file_path, "r") as f:
        header = f.read()

    def array(name: str) -> List[int]:
        match = re.search(rf"const\s+uint8_t\s+{name}\s*\[\s*\w+\s*\]\s*=\s*\{{(.*?)\}};",
                          content, re.DOTALL)
        if not match:
            raise ValueError(f"Verification failed: Could not find {name} in chardefs.cc")
        return [int(x.strip(), 16) for x in match.group(1).split(",") if x.strip()]

    def constant(name: str) -> int:
        match = re.search(rf"\b{name}\s*=\s*(\d+);", header)
        if not match:
            raise ValueError(f"Verification failed: Could not find {name} in chardefs.h")
        return int(match.group(1))

    stream = bytes(array("chardef_bytes"))
    offset_lo = array("chardef_offset_lo")
    if len(stream) != constant("kCharDefBytes"):
        raise ValueError("Verification failed: chardef_bytes is not kCharDefBytes long")
    if len(offset_lo) != constant("kCharDefCount"):
        raise ValueError("Verification failed: chardef_offset_lo is not kCharDefCount long")
    page1 = constant("kCharDefPage1")
    page2 = constant("kCharDefPage2")

    rows = []
    for i, lo in enumerate(offset_lo):
        offset = lo + (256 if i >= page1 else 0) + (256 if i >= page2 else 0)
        if offset + 8 > len(stream):
            raise ValueError(f"Verification failed: character {i} runs past chardef_bytes")
        rows.append(stream[offset:offset + 8])
    return rows


def verify_chardefs_c(stored: List[Any], c_file_path: str):
    """
    Parses chardefs.cc and verifies it against find_boxes.build_c_charset().
    """
    rows = parse_chardefs_c(c_file_path)
    if len(rows) != len(stored):
        raise ValueError(f"Verification failed: chardefs.cc has {len(rows)} characters, expected {len(stored)}")
    if len(rows) > 256:
        raise ValueError("Verification failed: chardefs.cc has more rows than a byte can index")
    for i, (row, (expected, _ids)) in enumerate(zip(rows, stored)):
        if row != expected:
            raise ValueError(f"Verification failed: Character index {i} mismatch.\n  Expected: {expected.hex()}\n  Got:      {row.hex()}")

    print(f"Successfully verified {c_file_path} against global character set.")


def verify_boxdefs_c(box_defs: Dict[str, Dict[str, Any]],
                     c_file_path: str,
                     chardefs_c_path: str,
                     global_chars: Dict[bytes, Dict[str, Any]],
                     char_map: Dict[int, Any]):
    """
    Parses boxdefs.cc and verifies its contents against box_defs.

    The check decodes each box the way box_prepare() does - chardefs.cc row
    char_idx[i], copied backwards inside the run flip_around describes - and
    requires the bytes that come out to be the global character the box
    actually needs, so it covers chardefs.cc and the flip encoding as well as
    the fields.
    """
    from . import find_boxes

    if not os.path.exists(c_file_path):
        raise FileNotFoundError(f"Verification failed: {c_file_path} not found")

    with open(c_file_path, "r") as f:
        content = f.read()

    rows = parse_chardefs_c(chardefs_c_path)
    by_id = {info['id']: char_bytes for char_bytes, info in global_chars.items()}

    for name, expected in box_defs.items():
        cname = name.lower()
        layout = find_boxes.compute_box_layout(name, expected, char_map)

        struct_match = re.search(rf"static const boxdef_t {cname}_def = \{{(.*?)\}};", content, re.DOTALL)
        if not struct_match:
             raise ValueError(f"Verification failed: Could not find boxdef_t struct for {name}")
        body = re.sub(r"//.*", "", struct_match.group(1))
        clean_fields = [f.strip() for f in body.split(",") if f.strip()]

        # w, h, total, sx, sy, rx, ry, g1_start, cnt, flip_around, idx, chars
        if len(clean_fields) != 12:
            raise ValueError(f"Verification failed: {name} struct has {len(clean_fields)} fields, expected 12")

        chars_match = re.search(rf"(?:static\s+)?(?:const\s+)?uint8_t\s+{cname}_chars\s*\[\s*\]\s*=\s*\{{(.*?)\}};", content)
        if not chars_match:
             raise ValueError(f"Verification failed: Could not find chars array for {name} in boxdefs.cc")
        actual_chars = [int(x.strip()) for x in chars_match.group(1).split(",") if x.strip()]

        idx_match = re.search(rf"(?:static\s+)?(?:const\s+)?uint8_t\s+{cname}_idx\s*\[\s*\]\s*=\s*\{{(.*?)\}};", content)
        if not idx_match:
            raise ValueError(f"Verification failed: Could not find idx array for {name} in boxdefs.cc")
        raw_idx = [int(x.strip()) for x in idx_match.group(1).split(",") if x.strip()]

        grad1_start = int(clean_fields[7])
        char_count = int(clean_fields[8])
        flip_around = int(clean_fields[9], 0)
        flip_start = grad1_start - (flip_around & 0x0F)
        flip_end = grad1_start + (flip_around >> 4)

        # What box_prepare() copies into character RAM, slot by slot.
        for i in range(char_count):
            if raw_idx[i] >= len(rows):
                raise ValueError(f"Verification failed: {name} char {i} indexes past chardefs.cc")
            got = rows[raw_idx[i]]
            if flip_start <= i < flip_end:
                got = bytes(reversed(got))
            want = by_id[layout['char_ids'][i]]
            if got != want:
                raise ValueError(f"Verification failed: {name} char {i} decodes to {got.hex()}, expected {want.hex()}")

        if raw_idx[:char_count] != layout['char_idx']:
            raise ValueError(f"Verification failed: {name} char_idx mismatch")
        if (flip_start, flip_end) != (layout['flip_start'], layout['flip_end']):
            raise ValueError(f"{name} flip range mismatch")
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

    print(f"Successfully verified {c_file_path} against box definitions.")
