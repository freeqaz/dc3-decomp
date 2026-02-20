#!/bin/bash
# Import DC3 XEX into Ghidra with full analysis and map symbols
#
# Usage: ./tools/ghidra/import-xex.sh [project-name]
#
# Creates a Ghidra project with:
# - Full analysis (disassembly, strings, references)
# - All symbols from ham_xbox_r.map (119k+ symbols)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-/opt/ghidra}"
GHIDRA="$GHIDRA_INSTALL_DIR/support/analyzeHeadless"

XEX="$PROJECT_DIR/orig/373307D9/default.xex"
MAP="$PROJECT_DIR/orig/373307D9/ham_xbox_r.map"
PROJECT_LOC="$PROJECT_DIR/ghidra_projects"
PROJECT_NAME="${1:-DC3}"

# Check prerequisites
if [[ ! -x "$GHIDRA" ]]; then
    echo "Error: Ghidra not found at $GHIDRA"
    exit 1
fi

if [[ ! -f "$XEX" ]]; then
    echo "Error: XEX not found at $XEX"
    exit 1
fi

if [[ ! -f "$MAP" ]]; then
    echo "Error: MAP file not found at $MAP"
    exit 1
fi

# Create project directory
mkdir -p "$PROJECT_LOC"

echo "=== DC3 Ghidra Import ==="
echo "XEX: $XEX"
echo "MAP: $MAP"
echo "Project: $PROJECT_LOC/$PROJECT_NAME"
echo ""

# Check if project exists
if [[ -f "$PROJECT_LOC/$PROJECT_NAME.gpr" ]]; then
    echo "Project already exists. Delete it? [y/N]"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        rm -rf "$PROJECT_LOC/$PROJECT_NAME.gpr" "$PROJECT_LOC/$PROJECT_NAME.rep"
    else
        echo "Aborting."
        exit 1
    fi
fi

# Step 1: Import XEX with full analysis
echo "Step 1/2: Importing XEX with full analysis..."
echo "  (This may take several minutes)"
"$GHIDRA" "$PROJECT_LOC" "$PROJECT_NAME" \
    -import "$XEX" \
    -log /tmp/ghidra-dc3-import.log \
    2>&1 | grep -E "(INFO|ERROR|WARN)" | tail -20

echo ""
echo "Step 2/2: Importing map symbols..."
"$GHIDRA" "$PROJECT_LOC" "$PROJECT_NAME" \
    -process default.xex \
    -scriptPath "$SCRIPT_DIR" \
    -postScript ImportMapFile.java "$MAP" \
    -noanalysis \
    2>&1 | grep -E "(INFO|Added|Import complete)" | tail -15

echo ""
echo "=== Import Complete ==="
echo "Project: $PROJECT_LOC/$PROJECT_NAME.gpr"
echo ""
echo "To open in Ghidra GUI:"
echo "  ghidra &"
echo "  File -> Open Project -> $PROJECT_LOC/$PROJECT_NAME.gpr"
echo ""
echo "To search strings from CLI:"
echo "  $GHIDRA $PROJECT_LOC $PROJECT_NAME -process default.xex \\"
echo "    -scriptPath $SCRIPT_DIR -postScript SearchString.java \"pattern\" -noanalysis"
