#!/bin/sh
# Run the Plane Pilot cycle + RAM + visual-hash evaluation harness.
#
#   tools/bench_eval.sh --init-baseline      # record initial anchor + latest baseline
#   tools/bench_eval.sh --compare            # evaluate working tree against baseline
#   tools/bench_eval.sh --update-latest      # update latest accepted state (keeps anchor)
#   tools/bench_eval.sh --quick --compare    # skip Python generator tests during C64 loop
#
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
exec "${PYTHON:-python3}" "$HERE/bench_eval.py" "$@"
