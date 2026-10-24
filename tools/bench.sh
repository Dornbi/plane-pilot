#!/bin/sh
# Time fpilot's work per frame in seven scenes, on the emulator.
#
#   tools/bench.sh                 # every scene
#   tools/bench.sh runway town     # just those
#
# Prints one line per scene, then their total:
#
#   start     119463
#   ...
#   total     612345
#
# Each number is the average cycles per frame of a frame's work:
# bm_model_start() through bm_total() in sim_run(), the model step and the
# render, everything but the wait for the flip.
#
# Five scenes are frozen, timed over 16 frames: tools/shot.cc's poses, and
# the start of the first mission (fpilot's 01 Recon, airborne). Two are live,
# timed over the frames in which the opponents are near and redrawn:
# scramble, fpilot's mission 04 with both opponents overtaking the parked
# aeroplane and the guns firing, and shootdown, the same with the first
# opponent hit three times and downed on its pass. They are the only scenes
# in which the opponents' and the rounds' model steps, the opponents' fills
# and the smoke cost anything; frozen, everything stands still and a
# silhouette is filled once.
#
# x64sc is cycle-exact, frozen scenes hold still, and the live ones' models
# step once a frame however long a frame takes, so a given binary gives the
# same numbers on every run; only code changes move them. Raster IRQs and
# sprite DMA that land inside the frame are counted, because they are part of
# what a frame costs.
#
# With BENCH_SHOTS=<dir> it also saves each scene's screen, as the frame
# after the last one timed, to <dir>/bench_<scene>.png: the opponents and
# tracers that tools/make_shots.sh's ppilot pictures do not show.
#
# It builds the same way tools/make_shots.sh does: a scratch copy of c64o/
# with tools/shot.cc and tools/bench.cc added and the loops patched, so the
# tree is never touched. The flags are the fpilot.prg rule's in c64o/Makefile.
# The posed scenes' camera is computed on the host (tools/shot_bake.cc): the
# code that computes it no longer fits beside fpilot.
#
# Needs oscar64 and x64sc. About ten seconds per scene.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)

# Short on purpose: oscar64 aborts on long source paths, see make_shots.sh.
# A fresh directory per run by default, so two runs cannot delete each
# other's build.
BUILD="${BENCH_BUILD_DIR:-$(mktemp -d /tmp/pb.XXXXXX)}"
OSCAR64_INCLUDE="${OSCAR64_INCLUDE:-$REPO/../../oscar64-main/include}"

# name, tools/shot.cc scene number, frames skipped, frames timed, live.
# Shootdown is scramble with the first opponent hit three times on its pass
# and downed (tools/shot.cc scene 10): the smoke and the hit logic.
# Scramble's window is frames 48-175 of the mission: the close pass, from
# just before the first opponent comes into view overhead, overtaking, to
# both well ahead and shrinking. The frames cost most there (a sweep in
# 16-frame windows peaked at 64-160), and the rounds fly throughout.
SCENES='
start    8  40  16 0
approach 3  40  16 0
cruise   4  40  16 0
town     6  40  16 0
lake     7  40  16 0
scramble 9  48 128 1
shootdown 10 48 128 1
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
rm -f "$BUILD/src/polydemo.cc" "$BUILD/src/vecdemo.cc" "$BUILD/src/vectest.cc"
cp "$REPO/tools/shot.cc" "$REPO/tools/shot.h" "$REPO/tools/shot_pose.h" \
  "$REPO/tools/bench.cc" "$REPO/tools/bench.h" "$BUILD/src/"
ln -sfn "$OSCAR64_INCLUDE" "$BUILD/include"

# The keyboard poll redirect is make_shots.sh's, which explains it. Then a
# frame's work in sim_run() gets bench_begin() before bm_model_start() and
# bench_end() after bm_total(). Every patch is checked: a silent miss would
# time the wrong thing, or nothing, and still print a plausible number.
cd "$BUILD/src"
perl -0pi -e 's/(while \(1\) \{\n)    keys_poll\(\);/$1    shot_poll();/' help.cc sim.cc
perl -0pi -e 's/(title_tick\(\);\n\n)    keys_poll\(\);/$1    shot_poll();/' menu.cc
perl -0pi -e 's/^(#include "screen\.h")$/$1\n#include "shot.h"/m' menu.cc sim.cc help.cc
perl -0pi -e 's/^(#include "screen\.h")$/$1\n#include "bench.h"/m' sim.cc
perl -0pi -e 's/^(    )(bm_model_start\(\);)$/$1bench_begin();\n$1$2/m' sim.cc
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
  while read -r name shot skip frames live; do
    [ -n "${name:-}" ] || continue
    if [ -n "$wanted" ]; then
      case " $wanted " in *" $name "*) ;; *) continue ;; esac
    fi
    # Checked by the file and not only the exit code: oscar64 has been seen to
    # leave no .prg behind without saying so.
    flags="-D__SHOT__=$shot -D__BENCH_SKIP__=$skip -D__BENCH_FRAMES__=$frames"
    [ "$live" = 1 ] && flags="$flags -D__BENCH_LIVE__"
    case "$shot" in
      3 | 4 | 6 | 7)
        c++ -D__SHOT__="$shot" -o "$BUILD/bake" "$REPO/tools/shot_bake.cc" &&
          "$BUILD/bake" > "$BUILD/src/shot_baked.h" ||
          { echo "bench: could not bake the pose of $name" >&2; exit 1; }
        flags="$flags -D__SHOT_BAKED__"
        ;;
    esac
    ( cd "$BUILD/src" && oscar64 $CFLAGS $flags -o="$BUILD/$name.prg" ppilot.cc ) >/dev/null 2>&1 &&
      [ -f "$BUILD/$name.prg" ] || { echo "bench: $name does not build" >&2; exit 1; }

    # VICE's autostart occasionally does not take (see make_shots.sh); the
    # breakpoint is then never reached and vice_dump.sh fails. Retried.
    try=1
    png=""
    [ -n "${BENCH_SHOTS:-}" ] && mkdir -p "$BENCH_SHOTS" && png="$BENCH_SHOTS/bench_$name.png"
    while ! out=$(VICE_DUMP_SHOT="$png" "$REPO/tools/vice_dump.sh" "$BUILD/$name.prg" @spin:bench_end \
        g_bench_cycles 4 "$BUILD/$name.dump.bin" 200000000 2>&1); do
      try=$((try + 1))
      [ "$try" -le 3 ] || { echo "bench: $name never reached the end of its frames" >&2; echo "$out" >&2; exit 1; }
    done
    sum=$(echo "$out" | sed -n 's/^ *le32 = //p')
    [ -n "$sum" ] && [ "$sum" -gt 0 ] || { echo "bench: $name read no cycles" >&2; echo "$out" >&2; exit 1; }
    per=$((sum / frames))
    printf '%-9s %7d\n' "$name" "$per"
    total=$((total + per))
  done
  printf '%-9s %7d\n' total "$total"
}

[ -n "${KEEP:-}" ] || rm -rf "$BUILD"
