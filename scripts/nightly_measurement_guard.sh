#!/usr/bin/env bash
# nightly_measurement_guard.sh — nightly drift detector for decomp metrics.
#
# Runs in two layers:
#   (a) [always] python3 scripts/reconcile_db.py  — exits nonzero if DB drifts
#       from report.json (percent mismatch, stale COMPLETE, stale is_stub).
#   (b) [--strict] regenerate report_strict.json with the objdiff fork's NameOnly
#       mode, then run scripts/analysis/reloc_strict_classify.py --jobs 30 and
#       alert if genuine_wrong_target (authorable) grew vs a checked-in baseline.
#
# WIRING INSTRUCTIONS (do NOT install a crontab — reference only):
#   Cron/nightly:
#     0 4 * * * cd /path/to/dc3-decomp && bash scripts/nightly_measurement_guard.sh --strict 2>&1 | tee /tmp/dc3-nightly.log
#   Ninja post-build (add to build.ninja phony "post-build" edge):
#     rule reconcile
#       command = python3 $root/scripts/reconcile_db.py
#       description = RECONCILE DB
#     build reconcile_db.stamp: reconcile build/373307D9/report.json | scripts/reconcile_db.py
#   Pre-merge CI check:
#     bash scripts/nightly_measurement_guard.sh   # (no --strict; fast, read-only)
#
# Exit codes:
#   0  clean — no drift
#   1  drift detected or strict alert
#   2  missing prerequisite (report.json, DB, objdiff binary, etc.)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Resolve the main repo root through the git common-dir so this works from
# worktrees (which share the main repo's decomp.db).
# --git-common-dir returns the main .git dir, e.g. /main-repo/.git
GIT_COMMON_DIR="$(git -C "${REPO_ROOT}" rev-parse --git-common-dir 2>/dev/null || true)"
if [[ -n "${GIT_COMMON_DIR}" ]]; then
    # Strip trailing "/.git" to get the main repo root
    MAIN_REPO_ROOT="$(cd "${GIT_COMMON_DIR}" && cd .. && pwd)"
else
    MAIN_REPO_ROOT="${REPO_ROOT}"
fi

REPORT_JSON="${REPO_ROOT}/build/373307D9/report.json"
# Always use the canonical decomp.db from the main repo (worktrees share it
# via git-common-dir; reads are safe per global rules).
DECOMP_DB="${MAIN_REPO_ROOT}/decomp.db"
STRICT_REPORT="${REPO_ROOT}/build/373307D9/report_strict.json"
STRICT_BASELINE_DIR="${MAIN_REPO_ROOT}/scripts/analysis/baselines"
STRICT_BASELINE="${STRICT_BASELINE_DIR}/genuine_wrong_target_baseline.txt"

# The objdiff fork binary with NameOnly support (wave-1/c landed at 72b553f).
# bin/objdiff-cli is a symlink -> objdiff fork in both main repo and worktrees.
OBJDIFF_FORK="${REPO_ROOT}/bin/objdiff-cli"

STRICT=0
VERBOSE=0
for arg in "$@"; do
    case "$arg" in
        --strict) STRICT=1 ;;
        -v|--verbose) VERBOSE=1 ;;
        -h|--help)
            echo "Usage: $0 [--strict] [-v]"
            echo "  --strict  also regenerate report_strict.json and check genuine_wrong_target"
            exit 0
            ;;
    esac
done

# ────────────────────────────────────────────────────────────────────────────
# Prerequisite checks
# ────────────────────────────────────────────────────────────────────────────
fail=0
if [[ ! -f "${REPORT_JSON}" ]]; then
    echo "ERROR: report.json not found: ${REPORT_JSON}" >&2
    echo "       Run 'ninja build/373307D9/report.json' first." >&2
    fail=1
fi
if [[ ! -f "${DECOMP_DB}" ]]; then
    echo "ERROR: decomp.db not found: ${DECOMP_DB}" >&2
    fail=1
fi
if [[ $fail -eq 1 ]]; then
    exit 2
fi

# ────────────────────────────────────────────────────────────────────────────
# Layer (a): reconcile_db.py — drift check (always)
# ────────────────────────────────────────────────────────────────────────────
echo "=== [reconcile] Checking DB vs report.json drift ==="
RECONCILE_ARGS=(--db "${DECOMP_DB}" --report "${REPORT_JSON}")
if [[ $VERBOSE -eq 1 ]]; then
    RECONCILE_ARGS+=(-v)
fi

if python3 "${SCRIPT_DIR}/reconcile_db.py" "${RECONCILE_ARGS[@]}"; then
    echo "[reconcile] CLEAN — no drift detected."
    RECONCILE_EXIT=0
else
    echo "[reconcile] DRIFT DETECTED — re-run sync_match_percent.py then reconcile_db.py --fix." >&2
    RECONCILE_EXIT=1
fi

# ────────────────────────────────────────────────────────────────────────────
# Layer (b): strict-reloc recert (only with --strict)
# ────────────────────────────────────────────────────────────────────────────
STRICT_EXIT=0
if [[ $STRICT -eq 1 ]]; then
    echo ""
    echo "=== [strict] Regenerating report_strict.json (NameOnly reloc mode) ==="

    if [[ ! -x "${OBJDIFF_FORK}" ]]; then
        echo "ERROR: objdiff fork binary not found or not executable: ${OBJDIFF_FORK}" >&2
        echo "       Expected the fork at 72b553f (wave-1/c) to be symlinked to bin/objdiff-cli." >&2
        exit 2
    fi

    # Regenerate report_strict.json with NameOnly mode.
    # This uses the same project dir as the standard report but overrides functionRelocDiffs.
    "${OBJDIFF_FORK}" report generate \
        -c "functionRelocDiffs=name_only" \
        -o "${STRICT_REPORT}" \
        2>&1 | grep -v "^$"

    echo "[strict] report_strict.json written to ${STRICT_REPORT}"

    # Run the classifier.
    CLASSIFY_OUT="${REPO_ROOT}/build/373307D9/reloc_strict_classify.json"
    echo ""
    echo "=== [strict] Running reloc_strict_classify.py --jobs 30 ==="
    python3 "${SCRIPT_DIR}/analysis/reloc_strict_classify.py" \
        --lenient "${REPORT_JSON}" \
        --strict  "${STRICT_REPORT}" \
        --objdiff "${OBJDIFF_FORK}" \
        --project "${REPO_ROOT}" \
        --out     "${CLASSIFY_OUT}" \
        --jobs    30

    # Extract genuine_wrong_target authorable count from the JSON output.
    GENUINE_COUNT=$(python3 - <<PYEOF
import json, sys
with open("${CLASSIFY_OUT}") as f:
    d = json.load(f)
print(d["summary"]["genuine_wrong_target_authorable"])
PYEOF
)

    echo ""
    echo "[strict] genuine_wrong_target (authorable): ${GENUINE_COUNT}"

    # Compare against baseline.
    if [[ -f "${STRICT_BASELINE}" ]]; then
        BASELINE_COUNT=$(cat "${STRICT_BASELINE}")
        if [[ "${GENUINE_COUNT}" -gt "${BASELINE_COUNT}" ]]; then
            echo "ALERT: genuine_wrong_target grew ${BASELINE_COUNT} -> ${GENUINE_COUNT}!" >&2
            echo "       Inspect ${CLASSIFY_OUT} for new non-ICF entries." >&2
            STRICT_EXIT=1
        else
            echo "[strict] OK — genuine_wrong_target ${GENUINE_COUNT} <= baseline ${BASELINE_COUNT}."
        fi
    else
        echo "[strict] No baseline found at ${STRICT_BASELINE}."
        echo "         Creating baseline with current count: ${GENUINE_COUNT}"
        mkdir -p "${STRICT_BASELINE_DIR}"
        echo "${GENUINE_COUNT}" > "${STRICT_BASELINE}"
        echo "         Baseline written. Re-run tomorrow to catch regressions."
    fi
fi

# ────────────────────────────────────────────────────────────────────────────
# Final exit
# ────────────────────────────────────────────────────────────────────────────
echo ""
if [[ $RECONCILE_EXIT -eq 0 && $STRICT_EXIT -eq 0 ]]; then
    echo "nightly_measurement_guard: ALL CHECKS PASSED."
    exit 0
else
    echo "nightly_measurement_guard: CHECKS FAILED (reconcile=${RECONCILE_EXIT} strict=${STRICT_EXIT})." >&2
    exit 1
fi
