#!/bin/bash
#
# Sets up a git worktree with a fully working build system.
#
# Usage:
#   scripts/setup_worktree.sh [path] [branch-name]
#
# Examples:
#   scripts/setup_worktree.sh /tmp/claude/my-feature my-feature
#   scripts/setup_worktree.sh                         # auto-generates path & branch
#
# What this does:
#   1. Creates a git worktree from HEAD
#   2. Symlinks clangd config, orig/, and bin/objdiff-cli
#   3. Re-runs configure.py with absolute tool paths so build.ninja works
#   4. Symlinks shared build artifacts (compilers, target objects, etc.)
#
# After setup, you can build normally from the worktree:
#   cd /tmp/claude/my-feature && ninja build/373307D9/src/system/flow/FlowCommand.obj
#
# The MCP orchestrator tools also work:
#   run_objdiff(symbol, project_dir="/tmp/claude/my-feature")

set -euo pipefail

MAIN_REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORKTREE_PATH="${1:-/tmp/claude/worktree-$(date +%s)}"
BRANCH="${2:-wt-$(basename "$WORKTREE_PATH")}"

# Resolve tool paths (relative to main repo's parent)
TOOL_DIR="$(cd "$MAIN_REPO/.." && pwd)"
DTK_PATH="$TOOL_DIR/jeff/target/release/dtk"
OBJDIFF_PATH="$TOOL_DIR/objdiff/target/release/objdiff-cli"
WIBO_PATH="$TOOL_DIR/wibo/build/release/wibo"

# Verify tools exist
for tool in "$DTK_PATH" "$OBJDIFF_PATH" "$WIBO_PATH"; do
    if [ ! -f "$tool" ]; then
        echo "ERROR: Required tool not found: $tool" >&2
        exit 1
    fi
done

echo "==> Creating worktree at $WORKTREE_PATH (branch: $BRANCH)"
git -C "$MAIN_REPO" worktree add "$WORKTREE_PATH" -b "$BRANCH" HEAD

echo "==> Symlinking clangd config"
ln -sf "$MAIN_REPO/compile_commands.json" "$WORKTREE_PATH/"
ln -sf "$MAIN_REPO/.clangd" "$WORKTREE_PATH/"

echo "==> Symlinking orig/ (target binary)"
# orig/ is gitignored, so the worktree gets an empty dir with .gitkeep
# Remove the empty dir and symlink the real one
rm -rf "$WORKTREE_PATH/orig"
ln -sf "$MAIN_REPO/orig" "$WORKTREE_PATH/orig"

echo "==> Symlinking bin/objdiff-cli"
ln -sf "$MAIN_REPO/bin/objdiff-cli" "$WORKTREE_PATH/bin/objdiff-cli"

echo "==> Running configure.py with absolute tool paths"
(
    cd "$WORKTREE_PATH"
    python3 configure.py \
        --dtk "$DTK_PATH" \
        --objdiff "$OBJDIFF_PATH" \
        --wibo "$WIBO_PATH"
)

echo "==> Symlinking shared build artifacts"
WT_BUILD="$WORKTREE_PATH/build/373307D9"
MAIN_BUILD="$MAIN_REPO/build/373307D9"
mkdir -p "$WT_BUILD" "$WT_BUILD/pch"
# Pre-create empty PCH file — WIBO_FS_CACHE=1 breaks creating new files in
# case-insensitive path components (373307D9). cl.exe can overwrite existing files fine.
touch "$WT_BUILD/pch/system.pch"

# Target objects (original binary, never changes)
ln -sf "$MAIN_BUILD/obj" "$WT_BUILD/obj"

# Pre-split config (avoid re-running dtk xex split)
if [ -f "$MAIN_BUILD/config.json" ]; then
    ln -sf "$MAIN_BUILD/config.json" "$WT_BUILD/config.json"
fi

# Downloaded tools (compilers, binutils, sjiswrap)
for dir in compilers binutils tools; do
    if [ -e "$MAIN_REPO/build/$dir" ]; then
        ln -sf "$MAIN_REPO/build/$dir" "$WORKTREE_PATH/build/$dir"
    fi
done

echo ""
echo "Worktree ready at: $WORKTREE_PATH"
echo "Branch: $BRANCH"
echo ""
echo "Usage with MCP orchestrator:"
echo "  run_objdiff(symbol, project_dir=\"$WORKTREE_PATH\")"
