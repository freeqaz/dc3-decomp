#!/bin/bash
# pyghidra-mcp HTTP service manager
#
# Usage:
#   ./tools/ghidra/pyghidra-service.sh start   # Start the service
#   ./tools/ghidra/pyghidra-service.sh stop    # Stop the service
#   ./tools/ghidra/pyghidra-service.sh status  # Check status
#   ./tools/ghidra/pyghidra-service.sh restart # Restart
#   ./tools/ghidra/pyghidra-service.sh logs    # View logs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

PORT=8000
HOST=127.0.0.1
PROJECT_PATH="$PROJECT_DIR/ghidra_projects/DC3/DC3"
XEX_PATH="$PROJECT_DIR/orig/373307D9/default.xex"
MAP_FILE="$PROJECT_DIR/orig/373307D9/ham_xbox_r.map"
PYGHIDRA_MCP="$HOME/code/milohax/pyghidra-mcp"
PIDFILE="/tmp/claude/pyghidra-mcp-dc3.pid"
LOGFILE="/tmp/claude/pyghidra-mcp-dc3.log"

export JAVA_HOME="/usr/lib/jvm/java-17-openjdk"
# Use VMX128-enabled Ghidra build (not stock /opt/ghidra)
export GHIDRA_INSTALL_DIR="$HOME/code/milohax/vmx128-research/ghidra-test/ghidra_12.0_DEV"
# Use writable temp directory for Ghidra user home (avoids read-only filesystem issues)
export GHIDRA_USER_HOME="/tmp/claude/ghidra_user"

cmd_start() {
    if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "Service already running (PID: $(cat "$PIDFILE"))"
        return 0
    fi

    # Clear any stale locks
    rm -f "$PROJECT_PATH"/*.lock* 2>/dev/null || true

    # Ensure log directory exists
    mkdir -p "$(dirname "$LOGFILE")" 2>/dev/null || true

    echo "Starting pyghidra-mcp service..."
    echo "  Project: $PROJECT_PATH"
    echo "  Binary: $XEX_PATH"
    echo "  Map file: $MAP_FILE"
    echo "  Log: $LOGFILE"

    # Start service using upstream pyghidra-mcp repo (via uv run with pinned Python)
    # --wait-for-analysis ensures map symbols are applied after the binary is fully analyzed
    nohup uv run --python 3.10 --project "$PYGHIDRA_MCP" pyghidra-mcp \
        --transport streamable-http \
        --project-path "$PROJECT_PATH" \
        --map-file "$MAP_FILE" \
        --wait-for-analysis \
        --cache-dir "$PROJECT_DIR" \
        --log-file "$LOGFILE" \
        "$XEX_PATH" \
        > "$LOGFILE" 2>&1 &

    PID=$!
    echo $PID > "$PIDFILE"
    echo "Started with PID: $PID"
    echo ""
    echo "Server is starting in the background..."
    echo "Check logs with: $0 logs"

    sleep 5
    if ps -p $PID > /dev/null 2>&1; then
        echo "Service process is running (PID: $PID)"
        return 0
    else
        echo "Error: Service process exited unexpectedly"
        tail -20 "$LOGFILE"
        return 1
    fi
}

cmd_stop() {
    if [[ -f "$PIDFILE" ]]; then
        PID=$(cat "$PIDFILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "Stopping service (PID: $PID)..."
            kill "$PID"
            rm -f "$PIDFILE"
            echo "Stopped."
        else
            echo "PID file exists but process not running. Cleaning up."
            rm -f "$PIDFILE"
        fi
    else
        # Try to find and kill any running instance
        PIDS=$(pgrep -f "pyghidra-mcp.*$PORT" || true)
        if [[ -n "$PIDS" ]]; then
            echo "Killing pyghidra-mcp processes: $PIDS"
            kill $PIDS 2>/dev/null || true
        else
            echo "Service not running."
        fi
    fi
}

cmd_status() {
    if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "Service running (PID: $(cat "$PIDFILE"))"
        echo "URL: http://$HOST:$PORT/mcp/v1"
        
        # Check if responsive
        if curl -s "http://$HOST:$PORT/mcp/v1" > /dev/null 2>&1; then
            echo "Status: Ready"
        else
            echo "Status: Starting/Not responding"
        fi
    else
        echo "Service not running."
        return 1
    fi
}

cmd_logs() {
    if [[ -f "$LOGFILE" ]]; then
        tail -f "$LOGFILE"
    else
        echo "No log file found at $LOGFILE"
    fi
}

cmd_restart() {
    cmd_stop
    sleep 2
    cmd_start
}

cmd_diagnose() {
    echo "Running Ghidra service diagnostics..."
    echo ""
    uv run --python 3.10 --project "$PYGHIDRA_MCP" pyghidra-mcp --diagnose
}

case "${1:-}" in
    start)      cmd_start ;;
    stop)       cmd_stop ;;
    status)     cmd_status ;;
    restart)    cmd_restart ;;
    logs)       cmd_logs ;;
    diagnose)   cmd_diagnose ;;
    *)
        echo "Usage: $0 {start|stop|status|restart|logs|diagnose}"
        exit 1
        ;;
esac
