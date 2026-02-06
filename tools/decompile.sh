#!/bin/bash
# tools/decompile.sh - Combined m2c decompilation workflow
#
# Automates the pipeline for decompiling a function from the target binary:
# 1. Optionally generate type context from Ghidra
# 2. Get target binary disassembly from objdiff
# 3. Convert to m2c format
# 4. Run m2c with type context
# 5. Output decompiled code
#
# Usage:
#   tools/decompile.sh "CharClip::SetFlags"
#   tools/decompile.sh "Object::Load" -u default/system/char/Character
#   tools/decompile.sh "Game::Poll" -o decompiled.c
#   tools/decompile.sh "CharMirror::Load" --context
#   tools/decompile.sh "CharClip::SetFlags" -v

set -e

# =============================================================================
# Configuration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OBJDIFF_CLI="$PROJECT_DIR/bin/objdiff-cli"
OBJDIFF_TO_M2C="$PROJECT_DIR/tools/objdiff_to_m2c.py"
EXPORT_TYPES="$PROJECT_DIR/tools/ghidra/export_types.py"
M2C="$HOME/code/milohax/m2c/m2c.py"

# Temporary files
TMPDIR="${TMPDIR:-/tmp/claude}"
mkdir -p "$TMPDIR"
TMP_JSON="$TMPDIR/decompile_$$.json"
TMP_ASM="$TMPDIR/decompile_$$.s"
TMP_CONTEXT="$TMPDIR/decompile_context_$$.h"

# =============================================================================
# Helper Functions
# =============================================================================

usage() {
    cat << 'EOF'
Usage: decompile.sh [OPTIONS] <function_name>

Decompile a function from the target binary using m2c.

Arguments:
  function_name         Function to decompile (e.g., "CharClip::SetFlags")

Options:
  -u, --unit UNIT       Specify objdiff unit for disambiguation
                        (e.g., default/system/char/Character)
  -o, --output FILE     Write output to file instead of stdout
  --context             Generate type context from Ghidra (if available)
  --gotos-only          Pass --gotos-only to m2c for complex control flow
  --no-andor            Pass --no-andor to m2c
  --decomp              Decomp-friendly output (--noise=low + --show-offsets)
  --show-offsets        Show struct field offsets in comments
  --noise LEVEL         Output noise level: full, low, minimal
  -v, --verbose         Show progress messages
  -h, --help            Show this help message

Examples:
  # Basic decompilation
  tools/decompile.sh "CharClip::SetFlags"

  # With unit disambiguation
  tools/decompile.sh "Object::Load" -u default/system/char/Character

  # Output to file
  tools/decompile.sh "Game::Poll" -o decompiled.c

  # With Ghidra type context
  tools/decompile.sh "CharMirror::Load" --context

  # Verbose output
  tools/decompile.sh "CharClip::SetFlags" -v

  # Complex control flow
  tools/decompile.sh "Parser::Run" --gotos-only

Pipeline:
  This script chains: objdiff -> objdiff_to_m2c.py -> m2c

  1. objdiff-cli exports JSON with target binary instructions
  2. objdiff_to_m2c.py converts to GNU-as format for m2c
  3. m2c decompiles PPC assembly to C code
EOF
}

log() {
    if [ "$VERBOSE" = "1" ]; then
        echo "[decompile] $*" >&2
    fi
}

error() {
    echo "Error: $*" >&2
    exit 1
}

cleanup() {
    rm -f "$TMP_JSON" "$TMP_ASM" "$TMP_CONTEXT" 2>/dev/null || true
}

trap cleanup EXIT

# =============================================================================
# Dependency Checks
# =============================================================================

check_dependencies() {
    local missing=()

    if [ ! -x "$OBJDIFF_CLI" ]; then
        missing+=("objdiff-cli ($OBJDIFF_CLI)")
    fi

    if [ ! -f "$OBJDIFF_TO_M2C" ]; then
        missing+=("objdiff_to_m2c.py ($OBJDIFF_TO_M2C)")
    fi

    if [ ! -f "$M2C" ]; then
        missing+=("m2c ($M2C)")
    fi

    if ! command -v python3 &>/dev/null; then
        missing+=("python3")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        echo "Missing dependencies:" >&2
        for dep in "${missing[@]}"; do
            echo "  - $dep" >&2
        done
        exit 1
    fi
}

# =============================================================================
# Main Pipeline Functions
# =============================================================================

generate_context() {
    local func_name="$1"

    log "Generating type context from Ghidra..."

    if [ ! -f "$EXPORT_TYPES" ]; then
        echo "Warning: export_types.py not found, skipping context generation" >&2
        return 1
    fi

    # Try to generate context, but don't fail if Ghidra is unavailable
    if python3 "$EXPORT_TYPES" --function "$func_name" -o "$TMP_CONTEXT" 2>/dev/null; then
        log "Generated type context: $TMP_CONTEXT"
        return 0
    else
        echo "Warning: Could not generate Ghidra context (is Ghidra MCP running?)" >&2
        echo "  Start with: ./tools/ghidra/pyghidra-service.sh start" >&2
        return 1
    fi
}

get_objdiff_json() {
    local func_name="$1"
    local unit_opt="$2"

    log "Fetching disassembly from objdiff..."

    local cmd=("$OBJDIFF_CLI" diff -p "$PROJECT_DIR" "$func_name" -f json --include-instructions)

    if [ -n "$unit_opt" ]; then
        cmd+=(-u "$unit_opt")
    fi

    log "Running: ${cmd[*]}"

    if ! "${cmd[@]}" > "$TMP_JSON" 2>/dev/null; then
        # Try again and capture stderr for better error message
        local stderr
        stderr=$("${cmd[@]}" 2>&1 >/dev/null || true)

        if echo "$stderr" | grep -q "Multiple matches"; then
            echo "Error: Ambiguous symbol '$func_name'. Multiple matches found." >&2
            echo "Use -u/--unit to specify which unit. Available units:" >&2
            # Try to extract unit suggestions from error
            echo "$stderr" | grep -E '^\s+' >&2 || true
            return 1
        elif echo "$stderr" | grep -q "not found\|No symbol"; then
            error "Symbol '$func_name' not found in project"
        else
            error "objdiff failed: $stderr"
        fi
    fi

    # Verify we got valid JSON with instructions
    if ! python3 -c "import json; d=json.load(open('$TMP_JSON')); assert d.get('instructions')" 2>/dev/null; then
        error "objdiff returned empty or invalid instruction data"
    fi

    log "Got disassembly JSON"
}

convert_to_m2c() {
    log "Converting to m2c assembly format..."

    if ! python3 "$OBJDIFF_TO_M2C" -i "$TMP_JSON" -o "$TMP_ASM" --project-dir "$PROJECT_DIR"; then
        error "Failed to convert objdiff output to m2c format"
    fi

    log "Generated assembly: $TMP_ASM"
}

run_m2c() {
    local output_file="$1"
    local gotos_only="$2"
    local no_andor="$3"
    local has_context="$4"
    local decomp_mode="$5"
    local show_offsets="$6"
    local noise_level="$7"

    log "Running m2c decompiler..."

    local cmd=(python3 "$M2C" -t ppc)

    # Add context file if available
    if [ "$has_context" = "1" ] && [ -f "$TMP_CONTEXT" ]; then
        cmd+=(--context "$TMP_CONTEXT")
        log "Using type context file"
    fi

    # Add optional flags
    if [ "$gotos_only" = "1" ]; then
        cmd+=(--gotos-only)
    fi

    if [ "$no_andor" = "1" ]; then
        cmd+=(--no-andor)
    fi

    # Decomp-friendly output flags
    if [ "$decomp_mode" = "1" ]; then
        cmd+=(--decomp)
    fi

    if [ "$show_offsets" = "1" ]; then
        cmd+=(--show-offsets)
    fi

    if [ -n "$noise_level" ]; then
        cmd+=(--noise "$noise_level")
    fi

    # Input file
    cmd+=("$TMP_ASM")

    log "Running: ${cmd[*]}"

    local result
    if ! result=$("${cmd[@]}" 2>&1); then
        # Check for common m2c errors
        if echo "$result" | grep -q "Parse error\|Syntax error"; then
            echo "Warning: m2c parse error. The assembly may have unsupported instructions." >&2
            echo "Try --gotos-only for complex control flow." >&2
        fi
        error "m2c failed: $result"
    fi

    # Output result
    if [ -n "$output_file" ]; then
        echo "$result" > "$output_file"
        log "Wrote output to: $output_file"
        echo "Decompiled code written to: $output_file" >&2
    else
        echo "$result"
    fi
}

# =============================================================================
# Argument Parsing
# =============================================================================

FUNCTION_NAME=""
UNIT=""
OUTPUT_FILE=""
USE_CONTEXT=0
GOTOS_ONLY=0
NO_ANDOR=0
DECOMP_MODE=0
SHOW_OFFSETS=0
NOISE_LEVEL=""
VERBOSE=0

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -u|--unit)
            UNIT="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --context)
            USE_CONTEXT=1
            shift
            ;;
        --gotos-only)
            GOTOS_ONLY=1
            shift
            ;;
        --no-andor)
            NO_ANDOR=1
            shift
            ;;
        --decomp)
            DECOMP_MODE=1
            shift
            ;;
        --show-offsets)
            SHOW_OFFSETS=1
            shift
            ;;
        --noise)
            NOISE_LEVEL="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -*)
            error "Unknown option: $1. Use --help for usage."
            ;;
        *)
            if [ -z "$FUNCTION_NAME" ]; then
                FUNCTION_NAME="$1"
            else
                error "Unexpected argument: $1"
            fi
            shift
            ;;
    esac
done

# =============================================================================
# Main
# =============================================================================

if [ -z "$FUNCTION_NAME" ]; then
    echo "Error: Function name is required." >&2
    echo "" >&2
    usage
    exit 1
fi

# Check dependencies
check_dependencies

# Step 1: Optionally generate type context
HAS_CONTEXT=0
if [ "$USE_CONTEXT" = "1" ]; then
    if generate_context "$FUNCTION_NAME"; then
        HAS_CONTEXT=1
    fi
fi

# Step 2: Get objdiff JSON with instructions
get_objdiff_json "$FUNCTION_NAME" "$UNIT"

# Step 3: Convert to m2c format
convert_to_m2c

# Step 4: Run m2c
run_m2c "$OUTPUT_FILE" "$GOTOS_ONLY" "$NO_ANDOR" "$HAS_CONTEXT" "$DECOMP_MODE" "$SHOW_OFFSETS" "$NOISE_LEVEL"

log "Done!"
