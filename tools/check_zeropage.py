#!/usr/bin/env python3
"""
Fails the build if oscar64's runtime zero page collides with ours.

c64o/mem.h widens the zeropage region below oscar64's 0x80 default, which is
only safe because the compiler's own runtime happens not to reach that far.
The dangerous part of that runtime is the spilled-temporary area: it starts at
BC_REG_TMP_SAVED (0x53 with -xz) and grows *upward* with the call graph, and
oscar64 neither bounds it nor knows where the zeropage region starts. Overrun
is silent - the spill just writes over whatever __zeropage global landed there,
and the symptom is a wrong number somewhere far away.

The check reads the .asm listing that -g emits and works out which zero page
addresses belong to the compiler rather than to us. Two things mark an access
as the runtime's:

  - a named register operand - ACCU, IP, SP, ADDR, WORK, the P<n> call
    parameters, the T<n> temporaries;
  - a bare "$xx" operand with no "; (symbol + n)" comment, which is how
    hand-written assembly reaches the temporaries. oscar64 comments every
    access to a variable it placed, so an uncommented one is nobody's variable.

Both are needed. The names miss the raw accesses (vec_asm.cc and the startup
stub reach T0 and friends numerically, so a name-only scan reads vecdemo's
runtime as ending at $48 when it really reaches $54). The bare operands miss
everything the compiler names, which is most of it.

Deriving the runtime positively, rather than by elimination, also matters. The
obvious alternative - every zero page address the program touches, minus the
ones the .map says our region handed out - looks equivalent and is not: once
the region is low enough to actually collide, the contested addresses appear in
*both* sets, so subtracting hides the very overlap being looked for. That
version reports a clean build at region start 0x40, which is fifteen bytes
inside the T registers.

Usage:  check_zeropage.py c64o/ppilot.asm [more.asm ...]
The .map is assumed to sit beside each .asm under the same stem.
"""

import argparse
import os
import re
import sys

# "63bb : a4 27 __ LDY ACCU + 0" - address, up to three bytes, mnemonic, operand.
ASM_LINE = re.compile(
    r'^([0-9a-f]{4}) : ([0-9a-f]{2}) ([0-9a-f]{2}|__) ([0-9a-f]{2}|__) '
    r'(\w+)\s+(.*)$')

# The operand, when it names something: "ACCU + 0", "T3", "(P1),y".
OPERAND_SYMBOL = re.compile(r'^\(?([A-Za-z_][A-Za-z0-9_]*)')

# oscar64's own zero page registers, as it names them in the listing.
# P<n> is the call parameter area, T<n> the temporaries including the spill.
RUNTIME_SYMBOL = re.compile(
    r'^(ACCU|ADDR|FP|IP|SP|WORK|WORKY|TMP|TMPY|SREGS|REGS|[PT]\d+)$')

# A bare operand, and whether it carries oscar64's "this is variable v" comment:
# "$8c ; (flight_speed + 0)" is ours, plain "$33" is not.
BARE_OPERAND = re.compile(r'^\(?\$([0-9a-f]{2})\b(?!\s*[0-9a-f])')
OWNED_COMMENT = re.compile(r';\s*\([A-Za-z_]')

# Opcodes with a one-byte zero page operand: zp, zp,x, zp,y, (zp,x), (zp),y.
# Needed to tell "$44" the address from "$44" the immediate or branch target.
ZP_OPCODES = frozenset([
    0x01, 0x04, 0x05, 0x06, 0x11, 0x14, 0x15, 0x16, 0x21, 0x24, 0x25, 0x26,
    0x31, 0x34, 0x35, 0x36, 0x41, 0x44, 0x45, 0x46, 0x51, 0x54, 0x55, 0x56,
    0x61, 0x64, 0x65, 0x66, 0x71, 0x74, 0x75, 0x76, 0x81, 0x84, 0x85, 0x86,
    0x91, 0x94, 0x95, 0x96, 0xA1, 0xA4, 0xA5, 0xA6, 0xB1, 0xB4, 0xB5, 0xB6,
    0xC1, 0xC4, 0xC5, 0xC6, 0xD1, 0xD4, 0xD5, 0xD6, 0xE1, 0xE4, 0xE5, 0xE6,
    0xF1, 0xF4, 0xF5, 0xF6,
])

# The 6510 I/O port belongs to the hardware, not to either side.
IO_PORT = frozenset([0x00, 0x01])

# "0060 - 0100 : ZEROPAGE, zeropage" - the region itself. End is exclusive.
MAP_REGION = re.compile(r'^([0-9a-f]{4}) - ([0-9a-f]{4}) : ZEROPAGE, zeropage')

# Warn below this much slack between the runtime and the region. Not a failure:
# a build with two bytes spare is correct today, just fragile.
MIN_HEADROOM = 4


def runtime_addresses(asm_path):
    """(named, raw) zero page addresses belonging to oscar64's runtime."""
    named, raw = set(), set()
    with open(asm_path) as f:
        for line in f:
            m = ASM_LINE.match(line)
            if not m or m.group(3) == '__':
                continue
            operand, address = m.group(6).strip(), int(m.group(3), 16)

            symbol = OPERAND_SYMBOL.match(operand)
            if symbol:
                if RUNTIME_SYMBOL.match(symbol.group(1)):
                    named.add(address)
                continue

            if (int(m.group(2), 16) in ZP_OPCODES
                    and BARE_OPERAND.match(operand)
                    and not OWNED_COMMENT.search(operand)
                    and address not in IO_PORT):
                raw.add(address)
    return named, raw


def zp_region(map_path):
    """(start, end) of the zeropage region. End is exclusive."""
    with open(map_path) as f:
        for line in f:
            m = MAP_REGION.match(line)
            if m:
                return int(m.group(1), 16), int(m.group(2), 16)
    raise SystemExit('%s: no zeropage region line - stale .map?' % map_path)


def check(asm_path):
    """Returns (ok, message) for one program."""
    stem = os.path.splitext(asm_path)[0]
    map_path = stem + '.map'
    for path in (asm_path, map_path):
        if not os.path.exists(path):
            raise SystemExit('%s: missing - build first' % path)

    name = os.path.basename(stem)
    start, end = zp_region(map_path)
    named, raw = runtime_addresses(asm_path)

    # No named registers at all means the listing format moved out from under
    # us and every build would pass vacuously. Fail instead of finding nothing.
    if not named:
        return False, ('%-9s BROKEN CHECK: no oscar64 register annotations in '
                       '%s.\n          The .asm format changed; '
                       'tools/check_zeropage.py needs updating.'
                       % (name, os.path.basename(asm_path)))

    runtime = named | raw
    intruders = sorted(a for a in runtime if start <= a < end)
    if intruders:
        return False, (
            '%-9s COLLISION: oscar64 runtime uses %s inside the zeropage '
            'region $%02X-$%02X.\n'
            '          Raise the zeropage region start in c64o/mem.h above '
            '$%02X.'
            % (name, ', '.join('$%02X' % a for a in intruders[:8]),
               start, end - 1, max(intruders)))

    high = max(runtime)
    headroom = start - high - 1
    message = ('%-9s ok: runtime tops out at $%02X, region starts at $%02X '
               '(%d bytes headroom)' % (name, high, start, headroom))
    # Passing on nothing is not really passing. The spill area grows with the
    # call graph, so a build sitting flush against it is one refactor from
    # silent corruption, and the failure would land on whoever made that
    # refactor rather than on whoever chose the address.
    if headroom < MIN_HEADROOM:
        message += ('\n          WARNING: less than %d bytes spare. The spill '
                    'area grows with the call graph;\n          consider '
                    'raising the region start in c64o/mem.h.' % MIN_HEADROOM)
    return True, message


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('asm', nargs='+', help='generated .asm files')
    parser.add_argument('-q', '--quiet', action='store_true',
                        help='print only failures')
    args = parser.parse_args()

    failed = False
    for asm_path in args.asm:
        ok, message = check(asm_path)
        if not ok:
            failed = True
            print(message, file=sys.stderr)
        elif not args.quiet:
            print(message)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
