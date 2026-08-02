#!/usr/bin/env bash
# dc3-agent-test.sh — Launch dc3-native with default env vars for agent testing.
#
# Usage:
#   ./scripts/dc3-agent-test.sh                          # headless, HTTP on :9090
#   ./scripts/dc3-agent-test.sh --windowed                # with window
#   ./scripts/dc3-agent-test.sh --port 8080               # custom HTTP port
#   ./scripts/dc3-agent-test.sh --script ymca.txt         # with input script
#   ./scripts/dc3-agent-test.sh -- --extra-engine-args    # pass args to dc3-native
#
# All DC3_*/MILO_* env vars can still be overridden from the caller:
#   MILO_MAX_FRAMES=500 ./scripts/dc3-agent-test.sh
#
# Move-scoring is now DEFAULT-ON (this script sets none of the scoring vars, so
# they take their new defaults): DC3_NATIVE_SCORING and DC3_REAL_MOVE_PASSED both
# run by default. With no pose provider the static tracked dummy yields a
# deterministic DetectFrac ~0 (the correct "standing still" signal, not a bug).
# To reproduce the pre-flip baseline: DC3_NATIVE_SCORING=0 DC3_REAL_MOVE_PASSED=0.
# See docs/native/SCORING_ENV_VARS.md for the full parsing rules (=0/false/off/no
# disables) and the deterministic-dummy expectation.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/native/build"
BINARY="$BUILD_DIR/dc3-native"

if [ ! -x "$BINARY" ]; then
    echo "Error: $BINARY not found. Run 'ninja dc3-native' in native/build/ first." >&2
    exit 1
fi

# Defaults (can be overridden by caller's env)
: "${DC3_HTTP:=1}"
: "${DC3_HTTP_PORT:=9090}"
: "${DC3_FAST_BOOT:=1}"
: "${DC3_TEL:=1}"
: "${DC3_SHOW_SPLASH:=0}"
# Frame cap. 0 = unlimited (the engine treats <=0 as "no cap").
#
# This used to default to 100000, which at 30 fps self-terminates a headless run
# after ~55 minutes with exit code 0 — repeatedly mistaken for an unexplained
# crash, because a truncated long capture is otherwise indistinguishable from a
# completed one. Unattended agent runs should be bounded by the caller (or by
# --frames), not by a surprise default. Pass --frames N to reinstate a cap.
: "${MILO_MAX_FRAMES:=0}"

export DC3_HTTP DC3_HTTP_PORT DC3_FAST_BOOT DC3_TEL DC3_SHOW_SPLASH MILO_MAX_FRAMES

# Parse script args
EXTRA_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --windowed)
            # Don't set MILO_HEADLESS — window is the default when GLFW is available
            shift
            ;;
        --headless)
            export MILO_HEADLESS=1
            : "${DC3_FAST_TIME:=1}"
            export DC3_FAST_TIME
            shift
            ;;
        --port)
            export DC3_HTTP_PORT="$2"
            shift 2
            ;;
        --script)
            # Resolve relative to dc3-input-flows/ if not an absolute path
            SCRIPT_PATH="$2"
            if [ "${SCRIPT_PATH:0:1}" != "/" ] && [ ! -f "$SCRIPT_PATH" ]; then
                SCRIPT_PATH="$SCRIPT_DIR/dc3-input-flows/$SCRIPT_PATH"
            fi
            if [ ! -f "$SCRIPT_PATH" ]; then
                echo "Error: input script not found: $2" >&2
                echo "Available scripts in scripts/dc3-input-flows/:" >&2
                ls "$SCRIPT_DIR/dc3-input-flows/"*.txt 2>/dev/null | xargs -n1 basename >&2
                exit 1
            fi
            export MILO_INPUT_SCRIPT="$SCRIPT_PATH"
            shift 2
            ;;
        --frames)
            export MILO_MAX_FRAMES="$2"
            shift 2
            ;;
        --)
            shift
            EXTRA_ARGS+=("$@")
            break
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS] [-- ENGINE_ARGS]"
            echo ""
            echo "Options:"
            echo "  --windowed         Run with a window (default if GLFW available)"
            echo "  --headless         Force headless mode"
            echo "  --port PORT        HTTP server port (default: 9090)"
            echo "  --script FILE      Input script (name or path, e.g. ymca.txt)"
            echo "  --frames N         Max frames before exit (default: 100000)"
            echo "  --help             Show this help"
            echo ""
            echo "Default env vars (override from caller):"
            echo "  DC3_HTTP=1  DC3_HTTP_PORT=9090  DC3_FAST_BOOT=1"
            echo "  DC3_TEL=1  DC3_SHOW_SPLASH=0  MILO_MAX_FRAMES=100000"
            exit 0
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

# Print active config
echo "dc3-agent-test: HTTP=:${DC3_HTTP_PORT} fast_boot=${DC3_FAST_BOOT} tel=${DC3_TEL} max_frames=${MILO_MAX_FRAMES}"
[ -n "${MILO_INPUT_SCRIPT:-}" ] && echo "dc3-agent-test: input_script=${MILO_INPUT_SCRIPT}"

cd "$BUILD_DIR"
exec "$BINARY" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
