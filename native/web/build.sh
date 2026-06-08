#!/usr/bin/env bash
# DC3 Web Port — Build Script
# Compiles the dc3-web target to WASM (emcmake + emdawnwebgpu), then deploys TWO
# variants you switch between at runtime, with HTTP caching so reloads are fast:
#
#   release/ — debug-info stripped (wasm-opt --strip-debug) + brotli/gzip
#              precompressed, served `immutable` by server.py. This is what
#              http://localhost:8420/ loads by default; a reload reuses the
#              cached + already-compiled wasm (no re-download, no recompile).
#   debug/   — full -g2 wasm + gzip, served `no-store`. Loaded by
#              http://localhost:8420/?debug=true for fast iteration / DWARF.
#
# Both variants come from ONE build (release is just the stripped copy), so the
# extra cost over the old single-deploy is one wasm-opt + one brotli pass.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NATIVE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CMAKE_DIR="$NATIVE_DIR/build-web"   # cmake binary dir (gitignored via native/build*/)
DEPLOY_DIR="$SCRIPT_DIR/build"      # pure deploy dir — what server.py serves

# Source emsdk if not already active
if ! command -v emcc &>/dev/null; then
    if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$HOME/emsdk/emsdk_env.sh"
    else
        echo "ERROR: emcc not found. Install emsdk first." >&2
        exit 1
    fi
fi

echo "Using emcc: $(emcc --version | head -1)"

# Check for --cmake-only flag (configure without building)
CMAKE_ONLY=false
if [[ "${1:-}" == "--cmake-only" ]]; then
    CMAKE_ONLY=true
fi

mkdir -p "$CMAKE_DIR" "$DEPLOY_DIR"

# Configure with emcmake (only if needed)
if [ ! -f "$CMAKE_DIR/CMakeCache.txt" ]; then
    echo "Configuring CMake with Emscripten..."
    ( cd "$CMAKE_DIR" && emcmake cmake "$NATIVE_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF )
fi

if $CMAKE_ONLY; then
    echo "CMake configured. Run without --cmake-only to build."
    exit 0
fi

# Build dc3-web target. Output lands in the cmake binary dir; audio-worklet.js is
# POST_BUILD-copied into DEPLOY_DIR by milo_engine_apply_web_target_options().
echo "Building dc3-web..."
cmake --build "$CMAKE_DIR" --target dc3-web -j"$(nproc)" 2>&1

SRC_JS="$CMAKE_DIR/dc3-web.js"
SRC_WASM="$CMAKE_DIR/dc3-web.wasm"
if [ ! -f "$SRC_WASM" ]; then
    echo "ERROR: build output not found at $SRC_WASM" >&2
    exit 1
fi

# Locate wasm-opt — emsdk's lives in upstream/bin, which isn't always on PATH
# even after emsdk_env.sh.
WASM_OPT="$(command -v wasm-opt || true)"
if [ -z "$WASM_OPT" ] && [ -x "${EMSDK:-$HOME/emsdk}/upstream/bin/wasm-opt" ]; then
    WASM_OPT="${EMSDK:-$HOME/emsdk}/upstream/bin/wasm-opt"
fi

REL="$DEPLOY_DIR/release"
DBG="$DEPLOY_DIR/debug"
mkdir -p "$REL" "$DBG"

# Drop stale flat artifacts from the old single-deploy layout (index.html now
# loads from release/ or debug/). Harmless if already gone.
rm -f "$DEPLOY_DIR"/dc3-web.js "$DEPLOY_DIR"/dc3-web.js.* \
      "$DEPLOY_DIR"/dc3-web.wasm "$DEPLOY_DIR"/dc3-web.wasm.*

# debug/ — full -g2 wasm, gzip only (brotli q11 on the big -g2 wasm is slow and
# debug is served no-store for local iteration anyway).
cp "$SRC_JS" "$SRC_WASM" "$DBG/"
gzip -9 -kf "$DBG/dc3-web.js" "$DBG/dc3-web.wasm"

# release/ — strip debug info (the big size win), then brotli q11 + gzip so the
# server negotiates Content-Encoding with no runtime CPU cost.
cp "$SRC_JS" "$REL/"
if [ -n "$WASM_OPT" ]; then
    echo "==> wasm-opt --strip-debug (release)"
    "$WASM_OPT" --strip-debug --all-features -o "$REL/dc3-web.wasm" "$SRC_WASM"
else
    echo "  wasm-opt not found — shipping unstripped release wasm (still precompressed)"
    cp "$SRC_WASM" "$REL/"
fi
echo "==> pre-compressing release (brotli q11 + gzip -9)"
for f in "$REL/dc3-web.wasm" "$REL/dc3-web.js"; do
    if command -v brotli >/dev/null 2>&1; then
        brotli -q 11 -fk "$f"
    else
        echo "  brotli not installed — skipping .br (gzip fallback only)"
    fi
    gzip -9 -kf "$f"
done

# index.html is shared (picks release/ or debug/ at runtime); audio-worklet.js is
# already in DEPLOY_DIR (engine POST_BUILD copy).
cp "$SCRIPT_DIR/index.html" "$DEPLOY_DIR/index.html"
touch "$DEPLOY_DIR/favicon.ico"

echo ""
echo "Build complete!"
( cd "$DEPLOY_DIR" && ls -lh release/dc3-web.{js,wasm}{,.br,.gz} debug/dc3-web.{js,wasm}{,.gz} 2>/dev/null \
    | awk '{printf "  %-30s %s\n",$NF,$5}' )
echo ""
echo "Run:    python native/web/server.py"
echo "Play:   http://localhost:8420/            (release, cached — fast reloads)"
echo "Debug:  http://localhost:8420/?debug=true (debug, no-store)"
