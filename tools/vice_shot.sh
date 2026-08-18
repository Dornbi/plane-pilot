#!/bin/sh
# Screenshot a .prg from a headless VICE run.
#
#   tools/vice_shot.sh <prg> <out.png> [cycles] [keybuf]
#
# Runs <prg> in x64sc under Xvfb with warp on, stops after <cycles> emulated
# cycles and writes the final frame to <out.png>. One second of C64 time is
# about 985,000 cycles and warp makes that a fraction of a second of real time,
# so 40000000 - the default, ~40 s of C64 time - is cheap.
#
# Two builds can then be compared with `cmp a.png b.png`: byte-identical PNGs
# mean pixel-identical frames, which is a stronger check than looking.
#
# [keybuf] types into the *BASIC* keyboard buffer. It is no use for driving the
# simulation, which scans the key matrix directly - patch the source for that
# (docs/emulator.md).
#
# Needs x64sc and the three C64 ROMs. On a headless machine it also needs
# xvfb-run; where there is a display it just uses it. See docs/emulator.md.
set -eu

if [ $# -lt 2 ]; then
  sed -n '2,20p' "$0" >&2
  exit 2
fi

PRG="$1"
OUT="$2"
CYCLES="${3:-40000000}"
KEYS="${4:-}"

[ -f "$PRG" ] || { echo "vice_shot: no such file: $PRG" >&2; exit 1; }
command -v x64sc >/dev/null || { echo "vice_shot: x64sc not on PATH" >&2; exit 1; }

# Headless: wrap in a virtual X server. With a display (or on macOS) run direct.
if command -v xvfb-run >/dev/null 2>&1 && [ -z "${DISPLAY:-}" ]; then
  RUN="xvfb-run -a"
else
  RUN=""
fi

# -autostartprgmode 1 injects the PRG straight into RAM. The default mode wraps
# it in a disk image first, which fails on a machine with no 1541 ROM and leaves
# you staring at a black screenshot with no error.
ARGS="-default -warp -autostartprgmode 1 -jamaction 1"
ARGS="$ARGS -autostart $PRG -limitcycles $CYCLES -exitscreenshot $OUT"
[ -n "$KEYS" ] && ARGS="$ARGS -keybuf $KEYS"

rm -f "$OUT"
$RUN x64sc $ARGS >/dev/null 2>&1 || true

if [ -f "$OUT" ]; then
  echo "wrote $OUT"
else
  echo "vice_shot: no screenshot produced." >&2
  echo "  Check the ROMs are installed and that $CYCLES cycles is enough." >&2
  exit 1
fi
