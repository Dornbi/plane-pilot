#!/bin/sh
# Time fpilot's render work in five frozen scenes, on the emulator.
#
#   tools/bench.sh                 # every scene
#   tools/bench.sh runway town     # just those
#
# Prints one line per scene, then their total:
#
#   runway    129841
#   ...
#   total     612345
#
# Each number is the average cycles per frame of the render work: bm_start()
# through bm_total() in sim_run(), over 16 frames of a frozen scene. The
# scenes are tools/shot.cc's poses, and the runway one is the parked start of
# the first mission. x64sc is cycle-exact and the scene holds still, so a
# given binary gives the same numbers on every run; only code changes move
# them. Raster IRQs and sprite DMA that land inside the render are counted,
# because they are part of what a frame costs.
#
# It builds the same way tools/make_shots.sh does: a scratch copy of c64o/
# with tools/shot.cc and tools/bench.cc added and the loops patched, so the
# tree is never touched. The flags are the fpilot.prg rule's in c64o/Makefile.
#
# Needs oscar64 and x64sc. About ten seconds per scene.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)

# Short on purpose: oscar64 aborts on long source paths, see make_shots.sh.
# A fresh directory per run by default, so two runs cannot delete each
# other's build.
BUILD="${BENCH_BUILD_DIR:-$(mktemp -d /tmp/pb.XXXXXX)}"
OSCAR64_INCLUDE="${OSCAR64_INCLUDE:-$REPO/../../oscar64-main/include}"

# name, tools/shot.cc scene number.
SCENES='
runway   8
approach 3
cruise   4
town     6
lake     7
combat   9
'

command -v oscar64 >/dev/null || { echo "bench: oscar64 not on PATH" >&2; exit 1; }
if ! command -v x64sc >/dev/null; then
  for d in /Applications/vice-*/bin; do
    [ -x "$d/x64sc" ] && PATH="$d:$PATH" && export PATH && break
  done
fi
command -v x64sc >/dev/null || { echo "bench: x64sc not on PATH (see docs/emulator.md)" >&2; exit 1; }
[ -d "$OSCAR64_INCLUDE" ] || { echo "bench: no oscar64 include dir at $OSCAR64_INCLUDE" >&2; exit 1; }

rm -rf "$BUILD"
mkdir -p "$BUILD/src"
cp "$REPO/c64o"/*.cc "$REPO/c64o"/*.h "$REPO/c64o"/*.bin "$REPO/c64o"/*.koa "$BUILD/src/"
rm -f "$BUILD/src/polydemo.cc" "$BUILD/src/vecdemo.cc" "$BUILD/src/vectest.cc" "$BUILD/src/planedemo.cc"
cp "$REPO/tools/shot.cc" "$REPO/tools/shot.h" "$REPO/tools/bench.cc" "$REPO/tools/bench.h" "$BUILD/src/"
ln -sfn "$OSCAR64_INCLUDE" "$BUILD/include"

# The keyboard poll redirect is make_shots.sh's, which explains it. Then the
# render work in sim_run() gets bench_begin() before bm_start() and
# bench_end() after bm_total(). Every patch is checked: a silent miss would
# time the wrong thing, or nothing, and still print a plausible number.
cd "$BUILD/src"
perl -0pi -e 's/(while \(1\) \{\n)    keys_poll\(\);/$1    shot_poll();/' help.cc sim.cc
perl -0pi -e 's/(title_tick\(\);\n\n)    keys_poll\(\);/$1    shot_poll();/' menu.cc
perl -0pi -e 's/^(#include "screen\.h")$/$1\n#include "shot.h"/m' menu.cc sim.cc help.cc
perl -0pi -e 's/^(#include "screen\.h")$/$1\n#include "bench.h"/m' sim.cc
perl -0pi -e 's/^(    )(bm_model_start\(\);)/$1bench_begin();\n$1$2/m' sim.cc
perl -0pi -e 's/^(    bm_total\(990, "TOT:"\);)$/$1\n    bench_end();/m' sim.cc
for f in menu.cc help.cc sim.cc; do
  [ "$(grep -c 'shot_poll();' "$f")" = 1 ] ||
    { echo "bench: could not redirect the keyboard poll in c64o/$f" >&2; exit 1; }
done
for p in 'bench_begin();' 'bench_end();' '#include "bench.h"'; do
  [ "$(grep -cF "$p" sim.cc)" = 1 ] ||
    { echo "bench: could not patch '$p' into c64o/sim.cc" >&2; exit 1; }
done

CFLAGS="-ii=../include -D__OSCAR64__ -g -xz -O2 -Op -Oa -Oi -Oz -Oo"
CFLAGS="$CFLAGS -D__ENABLE_SOUND__ -D__ENABLE_CLOUDS__ -D__FPILOT__ -D__MAX_RAM__"

wanted="${*:-}"
total=0
echo "$SCENES" | {
  while read -r name shot; do
    [ -n "${name:-}" ] || continue
    if [ -n "$wanted" ]; then
      case " $wanted " in *" $name "*) ;; *) continue ;; esac
    fi
    # Checked by the file and not only the exit code: oscar64 has been seen to
    # leave no .prg behind without saying so.
    ( cd "$BUILD/src" && oscar64 $CFLAGS -D__SHOT__="$shot" -o="$BUILD/$name.prg" ppilot.cc ) >/dev/null 2>&1 &&
      [ -f "$BUILD/$name.prg" ] || { echo "bench: $name does not build" >&2; exit 1; }

    # VICE's autostart occasionally does not take (see make_shots.sh); the
    # breakpoint is then never reached and vice_dump.sh fails. Retried.
    try=1
    while ! out=$("$REPO/tools/vice_dump.sh" "$BUILD/$name.prg" @spin:bench_end g_bench_cycles 4 \
        "$BUILD/$name.dump.bin" 100000000 2>&1); do
      try=$((try + 1))
      [ "$try" -le 3 ] || { echo "bench: $name never reached the end of its frames" >&2; echo "$out" >&2; exit 1; }
    done
    sum=$(echo "$out" | sed -n 's/^ *le32 = //p')
    [ -n "$sum" ] && [ "$sum" -gt 0 ] || { echo "bench: $name read no cycles" >&2; echo "$out" >&2; exit 1; }
    per=$((sum / 16))
    printf '%-9s %7d\n' "$name" "$per"
    total=$((total + per))
  done
  printf '%-9s %7d\n' total "$total"
}

[ -n "${KEEP:-}" ] || rm -rf "$BUILD"
