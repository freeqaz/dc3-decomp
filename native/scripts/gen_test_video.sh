#!/bin/bash
# Generate a test video pattern for TexMovie pipeline testing.
# Requires ffmpeg.
#
# Usage: ./gen_test_video.sh [output.mp4] [duration] [size]

set -e

OUT="${1:-native/test_assets/test_pattern.mp4}"
DUR="${2:-5}"
SIZE="${3:-512x512}"

mkdir -p "$(dirname "$OUT")"

ffmpeg -y -f lavfi \
    -i "testsrc2=duration=${DUR}:size=${SIZE}:rate=30" \
    -c:v libx264 -pix_fmt yuv420p -preset ultrafast \
    "$OUT" 2>/dev/null

echo "Generated test video: $OUT (${SIZE}, ${DUR}s)"
