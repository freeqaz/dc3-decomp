#!/usr/bin/env python3
"""Reconcile decomp.db against build/373307D9/report.json — drift detector.

Read-only by default. Runs four drift checks, prints loud per-check counts, and
exits non-zero if ANY drift is found. `--fix` applies only the corrections that
sync_match_percent.py owns (the same logic, idempotently), so this can run as a
ninja-postbuild step or nightly guard after `sync_match_percent.py`.

Checks (roadmap 0.7, audit docs 03/04):
  (a) db.current_percent vs report.fuzzy_match_percent differ >= 0.5  (FALSE-COMPLETE;
      doc 03 F6 once found 639). Read-only signal — sync owns the percent UPDATE.
  (b) verdict='COMPLETE' AND current_percent < 100               (stale COMPLETE; doc 03 F4: 20)
  (c) is_stub=1 AND current_percent >= 100                       (stale stub; doc 04 F3: 1,728)
  (d) symbols in db but absent from report (authorable only)     +  report-only symbols
                                                                  (jeff boundary churn; doc 03 F1)
      NOTE — the ~170 "db-only COMPLETE" symbols (Wave 6 Lane D investigation):
        135 are in report.json but with match_percent_normalized=0 and no
        fuzzy_match_percent (target-only ICF/template instantiations — sync skips
        them), and 35 are fully absent (jeff boundary churn).  Both sub-populations
        have verdict=COMPLETE and current_percent=100 from an earlier sync and are
        real done rows.  The authorable_done view in certify_floor.py counts them as
        'matched' via its COMPLETE+current>=100+normalized NULL rule, so they do not
        inflate the open count.  The d_db_only count below WILL include them; that
        is correct — they are a known class that an orchestrator clean-up pass may
        optionally delete (but they do no harm).
  (e) stale floor certificates: floor_certificate set but match_percent_normalized has
      moved away from floor_cert_pct (or is now >=100). The cosmetic-floor proof no
      longer applies, so the cert must be re-evaluated by certify_floor.py. Only checked
      when the floor_cert_* columns exist (added by certify_floor.py, Lane B / doc 08).

`--fix` corrections (the subset sync owns; keyed off the DB's own current_percent so
db-only rows the report can't see are still handled):
  - (b) COMPLETE & current_percent<100  -> verdict NULL
        EXCEPT rows whose match_percent_normalized >= 100 (legitimately complete
        under the normalized scorer — these are the ~206 fuzzy<100/norm==100 fns
        that sync --promote keeps COMPLETE; demoting them would re-introduce drift).
  - (c) is_stub=1 & current_percent>=100 -> is_stub=0
  - (e) clears floor_cert_* columns for stale certs (certify_floor.py re-adds them).
Check (a) and the percent values themselves are NOT fixed here — re-run
`sync_match_percent.py` to repair percents from report.json.

Usage:
    python3 scripts/reconcile_db.py                 # read-only; exit 1 on drift
    python3 scripts/reconcile_db.py --fix           # apply (b)+(c) corrections
    python3 scripts/reconcile_db.py --db /path/copy.db --report /path/report.json
    python3 scripts/reconcile_db.py -v              # list offending symbols
"""
from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REPORT = REPO_ROOT / "build" / "373307D9" / "report.json"
DEFAULT_DB = REPO_ROOT / "decomp.db"

# Import the canonical SDK exclusion list from the sync script (single source).
try:
    from sync_match_percent import SDK_UNIT_PREFIXES
except ImportError:  # when run from elsewhere, fall back to a local copy
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from sync_match_percent import SDK_UNIT_PREFIXES

DRIFT_THRESHOLD = 0.5
SAMPLE_LIMIT = 40


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Reconcile decomp.db vs report.json (drift detector)")
    p.add_argument("--report", type=Path, default=DEFAULT_REPORT,
                   help=f"Path to report.json (default: {DEFAULT_REPORT})")
    p.add_argument("--db", type=Path, default=DEFAULT_DB,
                   help=f"Path to decomp.db (default: {DEFAULT_DB})")
    p.add_argument("--fix", action="store_true",
                   help="Apply the (b) stale-COMPLETE-demote and (c) stale-is_stub-clear corrections")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="List offending symbols for each check")
    return p.parse_args()


def load_report(report_path: Path) -> dict[str, dict]:
    """Return {symbol: {fuzzy, normalized}} for non-SDK functions with match data."""
    with open(report_path) as f:
        data = json.load(f)
    out: dict[str, dict] = {}
    for unit in data.get("units", []):
        uname = unit["name"]
        if any(uname.startswith(p) for p in SDK_UNIT_PREFIXES):
            continue
        for fn in unit.get("functions", []):
            fz = fn.get("fuzzy_match_percent")
            if fz is None:
                continue
            nm = fn.get("match_percent_normalized")
            out[fn["name"]] = {
                "fuzzy": round(fz, 2),
                "normalized": round(nm, 2) if nm is not None else None,
            }
    return out


def has_column(conn: sqlite3.Connection, table: str, col: str) -> bool:
    return col in {row[1] for row in conn.execute(f"PRAGMA table_info({table})")}


def _sample(verbose: bool, label: str, rows: list[str]) -> None:
    if verbose and rows:
        print(f"    {label}:")
        for s in rows[:SAMPLE_LIMIT]:
            print(f"      {s}")
        if len(rows) > SAMPLE_LIMIT:
            print(f"      ... and {len(rows) - SAMPLE_LIMIT} more")


def reconcile(args: argparse.Namespace) -> int:
    if not args.report.exists():
        print(f"Error: report not found: {args.report}", file=sys.stderr)
        return 2
    if not args.db.exists():
        print(f"Error: db not found: {args.db}", file=sys.stderr)
        return 2

    report = load_report(args.report)
    report_symbols = set(report)

    conn = sqlite3.connect(str(args.db))
    conn.row_factory = sqlite3.Row
    has_norm = has_column(conn, "functions", "match_percent_normalized")
    has_stub = has_column(conn, "functions", "is_stub")
    has_excluded = has_column(conn, "functions", "excluded")
    has_cert = has_column(conn, "functions", "floor_certificate")

    sel = ["id", "symbol", "unit", "current_percent", "verdict"]
    if has_norm:
        sel.append("match_percent_normalized")
    if has_stub:
        sel.append("is_stub")
    if has_excluded:
        sel.append("excluded")
    if has_cert:
        sel += ["floor_certificate", "floor_cert_pct"]
    rows = conn.execute(f"SELECT {', '.join(sel)} FROM functions").fetchall()

    def is_authorable(r: sqlite3.Row) -> bool:
        unit = r["unit"] or ""
        if any(unit.startswith(p) for p in SDK_UNIT_PREFIXES):
            return False
        if has_excluded and r["excluded"]:
            return False
        if r["symbol"].startswith("merged_"):
            return False
        return True

    # (a) db.current_percent vs report.fuzzy differ >= threshold (shared non-SDK)
    a_drift: list[str] = []
    for r in rows:
        sym = r["symbol"]
        if sym not in report:
            continue
        cp = r["current_percent"]
        if cp is None:
            a_drift.append(sym)
            continue
        if abs(cp - report[sym]["fuzzy"]) >= DRIFT_THRESHOLD:
            a_drift.append(sym)

    # (b) verdict=COMPLETE AND current_percent<100 (stale COMPLETE)
    b_stale: list[sqlite3.Row] = [
        r for r in rows
        if r["verdict"] == "COMPLETE"
        and (r["current_percent"] is None or r["current_percent"] < 100)
    ]
    # Split: rows that are legitimately complete under normalized (do NOT demote)
    b_demote: list[sqlite3.Row] = []
    b_keep_norm: list[sqlite3.Row] = []
    for r in b_stale:
        norm = r["match_percent_normalized"] if has_norm else None
        if norm is not None and norm >= 100:
            b_keep_norm.append(r)
        else:
            b_demote.append(r)

    # (c) is_stub=1 AND current_percent>=100 (stale stub)
    c_stub: list[sqlite3.Row] = []
    if has_stub:
        c_stub = [
            r for r in rows
            if r["is_stub"] and r["current_percent"] is not None and r["current_percent"] >= 100
        ]

    # (d) db authorable symbols absent from report; and report-only symbols
    d_db_only = [r["symbol"] for r in rows if is_authorable(r) and r["symbol"] not in report_symbols]
    db_symbols = {r["symbol"] for r in rows}
    d_report_only = [s for s in report_symbols if s not in db_symbols]

    # (e) STALE FLOOR CERTIFICATES (added by certify_floor.py, Lane B).
    # A cert stores floor_cert_pct = the match_percent_normalized at cert time.
    # If the function's normalized percent has since moved (the source changed and
    # rebuilt), the cosmetic-floor proof no longer applies and the cert must be
    # invalidated so certify_floor.py re-evaluates it. Two invalidation triggers:
    #   - normalized now >= 100  -> function is matched; cert no longer needed
    #   - |normalized - floor_cert_pct| >= threshold -> percent moved; re-certify
    # Read-only signal; --fix clears the floor_cert_* columns (certify owns re-add).
    e_stale_cert: list[sqlite3.Row] = []
    if has_cert and has_norm:
        for r in rows:
            if not r["floor_certificate"]:
                continue
            norm = r["match_percent_normalized"]
            cert_pct = r["floor_cert_pct"]
            if norm is None:
                e_stale_cert.append(r)  # lost its percent -> can't trust the cert
            elif norm >= 100:
                e_stale_cert.append(r)  # now fully matched -> cert obsolete
            elif cert_pct is None or abs(norm - cert_pct) >= DRIFT_THRESHOLD:
                e_stale_cert.append(r)  # percent moved since cert -> re-evaluate

    # --- Report ---
    print(f"Reconcile: db={args.db}")
    print(f"           report={args.report}  ({len(report)} non-SDK functions)")
    print(f"           normalized column: {'present' if has_norm else 'ABSENT (run sync to add)'}")
    print()
    print("=== Drift checks ===")
    print(f"  (a) current_percent vs report.fuzzy differ >= {DRIFT_THRESHOLD}: {len(a_drift)}")
    _sample(args.verbose, "drifted symbols", a_drift)
    print(f"  (b) verdict=COMPLETE AND current_percent<100:        {len(b_stale)}")
    print(f"        - demotable (norm<100 / unknown):              {len(b_demote)}")
    print(f"        - kept (norm>=100, legitimately complete):     {len(b_keep_norm)}")
    _sample(args.verbose, "demotable stale-COMPLETE", [r["symbol"] for r in b_demote])
    print(f"  (c) is_stub=1 AND current_percent>=100:               {len(c_stub)}")
    _sample(args.verbose, "stale is_stub", [r["symbol"] for r in c_stub])
    print(f"  (d) db authorable symbols absent from report:         {len(d_db_only)}")
    print(f"      report-only symbols (in report, not db):          {len(d_report_only)}")
    _sample(args.verbose, "report-only", d_report_only)
    if has_cert:
        print(f"  (e) stale floor certificates (percent moved/matched): {len(e_stale_cert)}")
        _sample(args.verbose, "stale floor certs", [r["symbol"] for r in e_stale_cert])

    # Real drift excludes the legitimately-complete (norm>=100) COMPLETE rows:
    # those are the ~206 fuzzy<100/norm==100 functions and are CORRECT, not drift.
    total_drift = (len(a_drift) + len(b_demote) + len(c_stub)
                   + len(d_report_only) + len(e_stale_cert))

    if args.fix:
        fixed_demote = 0
        fixed_stub = 0
        if b_demote:
            conn.executemany(
                "UPDATE functions SET verdict=NULL, updated_at=CURRENT_TIMESTAMP WHERE id=?",
                [(r["id"],) for r in b_demote],
            )
            fixed_demote = len(b_demote)
        if c_stub:
            conn.executemany(
                "UPDATE functions SET is_stub=0, updated_at=CURRENT_TIMESTAMP WHERE id=?",
                [(r["id"],) for r in c_stub],
            )
            fixed_stub = len(c_stub)
        fixed_cert = 0
        if has_cert and e_stale_cert:
            conn.executemany(
                "UPDATE functions SET floor_certificate=NULL, floor_cert_pct=NULL, "
                "floor_cert_build=NULL, floor_cert_at=NULL, floor_cert_evidence=NULL, "
                "updated_at=CURRENT_TIMESTAMP WHERE id=?",
                [(r["id"],) for r in e_stale_cert],
            )
            fixed_cert = len(e_stale_cert)
        conn.commit()
        print()
        print("=== Applied --fix corrections ===")
        print(f"  (b) demoted COMPLETE->NULL (norm<100):  {fixed_demote}")
        print(f"  (c) cleared stale is_stub:              {fixed_stub}")
        if has_cert:
            print(f"  (e) cleared stale floor certs:          {fixed_cert} (re-run certify_floor.py --apply)")
        print("  (a) percent drift is NOT auto-fixed here — re-run sync_match_percent.py.")
        print("  (d) report-only symbols are NOT auto-fixed — investigate jeff boundary churn.")
        # After fix, the remaining drift that --fix owns should be 0; (a)/(d) may persist.
        residual = len(a_drift) + len(d_report_only)
        if residual == 0:
            print("\nOK: all sync-owned drift corrected (a/d require sync/jeff follow-up if nonzero).")
            return 0
        print(f"\nWARNING: {residual} drift items remain that --fix does not own "
              f"(a={len(a_drift)} percent-drift, d={len(d_report_only)} report-only).")
        return 1

    conn.close()
    print()
    if total_drift == 0:
        print("OK: no drift detected.")
        return 0
    print(f"DRIFT DETECTED: {total_drift} total items "
          f"(a={len(a_drift)}, b={len(b_demote)}, c={len(c_stub)}, "
          f"report-only={len(d_report_only)}, stale-certs={len(e_stale_cert)}).")
    print("Run with --fix to apply sync-owned corrections (b/c/e), "
          "or re-run sync_match_percent.py for percent/promotion drift (a).")
    return 1


if __name__ == "__main__":
    sys.exit(reconcile(parse_args()))
