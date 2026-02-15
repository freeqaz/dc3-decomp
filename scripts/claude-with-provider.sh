#!/usr/bin/env bash
# Usage: ./claude-with-provider.sh <provider> [claude args...]
# Example: ./claude-with-provider.sh zai --help

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <provider> [claude args...]"
    echo "Example: $0 zai --help"
    echo ""
    echo "Available providers:"
    for env_file in .env.*; do
        if [ -f "$env_file" ]; then
            provider="${env_file#.env.}"
            echo "  - $provider"
        fi
    done
    exit 1
fi

PROVIDER="$1"
shift

ENV_FILE=".env.$PROVIDER"

if [ ! -f "$ENV_FILE" ]; then
    echo "Error: Environment file '$ENV_FILE' not found"
    exit 1
fi

# Export variables from .env file
set -a
source "$ENV_FILE"
set +a

# Call claude with remaining arguments
exec claude "$@"
