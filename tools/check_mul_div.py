#!/usr/bin/env python3
"""
Fails the build if oscar64's runtime multiplication, division, or modulo
functions are linked into the binary.

On the 6502, software integer division, modulo (such as divmod, divmod32, divs32)
and multiplication (such as mul32, mul32by8) routines from the compiler's CRT
are slow and consume substantial memory (e.g. 139 bytes for divmod alone).

Plane Pilot explicitly avoids general *, /, and % operators in performance-
and memory-critical code, relying instead on bit shifts, lookup tables (LUTs),
and dedicated assembly subroutines (vec_fastmul8p8, vec_div8p8, roll_mul_*, etc.).

If an inadvertent operator is introduced, oscar64 will silently link runtime
math subroutines. This tool reads both the .map and .asm files to verify that
no forbidden runtime math functions are linked, and reports call sites with line
numbers if any violation occurs.

Usage:  check_mul_div.py c64o/ppilot.asm [more.asm ...]
        check_mul_div.py c64o/ppilot.map [more.map ...]
"""

import argparse
import os
import re
import sys

# Matches compiler runtime math subroutines (and optional proxy/stack suffixes)
FORBIDDEN_BASE = re.compile(
    r'^(divmod\d*|div[su]?\d*|mod[su]?\d*|mul[su]?\d*|mul\d+by\d+|negaccu\d+|negtmp\d+)$',
    re.IGNORECASE
)

# Parse .map object lines: "28e6 - 2971 : divmod, NATIVE_CODE:code"
MAP_OBJECT_LINE = re.compile(
    r'^([0-9a-fA-F]{4})\s*-\s*([0-9a-fA-F]{4})\s*:\s*([^,]+),\s*(.*)$'
)

# Parse .asm JSR call comments: "094f : 20 1b 29 JSR $291b ; (divmod + 53)"
ASM_JSR_CALL = re.compile(
    r'JSR\s+\$[0-9a-fA-F]{4}\s*;\s*\(([A-Za-z0-9_]+)\s*\+\s*(\d+)\)'
)

# Parse .asm label definitions: "divmod: ; divmod"
ASM_LABEL_DEF = re.compile(
    r'^([A-Za-z0-9_]+):\s*;\s*([A-Za-z0-9_]+)'
)


def _base_symbol(sym):
    """Strip trailing @proxy, @stack, etc."""
    return sym.split('@')[0]


def is_forbidden(sym):
    """Returns True if the symbol is a forbidden runtime math function."""
    base = _base_symbol(sym)
    return bool(FORBIDDEN_BASE.match(base))


def inspect_map(map_path):
    """Scans .map file and returns dict of forbidden symbols with memory info."""
    forbidden = {}
    with open(map_path, 'r', encoding='utf-8', errors='replace') as f:
        in_objects = False
        for line in f:
            line_s = line.strip()
            if line_s == 'objects':
                in_objects = True
                continue
            if line_s == 'objects by size':
                in_objects = False
                continue
            if not in_objects:
                continue

            m = MAP_OBJECT_LINE.match(line_s)
            if m:
                sym = m.group(3).strip()
                sec_type = m.group(4).strip()
                if is_forbidden(sym):
                    start = int(m.group(1), 16)
                    end = int(m.group(2), 16)
                    forbidden[sym] = {
                        'start': start,
                        'end': end,
                        'size': end - start,
                        'section': sec_type,
                    }
    return forbidden


def inspect_asm(asm_path):
    """Scans .asm file for forbidden labels and JSR call sites with line numbers."""
    call_sites = {}
    labels = set()
    with open(asm_path, 'r', encoding='utf-8', errors='replace') as f:
        for line_no, line in enumerate(f, 1):
            m_lbl = ASM_LABEL_DEF.match(line.strip())
            if m_lbl and is_forbidden(m_lbl.group(1)):
                labels.add(m_lbl.group(1))

            m_call = ASM_JSR_CALL.search(line)
            if m_call:
                target = m_call.group(1)
                if is_forbidden(target):
                    call_sites.setdefault(target, []).append((line_no, line.strip()))

    return labels, call_sites


def check(file_path):
    """
    Checks a program (from .asm or .map path) for forbidden math functions.
    Returns (ok, message).
    """
    stem = os.path.splitext(file_path)[0]
    map_path = stem + '.map'
    asm_path = stem + '.asm'

    for p in (map_path, asm_path):
        if not os.path.exists(p):
            raise SystemExit(f'{p}: missing - build first')

    name = os.path.basename(stem)
    map_forbidden = inspect_map(map_path)
    asm_labels, asm_calls = inspect_asm(asm_path)

    all_forbidden = set(map_forbidden.keys()) | asm_labels | set(asm_calls.keys())

    if not all_forbidden:
        return True, f'{name:<9} ok: no runtime divmod/mul functions linked'

    msg = [f'{name:<9} FORBIDDEN FUNCTIONS: runtime math linked into binary:']
    if map_forbidden:
        for sym in sorted(map_forbidden.keys()):
            info = map_forbidden[sym]
            msg.append(
                f'          - {sym}: {info["size"]} bytes at '
                f'${info["start"]:04X}-${info["end"]:04X} ({info["section"]})'
            )
    else:
        for sym in sorted(all_forbidden):
            msg.append(f'          - {sym}')

    if asm_calls:
        for target in sorted(asm_calls.keys()):
            calls = asm_calls[target]
            msg.append(f'          Call sites for {target} ({len(calls)} total):')
            for lno, text in calls[:5]:
                msg.append(f'            line {lno}: {text}')
            if len(calls) > 5:
                msg.append(f'            ... and {len(calls) - 5} more')

    msg.append('          Replace software *, /, or % with fast bit shifts, LUTs, or vec_fastmul8p8/vec_div8p8.')
    return False, '\n'.join(msg)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='+', help='generated .asm or .map files')
    parser.add_argument('-q', '--quiet', action='store_true',
                        help='print only failures')
    args = parser.parse_args()

    failed = False
    for file_path in args.files:
        ok, message = check(file_path)
        if not ok:
            failed = True
            print(message, file=sys.stderr)
        elif not args.quiet:
            print(message)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
