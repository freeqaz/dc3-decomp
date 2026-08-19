#!/usr/bin/env python3
"""Query unicorn behavioral test results from the orchestrator database.

Filters functions by verdict (EQUIVALENT/DIVERGENT), divergence class
(logic/build_env/regalloc), unit pattern, and function status.
"""

import argparse
import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, PROJECT_ROOT)

from scripts.orchestrator.database import get_connection

DEFAULT_DB = os.path.join(PROJECT_ROOT, "decomp.db")


def query_functions(args):
    conn = get_connection(args.db)

    select = """
        SELECT symbol, demangled, unit, current_percent, best_percent, verdict,
               unicorn_verdict, unicorn_class, unicorn_confidence, unicorn_tested_at,
               unicorn_harness_version
        FROM functions
        WHERE unicorn_verdict IS NOT NULL
    """
    params = []

    if args.min_harness is not None:
        select += (" AND unicorn_harness_version IS NOT NULL"
                   " AND unicorn_harness_version >= ?")
        params.append(args.min_harness)

    if args.verdict:
        select += " AND unicorn_verdict = ?"
        params.append(args.verdict.upper())

    if args.cls:
        select += " AND unicorn_class = ?"
        params.append(args.cls.lower())

    if args.unit:
        # Support glob patterns
        pattern = args.unit.replace("*", "%")
        select += " AND unit LIKE ?"
        params.append(f"%{pattern}%")

    if args.status:
        if args.status == "workable":
            select += " AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"
        elif args.status == "complete":
            select += " AND verdict = 'COMPLETE'"
        elif args.status == "at_limit":
            select += " AND verdict = 'AT_LIMIT'"
        # 'all' means no filter

    select += " ORDER BY unicorn_class, unit, current_percent DESC"

    if args.limit:
        select += f" LIMIT {args.limit}"

    rows = conn.execute(select, params).fetchall()
    return rows


def print_summary(conn, args):
    """Print counts by verdict and class."""
    where = "WHERE unicorn_verdict IS NOT NULL"
    params = []

    if args.min_harness is not None:
        where += (" AND unicorn_harness_version IS NOT NULL"
                  " AND unicorn_harness_version >= ?")
        params.append(args.min_harness)

    if args.unit:
        pattern = args.unit.replace("*", "%")
        where += " AND unit LIKE ?"
        params.append(f"%{pattern}%")

    if args.status:
        if args.status == "workable":
            where += " AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"
        elif args.status == "complete":
            where += " AND verdict = 'COMPLETE'"
        elif args.status == "at_limit":
            where += " AND verdict = 'AT_LIMIT'"

    # Verdict counts
    rows = conn.execute(
        f"SELECT unicorn_verdict, COUNT(*) as cnt FROM functions {where} GROUP BY unicorn_verdict ORDER BY cnt DESC",
        params,
    ).fetchall()

    if not rows:
        print("No unicorn results found.")
        return

    total = sum(r["cnt"] for r in rows)
    print(f"Unicorn verdict summary ({total} tested):")
    for r in rows:
        print(f"  {r['unicorn_verdict']:12s}  {r['cnt']:>5d}")

    # Class breakdown for DIVERGENT
    class_rows = conn.execute(
        f"""SELECT unicorn_class, COUNT(*) as cnt FROM functions
            {where} AND unicorn_verdict = 'DIVERGENT' AND unicorn_class IS NOT NULL
            GROUP BY unicorn_class ORDER BY cnt DESC""",
        params,
    ).fetchall()

    if class_rows:
        div_total = sum(r["cnt"] for r in class_rows)
        print(f"\nDivergence class breakdown ({div_total} divergent):")
        for r in class_rows:
            print(f"  {r['unicorn_class']:16s}  {r['cnt']:>5d}")

    # PROVENANCE. Never print a unicorn count without saying which harness
    # produced it: h1 (pre-2026-08-18) overstated real bugs by roughly 8x.
    hv_rows = conn.execute(
        f"""SELECT COALESCE(unicorn_harness_version, 1) AS hv, COUNT(*) AS cnt
            FROM functions {where} GROUP BY hv ORDER BY hv DESC""",
        params,
    ).fetchall()
    if hv_rows:
        print("\nHarness provenance (see scripts/unicorn_runner/signal_version.py):")
        for r in hv_rows:
            note = "" if r["hv"] >= 4 else "   <-- STALE, do not trust"
            print(f"  h{r['hv']:<15d}  {r['cnt']:>5d}{note}")


def print_table(rows):
    """Print function rows as a formatted table."""
    if not rows:
        print("No functions matched the query.")
        return

    print(f"\n{'Symbol':<60s} {'Match%':>6s} {'Verdict':>10s} {'Class':>16s} "
          f"{'Status':>10s} {'Hrn':>4s}")
    print("-" * 112)

    for r in rows:
        name = r["demangled"] or r["symbol"]
        if len(name) > 58:
            name = name[:55] + "..."
        pct = f"{r['current_percent']:.1f}" if r["current_percent"] is not None else "?"
        u_verdict = r["unicorn_verdict"] or ""
        u_class = r["unicorn_class"] or ""
        status = r["verdict"] or ""
        hv = r["unicorn_harness_version"]
        hv_s = f"h{hv}" if hv is not None else "h1?"
        print(f"{name:<60s} {pct:>6s} {u_verdict:>10s} {u_class:>16s} "
              f"{status:>10s} {hv_s:>4s}")

    print(f"\nTotal: {len(rows)} functions")


def main():
    parser = argparse.ArgumentParser(
        description="Query unicorn behavioral test results from the decomp database"
    )
    parser.add_argument(
        "--verdict", type=str, choices=["DIVERGENT", "EQUIVALENT", "SKIPPED", "ERROR"],
        help="Filter by unicorn verdict"
    )
    parser.add_argument(
        "--class", dest="cls", type=str,
        help="Filter by divergence class (logic, build_env, regalloc, data_layout, "
             "stack_layout, call_count, call_arg, cap_exhausted[_orig|_decomp], "
             "wild_jump_match, merged_call, merged_arg, fpr_precision, orig_error, "
             "error, return_value, object_memory, unmapped_access_mismatch)"
    )
    parser.add_argument(
        "--unit", type=str,
        help="Filter by unit path (glob pattern, e.g. 'system/char/*')"
    )
    parser.add_argument(
        "--status", type=str, choices=["workable", "complete", "at_limit", "all"],
        default=None,
        help="Filter by function status"
    )
    parser.add_argument(
        "--min-harness", type=int, default=None, metavar="N",
        help="Only rows measured by unicorn harness version >= N. Pass 4 to "
             "exclude every verdict from before the 2026-08-18/19 defect fixes."
    )
    parser.add_argument(
        "--limit", type=int, default=None,
        help="Max results to return"
    )
    parser.add_argument(
        "--summary-only", action="store_true",
        help="Only show counts, no function list"
    )
    parser.add_argument(
        "--db", type=str, default=DEFAULT_DB,
        help="Path to orchestrator database"
    )

    args = parser.parse_args()

    # Default: show DIVERGENT if no filters specified
    if not args.verdict and not args.cls and not args.summary_only:
        args.verdict = "DIVERGENT"

    conn = get_connection(args.db)
    print_summary(conn, args)

    if not args.summary_only:
        rows = query_functions(args)
        print_table(rows)


if __name__ == "__main__":
    main()
