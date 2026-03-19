#!/usr/bin/env bash
# Start DC3 native port on macOS
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/native/build"
BINARY="$BUILD_DIR/dc3-native"
DATA_DIR="$REPO_ROOT/orig-assets"

# Build if needed
if [ ! -f "$BINARY" ]; then
    echo "Binary not found, building..."
    cmake --build "$BUILD_DIR" --target dc3-native -j"$(sysctl -n hw.logicalcpu)"
fi

exec env DC3_DATA="$DATA_DIR" "$BINARY" "$@" 2>&1
