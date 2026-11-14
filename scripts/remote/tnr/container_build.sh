#!/usr/bin/env bash
# Runs INSIDE the tnr build container (scripts/remote/tnr/remote_build.sh), with
# the worktree mounted at its desk path. Configures when needed, then builds
# (TNR_MODE=build) or runs the native gate (TNR_MODE=test). Writes the log to
# native/build/tnr-build-last.log, which travels back with the build dir.

set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 9
BUILD_DIR=native/build
LOG="$BUILD_DIR/tnr-build-last.log"
JOBS="${TNR_JOBS:-32}"
CPUSET="${TNR_CPUSET:-}"

# nice 19 + SCHED_IDLE + idle IO class, pinned to the agreed CPU set.
low() {
    local pre=(nice -n 19 ionice -c 3 chrt -i 0)
    [ -n "$CPUSET" ] && pre=(taskset -c "$CPUSET" "${pre[@]}")
    "${pre[@]}" "$@"
}

ccstats() {
    [ -n "${MILO_CCACHE_BIN:-}" ] && [ -x "$MILO_CCACHE_BIN" ] || return 0
    "$MILO_CCACHE_BIN" -d "$MILO_CCACHE_DIR" --print-stats 2>/dev/null |
        awk '$1 ~ /^(direct_cache_hit|preprocessed_cache_hit|cache_miss)$/ {print $1"="$2}' | tr '\n' ' '
}

main() {
    echo "== tnr $(date -u +%FT%TZ) host=$(cat /proc/sys/kernel/hostname) mode=${TNR_MODE:-build} -j$JOBS cpus=${CPUSET:-all}"
    local cfg=(-DMILO_ENGINE_PATH="$TNR_ENGINE")
    [ -n "${TNR_IMGUI:-}" ] && cfg+=(-DFETCHCONTENT_SOURCE_DIR_IMGUI="$TNR_IMGUI")

    local need=0
    [ -f "$BUILD_DIR/CMakeCache.txt" ] && [ -f "$BUILD_DIR/build.ninja" ] || need=1
    [ "${TNR_RECONFIGURE:-0}" = 1 ] && need=1
    if [ "$need" = 0 ] && ! python3 scripts/native_toolchain_check.py --check --quiet "$BUILD_DIR"; then
        echo "toolchain fingerprint differs from this build dir's: reconfiguring"
        need=1
    fi
    if [ "$need" = 1 ]; then
        local t0=$SECONDS
        CC="${TNR_CC:-${CC:-}}" CXX="${TNR_CXX:-${CXX:-}}" \
            low scripts/native_configure.sh "$PWD/$BUILD_DIR" "${cfg[@]}" || { echo "CONFIGURE FAILED"; return 10; }
        echo "configure: $((SECONDS - t0)) s"
    fi

    if [ "${TNR_MODE:-build}" = test ]; then
        # native_test.sh builds first (its own required targets), then ctest.
        # shellcheck disable=SC2086
        CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" low scripts/native_test.sh ${TNR_TEST_ARGS:-}
        return
    fi

    local targets
    if [ -n "${TNR_TARGETS:-}" ]; then
        read -r -a targets <<< "$TNR_TARGETS"
    else
        read -r -a targets < "$BUILD_DIR/milo_test_required_targets.txt" || return 7
    fi
    echo "targets: ${targets[*]}"
    echo "ccache before: $(ccstats)"
    local t0=$SECONDS rc
    low ninja -C "$BUILD_DIR" -j "$JOBS" "${targets[@]}"
    rc=$?
    echo "ninja rc=$rc wall=$((SECONDS - t0)) s"
    echo "ccache after:  $(ccstats)"
    return "$rc"
}

mkdir -p "$BUILD_DIR"
main > "$LOG" 2>&1
rc=$?
echo "exit $rc" >> "$LOG"
exit "$rc"
