#!/bin/bash
# Transcode BINK (.bik) video files to WebM (VP9) for browser playback.
# Requires: ffmpeg with bink decoder support
#
# Usage:
#   ./transcode_bik.sh <input_dir> <output_dir>
#   ./transcode_bik.sh /path/to/extracted/videos /path/to/web/videos
#
# The output directory structure mirrors the input, with .bik → .webm
# This is used by the DC3 web port's WebMovieImpl which rewrites
# movie paths from .bik to .webm at runtime.

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <input_dir> <output_dir>"
    echo ""
    echo "Transcodes all .bik files to .webm (VP9 + Opus)"
    echo "Output structure mirrors input directory."
    exit 1
fi

INPUT_DIR="$1"
OUTPUT_DIR="$2"

if ! command -v ffmpeg &>/dev/null; then
    echo "Error: ffmpeg not found. Install with: sudo pacman -S ffmpeg"
    exit 1
fi

# Check ffmpeg has bink decoder
if ! ffmpeg -decoders 2>/dev/null | grep -q bink; then
    echo "Error: ffmpeg doesn't have bink decoder support"
    exit 1
fi

echo "Transcoding BINK → WebM"
echo "  Input:  $INPUT_DIR"
echo "  Output: $OUTPUT_DIR"
echo ""

count=0
errors=0

find "$INPUT_DIR" -iname "*.bik" -print0 | while IFS= read -r -d '' bik_file; do
    # Compute relative path
    rel_path="${bik_file#$INPUT_DIR/}"
    # Change extension to .webm
    webm_path="$OUTPUT_DIR/${rel_path%.bik}.webm"

    # Create output directory
    mkdir -p "$(dirname "$webm_path")"

    # Skip if output already exists and is newer
    if [ -f "$webm_path" ] && [ "$webm_path" -nt "$bik_file" ]; then
        echo "  SKIP: $rel_path (up to date)"
        continue
    fi

    echo "  TRANSCODE: $rel_path → ${rel_path%.bik}.webm"

    # Transcode with VP9 video + Opus audio
    # -crf 30: reasonable quality for game FMVs
    # -b:v 0: VBR mode (quality-based)
    if ffmpeg -y -i "$bik_file" \
        -c:v libvpx-vp9 -crf 30 -b:v 0 \
        -c:a libopus -b:a 128k \
        -deadline good \
        "$webm_path" 2>/dev/null; then
        count=$((count + 1))
    else
        echo "  ERROR: Failed to transcode $rel_path"
        errors=$((errors + 1))
    fi
done

echo ""
echo "Done. Transcoded $count files, $errors errors."
