#!/usr/bin/env bash
# DC3 Native Port — Pose Regression Screenshot Capture
# Captures deterministic pose screenshots for regression comparison.
#
# Usage:
#   ./native/scripts/pose_regression.sh                  # Capture all poses
#   ./native/scripts/pose_regression.sh --capture-with-dump
#   ./native/scripts/pose_regression.sh --update-goldens # Copy captures to golden dir
#   ./native/scripts/pose_regression.sh --compare        # Compare captures against goldens
#   ./native/scripts/pose_regression.sh --compare-pose-json
#
# Prerequisites:
#   - milo-viewer built:  cd native/build && cmake --build . --target milo-viewer
#   - Vulkan ICD available (headless rendering)
#   - Dance Central 3 assets in MILO_LIB path
#
# Each pose entry defines: character, clip file, clip name, beat, camera setup.
# Screenshots are pixel-deterministic for the same build + assets.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VIEWER="$PROJECT_DIR/native/build/milo-viewer"
CAPTURE_DIR="$PROJECT_DIR/archive/screenshots/pose_regression/captures"
GOLDEN_DIR="$PROJECT_DIR/archive/screenshots/pose_regression/goldens"
POSE_COMPARE="$PROJECT_DIR/native/scripts/compare_pose_json.py"
MILO_LIB="${MILO_LIB:-$HOME/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3}"

WIDTH=1280
HEIGHT=720

# Suppress known ASan issues
export ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0"

# Pose entries: "name|char_milo|clips_milo|clip_name|beat|extra_args"
# beat=START means use StartBeat, beat=MID means midpoint
POSES=(
    # T-pose (no clip, rest pose)
    "tpose_aubrey|char/main/dancer/gen/aubrey01.milo_xbox||||"

    # Dance poses at specific beats (--direct-pose uses PoseMeshes)
    "crouch_great_start|char/main/dancer/gen/aubrey01.milo_xbox|char/crowd/anim/gen/female_base.milo_xbox|crouching_great_01|START|--direct-pose"
    "crouch_great_mid|char/main/dancer/gen/aubrey01.milo_xbox|char/crowd/anim/gen/female_base.milo_xbox|crouching_great_01|MID|--direct-pose"
    "stand_bad_start|char/main/dancer/gen/aubrey01.milo_xbox|char/crowd/anim/gen/female_base.milo_xbox|stand_bad_01|START|--direct-pose"
    "stand_bad_mid|char/main/dancer/gen/aubrey01.milo_xbox|char/crowd/anim/gen/female_base.milo_xbox|stand_bad_01|MID|--direct-pose"
)

# --- Argument parsing ---
MODE="capture"
CAPTURE_WITH_DUMP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --capture-with-dump) CAPTURE_WITH_DUMP=1; shift ;;
        --update-goldens) MODE="update" ; shift ;;
        --compare)        MODE="compare"; shift ;;
        --compare-pose-json) MODE="compare_pose_json"; shift ;;
        --help|-h)
            echo "Usage: $0 [--capture-with-dump | --update-goldens | --compare | --compare-pose-json]"
            echo ""
            echo "  (default)          Capture pose screenshots to archive/pose_regression/captures/"
            echo "  --capture-with-dump Capture screenshots and pose JSON sidecars"
            echo "  --update-goldens   Copy current captures to goldens/ as new baselines"
            echo "  --compare          Compare captures against goldens (pixel diff)"
            echo "  --compare-pose-json Compare pose JSON sidecars against goldens (tolerance-based)"
            exit 0
            ;;
        *) shift ;;
    esac
done

# --- Update goldens mode ---
if [ "$MODE" = "update" ]; then
    if [ ! -d "$CAPTURE_DIR" ]; then
        echo "Error: no captures found. Run without --update-goldens first."
        exit 1
    fi
    mkdir -p "$GOLDEN_DIR"
    count=0
    for f in "$CAPTURE_DIR"/*.png "$CAPTURE_DIR"/*.pose.json; do
        [ -f "$f" ] || continue
        cp "$f" "$GOLDEN_DIR/"
        count=$((count + 1))
    done
    echo "Updated $count golden(s) in $GOLDEN_DIR"
    exit 0
fi

# --- Compare mode ---
if [ "$MODE" = "compare" ]; then
    if [ ! -d "$GOLDEN_DIR" ]; then
        echo "Error: no goldens found. Run --update-goldens first."
        exit 1
    fi
    if [ ! -d "$CAPTURE_DIR" ]; then
        echo "Error: no captures found. Run capture first."
        exit 1
    fi

    pass=0
    fail=0
    missing=0

    for golden in "$GOLDEN_DIR"/*.png; do
        [ -f "$golden" ] || continue
        name="$(basename "$golden")"
        capture="$CAPTURE_DIR/$name"

        if [ ! -f "$capture" ]; then
            printf "  %-35s MISSING\n" "$name"
            missing=$((missing + 1))
            continue
        fi

        # Byte-exact comparison (deterministic renderer)
        if cmp -s "$golden" "$capture"; then
            printf "  %-35s PASS\n" "$name"
            pass=$((pass + 1))
        else
            printf "  %-35s DIFF\n" "$name"
            fail=$((fail + 1))
            # If ImageMagick is available, compute pixel diff
            if command -v compare &>/dev/null; then
                diff_png="$CAPTURE_DIR/${name%.png}_diff.png"
                compare "$golden" "$capture" -compose src "$diff_png" 2>/dev/null || true
                if [ -f "$diff_png" ]; then
                    printf "    diff saved: %s\n" "$diff_png"
                fi
            fi
        fi
    done

    total=$((pass + fail + missing))
    echo ""
    echo "=== Regression: $pass/$total passed, $fail diffs, $missing missing ==="
    [ "$fail" -eq 0 ] && [ "$missing" -eq 0 ]
    exit $?
fi

# --- Compare pose JSON mode ---
if [ "$MODE" = "compare_pose_json" ]; then
    if [ ! -x "$POSE_COMPARE" ]; then
        echo "Error: pose compare tool not found at $POSE_COMPARE"
        exit 1
    fi
    if [ ! -d "$GOLDEN_DIR" ]; then
        echo "Error: no goldens found. Run --update-goldens first."
        exit 1
    fi
    if [ ! -d "$CAPTURE_DIR" ]; then
        echo "Error: no captures found. Run capture first."
        exit 1
    fi

    pass=0
    fail=0
    missing=0
    shopt -s nullglob
    json_goldens=("$GOLDEN_DIR"/*.pose.json)
    if [ "${#json_goldens[@]}" -eq 0 ]; then
        echo "Error: no golden pose JSON files found in $GOLDEN_DIR"
        exit 1
    fi

    for golden in "${json_goldens[@]}"; do
        name="$(basename "$golden")"
        capture="$CAPTURE_DIR/$name"
        if [ ! -f "$capture" ]; then
            printf "  %-35s MISSING\n" "$name"
            missing=$((missing + 1))
            continue
        fi

        if "$POSE_COMPARE" "$golden" "$capture" --pos-tol 0.01 --mat-tol 0.01 --beat-tol 0.001 --require-same-clip >/tmp/pose_cmp.out 2>&1; then
            printf "  %-35s PASS\n" "$name"
            pass=$((pass + 1))
        else
            printf "  %-35s DIFF\n" "$name"
            sed -n '1,2p' /tmp/pose_cmp.out | sed 's/^/    /'
            fail=$((fail + 1))
        fi
    done

    total=$((pass + fail + missing))
    echo ""
    echo "=== Pose JSON Regression: $pass/$total passed, $fail diffs, $missing missing ==="
    [ "$fail" -eq 0 ] && [ "$missing" -eq 0 ]
    exit $?
fi

# --- Capture mode ---
if [ ! -x "$VIEWER" ]; then
    echo "Error: milo-viewer not found at $VIEWER"
    echo "  Build it: cd native/build && cmake --build . --target milo-viewer"
    exit 1
fi

mkdir -p "$CAPTURE_DIR"

echo "=== Pose Regression Capture ==="
echo "Output: $CAPTURE_DIR"
echo "Resolution: ${WIDTH}x${HEIGHT}"
if [ "$CAPTURE_WITH_DUMP" -eq 1 ]; then
    echo "Pose dump: enabled (.pose.json sidecars)"
fi
echo ""

success=0
failed=0
skipped=0

for entry in "${POSES[@]}"; do
    IFS='|' read -r name char_rel clips_rel clip_name beat extra <<< "$entry"
    char_path="$MILO_LIB/$char_rel"

    if [ ! -f "$char_path" ]; then
        printf "  %-35s SKIP (char not found)\n" "$name"
        skipped=$((skipped + 1))
        continue
    fi

    png="$CAPTURE_DIR/${name}.png"
    log="$CAPTURE_DIR/${name}.log"
    rm -f "$png" "$log"
    cmd=("$VIEWER" "$char_path" --screenshot "$png" --width "$WIDTH" --height "$HEIGHT")

    # Add clips if specified
    if [ -n "$clips_rel" ]; then
        clips_path="$MILO_LIB/$clips_rel"
        if [ ! -f "$clips_path" ]; then
            printf "  %-35s SKIP (clips not found)\n" "$name"
            skipped=$((skipped + 1))
            continue
        fi
        cmd+=(--clips "$clips_path")
    fi

    # Add clip name if specified
    if [ -n "${clip_name:-}" ]; then
        cmd+=(--clip "$clip_name")
    fi

    # Add beat (resolve START/MID placeholders in viewer)
    if [ -n "${beat:-}" ]; then
        if [ "$beat" = "START" ]; then
            cmd+=(--frame -1)  # viewer uses StartBeat by default
        elif [ "$beat" = "MID" ]; then
            cmd+=(--frame -2)  # signal for midpoint (needs viewer support)
        else
            cmd+=(--frame "$beat")
        fi
    fi

    if [ "$CAPTURE_WITH_DUMP" -eq 1 ]; then
        pose_json="$CAPTURE_DIR/${name}.pose.json"
        rm -f "$pose_json"
        cmd+=(--pose-dump "$pose_json")
        if [ -n "${beat:-}" ]; then
            cmd+=(--pose-dump-beat "$beat")
        fi
    fi

    # Add extra args
    if [ -n "${extra:-}" ]; then
        # shellcheck disable=SC2086
        cmd+=($extra)
    fi

    # Run viewer and retain logs for backend diagnostics
    set +e
    timeout 120 "${cmd[@]}" >"$log" 2>&1
    run_status=$?
    set -e

    if grep -q "GPU = Null backend" "$log"; then
        printf "  %-35s SKIP (Null backend)\n" "$name"
        skipped=$((skipped + 1))
        rm -f "$png"
        [ "$CAPTURE_WITH_DUMP" -eq 1 ] && rm -f "${pose_json:-}"
        continue
    fi

    note=""
    if [ "$run_status" -ne 0 ]; then
        note="; viewer exit ${run_status}"
    fi

    if [ -f "$png" ] && [ -s "$png" ]; then
        size=$(stat -c%s "$png" 2>/dev/null || echo 0)
        if [ "$CAPTURE_WITH_DUMP" -eq 1 ]; then
            pose_json="$CAPTURE_DIR/${name}.pose.json"
            if [ -f "$pose_json" ] && [ -s "$pose_json" ]; then
                printf "  %-35s OK  (%s bytes + pose%s)\n" "$name" "$size" "$note"
                success=$((success + 1))
            else
                printf "  %-35s FAIL (missing pose json)\n" "$name"
                failed=$((failed + 1))
            fi
        else
            printf "  %-35s OK  (%s bytes%s)\n" "$name" "$size" "$note"
            success=$((success + 1))
        fi
    else
        printf "  %-35s FAIL\n" "$name"
        failed=$((failed + 1))
    fi
done

total=${#POSES[@]}
echo ""
echo "=== Capture: $success/$total succeeded, $failed failed, $skipped skipped ==="
echo "Captures in: $CAPTURE_DIR"
echo ""
echo "To establish goldens: $0 --update-goldens"
echo "To check regression:  $0 --compare"
echo "To check pose JSON:   $0 --compare-pose-json"
