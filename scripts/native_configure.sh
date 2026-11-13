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
#   (ncnn_DIR + ENABLE_NCNN used to be on this list. The in-process ncnn pose
#                         path they enabled was removed 2026-11-13, PLAN P-6;
#                         native/CMakeLists.txt drops the cached options.)
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
#   DAWN_PREFIX           a Dawn install / prebuilt-tarball root (the dir with
#                         include/ and lib/); lib/cmake/Dawn under it is probed.
#                         This is the macOS path: Google's macos-latest-Release
#                         tarball unpacked anywhere (see docs/native/MACOS.md).
#   BUILD_TESTS           ON (default) / OFF. OFF skips the GTest suite, for a
#                         box without googletest (a first Mac bring-up). The
#                         native gate then has nothing to run, so this script
#                         says so instead of asserting CTestTestfile.cmake.
#   MILO_CONFIGURE_PLATFORM  override `uname -s` (Linux / Darwin); for testing
#                         the Darwin branch on Linux together with
#   MILO_CONFIGURE_DRY_RUN=1  print the cmake command and exit 0 without running
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

# Only POSIX sh + bash 3.2 features below: macOS ships /bin/bash 3.2 and BSD
# userland, so no GNU-only flags (readlink -f, sed -i, stat -c, grep -P).
PLATFORM="${MILO_CONFIGURE_PLATFORM:-$(uname -s)}"
BUILD_TESTS_VALUE="${BUILD_TESTS:-ON}"
for _a in "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"; do
    case "$_a" in
        -DBUILD_TESTS=*|-DBUILD_TESTS:BOOL=*) BUILD_TESTS_VALUE="${_a#*=}" ;;
    esac
done
case "$BUILD_TESTS_VALUE" in
    ON|on|1|TRUE|true|YES|yes) BUILD_TESTS_VALUE=ON ;;
    OFF|off|0|FALSE|false|NO|no) BUILD_TESTS_VALUE=OFF ;;
    *) echo "error: BUILD_TESTS must be ON or OFF (got '$BUILD_TESTS_VALUE')" >&2; exit 1 ;;
esac

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
_try_dawn_prefix() {
    # A prebuilt tarball / install prefix: lib/ or lib64/ (the Linux tarball
    # ships lib64; CI symlinks lib -> lib64).
    [ -n "${1:-}" ] || return 1
    _try_dawn "$1/lib/cmake/Dawn" || _try_dawn "$1/lib64/cmake/Dawn"
}
_try_dawn "${Dawn_DIR:-}" \
    || _try_dawn "${DAWN_DIR:-}" \
    || _try_dawn_prefix "${DAWN_PREFIX:-}" \
    || _try_dawn "$REPOS_ROOT/dc3-decomp-deps/dawn/lib/cmake/Dawn" \
    || _try_dawn "$MAIN_CHECKOUT-deps/dawn/lib/cmake/Dawn" \
    || _try_dawn "$REPO_ROOT/../dc3-decomp-deps/dawn/lib/cmake/Dawn" \
    || true
if [ -z "$dawn_dir" ] && [ "$PLATFORM" = "Darwin" ]; then
    # The March 2026 Mac build used a Metal Dawn at ~/dc3-deps/dawn; Homebrew
    # has no Dawn formula, so these are the only conventional spots.
    _try_dawn_prefix "${HOME:-/nonexistent}/dc3-deps/dawn" \
        || _try_dawn_prefix "$REPOS_ROOT/dawn-prebuilt" \
        || _try_dawn_prefix "/opt/homebrew/opt/dawn" \
        || _try_dawn_prefix "/usr/local/opt/dawn" \
        || true
fi

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
    echo "         DAWN_PREFIX=/path/to/unpacked/Dawn-...-Release scripts/native_configure.sh" >&2
    if [ "$PLATFORM" = "Darwin" ]; then
        echo "       macOS: download Dawn-<hash>-macos-latest-Release.tar.gz from" >&2
        echo "       https://github.com/google/dawn/releases (tag v20260423.175430)," >&2
        echo "       unpack it, and pass DAWN_PREFIX. See docs/native/MACOS.md." >&2
    fi
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
    -DBUILD_TESTS="$BUILD_TESTS_VALUE"
)
if [ -n "$imgui_src" ]; then
    CMAKE_ARGS+=(-DFETCHCONTENT_SOURCE_DIR_IMGUI="$imgui_src")
fi
CMAKE_ARGS+=("${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}")

echo "==> configuring native build"
echo "    source     : $REPO_ROOT/native"
echo "    build dir  : $BUILD_DIR"
echo "    main repo  : $MAIN_CHECKOUT"
echo "    Dawn_DIR   : $dawn_dir"
echo "    platform   : $PLATFORM    BUILD_TESTS=$BUILD_TESTS_VALUE"
[ -n "$imgui_src" ] && echo "    imgui src  : $imgui_src (reused from main; no clone)"

if [ "${MILO_CONFIGURE_DRY_RUN:-0}" = "1" ]; then
    printf 'DRY RUN: cmake'; printf ' %q' "${CMAKE_ARGS[@]}"; printf '\n'
    exit 0
fi

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
if [ "$BUILD_TESTS_VALUE" = "OFF" ]; then
    echo "==> BUILD_TESTS=OFF: no milo-tests in $BUILD_DIR."
    echo "    scripts/native_test.sh has NOTHING to run in this build dir; a build"
    echo "    that compiles is not a gate that passed."
elif [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    echo >&2
    echo "error: cmake exited 0 but produced no $BUILD_DIR/CTestTestfile.cmake." >&2
    echo "       There is nothing for ctest to run, so the native gate cannot" >&2
    echo "       execute. Was BUILD_TESTS turned off?" >&2
    exit 10
fi

# Record what the SYSTEM toolchain looked like at configure time, by content
# hash. ninja tracks these files but compares mtimes, and pacman restores
# upstream mtimes -- so a newer package can install OLDER files and no
# mtime-based rule can ever notice. See scripts/native_toolchain_check.py.
# (Linux only: it reads DT_NEEDED with ldd, which macOS does not have.)
if [ "$PLATFORM" != "Darwin" ] && [ -f "$REPO_ROOT/scripts/native_toolchain_check.py" ]; then
    python3 "$REPO_ROOT/scripts/native_toolchain_check.py" --record "$BUILD_DIR" || true
fi

echo "==> native build configured: $BUILD_DIR"
