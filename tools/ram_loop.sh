#!/bin/bash
# Unattended loop that frees fpilot RAM, or saves cycles, one reviewed change
# at a time.
#
#   tools/ram_loop.sh [max_passes]        # default 40
#
# Each pass:
#   1. A fresh `claude -p` worker reads docs/ram-loop.md (rules, dead ends,
#      log) and implements ONE new saving of RAM or cycles. It does not
#      commit.
#   2. This script rebuilds everything from clean, runs every test, measures
#      fpilot's "Free, allocatable" and times five frozen scenes with
#      tools/bench.sh. The change is reverted on any failure, if free RAM
#      drops, if any scene is more than 2% slower than the cycle baseline in
#      docs/ram-loop.md, or if it gains neither RAM nor at least 0.5% of total
#      cycles.
#   3. It runs tools/make_shots.sh and compares the shots with the ones from
#      the last kept state. screens/ itself is always restored: the loop never
#      commits screenshots.
#   4. A fresh read-only reviewer checks the diff and the changed screens
#      against the rules and answers ACCEPT or REJECT.
#   5. An accepted change is committed. Either way a row goes into the log
#      in docs/ram-loop.md, so that later passes do not repeat the idea.
#
# It stops after MAX_FAILS passes in a row with nothing kept, when the worker
# reports NO_MORE_IDEAS, when an agent call fails or hits the account's usage
# limit, or after max_passes. All work goes on branch $BRANCH,
# which is created from the current HEAD if it does not exist. Nothing is
# pushed.
#
# Environment knobs:
#   BRANCH=claude-loop  MAX_FAILS=4  REVIEW=1  SHOTS=1
#   MODEL=<model>    BUDGET=<usd per agent call>  PERM=acceptEdits
#   CLAUDE=<path to the claude CLI>  RUN_DIR=/tmp/ram-loop (logs, rejected diffs)
set -u

REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO"

LEDGER=docs/ram-loop.md
MAX_PASSES=${1:-40}
BRANCH=${BRANCH:-claude-loop}
MAX_FAILS=${MAX_FAILS:-4}
REVIEW=${REVIEW:-1}
SHOTS=${SHOTS:-1}
PERM=${PERM:-acceptEdits}
# Short on purpose: oscar64 aborts on long paths (see tools/make_shots.sh).
RUN_DIR=${RUN_DIR:-/tmp/ram-loop}
export SHOT_BUILD_DIR=${SHOT_BUILD_DIR:-/tmp/ram-loop-shots}
# Each scene may be this many percent slower than the baseline, and a change
# that saves no RAM has to save at least SPEED_GAIN_PERMILLE of total cycles.
# The bench is deterministic, so this threshold is about whether a change is
# worth having, not about noise.
CYCLE_SLACK_PERCENT=2
SPEED_GAIN_PERMILLE=5

if [ -z "${CLAUDE:-}" ]; then
  CLAUDE=$(command -v claude || true)
  if [ -z "$CLAUDE" ]; then
    # The desktop app's bundled CLI, newest first.
    CLAUDE=$(ls -td "$HOME"/Library/Application\ Support/Claude/claude-code/*/*/claude.app/Contents/MacOS/claude 2>/dev/null | head -1)
  fi
fi
[ -x "$CLAUDE" ] || { echo "ram_loop: claude CLI not found; set CLAUDE=" >&2; exit 1; }
# The desktop app's sessions are signed in through the app, but the CLI on its
# own is not, so check before spending a baseline build on it.
"$CLAUDE" auth status 2>/dev/null | grep -q '"loggedIn": *true' ||
  { echo "ram_loop: the claude CLI is not logged in; run: \"$CLAUDE\" auth login" >&2; exit 1; }

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

bench() {  # bench <out>: tools/bench.sh's "scene cycles" lines
  # Once more on failure: the numbers are deterministic, but a run has been
  # seen to fail once in VICE or oscar64 and pass when repeated.
  tools/bench.sh > "$1" 2>&1 || tools/bench.sh > "$1" 2>&1
}

cycles_of() {  # cycles_of <bench out> <scene|total>
  awk -v s="$2" '$1 == s { print $2 }' "$1"
}

# The fixed reference for the 2% budget, so that small slowdowns cannot add up
# pass by pass. One "cycles-baseline: scene=N ..." line in the ledger.
BASELINE=$(sed -n 's/^cycles-baseline: *//p' "$LEDGER" | head -1)
[ -n "$BASELINE" ] || die "no cycles-baseline line in $LEDGER"

# Prints why the bench output is over budget, or nothing when it is within it.
over_budget() {  # over_budget <bench out>
  local kv scene base now
  for kv in $BASELINE; do
    scene=${kv%%=*}; base=${kv#*=}
    now=$(cycles_of "$1" "$scene")
    if [ -z "$now" ]; then
      echo "no $scene timing"; return
    fi
    if [ $((now * 100)) -gt $((base * (100 + CYCLE_SLACK_PERCENT))) ]; then
      echo "$scene $now cycles, over $base +$CYCLE_SLACK_PERCENT%"; return
    fi
  done
}

# Whether an agent's log reads like the account ran out of usage rather than
# like an answer. Checked only when the agent left no result behind, so a
# worker that merely writes about limits in its analysis cannot trip it.
limit_hit() {  # limit_hit <log>
  grep -qiE 'usage limit|limit reached|hit your limit|out of extra usage|credit balance is too low|rate[ _-]limit' "$1"
}

pct() {  # pct <new> <old>: signed change in percent, one decimal
  awk -v n="$1" -v o="$2" 'BEGIN { printf "%+.1f%%", (n - o) * 100 / o }'
}

# Pass, idea, fpilot delta, ppilot delta, cycles change, result.
log_row() {
  local idea=${2//|//}
  local result=${6//|//}
  printf '| %s | %s | %s | %s | %s | %s | %s |\n' "$1" "$(date +%F)" "$3" "$4" "$5" "$result" "$idea" >> "$LEDGER"
}

say "baseline build on $BRANCH"
build_and_test "$RUN_DIR/baseline.log" || die "baseline build/tests fail; see $RUN_DIR/baseline.log"
# The last kept state. The .map files can't be trusted for this: after a
# rejected pass they describe the rejected build until the next rebuild.
good_f=$(free_of fpilot); good_p=$(free_of ppilot)
bench "$RUN_DIR/baseline.bench" || die "baseline bench fails; see $RUN_DIR/baseline.bench"
[ -z "$(over_budget "$RUN_DIR/baseline.bench")" ] ||
  die "HEAD is already over the cycle budget: $(over_budget "$RUN_DIR/baseline.bench")"
good_c=$(cycles_of "$RUN_DIR/baseline.bench" total)
rm -f "$RUN_DIR/current.bench"

# Screenshots go to $RUN_DIR, never into a commit. ref/ is the last kept
# state's set, shots/ the current pass's.
shots() {  # shots <dir> <log>
  rm -rf "$1"; mkdir -p "$1"
  tools/make_shots.sh > "$2" 2>&1
  local rc=$?
  cp screens/*.png "$1/"
  git checkout -q -- screens
  return $rc
}
if [ "$SHOTS" = 1 ]; then
  shots "$RUN_DIR/ref" "$RUN_DIR/baseline.shots.log" || die "baseline make_shots fails; see $RUN_DIR/baseline.shots.log"
fi

# --- Prompts ---------------------------------------------------------------

worker_prompt() {
  cat <<EOF
You are one pass of an unattended loop that frees RAM and saves cycles in fpilot
(C64, oscar64). RAM and cycles matter equally.

Read $LEDGER first. Its rules are binding. Its dead ends and log list ideas that were
already tried: never retry one, or a trivial variant of one. Your auto-memory notes about
fpilot RAM, inlining and oscar64 bugs are background.

Current state: fpilot $1 B free allocatable, ppilot $2 B. The tree is clean on branch $BRANCH.
Render cycles per frame now (tools/bench.sh):
$3
The fixed budget: every scene must stay within +$CYCLE_SLACK_PERCENT% of: $BASELINE

Your task: find ONE new saving and implement it. It must either raise fpilot's
"Free, allocatable" while staying inside the cycle budget, or cut total cycles by at
least 0.$SPEED_GAIN_PERMILLE% without losing any free RAM. A change that does both is best.
Prefer changes that are cycle-neutral or faster.
- Measure RAM with: make -C c64o all && make fram   (also make pram for ppilot)
- Measure cycles with: tools/bench.sh   (about 20 s)
- Before you finish, make sure that make -C c64o all and make test pass.
- If the change could affect rendering, look at the result with tools/make_shots.sh
  (SHOT_BUILD_DIR is already set to a short path). It overwrites screens/; the loop
  restores that afterwards. The menu and debug shots differ from run to run anyway.
- If the idea does not pan out, revert your own edits by hand. Then either try one other
  idea, or report the dead end.
- Do NOT commit, switch branch, edit $LEDGER, or write auto-memory notes. The loop records
  everything.

When done, write $RUN_DIR/summary.txt in exactly this form:
TITLE: <commit subject, at most 60 characters, imperative, ending in a full stop, e.g.
       "Compute the music volume mix instead of a 48 B table.">
IDEA: <one line: what you changed and the measured byte and cycle change, or why it was a dead end>
NOTES: <optional extra lines: test changes and why, visual changes>
If, after a real search, you believe no worthwhile saving is left within the rules, write
just the line NO_MORE_IDEAS: <why> instead.
EOF
}

reviewer_prompt() {
  cat <<EOF
You are the independent reviewer in an unattended RAM and cycle optimisation loop for
fpilot (C64).
Read the rules in $LEDGER. A worker made an uncommitted change. Inspect it with
git status and git diff HEAD. The worker's summary is in $RUN_DIR/summary.txt.

It already builds and passes every test. fpilot free RAM goes from $1 to $2 B
(ppilot $3 -> $4 B). Render cycles per frame, before and after (tools/bench.sh):
$6
These are within the cycle budget, which the loop has already checked.

Screenshots that changed (before in $RUN_DIR/ref/, after in $RUN_DIR/shots/, same names):
$5

Judge only against the rules:
- Rendering: is it the same, apart from allowed minor dot or polygon differences? Look at
  every changed screenshot pair with Read.
- Flight model: does it behave similarly (loops, climb, turn, takeoff, landing)?
  Scrutinise any change to test expectations.
- Every feature kept, the change small and readable, and docs/comments kept accurate?

Do not edit anything. End your answer with exactly one line:
VERDICT: ACCEPT
or
VERDICT: REJECT: <short reason>
EOF
}

WORKER_TOOLS=(Read Edit Write Grep Glob
  "Bash(make:*)" "Bash(python3:*)" "Bash(oscar64:*)"
  "Bash(tools/make_shots.sh:*)" "Bash(tools/bench.sh:*)" "Bash(tools/vice_dump.sh:*)" "Bash(tools/vice_shot.sh:*)"
  "Bash(git diff:*)" "Bash(git status:*)" "Bash(git log:*)" "Bash(git show:*)" "Bash(git archive:*)"
  "Bash(ls:*)" "Bash(cat:*)" "Bash(head:*)" "Bash(tail:*)" "Bash(wc:*)" "Bash(grep:*)"
  "Bash(sed -n:*)" "Bash(cmp:*)" "Bash(diff:*)" "Bash(cp:*)" "Bash(mkdir:*)" "Bash(tar:*)")
DENY_TOOLS=("Bash(git commit:*)" "Bash(git push:*)" "Bash(git reset:*)" "Bash(git checkout:*)"
  "Bash(git switch:*)" "Bash(git stash:*)" "Bash(git clean:*)" "Bash(rm -rf:*)"
  "Edit($LEDGER)")
REVIEW_TOOLS=(Read Grep Glob "Bash(git diff:*)" "Bash(git status:*)" "Bash(git log:*)" "Bash(git show:*)")

# --- The loop --------------------------------------------------------------

fails=0
pass=$(grep -cE '^\| *[0-9]+ \|' "$LEDGER")
for ((n = 1; n <= MAX_PASSES && fails < MAX_FAILS; n++)); do
  pass=$((pass + 1))
  head=$(git rev-parse HEAD)
  f0=$good_f; p0=$good_p; c0=$good_c
  say "pass $pass: fpilot $f0 B, ppilot $p0 B free, $c0 cycles (fails in a row: $fails)"
  cur_bench=$(cat "$RUN_DIR/current.bench" 2>/dev/null || cat "$RUN_DIR/baseline.bench")

  rm -f "$RUN_DIR/summary.txt"
  "$CLAUDE" -p "$(worker_prompt "$f0" "$p0" "$cur_bench")" ${EXTRA[@]+"${EXTRA[@]}"} \
    --add-dir "$RUN_DIR" --permission-mode "$PERM" --allowedTools "${WORKER_TOOLS[@]}" --disallowedTools "${DENY_TOOLS[@]}" \
    > "$RUN_DIR/pass$pass.worker.log" 2>&1
  rc=$?
  # A failed call (signed out, out of budget, API error) says nothing about
  # the idea, so stop rather than log it as a dead end.
  if [ $rc -ne 0 ]; then
    git reset -q --hard "$head"; git clean -fdq
    die "worker call failed (exit $rc); see $RUN_DIR/pass$pass.worker.log"
  fi
  if [ ! -f "$RUN_DIR/summary.txt" ] && limit_hit "$RUN_DIR/pass$pass.worker.log"; then
    git reset -q --hard "$head"; git clean -fdq
    die "usage limit reached during pass $pass; see $RUN_DIR/pass$pass.worker.log"
  fi

  if grep -q '^NO_MORE_IDEAS' "$RUN_DIR/summary.txt" 2>/dev/null; then
    why=$(sed -n 's/^NO_MORE_IDEAS:* *//p' "$RUN_DIR/summary.txt" | head -1)
    git reset -q --hard "$head"; git clean -fdq
    log_row "$pass" "${why:-no reason given}" 0 0 0 "stop: no more ideas"
    git commit -qm "Optimisation loop: pass $pass found no more ideas" -- "$LEDGER"
    say "worker reports no more ideas: $why"
    break
  fi
  # Shots the worker took are not part of the change.
  git checkout -q -- screens
  idea=$(sed -n 's/^IDEA: *//p' "$RUN_DIR/summary.txt" 2>/dev/null | head -1)
  idea=${idea:-"(no summary written)"}
  title=$(sed -n 's/^TITLE: *//p' "$RUN_DIR/summary.txt" 2>/dev/null | head -1 | cut -c1-72)
  [ -n "$title" ] || title=$(printf '%s' "$idea" | cut -c1-60)

  result=""; f1=$f0; p1=$p0; c1=$c0
  pass_bench="$RUN_DIR/pass$pass.bench"
  if [ -z "$(git status --porcelain)" ]; then
    result="dead end"
  elif ! build_and_test "$RUN_DIR/pass$pass.build.log"; then
    result="rejected: build or tests fail"
  elif ! bench "$pass_bench"; then
    result="rejected: bench fails"
  else
    f1=$(free_of fpilot); p1=$(free_of ppilot); c1=$(cycles_of "$pass_bench" total)
    over=$(over_budget "$pass_bench")
    if [ -z "$f1" ] || [ "$f1" -lt "$f0" ]; then
      result="rejected: fpilot loses RAM"
    elif [ -n "$over" ]; then
      result="rejected: $over"
    elif [ "$f1" -eq "$f0" ] && [ $((c1 * 1000)) -gt $((c0 * (1000 - SPEED_GAIN_PERMILLE))) ]; then
      result="rejected: no RAM gain and under 0.$SPEED_GAIN_PERMILLE% faster"
    fi
  fi

  changed=""
  if [ -z "$result" ] && [ "$SHOTS" = 1 ]; then
    if ! shots "$RUN_DIR/shots" "$RUN_DIR/pass$pass.shots.log"; then
      result="rejected: make_shots fails"
    else
      # The menu (title flyby) and debug (cycle counters) shots differ from
      # run to run even on the same source, so they are left out.
      for f in "$RUN_DIR"/shots/*.png; do
        b=$(basename "$f")
        case "$b" in screen04_crt.png | debug_crt.png) continue ;; esac
        cmp -s "$f" "$RUN_DIR/ref/$b" || changed="$changed $b"
      done
    fi
  fi

  if [ -z "$result" ] && [ "$REVIEW" = 1 ]; then
    both=$(paste "$RUN_DIR/current.bench" "$pass_bench" 2>/dev/null || paste "$RUN_DIR/baseline.bench" "$pass_bench")
    "$CLAUDE" -p "$(reviewer_prompt "$f0" "$f1" "$p0" "$p1" "${changed:-none (pixel-identical)}" "$both")" ${EXTRA[@]+"${EXTRA[@]}"} \
      --add-dir "$RUN_DIR" --permission-mode default --allowedTools "${REVIEW_TOOLS[@]}" --disallowedTools Edit Write \
      > "$RUN_DIR/pass$pass.review.log" 2>&1 ||
      { git reset -q --hard "$head"; git clean -fdq; die "reviewer call failed; see $RUN_DIR/pass$pass.review.log"; }
    verdict=$(grep -E '^VERDICT:' "$RUN_DIR/pass$pass.review.log" | tail -1)
    if [ -z "$verdict" ] && limit_hit "$RUN_DIR/pass$pass.review.log"; then
      git reset -q --hard "$head"; git clean -fdq
      die "usage limit reached reviewing pass $pass; see $RUN_DIR/pass$pass.review.log"
    fi
    case "$verdict" in
      "VERDICT: ACCEPT"*) ;;
      "VERDICT: REJECT"*) result="rejected by review: ${verdict#VERDICT: REJECT: }" ;;
      *) result="rejected: reviewer gave no verdict" ;;
    esac
  fi

  df=$((f1 - f0)); dp=$((p1 - p0)); dc=$(pct "$c1" "$c0")
  if [ -z "$result" ]; then
    log_row "$pass" "$idea" "$df" "$dp" "$dc" "kept"
    git add -A
    git commit -qm "$(printf '%s\n\n%s\n\nfpilot free %s -> %s B, ppilot %s -> %s B.\nRender cycles (tools/bench.sh total) %s -> %s, %s.\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>' \
      "$title" "$idea" "$f0" "$f1" "$p0" "$p1" "$c0" "$c1" "$dc")"
    good_f=$f1; good_p=$p1; good_c=$c1
    cp "$pass_bench" "$RUN_DIR/current.bench"
    [ "$SHOTS" = 1 ] && cp "$RUN_DIR"/shots/*.png "$RUN_DIR/ref/"
    fails=0
    say "kept: $df B fpilot, $dc cycles ($idea)"
  else
    git diff HEAD > "$RUN_DIR/pass$pass.rejected.diff"
    git reset -q --hard "$head"; git clean -fdq
    log_row "$pass" "$idea" "$df" "$dp" "$dc" "$result"
    git commit -qm "Optimisation loop: log pass $pass ($(printf '%s' "$result" | cut -c1-40))" -- "$LEDGER"
    fails=$((fails + 1))
    say "$result ($idea)"
    # Rebuild, so the next worker starts from a build that matches the source.
    build_and_test "$RUN_DIR/pass$pass.restore.log" || die "HEAD no longer builds after revert; see $RUN_DIR/pass$pass.restore.log"
  fi
done

# Leave the tree built, so that make fram/pram report the final state.
build_and_test "$RUN_DIR/final.log" || say "final rebuild failed; see $RUN_DIR/final.log"
say "done on $BRANCH: fpilot $(free_of fpilot) B, ppilot $(free_of ppilot) B free, $good_c cycles; logs in $RUN_DIR"
