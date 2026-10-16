#!/bin/bash
# Unattended loop that frees fpilot RAM one reviewed change at a time.
#
#   tools/ram_loop.sh [max_passes]        # default 40
#
# Each pass:
#   1. A fresh `claude -p` worker reads docs/ram-loop.md (rules, dead ends,
#      log) and implements ONE new saving. It does not commit.
#   2. This script rebuilds everything from clean, runs every test, and
#      measures fpilot's "Free, allocatable". No gain, or any failure, means
#      the change is reverted.
#   3. It runs tools/make_shots.sh, so that screens/ shows any visual change.
#   4. A fresh read-only reviewer checks the diff and the changed screens
#      against the rules and answers ACCEPT or REJECT.
#   5. An accepted change is committed. Either way a row goes into the log
#      in docs/ram-loop.md, so that later passes do not repeat the idea.
#
# It stops after MAX_FAILS passes in a row with nothing kept, when the worker
# reports NO_MORE_IDEAS, or after max_passes. All work goes on branch $BRANCH,
# which is created from the current HEAD if it does not exist. Nothing is
# pushed.
#
# Environment knobs:
#   BRANCH=ram-loop  MAX_FAILS=4  REVIEW=1  SHOTS=1
#   MODEL=<model>    BUDGET=<usd per agent call>  PERM=acceptEdits
#   CLAUDE=<path to the claude CLI>  RUN_DIR=/tmp/ram-loop (logs, rejected diffs)
set -u

REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO"

LEDGER=docs/ram-loop.md
MAX_PASSES=${1:-40}
BRANCH=${BRANCH:-ram-loop}
MAX_FAILS=${MAX_FAILS:-4}
REVIEW=${REVIEW:-1}
SHOTS=${SHOTS:-1}
PERM=${PERM:-acceptEdits}
# Short on purpose: oscar64 aborts on long paths (see tools/make_shots.sh).
RUN_DIR=${RUN_DIR:-/tmp/ram-loop}
export SHOT_BUILD_DIR=${SHOT_BUILD_DIR:-/tmp/ram-loop-shots}

if [ -z "${CLAUDE:-}" ]; then
  CLAUDE=$(command -v claude || true)
  if [ -z "$CLAUDE" ]; then
    # The desktop app's bundled CLI, newest first.
    CLAUDE=$(ls -td "$HOME"/Library/Application\ Support/Claude/claude-code/*/*/claude.app/Contents/MacOS/claude 2>/dev/null | head -1)
  fi
fi
[ -x "$CLAUDE" ] || { echo "ram_loop: claude CLI not found; set CLAUDE=" >&2; exit 1; }

EXTRA=()
[ -n "${MODEL:-}" ] && EXTRA+=(--model "$MODEL")
[ -n "${BUDGET:-}" ] && EXTRA+=(--max-budget-usd "$BUDGET")

mkdir -p "$RUN_DIR"

die() { echo "ram_loop: $*" >&2; exit 1; }
say() { echo "[$(date +%H:%M:%S)] $*"; }

# --- Preconditions ---------------------------------------------------------

[ -z "$(git status --porcelain)" ] || die "working tree not clean; commit or stash first"
if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
  git switch -q "$BRANCH" || die "cannot switch to $BRANCH"
else
  git switch -q -c "$BRANCH" || die "cannot create $BRANCH"
fi

# --- Measurement -----------------------------------------------------------

free_of() {  # free_of ppilot|fpilot -> bytes
  python3 tools/analyze_ram.py "c64o/$1.map" | awk '/Free, allocatable/ { gsub(",", "", $3); print $3 }'
}

build_and_test() {  # build_and_test <log>
  { make -C c64o clean && make -C c64o all && make test; } > "$1" 2>&1
}

# Pass, idea, fpilot delta, ppilot delta, result.
log_row() {
  local idea=${2//|//}
  local result=${5//|//}
  printf '| %s | %s | %s | %s | %s | %s |\n' "$1" "$(date +%F)" "$3" "$4" "$result" "$idea" >> "$LEDGER"
}

say "baseline build on $BRANCH"
build_and_test "$RUN_DIR/baseline.log" || die "baseline build/tests fail; see $RUN_DIR/baseline.log"
# The last kept state. The .map files can't be trusted for this: after a
# rejected pass they describe the rejected build until the next rebuild.
good_f=$(free_of fpilot); good_p=$(free_of ppilot)

# --- Prompts ---------------------------------------------------------------

worker_prompt() {
  cat <<EOF
You are one pass of an unattended loop that frees RAM in fpilot (C64, oscar64).

Read $LEDGER first. Its rules are binding. Its dead ends and log list ideas that were
already tried: never retry one, or a trivial variant of one. Your auto-memory notes about
fpilot RAM, inlining and oscar64 bugs are background.

Current state: fpilot $1 B free allocatable, ppilot $2 B. The tree is clean on branch $BRANCH.

Your task: find ONE new saving that raises fpilot's "Free, allocatable", and implement it.
- Measure with: make -C c64o all && make fram   (also make pram for ppilot)
- Before you finish, make sure that make -C c64o all and make test pass.
- If the change could affect rendering, look at the result with tools/make_shots.sh
  (SHOT_BUILD_DIR is already set to a short path).
- If the idea does not pan out, revert your own edits by hand. Then either try one other
  idea, or report the dead end.
- Do NOT commit, switch branch, edit $LEDGER, or write auto-memory notes. The loop records
  everything.

When done, write $RUN_DIR/summary.txt in exactly this form:
IDEA: <one line: what you changed and the measured byte gain, or why it was a dead end>
NOTES: <optional extra lines: speed impact, test changes and why, visual changes>
If, after a real search, you believe no worthwhile saving is left within the rules, write
just the line NO_MORE_IDEAS: <why> instead.
EOF
}

reviewer_prompt() {
  cat <<EOF
You are the independent reviewer in an unattended RAM-optimisation loop for fpilot (C64).
Read the rules in $LEDGER. A worker made an uncommitted change. Inspect it with
git status and git diff HEAD. The worker's summary is in $RUN_DIR/summary.txt.

It already builds, passes every test, and raises fpilot free RAM from $1 to $2 B
(ppilot $3 -> $4 B).

Screenshots that changed in screens/ (new files in the tree, old copies in $RUN_DIR/old/):
$5

Judge only against the rules:
- Rendering: is it the same, apart from allowed minor dot or polygon differences? Look at
  every changed screenshot pair with Read.
- Flight model: does it behave similarly (loops, climb, turn, takeoff, landing)?
  Scrutinise any change to test expectations.
- Frame rate: is there no noticeable slowdown in the hot paths?
- Every feature kept, the change small and readable, and docs/comments kept accurate?

Do not edit anything. End your answer with exactly one line:
VERDICT: ACCEPT
or
VERDICT: REJECT: <short reason>
EOF
}

WORKER_TOOLS=(Read Edit Write Grep Glob
  "Bash(make:*)" "Bash(python3:*)" "Bash(oscar64:*)"
  "Bash(tools/make_shots.sh:*)" "Bash(tools/vice_dump.sh:*)" "Bash(tools/vice_shot.sh:*)"
  "Bash(git diff:*)" "Bash(git status:*)" "Bash(git log:*)" "Bash(git show:*)" "Bash(git archive:*)"
  "Bash(ls:*)" "Bash(cat:*)" "Bash(head:*)" "Bash(tail:*)" "Bash(wc:*)" "Bash(grep:*)"
  "Bash(sed -n:*)" "Bash(cmp:*)" "Bash(diff:*)" "Bash(cp:*)" "Bash(mkdir:*)" "Bash(tar:*)")
DENY_TOOLS=("Bash(git commit:*)" "Bash(git push:*)" "Bash(git reset:*)" "Bash(git checkout:*)"
  "Bash(git switch:*)" "Bash(git stash:*)" "Bash(git clean:*)" "Bash(rm -rf:*)"
  "Edit($LEDGER)" "Write($LEDGER)")
REVIEW_TOOLS=(Read Grep Glob "Bash(git diff:*)" "Bash(git status:*)" "Bash(git log:*)" "Bash(git show:*)")

# --- The loop --------------------------------------------------------------

fails=0
pass=$(grep -cE '^\| *[0-9]+ \|' "$LEDGER")
for ((n = 1; n <= MAX_PASSES && fails < MAX_FAILS; n++)); do
  pass=$((pass + 1))
  head=$(git rev-parse HEAD)
  f0=$good_f; p0=$good_p
  say "pass $pass: fpilot $f0 B, ppilot $p0 B free (fails in a row: $fails)"

  rm -f "$RUN_DIR/summary.txt"; rm -rf "$RUN_DIR/old"
  "$CLAUDE" -p "$(worker_prompt "$f0" "$p0")" ${EXTRA[@]+"${EXTRA[@]}"} \
    --permission-mode "$PERM" --allowedTools "${WORKER_TOOLS[@]}" --disallowedTools "${DENY_TOOLS[@]}" \
    > "$RUN_DIR/pass$pass.worker.log" 2>&1

  if grep -q '^NO_MORE_IDEAS' "$RUN_DIR/summary.txt" 2>/dev/null; then
    why=$(sed -n 's/^NO_MORE_IDEAS:* *//p' "$RUN_DIR/summary.txt" | head -1)
    git reset -q --hard "$head"; git clean -fdq
    log_row "$pass" "${why:-no reason given}" 0 0 "stop: no more ideas"
    git commit -qm "RAM loop: pass $pass found no more ideas" -- "$LEDGER"
    say "worker reports no more ideas: $why"
    break
  fi
  idea=$(sed -n 's/^IDEA: *//p' "$RUN_DIR/summary.txt" 2>/dev/null | head -1)
  idea=${idea:-"(no summary written)"}

  result=""; f1=$f0; p1=$p0
  if [ -z "$(git status --porcelain)" ]; then
    result="dead end"
  elif ! build_and_test "$RUN_DIR/pass$pass.build.log"; then
    result="rejected: build or tests fail"
  else
    f1=$(free_of fpilot); p1=$(free_of ppilot)
    if [ -z "$f1" ] || [ "$f1" -le "$f0" ]; then
      result="rejected: no fpilot gain"
    fi
  fi

  if [ -z "$result" ] && [ "$SHOTS" = 1 ]; then
    if ! tools/make_shots.sh > "$RUN_DIR/pass$pass.shots.log" 2>&1; then
      result="rejected: make_shots fails"
    fi
  fi

  if [ -z "$result" ] && [ "$REVIEW" = 1 ]; then
    changed=$(git status --porcelain -- screens | awk '{ print $2 }')
    mkdir -p "$RUN_DIR/old"
    for f in $changed; do git show "HEAD:$f" > "$RUN_DIR/old/$(basename "$f")" 2>/dev/null; done
    "$CLAUDE" -p "$(reviewer_prompt "$f0" "$f1" "$p0" "$p1" "${changed:-none (pixel-identical)}")" ${EXTRA[@]+"${EXTRA[@]}"} \
      --permission-mode default --allowedTools "${REVIEW_TOOLS[@]}" --disallowedTools Edit Write \
      > "$RUN_DIR/pass$pass.review.log" 2>&1
    verdict=$(grep -E '^VERDICT:' "$RUN_DIR/pass$pass.review.log" | tail -1)
    case "$verdict" in
      "VERDICT: ACCEPT"*) ;;
      "VERDICT: REJECT"*) result="rejected by review: ${verdict#VERDICT: REJECT: }" ;;
      *) result="rejected: reviewer gave no verdict" ;;
    esac
  fi

  df=$((f1 - f0)); dp=$((p1 - p0))
  if [ -z "$result" ]; then
    log_row "$pass" "$idea" "+$df" "$dp" "kept"
    git add -A
    git commit -qm "$(printf 'RAM loop: %s\n\nfpilot free %s -> %s B, ppilot %s -> %s B.\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>' \
      "$idea" "$f0" "$f1" "$p0" "$p1")"
    good_f=$f1; good_p=$p1
    fails=0
    say "kept: +$df B fpilot ($idea)"
  else
    git diff HEAD > "$RUN_DIR/pass$pass.rejected.diff"
    git reset -q --hard "$head"; git clean -fdq
    log_row "$pass" "$idea" "$df" "$dp" "$result"
    git commit -qm "RAM loop: log pass $pass ($result)" -- "$LEDGER"
    fails=$((fails + 1))
    say "$result ($idea)"
    # Rebuild, so the next worker starts from a build that matches the source.
    build_and_test "$RUN_DIR/pass$pass.restore.log" || die "HEAD no longer builds after revert; see $RUN_DIR/pass$pass.restore.log"
  fi
done

# Leave the tree built, so that make fram/pram report the final state.
build_and_test "$RUN_DIR/final.log" || say "final rebuild failed; see $RUN_DIR/final.log"
say "done on $BRANCH: fpilot $(free_of fpilot) B, ppilot $(free_of ppilot) B free; logs in $RUN_DIR"
