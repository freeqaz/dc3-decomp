#!/usr/bin/env bash
# DC3 Native Port — Batch Screenshot Renderer
# Renders .milo_xbox props into archive/screenshots/ as PNG files.
#
# Usage:
#   ./native/scripts/render_screenshots.sh              # Render all default props
#   ./native/scripts/render_screenshots.sh --only disco # Render only entries matching "disco"
#
# Prerequisites:
#   - milo-viewer built:  cd native/build && cmake --build . --target milo-viewer
#   - ImageMagick:        magick (for PPM -> PNG conversion)
#   - Vulkan ICD:         /usr/share/vulkan/icd.d/nvidia_icd.json (or equivalent)
#
# To add new props, add entries to the PROPS array below.
# Format: "output_name|path_to_milo_xbox"

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VIEWER="$PROJECT_DIR/native/build/milo-viewer"
OUTPUT_DIR="$PROJECT_DIR/archive/screenshots"
MILO_LIB="$HOME/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3"
TMP_DIR="${TMPDIR:-/tmp/claude-1000}"

# Suppress known ASan issues:
# - alloc_dealloc_mismatch: new[]/delete mismatch on compressed vertex cleanup
# - halt_on_error=0: continue past CharBone vtable OOB read during init
# - detect_odr_violation=0: ODR violations from linking full engine
export ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0"

# Props to render: "output_name|relative_path_from_milo_lib[|azimuth_deg|elevation_deg]"
# Azimuth/elevation are optional — defaults used if omitted.
PROPS=(
    # --- Shared Props ---
    "discoball|world/shared/props/gen/discoballsml.milo_xbox"
    "chandelier|world/shared/props/gen/hsp_chandelier.milo_xbox"
    "wineglass|world/shared/props/gen/dci_wineglass.milo_xbox"
    "microphone|world/shared/props/gen/dci_microphone.milo_xbox"
    "pinball|world/shared/props/gen/pinball_a.milo_xbox|30|15"
    "lamp|world/shared/props/gen/hsp_livingroomlamp.milo_xbox"
    "couch|world/shared/props/gen/hsp_couch.milo_xbox|40|20"
    "fridge|world/shared/props/gen/hsp_fridge.milo_xbox"
    "stool|world/shared/props/gen/dci_stool.milo_xbox"
    "coffeetable|world/shared/props/gen/dci_coffeetable.milo_xbox|30|25"
    "tv|world/shared/props/gen/hsp_familyroomtv.milo_xbox|0|10"
    "arcade|world/shared/props/gen/arcade_b.milo_xbox|35|20"
    "lockers|world/shared/props/gen/lockers.milo_xbox"
    "duffelbag|world/shared/props/gen/duffelbag.milo_xbox"
    "big_gulp|world/shared/props/gen/big_gulp.milo_xbox|25|15"

    # --- Venue Scenes ---
    "glitterati_set|world/glitterati/gen/glitterati_set.milo_xbox"
    "glitterati_chairs|world/glitterati/gen/glitterati_chairs.milo_xbox"
)

# Parse arguments
FILTER=""
for arg in "$@"; do
    case "$arg" in
        --only)
            shift
            FILTER="${1:-}"
            shift || true
            ;;
        --only=*)
            FILTER="${arg#--only=}"
            ;;
    esac
done

# Check prerequisites
if [ ! -x "$VIEWER" ]; then
    echo "Error: milo-viewer not found at $VIEWER"
    echo "  Build it: cd native/build && cmake --build . --target milo-viewer"
    exit 1
fi

if ! command -v magick &>/dev/null; then
    echo "Warning: ImageMagick 'magick' not found — PPM files will not be converted to PNG"
    HAS_MAGICK=0
else
    HAS_MAGICK=1
fi

mkdir -p "$OUTPUT_DIR" "$TMP_DIR"

echo "=== DC3 Milo Viewer Batch Screenshot ==="
echo "Output: $OUTPUT_DIR"
echo ""

total=0
success=0
failed=0
skipped=0

for entry in "${PROPS[@]}"; do
    IFS='|' read -r name relpath cam_az cam_el <<< "$entry"
    milopath="$MILO_LIB/$relpath"

    # Apply filter
    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        continue
    fi

    total=$((total + 1))

    if [ ! -f "$milopath" ]; then
        echo "  SKIP  $name — file not found: $milopath"
        skipped=$((skipped + 1))
        continue
    fi

    ppm="$TMP_DIR/${name}.ppm"
    png="$OUTPUT_DIR/${name}.png"

    printf "  %-20s " "$name"

    # Build viewer command with optional camera args
    VIEWER_CMD=("$VIEWER" "$milopath" --screenshot "$ppm")
    if [ -n "${cam_az:-}" ]; then
        VIEWER_CMD+=(--azimuth "$cam_az")
    fi
    if [ -n "${cam_el:-}" ]; then
        VIEWER_CMD+=(--elevation "$cam_el")
    fi

    # Run viewer with timeout (segfault on cleanup is expected — screenshot saves before it)
    (timeout 120 "${VIEWER_CMD[@]}" >/dev/null 2>&1 || true) 2>/dev/null

    # Check if PPM was written
    if [ -f "$ppm" ] && [ -s "$ppm" ]; then
        if [ "$HAS_MAGICK" -eq 1 ]; then
            magick "$ppm" "$png" 2>/dev/null
            rm -f "$ppm"
            size=$(stat -c%s "$png" 2>/dev/null || echo 0)
            if [ "$size" -gt 1000 ]; then
                echo "OK  ($size bytes)"
            else
                echo "DARK ($size bytes — material too dark?)"
            fi
        else
            mv "$ppm" "$OUTPUT_DIR/${name}.ppm"
            echo "OK  (PPM)"
        fi
        success=$((success + 1))
    else
        echo "FAIL"
        failed=$((failed + 1))
    fi
done

echo ""
echo "=== Results: $success/$total succeeded, $failed failed, $skipped skipped ==="
echo "Screenshots in: $OUTPUT_DIR"
