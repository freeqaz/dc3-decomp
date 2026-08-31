#!/usr/bin/env bash
# check_native_compiles.sh — native compile smoke test / pre-merge gate.
#
# Builds the `dc3-native` and `milo-tests` targets (RelWithDebInfo) using the
# project's native CMake build in native/build/. Exits nonzero on any build
# failure.
#
# INTENDED USE: pre-merge gate so that PPC-only commits (normal decomp work)
# cannot silently break the native port. The failure mode this guards against:
# wave-1 found that asm-archaeology commits 2b50b35e and 6eeba04f broke
# Mesh.cpp + AmbientOcclusion.cpp for modern Clang (ObjPtr<> ?: ambiguity;
# std::vector iterator-as-pointer C-casts) — a native compile smoke in CI
# would have caught both immediately.
#
# EXCLUDED: `wgpu-window-test` — this target is stale (its gfx headers moved
# to the milo-native-engine repo) and is NOT a dependency of dc3-native or
# milo-tests. Chasing it would be misleading: the real GPU test coverage lives
# in the engine repo's own test suite.
#
# PERFORMANCE: incremental builds when the tree is clean take <1 s (ninja no-op).
# A dirty build (after a header touch) is bounded by the TU count that changed.
# The first build after configure in a fresh worktree can take 10–30 minutes;
# that is not covered by the "<10s incremental when clean" spec.
#
# WIRING:
#   Pre-merge CI (GitHub Actions / local pre-push hook):
#     bash scripts/check_native_compiles.sh
#   As a periodic safety check after decomp commits:
#     bash scripts/check_native_compiles.sh --build-type RelWithDebInfo
#
# Exit codes:
#   0  both targets built successfully
#   1  build failure (error output on stderr)
#   2  missing prerequisite or bad arguments

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
NATIVE_DIR="${REPO_ROOT}/native"
BUILD_DIR="${NATIVE_DIR}/build"

# ─── Resolve milo-native-engine path absolutely ─────────────────────────────
# native/CMakeLists.txt defaults MILO_ENGINE_PATH to
# "${CMAKE_SOURCE_DIR}/../../milo-native-engine". That is correct only for the
# MAIN repo: from a git worktree under wt/<name>, CMAKE_SOURCE_DIR is the
# worktree's native/, so the default resolves to the non-existent
# wt/milo-native-engine and the configure fails (wave-9 lane A had to work
# around it with an explicit -DMILO_ENGINE_PATH).
#
# The engine is a sibling of the MAIN checkout, regardless of which worktree we
# run from. `git rev-parse --git-common-dir` always points at the main repo's
# .git (worktrees share it), so its parent is the main repo root and the engine
# is one level above that. Honor an explicit MILO_ENGINE_PATH env override.
if [[ -z "${MILO_ENGINE_PATH:-}" ]]; then
    # `git rev-parse --git-common-dir` may return a RELATIVE path (".git") when
    # run in the main checkout, or an ABSOLUTE one in a worktree. Resolve it to an
    # absolute path from within REPO_ROOT either way; its parent is the main repo.
    if _GCD="$(git -C "${REPO_ROOT}" rev-parse --git-common-dir 2>/dev/null)"; then
        GIT_COMMON_DIR="$(cd "${REPO_ROOT}" && cd "${_GCD}" && pwd)"
        MAIN_REPO_ROOT="$(cd "${GIT_COMMON_DIR}/.." && pwd)"
        MILO_ENGINE_PATH="$(cd "${MAIN_REPO_ROOT}/.." && pwd)/milo-native-engine"
    else
        # Not a git checkout (e.g. tarball) — fall back to the CMake default
        # relative to the actual source dir.
        MILO_ENGINE_PATH="$(cd "${REPO_ROOT}/.." && pwd)/milo-native-engine"
    fi
fi

BUILD_TYPE="RelWithDebInfo"
TARGETS=(dc3-native milo-tests)
JOBS="${NPROC:-$(nproc 2>/dev/null || echo 8)}"
CONFIGURE=0

for arg in "$@"; do
    case "$arg" in
        --configure) CONFIGURE=1 ;;
        --build-type=*) BUILD_TYPE="${arg#--build-type=}" ;;
        --jobs=*) JOBS="${arg#--jobs=}" ;;
        -h|--help)
            echo "Usage: $0 [--configure] [--build-type=TYPE] [--jobs=N]"
            echo "  --configure     Force cmake reconfiguration (needed for a fresh build dir)"
            echo "  --build-type=T  CMake build type (default: RelWithDebInfo)"
            echo "  --jobs=N        Parallel jobs (default: nproc)"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            exit 2
            ;;
    esac
done

# ─── Prerequisite checks ────────────────────────────────────────────────────
if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found in PATH" >&2
    exit 2
fi
if ! command -v ninja &>/dev/null; then
    echo "ERROR: ninja not found in PATH" >&2
    exit 2
fi
if ! command -v clang++ &>/dev/null; then
    echo "ERROR: clang++ not found in PATH (required — native port uses Clang only)" >&2
    exit 2
fi

if [[ ! -d "${MILO_ENGINE_PATH}" ]]; then
    echo "ERROR: milo-native-engine not found at ${MILO_ENGINE_PATH}" >&2
    echo "       Set MILO_ENGINE_PATH=/abs/path/to/milo-native-engine to override." >&2
    exit 2
fi
echo "=== milo-native-engine: ${MILO_ENGINE_PATH} ==="

# If an existing build dir was configured with a DIFFERENT engine path (e.g. the
# broken worktree default baked into the CMake cache), force a reconfigure so the
# corrected absolute path takes effect. Compare RESOLVED paths so a cache holding
# the un-normalized but equivalent default (.../native/../../milo-native-engine)
# in an already-configured main checkout does NOT trigger a needless full rebuild.
if [[ $CONFIGURE -eq 0 && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED_ENGINE="$(sed -n 's/^MILO_ENGINE_PATH:[^=]*=//p' "${BUILD_DIR}/CMakeCache.txt" | head -1)"
    CACHED_REAL="$( [[ -d "${CACHED_ENGINE}" ]] && (cd "${CACHED_ENGINE}" && pwd) || echo "${CACHED_ENGINE}")"
    WANT_REAL="$( [[ -d "${MILO_ENGINE_PATH}" ]] && (cd "${MILO_ENGINE_PATH}" && pwd) || echo "${MILO_ENGINE_PATH}")"
    if [[ -n "${CACHED_ENGINE}" && "${CACHED_REAL}" != "${WANT_REAL}" ]]; then
        echo "=== Cached MILO_ENGINE_PATH (${CACHED_ENGINE}) differs — reconfiguring ==="
        CONFIGURE=1
    fi
fi

# ─── Configure (if needed) ──────────────────────────────────────────────────
if [[ $CONFIGURE -eq 1 || ! -f "${BUILD_DIR}/build.ninja" ]]; then
    echo "=== Configuring native build (${BUILD_TYPE}) ==="
    # The configure line lives in ONE place now: scripts/native_configure.sh.
    # This used to inline it, with a hardcoded /home/free fallback for Dawn_DIR
    # and no ncnn — so it produced a subtly different build dir from the one
    # scripts/native_test.sh uses, on the same tree. One derivation, several
    # consumers; -DMILO_ENGINE_PATH is passed through as an extra arg.
    MILO_BUILD_TYPE="${BUILD_TYPE}" \
        "${REPO_ROOT}/scripts/native_configure.sh" "${BUILD_DIR}" \
        -DMILO_ENGINE_PATH="${MILO_ENGINE_PATH}" 2>&1
fi

# ─── Build ──────────────────────────────────────────────────────────────────
echo "=== Building targets: ${TARGETS[*]} (jobs=${JOBS}) ==="
BUILD_START=$(date +%s%N)

cmake --build "${BUILD_DIR}" \
      --target "${TARGETS[@]}" \
      --parallel "${JOBS}" \
      2>&1

BUILD_END=$(date +%s%N)
BUILD_MS=$(( (BUILD_END - BUILD_START) / 1000000 ))

echo ""
echo "=== Build complete in ${BUILD_MS}ms ==="
echo "Targets built:"
for t in "${TARGETS[@]}"; do
    BINARY="${BUILD_DIR}/${t}"
    if [[ -f "${BINARY}" ]]; then
        echo "  ${BINARY}"
    fi
done
echo ""
echo "check_native_compiles: PASS"
exit 0
