#!/usr/bin/env bash
# Run the native test suite and report SKIPPED as a first-class outcome.
#
# Why this wrapper exists
# -----------------------
# `ctest` counts skipped tests as passed. The 2026-08-19 toolchain audit found
# it printing
#
#     100% tests passed, 0 tests failed out of 441
#
# while 79 tests skipped — only 362 actually executed. That headline is a lying
# instrument: it is identical whether the optional tier ran and passed, or never
# ran at all. And the skipped tier was where two live native bugs were sitting
# (an intermittent SIGSEGV in TaskMgr::Poll, and a 130-unit ankle jump).
#
# This wrapper:
#   * prints executed / passed / failed / SKIPPED separately,
#   * fails (exit 2) when the skip count EXCEEDS the recorded budget, so a
#     newly-added gate cannot silently shrink coverage,
#   * fails (exit 3) when the skip count is BELOW the budget without the budget
#     file being updated — a ratchet, so recovered coverage stays recovered.
#
# The budget lives in native/tests/skip_budget.txt and is meant to be edited
# deliberately, with the commit explaining why.
#
# Usage:
#   scripts/native_test.sh                  # default gates
#   scripts/native_test.sh --all-gates      # also DC3_GAMEPLAY_TESTS / audio / long
#
# NOTE on --all-gates cost. gtest_discover_tests registers one CTest test per
# gtest case, and CTest runs each in its own process -- so each of the 48
# GameplayTelemetryTest cases re-runs the suite's shared 9050-frame engine
# fixture from scratch (~40 s each, ~35 min for the tier). To exercise that tier
# quickly, run the binary directly so the fixture is built once:
#
#   cd orig-assets && DC3_GAMEPLAY_TESTS=1 \
#       native/build/milo-tests --gtest_filter='GameplayTelemetryTest.*'
#
# That is ~2 minutes for the same 48 assertions.
#   scripts/native_test.sh -R SomeRegex     # extra args pass through to ctest
#   SKIP_BUDGET_UPDATE=1 scripts/native_test.sh   # rewrite the budget file

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${MILO_TEST_BUILD_DIR:-$REPO_ROOT/native/build}"
BUDGET_FILE="$REPO_ROOT/native/tests/skip_budget.txt"

CTEST_ARGS=()
ALL_GATES=0
for arg in "$@"; do
    case "$arg" in
        --all-gates)
            export DC3_GAMEPLAY_TESTS=1
            export DC3_AUDIO_TESTS=1
            export MILO_LONG_TEST=1
            ALL_GATES=1
            ;;
        *) CTEST_ARGS+=("$arg") ;;
    esac
done

if [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    echo "error: no configured build at $BUILD_DIR" >&2
    echo "       configure it first, or set MILO_TEST_BUILD_DIR." >&2
    exit 1
fi

LOG="$(mktemp -t native_test.XXXXXX.log)"
# shellcheck disable=SC2086
( cd "$BUILD_DIR" && ctest "${CTEST_ARGS[@]+"${CTEST_ARGS[@]}"}" ) 2>&1 | tee "$LOG"
CTEST_RC=${PIPESTATUS[0]}

# ctest prints "100% tests passed out of N" when nothing failed, and
# "X% tests passed, F tests failed out of N" when something did. Parse both --
# a regex that only matched the failing form silently reported 0 registered
# tests on a green run, which is exactly the class of bug this script exists to
# catch, so it is parsed defensively and asserted below.
total=$(grep -oE 'tests passed[,a-z ]*[0-9]* *[a-z]* *[a-z]* out of [0-9]+' "$LOG" \
        | grep -oE '[0-9]+$' | tail -1)
failed=$(grep -oE '[0-9]+ tests failed out of' "$LOG" | grep -oE '^[0-9]+' | tail -1)
skipped=$(grep -c '\*\*\*Skipped' "$LOG")
: "${failed:=0}"
if [ -z "${total:-}" ] || [ "$total" -eq 0 ]; then
    echo "error: could not parse the ctest summary line -- this script's own" >&2
    echo "       instrument is broken; refusing to report a number." >&2
    grep -E 'tests passed' "$LOG" >&2
    exit 4
fi
executed=$(( total - skipped ))
passed=$(( executed - failed ))

# The budget file is a PRECONDITION, not an optional decoration.
#
# This used to be `budget=""` plus `[ -f ... ] && budget=$(...)`, with no branch
# for the empty case: both the over-budget check (exit 2) and the ratchet
# (exit 3) sat inside `if [ -n "$budget" ]`, so an absent or unparseable file
# skipped the ONLY mechanism enforcing native coverage and the script exited
# $CTEST_RC -- which is 0, because ctest scores skips as passes. The only
# visible difference was that the `budget :` line vanished from the banner: an
# absence, not a signal.
#
# Two one-character launderings, both measured 2026-08-22:
#   rm native/tests/skip_budget.txt            -> gate off, exit 0
#   rewrite its contents as "budget: 69"       -> `grep -oE '^[0-9]+'` needs
#      digits at column 1, matches nothing, gate off, exit 0 -- and the diff
#      reads like a documentation improvement.
# Both are now exit 5.
budget=""
if [ ! -f "$BUDGET_FILE" ]; then
    echo "error: skip budget file missing: $BUDGET_FILE" >&2
    echo "       The budget is the only thing enforcing native test coverage." >&2
    echo "       Refusing to report a pass with the ratchet disarmed." >&2
    exit 5
fi
budget=$(grep -oE '^[0-9]+' "$BUDGET_FILE" | head -1)
if [ -z "$budget" ]; then
    echo "error: could not parse a skip budget from $BUDGET_FILE" >&2
    echo "       Wanted a bare integer at the start of a line; got:" >&2
    sed -n '1,5p' "$BUDGET_FILE" | sed 's/^/         /' >&2
    echo "       An unparseable budget used to silently disable the gate." >&2
    exit 5
fi

echo
echo "=============================================================="
echo " milo-tests summary   (ctest exit $CTEST_RC)"
echo "--------------------------------------------------------------"
printf "   registered : %d\n" "$total"
printf "   EXECUTED   : %d\n" "$executed"
printf "     passed   : %d\n" "$passed"
printf "     FAILED   : %d\n" "$failed"
printf "   SKIPPED    : %d   <-- ctest reports these as PASSED\n" "$skipped"
if [ -n "$budget" ]; then
    printf "   budget     : %s\n" "$budget"
fi
echo "=============================================================="

if [ "$skipped" -gt 0 ]; then
    echo
    echo "Skipped suites (a green ctest says nothing about these):"
    grep '\*\*\*Skipped' "$LOG" | sed -E 's/.*Test +#[0-9]+: ([^ ]+).*/  \1/' \
        | sed 's/\..*//' | sort | uniq -c | sort -rn
fi

if [ "${SKIP_BUDGET_UPDATE:-0}" = "1" ]; then
    # Same command ratchets both ways, and the loosening direction exits 0
    # because ctest scores skips as passes. So `SKIP_BUDGET_UPDATE=1` after
    # gating out 100 tests wrote the larger number and reported success --
    # laundering a coverage regression through the mechanism that exists to
    # catch it. Tightening stays a one-liner; loosening now needs to be said
    # out loud.
    if [ "$skipped" -gt "$budget" ] && [ "${ALLOW_BUDGET_LOOSEN:-0}" != "1" ]; then
        echo
        echo "REFUSING to loosen the skip budget: $budget -> $skipped (+$((skipped - budget)))."
        echo "      That records a coverage REGRESSION as the new normal, and"
        echo "      this command exits 0 either way. If it is genuinely"
        echo "      intended, re-run with ALLOW_BUDGET_LOOSEN=1 and say why in"
        echo "      the commit message."
        exit 5
    fi
    echo "$skipped" > "$BUDGET_FILE"
    echo "skip budget updated: $budget -> $skipped"
    exit "$CTEST_RC"
fi

rc=$CTEST_RC
# The budget describes the DEFAULT configuration. --all-gates deliberately runs
# wider, so it will always come in under budget; checking the ratchet there
# would report "coverage improved, lock it in" on every single run and train
# people to ignore the message.
if [ "$ALL_GATES" = "1" ]; then
    echo
    echo "(--all-gates: skip budget not enforced; the budget describes the"
    echo " default gate set. $skipped skipped here vs a default budget of ${budget:-n/a}.)"
    budget=""
fi
if [ -n "$budget" ]; then
    if [ "$skipped" -gt "$budget" ]; then
        echo
        echo "FAIL: $skipped tests skipped, budget is $budget."
        echo "      Coverage shrank. Either restore the gate, or raise the budget"
        echo "      in $BUDGET_FILE with a commit message saying why."
        rc=2
    elif [ "$skipped" -lt "$budget" ]; then
        echo
        echo "FAIL (ratchet): only $skipped tests skipped but the budget is $budget."
        echo "      Coverage improved -- lock it in:"
        echo "      SKIP_BUDGET_UPDATE=1 scripts/native_test.sh"
        rc=3
    fi
fi

rm -f "$LOG"
exit "$rc"
