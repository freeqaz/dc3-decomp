#!/usr/bin/env bash
# DC3 Native Port — Batch Screenshot Renderer
# Renders .milo_xbox props into archive/screenshots/ as PNG files.
#
# Usage:
#   ./native/scripts/render_screenshots.sh              # Render all default props
#   ./native/scripts/render_screenshots.sh --only disco # Render only entries matching "disco"
#   ./native/scripts/render_screenshots.sh --jobs 4     # Render with 4 parallel workers
#
# Prerequisites:
#   - milo-viewer built:  cd native/build && cmake --build . --target milo-viewer
#   - Vulkan ICD:         /usr/share/vulkan/icd.d/nvidia_icd.json (or equivalent)
#
# To add new props, add entries to the PROPS array below.
# Format: "output_name|path_to_milo_xbox[|azimuth|elevation]"

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VIEWER="$PROJECT_DIR/native/build/milo-viewer"
OUTPUT_DIR="$PROJECT_DIR/archive/screenshots"
MILO_LIB="$HOME/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3"

# Suppress known ASan issues:
# - alloc_dealloc_mismatch: new[]/delete mismatch on compressed vertex cleanup
# - halt_on_error=0: continue past CharBone vtable OOB read during init
# - detect_odr_violation=0: ODR violations from linking full engine
export ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0"

# Props to render: "output_name|relative_path_from_milo_lib[|azimuth_deg|elevation_deg]"
# Azimuth/elevation are optional — defaults used if omitted.
PROPS=(
    # --- Shared Props ---
    # Z-up orbit: az=0 looks from +Y (front), az=90 from +X (right), az=180 from -Y (back)
    "discoball|world/shared/props/gen/discoballsml.milo_xbox"
    "chandelier|world/shared/props/gen/hsp_chandelier.milo_xbox|30|25"
    "wineglass|world/shared/props/gen/dci_wineglass.milo_xbox"
    "microphone|world/shared/props/gen/dci_microphone.milo_xbox"
    "pinball|world/shared/props/gen/pinball_a.milo_xbox|30|15"
    "lamp|world/shared/props/gen/hsp_livingroomlamp.milo_xbox"
    "couch|world/shared/props/gen/hsp_couch.milo_xbox|200|20"
    "fridge|world/shared/props/gen/hsp_fridge.milo_xbox|270|15"
    "stool|world/shared/props/gen/dci_stool.milo_xbox"
    "coffeetable|world/shared/props/gen/dci_coffeetable.milo_xbox|30|25"
    "tv|world/shared/props/gen/hsp_familyroomtv.milo_xbox|20|10"
    "arcade|world/shared/props/gen/arcade_b.milo_xbox|160|15"
    "lockers|world/shared/props/gen/lockers.milo_xbox|20|15"
    "duffelbag|world/shared/props/gen/duffelbag.milo_xbox"
    "big_gulp|world/shared/props/gen/big_gulp.milo_xbox|25|15"

    # --- Crowd Characters (skinned meshes) ---
    "skinned_crowd_f_01|char/crowd/gen/crowd_f_01.milo_xbox"
    "skinned_crowd_m_00s_01|char/crowd/gen/crowd_m_00s_01.milo_xbox"

    # --- Main Dancers (female) ---
    "dancer_angel01|char/main/dancer/gen/angel01.milo_xbox"
    "dancer_aubrey01|char/main/dancer/gen/aubrey01.milo_xbox"
    "dancer_aubrey02|char/main/dancer/gen/aubrey02.milo_xbox"
    "dancer_aubrey03|char/main/dancer/gen/aubrey03.milo_xbox"
    "dancer_aubrey04|char/main/dancer/gen/aubrey04.milo_xbox"
    "dancer_aubrey05|char/main/dancer/gen/aubrey05.milo_xbox"
    "dancer_emilia01|char/main/dancer/gen/emilia01.milo_xbox"
    "dancer_taye01|char/main/dancer/gen/taye01.milo_xbox"
    "dancer_jaryn01|char/main/dancer/gen/jaryn01.milo_xbox"
    "dancer_dare04|char/main/dancer/gen/dare04.milo_xbox"
    "dancer_lima05|char/main/dancer/gen/lima05.milo_xbox"
    "dancer_rasa05|char/main/dancer/gen/rasa05.milo_xbox"

    # --- Venue Scenes ---
    "glitterati_set|world/glitterati/gen/glitterati_set.milo_xbox"
    "glitterati_chairs|world/glitterati/gen/glitterati_chairs.milo_xbox"
)

# Parse arguments
FILTER=""
JOBS=4
WIDTH=2560
HEIGHT=1440
while [ $# -gt 0 ]; do
    case "$1" in
        --only)
            FILTER="${2:-}"
            shift 2 || shift 1
            ;;
        --only=*)
            FILTER="${1#--only=}"
            shift
            ;;
        --jobs)
            JOBS="${2:-4}"
            shift 2 || shift 1
            ;;
        --jobs=*)
            JOBS="${1#--jobs=}"
            shift
            ;;
        --width)
            WIDTH="${2:-2560}"
            shift 2 || shift 1
            ;;
        --height)
            HEIGHT="${2:-1440}"
            shift 2 || shift 1
            ;;
        *)
            shift
            ;;
    esac
done

# Check prerequisites
if [ ! -x "$VIEWER" ]; then
    echo "Error: milo-viewer not found at $VIEWER"
    echo "  Build it: cd native/build && cmake --build . --target milo-viewer"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

echo "=== DC3 Milo Viewer Batch Screenshot ==="
echo "Output: $OUTPUT_DIR"
echo "Resolution: ${WIDTH}x${HEIGHT}"
echo "Jobs: $JOBS parallel"
echo ""

export RENDER_WIDTH="$WIDTH"
export RENDER_HEIGHT="$HEIGHT"

# Worker function: render a single entry
render_one() {
    local entry="$1"
    local viewer="$2"
    local milo_lib="$3"
    local output_dir="$4"

    IFS='|' read -r name relpath cam_az cam_el <<< "$entry"
    local milopath="$milo_lib/$relpath"

    if [ ! -f "$milopath" ]; then
        printf "  %-28s SKIP (not found)\n" "$name"
        return 2
    fi

    local png="$output_dir/${name}.png"

    # Build viewer command with optional camera args
    local cmd=("$viewer" "$milopath" --screenshot "$png" --width "$RENDER_WIDTH" --height "$RENDER_HEIGHT")
    if [ -n "${cam_az:-}" ]; then
        cmd+=(--azimuth "$cam_az")
    fi
    if [ -n "${cam_el:-}" ]; then
        cmd+=(--elevation "$cam_el")
    fi

    # Run viewer with timeout (segfault on cleanup is expected)
    (timeout 120 "${cmd[@]}" >/dev/null 2>&1 || true) 2>/dev/null

    # Check result
    if [ -f "$png" ] && [ -s "$png" ]; then
        local size
        size=$(stat -c%s "$png" 2>/dev/null || echo 0)
        if [ "$size" -gt 1000 ]; then
            printf "  %-28s OK  (%s bytes)\n" "$name" "$size"
            return 0
        else
            printf "  %-28s DARK (%s bytes)\n" "$name" "$size"
            return 0
        fi
    else
        printf "  %-28s FAIL\n" "$name"
        return 1
    fi
}
export -f render_one

# Build filtered list
FILTERED=()
for entry in "${PROPS[@]}"; do
    IFS='|' read -r name _ <<< "$entry"
    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        continue
    fi
    FILTERED+=("$entry")
done

total=${#FILTERED[@]}
if [ "$total" -eq 0 ]; then
    echo "No entries match filter '$FILTER'"
    exit 0
fi

# Run in parallel using background jobs with a semaphore
success=0
failed=0
skipped=0

if [ "$JOBS" -gt 1 ] && [ "$total" -gt 1 ]; then
    # Parallel mode: use a FIFO-based job semaphore
    FIFO="${TMPDIR:-/tmp}/render_sem_$$"
    mkfifo "$FIFO"
    exec 3<>"$FIFO"
    rm -f "$FIFO"

    # Pre-fill semaphore with $JOBS tokens
    for ((i=0; i<JOBS; i++)); do
        echo >&3
    done

    RESULT_DIR="${TMPDIR:-/tmp}/render_results_$$"
    mkdir -p "$RESULT_DIR"

    idx=0
    for entry in "${FILTERED[@]}"; do
        read -u3  # acquire semaphore slot
        (
            render_one "$entry" "$VIEWER" "$MILO_LIB" "$OUTPUT_DIR"
            echo $? > "$RESULT_DIR/$idx"
            echo >&3  # release semaphore slot
        ) &
        idx=$((idx + 1))
    done

    # Wait for all jobs
    wait

    # Tally results
    for ((i=0; i<total; i++)); do
        rc=$(cat "$RESULT_DIR/$i" 2>/dev/null || echo 1)
        case "$rc" in
            0) success=$((success + 1)) ;;
            2) skipped=$((skipped + 1)) ;;
            *) failed=$((failed + 1)) ;;
        esac
    done

    rm -rf "$RESULT_DIR"
    exec 3>&-
else
    # Sequential mode (single job or single entry)
    for entry in "${FILTERED[@]}"; do
        render_one "$entry" "$VIEWER" "$MILO_LIB" "$OUTPUT_DIR"
        rc=$?
        case "$rc" in
            0) success=$((success + 1)) ;;
            2) skipped=$((skipped + 1)) ;;
            *) failed=$((failed + 1)) ;;
        esac
    done
fi

echo ""
echo "=== Results: $success/$total succeeded, $failed failed, $skipped skipped ==="
echo "Screenshots in: $OUTPUT_DIR"
