#!/usr/bin/env python3
"""Refresh unicorn behavioral verdicts over the authorable floor-cert frontier.

Wave-3 Lane B (94 follow-up #4, roadmap 0.8). 843 of 970 floor certs rest on
~98-day-old unicorn data; 335 frontier fns have no evidence at all. This re-runs
the SAME probe schedule `batch_to_db.py` used (zero-fill + 0xCD-fill, zero args)
over the authorable partial frontier, stamps each result with a per-function
SOURCE-HASH (scripts/unicorn/source_hash.py) so future staleness is detectable
by codegen change — not just by date — and writes to a SEPARATE results database
(NEVER the live decomp.db).

It then compares fresh verdicts against the live DB's prior (mostly stale)
verdicts and emits the FLIP LIST: functions whose behavioral verdict changed.
Each EQUIVALENT->DIVERGENT flip is a candidate real bug hiding under a floor cert.

Rule 2 (wave 1/3): this script NEVER writes the live decomp.db. It reads it
(absolute path), writes results to a worktree-local sqlite + JSON, and emits the
exact `--apply` runbook for the orchestrator to merge on main.

Usage:
    # Dry-run census of what WOULD be refreshed (no emulation):
    python3 scripts/unicorn/refresh_frontier.py --plan

    # Run the sweep over the frontier, write results DB + json + flip list:
    python3 scripts/unicorn/refresh_frontier.py --run \\
        --out-db /home/free/code/milohax/wt-wave3-b-unicorn-refresh/unicorn_refresh.db \\
        --json   /home/free/code/milohax/wt-wave3-b-unicorn-refresh/unicorn_refresh.json \\
        -j 12

    # Limit to N units (smoke):
    python3 scripts/unicorn/refresh_frontier.py --run --limit-units 5 ...
"""
from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime, timezone

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# NOTE: deliberately do NOT put PROJECT_ROOT/scripts on sys.path — that package
# dir contains `unicorn/` (this package), which would shadow the `unicorn`
# engine bindings module the runner imports. Import via the `scripts.*` package
# path instead (needs PROJECT_ROOT on path), and put the unicorn bindings FIRST.
sys.path.insert(0, PROJECT_ROOT)
# The bindings location is resolved (env override -> repo-adjacent -> the real
# repo's sibling when this is a worktree -> ~/code/milohax) rather than
# hardcoded to one absolute machine path, and is inserted at sys.path[0] so it
# outranks the shadow described above.
from scripts.unicorn_runner.unicorn_dep import ensure_unicorn_on_path  # noqa: E402

ensure_unicorn_on_path()

from scripts.authorable import SDK_UNIT_PREFIXES  # noqa: E402
from scripts.unicorn_runner.run import (  # noqa: E402
    get_all_units, resolve_unit,
    _find_common_text_symbols, _run_comparison_core,
    EXIT_EQUIVALENT, EXIT_DIVERGENT, EXIT_ERROR, EXIT_SKIPPED,
)
from scripts.unicorn_runner.coff import COFFParser  # noqa: E402
from scripts.unicorn_runner.comparator import classify_divergence  # noqa: E402
from scripts.unicorn_runner.memory_map import FILL_BYTE  # noqa: E402
from scripts.unicorn_runner.signal_version import (  # noqa: E402
    SIGNAL_VERSION, HARNESS_VERSION, compute_schedule_hash,
)
from scripts.unicorn.source_hash import function_source_hash  # noqa: E402

LIVE_DB = "/home/free/code/milohax/dc3-decomp/decomp.db"

# Same schedule batch_to_db.py uses (zero-fill + 0xCD-fill, zero args).
_SCHEDULE = [
    {"fill_pattern": None, "fixture_type": "fill", "arg_r4": 0, "arg_r5": 0, "arg_r6": 0},
    {"fill_pattern": FILL_BYTE, "fixture_type": "fill", "arg_r4": 0, "arg_r5": 0, "arg_r6": 0},
]
_SCHEDULE_HASH = compute_schedule_hash(_SCHEDULE)

ARTIFACT_PREFIXES = ("merged_", "lbl_", "fn_", "??_")


def _like_prefix_clause(column: str, prefix: str, negate: bool = True) -> str:
    """LIKE-prefix SQL with the '_'/'%'/'~' wildcards in the prefix ESCAPED.

    SQL LIKE treats '_' as a single-char wildcard, so a naive
    ``symbol NOT LIKE '??_%'`` excludes EVERY '??'-prefixed symbol (ctors ??0,
    dtors ??1, operators ??4...), not just the literal '??_' artifact prefix.
    That is the wave-9 measurement bug — it hid 108 authorable '??' partial
    functions from the unicorn frontier here. Mirror certify_floor.like_prefix_clause.
    """
    esc = prefix.replace("~", "~~").replace("_", "~_").replace("%", "~%")
    op = "NOT LIKE" if negate else "LIKE"
    return f"{column} {op} '{esc}%' ESCAPE '~'"


SCOPES = ("frontier", "all")


def is_authorable_sql(scope: str = "frontier") -> str:
    """Row filter for the sweep.

    scope='frontier' (default, historical behaviour): the AUTHORABLE PARTIAL
        frontier only — 0 < match% < 100, no SDK units, no artifact-prefix
        symbols. ~1,830 rows.
    scope='all': every row that a verdict could apply to, i.e. every non-stub
        function that appears in the diff AT ALL (including 100%-matched and
        0%-matched), PLUS every row that already carries a unicorn verdict
        (so a re-ingest can overwrite stale evidence wherever it lives, rather
        than refreshing the frontier and leaving ~26k pre-defect-fix rows to
        keep lying to query_functions). ~27k rows.
    """
    if scope not in SCOPES:
        raise ValueError(f"unknown scope {scope!r}; expected one of {SCOPES}")
    sdk = " AND ".join(
        f"(unit IS NULL OR {_like_prefix_clause('unit', p)})" for p in SDK_UNIT_PREFIXES)
    art = " AND ".join(_like_prefix_clause("symbol", p) for p in ARTIFACT_PREFIXES)
    if scope == "frontier":
        return (
            f"excluded=0 AND is_stub=0 AND {sdk} AND {art} "
            "AND match_percent_normalized IS NOT NULL "
            "AND match_percent_normalized > 0 AND match_percent_normalized < 100"
        )
    # scope == "all"
    return (
        f"((excluded=0 AND is_stub=0 AND {sdk} AND {art} "
        "  AND match_percent_normalized IS NOT NULL) "
        " OR unicorn_verdict IS NOT NULL)"
    )


def load_frontier(live_db: str, scope: str = "frontier") -> dict:
    """Return {symbol: row_dict} for the swept population (see is_authorable_sql)."""
    conn = sqlite3.connect(live_db)
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        f"SELECT symbol, unit, match_percent_normalized, "
        f"unicorn_verdict, unicorn_class, unicorn_confidence, "
        f"unicorn_tested_at, unicorn_signal_version, "
        f"floor_certificate, merged_symbol_count "
        f"FROM functions WHERE {is_authorable_sql(scope)}"
    ).fetchall()
    conn.close()
    return {r["symbol"]: dict(r) for r in rows}


def frontier_units(frontier: dict) -> set[str]:
    return {r["unit"] for r in frontier.values() if r["unit"]}


# --- per-unit worker ----------------------------------------------------------

def process_unit(name, decomp_path, orig_path, wanted_symbols, timeout=5_000_000):
    """Run the 2-fill schedule + source-hash for the wanted symbols in a unit.

    Returns list of result dicts. Mirrors batch_to_db.process_unit's verdict
    logic exactly (so verdicts are comparable) but only for `wanted_symbols`
    and adds source_hash + obj_size.
    """
    results = []
    if not os.path.exists(decomp_path) or not os.path.exists(orig_path):
        return results
    try:
        decomp_coff = COFFParser(decomp_path)
        orig_coff = COFFParser(orig_path)
    except Exception as e:
        return [{"symbol": s, "verdict": "ERROR", "class": None,
                 "confidence": None, "reason": f"coff_parse: {e}",
                 "source_hash": None} for s in wanted_symbols]

    common = set(_find_common_text_symbols(decomp_coff, orig_coff))
    targets = [s for s in wanted_symbols if s in common]
    # symbols wanted but not present in both objs (renamed/removed): record SKIP
    for s in wanted_symbols:
        if s not in common:
            results.append({"symbol": s, "verdict": "SKIPPED", "class": None,
                            "confidence": None, "reason": "not_in_common_text",
                            "source_hash": None})

    for sym_name in targets:
        src_hash = None
        try:
            src_hash = function_source_hash(decomp_coff, sym_name)
        except Exception:
            src_hash = None

        verdict = None
        div_class = None
        confidence = None
        reason = None

        try:
            exit_code, bundle, _, error_msg = _run_comparison_core(
                sym_name, decomp_coff, orig_coff, timeout=timeout)
        except Exception as e:
            results.append({"symbol": sym_name, "verdict": "ERROR",
                            "class": None, "confidence": None,
                            "reason": f"exec: {e}", "source_hash": src_hash})
            continue

        if exit_code == EXIT_EQUIVALENT:
            verdict = "EQUIVALENT"
        elif exit_code == EXIT_DIVERGENT:
            verdict = "DIVERGENT"
            if bundle is not None:
                div_class = classify_divergence(
                    bundle.result, bundle.decomp_result, bundle.orig_result,
                    bundle.decomp_relocs, bundle.orig_relocs)
                reason = bundle.result.details.get("reason")
                # Wave-4 doc 22: re-tag zero-fill-fixture degenerate-FP object
                # memory diffs (NaN/-0.0/inf vs orig 0) as cosmetic artifacts so
                # the flip-list does not surface them as candidate bugs.
                if reason == "memory_mismatch" and is_degenerate_fixture_diff(
                        bundle.result.details.get("object_diffs")):
                    reason = "fixture_artifact_degenerate"
        elif exit_code == EXIT_SKIPPED:
            verdict = "SKIPPED"
            reason = error_msg
        else:
            verdict = "ERROR"
            reason = error_msg

        # second fill for confidence (EQUIV/DIVERGENT only) — same as batch_to_db
        if exit_code in (EXIT_EQUIVALENT, EXIT_DIVERGENT):
            try:
                code2, _, _, _ = _run_comparison_core(
                    sym_name, decomp_coff, orig_coff, timeout=timeout,
                    fill_pattern=FILL_BYTE)
                if exit_code == code2:
                    confidence = "high" if exit_code == EXIT_EQUIVALENT else "stable_divergent"
                else:
                    confidence = "input_sensitive"
            except Exception:
                confidence = None

        results.append({"symbol": sym_name, "verdict": verdict,
                        "class": div_class, "confidence": confidence,
                        "reason": reason, "source_hash": src_hash})
    return results


def _worker(args):
    name, dp, op, wanted, timeout = args
    try:
        return (name, process_unit(name, dp, op, wanted, timeout), None)
    except Exception as e:
        return (name, [], str(e))


# --- results DB ---------------------------------------------------------------

RESULTS_SCHEMA = """
CREATE TABLE IF NOT EXISTS unicorn_refresh (
    symbol               TEXT PRIMARY KEY,
    unit                 TEXT,
    verdict              TEXT,
    class                TEXT,
    confidence           TEXT,
    reason               TEXT,
    source_hash          TEXT,
    signal_version       INTEGER,
    harness_version      INTEGER,
    probe_schedule_hash  TEXT,
    tested_at            TEXT,
    build                TEXT,
    -- prior (live-DB) state captured at refresh time, for flip detection:
    prev_verdict         TEXT,
    prev_class           TEXT,
    prev_tested_at       TEXT,
    flip                 TEXT,  -- NULL | 'new' | 'stable' | 'EQUIVALENT->DIVERGENT' | ...
    flip_cause           TEXT   -- signal_version | artifact | candidate_bug | recovered | other | NULL
);
CREATE INDEX IF NOT EXISTS idx_refresh_flip ON unicorn_refresh(flip);
CREATE INDEX IF NOT EXISTS idx_refresh_cause ON unicorn_refresh(flip_cause);
CREATE INDEX IF NOT EXISTS idx_refresh_verdict ON unicorn_refresh(verdict);
"""


def classify_flip(prev: str | None, new: str | None) -> str:
    if prev is None or prev == "":
        return "new"
    if prev == new:
        return "stable"
    return f"{prev}->{new}"


# Divergence reasons/classes that mean "the SIGNAL got stricter" (v2/v3
# tightening), not "the source has a new bug". A prior EQUIVALENT flipping to
# DIVERGENT for these is EXPECTED churn from the signal-version bump.
_SIGNAL_ARTIFACT_REASONS = ("cap_exhausted_both", "wild_jump_match")
# Divergence classes that are cosmetic build/emulation artifacts (still a floor;
# the cert just changes from `equivalent` to `artifact:<class>`).
_ARTIFACT_CLASSES = (
    "build_env", "regalloc", "stack_layout",
    "merged_call", "merged_arg", "fpr_precision", "orig_error",
)
# Classes that indicate a real behavioral divergence to adjudicate (candidate bug).
_REAL_BUG_CLASSES = (
    "logic", "error", "call_arg", "object_memory", "return_value",
    "call_count", "unmapped_access_mismatch",
)

# Wave-4 Lane B (doc 22): degenerate IEEE-754 bit patterns that the zero-fill /
# 0xCD fixtures manufacture from div-by-zero, signed-zero negation, etc. An
# object_memory flip whose decomp side is one of these against an orig 0 is
# almost always a FIXTURE ARTIFACT, not a code bug — the realistic-input core
# (fnmsubs/fsel/...) matches. Adjudicated cases: CharFeedback::Poll (qNaN at
# unk8, 100%-normalized objdiff), SkeletonUpdate::UpdateFakeArmPos (-0.0 at
# unk5398, fnmsubs matches both sides).
_DEGENERATE_FP_BITS = (
    0x7FC00000,  # quiet NaN  (x / 0.0)
    0xFFC00000,  # quiet NaN, sign set
    0x80000000,  # -0.0       (-(0*0 - 0))
    0x7F800000,  # +inf       (x / 0.0)
    0xFF800000,  # -inf
)


def is_degenerate_fixture_diff(object_diffs):
    """True iff every object-memory diff looks like a zero-fill-fixture artifact:
    the decomp value is a degenerate FP bit pattern (NaN/-0.0/inf) and the orig
    value is 0. Mechanical, conservative — only fires when ALL diffs match the
    shape and there is at least one diff. See doc 22."""
    if not object_diffs:
        return False
    for entry in object_diffs:
        # entries are (address, decomp_value, orig_value)
        if len(entry) < 3:
            return False
        _, decomp_v, orig_v = entry[0], entry[1], entry[2]
        if orig_v != 0:
            return False
        if (decomp_v & 0xFFFFFFFF) not in _DEGENERATE_FP_BITS:
            return False
    return True


def classify_flip_cause(prev_v, new_v, new_class, reason):
    """Adjudicate WHY a verdict flipped, so the flip-list separates expected
    signal-version churn from candidate real bugs.

    Returns one of:
      signal_version  — EQ->DIV caused by the v2/v3 cap/wild-jump tightening
                        (prior EQUIV was a truncation artifact). Not a new bug.
      artifact        — flip into a cosmetic build/emulation artifact class
                        (still a floor; cert moves equivalent -> artifact:*).
      candidate_bug   — EQ->DIV into a real-bug class (logic/error/call_arg/...);
                        THIS is the deliverable: a behavior divergence hiding
                        under a (stale) floor cert.
      recovered       — DIV->EQ (a prior divergence now tests equivalent).
      other           — anything else (e.g. ERROR/SKIPPED transitions).
    """
    if prev_v == "DIVERGENT" and new_v == "EQUIVALENT":
        return "recovered"
    if prev_v == "EQUIVALENT" and new_v == "DIVERGENT":
        if reason in _SIGNAL_ARTIFACT_REASONS:
            return "signal_version"
        # Wave-4 doc 22: zero-fill-fixture degenerate-FP object_memory flips are
        # cosmetic artifacts, not candidate bugs (the realistic-input core matches).
        if reason == "fixture_artifact_degenerate":
            return "artifact"
        if new_class in _ARTIFACT_CLASSES:
            return "artifact"
        if new_class in _REAL_BUG_CLASSES:
            return "candidate_bug"
        # cap_exhausted_decomp/orig: one-sided truncation — a real loop divergence
        if reason in ("cap_exhausted_decomp", "cap_exhausted_orig"):
            return "candidate_bug"
        return "candidate_bug"  # unclassified DIVERGENT on a prior-EQUIV = adjudicate
    return "other"


def git_short_rev() -> str:
    import subprocess
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                             cwd=PROJECT_ROOT, capture_output=True, text=True, check=True)
        return out.stdout.strip() or "unknown"
    except Exception:
        return "unknown"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", action="store_true",
                    help="Census the frontier + units (no emulation) and exit.")
    ap.add_argument("--run", action="store_true",
                    help="Run the unicorn sweep over the frontier.")
    ap.add_argument("--live-db", default=LIVE_DB, help="Live decomp.db (read-only).")
    ap.add_argument("--out-db", default=os.path.join(
        "/home/free/code/milohax/wt-wave3-b-unicorn-refresh", "unicorn_refresh.db"),
        help="Results DB (worktree-local, written by this script).")
    ap.add_argument("--json", default=os.path.join(
        "/home/free/code/milohax/wt-wave3-b-unicorn-refresh", "unicorn_refresh.json"),
        help="Results JSON sidecar.")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--timeout", type=int, default=5_000_000)
    ap.add_argument("--limit-units", type=int, default=0,
                    help="Only the first N frontier units (smoke test).")
    ap.add_argument("--scope", choices=SCOPES, default="frontier",
                    help="'frontier' (default): authorable 0<match%%<100 only. "
                         "'all': every diffable non-stub function plus every row "
                         "that already carries a unicorn verdict -- use this for a "
                         "whole-DB re-ingest.")
    args = ap.parse_args()

    frontier = load_frontier(args.live_db, args.scope)
    units = frontier_units(frontier)
    print(f"Sweep population (scope={args.scope}): {len(frontier)} fns "
          f"across {len(units)} units", file=sys.stderr)

    if args.plan and not args.run:
        # quick provenance census of the live DB's prior verdicts
        v = {}
        sv = {}
        for r in frontier.values():
            v[r["unicorn_verdict"]] = v.get(r["unicorn_verdict"], 0) + 1
            sv[r["unicorn_signal_version"]] = sv.get(r["unicorn_signal_version"], 0) + 1
        print("prior verdict dist:", dict(sorted(v.items(), key=lambda x: -x[1])),
              file=sys.stderr)
        print("prior signal_version dist:", dict(sv), file=sys.stderr)
        return 0

    if not args.run:
        ap.print_help()
        return 1

    # Build per-unit work: map unit -> wanted symbols
    all_units = {name: (dp, op) for name, dp, op in get_all_units()}
    by_unit: dict[str, list[str]] = {}
    for sym, r in frontier.items():
        by_unit.setdefault(r["unit"], []).append(sym)

    work = []
    for name in sorted(by_unit):
        if name not in all_units:
            continue
        dp, op = all_units[name]
        work.append((name, dp, op, by_unit[name], args.timeout))
    if args.limit_units:
        work = work[:args.limit_units]

    print(f"Sweeping {len(work)} units, {sum(len(w[3]) for w in work)} fns, "
          f"{args.jobs} workers", file=sys.stderr)

    build = git_short_rev()
    now_str = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    t0 = time.monotonic()

    rows_out = []
    done = 0

    def collect(name, results):
        nonlocal done
        done += 1
        for res in results:
            sym = res["symbol"]
            fr = frontier.get(sym, {})
            prev = fr.get("unicorn_verdict")
            flip = classify_flip(prev, res["verdict"])
            cause = classify_flip_cause(prev, res["verdict"], res["class"],
                                        res.get("reason"))
            rows_out.append({
                "symbol": sym, "unit": name,
                "verdict": res["verdict"], "class": res["class"],
                "confidence": res["confidence"], "reason": res.get("reason"),
                "source_hash": res.get("source_hash"),
                "signal_version": SIGNAL_VERSION,
                "harness_version": HARNESS_VERSION,
                "probe_schedule_hash": _SCHEDULE_HASH,
                "tested_at": now_str, "build": build,
                "prev_verdict": prev, "prev_class": fr.get("unicorn_class"),
                "prev_tested_at": fr.get("unicorn_tested_at"),
                "flip": flip, "flip_cause": cause,
            })

    if args.jobs == 1:
        for w in work:
            name, results, err = _worker(w)
            collect(name, results)
            print(f"  [{done}/{len(work)}] {name}: {len(results)} fns", file=sys.stderr)
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            for name, results, err in pool.map(_worker, work):
                if err:
                    print(f"  [unit error] {name}: {err}", file=sys.stderr)
                collect(name, results)
                if done % 20 == 0 or done == len(work):
                    print(f"  [{done}/{len(work)}] last={name} "
                          f"({len(rows_out)} fns, {time.monotonic()-t0:.0f}s)",
                          file=sys.stderr)

    elapsed = time.monotonic() - t0

    # Write results DB (worktree-local) ---------------------------------------
    out = sqlite3.connect(args.out_db)
    out.executescript(RESULTS_SCHEMA)
    out.execute("DELETE FROM unicorn_refresh")
    out.executemany(
        "INSERT OR REPLACE INTO unicorn_refresh "
        "(symbol, unit, verdict, class, confidence, reason, source_hash, "
        " signal_version, harness_version, probe_schedule_hash, tested_at, build, "
        " prev_verdict, prev_class, prev_tested_at, flip, flip_cause) VALUES "
        "(:symbol,:unit,:verdict,:class,:confidence,:reason,:source_hash,"
        " :signal_version,:harness_version,:probe_schedule_hash,:tested_at,:build,"
        " :prev_verdict,:prev_class,:prev_tested_at,:flip,:flip_cause)",
        rows_out,
    )
    out.commit()

    # Summary ------------------------------------------------------------------
    def count(pred):
        return sum(1 for r in rows_out if pred(r))

    vdist = {}
    cause_dist = {}
    for r in rows_out:
        vdist[r["verdict"]] = vdist.get(r["verdict"], 0) + 1
        cause_dist[r["flip_cause"]] = cause_dist.get(r["flip_cause"], 0) + 1
    flips = [r for r in rows_out if r["flip"] not in (None, "stable", "new")]
    new_ev = [r for r in rows_out if r["flip"] == "new"]
    eq2div = [r for r in rows_out if r["flip"] == "EQUIVALENT->DIVERGENT"]
    div2eq = [r for r in rows_out if r["flip"] == "DIVERGENT->EQUIVALENT"]
    # The deliverable: stale-EQUIVALENT that flipped to a REAL-bug class.
    candidate_bugs = [r for r in rows_out if r["flip_cause"] == "candidate_bug"]
    # EQUIV that stayed EQUIV (the floor certs that survive the refresh).
    eq_stable = [r for r in rows_out
                 if r["prev_verdict"] == "EQUIVALENT" and r["verdict"] == "EQUIVALENT"]
    eq_prior = [r for r in rows_out if r["prev_verdict"] == "EQUIVALENT"]

    summary = {
        "tested_at": now_str, "build": build,
        "signal_version": SIGNAL_VERSION, "harness_version": HARNESS_VERSION,
        "scope": args.scope, "probe_schedule_hash": _SCHEDULE_HASH,
        "elapsed_s": round(elapsed, 1),
        "units_swept": len(work), "fns_tested": len(rows_out),
        "verdict_dist": vdist,
        "flip_cause_dist": cause_dist,
        "flips_total": len(flips),
        "flip_EQUIVALENT_to_DIVERGENT": len(eq2div),
        "flip_DIVERGENT_to_EQUIVALENT": len(div2eq),
        # stale-EQUIVALENT cohort outcome (lane item 3):
        "prior_EQUIVALENT_tested": len(eq_prior),
        "prior_EQUIVALENT_stayed_EQUIVALENT": len(eq_stable),
        "prior_EQUIVALENT_flipped": len(eq_prior) - len(eq_stable),
        "flip_cause_signal_version": cause_dist.get("signal_version", 0),
        "flip_cause_artifact": cause_dist.get("artifact", 0),
        "flip_cause_candidate_bug": len(candidate_bugs),
        "new_evidence": len(new_ev),
        "new_evidence_EQUIVALENT": count(lambda r: r["flip"] == "new" and r["verdict"] == "EQUIVALENT"),
        "new_evidence_DIVERGENT": count(lambda r: r["flip"] == "new" and r["verdict"] == "DIVERGENT"),
        "new_evidence_DIVERGENT_artifact": count(
            lambda r: r["flip"] == "new" and r["verdict"] == "DIVERGENT"
            and r["class"] in _ARTIFACT_CLASSES),
        "new_evidence_DIVERGENT_realbug": count(
            lambda r: r["flip"] == "new" and r["verdict"] == "DIVERGENT"
            and r["class"] in _REAL_BUG_CLASSES),
    }
    flip_list = sorted(
        [{"symbol": r["symbol"], "unit": r["unit"], "flip": r["flip"],
          "flip_cause": r["flip_cause"],
          "new_class": r["class"], "new_confidence": r["confidence"],
          "reason": r["reason"], "prev_tested_at": r["prev_tested_at"],
          "source_hash": r["source_hash"],
          "norm_pct": frontier.get(r["symbol"], {}).get("match_percent_normalized"),
          "floor_certificate": frontier.get(r["symbol"], {}).get("floor_certificate")}
         for r in flips],
        # candidate_bug first (most important), then by cause/unit/symbol
        key=lambda x: (0 if x["flip_cause"] == "candidate_bug" else 1,
                       x["flip_cause"] or "", x["unit"], x["symbol"]),
    )

    with open(args.json, "w") as f:
        json.dump({"summary": summary, "flips": flip_list, "rows": rows_out},
                  f, indent=2)

    print(file=sys.stderr)
    print("=== REFRESH SUMMARY ===", file=sys.stderr)
    for k, val in summary.items():
        print(f"  {k}: {val}", file=sys.stderr)
    print(f"  results db:  {args.out_db}", file=sys.stderr)
    print(f"  results json:{args.json}", file=sys.stderr)
    out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
