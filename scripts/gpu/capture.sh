#!/usr/bin/env bash
# GFXReconstruct Vulkan capture wrapper for DC3 native port.
#
# Captures Vulkan API calls from a native port binary using the
# GFXReconstruct layer. Works headless and with Xvfb virtual display.
#
# Usage:
#   scripts/gpu/capture.sh [options] <binary> [binary-args...]
#
# Options:
#   -o <path>       Output capture file (default: /tmp/gpu_capture.gfxr)
#   -f <frames>     Frame range to capture (e.g. "100-200"). Requires Xvfb or display.
#   -s <submits>    Queue submit range (e.g. "100-200"). Works headless.
#   -q              Quit after captured frames (requires -f, uses Xvfb)
#   -t <seconds>    Kill the app after N seconds (for long-running apps like dc3-native)
#   -x              Use Xvfb virtual display (enables frame counting + swapchain)
#   -c <type>       Compression: LZ4, ZSTD (default), ZLIB, NONE
#   -l <level>      Log level: debug, info, warning (default), error
#   -h              Show this help
#
# Examples:
#   # Capture render-test (headless, exits on its own)
#   scripts/gpu/capture.sh native/build/render-test --output /tmp/out.png --test solid_quads
#
#   # Capture 60 seconds of dc3-native (headless, queue submit trimming)
#   scripts/gpu/capture.sh -t 60 native/build/dc3-native
#
#   # Capture dc3-native frames 100-200 with Xvfb (frame-accurate, auto-quit)
#   scripts/gpu/capture.sh -x -f 100-200 -q native/build/dc3-native
#
#   # Capture dc3-native for 60s with Xvfb (windowed mode, frame counting)
#   scripts/gpu/capture.sh -x -t 60 native/build/dc3-native
#
#   # Capture queue submits 50-150 headless (no display needed)
#   scripts/gpu/capture.sh -s 50-150 -t 30 native/build/dc3-native
#
# Source: ../gpu/gfxreconstruct/ (https://github.com/LunarG/gfxreconstruct)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GPU_DIR="$(cd "$REPO_ROOT/../gpu" && pwd)"

GFXR_LAYER="$GPU_DIR/gfxreconstruct/build/layer"
GFXR_LAYER_LIB="$GFXR_LAYER/libVkLayer_gfxreconstruct.so"

# Defaults — use /tmp/gpu_captures/ to avoid filling sandboxed tmpdir
CAPTURE_DIR="/tmp/gpu_captures"
OUTPUT="$CAPTURE_DIR/capture.gfxr"
FRAMES=""
SUBMITS=""
QUIT_AFTER=""
TIMEOUT=""
USE_XVFB=""
COMPRESSION="ZSTD"
LOG_LEVEL="warning"

usage() {
    sed -n '/^# Usage:/,/^# Source:/p' "$0" | sed 's/^# \?//'
    exit 0
}

# Parse options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) OUTPUT="$2"; shift 2 ;;
        -f) FRAMES="$2"; shift 2 ;;
        -s) SUBMITS="$2"; shift 2 ;;
        -q) QUIT_AFTER="true"; shift ;;
        -t) TIMEOUT="$2"; shift 2 ;;
        -x) USE_XVFB="true"; shift ;;
        -c) COMPRESSION="$2"; shift 2 ;;
        -l) LOG_LEVEL="$2"; shift 2 ;;
        -h|--help) usage ;;
        -*) echo "Unknown option: $1" >&2; usage ;;
        *) break ;;
    esac
done

if [[ $# -eq 0 ]]; then
    echo "Error: no binary specified" >&2
    usage
fi

# Auto-enable Xvfb when using frame ranges (needs swapchain for frame counting)
if [[ -n "$FRAMES" && -z "$USE_XVFB" && -z "$DISPLAY" ]]; then
    if command -v xvfb-run &>/dev/null; then
        echo "Note: auto-enabling Xvfb for frame-based capture (no \$DISPLAY)" >&2
        USE_XVFB="true"
    else
        echo "Warning: -f (frame range) requires a display or Xvfb. Install xorg-server-xvfb." >&2
    fi
fi

# Verify layer is built
if [[ ! -f "$GFXR_LAYER_LIB" ]]; then
    echo "Error: GFXReconstruct layer not built at $GFXR_LAYER_LIB" >&2
    echo "Build it with:" >&2
    echo "  cd $GPU_DIR/gfxreconstruct" >&2
    echo "  git submodule update --init" >&2
    echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGFXRECON_ENABLE_OPENXR=OFF" >&2
    echo "  cmake --build build -j\$(nproc)" >&2
    exit 1
fi

# Build env vars
export VK_LAYER_PATH="$GFXR_LAYER"
export VK_INSTANCE_LAYERS="VK_LAYER_LUNARG_gfxreconstruct"
export GFXRECON_CAPTURE_FILE="$OUTPUT"
export GFXRECON_CAPTURE_COMPRESSION_TYPE="$COMPRESSION"
export GFXRECON_LOG_LEVEL="$LOG_LEVEL"

if [[ -n "$FRAMES" ]]; then
    export GFXRECON_CAPTURE_FRAMES="$FRAMES"
fi
if [[ -n "$SUBMITS" ]]; then
    export GFXRECON_CAPTURE_QUEUE_SUBMITS="$SUBMITS"
fi
if [[ -n "$QUIT_AFTER" ]]; then
    export GFXRECON_QUIT_AFTER_CAPTURE_FRAMES="true"
fi

# Ensure capture directory exists
mkdir -p "$(dirname "$OUTPUT")" 2>/dev/null || true

echo "=== GFXReconstruct Capture ===" >&2
echo "Binary:  $1" >&2
echo "Output:  $OUTPUT" >&2
[[ -n "$FRAMES" ]] && echo "Frames:  $FRAMES" >&2
[[ -n "$SUBMITS" ]] && echo "Submits: $SUBMITS" >&2
[[ -n "$TIMEOUT" ]] && echo "Timeout: ${TIMEOUT}s" >&2
[[ -n "$USE_XVFB" ]] && echo "Display: Xvfb (virtual)" >&2
echo "Compress: $COMPRESSION" >&2

# Warn about capture size for long runs
if [[ -n "$TIMEOUT" && -z "$FRAMES" && -z "$SUBMITS" ]]; then
    RATE_MB=8  # ~8 MB/s with ZSTD compression (measured from dc3-native)
    EST_MB=$((TIMEOUT * RATE_MB))
    if [[ $EST_MB -gt 1000 ]]; then
        echo "WARNING: estimated capture ~${EST_MB}MB (${TIMEOUT}s * ~${RATE_MB}MB/s)." >&2
        echo "  Consider using -f (frame range) or -s (submit range) to trim." >&2
    fi
fi
echo "" >&2

# Build command with optional timeout and Xvfb wrappers
CMD=("$@")
if [[ -n "$TIMEOUT" ]]; then
    CMD=(timeout "$TIMEOUT" "${CMD[@]}")
fi
if [[ -n "$USE_XVFB" ]]; then
    CMD=(xvfb-run -a -s "-screen 0 1280x720x24" "${CMD[@]}")
fi

# Run the binary with the capture layer active (redirect app stdout to stderr
# so only the capture path goes to stdout for piping)
"${CMD[@]}" >&2 || true  # don't fail on timeout exit code

# Report result
echo "" >&2
CAPFILES=$(ls -t "${OUTPUT%.*}"*.gfxr 2>/dev/null | head -5)
if [[ -n "$CAPFILES" ]]; then
    echo "=== Capture complete ===" >&2
    for f in $CAPFILES; do
        echo "  $f ($(du -h "$f" | cut -f1))" >&2
    done
    # Print the most recent capture path to stdout for piping
    echo "$CAPFILES" | head -1
else
    echo "=== No capture file produced ===" >&2
    echo "This can happen with headless apps. Check that the layer loaded (look for '[gfxrecon]' in output above)." >&2
    exit 1
fi
