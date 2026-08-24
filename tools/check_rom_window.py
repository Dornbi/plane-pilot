#!/usr/bin/env python3
"""Fails the build if anything main() runs before mem_init() reads a global out
of a ROM window.

The C64 powers on with BASIC at $A000-$BFFF and KERNAL at $E000-$FFFF banked
in. mem_init() calls mmap_set(MMAP_NO_ROM) and from then on those addresses are
plain RAM, which is what the $0860..$D000 main region assumes. Before that
call, though, a *write* to a global in one of those windows lands in RAM while
a *read* of the same address comes back as a ROM byte.

That asymmetry is what makes it dangerous: nothing looks wrong, the variable
holds the right value, and the one instruction that reads it early gets a
constant out of ROM instead. Whether it bites depends on where the linker
happened to put the variable, so it appears and disappears with unrelated
changes.

It has bitten once. cpu_step_shift landed at $AC9D, ppilot.cc read it before
mem_init(), and the model was handed 165 ($A5, a BASIC ROM byte) instead of 0
as its step shift. kFlightSubstepMask became 31 and kFlightFramesPerStep 1,
sim_run()'s catch-up loop stopped terminating, and the simulation froze on the
first frame with the raster interrupt - and the engine sound - still running.

Usage:
  python3 tools/check_rom_window.py c64o/ppilot.asm [more.asm ...]
"""

import re
import sys

# BASIC and KERNAL, as seen before mmap_set(MMAP_NO_ROM). The I/O block at
# $D000-$DFFF is not listed: the main region stops at $D000, so the linker
# never places a global there, and the code that does use it (map.cc) banks
# deliberately.
ROM_WINDOWS = ((0xA000, 0xC000, 'BASIC'), (0xE000, 0x10000, 'KERNAL'))

# Opcodes that *read* memory through an absolute address. Stores are fine -
# they reach the RAM under the ROM - so STA/STX/STY are deliberately absent.
# The read-modify-writes are here because they read first.
READ_OPS = {
    0xAD, 0xBD, 0xB9,        # LDA abs, abs,x, abs,y
    0xAE, 0xBE,              # LDX abs, abs,y
    0xAC, 0xBC,              # LDY abs, abs,x
    0xCD, 0xDD, 0xD9,        # CMP
    0xEC, 0xCC,              # CPX, CPY
    0x6D, 0x7D, 0x79,        # ADC
    0xED, 0xFD, 0xF9,        # SBC
    0x2D, 0x3D, 0x39,        # AND
    0x0D, 0x1D, 0x19,        # ORA
    0x4D, 0x5D, 0x59,        # EOR
    0x2C,                    # BIT
    0xEE, 0xFE, 0xCE, 0xDE,  # INC, DEC
    0x0E, 0x1E, 0x4E, 0x5E,  # ASL, LSR
    0x2E, 0x3E, 0x6E, 0x7E,  # ROL, ROR
}

LINE = re.compile(
    r'^([0-9a-f]{4}) : ([0-9a-f]{2}) ([0-9a-f]{2}) ([0-9a-f]{2}) '
    r'(\w{3})\s+\$([0-9a-f]{4})')
JSR = re.compile(r'^[0-9a-f]{4} : 20 [0-9a-f]{2} [0-9a-f]{2} JSR '
                 r'\$[0-9a-f]{4} ; \((\w+)')
LABEL = re.compile(r'^(\w+): ;')


def parse(path):
    """asm text -> {function name: [source lines]}, in listing order."""
    funcs, cur = {}, None
    for line in open(path):
        line = line.rstrip('\n')
        m = LABEL.match(line)
        if m:
            cur = m.group(1)
            funcs[cur] = []
        elif cur is not None:
            funcs[cur].append(line)
    return funcs


def rom_window(addr):
    for lo, hi, name in ROM_WINDOWS:
        if lo <= addr < hi:
            return name
    return None


def scan(funcs, name, stop_at, seen, depth=0):
    """Reads from a ROM window in `name`, and in everything it calls."""
    if name in seen or depth > 6 or name not in funcs:
        return []
    seen.add(name)
    out = []
    for line in funcs[name]:
        m = LINE.match(line.strip())
        if m:
            op = int(m.group(2), 16)
            target = int(m.group(6), 16)
            win = rom_window(target)
            if op in READ_OPS and win:
                sym = line.split(';')[-1].strip() if ';' in line else ''
                out.append((m.group(1), m.group(5), target, win, sym, name))
        j = JSR.match(line.strip())
        if j:
            callee = j.group(1)
            if callee.startswith(stop_at):
                return out          # reached mem_init: everything after is safe
            out += scan(funcs, callee, stop_at, seen, depth + 1)
    return out


def check(path):
    funcs = parse(path)
    if 'main' not in funcs:
        print(f"{path}: no main() in the listing", file=sys.stderr)
        return 1

    # Only the part of main() before it calls mem_init(), plus everything that
    # part calls. After mem_init() the ROM is gone and the whole map is RAM.
    if not any(JSR.match(l.strip()) and JSR.match(l.strip()).group(1).startswith('mem_init')
               for l in funcs['main']):
        print(f"{path}: main() never calls mem_init() - check the assumption",
              file=sys.stderr)
        return 1

    bad = scan(funcs, 'main', 'mem_init', set())
    label = path.split('/')[-1].replace('.asm', '')
    if not bad:
        print(f"{label:<9} ok: nothing before mem_init() reads a ROM window")
        return 0

    print(f"{label}: read from a ROM window before mem_init() banks it out",
          file=sys.stderr)
    for addr, mnem, target, win, sym, fn in bad:
        print(f"  ${addr}  {mnem} ${target:04X}  in {fn}()  ({win} ROM) {sym}",
              file=sys.stderr)
    print("  The write went to RAM and this read comes back from ROM. Move the"
          " access after mem_init(), or pass the value in a register.",
          file=sys.stderr)
    return 1


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    return max(check(p) for p in sys.argv[1:])


if __name__ == '__main__':
    sys.exit(main())
