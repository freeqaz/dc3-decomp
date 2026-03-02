#!/usr/bin/env bash
# DC3 Native Port — Screenshot Crop Tool
# Renders a .milo file and crops specific body regions for close inspection.
#
# Usage:
#   ./native/scripts/crop_screenshot.sh <milo_file> [output_prefix]
#
# Outputs: <prefix>_full.png, <prefix>_head.png, <prefix>_torso.png,
#          <prefix>_legs.png, <prefix>_hands.png
#
# Requires: ImageMagick (magick/convert)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VIEWER="$PROJECT_DIR/native/build/milo-viewer"
OUTPUT_DIR="${TMPDIR:-/tmp}/dc3_crops"

export ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <milo_file> [output_prefix]"
    exit 1
fi

MILO_FILE="$1"
PREFIX="${2:-crop}"

mkdir -p "$OUTPUT_DIR"

FULL="$OUTPUT_DIR/${PREFIX}_full.png"

echo "Rendering $MILO_FILE ..."
(timeout 120 "$VIEWER" "$MILO_FILE" --screenshot "$FULL" 2>&1 || true) | grep -v "^$"

if [ ! -f "$FULL" ] || [ ! -s "$FULL" ]; then
    echo "ERROR: render failed"
    exit 1
fi

# Get image dimensions
W=$(magick identify -format "%w" "$FULL")
H=$(magick identify -format "%h" "$FULL")
echo "Full image: ${W}x${H}"

# Crop regions (percentages of full image)
# Head: top-center 30% width, top 30% height
magick "$FULL" -crop "$(( W * 30 / 100 ))x$(( H * 30 / 100 ))+$(( W * 35 / 100 ))+0" +repage "$OUTPUT_DIR/${PREFIX}_head.png"

# Torso: center 40% width, 25-55% height
magick "$FULL" -crop "$(( W * 40 / 100 ))x$(( H * 30 / 100 ))+$(( W * 30 / 100 ))+$(( H * 25 / 100 ))" +repage "$OUTPUT_DIR/${PREFIX}_torso.png"

# Legs: center 50% width, 50-90% height
magick "$FULL" -crop "$(( W * 50 / 100 ))x$(( H * 40 / 100 ))+$(( W * 25 / 100 ))+$(( H * 50 / 100 ))" +repage "$OUTPUT_DIR/${PREFIX}_legs.png"

# Left hand/arm: left 35% width, 30-60% height
magick "$FULL" -crop "$(( W * 35 / 100 ))x$(( H * 30 / 100 ))+$(( W * 15 / 100 ))+$(( H * 30 / 100 ))" +repage "$OUTPUT_DIR/${PREFIX}_left_arm.png"

# Right hand/arm: right 35% width, 30-60% height
magick "$FULL" -crop "$(( W * 35 / 100 ))x$(( H * 30 / 100 ))+$(( W * 50 / 100 ))+$(( H * 30 / 100 ))" +repage "$OUTPUT_DIR/${PREFIX}_right_arm.png"

echo ""
echo "Crops saved to $OUTPUT_DIR/"
ls -la "$OUTPUT_DIR/${PREFIX}"_*.png
echo ""
echo "Paths for inspection:"
for f in "$OUTPUT_DIR/${PREFIX}"_*.png; do
    echo "  $f"
done
