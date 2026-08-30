#!/bin/sh
# Capture the screenshots in screens/ from the current sources.
#
#   tools/make_shots.sh [scene ...]      # default: every scene
#   tools/make_shots.sh menu help        # just those two
#   KEEP=1 tools/make_shots.sh           # leave the scratch build in place
#
# Each scene is one headless VICE run of a purpose-built binary: c64o/ is
# copied to a scratch directory, tools/shot.cc is dropped in beside it, the
# keyboard poll of the three screen loops is redirected at it, and the scene's
# script presses the keys that get the game to the screen being photographed.
# That detour exists because the game scans the key matrix directly and VICE's
# -keybuf cannot reach it; tools/shot.cc has the details.
#
# The emulator is deterministic, so a scene is reproducible: the same sources,
# script and cycle count give a byte-identical PNG. Retuning a scene means
# editing its script in tools/shot.cc, or its capture point in the table below,
# and running this again.
#
# Needs oscar64, x64sc and PIL. About five seconds per scene.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
SCREENS="$REPO/screens"

# oscar64 smashes a stack buffer and dies with an unexplained SIGABRT when the
# path to the sources is long - it builds an absolute path in a fixed buffer
# while parsing #pragma compile(). A scratch directory under the system temp
# dir is far enough down that road to trip it on macOS, so this is short by
# construction and not merely by taste. Override for a different disk, but keep
# it short.
BUILD="${SHOT_BUILD_DIR:-/tmp/ppilot-shots}"

# Same include tree the c64o Makefile compiles against, resolved from the repo
# rather than from c64o/.
OSCAR64_INCLUDE="${OSCAR64_INCLUDE:-$REPO/../../oscar64-main/include}"

# The scenes. Each line is: name, the -D__SHOT__ number of its script in
# tools/shot.cc, the emulated cycle count at which the frame is grabbed, and
# the file it lands in. One second of C64 time is 985,248 cycles.
#
# The capture point only has to fall inside the stretch of the script that
# holds the screen being photographed - every scene either freezes the model or
# ends on a modal screen, so there is a wide window and these numbers are not
# delicate. The exception is the menu, where the title flyby is still moving.
SCENES='
menu   1   18000000  screen04_crt.png
help   2   10000000  screen03_crt.png
final  3   14000000  screen01_crt.png
turn   4   17000000  screen02_crt.png
map    5  110000000  screen05_crt.png
debug  6   16000000  debug_crt.png
side   7   14000000  screen06_crt.png
'

# Cropping the border and building the tube - scanlines, bleed, bloom - is
# tools/shot_crt.py. VICE writes its screenshots from the native frame buffer
# before the video chain runs, so -VICIIfilter changes what is on the emulator
# window and nothing at all in the file; the CRT look has to be made after.

command -v oscar64 >/dev/null || { echo "make_shots: oscar64 not on PATH" >&2; exit 1; }
if ! command -v x64sc >/dev/null; then
  for d in /Applications/vice-*/bin; do
    [ -x "$d/x64sc" ] && PATH="$d:$PATH" && export PATH && break
  done
fi
command -v x64sc >/dev/null || { echo "make_shots: x64sc not on PATH (see docs/emulator.md)" >&2; exit 1; }
[ -d "$OSCAR64_INCLUDE" ] || { echo "make_shots: no oscar64 include dir at $OSCAR64_INCLUDE" >&2; exit 1; }

# --- The scratch build tree ------------------------------------------------

rm -rf "$BUILD"
mkdir -p "$BUILD/src"
cp "$REPO/c64o"/*.cc "$REPO/c64o"/*.h "$REPO/c64o"/*.bin "$REPO/c64o"/*.koa "$BUILD/src/"
rm -f "$BUILD/src/polydemo.cc" "$BUILD/src/vecdemo.cc" "$BUILD/src/vectest.cc"
cp "$REPO/tools/shot.cc" "$REPO/tools/shot.h" "$BUILD/src/"
# Symlinked rather than passed as an absolute path, for the same reason BUILD is
# short: what oscar64 has to hold is the path it resolves, not the one typed.
ln -sfn "$OSCAR64_INCLUDE" "$BUILD/include"

# Redirect the keyboard poll of the three screen loops - the menu, the help
# screen and the flight loop - at the scripted one. Each is the poll at the top
# of a loop that owns a screen; the other keyb_poll() calls in these files are
# inside keys_wait_release() and its inline twin in help.cc, which have to keep
# reading the real matrix so a one-poll scripted press releases itself.
#
# Anchored on the surrounding code rather than on line numbers, and checked
# below: a silent miss here would produce a screenshot of the menu for every
# scene, which is exactly the kind of wrong that looks plausible.
cd "$BUILD/src"
perl -0pi -e 's/(while \(1\) \{\n)    keyb_poll\(\);/$1    shot_poll();/' help.cc sim.cc
perl -0pi -e 's/(title_tick\(\);\n\n)    keyb_poll\(\);/$1    shot_poll();/' menu.cc
perl -0pi -e 's/^(#include "screen\.h")$/$1\n#include "shot.h"/m' menu.cc sim.cc help.cc
for f in menu.cc help.cc sim.cc; do
  [ "$(grep -c 'shot_poll();' "$f")" = 1 ] ||
    { echo "make_shots: could not redirect the keyboard poll in c64o/$f" >&2; exit 1; }
  [ "$(grep -c '#include "shot.h"' "$f")" = 1 ] ||
    { echo "make_shots: could not add the shot.h include to c64o/$f" >&2; exit 1; }
done

# --- The scenes ------------------------------------------------------------

# Same flags as the ppilot.prg rule in c64o/Makefile: a scene has to be a
# picture of the binary people actually run.
CFLAGS="-ii=../include -D__OSCAR64__ -g -xz -O2 -Op -Oa -Oi -Oz -Oo"
CFLAGS="$CFLAGS -D__ENABLE_SOUND__ -D__ENABLE_DEBUG__ -D__ENABLE_CLOUDS__ -D__MAX_RAM__"

wanted="${*:-}"
echo "$SCENES" | while read -r name shot cycles out; do
  [ -n "${name:-}" ] || continue
  if [ -n "$wanted" ]; then
    case " $wanted " in *" $name "*) ;; *) continue ;; esac
  fi

  ( cd "$BUILD/src" && oscar64 $CFLAGS -D__SHOT__="$shot" -o="$BUILD/$name.prg" ppilot.cc ) >/dev/null

  # VICE's autostart occasionally does not take, and what lands in the file
  # then is the BASIC prompt - a perfectly valid PNG of the wrong thing, which
  # is why this is checked rather than trusted. Every screen in the game draws
  # a black border and the BASIC screen draws a light blue one, so the corner
  # pixel tells them apart. Retried rather than failed: it is a race in the
  # emulator's startup, and it has taken on the next run every time so far.
  try=1
  while : ; do
    "$REPO/tools/vice_shot.sh" "$BUILD/$name.prg" "$BUILD/$name.raw.png" "$cycles" >/dev/null
    started=$(python3 -c 'import sys; from PIL import Image; print(max(Image.open(sys.argv[1]).convert("RGB").getpixel((0, 0))) < 64)' "$BUILD/$name.raw.png")
    [ "$started" = "True" ] && break
    try=$((try + 1))
    if [ "$try" -gt 3 ]; then
      echo "make_shots: $name never got past the BASIC prompt - autostart failed three times" >&2
      exit 1
    fi
  done

  python3 "$REPO/tools/shot_crt.py" "$BUILD/$name.raw.png" "$SCREENS/$out"
  echo "  $out  (scene $name, $cycles cycles)"
done

[ -n "${KEEP:-}" ] || rm -rf "$BUILD"
