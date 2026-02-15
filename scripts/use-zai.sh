#!/usr/bin/env bash
# Source Z.AI credentials and run a command with them
# Usage: source scripts/use-zai.sh
# Or: scripts/use-zai.sh <command> [args...]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ZAI_ENV="$PROJECT_ROOT/.env.zai"

if [ ! -f "$ZAI_ENV" ]; then
    echo "Error: .env.zai not found at $ZAI_ENV"
    return 1 2>/dev/null || exit 1
fi

# Export all variables from .env.zai
set -a
source "$ZAI_ENV"
set +a

echo "✓ Z.AI credentials loaded"
echo "  Base URL: $ZAI_BASE_URL"
echo "  Timeout: ${ZAI_API_TIMEOUT_MS}ms"

# If arguments provided, execute command
if [ $# -gt 0 ]; then
    exec "$@"
fi
