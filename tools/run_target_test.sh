#!/bin/sh
# Run c64o/test/target_test.prg on an emulated 6510 and report its verdict.
#
#   tools/run_target_test.sh <target_test.prg>
#
# The suite has no output: it writes its results into globals and spins, and
# this reads them back with vice_dump.sh. That is the only channel there is -
# VICE has no stdout from the guest, and reading text off a screenshot would be
# worse than reading two integers out of RAM. docs/emulator.md has the method.
#
# Skips, rather than fails, when x64sc is missing. The suite exists to catch a
# class of bug the host build cannot see, and a machine without VICE should
# still be able to run the other nine suites.
set -eu

if [ $# -lt 1 ]; then
  sed -n '2,16p' "$0" >&2
  exit 2
fi

PRG="$1"
HERE=$(dirname "$0")

# A stock macOS VICE install puts the binaries inside the app bundle directory
# and does not touch the PATH, so look there before giving up. On Linux the
# package installs x64sc normally and this expands to nothing.
if ! command -v x64sc >/dev/null 2>&1; then
  for d in /Applications/vice-*/bin; do
    if [ -x "$d/x64sc" ]; then
      PATH="$d:$PATH"
      export PATH
      break
    fi
  done
fi

if ! command -v x64sc >/dev/null 2>&1; then
  echo "target_test: x64sc not found - skipped (the 16-bit cases were not run)"
  exit 0
fi

[ -f "$PRG" ] || { echo "target_test: no such file: $PRG" >&2; exit 1; }

# vice_dump.sh prints "  le16 = N" for a two-byte read; that is the whole
# protocol. One emulator run per symbol, which is why only two are read on the
# passing path.
read_u16() {
  "$HERE/vice_dump.sh" "$PRG" @spin "$1" 2 "/tmp/target_test_$1.bin" 40000000 2>/dev/null |
    sed -n 's/.*le16 = \([0-9]*\).*/\1/p'
}

# Signed, for the diagnosis line: the values that differ are usually negative
# on the target and positive on the host, which is the whole point.
as_signed() {
  if [ "$1" -gt 32767 ]; then echo $(( $1 - 65536 )); else echo "$1"; fi
}

CASES=$(read_u16 g_cases)
FAILURES=$(read_u16 g_failures)

if [ -z "$CASES" ] || [ -z "$FAILURES" ]; then
  echo "target_test: could not read the result globals out of $PRG" >&2
  echo "  is it built with -g, and are the globals still volatile?" >&2
  exit 1
fi

if [ "$FAILURES" = "0" ]; then
  echo "target_test: $CASES cases on an emulated 6510, 0 failures"
  exit 0
fi

ID=$(read_u16 g_first_fail)
GOT=$(as_signed "$(read_u16 g_first_got)")
WANT=$(as_signed "$(read_u16 g_first_want)")

echo "target_test: $FAILURES of $CASES cases FAILED on a 6510" >&2
echo "  first failure: case $ID, got $GOT, expected $WANT" >&2
echo "  the case table is in the header of c64o/test/target_test.cc" >&2
exit 1
