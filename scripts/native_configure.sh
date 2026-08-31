#!/usr/bin/env bash
# Configure the native (x86_64 / WebGPU) build dir — from ANY checkout of this
# repo, main or worktree, with no hand-typed flags.
#
# WHY THIS EXISTS
# ---------------
# `find_package(Dawn REQUIRED)` in native/CMakeLists.txt cannot find Dawn on its
# own: Dawn is a pre-built vendored dependency living in a SIBLING repo
# (dc3-decomp-deps, cloned by scripts/dev-setup/setup-deps.sh), not in any
# system prefix. So every native configure needs `-DDawn_DIR=...`, and that flag
# was written down in eight different docs and two scripts and in nobody's
# muscle memory. `scripts/setup_worktree.sh` did not configure the native build
# at all, so `scripts/native_test.sh` in a fresh worktree said
#
#     error: no configured build at <wt>/native/build
#
# and stopped. Three lanes hit that on 2026-08-31; two configured by hand and
# one lost the gate entirely. A gate that is awkward to run is a gate that gets
# skipped, and a skipped gate looks exactly like a passed one in a lane report —
# which is how a correct ChunkStream.cpp decompilation reached main with 233
# native failures behind it.
#
# Dawn was the reported symptom. It is not the whole set. Everything below is a
# piece of configuration the MAIN checkout's native/build has and a fresh
# worktree does not, derived by diffing native/build/CMakeCache.txt against what
# a bare `cmake -S native -B native/build` produces:
#
#   Dawn_DIR              sibling repo, gitignored/absent in a worktree — HARD
#                         requirement, cmake fails without it
#   ncnn_DIR + ENABLE_NCNN native/third_party/ is in .gitignore, so a worktree
#                         has no ncnn; main configures with it ON. Soft: cmake
#                         degrades to OFF, but then the worktree is silently
#                         building a different dc3-native than main.
#   CMAKE_BUILD_TYPE      RelWithDebInfo (cmake's default is an empty, -O0 build)
#   C/CXX compiler        clang (the decomp sources need -fms-compatibility;
#                         cmake's default cc is not necessarily clang)
#   generator             Ninja (TestGates.BuildMatchesSources asks *ninja* the
#                         real dependency graph; a Makefile build dir has no
#                         answer for it)
#   EXPORT_COMPILE_COMMANDS  clangd
#   imgui source          FetchContent clones imgui from github. Reusing the
#                         main checkout's already-cloned copy makes worktree
#                         configure offline and instant. Purely a cache: if the
#                         main copy is absent we fall back to the clone.
#
# Nothing here is hardcoded to /home/free. Paths are derived from the MAIN
# checkout, located via `git rev-parse --git-common-dir` — the same idiom that
# fixed MILO_ENGINE_PATH's worktree bug in native/CMakeLists.txt. Where a path
# genuinely cannot be derived (Dawn is simply not installed on this box), this
# script FAILS LOUDLY and names every location it probed, rather than leaving
# the tree unconfigured and letting the gate go missing.
#
# Usage:
#   scripts/native_configure.sh [build-dir] [extra cmake args...]
#
#   build-dir defaults to $MILO_TEST_BUILD_DIR, then <repo>/native/build.
#
# Environment overrides (all optional):
#   DAWN_DIR / Dawn_DIR   path to the dir containing DawnConfig.cmake
#   NCNN_DIR / ncnn_DIR   path to the dir containing ncnnConfig.cmake
#   CC / CXX              compilers (default: clang / clang++ from PATH)
#   MILO_BUILD_TYPE       CMAKE_BUILD_TYPE (default: RelWithDebInfo)
#
# Exit codes:
#   0  configured
#   1  usage / internal error
#   9  a REQUIRED external dependency could not be located (Dawn, clang, cmake).
#      Distinct on purpose: "this box is missing a dependency" is a different
#      thing from "cmake rejected your CMakeLists".
#  10  cmake itself failed.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The MAIN checkout, from anywhere in the repo. `--git-common-dir` is
# <main>/.git even inside a worktree, whose own .git is a file pointing at
# <main>/.git/worktrees/<name>. This is the idiom native/CMakeLists.txt uses to
# find milo-native-engine from a worktree; the "sibling of CMAKE_SOURCE_DIR"
# guess lands in the worktree pool instead, which is exactly the bug class here.
MAIN_CHECKOUT="$REPO_ROOT"
_common="$(git -C "$REPO_ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
if [ -n "$_common" ]; then
    MAIN_CHECKOUT="$(dirname "$_common")"
fi
REPOS_ROOT="$(dirname "$MAIN_CHECKOUT")"

BUILD_DIR="${1:-${MILO_TEST_BUILD_DIR:-$REPO_ROOT/native/build}}"
[ $# -gt 0 ] && shift
EXTRA_ARGS=("$@")

command -v cmake >/dev/null 2>&1 || {
    echo "error: cmake is not on PATH; cannot configure the native build." >&2
    exit 9
}

# ---- Dawn (REQUIRED) --------------------------------------------------------
# Probe every plausible location and REPORT THEM ALL on failure. A "not found"
# that does not say where it looked is how a five-second fix becomes an hour.
dawn_dir=""
dawn_probed=()
_try_dawn() {
    [ -n "${1:-}" ] || return 1
    local seen
    for seen in "${dawn_probed[@]+"${dawn_probed[@]}"}"; do
        [ "$seen" = "$1" ] && return 1   # already probed; do not list it twice
    done
    dawn_probed+=("$1")
    [ -f "$1/DawnConfig.cmake" ] && { dawn_dir="$1"; return 0; }
    return 1
}
_try_dawn "${Dawn_DIR:-}" \
    || _try_dawn "${DAWN_DIR:-}" \
    || _try_dawn "$REPOS_ROOT/dc3-decomp-deps/dawn/lib/cmake/Dawn" \
    || _try_dawn "$MAIN_CHECKOUT-deps/dawn/lib/cmake/Dawn" \
    || _try_dawn "$REPO_ROOT/../dc3-decomp-deps/dawn/lib/cmake/Dawn" \
    || true

if [ -z "$dawn_dir" ]; then
    echo "error: Dawn (WebGPU) not found — cannot configure the native build." >&2
    echo "       native/CMakeLists.txt does find_package(Dawn REQUIRED), and Dawn is a" >&2
    echo "       PRE-BUILT vendored dependency in the sibling repo dc3-decomp-deps." >&2
    echo "       It is not installed system-wide, so cmake cannot find it unaided." >&2
    echo >&2
    echo "       Probed, in order:" >&2
    for p in "${dawn_probed[@]}"; do echo "         $p" >&2; done
    echo >&2
    echo "       Fix, either:" >&2
    echo "         scripts/dev-setup/setup-deps.sh     # clones dc3-decomp-deps" >&2
    echo "         DAWN_DIR=/path/to/dawn/lib/cmake/Dawn scripts/native_configure.sh" >&2
    exit 9
fi

# ---- compilers (REQUIRED) ---------------------------------------------------
cc="${CC:-$(command -v clang 2>/dev/null || true)}"
cxx="${CXX:-$(command -v clang++ 2>/dev/null || true)}"
if [ -z "$cc" ] || [ -z "$cxx" ]; then
    echo "error: clang / clang++ not found on PATH." >&2
    echo "       The decomp sources compile with -fms-compatibility and an MSVC STL" >&2
    echo "       shim; gcc does not build this tree. Install clang, or set CC/CXX." >&2
    exit 9
fi

# ---- ncnn (OPTIONAL, but main has it ON) ------------------------------------
# native/third_party/ is gitignored, so it exists only in the main checkout.
# Without this a worktree silently configures ENABLE_NCNN=OFF and builds a
# dc3-native with no internal pose estimator — a different binary from main's,
# under a gate whose whole job is to compare against main.
ncnn_dir=""
for c in "${ncnn_DIR:-}" "${NCNN_DIR:-}" \
         "$REPO_ROOT/native/third_party/ncnn-install/lib/cmake/ncnn" \
         "$MAIN_CHECKOUT/native/third_party/ncnn-install/lib/cmake/ncnn"; do
    if [ -n "$c" ] && [ -f "$c/ncnnConfig.cmake" ]; then ncnn_dir="$c"; break; fi
done

# ---- imgui source cache (OPTIONAL) ------------------------------------------
# Pure speed/offline convenience: reuse the main checkout's already-fetched
# imgui rather than re-cloning it per worktree. Falls back to FetchContent's
# clone when absent, so this can never be the reason a configure fails.
imgui_src=""
if [ -z "${FETCHCONTENT_SOURCE_DIR_IMGUI:-}" ] && [ "$REPO_ROOT" != "$MAIN_CHECKOUT" ]; then
    _cand="$MAIN_CHECKOUT/native/build/_deps/imgui-src"
    [ -f "$_cand/imgui.cpp" ] && imgui_src="$_cand"
fi

CMAKE_ARGS=(
    -S "$REPO_ROOT/native"
    -B "$BUILD_DIR"
    -G Ninja
    -DCMAKE_BUILD_TYPE="${MILO_BUILD_TYPE:-RelWithDebInfo}"
    -DCMAKE_C_COMPILER="$cc"
    -DCMAKE_CXX_COMPILER="$cxx"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DDawn_DIR="$dawn_dir"
)
if [ -n "$ncnn_dir" ]; then
    CMAKE_ARGS+=(-DENABLE_NCNN=ON -Dncnn_DIR="$ncnn_dir")
fi
if [ -n "$imgui_src" ]; then
    CMAKE_ARGS+=(-DFETCHCONTENT_SOURCE_DIR_IMGUI="$imgui_src")
fi
CMAKE_ARGS+=("${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}")

echo "==> configuring native build"
echo "    source     : $REPO_ROOT/native"
echo "    build dir  : $BUILD_DIR"
echo "    main repo  : $MAIN_CHECKOUT"
echo "    Dawn_DIR   : $dawn_dir"
echo "    ncnn_DIR   : ${ncnn_dir:-<not found — ENABLE_NCNN stays OFF>}"
[ -n "$imgui_src" ] && echo "    imgui src  : $imgui_src (reused from main; no clone)"

if ! cmake "${CMAKE_ARGS[@]}"; then
    echo >&2
    echo "error: cmake configure FAILED for $BUILD_DIR." >&2
    echo "       Command was:" >&2
    printf '         cmake' >&2; printf ' %q' "${CMAKE_ARGS[@]}" >&2; printf '\n' >&2
    exit 10
fi

# Assert the thing the caller actually needs, rather than trusting cmake's
# exit status. native_test.sh keys off CTestTestfile.cmake; if BUILD_TESTS were
# somehow off, cmake would exit 0 and the gate would still have nothing to run.
if [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    echo >&2
    echo "error: cmake exited 0 but produced no $BUILD_DIR/CTestTestfile.cmake." >&2
    echo "       There is nothing for ctest to run, so the native gate cannot" >&2
    echo "       execute. Was BUILD_TESTS turned off?" >&2
    exit 10
fi

echo "==> native build configured: $BUILD_DIR"
