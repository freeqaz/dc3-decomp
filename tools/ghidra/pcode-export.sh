#!/bin/bash
# Real Ghidra P-code exporter — thin wrapper around pcode_export.py
#
# Emits genuine Ghidra P-code (HIGH from the decompiler's HighFunction, or RAW
# 1:1 sleigh P-code from the function body) — NOT hand-decoded bytes like the old
# pcode_inspect.py.
#
# Usage:
#   ./tools/ghidra/pcode-export.sh "CharBones::PoseMeshes"            # HIGH (default)
#   ./tools/ghidra/pcode-export.sh "CharBones::PoseMeshes" --raw
#   ./tools/ghidra/pcode-export.sh "0x82878b58" --raw --json
#   ./tools/ghidra/pcode-export.sh "?OnBeat@HollaBackMinigame@@QAAXXZ" --high --json
#
# IMPORTANT: must run with the sandbox SKIPPED (dangerouslyDisableSandbox) so the
# JVM and native ICD can load — like every other Ghidra script here.
#
# Uses a private throwaway project (/tmp/claude/ghidra_projects/DirectGhidraClient),
# so it does NOT contend for the service-held ghidra_projects/DC3/DC3.lock. First run
# imports + auto-analyzes the XEX (minutes); later runs reuse it instantly.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MILOHAX_DIR="$(cd "$PROJECT_DIR/.." && pwd)"

# Same Ghidra/JVM env the pyghidra-mcp service uses (VMX128-enabled fork, writable home).
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk}"
export GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-$MILOHAX_DIR/ghidra/build/ghidra}"
export GHIDRA_USER_HOME="${GHIDRA_USER_HOME:-/tmp/claude/ghidra_user}"
mkdir -p "$GHIDRA_USER_HOME"

PYTHON="$PROJECT_DIR/venv/bin/python"
if [[ ! -x "$PYTHON" ]]; then
    PYTHON="python3"
fi

exec "$PYTHON" "$SCRIPT_DIR/pcode_export.py" "$@"
