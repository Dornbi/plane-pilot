#!/usr/bin/env python3
"""Fails the build if anything but main()'s start-up calls into the boot region.

Why this exists
---------------

c64o/mem.h puts the code that runs once at power on - ppilot.cc's _boot(),
cpu_probe(), mem_init() - in a linker region of its own at the bottom of RAM,
and once _boot() has returned the program reuses those bytes as scratch
(kBootScratch: box.cc's caches live there). That is only sound while nothing
ever runs that code again. A call into it after the scratch has been written
executes whatever box_prepare() last left in its caches, and the failure is a
crash, or worse something that works until the roll angle changes.

Nothing in oscar64 knows about that rule. The obvious ways to break it are all
quiet: a boot-only function gets a second caller, a helper that is not boot-only
gets wrapped in `#pragma code(bootcode)`, or the compiler puts something shared
- an outlined sequence, say - in the region. Each of them shows up in the
listing as a JSR or JMP from outside the region to an address inside it.

The rule
--------

Code outside the region may jump or call into it only from main(), and only
from outside every loop in main() - between a backward branch or jump and its
target is a loop body, and a call there would run again. In ppilot that leaves
the one call to _boot() ahead of the game loop; polydemo calls mem_init()
directly, after loops of its own that the compiler inlined, and that is fine
too. Calls between functions inside the region are fine: they all run before
the last of them returns.

A build without the region (vecdemo, vectest) passes trivially.

Usage:  check_boot_region.py <program.asm> [<program.asm> ...]
        (reads the .map beside each .asm for the region's bounds)
"""

import re
import sys

REGION = re.compile(r'^([0-9a-f]{4}) - ([0-9a-f]{4}) : [0-9a-f]{4}, [0-9a-f]{4}, boot$')
LABEL = re.compile(r'^(\w+): ;')
INSN = re.compile(
    r'^([0-9a-f]{4}) : ([0-9a-f]{2}) [0-9a-f_]{2} [0-9a-f_]{2} (\w{3})\b\s*(.*)$')
TARGET = re.compile(r'^\$([0-9a-f]{4})\b')

CALLS = {0x20: 'JSR', 0x4C: 'JMP'}
BRANCHES = {0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0}


def boot_region(map_path):
    for line in open(map_path):
        m = REGION.match(line.strip())
        if m:
            return int(m.group(1), 16), int(m.group(2), 16)
    return None


def parse(asm_path):
    """asm text -> [(function, address, opcode, mnemonic, target or None)]."""
    out, cur = [], None
    for line in open(asm_path):
        line = line.rstrip('\n')
        m = LABEL.match(line)
        if m:
            cur = m.group(1)
            continue
        m = INSN.match(line)
        if m and cur is not None:
            t = TARGET.match(m.group(4).strip())
            out.append((cur, int(m.group(1), 16), int(m.group(2), 16),
                        m.group(3), int(t.group(1), 16) if t else None))
    return out


def check(asm_path):
    label = asm_path.split('/')[-1].replace('.asm', '')
    region = boot_region(asm_path[:-len('.asm')] + '.map')
    if region is None:
        print(f"{label:<9} ok: no boot region")
        return 0
    lo, hi = region
    insns = parse(asm_path)
    inside = lambda a: lo <= a < hi

    boot_funcs = sorted({f for f, addr, *_ in insns if inside(addr)})

    # main()'s loop bodies: from each backward branch or jump's target to the
    # branch itself.
    loops = [(target, addr) for f, addr, op, _mn, target in insns
             if f == 'main' and target is not None and target <= addr and
             (op in BRANCHES or op == 0x4C)]
    in_loop = lambda a: any(t <= a <= b for t, b in loops)

    bad = []
    for f, addr, op, mn, target in insns:
        if inside(addr) or target is None or not inside(target):
            continue
        if op not in CALLS:
            continue  # data access: the scratch's tenants, which is the point
        if f == 'main' and not in_loop(addr):
            continue
        bad.append((f, addr, mn, target))

    if not bad:
        print(f"{label:<9} ok: boot region ${lo:04X}-${hi - 1:04X} "
              f"({', '.join(boot_funcs) or 'empty'}) entered only from "
              f"main(), outside its loops")
        return 0

    print(f"{label}: code outside the boot region calls into it", file=sys.stderr)
    for f, addr, mn, target in bad:
        print(f"  ${addr:04X}  {mn} ${target:04X}  in {f}()", file=sys.stderr)
    print("  After _boot() returns those bytes are kBootScratch (c64o/mem.h), so"
          " this would run whatever was last written there. Move the callee"
          " out of `#pragma code(bootcode)`, or the call into _boot().",
          file=sys.stderr)
    return 1


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    return max(check(p) for p in sys.argv[1:])


if __name__ == '__main__':
    sys.exit(main())
