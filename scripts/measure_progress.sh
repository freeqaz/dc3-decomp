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
#   scripts/measure_progress.sh --current-dir /path/to/worktree HEAD  # Use worktree as "current"
#   scripts/measure_progress.sh --authorable       # Print authorable-denominator metrics (no baseline needed)
#   scripts/measure_progress.sh --refresh-baseline # Ignore + rebuild the cached baseline report
#   scripts/measure_progress.sh --allow-stale      # Downgrade staleness/race errors to warnings
#
# Staleness safety: a report.json that is out of date (or that another agent
# rebuilds underneath us) shows up as a pile of phantom regressions. Both
# sides of the comparison are therefore gated: the "current" report must be
# ninja-clean before it is read, cached baselines carry a provenance stamp
# that is re-verified on reuse, and both files are fingerprinted before and
# after the diff to catch a concurrent rebuild. Use --allow-stale to override.
#
set -euo pipefail

MAIN_REPO="$(cd "$(dirname "$0")/.." && pwd)"
REPORT_REL="build/373307D9/report.json"
BASELINE_REF="HEAD~1"
COMPARE_FLAGS=()
WORKTREE_DIR="/tmp/claude/measure-progress"
CREATED_WORKTREE=0
CURRENT_DIR=""
ALLOW_STALE=0
REFRESH_BASELINE=0
# Config inputs whose content decides what dtk/objdiff measure against.
# Recorded per baseline so a later config or toolchain change invalidates it.
PROVENANCE_FILES=(
    "config/373307D9/config.yml"
    "config/373307D9/symbols.txt"
    "config/373307D9/splits.txt"
    "config/373307D9/objects.json"
    "config/373307D9/link_order.txt"
)

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --authorable)
            # Delegate to progress_metrics.py; no baseline worktree needed.
            REPORT_PATH="${MAIN_REPO}/${REPORT_REL}"
            if [[ ! -f "${REPORT_PATH}" ]]; then
                echo "Error: report.json not found: ${REPORT_PATH}"
                echo "Run 'ninja build/373307D9/report.json' first."
                exit 1
            fi
            exec python3 "${MAIN_REPO}/scripts/progress_metrics.py" \
                --report "${REPORT_PATH}" "${@:2}"
            ;;
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
        --current-dir)
            CURRENT_DIR="$2"
            shift 2
            ;;
        --limit)
            COMPARE_FLAGS+=("--limit" "$2")
            shift 2
            ;;
        --allow-stale)
            ALLOW_STALE=1
            shift
            ;;
        --refresh-baseline)
            REFRESH_BASELINE=1
            shift
            ;;
        --help|-h)
            sed -n '2,26p' "$0"
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

# =============================================================================
# Staleness / provenance guards
#
# A report.json that lags its sources produces phantom regressions that look
# exactly like real ones. Everything below exists so that can never happen
# quietly: either the comparison is provably fresh, or the script says why
# it is not.
# =============================================================================

# Loud failure that --allow-stale can downgrade to a warning.
stale_fail() {
    local msg="$1"
    if [[ "${ALLOW_STALE}" -eq 1 ]]; then
        echo "WARNING (--allow-stale): ${msg}" >&2
        return 0
    fi
    echo "" >&2
    echo "ERROR: ${msg}" >&2
    echo "       Refusing to compare — a stale report shows up as phantom regressions." >&2
    echo "       Re-run with --allow-stale to compare anyway (results are not trustworthy)." >&2
    exit 1
}

sha_of() { [[ -f "$1" ]] && sha256sum "$1" | cut -d' ' -f1 || echo "missing"; }

# inode:size:mtime — changes if anyone rewrites the file underneath us.
fingerprint_of() { stat -c '%i:%s:%Y:%Z' "$1" 2>/dev/null || echo "missing"; }

git_head_of() { git -C "$1" rev-parse HEAD 2>/dev/null || echo "unknown"; }

git_dirty_of() {
    git -C "$1" status --porcelain --untracked-files=no 2>/dev/null | wc -l | tr -d ' '
}

# Is `dir`'s report.json fully up to date with respect to its ninja graph?
# `ninja -n` is a pure dry run; "no work to do" is the only clean answer.
ninja_is_clean() {
    local dir="$1" out
    [[ -f "${dir}/build.ninja" ]] || return 2
    out="$(cd "${dir}" && ninja -n "${REPORT_REL}" 2>&1)" || return 3
    [[ "${out}" == *"no work to do"* ]]
}

# Gate a report we are about to read. Rebuilds it once if stale, then insists.
require_fresh_report() {
    local dir="$1" label="$2" rc=0

    ninja_is_clean "${dir}" || rc=$?
    case "${rc}" in
        0) return 0 ;;
        2)
            stale_fail "${label} (${dir}) has no build.ninja — cannot verify its report is current."
            return 0
            ;;
        3)
            stale_fail "${label} (${dir}): 'ninja -n ${REPORT_REL}' failed — the build graph is broken."
            return 0
            ;;
    esac

    echo "  ${label} report is STALE (ninja has pending work). Rebuilding..."
    if ! ninja -C "${dir}" "${REPORT_REL}" -j"$(nproc)" >/dev/null 2>&1; then
        stale_fail "${label} (${dir}): rebuild of ${REPORT_REL} failed."
        return 0
    fi
    rc=0
    ninja_is_clean "${dir}" || rc=$?
    if [[ "${rc}" -ne 0 ]]; then
        stale_fail "${label} (${dir}) is still stale after a rebuild — another process is probably building there concurrently."
    fi
}

# --- Resolve current directory (main repo or worktree) ---
if [[ -n "${CURRENT_DIR}" ]]; then
    CURRENT_DIR="$(cd "${CURRENT_DIR}" && pwd)"
    if [[ ! -f "${CURRENT_DIR}/${REPORT_REL}" ]]; then
        echo "Current report not found in worktree, building..."
        ninja -C "${CURRENT_DIR}" "${REPORT_REL}" -j"$(nproc)" 2>&1 | tail -1
    fi
    CURRENT_REPORT="${CURRENT_DIR}/${REPORT_REL}"
    CURRENT_LABEL="worktree:$(basename "${CURRENT_DIR}")"
else
    CURRENT_DIR="${MAIN_REPO}"
    CURRENT_REPORT="${MAIN_REPO}/${REPORT_REL}"
    CURRENT_LABEL="working tree"
fi

# --- Verify prerequisites ---
if [[ ! -f "${CURRENT_REPORT}" ]]; then
    echo "Error: Current report not found: ${CURRENT_REPORT}"
    echo "Run 'ninja' first."
    exit 1
fi

if [[ ! -d "${MAIN_REPO}/orig/373307D9" ]]; then
    echo "Error: orig/ binaries not found in main repo."
    exit 1
fi

# Resolve the baseline ref to an actual commit hash
BASELINE_COMMIT=$(git -C "${MAIN_REPO}" rev-parse "${BASELINE_REF}")
BASELINE_SHORT=$(git -C "${MAIN_REPO}" rev-parse --short "${BASELINE_COMMIT}")
CURRENT_SHORT="${CURRENT_LABEL}"

echo "Measuring progress: ${BASELINE_SHORT} (baseline) -> ${CURRENT_SHORT} (current)"

# --- Provenance banner: say exactly what is being compared ---
CURRENT_HEAD="$(git_head_of "${CURRENT_DIR}")"
CURRENT_DIRTY="$(git_dirty_of "${CURRENT_DIR}")"
DTK_SHA="unknown"
if DTK_PROBE="$(cd "${MAIN_REPO}" && realpath -e ../jeff/target/release/dtk 2>/dev/null)"; then
    DTK_SHA="$(sha_of "${DTK_PROBE}")"
fi
OBJDIFF_SHA="unknown"
if OBJDIFF_PROBE="$(cd "${MAIN_REPO}" && realpath -e ../objdiff/target/release/objdiff-cli 2>/dev/null)"; then
    OBJDIFF_SHA="$(sha_of "${OBJDIFF_PROBE}")"
fi
echo "  baseline : ${BASELINE_COMMIT}"
echo "  current  : ${CURRENT_DIR} @ ${CURRENT_HEAD} (${CURRENT_DIRTY} tracked file(s) modified)"
echo "  dtk      : ${DTK_SHA:0:12}   objdiff: ${OBJDIFF_SHA:0:12}"
if [[ "${CURRENT_DIRTY}" -gt 0 ]]; then
    echo "  NOTE: the 'current' tree has ${CURRENT_DIRTY} uncommitted tracked change(s), so the"
    echo "        numbers below include work that is in no commit. If this is a shared"
    echo "        checkout, that includes other agents' in-progress edits — prefer"
    echo "        --current-dir <your own worktree>."
fi

# --- Gate the current report: it must be ninja-clean before we read it ---
echo "Checking that the current report is up to date..."
require_fresh_report "${CURRENT_DIR}" "current (${CURRENT_LABEL})"

# --- Baseline provenance stamp -----------------------------------------------
# Records the commit and the config/toolchain inputs the cached report was
# produced from, so a later config edit or dtk rebuild cannot be reused blindly.
BASELINE_META="${CACHE_DIR}/${BASELINE_COMMIT}.meta"

expected_provenance() {
    echo "commit ${BASELINE_COMMIT}"
    echo "dtk ${DTK_SHA}"
    echo "objdiff ${OBJDIFF_SHA}"
    local f blob
    for f in "${PROVENANCE_FILES[@]}"; do
        blob="$(git -C "${MAIN_REPO}" rev-parse --verify --quiet "${BASELINE_COMMIT}:${f}" || echo absent)"
        echo "${f} ${blob}"
    done
}

# --- Check baseline cache ---
CACHED_REPORT="${CACHE_DIR}/${BASELINE_COMMIT}.json"
if [[ "${REFRESH_BASELINE}" -eq 1 && -f "${CACHED_REPORT}" ]]; then
    echo "--refresh-baseline: discarding cached baseline for ${BASELINE_SHORT}"
    rm -f "${CACHED_REPORT}" "${BASELINE_META}"
fi
if [[ -f "${CACHED_REPORT}" ]] && [[ -f "${BASELINE_META}" ]] \
   && diff -q <(expected_provenance) "${BASELINE_META}" >/dev/null 2>&1; then
    echo "Using cached baseline report for ${BASELINE_SHORT} (provenance verified)"
    BASELINE_REPORT="${CACHED_REPORT}"
elif [[ -f "${CACHED_REPORT}" && ! -f "${BASELINE_META}" ]]; then
    # Legacy cache entry from before provenance stamping. We cannot prove what
    # config/toolchain produced it, so say so instead of pretending.
    echo ""
    echo "WARNING: cached baseline ${BASELINE_SHORT} has no provenance stamp."
    echo "         It predates provenance tracking, so it cannot be verified against"
    echo "         the current config/373307D9/* or dtk build. If it was generated with"
    echo "         a different dtk, differences below may be tool artifacts, not code."
    echo "         Re-run with --refresh-baseline to rebuild it from scratch."
    echo ""
    BASELINE_REPORT="${CACHED_REPORT}"
else
    if [[ -f "${CACHED_REPORT}" ]]; then
        echo "Cached baseline for ${BASELINE_SHORT} is INVALID (config/toolchain changed since it was built):"
        diff <(expected_provenance) "${BASELINE_META}" | sed 's/^/    /' || true
        rm -f "${CACHED_REPORT}" "${BASELINE_META}"
    fi
    echo "No usable cached baseline for ${BASELINE_SHORT}, building..."
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
    mkdir -p "${WORKTREE}/build/tools" "${WORKTREE}/build/373307D9/pch"
    # Pre-create empty PCH file — WIBO_FS_CACHE=1 breaks creating new files in
    # case-insensitive path components (373307D9). cl.exe can overwrite existing files fine.
    touch "${WORKTREE}/build/373307D9/pch/system.pch"
    for tool in "${MAIN_REPO}/build/tools"/*; do
        dest="${WORKTREE}/build/tools/$(basename "$tool")"
        [[ -e "$dest" ]] || ln -sf "$tool" "$dest"
    done
    if [[ -d "${MAIN_REPO}/build/compilers" && ! -d "${WORKTREE}/build/compilers" ]]; then
        ln -sf "${MAIN_REPO}/build/compilers" "${WORKTREE}/build/compilers"
    fi
    # Symlink binutils if present
    if [[ -d "${MAIN_REPO}/build/binutils" && ! -d "${WORKTREE}/build/binutils" ]]; then
        ln -sf "${MAIN_REPO}/build/binutils" "${WORKTREE}/build/binutils"
    fi

    # --- Extract configure args from main repo (resolve relative paths to absolute) ---
    CONFIGURE_ARGS=()
    if [[ -f "${MAIN_REPO}/build.ninja" ]]; then
        # Read configure_args, joining continuation lines
        raw_args=$(sed -n '/^configure_args/{ :a; /\$$/{ N; s/\$\n\s*/ /; ba }; s/^configure_args = //; p }' \
            "${MAIN_REPO}/build.ninja")
        # Resolve relative paths to absolute (relative to MAIN_REPO)
        for arg in $raw_args; do
            if [[ "$arg" == --* ]]; then
                CONFIGURE_ARGS+=("$arg")
            elif [[ "$arg" == ../* || "$arg" == ./* ]]; then
                CONFIGURE_ARGS+=("$(cd "${MAIN_REPO}" && realpath "$arg")")
            else
                CONFIGURE_ARGS+=("$arg")
            fi
        done
    fi

    # --- Resolve tool paths from main repo's build.ninja to absolute ---
    # configure.py defaults to relative paths (../jeff/..., ../wibo/..., etc.)
    # which break in worktrees outside the source tree
    resolve_tool() {
        local rel_path="$1"
        local abs_path
        abs_path="$(cd "${MAIN_REPO}" && realpath -e "${rel_path}" 2>/dev/null)" || return 1
        echo "${abs_path}"
    }

    # Extract tool paths used in the main build and pass them explicitly
    for tool_flag_pair in \
        "--dtk:../jeff/target/release/dtk" \
        "--objdiff:../objdiff/target/release/objdiff-cli" \
        "--wrapper:../wibo/build/release/wibo"; do
        flag="${tool_flag_pair%%:*}"
        rel="${tool_flag_pair#*:}"
        abs="$(resolve_tool "${rel}")" && CONFIGURE_ARGS+=("${flag}" "${abs}")
    done

    echo "Using configure args: ${CONFIGURE_ARGS[*]}"

    # Extract dtk path from configure args so we can run the split step directly.
    # This avoids a misleading ninja "manifest still dirty" loop when the split
    # fails and build/373307D9/config.json is never produced.
    DTK_BIN=""
    for ((i = 0; i < ${#CONFIGURE_ARGS[@]}; i++)); do
        if [[ "${CONFIGURE_ARGS[$i]}" == "--dtk" && $((i + 1)) -lt ${#CONFIGURE_ARGS[@]} ]]; then
            DTK_BIN="${CONFIGURE_ARGS[$((i + 1))]}"
            break
        fi
    done

    # --- Generate split config explicitly (clear error path if dtk fails) ---
    if [[ -n "${DTK_BIN}" && -x "${DTK_BIN}" ]]; then
        echo "Generating baseline split config (dtk xex split)..."
        SPLIT_LOG="$(mktemp -t measure_progress_split.XXXXXX.log)"
        if ! (cd "${WORKTREE}" && "${DTK_BIN}" xex split config/373307D9/config.yml build/373307D9) \
            >"${SPLIT_LOG}" 2>&1; then
            echo "Error: Failed to generate baseline split config with dtk:"
            echo "  ${DTK_BIN} xex split config/373307D9/config.yml build/373307D9"
            echo ""
            tail -100 "${SPLIT_LOG}" || true
            echo ""
            if grep -q "Overlapping functions" "${SPLIT_LOG}"; then
                echo "Hint: 'Overlapping functions A-B -> C' means config/373307D9/symbols.txt at the"
                echo "      baseline commit declares a type:function symbol at C that falls inside the"
                echo "      function A..B that dtk derives from pdata/jump-table analysis. That is"
                echo "      almost always a symbol cut at an *internal* control-flow target (a switch"
                echo "      jump table, a loop head, or an EH funclet) rather than a real function"
                echo "      boundary. The overlap check is correct; the config is wrong."
                echo "      Inspect the range with: build/373307D9/asm/**  and fix symbols.txt."
                echo "      Baselines between 05f3e705 and its revert cannot be regenerated for this"
                echo "      reason — use a commit outside that range, or a cached baseline report."
            else
                echo "Hint: the selected baseline may require a different dtk version or a cached baseline report."
            fi
            exit 1
        fi
        rm -f "${SPLIT_LOG}" 2>/dev/null || true
    fi

    # --- Reconfigure for baseline's file set ---
    echo "Reconfiguring baseline..."
    (cd "${WORKTREE}" && python3 configure.py "${CONFIGURE_ARGS[@]}") >/dev/null

    # Ninja can loop on "manifest 'build.ninja' still dirty" when the reused
    # worktree/build artifacts have coarse or future mtimes (common with cached
    # build dirs in /tmp worktrees). Normalize generator deps, then bump the
    # generated manifest outputs to a strictly newer timestamp.
    normalize_manifest_timestamps() {
        local deps=(
            "${WORKTREE}/build/373307D9/config.json"
            "${WORKTREE}/configure.py"
            "${WORKTREE}/tools/project.py"
            "${WORKTREE}/tools/ninja_syntax.py"
            "${WORKTREE}/config/373307D9/config.json"
            "${WORKTREE}/config/373307D9/objects.json"
            "${WORKTREE}/config/373307D9/link_order.txt"
        )
        local touched_any=0
        for dep in "${deps[@]}"; do
            if [[ -e "${dep}" ]]; then
                touch "${dep}" 2>/dev/null || true
                touched_any=1
            fi
        done
        if [[ "${touched_any}" -eq 1 ]]; then
            # Ensure build.ninja/objdiff.json are newer than all configure deps.
            sleep 1
        fi
        touch "${WORKTREE}/build.ninja" "${WORKTREE}/objdiff.json" 2>/dev/null || true
    }
    normalize_manifest_timestamps

    # --- Build baseline report ---
    echo "Building baseline report (this may take a moment)..."
    BUILD_LOG="$(mktemp -t measure_progress_ninja.XXXXXX.log)"
    if ninja -C "${WORKTREE}" "${REPORT_REL}" -j"$(nproc)" >"${BUILD_LOG}" 2>&1; then
        tail -1 "${BUILD_LOG}" || true
    else
        tail -100 "${BUILD_LOG}" || true
        if grep -q "manifest 'build.ninja' still dirty" "${BUILD_LOG}" && \
           grep -q "output build/373307D9/config.json doesn't exist" "${BUILD_LOG}"; then
            echo ""
            echo "Hint: ninja's manifest-dirty loop is usually a secondary symptom."
            echo "      The baseline split step failed, so build/373307D9/config.json was never created."
        fi
        exit 1
    fi
    rm -f "${BUILD_LOG}" 2>/dev/null || true

    if [[ ! -f "${WORKTREE}/${REPORT_REL}" ]]; then
        echo "Error: Baseline report was not generated."
        exit 1
    fi

    # --- Cache the baseline report + its provenance stamp ---
    mkdir -p "${CACHE_DIR}"
    cp "${WORKTREE}/${REPORT_REL}" "${CACHED_REPORT}"
    expected_provenance > "${BASELINE_META}"
    echo "Cached baseline report -> ${CACHED_REPORT}"
    echo "Stamped provenance     -> ${BASELINE_META}"

    BASELINE_REPORT="${WORKTREE}/${REPORT_REL}"
fi

# --- Race detection: nobody may rewrite either report while we diff it ---
BASELINE_FP_BEFORE="$(fingerprint_of "${BASELINE_REPORT}")"
CURRENT_FP_BEFORE="$(fingerprint_of "${CURRENT_REPORT}")"

# --- Compare ---
echo ""
python3 "${MAIN_REPO}/scripts/analysis/compare_progress.py" \
    "${COMPARE_FLAGS[@]}" \
    "${BASELINE_REPORT}" \
    "${CURRENT_REPORT}"

if [[ "$(fingerprint_of "${BASELINE_REPORT}")" != "${BASELINE_FP_BEFORE}" ]]; then
    stale_fail "the baseline report ${BASELINE_REPORT} was rewritten while it was being compared — the numbers above are from a racing build."
fi
if [[ "$(fingerprint_of "${CURRENT_REPORT}")" != "${CURRENT_FP_BEFORE}" ]]; then
    stale_fail "the current report ${CURRENT_REPORT} was rewritten while it was being compared — the numbers above are from a racing build."
fi
