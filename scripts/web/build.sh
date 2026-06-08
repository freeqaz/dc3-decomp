#!/usr/bin/env bash
# Build the DC3 Emscripten/WebAssembly target and deploy artifacts next to
# native/web/index.html so server.py can serve them. This is THE one web build
# entry point — `scripts/build/web.sh` and `native/web/build.sh` are thin
# delegators kept for back-compat. Mirrors rb3's scripts/web/build.sh so the two
# repos share the same interface, flags, deploy layout, and HTTP caching flow.
#
# DUAL BUILD: produces TWO deployable builds you switch between at runtime via a
# URL param (?debug=true), each from its OWN cmake build dir + cmake config:
#   - release/  — DC3_WEB_RELEASE=ON: debug-info stripped (-g0) for the size win,
#                 brotli/gzip precompressed. Served `immutable` + version-busted
#                 by server.py, so http://localhost:8420/ RELOADS ARE FAST (the
#                 browser reuses the cached + already-compiled wasm).
#   - debug/    — DC3_WEB_RELEASE=OFF: full -O0 -g2 wasm (DWARF), gzip only.
#                 Served `no-store`. Loaded by http://localhost:8420/?debug=true.
#
# Usage:
#   scripts/web/build.sh                # build BOTH release + debug (default)
#   scripts/web/build.sh --release      # release only
#   scripts/web/build.sh --debug        # debug only (fast iteration loop)
#   scripts/web/build.sh --reconfigure  # force a fresh cmake configure
#   scripts/web/build.sh --opt Os       # higher -O for release (opt-in; -O>0 is
#                                       # risky for the matched-fork — defaults O0)
#
# Output:
#   native/build-web/         — emcc build dir for debug   (DC3_WEB_RELEASE=OFF)
#   native/build-web-release/ — emcc build dir for release (DC3_WEB_RELEASE=ON)
#   native/web/build/
#     index.html, audio-worklet.js          — shared, served from the root
#     release/{dc3-web.js,.wasm,.br,.gz}     — cached (immutable)
#     debug/{dc3-web.js,.wasm,.gz}           — no-store
#
# audio-worklet.js is POST_BUILD-copied to web/build/ root by
# milo_engine_apply_web_target_options() (it loads via a document-relative
# addModule(), so it must sit at the root for BOTH builds).
set -euo pipefail

NATIVE_DIR="$(cd "$(dirname "$0")/../../native" && pwd)"
REPO_ROOT="$(cd "$NATIVE_DIR/.." && pwd)"
DEPLOY_DIR="$NATIVE_DIR/web/build"  # pure deploy dir — what server.py serves

BUILD_DEBUG=1
BUILD_RELEASE=1
OPT_LEVEL=""
FORCE_RECONFIGURE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --release)     BUILD_DEBUG=0; BUILD_RELEASE=1 ;;
        --debug)       BUILD_DEBUG=1; BUILD_RELEASE=0 ;;
        --both|--all)  BUILD_DEBUG=1; BUILD_RELEASE=1 ;;
        --reconfigure) FORCE_RECONFIGURE=1 ;;
        --opt)         shift; OPT_LEVEL="$1" ;;
        --opt=*)       OPT_LEVEL="${1#--opt=}" ;;
        -h|--help)     sed -n '17,23p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

# Ensure the Emscripten toolchain is on PATH. If emcc/emcmake aren't active yet,
# source emsdk_env.sh from $EMSDK (set by a prior activation) or the default
# ~/emsdk checkout, so the script runs from a fresh shell.
if ! command -v emcc >/dev/null 2>&1; then
    EMSDK_DIR="${EMSDK:-$HOME/emsdk}"
    if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
        echo "==> Activating emsdk from $EMSDK_DIR"
        set +u                       # emsdk_env.sh references unset vars under -u
        # shellcheck disable=SC1091
        source "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1 || true
        set -u
    fi
fi
if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not found. Install emsdk or point \$EMSDK at your emsdk dir." >&2
    echo "       (looked for emsdk_env.sh under \${EMSDK:-\$HOME/emsdk})" >&2
    exit 1
fi
echo "Using emcc: $(emcc --version | head -1)"

# Resolve the engine path. ../../milo-native-engine relative to native/ is correct
# in the main repo but breaks from .claude/worktrees/<name>/; probe candidates and
# pass it to cmake explicitly so worktree builds mirror the main-repo build.
ENGINE_PATH_CANDIDATES=(
    "$NATIVE_DIR/../../milo-native-engine"
    "$REPO_ROOT/../milo-native-engine"
    "/home/free/code/milohax/milo-native-engine"
)
MILO_ENGINE_PATH=""
for candidate in "${ENGINE_PATH_CANDIDATES[@]}"; do
    if [ -d "$candidate/.git" ]; then
        MILO_ENGINE_PATH="$(cd "$candidate" && pwd)"
        break
    fi
done
if [ -z "$MILO_ENGINE_PATH" ]; then
    echo "ERROR: milo-native-engine not found in any candidate path." >&2
    printf '  - %s\n' "${ENGINE_PATH_CANDIDATES[@]}" >&2
    exit 1
fi

mkdir -p "$DEPLOY_DIR"

# Remove stale flat artifacts from the pre-dual-build layout. The deploy now
# lives under release/ and debug/; orphaned root-level dc3-web.* would just
# confuse. Harmless if already gone.
rm -f "$DEPLOY_DIR"/dc3-web.js "$DEPLOY_DIR"/dc3-web.js.* \
      "$DEPLOY_DIR"/dc3-web.wasm "$DEPLOY_DIR"/dc3-web.wasm.*

# Build one configuration (mode = release | debug) into its own emcc build dir
# and deploy it to web/build/<mode>/. Each build dir is pinned to its mode via
# DC3_WEB_RELEASE, so we only reconfigure when the cache is missing, the flag
# drifted (e.g. an old single-dir build), or --reconfigure was passed.
build_one() {
    local mode="$1"
    local release_flag bdir ddir
    if [ "$mode" = "release" ]; then
        release_flag=ON
        bdir="$NATIVE_DIR/build-web-release"
    else
        release_flag=OFF
        bdir="$NATIVE_DIR/build-web"
    fi
    ddir="$DEPLOY_DIR/$mode"
    mkdir -p "$ddir"

    local cmake_args=(
        -DMILO_ENGINE_PATH="$MILO_ENGINE_PATH"
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_TESTS=OFF
        -DDC3_WEB_RELEASE="$release_flag"
    )
    [ -n "$OPT_LEVEL" ] && cmake_args+=(-DDC3_WEB_OPT_LEVEL="$OPT_LEVEL")

    local need_configure=0
    if [ ! -f "$bdir/CMakeCache.txt" ] || [ "$FORCE_RECONFIGURE" = "1" ]; then
        need_configure=1
    else
        local cached_release cached_opt
        cached_release="$(grep -E '^DC3_WEB_RELEASE:BOOL=' "$bdir/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || echo OFF)"
        cached_opt="$(grep -E '^DC3_WEB_OPT_LEVEL:STRING=' "$bdir/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || echo O0)"
        if [ "$cached_release" != "$release_flag" ] \
           || { [ -n "$OPT_LEVEL" ] && [ "$cached_opt" != "$OPT_LEVEL" ]; }; then
            echo "==> [$mode] build flags changed; reconfiguring + relinking"
            rm -f "$bdir/dc3-web.wasm" "$bdir/dc3-web.js"
            need_configure=1
        fi
    fi

    if [ "$need_configure" = "1" ]; then
        echo "==> [$mode] configuring ($bdir, DC3_WEB_RELEASE=$release_flag)"
        emcmake cmake -S "$NATIVE_DIR" -B "$bdir" "${cmake_args[@]}"
    fi

    echo "==> [$mode] building dc3-web"
    cmake --build "$bdir" -- -j"$(nproc)" dc3-web

    if [ ! -f "$bdir/dc3-web.wasm" ]; then
        echo "ERROR: [$mode] build output not found at $bdir/dc3-web.wasm" >&2
        exit 1
    fi
    cp "$bdir/dc3-web.js" "$bdir/dc3-web.wasm" "$ddir/"

    # Compression. Release gets brotli q11 (best wire size, ~10x slower at build)
    # + gzip fallback; debug gets gzip only (brotli q11 on the big -g2 wasm is the
    # multi-minute step we skip for the fast-iteration build).
    echo "==> [$mode] pre-compressing artifacts"
    local f src
    for f in dc3-web.wasm dc3-web.js; do
        src="$ddir/$f"
        if [ "$mode" = "release" ] && command -v brotli >/dev/null 2>&1; then
            brotli -q 11 -f -k -o "$src.br" "$src"
        elif [ "$mode" = "release" ]; then
            echo "  brotli not installed — skipping .br (gzip fallback only)"
        fi
        gzip -9 -k -f "$src"  # produces $src.gz, keeps the original
    done
}

[ "$BUILD_RELEASE" = "1" ] && build_one release
[ "$BUILD_DEBUG" = "1" ]   && build_one debug

# Shared, served from the root. index.html picks release/ or debug/ at runtime.
cp "$NATIVE_DIR/web/index.html" "$DEPLOY_DIR/index.html"
touch "$DEPLOY_DIR/favicon.ico"

echo ""
echo "Deployed to $DEPLOY_DIR"
[ "$BUILD_RELEASE" = "1" ] && ( cd "$DEPLOY_DIR" && ls -lh release/dc3-web.{js,wasm}{,.br,.gz} 2>/dev/null | awk '{printf "  %-28s %s\n",$NF,$5}' )
[ "$BUILD_DEBUG" = "1" ]   && ( cd "$DEPLOY_DIR" && ls -lh debug/dc3-web.{js,wasm}{,.gz} 2>/dev/null     | awk '{printf "  %-28s %s\n",$NF,$5}' )
echo ""
echo "  Serve:  python3 native/web/server.py --port 8420"
echo "  Play:   http://localhost:8420/            (release, cached — fast reloads)"
echo "  Debug:  http://localhost:8420/?debug=true (debug, no-store)"
