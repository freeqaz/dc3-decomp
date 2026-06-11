#!/usr/bin/env bash
# nightly_measurement_guard.sh — nightly drift detector for decomp metrics.
#
# Runs in three layers:
#   (a) [always] python3 scripts/reconcile_db.py  — exits nonzero if DB drifts
#       from report.json (percent mismatch, stale COMPLETE, stale is_stub).
#   (b) [--strict] regenerate report_strict.json with the objdiff fork's NameOnly
#       mode, then run scripts/analysis/reloc_strict_classify.py --jobs 30 and
#       alert if genuine_wrong_target (authorable) grew vs a checked-in baseline.
#   (c) [--unicorn] run the unicorn behavioral refresh cadence (Wave-3 Lane B,
#       Wave-4 Lane D). After any sync that moves percents, the unicorn verdicts
#       should be refreshed so floor certs never go stale. Steps:
#         1. refresh_frontier.py --run   (~33s) — re-emulates the authorable
#            partial frontier, writes a worktree-local results DB + flip list.
#         2. apply_refresh.py --only-fresh-source  — updates only rows whose
#            unicorn_source_hash changed (skips unchanged codegen, safe to run
#            after every sync).
#         3. reconcile_db.py --fix  — clear any now-stale floor certs and
#            verdict drift introduced by the new verdicts.
#         4. certify_floor.py --apply  — re-cert from fresh unicorn evidence.
#
#       DRY-RUN SUPPORT: pass --unicorn --dry-run to run steps 1 (emulate) and
#       preview steps 2-4 WITHOUT writing the live decomp.db. All writes go to a
#       temporary copy under /tmp/nightly_guard_XXXXXX/decomp_dryrun.db.
#
#       SAFETY: steps 2-4 are single-writer operations intended for the
#       orchestrator on main. Do NOT run --unicorn --apply in a worktree that
#       shares the main decomp.db if other agents may be writing concurrently.
#       The flag --unicorn-apply enables writes; without it the unicorn stage is
#       always a dry-run (preview only).
#
#       Do NOT install a crontab for --unicorn — trigger it manually after each
#       merge + sync cycle (scripts/sync_match_percent.py --build --promote must
#       run first; the unicorn source_hash gates off match_percent_normalized).
#
# WIRING INSTRUCTIONS (do NOT install a crontab — reference only):
#   Cron/nightly (reconcile + strict only):
#     0 4 * * * cd /path/to/dc3-decomp && bash scripts/nightly_measurement_guard.sh --strict 2>&1 | tee /tmp/dc3-nightly.log
#   Post-merge unicorn cadence (run manually after sync, orchestrator only):
#     bash scripts/nightly_measurement_guard.sh --unicorn --unicorn-apply
#   Pre-merge CI check:
#     bash scripts/nightly_measurement_guard.sh   # (no --strict; fast, read-only)
#   Unicorn dry-run preview (safe in any worktree):
#     bash scripts/nightly_measurement_guard.sh --unicorn
#   Ninja post-build (add to build.ninja phony "post-build" edge):
#     rule reconcile
#       command = python3 $root/scripts/reconcile_db.py
#       description = RECONCILE DB
#     build reconcile_db.stamp: reconcile build/373307D9/report.json | scripts/reconcile_db.py
#
# Exit codes:
#   0  clean — no drift
#   1  drift detected, strict alert, or unicorn flip-list has candidate bugs
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
UNICORN=0
UNICORN_APPLY=0
VERBOSE=0
for arg in "$@"; do
    case "$arg" in
        --strict) STRICT=1 ;;
        --unicorn) UNICORN=1 ;;
        --unicorn-apply) UNICORN_APPLY=1 ;;
        -v|--verbose) VERBOSE=1 ;;
        -h|--help)
            echo "Usage: $0 [--strict] [--unicorn] [--unicorn-apply] [-v]"
            echo "  --strict         regenerate report_strict.json and check genuine_wrong_target"
            echo "  --unicorn        run unicorn refresh cadence (dry-run preview)"
            echo "  --unicorn-apply  write results to live decomp.db (orchestrator only)"
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
# Layer (c): unicorn refresh cadence (only with --unicorn)
#
# Steps:
#   1. refresh_frontier.py --run  (~33s, writes results to a temp dir)
#   2. apply_refresh.py --only-fresh-source [--apply if --unicorn-apply]
#   3. reconcile_db.py --fix [only if --unicorn-apply]
#   4. certify_floor.py --apply [only if --unicorn-apply]
#
# Without --unicorn-apply: steps 2-4 are previewed against a DB copy (dry-run).
# The DB copy is created in a temp dir and discarded at script exit.
# ────────────────────────────────────────────────────────────────────────────
UNICORN_EXIT=0
if [[ $UNICORN -eq 1 ]]; then
    echo ""
    echo "=== [unicorn] Unicorn behavioral refresh cadence ==="

    # Determine the DB target for apply steps:
    # dry-run → a temp copy; --unicorn-apply → the live decomp.db
    UNICORN_TMPDIR=""
    if [[ $UNICORN_APPLY -eq 1 ]]; then
        UNICORN_DB="${DECOMP_DB}"
        echo "[unicorn] APPLY mode — writes will go to LIVE decomp.db: ${UNICORN_DB}"
        echo "          (orchestrator single-writer gate: ensure no concurrent writers)"
    else
        UNICORN_TMPDIR="$(mktemp -d /tmp/nightly_guard_XXXXXX)"
        UNICORN_DB="${UNICORN_TMPDIR}/decomp_dryrun.db"
        cp "${DECOMP_DB}" "${UNICORN_DB}"
        echo "[unicorn] DRY-RUN mode — writes go to temp copy: ${UNICORN_DB}"
        echo "          (pass --unicorn-apply to write the live decomp.db)"
    fi

    # Results DB lives in the temp dir (or a fixed location if UNICORN_OUT_DB is set).
    UNICORN_RESULTS_DB="${UNICORN_TMPDIR:-${REPO_ROOT}/build/373307D9}/unicorn_refresh_nightly.db"
    UNICORN_RESULTS_JSON="${UNICORN_TMPDIR:-${REPO_ROOT}/build/373307D9}/unicorn_refresh_nightly.json"

    # Step 1: refresh_frontier.py --run
    echo ""
    echo "=== [unicorn step 1] refresh_frontier.py --run (~33s) ==="
    UNICORN_REFRESH_EXIT=0
    if python3 "${SCRIPT_DIR}/unicorn/refresh_frontier.py" \
            --run \
            --live-db "${DECOMP_DB}" \
            --out-db  "${UNICORN_RESULTS_DB}" \
            --json    "${UNICORN_RESULTS_JSON}"; then
        echo "[unicorn step 1] Refresh complete. Results: ${UNICORN_RESULTS_DB}"
    else
        echo "[unicorn step 1] FAILED — refresh_frontier.py returned non-zero." >&2
        UNICORN_REFRESH_EXIT=1
        UNICORN_EXIT=1
    fi

    if [[ $UNICORN_REFRESH_EXIT -eq 0 ]]; then
        # Emit flip-list summary (candidate bugs are the key signal).
        CANDIDATE_BUGS=$(python3 - <<PYEOF 2>/dev/null || echo "?"
import json
with open("${UNICORN_RESULTS_JSON}") as f:
    d = json.load(f)
print(d["summary"].get("flip_cause_candidate_bug", "?"))
PYEOF
)
        echo "[unicorn step 1] candidate_bug flips in this run: ${CANDIDATE_BUGS}"
        if [[ "${CANDIDATE_BUGS}" != "?" && "${CANDIDATE_BUGS}" -gt 0 ]]; then
            echo "[unicorn step 1] ALERT: ${CANDIDATE_BUGS} new candidate bug(s) found." \
                 "Review ${UNICORN_RESULTS_JSON} (flip_cause=candidate_bug rows)." >&2
            UNICORN_EXIT=1
        fi

        # Step 2: apply_refresh.py --only-fresh-source [--apply]
        echo ""
        echo "=== [unicorn step 2] apply_refresh.py --only-fresh-source ==="
        APPLY_ARGS=(
            --results "${UNICORN_RESULTS_DB}"
            --db      "${UNICORN_DB}"
            --only-fresh-source
        )
        if [[ $UNICORN_APPLY -eq 1 ]]; then
            APPLY_ARGS+=(--apply)
            echo "[unicorn step 2] APPLY mode — writing to ${UNICORN_DB}"
        else
            echo "[unicorn step 2] DRY-RUN — preview only (no writes)"
        fi
        if python3 "${SCRIPT_DIR}/unicorn/apply_refresh.py" "${APPLY_ARGS[@]}"; then
            echo "[unicorn step 2] apply_refresh.py complete."
        else
            echo "[unicorn step 2] FAILED — apply_refresh.py returned non-zero." >&2
            UNICORN_EXIT=1
        fi

        # Step 3: reconcile_db.py --fix (only in apply mode; dry-run in preview)
        echo ""
        echo "=== [unicorn step 3] reconcile_db.py --fix ==="
        RECON_ARGS=(--db "${UNICORN_DB}" --report "${REPORT_JSON}")
        if [[ $UNICORN_APPLY -eq 1 ]]; then
            RECON_ARGS+=(--fix)
            echo "[unicorn step 3] APPLY mode — fixing drift in ${UNICORN_DB}"
        else
            echo "[unicorn step 3] DRY-RUN — checking drift only (no writes)"
        fi
        if [[ $VERBOSE -eq 1 ]]; then
            RECON_ARGS+=(-v)
        fi
        if python3 "${SCRIPT_DIR}/reconcile_db.py" "${RECON_ARGS[@]}"; then
            echo "[unicorn step 3] reconcile_db.py: clean."
        else
            echo "[unicorn step 3] reconcile_db.py: drift detected (expected after refresh)." >&2
            # In apply mode this is a real problem; in dry-run it is just a preview.
            if [[ $UNICORN_APPLY -eq 1 ]]; then
                UNICORN_EXIT=1
            fi
        fi

        # Step 4: certify_floor.py --apply (only in apply mode)
        echo ""
        echo "=== [unicorn step 4] certify_floor.py ==="
        CERT_ARGS=(--db "${UNICORN_DB}")
        if [[ $UNICORN_APPLY -eq 1 ]]; then
            CERT_ARGS+=(--apply)
            echo "[unicorn step 4] APPLY mode — writing floor certs to ${UNICORN_DB}"
        else
            echo "[unicorn step 4] DRY-RUN — previewing cert changes (no writes)"
        fi
        if python3 "${SCRIPT_DIR}/certify_floor.py" "${CERT_ARGS[@]}"; then
            echo "[unicorn step 4] certify_floor.py complete."
        else
            echo "[unicorn step 4] FAILED — certify_floor.py returned non-zero." >&2
            UNICORN_EXIT=1
        fi
    fi

    # Cleanup temp dir (dry-run only — the copy is no longer needed)
    if [[ -n "${UNICORN_TMPDIR}" && -d "${UNICORN_TMPDIR}" ]]; then
        rm -rf "${UNICORN_TMPDIR}"
        echo "[unicorn] Temp DB copy discarded."
    fi

    if [[ $UNICORN_EXIT -eq 0 ]]; then
        echo "[unicorn] Cadence complete — no candidate bugs found."
    else
        echo "[unicorn] Cadence finished with alerts (see above)." >&2
    fi
fi

# ────────────────────────────────────────────────────────────────────────────
# Final exit
# ────────────────────────────────────────────────────────────────────────────
echo ""
if [[ $RECONCILE_EXIT -eq 0 && $STRICT_EXIT -eq 0 && $UNICORN_EXIT -eq 0 ]]; then
    echo "nightly_measurement_guard: ALL CHECKS PASSED."
    exit 0
else
    echo "nightly_measurement_guard: CHECKS FAILED (reconcile=${RECONCILE_EXIT} strict=${STRICT_EXIT} unicorn=${UNICORN_EXIT})." >&2
    exit 1
fi
