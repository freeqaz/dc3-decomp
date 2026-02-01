#!/usr/bin/env bash
# Setup environment for dc3-decomp development
#
# Usage:
#   source ./setup-env.sh        # Add bin/ to PATH (for interactive shells)
#   eval "$(./setup-env.sh)"     # Alternative sourcing method
#
# For agents/scripts, call bin/objdiff-cli directly:
#   ./bin/objdiff-cli report analyze ...

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"

# If being sourced, modify the current shell
if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
    export PATH="$SCRIPT_DIR/bin:$PATH"
    echo "Added $SCRIPT_DIR/bin to PATH"
    echo "objdiff-cli now points to the extended version"
else
    # If executed directly, output commands for eval
    echo "export PATH=\"$SCRIPT_DIR/bin:\$PATH\""
fi
