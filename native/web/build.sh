#!/usr/bin/env bash
# DC3 Web Port — Build Script
# Compiles dc3-web target to WASM via emcmake + emdawnwebgpu
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NATIVE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Source emsdk if not already active
if ! command -v emcc &>/dev/null; then
    if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        source "$HOME/emsdk/emsdk_env.sh"
    else
        echo "ERROR: emcc not found. Install emsdk first." >&2
        exit 1
    fi
fi

echo "Using emcc: $(emcc --version | head -1)"

mkdir -p "$BUILD_DIR"

# Check for --cmake-only flag (configure without building)
CMAKE_ONLY=false
if [[ "${1:-}" == "--cmake-only" ]]; then
    CMAKE_ONLY=true
fi

# Configure with emcmake (only if needed)
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring CMake with Emscripten..."
    cd "$BUILD_DIR"
    emcmake cmake "$NATIVE_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF
    cd "$NATIVE_DIR/web"
fi

if $CMAKE_ONLY; then
    echo "CMake configured. Run without --cmake-only to build."
    exit 0
fi

# Build dc3-web target
echo "Building dc3-web..."
cmake --build "$BUILD_DIR" --target dc3-web -j$(nproc) 2>&1

# Copy web assets to build dir
cp "$SCRIPT_DIR/index.html" "$BUILD_DIR/index.html"
cp "$SCRIPT_DIR/audio-worklet.js" "$BUILD_DIR/audio-worklet.js"
touch "$BUILD_DIR/favicon.ico"

echo ""
echo "Build complete!"
ls -lh "$BUILD_DIR/dc3-web"* 2>/dev/null || ls -lh "$BUILD_DIR/"
echo ""
echo "Run: python native/web/server.py"
echo "Open: http://localhost:8420"
