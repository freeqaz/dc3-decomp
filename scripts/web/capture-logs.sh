#!/usr/bin/env bash
# Capture verbose logs from the DC3 web port running in headless Chrome.
#
# Usage:
#   scripts/web/capture-logs.sh                    # 90s capture, auto xvfb
#   scripts/web/capture-logs.sh --duration 120     # 120s capture
#   scripts/web/capture-logs.sh --port 8421        # custom server port
#   scripts/web/capture-logs.sh --no-xvfb          # skip xvfb (use existing display)
#   scripts/web/capture-logs.sh --output /tmp/out  # custom output path
#   scripts/web/capture-logs.sh --silence 15       # CDP hang detection threshold
#
# Prerequisites:
#   - Server running: python3 native/web/server.py --port 8420
#   - Packages: xorg-server-xvfb chromium, npm install playwright
#
# Output: timestamped log file + summary to stdout
set -euo pipefail

DURATION=90
PORT=8420
USE_XVFB=true
SILENCE=20
OUTPUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --duration)  DURATION="$2"; shift 2 ;;
        --port)      PORT="$2"; shift 2 ;;
        --no-xvfb)   USE_XVFB=false; shift ;;
        --silence)   SILENCE="$2"; shift 2 ;;
        --output|-o) OUTPUT="$2"; shift 2 ;;
        --help|-h)
            sed -n '2,/^set -/p' "$0" | head -n -1
            exit 0 ;;
        *) echo "Unknown flag: $1"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
CDP_SCRIPT="$SCRIPT_DIR/native/web/tests/cdp-debugger-break.js"

if [[ ! -f "$CDP_SCRIPT" ]]; then
    echo "Error: CDP script not found at $CDP_SCRIPT"
    exit 1
fi

# Check server is running
if ! curl -s "http://localhost:$PORT" > /dev/null 2>&1; then
    echo "Error: web server not running on port $PORT"
    echo "Start it: python3 native/web/server.py --port $PORT"
    exit 1
fi

# Generate output path
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
if [[ -z "$OUTPUT" ]]; then
    OUTPUT="/tmp/dc3-web-log-${TIMESTAMP}.txt"
fi

echo "DC3 Web Log Capture"
echo "  Duration:  ${DURATION}s"
echo "  Port:      $PORT"
echo "  Silence:   ${SILENCE}s (CDP hang threshold)"
echo "  Output:    $OUTPUT"
echo ""

# Build the command
CMD="node $CDP_SCRIPT --no-server --port $PORT --silence $SILENCE --verbose"
if $USE_XVFB; then
    CMD="xvfb-run -a --server-args=\"-screen 0 1920x1080x24\" $CMD"
fi

# Run with timeout
echo "Capturing..."
eval "$CMD" > "$OUTPUT" 2>&1 &
PID=$!

# Wait for duration or process exit
ELAPSED=0
while kill -0 $PID 2>/dev/null && [[ $ELAPSED -lt $DURATION ]]; do
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done
kill $PID 2>/dev/null || true
wait $PID 2>/dev/null || true

LINES=$(wc -l < "$OUTPUT")
echo ""
echo "Captured $LINES lines to $OUTPUT"
echo ""

# Print summary
echo "=== ERRORS ==="
grep -c "PAGE_ERROR\|function signature\|memory access\|RuntimeError" "$OUTPUT" 2>/dev/null | xargs -I{} echo "  WASM errors: {}"
grep -c "\[stub\]" "$OUTPUT" 2>/dev/null | xargs -I{} echo "  Missing stubs: {}"
echo ""

echo "=== TIMELINE ==="
grep -E "frame [0-9]+$|StartGame|game_stage|IsLoaded.*Done|MoveGraph|rekick|LoadCharacters|AllCharsLoaded|PAGE_ERROR|function signature|Safety timeout|CALL STACK" "$OUTPUT" | head -20
echo ""

echo "=== LAST 5 LINES ==="
tail -5 "$OUTPUT"
