#!/usr/bin/env python3
"""
Re-check AT_LIMIT functions with NULL/0% match to find newly-matching functions.
Uses the project's objdiff-cli with --verdict flag for correct JSON output.

Usage:
    python3 scripts/recheck_stale.py             # Re-check all stale AT_LIMIT entries
    python3 scripts/recheck_stale.py --dry-run    # Preview without updating DB
    python3 scripts/recheck_stale.py -j 8         # Use 8 parallel workers
"""

import argparse
import sqlite3
import subprocess
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB_PATH = os.path.join(PROJECT_ROOT, "decomp.db")
OBJDIFF_CLI = os.path.join(PROJECT_ROOT, "bin", "objdiff-cli")


def run_objdiff(symbol):
    """Run objdiff-cli and return (symbol, match_pct) or (symbol, None) on error."""
    try:
        result = subprocess.run(
            [OBJDIFF_CLI, "diff", "-p", PROJECT_ROOT, symbol, "-f", "json", "--verdict"],
            capture_output=True, text=True, timeout=60
        )
        if result.returncode != 0:
            return (symbol, None)
        data = json.loads(result.stdout)
        summary = data.get("instruction_summary", {})
        return (symbol, summary.get("equal_percent", 0.0))
    except Exception:
        pass
    return (symbol, None)


def main():
    parser = argparse.ArgumentParser(
        description="Re-check AT_LIMIT functions with stale (NULL/0%%) match data"
    )
    parser.add_argument("-j", "--jobs", type=int, default=6, help="Parallel workers (default: 6)")
    parser.add_argument("--dry-run", action="store_true", help="Preview without updating DB")
    parser.add_argument("--db", type=str, default=DB_PATH, help="Database path")
    args = parser.parse_args()

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row

    rows = conn.execute("""
        SELECT symbol, demangled, unit
        FROM functions
        WHERE verdict = 'AT_LIMIT'
          AND (current_percent IS NULL OR current_percent = 0)
          AND excluded = 0
        ORDER BY unit
    """).fetchall()

    print(f"Found {len(rows)} stale AT_LIMIT functions to re-check")
    print(f"Using {args.jobs} parallel workers" + (" (dry-run)" if args.dry_run else ""))

    if not rows:
        print("Nothing to do.")
        conn.close()
        return

    symbols = [row["symbol"] for row in rows]
    complete = 0
    improved = 0
    unchanged = 0
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

            if match_pct >= 100.0:
                if not args.dry_run:
                    conn.execute(
                        "UPDATE functions SET current_percent = 100.0, best_percent = 100.0, verdict = 'COMPLETE' WHERE symbol = ?",
                        (sym,)
                    )
                complete += 1
            elif match_pct > 0:
                if not args.dry_run:
                    conn.execute(
                        "UPDATE functions SET current_percent = ?, best_percent = MAX(COALESCE(best_percent, 0), ?) WHERE symbol = ?",
                        (match_pct, match_pct, sym)
                    )
                improved += 1
            else:
                unchanged += 1

            if processed % 200 == 0:
                if not args.dry_run:
                    conn.commit()
                print(f"  [{processed}/{len(rows)}] COMPLETE: {complete}, improved: {improved}, "
                      f"unchanged: {unchanged}, errors: {errors}")
                sys.stdout.flush()

    if not args.dry_run:
        conn.commit()

    print(f"\nDone. Processed {processed}/{len(rows)} functions:")
    print(f"  -> COMPLETE (100%%): {complete}")
    print(f"  -> Improved (>0%%):  {improved}")
    print(f"  -> Unchanged (0%%):  {unchanged}")
    print(f"  -> Errors:           {errors}")

    conn.close()


if __name__ == "__main__":
    main()
