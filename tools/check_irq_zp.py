#!/usr/bin/env python3
"""Fails the build if a raster interrupt handler touches oscar64's runtime zero
page, or reaches memory through a zero page pointer.

Why this exists
---------------

The raster handlers in gfx.cc run from an interrupt, on top of whatever the
main line was doing. oscar64 keeps its own working registers in the low zero
page -- mem.h's comment maps the layout: 0x02-0x06 WORK, 0x0D-0x24 FPARAMS,
0x25 IP, 0x27 ACCU, 0x2B ADDR, 0x2F sp, 0x31 LOCALS, 0x33-0x52 TMP, and the
spilled temporaries above that. Those bytes belong to the interrupted code. A
handler that writes them corrupts the main line's arithmetic at a point it has
no way to detect, and the failure is a scrambled screen a long way from the
cause.

Handler code stays clear of them as long as every address it touches is a
link-time constant, which is the normal case for `vic.spr_pos[i].x = ...` and
friends: those compile to absolute-indexed stores. What breaks it is a *pointer
variable* -- a `const frame_t *f` parameter, a `p = cond ? &a : &b`, anything
oscar64 has to stage somewhere before it can dereference. It stages it in ACCU
or T1..T3 and reads through `(zp),y`, and the handler is then unsafe.

This is not what tools/check_zeropage.py checks. That one guards the boundary
between oscar64's spilled temporaries and the region mem.h claims at 0x60; this
one guards the boundary between interrupt and main line, which is a different
failure with the same smell.

Usage:  check_irq_zp.py <program.asm> [<program.asm> ...]
"""

import re
import sys

# Functions installed with rirq_call() in gfx.cc, plus anything they call that
# the compiler did not inline. A handler that is renamed and not listed here
# stops being checked, which is the one way this guard can rot -- keep it in
# step with gfx_init_raster_irqs().
HANDLERS = (
    "_gfx_switch_to_terrain",
    "_gfx_switch_to_panel_top",
    "_gfx_switch_to_panel_top_fast",
    "_gfx_panel_top_writes",
    "_switch_to_panel_bottom",
)

# oscar64's own zero page. Everything below the region mem.h claims at 0x60 is
# the runtime's, and the runtime is shared with the interrupted code.
RUNTIME_ZP_TOP = 0x60

# The named forms oscar64 prints in the listing for the same bytes.
RUNTIME_NAMES = re.compile(
    r"\b(ACCU|ADDR|WORK|IP|LOCALS|FPARAMS|T[0-9]+|BC_REG_\w+)\b"
)

# LDA ($27),y / STA ($33,x) -- dereferencing a zero page pointer at all.
INDIRECT = re.compile(r"\(\s*(?:\$[0-9a-fA-F]{1,2}|[A-Za-z_]\w*)(?:\s*\+\s*\d+)?\s*\)\s*,\s*[xy]",
                      re.IGNORECASE)

# A direct zero page operand: LDA $27 but not LDA $2700 and not LDA #$27.
DIRECT_ZP = re.compile(
    r"\b(?:LDA|STA|LDX|STX|LDY|STY|INC|DEC|ADC|SBC|AND|ORA|EOR|CMP|CPX|CPY|BIT|ASL|LSR|ROL|ROR)"
    r"\s+\$([0-9a-fA-F]{2})\b(?!\s*[0-9a-fA-F])",
    re.IGNORECASE,
)

FUNC_START = re.compile(r"^([A-Za-z_]\w*):\s*;")


def function_body(lines, name):
    """The listing lines of one function, or None if it is not in this build."""
    start = None
    for i, line in enumerate(lines):
        if line.startswith(name + ":"):
            start = i
            break
    if start is None:
        return None
    end = start + 1
    while end < len(lines) and not FUNC_START.match(lines[end]):
        end += 1
    return lines[start:end]


def check_program(path):
    try:
        lines = open(path).read().split("\n")
    except OSError as exc:
        print("check_irq_zp: %s" % exc, file=sys.stderr)
        return 1

    name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    problems = []
    checked = 0

    for handler in HANDLERS:
        body = function_body(lines, handler)
        if body is None:
            continue
        checked += 1
        for line in body:
            code = line.split(";", 1)[0]
            if not code.strip():
                continue
            hit = None
            if INDIRECT.search(code):
                hit = "reads through a zero page pointer"
            else:
                m = RUNTIME_NAMES.search(code)
                if m:
                    hit = "touches the oscar64 runtime register %s" % m.group(1)
                else:
                    m = DIRECT_ZP.search(code)
                    if m and int(m.group(1), 16) < RUNTIME_ZP_TOP:
                        hit = "touches oscar64 runtime zero page $%s" % m.group(1)
            if hit:
                problems.append((handler, hit, line.strip()))

    if not checked:
        print("%-9s check_irq_zp: none of the raster handlers found in %s -- "
              "has gfx_init_raster_irqs() been renamed?" % (name, path),
              file=sys.stderr)
        return 1

    if problems:
        print("%-9s FAIL: raster handler uses the shared zero page." % name,
              file=sys.stderr)
        for handler, hit, line in problems[:12]:
            print("            %s %s\n              %s" % (handler, hit, line),
                  file=sys.stderr)
        if len(problems) > 12:
            print("            ... and %d more" % (len(problems) - 12),
                  file=sys.stderr)
        print("\n            These bytes belong to the interrupted main line "
              "(see mem.h's zero\n            page comment). The usual cause is "
              "a pointer variable in handler\n            code: give the "
              "compiler a constant address instead, so the access\n"
              "            compiles to absolute indexing. sprites.cc's\n"
              "            _sprites_program_frame() macro is the worked "
              "example.", file=sys.stderr)
        return 1

    print("%-9s ok: %d raster handlers clear of the runtime zero page"
          % (name, checked))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    rc = 0
    for path in argv[1:]:
        rc |= check_program(path)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
