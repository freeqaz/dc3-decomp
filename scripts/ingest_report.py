#!/usr/bin/env python3
"""
Ingest report.json into the orchestrator database.

Usage:
    python3 scripts/ingest_report.py build/373307D9/report.json
    python3 scripts/ingest_report.py build/373307D9/report.json --db decomp.db
"""

import argparse
import os
import sqlite3
import sys
from pathlib import Path

# Add scripts to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from orchestrator import database
from orchestrator.database import init_database, ingest_report, get_stats


def is_worktree_shadow(db_path: Path) -> bool:
    """True if writing `db_path` would create/extend a worktree-local shadow.

    The ninja edge invokes us with a RELATIVE `--db decomp.db`, so running
    `ninja` inside a git worktree used to create a brand-new database there
    instead of touching the real one in the main repo. Safe for writes -- a
    worktree build cannot corrupt the shared DB -- but a trap for READS: every
    analysis script that defaults to `--db decomp.db` and is run from that
    worktree then answers out of the shadow copy.

    The shadow carries every row and no judgement at all: 48,325 rows, 0
    verdicts, 0 percentages (main repo: 52,547 rows / 34,598 verdicts).
    Identical work queries, measured 2026-08-19 -- AT_LIMIT certs 0 vs 3,796;
    near-misses 0 vs 89; the 80-95 band 0 vs 325. An empty result set reads as
    "this class is exhausted".

    We used to print a warning here and create it anyway. A warning inside a
    3,000-line ninja build is not a guard. Now the build simply does not write
    a shadow, and `orchestrator.database` refuses to read one.
    """
    if os.environ.get("DC3_ALLOW_SHADOW_DB") == "1":
        return False
    return database.check_is_shadow(db_path)


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

    if is_worktree_shadow(args.db):
        main_db = database.shadow_target(args.db)
        print(f"[db-sync] skipping: '{args.db}' resolves to a worktree-local "
              f"shadow database.")
        print(f"[db-sync]   the real one is {main_db}")
        print(f"[db-sync]   a worktree build has no business writing verdicts, "
              f"and a shadow DB answers")
        print(f"[db-sync]   work queries with an empty set that reads as "
              f"'this class is exhausted'.")
        print(f"[db-sync]   Pass --db {main_db} to sync for real, or "
              f"DC3_ALLOW_SHADOW_DB=1 to force a local one.")
        return

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
        # treat that (or any DB-level problem) as "skip this run", never a build
        # failure. Verdicts are owned by sync_objdiff.py, not this step.
        #
        # sqlite3.Error, not just OperationalError: the worktree tripwire file
        # (setup_worktree.sh) raises DatabaseError "file is not a database", and
        # a guard that breaks the build is a guard people rip out. Same for
        # ShadowDatabaseError if the early skip above is ever bypassed.
        try:
            init_database(args.db)
            result = ingest_report(
                args.report_path,
                db_path=args.db,
                update_existing=not args.no_update,
            )
        except database.ShadowDatabaseError as e:
            print(f"[db-sync] refusing a worktree-local DB; skipping. {e}")
            return
        except sqlite3.Error as e:
            print(f"[db-sync] DB unusable ({e}); skipping (fleet will catch up)")
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
