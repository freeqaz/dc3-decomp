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
    || true
if [ -z "$dawn_dir" ] && [ "$PLATFORM" = "Darwin" ]; then
    # scripts/mac/bootstrap.sh unpacks Google's macOS prebuilt to
    # <parent>/dawn-prebuilt. The March 2026 Mac build used a Metal Dawn at
    # ~/dc3-deps/dawn; Homebrew has no Dawn formula.
    #
    # dc3-decomp-deps is NOT probed here: its dawn/ is the LINUX prebuilt (ELF
    # objects). Found first, as it used to be, a Mac that had run
    # setup-deps.sh would configure against it and fail at link with errors
    # that never mention Dawn.
    _try_dawn_prefix "$REPOS_ROOT/dawn-prebuilt" \
        || _try_dawn_prefix "${HOME:-/nonexistent}/dc3-deps/dawn" \
        || _try_dawn_prefix "/opt/homebrew/opt/dawn" \
        || _try_dawn_prefix "/usr/local/opt/dawn" \
        || true
elif [ -z "$dawn_dir" ]; then
    _try_dawn "$REPOS_ROOT/dc3-decomp-deps/dawn/lib/cmake/Dawn" \
        || _try_dawn "$MAIN_CHECKOUT-deps/dawn/lib/cmake/Dawn" \
        || _try_dawn "$REPO_ROOT/../dc3-decomp-deps/dawn/lib/cmake/Dawn" \
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
    if [ "$PLATFORM" != "Darwin" ]; then
        echo "         scripts/dev-setup/setup-deps.sh     # clones dc3-decomp-deps" >&2
    fi
    echo "         DAWN_DIR=/path/to/dawn/lib/cmake/Dawn scripts/native_configure.sh" >&2
    echo "         DAWN_PREFIX=/path/to/unpacked/Dawn-...-Release scripts/native_configure.sh" >&2
    if [ "$PLATFORM" = "Darwin" ]; then
        echo "       macOS: scripts/mac/bootstrap.sh --fix downloads it, or by hand:" >&2
        echo "       download Dawn-<hash>-macos-latest-Release.tar.gz from" >&2
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

# ---- compiler cache (OPTIONAL, default ON when ccache is installed) ---------
# Every lane builds the same ~2,600 TUs in its own worktree. ccache shares the
# objects between worktrees (docs/native/BUILD_CACHE.md). Never a hard
# dependency: no ccache -> plain compiles, exactly as before.
#
#   MILO_CCACHE=0          disable (configure clears any launcher it set)
#   MILO_CCACHE=1          require: exit 9 if ccache cannot be found
#   MILO_CCACHE_BIN        ccache executable (default: PATH, then ~/.local/bin)
#   MILO_CCACHE_DIR        cache dir (default: ${XDG_CACHE_HOME:-~/.cache}/milo-native-ccache)
#   MILO_CCACHE_MAXSIZE    cache size cap (default 16G)
#   CCACHE_DISABLE=1 ninja ...   bypass the cache for one build, no reconfigure
#
# Everything is passed on ccache's own command line (`ccache KEY=VALUE ...
# compiler`), so a personal ~/.config/ccache/ccache.conf cannot change what this
# build does, and the settings travel with build.ninja:
#   base_dir=<this tree>   rewrites THIS tree's absolute paths to paths relative
#                          to the build dir, identical in every worktree; the
#                          engine, deps and system paths stay absolute and are
#                          identical anyway. (Consequence: __FILE__ is relative
#                          under ccache. Code must not locate files from it;
#                          use a per-file define, see DC3_DECOMP_SRC_DIR.)
#   hash_dir=false         -g puts the cwd in DWARF; do not hash it. A hit may
#                          carry another worktree's DW_AT_comp_dir.
#   pch_external_checksum  hash <pch>.sum (written by native_ccache_launcher.sh)
#                          instead of the .pch, which differs per tree.
#   depend_mode=true       take the include list from clang's own .d instead of
#                          running the preprocessor. Without it, 22 decomp files
#                          with `#line N "e:\\lazer_build_gmc1\\..."` (Bitmap.h is
#                          one, so most TUs) disable direct mode, and every
#                          lookup -- hit or miss -- paid a full extra -E pass.
#   sloppiness             pch_defines,time_macros: what ccache requires to cache
#                          PCH users at all; include_file_m/ctime: fresh
#                          worktree files are all "too new" otherwise.
#   compiler_check         a content hash of clang + libclang-cpp + libLLVM taken
#                          here. clang-23 is a 170 KB driver; its mtime/size (the
#                          default) does not move when only llvm-libs does.
LAUNCHER_ARGS=()
CCACHE_MODE="${MILO_CCACHE:-auto}"
ccache_bin=""
case "$CCACHE_MODE" in
    0|off|OFF|no|NO|false|FALSE) CCACHE_MODE=off ;;
    1|on|ON|yes|YES|true|TRUE)   CCACHE_MODE=require ;;
    auto|"")                     CCACHE_MODE=auto ;;
    *) echo "error: MILO_CCACHE must be 0, 1 or unset (got '$CCACHE_MODE')" >&2; exit 1 ;;
esac
if [ "$CCACHE_MODE" != off ]; then
    for _c in "${MILO_CCACHE_BIN:-}" "$(command -v ccache 2>/dev/null || true)" \
              "${HOME:-/nonexistent}/.local/bin/ccache"; do
        if [ -n "$_c" ] && [ -x "$_c" ]; then ccache_bin="$_c"; break; fi
    done
    if [ -z "$ccache_bin" ] && [ "$CCACHE_MODE" = require ]; then
        echo "error: MILO_CCACHE=1 but no ccache found (MILO_CCACHE_BIN, PATH, ~/.local/bin)." >&2
        exit 9
    fi
fi
if [ -n "$ccache_bin" ]; then
    ccache_dir="${MILO_CCACHE_DIR:-${XDG_CACHE_HOME:-${HOME:-/nonexistent}/.cache}/milo-native-ccache}"
    mkdir -p "$ccache_dir" || { echo "error: cannot create ccache dir $ccache_dir" >&2; exit 1; }
    cc_check=""
    if [ "$PLATFORM" = "Linux" ] && command -v ldd >/dev/null 2>&1 \
            && command -v sha256sum >/dev/null 2>&1; then
        _drv="$(readlink -f "$cxx" 2>/dev/null || echo "$cxx")"
        # shellcheck disable=SC2046
        _sum="$( { cat "$_drv"; for _l in $(ldd "$_drv" 2>/dev/null \
                    | awk '/=>/ && ($1 ~ /clang|LLVM/) {print $3}'); do cat "$_l"; done; } \
                 | sha256sum | cut -c1-16)"
        [ -n "$_sum" ] && cc_check="compiler_check=string:clang-$_sum"
    fi
    # The wrapper builds PCHs without ccache and writes the <pch>.sum digest
    # that pch_external_checksum=true makes ccache hash instead of the .pch
    # (whose bytes embed this tree's absolute paths); see its header.
    _launcher="$REPO_ROOT/scripts/native_ccache_launcher.sh;$ccache_bin"
    _launcher="$_launcher;cache_dir=$ccache_dir;max_size=${MILO_CCACHE_MAXSIZE:-16G}"
    _launcher="$_launcher;base_dir=$REPO_ROOT;hash_dir=false;pch_external_checksum=true;depend_mode=true"
    _launcher="$_launcher;sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime"
    [ -n "$cc_check" ] && _launcher="$_launcher;$cc_check"
    LAUNCHER_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER="$_launcher" -DCMAKE_CXX_COMPILER_LAUNCHER="$_launcher")
else
    # Explicitly empty, so reconfiguring with MILO_CCACHE=0 (or after ccache
    # was removed) takes a previously cached launcher back out.
    LAUNCHER_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=)
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
    "${LAUNCHER_ARGS[@]}"
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
if [ -n "$ccache_bin" ]; then
    echo "    ccache     : $ccache_bin -> $ccache_dir (MILO_CCACHE=0 to disable)"
else
    echo "    ccache     : off ($([ "$CCACHE_MODE" = off ] && echo 'MILO_CCACHE=0' || echo 'not installed'))"
fi

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
