#!/bin/sh
# Run a .prg in VICE up to a breakpoint, then dump memory and decode it.
#
#   tools/vice_dump.sh <prg> <break> <start> <bytes> [out.bin] [cycles]
#
# This is how you get a *number* out of the C64 rather than a picture: park the
# program on a known instruction, read a global, print it. See docs/emulator.md
# for the source-side half (a timer into a volatile global, and a spin loop to
# break on).
#
#   <break>  where to stop. A hex address, or a symbol from the .lbl beside the
#            .prg, or @spin to find the first branch-to-itself in the .asm, or
#            @spin:func to find it inside one function's listing.
#   <start>  first byte to dump. A hex address or a symbol.
#   <bytes>  how many. 2 and 4 are also decoded as little-endian integers.
#
# Example, after building with the instrumentation of docs/emulator.md:
#
#   tools/vice_dump.sh c64o/ppilot.prg @spin:world_update_objects g_frame_cycles 4
#
set -eu

if [ $# -lt 4 ]; then
  sed -n '2,25p' "$0" >&2
  exit 2
fi

PRG="$1"; BREAK="$2"; START="$3"; NBYTES="$4"
OUT="${5:-${PRG%.prg}.dump.bin}"
CYCLES="${6:-60000000}"

BASE="${PRG%.prg}"
LBL="$BASE.lbl"
ASM="$BASE.asm"

[ -f "$PRG" ] || { echo "vice_dump: no such file: $PRG" >&2; exit 1; }
command -v x64sc >/dev/null || { echo "vice_dump: x64sc not on PATH" >&2; exit 1; }

# Headless: wrap in a virtual X server. With a display (or on macOS) run direct.
if command -v xvfb-run >/dev/null 2>&1 && [ -z "${DISPLAY:-}" ]; then
  RUN="xvfb-run -a"
else
  RUN=""
fi

# oscar64 emits the .lbl and .asm only with -g, which c64o/Makefile always passes.
need_listing() {
  [ -f "$1" ] || {
    echo "vice_dump: $1 missing - build with -g (the Makefile already does)" >&2
    exit 1
  }
}

# A hex address, with or without $ or 0x, or a symbol in the .lbl. The .lbl
# lines look like:  al 02fa .g_frame_cycles
resolve() {
  case "$1" in
    '$'*)  echo "${1#$}" ;;
    0x*|0X*) echo "${1#0[xX]}" ;;
    [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) echo "$1" ;;
    *)
      need_listing "$LBL"
      a=$(grep -iE "^al [0-9a-f]+ \.$1\$" "$LBL" | head -1 | awk '{print $2}')
      [ -n "$a" ] || { echo "vice_dump: symbol '$1' not in $LBL" >&2; exit 1; }
      # oscar64 writes 16-bit labels with leading zeroes already.
      echo "$a" | sed 's/^0*\([0-9a-f]\{4\}\)$/\1/'
      ;;
  esac
}

# The address of an instruction that branches to itself - the `for (;;) {}` the
# instrumented build parks in. oscar64 emits it as a two-byte relative branch,
# occasionally as a JMP.
find_spin() {
  need_listing "$ASM"
  fn="$1"
  if [ -n "$fn" ]; then
    body=$(awk -v f="^$fn: " '$0 ~ f {s=1} s {print} s && /^-----/ {exit}' "$ASM")
  else
    body=$(cat "$ASM")
  fi
  a=$(printf '%s\n' "$body" \
    | grep -oE '^[0-9a-f]{4} : ([0-9a-f]{2} ){2}__ B[A-Z]{2} \$[0-9a-f]{4}' \
    | awk '{t=$NF; gsub(/\$/,"",t); if ($1==t) {print $1; exit}}')
  if [ -z "$a" ]; then
    a=$(printf '%s\n' "$body" \
      | grep -oE '^[0-9a-f]{4} : 4c ([0-9a-f]{2} ){2}JMP \$[0-9a-f]{4}' \
      | awk '{t=$NF; gsub(/\$/,"",t); if ($1==t) {print $1; exit}}')
  fi
  [ -n "$a" ] || {
    echo "vice_dump: no branch-to-itself found${fn:+ in $fn}." >&2
    echo "  The build needs a 'for (;;) {}' for this to have something to stop on." >&2
    exit 1
  }
  echo "$a"
}

case "$BREAK" in
  @spin)   BRK=$(find_spin "") ;;
  @spin:*) BRK=$(find_spin "${BREAK#@spin:}") ;;
  *)       BRK=$(resolve "$BREAK") ;;
esac
A1=$(resolve "$START")
A2=$(printf '%x' $(( 0x$A1 + NBYTES - 1 )))

# The monitor opens at the READY prompt (-initbreak), plays this script, and
# keeps playing it after `g` - which is what lets the save run once the
# breakpoint has been hit. $080D is oscar64's SYS entry; the BASIC stub the
# linker emits does the same jump.
MON=$(mktemp)
trap 'rm -f "$MON"' EXIT
cat > "$MON" <<MONEOF
l "$PRG" 0
break \$$BRK
g 080d
save "$OUT" 0 \$$A1 \$$A2
quit
MONEOF

rm -f "$OUT"
$RUN x64sc -default -warp -jamaction 1 -limitcycles "$CYCLES" \
  -initbreak ready -moncommands "$MON" >/dev/null 2>&1 || true

[ -f "$OUT" ] || {
  echo "vice_dump: nothing dumped (break \$$BRK, range \$$A1..\$$A2)." >&2
  echo "  Usually the breakpoint was never reached inside $CYCLES cycles." >&2
  exit 1
}

# VICE's `save` prefixes the two-byte load address; strip it.
python3 - "$OUT" "$BRK" "$A1" <<'PYEOF'
import sys
data = open(sys.argv[1], 'rb').read()[2:]
print("break $%s  $%s: %s" % (sys.argv[2], sys.argv[3], data.hex(' ')))
if len(data) in (2, 4):
    print("  le%d = %d" % (8 * len(data), int.from_bytes(data, 'little')))
PYEOF
