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
#   scripts/native_test.sh --no-build       # test what is already built (see below)
#   SKIP_BUDGET_UPDATE=1 scripts/native_test.sh   # rewrite the budget file
#
# WHY THIS SCRIPT BUILDS
# ----------------------
# It used to run ctest and nothing else, which made it a lying instrument in a
# second, quieter way than the skip counting it was written to fix. Measured
# 2026-08-23 in the main checkout: native/build had last been built on Aug 20,
# the source tree was at Aug 23, and this script happily tested the old binary.
# It reported
#
#     registered : 449   EXECUTED : 380   passed : 380   FAILED : 0
#     SKIPPED    : 69    budget : 69      exit 0
#
# while a freshly configured-and-built tree at the same commit registers 504.
# Fifty-five tests did not exist in the binary, so they could not fail, so the
# run was green -- and the skip budget matched exactly, because the budget
# describes gates and the missing tests were not gated, they were absent.
# Nothing in the wrapper, in ctest, or in the TestGates suite could see it: all
# three were correct about a binary that was simply not this tree's.
#
# So the build is now part of the measurement. `--no-build` still exists, but
# it prints a warning and TestGates.BuildMatchesSources will fail the run
# anyway if the tree has moved -- deliberately, because "I only wanted to
# re-run ctest" is exactly how the stale reading happened.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${MILO_TEST_BUILD_DIR:-$REPO_ROOT/native/build}"
BUDGET_FILE="$REPO_ROOT/native/tests/skip_budget.txt"

CTEST_ARGS=()
ALL_GATES=0
DO_BUILD=1
for arg in "$@"; do
    case "$arg" in
        --all-gates)
            export DC3_GAMEPLAY_TESTS=1
            export DC3_AUDIO_TESTS=1
            export MILO_LONG_TEST=1
            ALL_GATES=1
            ;;
        --no-build) DO_BUILD=0 ;;
        *) CTEST_ARGS+=("$arg") ;;
    esac
done

if [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    echo "error: no configured build at $BUILD_DIR" >&2
    echo "       configure it first, or set MILO_TEST_BUILD_DIR." >&2
    exit 1
fi

# Build BEFORE testing. A stale build dir silently narrows the suite: the tests
# that would fail are not in the binary, so ctest cannot run them, so the run is
# green. See the header comment for the measured 449-vs-504 incident.
if [ "$DO_BUILD" = "1" ]; then
    # The target list is NOT maintained here. CMake owns it
    # (MILO_TEST_REQUIRED_TARGETS in native/CMakeLists.txt) and writes it to the
    # build dir; TestGates.BuildMatchesSources verifies the currency of the same
    # list. One definition, two consumers, and if they drift the test fails.
    #
    # Why not just `all`: wgpu-window-test has been broken since GpuDevice.h
    # moved into the shared engine, so `cmake --build <dir>` with no target does
    # not succeed in this repo. Pre-existing, unrelated, last built 2026-03-25.
    #
    # Why a list at all is dangerous, measured 2026-08-23 during this very
    # investigation: the suite drives THREE binaries as subprocesses --
    # dc3-native (DtaFlowTest, HeadlessBootTest, GameplayTelemetryTest) and
    # milo-viewer (MiloViewerScreenshot, MiloViewerPosePipeline). Building only
    # `milo-tests dc3-native` left milo-viewer absent, its 5 tests SKIPPED, the
    # run went 69 -> 74 skips, and the ratchet fired saying coverage had shrunk.
    # Coverage had not shrunk; the build was incomplete. Hence the single source.
    TARGETS_FILE="$BUILD_DIR/milo_test_required_targets.txt"
    if [ ! -f "$TARGETS_FILE" ]; then
        echo "error: $TARGETS_FILE is missing." >&2
        echo "       CMake generates it from MILO_TEST_REQUIRED_TARGETS. Its" >&2
        echo "       absence means this build dir predates that, and guessing a" >&2
        echo "       target list here is exactly the failure mode it replaced." >&2
        echo "       Reconfigure: cmake $BUILD_DIR" >&2
        exit 7
    fi
    read -r -a BUILD_TARGETS < "$TARGETS_FILE"
    if [ "${#BUILD_TARGETS[@]}" -eq 0 ]; then
        echo "error: $TARGETS_FILE is empty; refusing to build nothing and" >&2
        echo "       report the result as a measurement." >&2
        exit 7
    fi
    echo "==> building ${BUILD_TARGETS[*]} in $BUILD_DIR"
    if ! cmake --build "$BUILD_DIR" --target "${BUILD_TARGETS[@]}"; then
        echo >&2
        echo "error: build FAILED. Not running ctest against the previous" >&2
        echo "       binary -- that would report the old tree's results as" >&2
        echo "       though they were this one's." >&2
        exit 6
    fi
    # cmake re-runs configure when a CMakeLists changed, which can add or remove
    # tests. gtest_discover_tests re-runs on relink, so the ctest list is fresh
    # by the time we get here.
else
    echo "==> --no-build: testing whatever is already in $BUILD_DIR"
    echo "    (TestGates.BuildMatchesSources will fail if it is stale)"
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
        # Check the environment before believing the number. The budget was
        # recorded in the main checkout; archive/ is in .gitignore, so it exists
        # ONLY there, and exactly one test reads a golden out of it. A worktree
        # without the symlink is therefore +1 skipped forever, which reads as a
        # coverage regression and is not one. Both lanes that hit this on
        # 2026-08-23 were looking at a real exit 2 with an environmental cause.
        if [ ! -e "$REPO_ROOT/archive/screenshots/pose_regression/goldens/stand_bad_mid.pose.json" ]; then
            echo
            echo "      NOTE, before you touch the budget: this checkout has no"
            echo "        archive/screenshots/pose_regression/goldens/stand_bad_mid.pose.json"
            echo "      archive/ is gitignored, so it exists only in the main"
            echo "      checkout. MiloViewerScreenshot.PoseDumpCanMatchGoldenWithTolerance"
            echo "      skips without it, and accounts for exactly 1 of the"
            echo "      $skipped skips above. Fix the environment, not the budget:"
            echo "        ln -s <main-checkout>/archive $REPO_ROOT/archive"
            echo "      (scripts/setup_worktree.sh now does this for new worktrees.)"
        fi
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
