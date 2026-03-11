#!/usr/bin/env bash
# GFXReconstruct capture inspection wrapper.
#
# Analyze .gfxr capture files: metadata, JSON export, shader extraction,
# API call summary.
#
# Usage:
#   scripts/gpu/inspect.sh <command> [options] <capture.gfxr>
#
# Commands:
#   info <file>                Show capture metadata (GPU, pipelines, memory)
#   summary <file>             Summarize Vulkan API calls by frequency
#   convert <file> [-o out]    Convert to JSON (default: stdout)
#   extract <file> [-d dir]    Extract SPIR-V shaders to directory
#   calls <file> [pattern]     List Vulkan calls matching a pattern (e.g. "vkCmdDraw")
#   shaders <file>             Extract + disassemble all shaders
#   query <file.jsonl> [opts]  Query converted JSON (see query_trace.py --help)
#   pipelines <file>           Show all pipeline creation details (blend, depth, etc.)
#   draws <file>               Show draw calls with pipeline context
#   labels <file> [-g pat]     Show debug labels, optionally filtered
#
# The query/pipelines/draws/labels commands work on converted .jsonl files.
# Pass a .gfxr file and it will auto-convert to a cached .jsonl first.
#
# Examples:
#   scripts/gpu/inspect.sh info /tmp/gpu_capture.gfxr
#   scripts/gpu/inspect.sh summary /tmp/gpu_capture.gfxr
#   scripts/gpu/inspect.sh convert /tmp/gpu_capture.gfxr -o /tmp/trace.jsonl
#   scripts/gpu/inspect.sh extract /tmp/gpu_capture.gfxr -d /tmp/shaders
#   scripts/gpu/inspect.sh calls /tmp/gpu_capture.gfxr vkCmdDraw
#   scripts/gpu/inspect.sh shaders /tmp/gpu_capture.gfxr
#   scripts/gpu/inspect.sh pipelines /tmp/gpu_capture.gfxr
#   scripts/gpu/inspect.sh draws /tmp/gpu_capture.gfxr
#   scripts/gpu/inspect.sh labels /tmp/gpu_capture.gfxr -g "Text"
#   scripts/gpu/inspect.sh query /tmp/trace.jsonl --call vkCmdDrawIndexed --limit 10
#   scripts/gpu/inspect.sh query /tmp/capture.gfxr --call CreateGraphicsPipelines --field pColorBlendState
#
# Source: ../gpu/gfxreconstruct/ (https://github.com/LunarG/gfxreconstruct)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GPU_DIR="$(cd "$REPO_ROOT/../gpu" && pwd)"

GFXR_TOOLS="$GPU_DIR/gfxreconstruct/build/tools"
GFXR_INFO="$GFXR_TOOLS/info/gfxrecon-info"
GFXR_CONVERT="$GFXR_TOOLS/convert/gfxrecon-convert"
GFXR_EXTRACT="$GFXR_TOOLS/extract/gfxrecon-extract"
QUERY_TOOL="$SCRIPT_DIR/query_trace.py"

check_tools() {
    for tool in "$GFXR_INFO" "$GFXR_CONVERT" "$GFXR_EXTRACT"; do
        if [[ ! -x "$tool" ]]; then
            echo "Error: $tool not found. Build GFXReconstruct first:" >&2
            echo "  cd $GPU_DIR/gfxreconstruct && cmake --build build -j\$(nproc)" >&2
            exit 1
        fi
    done
}

# Auto-convert .gfxr to .jsonl, caching the result next to the original.
# Returns the path to the .jsonl file.
ensure_jsonl() {
    local file="$1"
    if [[ "$file" == *.jsonl ]] || [[ "$file" == *.json ]]; then
        echo "$file"
        return
    fi
    # .gfxr file — convert to .jsonl next to it (cached)
    local jsonl="${file%.gfxr}.jsonl"
    if [[ ! -f "$jsonl" ]] || [[ "$file" -nt "$jsonl" ]]; then
        echo "Converting $(basename "$file") -> $(basename "$jsonl") ..." >&2
        check_tools
        "$GFXR_CONVERT" --output "$jsonl" "$file" >&2 2>&1
        echo "Done ($(du -h "$jsonl" | cut -f1))." >&2
    fi
    echo "$jsonl"
}

usage() {
    sed -n '/^# Usage:/,/^# Source:/p' "$0" | sed 's/^# \?//'
    exit 0
}

cmd_info() {
    local file="$1"
    "$GFXR_INFO" "$file" 2>&1
}

cmd_summary() {
    local file="$1"
    echo "=== API Call Summary for $(basename "$file") ==="
    "$GFXR_CONVERT" --output stdout "$file" 2>/dev/null \
        | grep -oP '"name"\s*:\s*"vk\w+"' \
        | sed 's/"name"\s*:\s*"//' | sed 's/"//' \
        | sort | uniq -c | sort -rn
    echo ""
    echo "=== Total calls: $("$GFXR_CONVERT" --output stdout "$file" 2>/dev/null | grep -c '"function"') ==="
}

cmd_convert() {
    local file="$1"
    shift
    local output="stdout"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -o) output="$2"; shift 2 ;;
            *) echo "Unknown option: $1" >&2; exit 1 ;;
        esac
    done
    "$GFXR_CONVERT" --output "$output" "$file" 2>/dev/null
}

cmd_extract() {
    local file="$1"
    shift
    local dir="${TMPDIR:-/tmp/claude-1000}/gfxr_shaders_$$"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -d) dir="$2"; shift 2 ;;
            *) echo "Unknown option: $1" >&2; exit 1 ;;
        esac
    done
    mkdir -p "$dir"
    "$GFXR_EXTRACT" --dir "$dir" "$file" 2>&1
    echo ""
    echo "=== Extracted shaders ==="
    ls -la "$dir"/sh* 2>/dev/null || echo "(none)"
    echo "Directory: $dir"
}

cmd_calls() {
    local file="$1"
    local pattern="${2:-vkCmd}"
    echo "=== Vulkan calls matching '$pattern' ==="
    "$GFXR_CONVERT" --output stdout "$file" 2>/dev/null \
        | grep -oP '"name"\s*:\s*"vk\w+"' \
        | sed 's/"name"\s*:\s*"//' | sed 's/"//' \
        | grep -i "$pattern" \
        | sort | uniq -c | sort -rn
}

cmd_shaders() {
    local file="$1"
    local dir="${TMPDIR:-/tmp/claude-1000}/gfxr_shaders_$$"
    mkdir -p "$dir"
    "$GFXR_EXTRACT" --dir "$dir" "$file" 2>/dev/null

    local count=0
    for shader in "$dir"/sh*; do
        [[ -f "$shader" ]] || continue
        count=$((count + 1))
        local name=$(basename "$shader")
        local size=$(stat -c%s "$shader")
        echo "=== $name ($size bytes) ==="
        if command -v spirv-dis &>/dev/null; then
            spirv-dis "$shader" 2>/dev/null | head -30
        else
            echo "(install spirv-tools for disassembly: pacman -S spirv-tools)"
            file "$shader"
        fi
        echo ""
    done

    if [[ $count -eq 0 ]]; then
        echo "No shaders found in capture."
    else
        echo "=== $count shaders extracted to $dir ==="
    fi
}

cmd_query() {
    local file="$1"
    shift
    local jsonl
    jsonl=$(ensure_jsonl "$file")
    python3 "$QUERY_TOOL" "$jsonl" "$@"
}

cmd_pipelines() {
    local file="$1"
    shift
    local jsonl
    jsonl=$(ensure_jsonl "$file")
    python3 "$QUERY_TOOL" "$jsonl" --pipelines "$@"
}

cmd_draws() {
    local file="$1"
    shift
    local jsonl
    jsonl=$(ensure_jsonl "$file")
    python3 "$QUERY_TOOL" "$jsonl" --draws "$@"
}

cmd_labels() {
    local file="$1"
    shift
    local jsonl
    jsonl=$(ensure_jsonl "$file")
    python3 "$QUERY_TOOL" "$jsonl" --labels "$@"
}

# Main
if [[ $# -eq 0 ]]; then
    usage
fi

CMD="$1"
shift

case "$CMD" in
    -h|--help) usage ;;
    info|summary|convert|extract|calls|shaders)
        check_tools
        if [[ $# -eq 0 ]]; then
            echo "Error: capture file required" >&2
            exit 1
        fi
        "cmd_$CMD" "$@"
        ;;
    query|pipelines|draws|labels)
        if [[ $# -eq 0 ]]; then
            echo "Error: capture/jsonl file required" >&2
            exit 1
        fi
        "cmd_$CMD" "$@"
        ;;
    *)
        echo "Unknown command: $CMD" >&2
        usage
        ;;
esac
