#!/usr/bin/env bash
# Build the Emscripten/WebAssembly port and deploy to the dev server directory.
# Requires: emsdk activated (emcmake on PATH)
set -euo pipefail
NATIVE_DIR="$(cd "$(dirname "$0")/../../native" && pwd)"
BUILD_DIR="$NATIVE_DIR/build-web"
DEPLOY_DIR="$NATIVE_DIR/web/build"
if [ ! -d "$BUILD_DIR" ]; then
    emcmake cmake -S "$NATIVE_DIR" -B "$BUILD_DIR"
fi
cmake --build "$BUILD_DIR" -- -j"$(nproc)"
cp "$BUILD_DIR/dc3-web.js" "$BUILD_DIR/dc3-web.wasm" "$DEPLOY_DIR/"
cp "$NATIVE_DIR/web/index.html" "$DEPLOY_DIR/"
echo "Deployed to $DEPLOY_DIR"
