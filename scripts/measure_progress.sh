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
#   scripts/measure_progress.sh --worktree /path   # Use existing worktree dir
#   scripts/measure_progress.sh --detailed HEAD~5  # Show per-unit breakdown
#   scripts/measure_progress.sh --functions c8d98a # Show function-level changes
#   scripts/measure_progress.sh --regressions      # Only show regressions
#
set -euo pipefail

MAIN_REPO="$(cd "$(dirname "$0")/.." && pwd)"
REPORT_REL="build/373307D9/report.json"
BASELINE_REF="HEAD~1"
COMPARE_FLAGS=()
WORKTREE_DIR="/tmp/claude/measure-progress"
CREATED_WORKTREE=0

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --worktree)
            WORKTREE_DIR="$2"
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

WORKTREE="${WORKTREE_DIR}"
CACHE_DIR="${MAIN_REPO}/build/373307D9/baselines"

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

# Resolve the baseline ref to an actual commit hash
BASELINE_COMMIT=$(git -C "${MAIN_REPO}" rev-parse "${BASELINE_REF}")
BASELINE_SHORT=$(git -C "${MAIN_REPO}" rev-parse --short "${BASELINE_COMMIT}")
CURRENT_SHORT=$(git -C "${MAIN_REPO}" rev-parse --short HEAD)

echo "Measuring progress: ${BASELINE_SHORT} (baseline) -> ${CURRENT_SHORT} (current)"

# --- Check baseline cache ---
CACHED_REPORT="${CACHE_DIR}/${BASELINE_COMMIT}.json"
if [[ -f "${CACHED_REPORT}" ]]; then
    echo "Using cached baseline report for ${BASELINE_SHORT}"
    BASELINE_REPORT="${CACHED_REPORT}"
else
    echo "No cached baseline for ${BASELINE_SHORT}, building..."
    echo "Using worktree: ${WORKTREE}"

    # --- Create worktree if it doesn't exist ---
    if [[ ! -d "${WORKTREE}" ]]; then
        echo "Creating worktree at ${WORKTREE}..."
        git -C "${MAIN_REPO}" worktree add --detach "${WORKTREE}" HEAD --quiet
        CREATED_WORKTREE=1
    fi

    # --- Save worktree state for restoration ---
    ORIGINAL_COMMIT=$(git -C "${WORKTREE}" rev-parse HEAD)

    cleanup() {
        echo ""
        if [[ "${CREATED_WORKTREE}" -eq 1 ]]; then
            echo "Removing temporary worktree..."
            git -C "${MAIN_REPO}" worktree remove --force "${WORKTREE}" 2>/dev/null || true
        else
            echo "Restoring worktree to ${ORIGINAL_COMMIT:0:7}..."
            git -C "${WORKTREE}" reset --hard --quiet "${ORIGINAL_COMMIT}" 2>/dev/null || true
        fi
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

    # --- Ensure orig/ symlink (replace if not already a symlink to main repo) ---
    if [[ ! -L "${WORKTREE}/orig" || "$(readlink "${WORKTREE}/orig")" != "${MAIN_REPO}/orig" ]]; then
        rm -rf "${WORKTREE}/orig"
        ln -sf "${MAIN_REPO}/orig" "${WORKTREE}/orig"
        echo "Restored orig/ symlink"
    fi

    # --- Ensure scripts symlink ---
    if [[ ! -L "${WORKTREE}/scripts" || "$(readlink "${WORKTREE}/scripts")" != "${MAIN_REPO}/scripts" ]]; then
        rm -rf "${WORKTREE}/scripts"
        ln -sf "${MAIN_REPO}/scripts" "${WORKTREE}/scripts"
        echo "Restored scripts/ symlink"
    fi

    # --- Ensure build tools and compilers are available (avoid downloads) ---
    mkdir -p "${WORKTREE}/build/tools"
    for tool in "${MAIN_REPO}/build/tools"/*; do
        dest="${WORKTREE}/build/tools/$(basename "$tool")"
        [[ -f "$dest" ]] || cp "$tool" "$dest"
    done
    if [[ -d "${MAIN_REPO}/build/compilers" && ! -d "${WORKTREE}/build/compilers" ]]; then
        ln -sf "${MAIN_REPO}/build/compilers" "${WORKTREE}/build/compilers"
    fi
    # Copy current download_tool.py (has existence check to skip downloads)
    cp "${MAIN_REPO}/tools/download_tool.py" "${WORKTREE}/tools/download_tool.py"
    echo "Synced build tools"

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

    # --- Cache the baseline report ---
    mkdir -p "${CACHE_DIR}"
    cp "${WORKTREE}/${REPORT_REL}" "${CACHED_REPORT}"
    echo "Cached baseline report -> ${CACHED_REPORT}"

    BASELINE_REPORT="${WORKTREE}/${REPORT_REL}"
fi

# --- Compare ---
echo ""
python3 "${MAIN_REPO}/scripts/analysis/compare_progress.py" \
    "${COMPARE_FLAGS[@]}" \
    "${BASELINE_REPORT}" \
    "${MAIN_REPO}/${REPORT_REL}"
