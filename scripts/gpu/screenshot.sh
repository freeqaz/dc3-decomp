#!/usr/bin/env bash
# Take screenshots from DC3 native port binaries.
#
# Handles ALL configuration automatically:
#   - Sets MILO_RENDER=1, MILO_HEADLESS=1, screenshot dir/frames
#   - Detects binary type (dc3-native, render-test, milo-viewer)
#   - Enforces timeout so dc3-native doesn't run forever
#   - Checks GPU access and gives clear error if blocked
#
# Usage:
#   scripts/gpu/screenshot.sh [options] <binary> [binary-args...]
#
# Options:
#   -o <dir>        Output directory (default: /tmp/dc3_screenshots)
#   -f <frames>     Comma-separated frame numbers (default: 10,50,100)
#   -t <seconds>    Timeout (default: 30)
#   -w <WxH>        Resolution (default: 1280x720)
#   -h              Show this help
#
# Examples:
#   scripts/gpu/screenshot.sh native/build/dc3-native
#   scripts/gpu/screenshot.sh -f 100,500 native/build/dc3-native
#   scripts/gpu/screenshot.sh native/build/milo-viewer path/to/scene.milo_xbox
#   scripts/gpu/screenshot.sh native/build/render-test --test solid_quads

set -euo pipefail

# Defaults
OUTPUT_DIR="/tmp/dc3_screenshots"
FRAMES="10,50,100"
TIMEOUT=30
WIDTH=1280
HEIGHT=720

usage() {
    sed -n '/^# Usage:/,/^# Examples:/p' "$0" | sed 's/^# \?//'
    exit 0
}

# Check if GPU/Vulkan is accessible. Common failure: Claude Code sandbox blocks
# /dev/dri, Vulkan ICD JSON paths, or libvulkan.so. This gives a clear error
# instead of a cryptic Dawn/GLFW failure deep in the binary's output.
check_gpu() {
    # Check for Vulkan ICD files (driver interface)
    local icd_found=false
    for dir in /usr/share/vulkan/icd.d /etc/vulkan/icd.d; do
        [[ -d "$dir" ]] && icd_found=true && break
    done
    if ! $icd_found; then
        echo "ERROR: No Vulkan ICD found. Is a GPU driver installed?" >&2
        exit 1
    fi

    # Check for GPU device nodes
    if [[ ! -d /dev/dri ]]; then
        echo "ERROR: /dev/dri not found — GPU device access is blocked." >&2
        echo "" >&2
        echo "If running via Claude Code, use: dangerouslyDisableSandbox: true" >&2
        echo "The sandbox blocks GPU device access needed for Vulkan rendering." >&2
        exit 1
    fi

    # Quick check: can we actually list render nodes?
    if ! ls /dev/dri/renderD* &>/dev/null 2>&1; then
        echo "ERROR: No GPU render nodes in /dev/dri/." >&2
        echo "" >&2
        echo "If running via Claude Code, use: dangerouslyDisableSandbox: true" >&2
        exit 1
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) OUTPUT_DIR="$2"; shift 2 ;;
        -f) FRAMES="$2"; shift 2 ;;
        -t) TIMEOUT="$2"; shift 2 ;;
        -w) WIDTH="${2%%x*}"; HEIGHT="${2##*x}"; shift 2 ;;
        -h|--help) usage ;;
        -*) echo "Unknown option: $1" >&2; usage ;;
        *) break ;;
    esac
done

if [[ $# -eq 0 ]]; then
    echo "Error: no binary specified" >&2
    usage
fi

BINARY="$1"
shift

if [[ ! -x "$BINARY" ]]; then
    echo "Error: $BINARY not found or not executable" >&2
    echo "Build it first: cd native/build && cmake --build . --target $(basename "$BINARY") -j\$(nproc)" >&2
    exit 1
fi

check_gpu

mkdir -p "$OUTPUT_DIR"
BINARY_NAME="$(basename "$BINARY")"

echo "=== DC3 Screenshot ==="
echo "Binary:  $BINARY_NAME"
echo "Output:  $OUTPUT_DIR"
echo "Frames:  $FRAMES"
echo "Size:    ${WIDTH}x${HEIGHT}"
echo "Timeout: ${TIMEOUT}s"
echo ""

case "$BINARY_NAME" in
    dc3-native)
        # dc3-native: headless rendering with GPU readback to PNG
        export MILO_RENDER=1
        export MILO_HEADLESS=1
        export MILO_SCREENSHOT_DIR="$OUTPUT_DIR"
        export MILO_SCREENSHOT_FRAMES="$FRAMES"
        export MILO_WIDTH="$WIDTH"
        export MILO_HEIGHT="$HEIGHT"

        timeout "$TIMEOUT" "$BINARY" "$@" 2>&1 || true

        echo ""
        echo "=== Screenshots ==="
        if ls "$OUTPUT_DIR"/frame_*.png &>/dev/null 2>&1; then
            ls -lh "$OUTPUT_DIR"/frame_*.png
        else
            echo "(no screenshots captured)"
            echo ""
            echo "Troubleshooting:"
            echo "  - Lower frame numbers? Try: -f 10,50"
            echo "  - Longer timeout? Try: -t 60"
            echo "  - Check output above for GPU init errors"
        fi
        ;;

    render-test)
        # render-test: always headless, uses --output flag
        export MILO_RENDER=1
        OUTPUT_FILE="$OUTPUT_DIR/screenshot.png"

        HAS_OUTPUT=false
        for arg in "$@"; do
            [[ "$arg" == "--output" ]] && HAS_OUTPUT=true
        done

        if $HAS_OUTPUT; then
            timeout "$TIMEOUT" "$BINARY" "$@" 2>&1 || true
        else
            timeout "$TIMEOUT" "$BINARY" --output "$OUTPUT_FILE" "$@" 2>&1 || true
        fi

        echo ""
        echo "=== Screenshots ==="
        ls -lh "$OUTPUT_DIR"/*.png 2>/dev/null || echo "(no screenshots)"
        ;;

    milo-viewer)
        # milo-viewer: always headless, uses --screenshot flag
        OUTPUT_FILE="$OUTPUT_DIR/screenshot.png"

        HAS_SCREENSHOT=false
        for arg in "$@"; do
            [[ "$arg" == "--screenshot" ]] && HAS_SCREENSHOT=true
        done

        if $HAS_SCREENSHOT; then
            timeout "$TIMEOUT" "$BINARY" "$@" 2>&1 || true
        else
            timeout "$TIMEOUT" "$BINARY" "$@" --screenshot "$OUTPUT_FILE" --frames 60 2>&1 || true
        fi

        echo ""
        echo "=== Screenshots ==="
        ls -lh "$OUTPUT_DIR"/*.png 2>/dev/null || echo "(no screenshots)"
        ;;

    *)
        echo "Warning: unknown binary '$BINARY_NAME', trying dc3-native mode" >&2
        export MILO_RENDER=1
        export MILO_HEADLESS=1
        export MILO_SCREENSHOT_DIR="$OUTPUT_DIR"
        export MILO_SCREENSHOT_FRAMES="$FRAMES"
        timeout "$TIMEOUT" "$BINARY" "$@" 2>&1 || true
        ;;
esac
