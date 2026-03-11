#!/usr/bin/env bash
# RenderDoc capture wrapper for DC3 native port.
#
# Captures a frame using RenderDoc's renderdoccmd. Requires a swapchain
# (vkQueuePresentKHR) — works with windowed apps like milo-viewer but
# NOT with headless render-test. For headless capture, use capture.sh
# (GFXReconstruct) instead.
#
# Usage:
#   scripts/gpu/rdc_capture.sh [options] <binary> [binary-args...]
#
# Options:
#   -o <path>       Output .rdc file (default: /tmp/gpu_capture.rdc)
#   -d <seconds>    Delay before triggering capture (default: 2)
#   --no-wait       Don't wait for the app to exit
#   -h              Show this help
#
# Examples:
#   # Capture milo-viewer (has a window/swapchain)
#   scripts/gpu/rdc_capture.sh -o /tmp/viewer.rdc native/build/milo-viewer path/to/scene.milo_xbox
#
#   # Capture with delay
#   scripts/gpu/rdc_capture.sh -d 5 -o /tmp/viewer.rdc native/build/milo-viewer scene.milo_xbox
#
# Note: For headless binaries (render-test), use scripts/gpu/capture.sh instead.
#
# Source: ../gpu/renderdoc/ (https://github.com/baldurk/renderdoc)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GPU_DIR="$(cd "$REPO_ROOT/../gpu" && pwd)"

RENDERDOCCMD="$GPU_DIR/renderdoc/build/bin/renderdoccmd"

# Defaults
OUTPUT="/tmp/gpu_capture.rdc"
DELAY="2"
WAIT="--wait-for-exit"

usage() {
    sed -n '/^# Usage:/,/^# Source:/p' "$0" | sed 's/^# \?//'
    exit 0
}

# Parse options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) OUTPUT="$2"; shift 2 ;;
        -d) DELAY="$2"; shift 2 ;;
        --no-wait) WAIT=""; shift ;;
        -h|--help) usage ;;
        -*) echo "Unknown option: $1" >&2; usage ;;
        *) break ;;
    esac
done

if [[ $# -eq 0 ]]; then
    echo "Error: no binary specified" >&2
    usage
fi

# Verify renderdoccmd is built
if [[ ! -x "$RENDERDOCCMD" ]]; then
    echo "Error: renderdoccmd not found at $RENDERDOCCMD" >&2
    echo "Build it with:" >&2
    echo "  cd $GPU_DIR/renderdoc" >&2
    echo "  cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QRENDERDOC=OFF -Bbuild -H." >&2
    echo "  cmake --build build -j\$(nproc)" >&2
    exit 1
fi

# Check Vulkan layer registration
if ! "$RENDERDOCCMD" vulkanlayer --explain 2>&1 | grep -q "correctly registered"; then
    echo "Warning: RenderDoc Vulkan layer not registered. Registering for current user..." >&2
    "$RENDERDOCCMD" vulkanlayer --register --user 2>/dev/null || true
fi

echo "=== RenderDoc Capture ===" >&2
echo "Binary:  $1" >&2
echo "Output:  $OUTPUT" >&2
echo "Delay:   ${DELAY}s" >&2
echo "" >&2

export ENABLE_VULKAN_RENDERDOC_CAPTURE=1

# Run with RenderDoc injection
$RENDERDOCCMD capture \
    $WAIT \
    -d "$DELAY" \
    -c "$OUTPUT" \
    "$@"

echo "" >&2
if [[ -f "$OUTPUT" ]]; then
    echo "=== Capture saved ===" >&2
    echo "  $OUTPUT ($(du -h "$OUTPUT" | cut -f1))" >&2
    echo "$OUTPUT"
else
    echo "=== No capture file produced ===" >&2
    echo "RenderDoc requires a swapchain (vkQueuePresentKHR)." >&2
    echo "For headless apps, use scripts/gpu/capture.sh (GFXReconstruct) instead." >&2
    exit 1
fi
