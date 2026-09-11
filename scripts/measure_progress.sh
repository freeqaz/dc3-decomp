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
#   scripts/measure_progress.sh --check-freshness  # Run ONLY the freshness gate and exit (0/1/2)
#
# Staleness safety: a report.json that is out of date (or that another agent
# rebuilds underneath us) shows up as a pile of phantom regressions. Both
# sides of the comparison are therefore gated: the "current" report must be
# current before it is read, cached baselines carry a provenance stamp
# that is re-verified on reuse, and both files are fingerprinted before and
# after the diff to catch a concurrent rebuild. Use --allow-stale to override.
#
# The freshness gate lives in scripts/report_freshness.py and asks "would a
# real build rewrite report.json", NOT "does ninja have nothing to run". Those
# stopped being the same question on 2026-08-21 -- see the comment above
# report_is_current() below, and that script's docstring.
#
# EXIT CODES (branch on these; same idiom as scripts/native_test.sh)
#
#    0  OK -- a COMPLETE comparison table was printed.
#    1  Usage / precondition / STALENESS refusal. No table, or (for the
#       staleness class) a refusal instead of one. --allow-stale downgrades
#       the staleness and race members of this class to warnings.
#    2  --check-freshness only: the freshness gate could not verify (passthrough
#       of scripts/report_freshness.py's own 0/1/2).
#   10  BASELINE BUILD FAILED. The baseline worktree could not be created,
#       reset, split, configured or built, so there is no baseline to compare
#       against. NOTHING was measured and no table is printed.
#   11  CURRENT TREE BUILD FAILED. Same, for the "current" side.
#   12  COMPARISON STEP FAILED. compare_progress.py died; whatever it had
#       written is suppressed rather than printed, so a partial table can
#       never be read as a complete one.
#
# ⚠ --allow-stale does NOT downgrade 10/11/12. It exists to let you compare a
# report that may be out of date; a build that FAILED produced no numbers to
# be stale about. Swallowing a build failure is how a run that measured
# nothing reads as a clean run to a caller checking $? -- which is exactly
# what happened before 2026-09-11: a failing rebuild under --allow-stale
# printed one WARNING line and then a full, authoritative-looking table, and
# exited 0.
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
CHECK_ONLY=0
# Config inputs whose content decides what dtk/objdiff measure against.
# Recorded per baseline so a later config or toolchain change invalidates it.
PROVENANCE_FILES=(
    "config/373307D9/config.yml"
    "config/373307D9/symbols.txt"
    "config/373307D9/splits.txt"
    "config/373307D9/objects.json"
    "config/373307D9/link_order.txt"
)

# Exit codes. See the table in the header; keep the two in sync.
EXIT_BASELINE_BUILD=10
EXIT_CURRENT_BUILD=11
EXIT_COMPARE=12

usage() {
    # Print the WHOLE leading comment block. This used to be `sed -n '2,31p'`,
    # a hardcoded line range that silently truncated the header the moment it
    # grew -- which it did, on 2026-09-11, when the exit-code table landed.
    awk 'NR == 1 { next } /^#/ { sub(/^#[[:space:]]?/, ""); print; next } { exit }' "$0"
}

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
        --check-freshness)
            CHECK_ONLY=1
            shift
            ;;
        --help|-h)
            usage
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

# --- --check-freshness: run only the gate, build nothing, exit with its code ---
if [[ "${CHECK_ONLY}" -eq 1 ]]; then
    check_dir="${CURRENT_DIR:-${MAIN_REPO}}"
    check_dir="$(cd "${check_dir}" && pwd)"
    exec python3 "${MAIN_REPO}/scripts/report_freshness.py" --project-dir "${check_dir}"
fi

# =============================================================================
# Build-failure reporting
#
# Every build this script runs used to report failure the same way: either a
# bare `exit 1` (indistinguishable from a usage error), or -- through
# require_fresh_report() under --allow-stale -- a single WARNING line followed
# by a full comparison table and exit 0. Measured 2026-09-11 in wt/mp-exit with
# a `ninja` shim that fails only for the tree being built:
#
#   scripts/measure_progress.sh --allow-stale HEAD~1   ->  exit 0, full table
#
# The table was real but it was computed from a report the failed build never
# refreshed, and `$?` said the run was clean. A build failure now gets its own
# code, its own banner, and no table at all.
# =============================================================================

FAIL_STEP=""
FAIL_TREE=""
WORKTREE_ACTIVE=0
ORIGINAL_COMMIT=""
COMPARE_OUT=""

# build_fail <exit-code> <step> <tree> [logfile]
#
# Loud, attributed, terminal. NOT downgradable by --allow-stale: --allow-stale
# is about trusting numbers that may be out of date, and a build that failed
# produced no numbers at all.
build_fail() {
    local code="$1" step="$2" tree="$3" log="${4:-}"
    FAIL_STEP="${step}"
    FAIL_TREE="${tree}"
    {
        echo ""
        echo "=============================================================================="
        case "${code}" in
            "${EXIT_BASELINE_BUILD}") echo "BASELINE BUILD FAILED — nothing was compared" ;;
            "${EXIT_CURRENT_BUILD}")  echo "CURRENT TREE BUILD FAILED — nothing was compared" ;;
            *)                        echo "BUILD FAILED — nothing was compared" ;;
        esac
        echo "  failing step : ${step}"
        echo "  tree         : ${tree}"
        echo "  exit code    : ${code}"
        echo "=============================================================================="
        if [[ -n "${log}" && -f "${log}" ]]; then
            echo "--- last 100 lines of ${log} ---"
            tail -100 "${log}" || true
            echo "--- end of ${log} ---"
        fi
        echo ""
        echo "NO COMPARISON TABLE WAS PRINTED. This run measured nothing: do not read"
        echo "the absence of regressions as a clean result. --allow-stale does not"
        echo "downgrade this — it downgrades staleness, not a build that did not run."
    } >&2
    exit "${code}"
}

worktree_cleanup() {
    [[ "${WORKTREE_ACTIVE}" -eq 1 ]] || return 0
    WORKTREE_ACTIVE=0
    echo ""
    if [[ "${CREATED_WORKTREE}" -eq 1 ]]; then
        echo "Removing temporary worktree..."
        git -C "${MAIN_REPO}" worktree remove --force "${WORKTREE}" 2>/dev/null || true
    else
        echo "Restoring worktree to ${ORIGINAL_COMMIT:0:7}..."
        git -C "${WORKTREE}" reset --hard --quiet "${ORIGINAL_COMMIT}" 2>/dev/null || true
    fi
}

# The EXIT trap has to leave the verdict as the LAST thing written, because a
# refusal that is not the last line does not read as a refusal -- the cleanup
# chatter above used to be the final word on a failed run. Bash preserves the
# triggering status across an EXIT trap that does not itself call `exit`
# (verified: a trap whose last command is `|| true` still exits 7 on `exit 7`).
on_exit() {
    local rc=$?
    [[ -z "${COMPARE_OUT}" ]] || rm -f "${COMPARE_OUT}" 2>/dev/null || true
    worktree_cleanup
    if [[ "${rc}" -ne 0 ]]; then
        echo "" >&2
        echo "measure_progress.sh FAILED — exit ${rc}${FAIL_STEP:+ (step: ${FAIL_STEP})}${FAIL_TREE:+ in ${FAIL_TREE}}" >&2
    fi
    return 0
}
trap on_exit EXIT

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

# Which dtk / objdiff-cli does a build directory actually use? Read it out of
# the generated build.ninja rather than assuming. dtk decides function
# boundaries from symbols.txt and objdiff-cli computes the percentages, so a
# version skew between the two sides of the comparison invents differences that
# have nothing to do with the code. The baseline is therefore built with the
# *current* side's binaries.
tool_from_ninja() {
    local dir="$1" rule="$2" pat="$3" path
    [[ -f "${dir}/build.ninja" ]] || return 1
    # Strip ninja's trailing '$' line-continuations and squeeze whitespace so a
    # wrapped `command =` line reads as one line before matching.
    path="$(sed -n "/^rule ${rule}\$/,/^ *description/p" "${dir}/build.ninja" 2>/dev/null \
        | sed -e 's/\$$//' | tr '\n' ' ' | tr -s ' ' \
        | grep -oE "${pat}" | head -n1 | sed -E 's/ .*//')"
    [[ -n "${path}" ]] || return 1
    case "${path}" in
        /*) ;;
        *) path="$(cd "${dir}" && realpath -e "${path}" 2>/dev/null)" || return 1 ;;
    esac
    [[ -x "${path}" ]] || return 1
    echo "${path}"
}

dtk_of_dir() { tool_from_ninja "$1" split '[^ ]+dtk xex split'; }
objdiff_of_dir() { tool_from_ninja "$1" report '[^ ]+objdiff-cli report generate'; }

# Is `dir`'s report.json current with respect to the objects and config?
#
# NOT "does ninja have nothing to run". That WAS the check here, written
# 2026-08-04 (dca4a6ca0) as `ninja -n <report> | grep "no work to do"`, and it
# was correct until 2026-08-21, when 6e1763aac gave report.json an `always`-
# dirty implicit (the split-currency guard: the 2,223 target objects are an
# undeclared build output, so no mtime can describe them). 7b4044fb7 then made
# that edge always-DIRTY without being always-CHANGED, via `--stamp-out` +
# `restat`, so a real `ninja` on a current tree is a ~0.4 s no-op -- but
# `restat` is applied WHILE THE BUILD RUNS, and a dry run cannot apply it.
# Nine more always-rooted guard/patcher edges have landed since, so today
# `ninja -n build/373307D9/report.json` cannot print "no work to do" in ANY
# tree, the main checkout included. This gate was therefore permanently red
# (measured 2026-09-11 in a freshly and fully built worktree: 12 pending
# edges), and it blamed a race that was not happening. Lanes routed around it.
# A guard that is always red is a guard nobody runs.
#
# scripts/report_freshness.py asks the question the gate MEANT: it classifies
# `ninja -n -d explain`'s root causes, discards the ones rooted in the `always`
# phony, and separately runs the split-currency guard -- the one input ninja
# genuinely cannot see. 0 current | 1 stale (reasons on stderr) | 2 cannot
# verify (broken graph / unparseable explain output; never conflated with 0).
report_is_current() {
    local dir="$1"
    python3 "${MAIN_REPO}/scripts/report_freshness.py" --project-dir "${dir}" --quiet
}

# Gate a report we are about to read. Rebuilds it once if stale, then insists.
#
# The rebuild's failure used to go through stale_fail(), so --allow-stale
# turned "the build did not run" into a warning and the run continued to print
# a table and exit 0. It goes through build_fail() now: a failed build is not a
# staleness judgement call, and its log is kept instead of being sent to
# /dev/null, where the reason for the failure used to die.
require_fresh_report() {
    local dir="$1" label="$2" code="${3:-${EXIT_CURRENT_BUILD}}" rc=0 log

    report_is_current "${dir}" || rc=$?
    case "${rc}" in
        0) return 0 ;;
        2)
            stale_fail "${label} (${dir}): cannot verify that ${REPORT_REL} is current (reason above)."
            return 0
            ;;
    esac

    echo "  ${label} report is STALE (reasons above). Rebuilding..."
    log="$(mktemp -t measure_progress_refresh.XXXXXX.log)"
    if ! ninja -C "${dir}" "${REPORT_REL}" -j"$(nproc)" >"${log}" 2>&1; then
        build_fail "${code}" "ninja ${REPORT_REL} (stale-report rebuild of the ${label} side)" \
                   "${dir}" "${log}"
    fi
    rm -f "${log}" 2>/dev/null || true
    rc=0
    report_is_current "${dir}" || rc=$?
    if [[ "${rc}" -ne 0 ]]; then
        stale_fail "${label} (${dir}) is still stale after a rebuild — another process is probably building there concurrently, or an input's mtime is in the future."
    fi
}

# --- Resolve current directory (main repo or worktree) ---
if [[ -n "${CURRENT_DIR}" ]]; then
    CURRENT_DIR="$(cd "${CURRENT_DIR}" && pwd)"
    if [[ ! -f "${CURRENT_DIR}/${REPORT_REL}" ]]; then
        echo "Current report not found in worktree, building..."
        # This was `ninja ... 2>&1 | tail -1`. `set -o pipefail` is on, so the
        # status did survive -- but as a bare 1, with the single line
        # "ninja: build stopped: subcommand failed" as the entire output and
        # nothing saying which tree or which step. Measured 2026-09-11.
        CUR_BUILD_LOG="$(mktemp -t measure_progress_current.XXXXXX.log)"
        if ninja -C "${CURRENT_DIR}" "${REPORT_REL}" -j"$(nproc)" >"${CUR_BUILD_LOG}" 2>&1; then
            tail -1 "${CUR_BUILD_LOG}" || true
            rm -f "${CUR_BUILD_LOG}" 2>/dev/null || true
        else
            build_fail "${EXIT_CURRENT_BUILD}" \
                       "ninja ${REPORT_REL} (initial build of the current tree)" \
                       "${CURRENT_DIR}" "${CUR_BUILD_LOG}"
        fi
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
# Tools the *current* side actually built with; the baseline is forced to match.
BUILD_DTK="$(dtk_of_dir "${CURRENT_DIR}" || dtk_of_dir "${MAIN_REPO}" \
    || (cd "${MAIN_REPO}" && realpath -e ../jeff/target/release/dtk 2>/dev/null) || true)"
BUILD_OBJDIFF="$(objdiff_of_dir "${CURRENT_DIR}" || objdiff_of_dir "${MAIN_REPO}" \
    || (cd "${MAIN_REPO}" && realpath -e ../objdiff/target/release/objdiff-cli 2>/dev/null) || true)"
if [[ -z "${BUILD_DTK}" || -z "${BUILD_OBJDIFF}" ]]; then
    echo "ERROR: cannot determine which dtk/objdiff-cli ${CURRENT_DIR} builds with." >&2
    echo "       dtk='${BUILD_DTK:-<none>}' objdiff='${BUILD_OBJDIFF:-<none>}'" >&2
    echo "       Without them the baseline would be built by different tools than the" >&2
    echo "       current side, which invents differences. Run 'ninja' there first." >&2
    exit 1
fi
DTK_SHA="$(sha_of "${BUILD_DTK}")"
OBJDIFF_SHA="$(sha_of "${BUILD_OBJDIFF}")"

echo "  baseline : ${BASELINE_COMMIT}"
echo "  current  : ${CURRENT_DIR} @ ${CURRENT_HEAD} (${CURRENT_DIRTY} tracked file(s) modified)"
echo "  dtk      : ${DTK_SHA:0:12}  ${BUILD_DTK:-<unresolved>}"
echo "  objdiff  : ${OBJDIFF_SHA:0:12}  ${BUILD_OBJDIFF:-<unresolved>}"

# A worktree that splits with a different dtk than the main repo produces a
# report that differs from main's for tool reasons alone. Say so.
if [[ "${CURRENT_DIR}" != "${MAIN_REPO}" ]]; then
    MAIN_DTK="$(dtk_of_dir "${MAIN_REPO}" || true)"
    if [[ -n "${MAIN_DTK}" && "$(sha_of "${MAIN_DTK}")" != "${DTK_SHA}" ]]; then
        echo "  WARNING: this tree splits with a different dtk than the main repo:"
        echo "             current : ${BUILD_DTK}"
        echo "             main    : ${MAIN_DTK}"
        echo "           Function boundaries can differ, so comparing this tree's numbers"
        echo "           against main's directly will show phantom diffs. (The baseline"
        echo "           built below uses this tree's dtk, so THIS comparison is sound.)"
    fi
fi
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

    # --- Create worktree if it isn't already a git worktree ---
    #
    # The test here has to be "is this a git worktree", NOT "does this path
    # exist". Anything can leave the path behind -- a killed run, a `mkdir -p`
    # from another script, a tmpfiles sweep that recreates the parent -- and an
    # empty leftover directory used to make this branch skip `worktree add`
    # entirely and then fail on the very next line with
    #
    #     fatal: not a git repository (or any parent up to mount point /)
    #
    # exiting 128 with no hint that the cause was a stale directory. It looked
    # like a --current-dir bug (it is not: it hits plain baseline builds too,
    # since this block runs whenever the baseline is not cached).
    if git -C "${WORKTREE}" rev-parse --git-dir >/dev/null 2>&1; then
        :
    else
        if [[ -e "${WORKTREE}" ]]; then
            if [[ -d "${WORKTREE}" ]] && [[ -z "$(ls -A "${WORKTREE}" 2>/dev/null)" ]]; then
                echo "Removing empty non-worktree leftover at ${WORKTREE}..."
                rmdir "${WORKTREE}"
            else
                echo "Error: ${WORKTREE} exists but is not a git worktree, and is not empty."
                echo "       Inspect it and remove it, or pass --worktree <other path>."
                exit 1
            fi
        fi
        # A worktree git still has registered but whose directory is gone would
        # make `worktree add` refuse; prune those first.
        git -C "${MAIN_REPO}" worktree prune >/dev/null 2>&1 || true
        echo "Creating worktree at ${WORKTREE}..."
        WT_LOG="$(mktemp -t measure_progress_worktree.XXXXXX.log)"
        if ! git -C "${MAIN_REPO}" worktree add --detach "${WORKTREE}" HEAD --quiet \
             >"${WT_LOG}" 2>&1; then
            build_fail "${EXIT_BASELINE_BUILD}" "git worktree add --detach" \
                       "${WORKTREE}" "${WT_LOG}"
        fi
        rm -f "${WT_LOG}" 2>/dev/null || true
        CREATED_WORKTREE=1
    fi

    # --- Save worktree state for restoration ---
    # The cleanup itself lives in worktree_cleanup(), called from the single
    # on_exit trap installed near the top, so that the failure verdict is the
    # LAST line of a failed run rather than being buried under this chatter.
    ORIGINAL_COMMIT=$(git -C "${WORKTREE}" rev-parse HEAD)
    WORKTREE_ACTIVE=1

    # --- The baseline worktree must own its own scripts/ ---------------------
    #
    # This script used to REPLACE the baseline worktree's scripts/ with a
    # symlink to the main checkout's, and restore it after every reset. Two
    # things were wrong with that, found 2026-09-11:
    #
    #  1. Every guard and patcher the baseline build runs is invoked by ninja
    #     as `python3 scripts/<name>.py` with cwd set to this worktree. They
    #     derived their project root from `Path(__file__).resolve()`, which
    #     FOLLOWS the symlink -- so they acted on the MAIN checkout. Measured
    #     in a baseline-shaped tree holding one object and its own matching
    #     manifest: `verify_objs_patched.py --verify-manifest` exited 1 quoting
    #     main's 990-object manifest and 817 drifted objects, and
    #     `verify_split_current.py --check` said "split current" for a tree
    #     that had never been split. The patcher edges run with `--apply`, so
    #     the baseline build was also a WRITER into main's build directory.
    #     (scripts/project_root.py now fixes the root resolution itself; this
    #     is the other half, and either alone would have been enough.)
    #  2. main's scripts + a baseline commit's sources is a tree that no commit
    #     reproduces. The dtk/objdiff forcing below exists because those are
    #     external, unversioned tools; the in-repo patchers are versioned, and
    #     a patcher change between the baseline and now is a real part of the
    #     delta being measured, not a tool skew to be normalised away.
    #
    # So: if a previous run (or an older version of this script) left a
    # symlink here, drop it BEFORE the reset -- `git reset --hard` cannot check
    # files out through a symlinked directory.
    if [[ -L "${WORKTREE}/scripts" ]]; then
        echo "Replacing legacy scripts/ symlink with this worktree's own checkout..."
        rm -f "${WORKTREE}/scripts"
    fi

    # --- Reset worktree to baseline commit ---
    echo "Resetting worktree to baseline ${BASELINE_SHORT}..."
    RESET_LOG="$(mktemp -t measure_progress_reset.XXXXXX.log)"
    if ! git -C "${WORKTREE}" reset --hard --quiet "${BASELINE_COMMIT}" >"${RESET_LOG}" 2>&1; then
        build_fail "${EXIT_BASELINE_BUILD}" "git reset --hard ${BASELINE_SHORT}" \
                   "${WORKTREE}" "${RESET_LOG}"
    fi
    rm -f "${RESET_LOG}" 2>/dev/null || true

    # The reset restores scripts/ from the baseline commit. Insist on it: a
    # build whose guards are missing is a build whose guards do not run.
    if [[ ! -d "${WORKTREE}/scripts" || -L "${WORKTREE}/scripts" ]]; then
        echo "Error: ${WORKTREE}/scripts is not a real directory after reset." >&2
        echo "       The baseline build's guards and patchers would act on" >&2
        echo "       whatever tree that path resolves to. Refusing." >&2
        build_fail "${EXIT_BASELINE_BUILD}" "scripts/ is not a real directory after reset" \
                   "${WORKTREE}"
    fi

    # Clean untracked source files but preserve build artifacts and symlinks.
    # scripts/ is deliberately NOT excluded any more: it is an ordinary tracked
    # directory here now, and leaving untracked leftovers (a newer commit's
    # helper module, a stale __pycache__) inside it is how a baseline build
    # ends up running code from no commit at all.
    git -C "${WORKTREE}" clean -fd \
        --exclude=build/ \
        --exclude=bin/ \
        --exclude=orig \
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

    # --- Ensure build tools and compilers are available (avoid downloads) ---
    mkdir -p "${WORKTREE}/build/tools" "${WORKTREE}/build/373307D9/pch"
    # Pre-create empty PCH file — WIBO_FS_CACHE=1 breaks creating new files in
    # case-insensitive path components (373307D9). cl.exe can overwrite existing files fine.
    touch "${WORKTREE}/build/373307D9/pch/system.pch"
    for tool in "${MAIN_REPO}/build/tools"/*; do
        # An unmatched glob stays literal here (nullglob is off), which used to
        # plant a broken symlink named `*` in the baseline worktree.
        [[ -e "$tool" ]] || continue
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

    # Drop any --dtk/--objdiff inherited from the main repo's configure_args.
    # They are replaced below by the *current* side's binaries so both halves of
    # the comparison are produced by the same tools. Leaving both in relied on
    # configure.py preferring the last occurrence while the explicit split step
    # below picked the first — i.e. the two split invocations could disagree.
    FILTERED_ARGS=()
    skip_next=0
    for arg in "${CONFIGURE_ARGS[@]+"${CONFIGURE_ARGS[@]}"}"; do
        if [[ "${skip_next}" -eq 1 ]]; then skip_next=0; continue; fi
        if [[ "${arg}" == "--dtk" || "${arg}" == "--objdiff" ]]; then skip_next=1; continue; fi
        FILTERED_ARGS+=("${arg}")
    done
    CONFIGURE_ARGS=("${FILTERED_ARGS[@]+"${FILTERED_ARGS[@]}"}")

    [[ -n "${BUILD_DTK}" ]] && CONFIGURE_ARGS+=("--dtk" "${BUILD_DTK}")
    [[ -n "${BUILD_OBJDIFF}" ]] && CONFIGURE_ARGS+=("--objdiff" "${BUILD_OBJDIFF}")
    if abs="$(resolve_tool "../wibo/build/release/wibo")"; then
        CONFIGURE_ARGS+=("--wrapper" "${abs}")
    fi

    echo "Using configure args: ${CONFIGURE_ARGS[*]}"

    # Same dtk for the explicit split step as for the generated ninja manifest.
    # Running it here (instead of only via ninja) gives a clear error path when
    # the split fails and build/373307D9/config.json is never produced.
    DTK_BIN="${BUILD_DTK}"

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
            build_fail "${EXIT_BASELINE_BUILD}" "dtk xex split (baseline config)" \
                       "${WORKTREE}" "${SPLIT_LOG}"
        fi
        rm -f "${SPLIT_LOG}" 2>/dev/null || true
    else
        build_fail "${EXIT_BASELINE_BUILD}" \
                   "dtk binary '${DTK_BIN:-<unresolved>}' is missing or not executable" \
                   "${WORKTREE}"
    fi

    # --- Reconfigure for baseline's file set ---
    # configure.py's output went to /dev/null, so `set -e` killed the run with
    # a bare 1 and the reason -- which configure.py prints on stdout -- was
    # gone. Keep it.
    echo "Reconfiguring baseline..."
    CONFIGURE_LOG="$(mktemp -t measure_progress_configure.XXXXXX.log)"
    if ! (cd "${WORKTREE}" && python3 configure.py "${CONFIGURE_ARGS[@]}") \
         >"${CONFIGURE_LOG}" 2>&1; then
        build_fail "${EXIT_BASELINE_BUILD}" "python3 configure.py (baseline)" \
                   "${WORKTREE}" "${CONFIGURE_LOG}"
    fi
    rm -f "${CONFIGURE_LOG}" 2>/dev/null || true

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
        if grep -q "manifest 'build.ninja' still dirty" "${BUILD_LOG}" && \
           grep -q "output build/373307D9/config.json doesn't exist" "${BUILD_LOG}"; then
            echo "" >&2
            echo "Hint: ninja's manifest-dirty loop is usually a secondary symptom." >&2
            echo "      The baseline split step failed, so build/373307D9/config.json was never created." >&2
        fi
        build_fail "${EXIT_BASELINE_BUILD}" "ninja ${REPORT_REL} (baseline build)" \
                   "${WORKTREE}" "${BUILD_LOG}"
    fi
    rm -f "${BUILD_LOG}" 2>/dev/null || true

    if [[ ! -f "${WORKTREE}/${REPORT_REL}" ]]; then
        build_fail "${EXIT_BASELINE_BUILD}" \
                   "ninja reported success but ${REPORT_REL} was never produced" \
                   "${WORKTREE}"
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
#
# The table is BUFFERED and only printed once the comparison has succeeded AND
# the race check has passed. compare_progress.py used to write straight to the
# terminal, so a crash partway through left a truncated table on screen that
# looks exactly like a complete one, and a report rewritten mid-diff was
# reported only AFTER its numbers had already been read.
COMPARE_OUT="$(mktemp -t measure_progress_table.XXXXXX.txt)"
COMPARE_RC=0
python3 "${MAIN_REPO}/scripts/analysis/compare_progress.py" \
    "${COMPARE_FLAGS[@]}" \
    "${BASELINE_REPORT}" \
    "${CURRENT_REPORT}" >"${COMPARE_OUT}" 2>&1 || COMPARE_RC=$?

if [[ "${COMPARE_RC}" -ne 0 ]]; then
    FAIL_STEP="compare_progress.py"
    {
        echo ""
        echo "=============================================================================="
        echo "COMPARISON FAILED — compare_progress.py exited ${COMPARE_RC}"
        echo "  baseline : ${BASELINE_REPORT}"
        echo "  current  : ${CURRENT_REPORT}"
        echo "  exit code: ${EXIT_COMPARE}"
        echo "=============================================================================="
        echo "--- last 60 lines of its output (SUPPRESSED as a table: it is incomplete) ---"
        tail -60 "${COMPARE_OUT}" || true
        echo "--- end ---"
    } >&2
    rm -f "${COMPARE_OUT}" 2>/dev/null || true
    exit "${EXIT_COMPARE}"
fi

if [[ "$(fingerprint_of "${BASELINE_REPORT}")" != "${BASELINE_FP_BEFORE}" ]]; then
    stale_fail "the baseline report ${BASELINE_REPORT} was rewritten while it was being compared — the numbers are from a racing build and have been WITHHELD."
fi
if [[ "$(fingerprint_of "${CURRENT_REPORT}")" != "${CURRENT_FP_BEFORE}" ]]; then
    stale_fail "the current report ${CURRENT_REPORT} was rewritten while it was being compared — the numbers are from a racing build and have been WITHHELD."
fi

echo ""
cat "${COMPARE_OUT}"
rm -f "${COMPARE_OUT}" 2>/dev/null || true
echo ""
echo "--- end of comparison (measure_progress.sh completed: baseline ${BASELINE_SHORT} -> ${CURRENT_LABEL}) ---"
