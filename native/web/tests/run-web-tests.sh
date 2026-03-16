#!/usr/bin/env bash
# DC3 Web Port — Test Runner
#
# Wraps web-smoke.js with xvfb-run for WebGPU support.
# Starts the dev server, runs smoke test, captures results.
#
# Usage:
#   native/web/tests/run-web-tests.sh                    # basic smoke test
#   native/web/tests/run-web-tests.sh --verbose           # show all console output
#   native/web/tests/run-web-tests.sh --wait-for "DONE"   # wait for specific log
#   native/web/tests/run-web-tests.sh --no-xvfb           # skip xvfb (headless fallback)
#   native/web/tests/run-web-tests.sh --diagnose-hang     # long timeout + verbose for hang diagnosis
#
# Exit codes match web-smoke.js:
#   0 = success
#   1 = crash / WebGPU failure
#   2 = hang detected
#   3 = infrastructure error

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TEST_SCRIPT="$SCRIPT_DIR/web-smoke.js"
LOG_DIR="$SCRIPT_DIR/../build/test-results"

# Parse our flags (pass the rest through to web-smoke.js)
USE_XVFB=true
EXTRA_ARGS=()
DIAGNOSE=false

for arg in "$@"; do
    case "$arg" in
        --no-xvfb)   USE_XVFB=false ;;
        --diagnose-hang)
            DIAGNOSE=true
            EXTRA_ARGS+=(--verbose --timeout 120 --hang-timeout 15)
            ;;
        *)           EXTRA_ARGS+=("$arg") ;;
    esac
done

# Create log directory
mkdir -p "$LOG_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$LOG_DIR/smoke_${TIMESTAMP}.json"
EXTRA_ARGS+=(--save-logs "$LOG_FILE")

# Check prerequisites
if ! command -v node &>/dev/null; then
    echo "ERROR: node not found" >&2
    exit 3
fi

if ! node -e "require('playwright')" 2>/dev/null; then
    echo "ERROR: playwright not installed. Run: npm install" >&2
    exit 3
fi

if $USE_XVFB && ! command -v xvfb-run &>/dev/null; then
    echo "WARNING: xvfb-run not found, falling back to headless mode" >&2
    echo "  WebGPU may not initialize. Install: sudo pacman -S xorg-server-xvfb" >&2
    USE_XVFB=false
fi

# Check that the WASM build exists
if [ ! -f "$SCRIPT_DIR/../build/dc3-web.js" ]; then
    echo "ERROR: WASM build not found at native/web/build/dc3-web.js" >&2
    echo "  Run: native/web/build.sh" >&2
    exit 3
fi

# Check that assets are configured
if [ -z "${DC3_ASSETS:-}" ]; then
    # Auto-detect common locations
    for candidate in \
        "$REPO_ROOT/orig-assets/extracted" \
        "$REPO_ROOT/../dc3-assets" \
        "$HOME/dc3-assets"; do
        if [ -d "$candidate" ]; then
            export DC3_ASSETS="$candidate"
            break
        fi
    done
fi

if [ -z "${DC3_ASSETS:-}" ]; then
    echo "WARNING: DC3_ASSETS not set and no asset directory found" >&2
    echo "  Set DC3_ASSETS=/path/to/extracted/assets for full testing" >&2
fi

echo "=== DC3 Web Smoke Test ==="
echo "  xvfb:    $USE_XVFB"
echo "  assets:  ${DC3_ASSETS:-<none>}"
echo "  log:     $LOG_FILE"
echo "  args:    ${EXTRA_ARGS[*]}"
echo ""

# Run the test
EXIT_CODE=0
if $USE_XVFB; then
    xvfb-run -a --server-args="-screen 0 1920x1080x24" \
        node "$TEST_SCRIPT" "${EXTRA_ARGS[@]}" || EXIT_CODE=$?
else
    node "$TEST_SCRIPT" "${EXTRA_ARGS[@]}" || EXIT_CODE=$?
fi

# Report
echo ""
case $EXIT_CODE in
    0) echo "RESULT: PASS" ;;
    1) echo "RESULT: CRASH (WebGPU or page crash)" ;;
    2) echo "RESULT: HANG (no console output)" ;;
    3) echo "RESULT: INFRA ERROR" ;;
    *) echo "RESULT: UNKNOWN ($EXIT_CODE)" ;;
esac
echo "Logs: $LOG_FILE"

exit $EXIT_CODE
