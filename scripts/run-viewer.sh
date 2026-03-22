#!/usr/bin/env bash
# Quick-launch milo-viewer with the lilt01 character for lighting debug.
# Usage: scripts/run-viewer.sh [extra viewer args...]
# Override model: MILO_FILE=path/to/file.milo_xbox scripts/run-viewer.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/native/build"
VIEWER="$BUILD_DIR/milo-viewer"

# Default test asset
MILO_FILE="${MILO_FILE:-$REPO_ROOT/orig-assets/extracted/char/main/dancer/gen/lilt01.milo_xbox}"

# Build if needed
if [ ! -f "$VIEWER" ]; then
    echo "Building milo-viewer..."
    cmake --build "$BUILD_DIR" --target milo-viewer -- -j"$(nproc)"
fi

# The engine lowercases all paths (Xbox heritage), which breaks on
# case-sensitive Linux filesystems if the home directory has uppercase
# letters.  Work around this by copying the file to /tmp.
LOWER_FILE="$(echo "$MILO_FILE" | tr '[:upper:]' '[:lower:]')"
if [ "$LOWER_FILE" != "$MILO_FILE" ] && [ ! -f "$LOWER_FILE" ]; then
    TMP_FILE="/tmp/$(basename "$MILO_FILE")"
    cp "$MILO_FILE" "$TMP_FILE"
    echo "Copied to $TMP_FILE (case-sensitivity workaround)"
    MILO_FILE="$TMP_FILE"
fi

cd "$BUILD_DIR"
exec ./milo-viewer "$MILO_FILE" "$@"
