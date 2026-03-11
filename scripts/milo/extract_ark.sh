#!/usr/bin/env bash
# Extract DC3 ark archive to a directory using arkhelper.
# Usage: extract_ark.sh [output_dir] [options]
#   output_dir     Where to extract (default: orig-assets/extracted)
#   --dta          Convert .dtb scripts to .dta text
#   --inflate      Decompress .milo_xbox archives
#   --all          Extract everything (implies --dta --inflate)
#   --hdr PATH     Path to .hdr file (default: orig-assets/gen/main_xbox.hdr)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Locate arkhelper
ARKHELPER=""
for candidate in \
    "$REPO_ROOT/../milo-executable-library/dance-central-3-deluxe/dependencies/linux/arkhelper" \
    "$(command -v arkhelper 2>/dev/null || true)"; do
    if [[ -x "$candidate" ]]; then
        ARKHELPER="$candidate"
        break
    fi
done

if [[ -z "$ARKHELPER" ]]; then
    echo "ERROR: arkhelper not found." >&2
    echo "Expected at: ../milo-executable-library/dance-central-3-deluxe/dependencies/linux/arkhelper" >&2
    exit 1
fi

# Defaults
HDR_PATH="$REPO_ROOT/orig-assets/gen/main_xbox.hdr"
OUTPUT_DIR=""
CONVERT_SCRIPTS=""
INFLATE_MILOS=""
EXTRACT_ALL=""

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dta)          CONVERT_SCRIPTS="-s"; shift ;;
        --inflate)      INFLATE_MILOS="-m"; shift ;;
        --all)          EXTRACT_ALL="-a"; shift ;;
        --hdr)          HDR_PATH="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: extract_ark.sh [output_dir] [--dta] [--inflate] [--all] [--hdr PATH]"
            echo ""
            echo "  output_dir   Where to extract (default: orig-assets/extracted)"
            echo "  --dta        Convert .dtb scripts to .dta text"
            echo "  --inflate    Decompress .milo_xbox archives"
            echo "  --all        Extract everything (implies --dta --inflate)"
            echo "  --hdr PATH   Path to .hdr file (default: orig-assets/gen/main_xbox.hdr)"
            exit 0
            ;;
        *)
            if [[ -z "$OUTPUT_DIR" ]]; then
                OUTPUT_DIR="$1"
            else
                echo "ERROR: Unknown argument: $1" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

# Default output directory
if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="$REPO_ROOT/orig-assets/extracted"
fi

# Verify hdr exists
if [[ ! -f "$HDR_PATH" ]]; then
    echo "ERROR: HDR file not found: $HDR_PATH" >&2
    exit 1
fi

# Build arkhelper command
CMD=("$ARKHELPER" ark2dir)
[[ -n "$CONVERT_SCRIPTS" ]] && CMD+=("$CONVERT_SCRIPTS")
[[ -n "$INFLATE_MILOS" ]] && CMD+=("$INFLATE_MILOS")
[[ -n "$EXTRACT_ALL" ]] && CMD+=("$EXTRACT_ALL")
CMD+=("$HDR_PATH" "$OUTPUT_DIR")

echo "Extracting ark..."
echo "  HDR:    $HDR_PATH"
echo "  Output: $OUTPUT_DIR"
echo "  Flags:  ${CONVERT_SCRIPTS:-(none)} ${INFLATE_MILOS:-(none)} ${EXTRACT_ALL:-(none)}"
echo "  Command: ${CMD[*]}"
echo ""

mkdir -p "$OUTPUT_DIR"
"${CMD[@]}"

echo ""
echo "Done. Extracted to: $OUTPUT_DIR"

# Quick summary
MILO_COUNT=$(find "$OUTPUT_DIR" -name "*.milo_xbox" 2>/dev/null | wc -l)
DTA_COUNT=$(find "$OUTPUT_DIR" -name "*.dta" 2>/dev/null | wc -l)
DTB_COUNT=$(find "$OUTPUT_DIR" -name "*.dtb" 2>/dev/null | wc -l)
echo "  .milo_xbox files: $MILO_COUNT"
echo "  .dta files:       $DTA_COUNT"
echo "  .dtb files:       $DTB_COUNT"
