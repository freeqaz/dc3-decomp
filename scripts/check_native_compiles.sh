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

# ─── Configure (if needed) ──────────────────────────────────────────────────
if [[ $CONFIGURE -eq 1 || ! -f "${BUILD_DIR}/build.ninja" ]]; then
    echo "=== Configuring native build (${BUILD_TYPE}) ==="
    cmake -S "${NATIVE_DIR}" \
          -B "${BUILD_DIR}" \
          -G Ninja \
          -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
          -DCMAKE_C_COMPILER=clang \
          -DCMAKE_CXX_COMPILER=clang++ \
          -DDawn_DIR="${DAWN_DIR:-/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn}" \
          2>&1
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
