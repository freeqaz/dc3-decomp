#!/usr/bin/env bash
# Upscale a .bik video to a high-quality _high.webm using AI super-resolution.
# Uses Real-ESRGAN (via video2x) to 4x upscale, then downscale to 1080p.
#
# Requires: video2x, ffmpeg, Vulkan GPU
#
# Usage:
#   ./upscale_video.sh <input.bik> [--gpu 0] [--scale 4] [--target 1080]
#   ./upscale_video.sh orig-assets/extracted/videos/intro.bik
#   ./upscale_video.sh orig-assets/extracted/videos/intro.bik --gpu 1
#
# Output: places <name>_high.webm next to the input file.
# The web port's WebMovieImpl can be configured to prefer _high variants.

set -euo pipefail

# Defaults
GPU_ID=0
SCALE=4
TARGET_HEIGHT=1080
MODEL="realesrgan-plus"
CRF=18
AUDIO_BITRATE="192k"

usage() {
    echo "Usage: $0 <input.bik> [options]"
    echo ""
    echo "Options:"
    echo "  --gpu N         Vulkan GPU index (default: 0)"
    echo "  --scale N       AI upscale factor: 2 or 4 (default: 4)"
    echo "  --target N      Final output height in pixels (default: 1080)"
    echo "  --model NAME    Real-ESRGAN model (default: realesrgan-plus)"
    echo "                  Options: realesrgan-plus, realesr-animevideov3,"
    echo "                           realesrgan-plus-anime, realesr-generalv3"
    echo "  --crf N         VP9 quality (default: 18, lower = better)"
    exit 1
}

if [ $# -lt 1 ] || [[ "$1" == --* ]]; then
    usage
fi

INPUT="$1"
shift

while [ $# -gt 0 ]; do
    case "$1" in
        --gpu)    GPU_ID="$2"; shift 2 ;;
        --scale)  SCALE="$2"; shift 2 ;;
        --target) TARGET_HEIGHT="$2"; shift 2 ;;
        --model)  MODEL="$2"; shift 2 ;;
        --crf)    CRF="$2"; shift 2 ;;
        *)        echo "Unknown option: $1"; usage ;;
    esac
done

if [ ! -f "$INPUT" ]; then
    echo "Error: input file not found: $INPUT"
    exit 1
fi

for cmd in video2x ffmpeg ffprobe; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "Error: $cmd not found"
        exit 1
    fi
done

# Derive output path: intro.bik -> intro_high.webm (next to the input)
INPUT_DIR="$(dirname "$INPUT")"
BASENAME="$(basename "$INPUT")"
NAME="${BASENAME%.*}"
OUTPUT="${INPUT_DIR}/${NAME}_high.webm"

# Temp file for the raw upscaled output (before final encode)
TMPDIR="${TMPDIR:-/tmp}"
UPSCALED_TMP="${TMPDIR}/${NAME}_upscaled_${SCALE}x.mkv"

# Probe input
echo "=== AI Video Upscaler ==="
echo "  Input:    $INPUT"
echo "  Output:   $OUTPUT"
echo "  GPU:      $GPU_ID"
echo "  Model:    $MODEL"
echo "  Scale:    ${SCALE}x -> ${TARGET_HEIGHT}p"
echo "  CRF:      $CRF"
echo ""

# Get source dimensions
SRC_WIDTH=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$INPUT")
SRC_HEIGHT=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$INPUT")
SRC_FPS=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$INPUT")
DURATION=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$INPUT")

UPSCALED_HEIGHT=$((SRC_HEIGHT * SCALE))
UPSCALED_WIDTH=$((SRC_WIDTH * SCALE))
# Compute target width maintaining aspect ratio (round to even)
TARGET_WIDTH=$(python3 -c "w=int(${SRC_WIDTH}*${TARGET_HEIGHT}/${SRC_HEIGHT}); print(w + (w % 2))")

echo "  Source:   ${SRC_WIDTH}x${SRC_HEIGHT} @ ${SRC_FPS} fps, ${DURATION}s"
echo "  Upscale:  ${UPSCALED_WIDTH}x${UPSCALED_HEIGHT} (${SCALE}x AI)"
echo "  Final:    ${TARGET_WIDTH}x${TARGET_HEIGHT}"
echo ""

# Step 1: AI upscale with video2x
echo "[1/2] Running Real-ESRGAN ${SCALE}x upscale on GPU ${GPU_ID}..."
echo "      This will take a while. Model: ${MODEL}"
echo ""

# video2x can't mux Bink audio into the intermediate container,
# so we strip audio during upscale and re-mux from the original later.
video2x \
    -i "$INPUT" \
    -o "$UPSCALED_TMP" \
    -p realesrgan \
    -s "$SCALE" \
    -d "$GPU_ID" \
    --realesrgan-model "$MODEL" \
    --no-copy-audio-streams \
    --no-copy-subtitle-streams \
    -c libx264 \
    -e crf=10 \
    -e preset=ultrafast

echo ""
echo "[2/2] Downscaling to ${TARGET_WIDTH}x${TARGET_HEIGHT} and encoding VP9..."

# Step 2: Downscale to target resolution + encode as high-quality WebM
# Extract audio from original (Bink audio -> Opus)
HAS_AUDIO=$(ffprobe -v error -select_streams a -show_entries stream=codec_type -of csv=p=0 "$INPUT" | head -1)

AUDIO_ARGS=()
if [ -n "$HAS_AUDIO" ]; then
    AUDIO_ARGS=(-i "$INPUT" -map 0:v:0 -map 1:a:0 -c:a libopus -b:a "$AUDIO_BITRATE")
else
    AUDIO_ARGS=(-an)
fi

ffmpeg -y \
    -i "$UPSCALED_TMP" \
    "${AUDIO_ARGS[@]}" \
    -vf "scale=${TARGET_WIDTH}:${TARGET_HEIGHT}:flags=lanczos" \
    -c:v libvpx-vp9 \
    -crf "$CRF" \
    -b:v 0 \
    -deadline good \
    -cpu-used 1 \
    -row-mt 1 \
    -pix_fmt yuv420p \
    "$OUTPUT" 2>/dev/null

# Cleanup temp
rm -f "$UPSCALED_TMP"

# Report
OUTPUT_SIZE=$(du -h "$OUTPUT" | cut -f1)
echo ""
echo "=== Done ==="
echo "  Output: $OUTPUT ($OUTPUT_SIZE)"
echo "  Resolution: ${TARGET_WIDTH}x${TARGET_HEIGHT}"
