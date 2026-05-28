#!/usr/bin/env python3
"""
Ingest report.json into the orchestrator database.

Usage:
    python3 scripts/ingest_report.py build/373307D9/report.json
    python3 scripts/ingest_report.py build/373307D9/report.json --db decomp.db
"""

import argparse
import sqlite3
import sys
from pathlib import Path

# Add scripts to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from orchestrator.database import init_database, ingest_report, get_stats


def main():
    parser = argparse.ArgumentParser(
        description="Ingest report.json into orchestrator database"
    )
    parser.add_argument(
        "report_path",
        type=Path,
        help="Path to report.json (e.g., build/373307D9/report.json)",
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=Path("decomp.db"),
        help="Database path (default: decomp.db)",
    )
    parser.add_argument(
        "--no-update",
        action="store_true",
        help="Skip updating existing functions (only insert new)",
    )
    parser.add_argument(
        "--build-safe",
        action="store_true",
        help="Quiet, one-line output and never fail: if the DB is locked by "
        "the live fleet, warn and exit 0. For use as a ninja build step.",
    )

    args = parser.parse_args()

    if not args.report_path.exists():
        # Build-safe: a missing report just means nothing to ingest yet.
        if args.build_safe:
            print(f"[db-sync] report not found ({args.report_path}); skipping")
            return
        print(f"Error: Report file not found: {args.report_path}")
        sys.exit(1)

    if args.build_safe:
        # Best-effort metadata sync as a ninja step. WAL connections have no
        # busy_timeout, so a concurrent fleet writer can raise SQLITE_BUSY —
        # treat that (or any sqlite error) as "skip this run", never a build
        # failure. Verdicts are owned by sync_objdiff.py, not this step.
        try:
            init_database(args.db)
            result = ingest_report(
                args.report_path,
                db_path=args.db,
                update_existing=not args.no_update,
            )
        except sqlite3.OperationalError as e:
            print(f"[db-sync] DB busy ({e}); skipping (fleet will catch up)")
            return
        print(
            f"[db-sync] report.json -> {args.db}: "
            f"+{result['inserted']} new, {result['updated']} metadata-updated"
        )
        return

    print(f"Initializing database: {args.db}")
    init_database(args.db)

    print(f"Ingesting report: {args.report_path}")
    result = ingest_report(
        args.report_path,
        db_path=args.db,
        update_existing=not args.no_update,
    )

    print(f"\nIngestion complete:")
    print(f"  Inserted: {result['inserted']}")
    print(f"  Updated:  {result['updated']}")
    print(f"  Skipped:  {result['skipped']}")

    stats = get_stats(args.db)
    print(f"\nDatabase statistics:")
    print(f"  Total functions:   {stats['total_functions']}")
    print(f"  With match %:      {stats['with_percent']}")
    print(f"  Complete (100%):   {stats['complete']}")
    print(f"  At limit:          {stats['at_limit']}")
    if stats['avg_percent']:
        print(f"  Average match %:   {stats['avg_percent']:.1f}%")


if __name__ == "__main__":
    main()
