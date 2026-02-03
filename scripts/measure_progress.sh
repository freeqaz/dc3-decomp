#!/usr/bin/env bash
#
# Measure incremental decomp progress between a baseline commit and HEAD.
#
# Uses a git worktree to build the baseline report, then compares it
# against the main repo's current report using compare_progress.py.
#
# Usage:
#   scripts/measure_progress.sh                    # Compare HEAD vs HEAD~1
#   scripts/measure_progress.sh dd02a3e            # Compare HEAD vs specific commit
#   scripts/measure_progress.sh --worktree 15      # Use agent-15 worktree
#   scripts/measure_progress.sh --detailed HEAD~5  # Show per-unit breakdown
#   scripts/measure_progress.sh --functions c8d98a # Show function-level changes
#   scripts/measure_progress.sh --regressions      # Only show regressions
#
set -euo pipefail

MAIN_REPO="$(cd "$(dirname "$0")/.." && pwd)"
REPORT_REL="build/373307D9/report.json"
WORKTREE_ID=19
BASELINE_REF="HEAD~1"
COMPARE_FLAGS=()
WORKTREE_BASE="/tmp/claude/decomp-agents"

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --worktree)
            WORKTREE_ID="$2"
            shift 2
            ;;
        --detailed)
            COMPARE_FLAGS+=("--detailed")
            shift
            ;;
        --functions|-f)
            COMPARE_FLAGS+=("--functions")
            shift
            ;;
        --regressions|-r)
            COMPARE_FLAGS+=("--regressions")
            shift
            ;;
        --limit)
            COMPARE_FLAGS+=("--limit" "$2")
            shift 2
            ;;
        --help|-h)
            sed -n '2,14p' "$0"
            exit 0
            ;;
        *)
            BASELINE_REF="$1"
            shift
            ;;
    esac
done

WORKTREE="${WORKTREE_BASE}/agent-${WORKTREE_ID}"

# --- Verify prerequisites ---
if [[ ! -f "${MAIN_REPO}/${REPORT_REL}" ]]; then
    echo "Error: Current report not found: ${MAIN_REPO}/${REPORT_REL}"
    echo "Run 'ninja' in the main repo first."
    exit 1
fi

if [[ ! -d "${MAIN_REPO}/orig/373307D9" ]]; then
    echo "Error: orig/ binaries not found in main repo."
    exit 1
fi

if [[ ! -d "${WORKTREE}" ]]; then
    echo "Error: Worktree not found: ${WORKTREE}"
    echo "Available worktrees:"
    ls -1 "${WORKTREE_BASE}/" 2>/dev/null | sed 's/^/  /'
    exit 1
fi

# Resolve the baseline ref to an actual commit hash
BASELINE_COMMIT=$(git -C "${MAIN_REPO}" rev-parse "${BASELINE_REF}")
BASELINE_SHORT=$(git -C "${MAIN_REPO}" rev-parse --short "${BASELINE_COMMIT}")
CURRENT_SHORT=$(git -C "${MAIN_REPO}" rev-parse --short HEAD)

echo "Measuring progress: ${BASELINE_SHORT} (baseline) -> ${CURRENT_SHORT} (current)"
echo "Using worktree: ${WORKTREE}"

# --- Save worktree state for restoration ---
ORIGINAL_COMMIT=$(git -C "${WORKTREE}" rev-parse HEAD)

cleanup() {
    echo ""
    echo "Restoring worktree to ${ORIGINAL_COMMIT:0:7}..."
    git -C "${WORKTREE}" reset --hard --quiet "${ORIGINAL_COMMIT}" 2>/dev/null || true
}
trap cleanup EXIT

# --- Reset worktree to baseline commit ---
echo "Resetting worktree to baseline ${BASELINE_SHORT}..."
git -C "${WORKTREE}" reset --hard --quiet "${BASELINE_COMMIT}"

# Clean untracked source files but preserve build artifacts and symlinks
git -C "${WORKTREE}" clean -fd \
    --exclude=build/ \
    --exclude=bin/ \
    --exclude=orig \
    --exclude=scripts \
    --exclude=compile_commands.json \
    --exclude=decomp.db \
    --exclude=objdiff.json \
    --exclude=build.ninja \
    --quiet 2>/dev/null || true

# --- Ensure orig/ symlink ---
if [[ ! -e "${WORKTREE}/orig" ]]; then
    ln -sf "${MAIN_REPO}/orig" "${WORKTREE}/orig"
    echo "Restored orig/ symlink"
fi

# --- Ensure scripts symlink ---
if [[ ! -e "${WORKTREE}/scripts" ]]; then
    ln -sf "${MAIN_REPO}/scripts" "${WORKTREE}/scripts"
    echo "Restored scripts/ symlink"
fi

# --- Reconfigure for baseline's file set ---
echo "Reconfiguring baseline..."
(cd "${WORKTREE}" && python3 configure.py) >/dev/null

# --- Build baseline report ---
echo "Building baseline report (this may take a moment)..."
ninja -C "${WORKTREE}" "${REPORT_REL}" -j"$(nproc)" 2>&1 | tail -1

if [[ ! -f "${WORKTREE}/${REPORT_REL}" ]]; then
    echo "Error: Baseline report was not generated."
    exit 1
fi

# --- Compare ---
echo ""
python3 "${MAIN_REPO}/scripts/compare_progress.py" \
    "${COMPARE_FLAGS[@]}" \
    "${WORKTREE}/${REPORT_REL}" \
    "${MAIN_REPO}/${REPORT_REL}"
