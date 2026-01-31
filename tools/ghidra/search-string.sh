#!/bin/bash
# Search for strings in DC3 Ghidra project
#
# Usage: ./tools/ghidra/search-string.sh <pattern>
#
# Examples:
#   ./tools/ghidra/search-string.sh dance
#   ./tools/ghidra/search-string.sh "App.cpp"

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
GHIDRA="/opt/ghidra/support/analyzeHeadless"

PROJECT_LOC="$PROJECT_DIR/ghidra_projects"
PROJECT_NAME="DC3"

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <pattern>"
    echo ""
    echo "Examples:"
    echo "  $0 dance"
    echo "  $0 \"App.cpp\""
    exit 1
fi

PATTERN="$1"

if [[ ! -f "$PROJECT_LOC/$PROJECT_NAME.gpr" ]]; then
    echo "Error: Project not found. Run import-xex.sh first."
    exit 1
fi

"$GHIDRA" "$PROJECT_LOC" "$PROJECT_NAME" \
    -process default.xex \
    -scriptPath "$SCRIPT_DIR" \
    -postScript SearchString.java "$PATTERN" \
    -noanalysis \
    2>&1 | grep -E "(Searching|===|Found|  [0-9a-f]{8}:)"
