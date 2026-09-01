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
#   scripts/native_test.sh --no-configure   # do NOT auto-configure a missing build dir
#   SKIP_BUDGET_UPDATE=1 scripts/native_test.sh   # rewrite the budget file
#
# EXIT CODES — each failure mode is its own number, deliberately.
#   0  all executed tests passed and the skip count equals the budget
#   2  MORE tests skipped than the budget: coverage shrank
#   3  FEWER tests skipped than the budget: coverage improved, lock it in
#   4  the ctest summary line could not be parsed (this script's own instrument
#      is broken; refusing to report a number)
#   5  the skip budget file is missing or unparseable — the ratchet would be
#      disarmed, and a disarmed ratchet exits 0 because ctest scores skips as
#      passes. Also: refusing to LOOSEN the budget without ALLOW_BUDGET_LOOSEN=1
#   6  the build failed (so ctest was NOT run against the previous binary)
#   7  milo_test_required_targets.txt missing/empty — CMake's target list did not
#      reach this script, and guessing one is the bug that cost milo-viewer's
#      5 tests to a skip
#   8  RESERVED — this is CTEST's own exit status when tests failed, passed
#      through by the `exit "$rc"` at the bottom. Verified empirically, not
#      assumed: a two-test CTest project with one failing test exits 8. Do not
#      allocate 8 to anything here; "some tests failed" and any wrapper-level
#      condition sharing one number is precisely the ambiguity this table exists
#      to remove. (Caught while adding 9 below, which was written as 8 first.)
#   9  NO CONFIGURED BUILD, and it could not be configured. ZERO tests ran.
#      This is the "the gate never executed" code and it exists because that
#      state used to be a bare exit 1, indistinguishable in a lane's report from
#      any other hiccup — and indistinguishable from a pass to anyone reading
#      only a summary. "Examined zero things" must never look like success.
#      (scripts/native_configure.sh has its own space: 9 = a required external
#      dependency is missing, 10 = cmake failed. Its code is printed in the
#      banner, never returned from here.)
#  10  THE SYSTEM TOOLCHAIN MOVED under this build dir and it could not be
#      refreshed (--no-configure, or the reconfigure itself failed). See
#      scripts/native_toolchain_check.py; the WHY is below.
#   *  anything else is ctest's own exit status
#
# WHY THIS SCRIPT CHECKS THE SYSTEM TOOLCHAIN
# -------------------------------------------
# Measured 2026-09-01. One `pacman -Syu` moved gtest 1.17->1.18, ffmpeg 8->9,
# glfw and nvidia-utils in a single transaction, and every configured
# native/build in every checkout became unbuildable at once. The main
# checkout's milo-tests had SIX unresolvable DT_NEEDED entries -- it could not
# be exec'd -- yet `ninja` considered the tree up to date and re-ran nothing.
#
# The reason is worth stating exactly, because it is not a missing dependency
# edge: cmake DOES declare /usr/lib/cmake/GTest/GTestConfig.cmake as an input
# of the build.ninja regeneration edge. **pacman restores each file's upstream
# mtime**, so gtest 1.18's files landed dated 2026-08-30 21:08 -- OLDER than
# the build.ninja written 2026-08-31 17:05. Ninja's whole staleness model is
# "input newer than output"; a package manager that moves mtimes BACKWARDS
# defeats it silently, and TestGates.BuildMatchesSources asks ninja, so it is
# blind by construction too.
#
# The loud form of this is a deleted soname (ninja hard-errors, exit 6 here,
# with a message that does not say "reconfigure"). The quiet form is an
# in-place ABI bump: same path, new content, headers' mtimes also backwards,
# so ninja recompiles nothing and you link last month's objects against this
# month's library. native_toolchain_check.py catches both by CONTENT HASH,
# never mtime, and this script refreshes the build dir rather than measuring
# through it.
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
DO_CONFIGURE=1
for arg in "$@"; do
    case "$arg" in
        --all-gates)
            export DC3_GAMEPLAY_TESTS=1
            export DC3_AUDIO_TESTS=1
            export MILO_LONG_TEST=1
            ALL_GATES=1
            ;;
        --no-build) DO_BUILD=0 ;;
        --no-configure) DO_CONFIGURE=0 ;;
        *) CTEST_ARGS+=("$arg") ;;
    esac
done

# ---------------------------------------------------------------------------
# "No configured build" is a DISTINCT outcome, not a generic failure.
# ---------------------------------------------------------------------------
# This used to be `exit 1` with "configure it first" and no hint as to how. In a
# fresh worktree that was the *only* thing standing between a lane and its
# native gate, because scripts/setup_worktree.sh never configured the native
# build — so `scripts/native_test.sh` in a worktree failed instantly, every
# time, and three lanes on 2026-08-31 either configured by hand or gave up.
#
# A lane that gives up here has EXAMINED ZERO TESTS, and exit 1 buried among
# every other exit 1 in a shell pipeline is indistinguishable from noise. So:
#   * try to configure automatically (scripts/native_configure.sh derives every
#     path, including Dawn_DIR, from the main checkout);
#   * if that is impossible, exit 9 — a code used for nothing else — under a
#     banner that says the gate DID NOT RUN, in the words a reader of a lane
#     report needs to see.
#     (This prose said "exit 8" until 2026-09-01, contradicting both the code
#     below and the table above, which reserves 8 for ctest's own "tests
#     failed". The table was right; 8 was this block's first draft.)
if [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    CONFIGURE_SH="$REPO_ROOT/scripts/native_configure.sh"
    if [ "$DO_CONFIGURE" = "1" ] && [ -x "$CONFIGURE_SH" ]; then
        echo "==> no configured build at $BUILD_DIR; configuring it now"
        echo "    ($CONFIGURE_SH — pass --no-configure to refuse this)"
        "$CONFIGURE_SH" "$BUILD_DIR"
        configure_rc=$?
        if [ "$configure_rc" -ne 0 ]; then
            echo >&2
            echo "==============================================================" >&2
            echo " NATIVE GATE DID NOT RUN — this is NOT a pass." >&2
            echo "--------------------------------------------------------------" >&2
            echo " 0 tests were registered, 0 executed, 0 skipped." >&2
            echo " Auto-configure failed (exit $configure_rc); see the error above." >&2
            echo " Do not report this run as green: nothing was examined." >&2
            echo "==============================================================" >&2
            exit 9
        fi
    fi
fi
if [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    echo >&2
    echo "==============================================================" >&2
    echo " NATIVE GATE DID NOT RUN — this is NOT a pass." >&2
    echo "--------------------------------------------------------------" >&2
    echo " No configured build at:" >&2
    echo "   $BUILD_DIR" >&2
    echo " 0 tests were registered, 0 executed, 0 skipped. A report saying" >&2
    echo " \"native tests pass\" on the strength of this run would be false." >&2
    echo >&2
    echo " Configure it:" >&2
    echo "   $REPO_ROOT/scripts/native_configure.sh" >&2
    echo " or point at an existing build dir:" >&2
    echo "   MILO_TEST_BUILD_DIR=<dir> $0" >&2
    if [ "$DO_CONFIGURE" != "1" ]; then
        echo >&2
        echo " (--no-configure was given, so no attempt was made.)" >&2
    fi
    echo "==============================================================" >&2
    exit 9
fi

# ---------------------------------------------------------------------------
# The system toolchain is an input to this build dir, and ninja cannot see it
# move. Check by content hash before building. See the header comment.
# ---------------------------------------------------------------------------
TOOLCHAIN_CHECK="$REPO_ROOT/scripts/native_toolchain_check.py"
if [ -f "$TOOLCHAIN_CHECK" ]; then
    TC_OUT="$(python3 "$TOOLCHAIN_CHECK" --check "$BUILD_DIR" 2>&1)"
    TC_RC=$?
    case "$TC_RC" in
        0) ;;                                   # current
        4) python3 "$TOOLCHAIN_CHECK" --record "$BUILD_DIR" --quiet ;;
        2|3)
            echo >&2
            echo "==============================================================" >&2
            echo " SYSTEM TOOLCHAIN MOVED UNDER THIS BUILD DIR" >&2
            echo "--------------------------------------------------------------" >&2
            echo "$TC_OUT" | sed 's/^/ /' >&2
            echo "--------------------------------------------------------------" >&2
            echo " ninja CANNOT see this: pacman restores upstream mtimes, so" >&2
            echo " newer packages install OLDER files and every staleness rule" >&2
            echo " keyed on mtime -- ninja's, and TestGates.BuildMatchesSources" >&2
            echo " which asks ninja -- reports the tree as current." >&2
            echo " Measuring through it would report the old toolchain's" >&2
            echo " results, or link objects against a library they were not" >&2
            echo " compiled for." >&2
            echo "==============================================================" >&2
            # --no-build is refused here rather than warned about: healing this
            # means reconfiguring and (for a library move) discarding object
            # files, and doing that without then rebuilding would hand ctest an
            # even emptier build dir than it started with.
            if [ "$DO_CONFIGURE" != "1" ] || [ "$DO_BUILD" != "1" ] \
               || [ ! -x "$REPO_ROOT/scripts/native_configure.sh" ]; then
                echo >&2
                echo " NATIVE GATE DID NOT RUN -- this is NOT a pass." >&2
                echo " Refresh it:  scripts/native_configure.sh $BUILD_DIR" >&2
                echo " then, if a LIBRARY moved:  cmake --build $BUILD_DIR --target clean" >&2
                echo " then re-run this script WITHOUT --no-build/--no-configure." >&2
                exit 10
            fi
            echo "==> refreshing: reconfigure$(echo "$TC_OUT" | grep -q 'REMEDY: clean-rebuild' && echo ' + clean rebuild')" >&2
            if ! "$REPO_ROOT/scripts/native_configure.sh" "$BUILD_DIR"; then
                echo "error: reconfigure FAILED; the build dir is still stale." >&2
                exit 10
            fi
            if echo "$TC_OUT" | grep -q 'REMEDY: clean-rebuild'; then
                # A library moved. Object files compiled against the old
                # headers are suspect and ninja will not rebuild them, because
                # the new headers' mtimes went backwards too. Only a clean
                # rebuild is honest here.
                cmake --build "$BUILD_DIR" --target clean >/dev/null 2>&1 || true
            fi
            python3 "$TOOLCHAIN_CHECK" --record "$BUILD_DIR" --quiet
            ;;
        *)
            echo "warning: native_toolchain_check.py exited $TC_RC; continuing." >&2
            echo "$TC_OUT" | sed 's/^/  /' >&2
            ;;
    esac
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

# Why the GPU gets named here, and not left to the reader.
#
# On 2026-09-01 a `pacman -Syu` upgraded nvidia-utils 610.43.03 -> 610.57.04
# WITHOUT a reboot, so the loaded kernel module and the userspace libraries
# disagreed and the only Vulkan ICD on this box (nvidia_icd.json) failed to
# load: `vulkaninfo` reported "Found no drivers". Every GPU-gated test skipped,
# the count went 69 -> 74, and the ratchet fired with "Coverage shrank. Either
# restore the gate, or raise the budget" -- a message that points a lane at the
# SOURCE TREE for a fault that is entirely in the machine. A lane that believes
# it launders an environmental outage into the budget file permanently. So the
# environment gets audited before the number is believed, exactly as it already
# is for the gitignored archive/ golden.
gpu_unavailable_reason() {
    local nvrm userspace glcore
    if [ -r /proc/driver/nvidia/version ]; then
        nvrm=$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' /proc/driver/nvidia/version | head -1)
        glcore=$(ls /usr/lib/libnvidia-glcore.so.[0-9]* 2>/dev/null | head -1)
        userspace=$(printf '%s' "${glcore:-}" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        if [ -n "${nvrm:-}" ] && [ -n "${userspace:-}" ] && [ "$nvrm" != "$userspace" ]; then
            echo "NVIDIA kernel module is $nvrm but the userspace libraries are $userspace."
            echo "The driver was upgraded without a reboot, so every Vulkan ICD fails to"
            echo "load and this box has NO GPU at all until it is rebooted. Confirm with:"
            echo "  vulkaninfo --summary     (expect: 'Found no drivers')"
            return 0
        fi
    fi
    if command -v vulkaninfo >/dev/null 2>&1 \
       && ! timeout 60 vulkaninfo --summary >/dev/null 2>&1; then
        echo "vulkaninfo cannot create a Vulkan instance: no working Vulkan driver."
        echo "Every GPU-gated test will skip until that is fixed."
        return 0
    fi
    return 1
}

GPU_REASON=""
if [ "$skipped" -gt 0 ]; then
    echo
    echo "Skipped suites (a green ctest says nothing about these):"
    grep '\*\*\*Skipped' "$LOG" | sed -E 's/.*Test +#[0-9]+: ([^ ]+).*/  \1/' \
        | sed 's/\..*//' | sort | uniq -c | sort -rn
    GPU_REASON="$(gpu_unavailable_reason || true)"
    if [ -n "$GPU_REASON" ]; then
        echo
        echo "  NOTE: the GPU is unavailable on this box right now."
        echo "$GPU_REASON" | sed 's/^/    /'
        echo "    GPU-gated tests skip for that reason and NOT because of the tree."
    fi
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
        if [ -n "$GPU_REASON" ]; then
            echo
            echo "      DO NOT TOUCH THE BUDGET YET -- this box has no GPU:"
            echo "$GPU_REASON" | sed 's/^/        /'
            echo "      That is an outage on the machine, not a regression in the"
            echo "      tree. Raising the budget would make an environmental"
            echo "      outage the permanent new normal. Fix the box (reboot),"
            echo "      or report the gate red with this as the reason."
        fi
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
