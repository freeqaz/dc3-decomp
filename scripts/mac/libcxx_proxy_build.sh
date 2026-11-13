#!/usr/bin/env bash
# libcxx_proxy_build.sh -- the Linux stand-in for a macOS build.
#
# macOS builds against libc++; Linux normally builds against libstdc++. Most
# of what broke the Mac build between March and November 2026 was libc++
# compatibility (the generated MSVC iterator shim, `::pow(float, float)`,
# headers libstdc++ pulls in transitively and libc++ does not). This script
# builds the native desktop targets against LLVM's libc++ on Linux, so those
# breakages show up here first. It cannot see Apple clang, Apple ld, the SDK
# headers, Metal or CoreAudio; docs/native/MACOS.md lists what stays unverified.
#
# Usage:
#   scripts/mac/libcxx_proxy_build.sh [--smoke] [--jobs N] [build-dir] [extra cmake args...]
#
#   build-dir   default <repo>/native/build-libcxx (gitignored: native/build*/)
#   --smoke     after the build, run a headless no-GPU YMCA flow (needs
#               orig-assets) and require it to reach state=playing on
#               game_screen and end at the frame cap with code 0
#   --jobs N    ninja -j (default: ninja's own)
#
#   Extra args go to cmake, e.g. -DMILO_ENGINE_PATH=/path/to/engine-clone to
#   build against an engine branch.
#
# What it checks (each is its own exit code):
#   1. clang can compile and link a libc++ program here      (exit 9 if not)
#   2. configure + build of dc3-native, milo-viewer, render-test with
#      -stdlib=libc++ (Linux links with --no-undefined, so a link that
#      succeeds has zero undefined symbols)                   (exit 6)
#   3. every object that includes libc++'s wrap_iter.h got the GENERATED
#      libc++ iterator shim (build/host_stl_shim), not a checked-in copy:
#      counted from ninja's own deps log                      (exit 7)
#   4. --smoke: the YMCA flow                                 (exit 8)
#
# The extra `-l:libstdc++.so.6` is a Linux-only artefact: the prebuilt Linux
# libwebgpu_dawn.a is built against libstdc++. Only Dawn's C API crosses that
# boundary, so mixing is safe. It is named by soname because clang rewrites a
# bare -lstdc++ into the selected -stdlib. The macOS Dawn tarball is libc++.
#
# BUILD_TESTS=OFF: the system googletest is built against libstdc++, so
# milo-tests cannot link against libc++ here. (On a Mac, brew's googletest is
# libc++ and milo-tests builds; see .github/workflows/native-macos.yml.)

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SMOKE=0
JOBS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --smoke) SMOKE=1; shift ;;
        --jobs) JOBS=(-j "$2"); shift 2 ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        *) break ;;
    esac
done
BUILD_DIR="${1:-$REPO_ROOT/native/build-libcxx}"
[ $# -gt 0 ] && shift
EXTRA=("$@")
TARGETS=(dc3-native milo-viewer render-test)

if [ "$(uname -s)" != "Linux" ]; then
    echo "error: this is the LINUX proxy for a Mac build. On a Mac, build for" >&2
    echo "       real: scripts/mac/bootstrap.sh" >&2
    exit 1
fi

# ---- 1. can this box build against libc++ at all? -------------------------
CXX_BIN="${CXX:-$(command -v clang++ || true)}"
if [ -z "$CXX_BIN" ]; then
    echo "error: clang++ not found (set CXX)." >&2; exit 9
fi
probe_dir="$(mktemp -d)"
printf '#include <vector>\n#include <string>\nint main(){std::vector<std::string> v{"a"};return (int)v.size()-1;}\n' \
    > "$probe_dir/p.cpp"
if ! "$CXX_BIN" -stdlib=libc++ -o "$probe_dir/p" "$probe_dir/p.cpp" 2> "$probe_dir/err" \
   || ! "$probe_dir/p"; then
    echo "error: $CXX_BIN cannot build a libc++ program on this box:" >&2
    sed 's/^/       /' "$probe_dir/err" >&2
    echo "       Install LLVM's libc++ and libc++abi (Arch: pacman -S libc++;" >&2
    echo "       Debian/Ubuntu: apt install libc++-dev libc++abi-dev)." >&2
    exit 9
fi
LIBCXX_VERSION="$(printf '#include <version>\n_LIBCPP_VERSION\n' \
    | "$CXX_BIN" -stdlib=libc++ -E -x c++ - 2>/dev/null | tail -1)"
echo "==> libc++ ${LIBCXX_VERSION:-?} via $CXX_BIN"

# ---- 2. configure + build -------------------------------------------------
BUILD_TESTS=OFF "$REPO_ROOT/scripts/native_configure.sh" "$BUILD_DIR" \
    -DCMAKE_CXX_FLAGS=-stdlib=libc++ \
    "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -Wl,--no-as-needed -l:libstdc++.so.6" \
    "${EXTRA[@]+"${EXTRA[@]}"}"
rc=$?
[ "$rc" -eq 0 ] || { echo "error: configure failed (native_configure.sh exit $rc)" >&2; exit "$rc"; }

echo "==> building ${TARGETS[*]} against libc++"
if ! ninja -C "$BUILD_DIR" "${JOBS[@]+"${JOBS[@]}"}" "${TARGETS[@]}"; then
    echo >&2
    echo "LIBC++ PROXY BUILD FAILED. The first errors above are what a Mac build" >&2
    echo "will hit too (libc++ is libc++); fix them on Linux." >&2
    exit 6
fi

# ---- 3. the generated shim reached every C++ TU ---------------------------
# The shim replaces libc++'s private __iterator/wrap_iter.h, so every object
# that includes <vector>/<string>/... must name the shim's copy, and none may
# name the toolchain's own (that object bypassed the shim: a checked-in copy
# ahead of it on the include path, or the -I order changed). Read from ninja's
# deps log, i.e. from what the compiler actually opened.
SHIM_DIR="$BUILD_DIR/host_stl_shim"
deps_scan() {
    ninja -C "$BUILD_DIR" -t deps 2>/dev/null | awk -v shim="$SHIM_DIR/" -v mode="$1" '
        function flush() {
            if (obj == "") return
            if (s) n_shim++
            if (b) { n_bypass++; if (mode == "list") print "         " obj }
        }
        /^[^ ].*: #deps/ { flush(); obj = $1; sub(/:$/, "", obj); s = 0; b = 0; next }
        /__iterator\/wrap_iter\.h$/ { if (index($0, shim)) s = 1; else b = 1 }
        END { flush(); if (mode != "list") print n_shim + 0, n_bypass + 0 }'
}
read -r n_shim n_bypass <<< "$(deps_scan count)"
echo "==> objects compiled against the generated shim: $n_shim; against the toolchain's own wrap_iter.h: $n_bypass"
if [ "$n_shim" -eq 0 ] || [ "$n_bypass" -ne 0 ]; then
    echo "error: the generated libc++ iterator shim did not reach every C++ object." >&2
    [ "$n_shim" -eq 0 ] && echo "       No object names $SHIM_DIR at all." >&2
    [ "$n_bypass" -ne 0 ] && { echo "       These saw libc++'s own wrap_iter.h:" >&2; deps_scan list | head -20 >&2; }
    exit 7
fi

echo "==> LIBC++ PROXY BUILD OK: ${TARGETS[*]} in $BUILD_DIR"

# ---- 4. optional headless flow --------------------------------------------
[ "$SMOKE" = "1" ] || exit 0
DATA="${DC3_DATA:-$REPO_ROOT/orig-assets}"
if [ ! -f "$DATA/gen/main_xbox.hdr" ]; then
    echo "error: --smoke needs game data; $DATA/gen/main_xbox.hdr is missing." >&2
    exit 8
fi
run_dir="$(mktemp -d -t dc3-libcxx-smoke.XXXXXX)"
mkdir -p "$run_dir/cache" "$run_dir/saves"
echo "==> smoke: headless no-GPU YMCA flow, log $run_dir/run.log"
( cd "$REPO_ROOT" && env DC3_DATA="$DATA" MILO_HEADLESS=1 MILO_NORENDER=1 \
    DC3_TEL=1 DC3_FAST_TIME=1 MILO_MAX_FRAMES=8000 \
    DC3_CACHE_DIR="$run_dir/cache" DC3_SAVE_DIR="$run_dir/saves" \
    MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
    timeout 600 "$BUILD_DIR/dc3-native" ) > "$run_dir/run.log" 2>&1
smoke_rc=$?
playing=$(grep -c 'DC3_TEL: .* state=playing screen=game_screen' "$run_dir/run.log")
exit_line=$(grep 'DC3_EXIT:' "$run_dir/run.log" | tail -1)
echo "    exit status $smoke_rc; frames in state=playing on game_screen: $playing"
echo "    ${exit_line:-no DC3_EXIT line}"
if [ "$smoke_rc" -ne 0 ] || [ "$playing" -eq 0 ] \
   || ! echo "$exit_line" | grep -q 'detail=frame_cap_reached code=0'; then
    echo "SMOKE FAILED: see $run_dir/run.log" >&2
    exit 8
fi
echo "==> SMOKE OK"
