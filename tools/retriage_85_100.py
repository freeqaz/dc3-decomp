#!/usr/bin/env python3
"""
Bulk-triage unverdicted functions in the decomp database.

Runs objdiff on functions that have match data but no verdict,
updates current_percent, and auto-assigns verdicts:
  - 100%: COMPLETE
  - <100% with match data: AT_LIMIT (already implemented, just not perfect)

Usage:
    python3 tools/retriage_85_100.py                    # All unverdicted with match >= 85%
    python3 tools/retriage_85_100.py --min 0            # All unverdicted functions
    python3 tools/retriage_85_100.py --min 95 --max 100 # Just 95-100% range
    python3 tools/retriage_85_100.py --dry-run           # Preview without updating DB
    python3 tools/retriage_85_100.py --fix-100-at-limit  # Fix 100% AT_LIMIT -> COMPLETE
    python3 tools/retriage_85_100.py -j 8               # 8 parallel workers
"""

import argparse
import sqlite3
import subprocess
import json
import os
from concurrent.futures import ProcessPoolExecutor, as_completed

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB_PATH = os.path.join(PROJECT_ROOT, "decomp.db")
OBJDIFF_CLI = os.path.join(PROJECT_ROOT, "bin", "objdiff-cli")


def get_unverdicted(conn, min_pct, max_pct, limit):
    """Get functions with match data but no verdict."""
    return conn.execute("""
        SELECT symbol, demangled, unit, current_percent
        FROM functions
        WHERE current_percent >= ? AND current_percent <= ?
        AND verdict IS NULL
        ORDER BY current_percent DESC
        LIMIT ?
    """, (min_pct, max_pct, limit)).fetchall()


def get_100_at_limit(conn):
    """Get functions at 100% incorrectly marked AT_LIMIT."""
    return conn.execute("""
        SELECT symbol, demangled, unit, current_percent
        FROM functions
        WHERE current_percent >= 100.0 AND verdict = 'AT_LIMIT'
    """).fetchall()


def run_objdiff(symbol):
    """Run objdiff-cli and return (symbol, match_pct) or (symbol, None) on error."""
    try:
        result = subprocess.run(
            [OBJDIFF_CLI, "diff", "-p", PROJECT_ROOT, symbol, "-f", "json"],
            capture_output=True, text=True, timeout=60
        )
        if result.returncode != 0:
            return (symbol, None)
        data = json.loads(result.stdout)
        # Top-level fuzzy_match_percent (current objdiff format)
        if "fuzzy_match_percent" in data:
            return (symbol, data["fuzzy_match_percent"])
        # Fallback: sections format (older objdiff)
        sections = data.get("sections", [])
        if sections:
            return (symbol, sections[0].get("match_percent", 0))
    except Exception:
        pass
    return (symbol, None)


def main():
    parser = argparse.ArgumentParser(description="Bulk-triage unverdicted decomp functions")
    parser.add_argument("--min", type=float, default=85.0, help="Minimum match %% (default: 85)")
    parser.add_argument("--max", type=float, default=100.0, help="Maximum match %% (default: 100)")
    parser.add_argument("--limit", type=int, default=50000, help="Max functions to process (default: 50000)")
    parser.add_argument("-j", "--jobs", type=int, default=4, help="Parallel workers (default: 4)")
    parser.add_argument("--dry-run", action="store_true", help="Preview without updating DB")
    parser.add_argument("--fix-100-at-limit", action="store_true",
                        help="Fix 100%% functions incorrectly marked AT_LIMIT -> COMPLETE")
    parser.add_argument("--db", type=str, default=DB_PATH, help="Database path")
    args = parser.parse_args()

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row

    # Fix 100% AT_LIMIT -> COMPLETE
    if args.fix_100_at_limit:
        rows = get_100_at_limit(conn)
        print(f"Found {len(rows)} functions at 100% marked AT_LIMIT")
        symbols = [row["symbol"] for row in rows]
        fixed = 0
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(run_objdiff, sym): sym for sym in symbols}
            for future in as_completed(futures):
                sym, match_pct = future.result()
                if match_pct is not None and match_pct >= 100.0:
                    if not args.dry_run:
                        conn.execute("UPDATE functions SET verdict = 'COMPLETE' WHERE symbol = ?", (sym,))
                    fixed += 1
                elif match_pct is not None:
                    if not args.dry_run:
                        conn.execute("UPDATE functions SET current_percent = ? WHERE symbol = ?",
                                     (match_pct, sym))
        if not args.dry_run:
            conn.commit()
        print(f"Fixed {fixed} -> COMPLETE")
        return

    # Main triage: unverdicted functions
    rows = get_unverdicted(conn, args.min, args.max, args.limit)
    print(f"Checking {len(rows)} unverdicted functions ({args.min:.0f}-{args.max:.0f}%)...")
    print(f"Using {args.jobs} parallel workers")

    symbols = [row["symbol"] for row in rows]
    complete = 0
    at_limit = 0
    errors = 0
    processed = 0

    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_objdiff, sym): sym for sym in symbols}
        for future in as_completed(futures):
            sym, match_pct = future.result()
            processed += 1

            if match_pct is None:
                errors += 1
                continue

            verdict = "COMPLETE" if match_pct >= 100.0 else "AT_LIMIT"

            if not args.dry_run:
                conn.execute(
                    "UPDATE functions SET current_percent = ?, verdict = ? WHERE symbol = ?",
                    (match_pct, verdict, sym)
                )

            if verdict == "COMPLETE":
                complete += 1
            else:
                at_limit += 1

            if processed % 100 == 0:
                if not args.dry_run:
                    conn.commit()
                print(f"  Processed {processed}/{len(rows)} "
                      f"(COMPLETE: {complete}, AT_LIMIT: {at_limit}, errors: {errors})")

    if not args.dry_run:
        conn.commit()

    print(f"\nDone. Processed {len(rows)} functions:")
    print(f"  COMPLETE:  {complete}")
    print(f"  AT_LIMIT:  {at_limit}")
    print(f"  Errors:    {errors}")

    conn.close()


if __name__ == "__main__":
    main()
