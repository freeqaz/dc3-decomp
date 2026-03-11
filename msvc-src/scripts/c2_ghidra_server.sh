#!/usr/bin/env bash
# Start/stop the pyghidra-mcp server for c2.dll analysis.
#
# Usage:
#   msvc-src/scripts/c2_ghidra_server.sh start [--port PORT]
#   msvc-src/scripts/c2_ghidra_server.sh stop
#   msvc-src/scripts/c2_ghidra_server.sh status
#
# The server runs on port 8001 by default (8000 is for the DC3 XEX project).
# Project data is stored under /tmp/claude-1000/c2-analysis/.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
C2_DLL="$REPO_ROOT/build/compilers/X360/16.00.11886.00/c2.dll"
PROJECT_PATH="/tmp/claude-1000/c2-analysis/c2_project"
PIDFILE="/tmp/claude-1000/c2-analysis/server.pid"
LOGFILE="/tmp/claude-1000/c2-analysis/server.log"
DEFAULT_PORT=8001
GHIDRA_DIR="${GHIDRA_INSTALL_DIR:-/opt/ghidra}"
VENV="$REPO_ROOT/venv"

usage() {
    echo "Usage: $0 {start|stop|status} [--port PORT]"
    echo ""
    echo "  start   Launch the pyghidra-mcp server for c2.dll"
    echo "  stop    Stop a running server"
    echo "  status  Check if the server is running"
    echo ""
    echo "Options:"
    echo "  --port PORT  Server port (default: $DEFAULT_PORT)"
    exit 1
}

cmd="${1:-}"
shift || true

PORT=$DEFAULT_PORT
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

case "$cmd" in
start)
    if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "Server already running (PID $(cat "$PIDFILE"))"
        exit 0
    fi

    if [[ ! -f "$C2_DLL" ]]; then
        echo "ERROR: c2.dll not found at $C2_DLL"
        exit 1
    fi

    if [[ ! -d "$GHIDRA_DIR" ]]; then
        echo "ERROR: Ghidra not found at $GHIDRA_DIR"
        echo "Set GHIDRA_INSTALL_DIR to the Ghidra installation directory"
        exit 1
    fi

    mkdir -p "$(dirname "$PIDFILE")"

    echo "Starting pyghidra-mcp for c2.dll on port $PORT..."
    echo "  Ghidra: $GHIDRA_DIR"
    echo "  Binary: $C2_DLL"
    echo "  Project: $PROJECT_PATH"
    echo "  Log: $LOGFILE"

    GHIDRA_INSTALL_DIR="$GHIDRA_DIR" \
    GHIDRA_USER_HOME="/tmp/claude-1000/ghidra_user" \
    "$VENV/bin/pyghidra-mcp" \
        --transport sse \
        --port "$PORT" \
        --project-path "$PROJECT_PATH" \
        --wait-for-analysis \
        --no-symbols \
        "$C2_DLL" \
        >"$LOGFILE" 2>&1 &

    echo $! > "$PIDFILE"
    echo "Server starting (PID $!)..."
    echo "Waiting for analysis to complete (may take 1-3 minutes)..."

    # Wait for the server to be ready
    for i in $(seq 1 120); do
        if curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/sse" 2>/dev/null | grep -q "200"; then
            echo "Server ready at http://127.0.0.1:$PORT"
            exit 0
        fi
        sleep 2
    done

    echo "WARNING: Server did not become ready within 4 minutes."
    echo "Check $LOGFILE for details."
    ;;

stop)
    if [[ -f "$PIDFILE" ]]; then
        PID=$(cat "$PIDFILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "Stopping server (PID $PID)..."
            kill "$PID"
            rm -f "$PIDFILE"
            echo "Stopped."
        else
            echo "Server not running (stale pidfile)."
            rm -f "$PIDFILE"
        fi
    else
        echo "No pidfile found. Server not running?"
    fi
    ;;

status)
    if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "Server running (PID $(cat "$PIDFILE"))"
        # Try to check port
        if curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/sse" 2>/dev/null | grep -q "200"; then
            echo "SSE endpoint responding on port $PORT"
        else
            echo "SSE endpoint not yet responding on port $PORT (still initializing?)"
        fi
    else
        echo "Server not running."
    fi
    ;;

*)
    usage
    ;;
esac
