#!/usr/bin/env bash
# Start the DC3 native port on macOS, windowed.
#   scripts/start-mac.sh [dc3-native args...]
# DC3_DATA defaults to <repo>/orig-assets. A first-time setup (deps, Dawn,
# configure, build) is scripts/mac/bootstrap.sh; see docs/native/MACOS.md.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/native/build"
BINARY="$BUILD_DIR/dc3-native"
DATA_DIR="${DC3_DATA:-$REPO_ROOT/orig-assets}"

if [ ! -f "$DATA_DIR/gen/main_xbox.hdr" ]; then
    echo "error: no game data at $DATA_DIR (gen/main_xbox.hdr missing)." >&2
    echo "       Set DC3_DATA, or run scripts/mac/bootstrap.sh --assets <dir or tarball>." >&2
    exit 1
fi

# Build if needed
if [ ! -f "$BINARY" ]; then
    if [ ! -f "$BUILD_DIR/build.ninja" ]; then
        echo "error: $BUILD_DIR is not configured; run scripts/mac/bootstrap.sh first." >&2
        exit 1
    fi
    echo "Binary not found, building..."
    cmake --build "$BUILD_DIR" --target dc3-native -j"$(sysctl -n hw.logicalcpu)"
fi

cd "$REPO_ROOT"
exec env DC3_DATA="$DATA_DIR" "$BINARY" "$@"
