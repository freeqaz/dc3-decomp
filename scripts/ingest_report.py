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


def warn_if_shadow_db(db_path: Path) -> None:
    """Warn when this run is about to create a worktree-local `decomp.db`.

    The ninja edge invokes us with a RELATIVE `--db decomp.db`, so running
    `ninja` inside a git worktree creates a brand-new database there instead
    of touching the real one in the main repo. That is deliberately safe for
    writes -- a worktree build cannot corrupt the shared DB -- but it lays a
    trap for READS: every analysis script that defaults to `--db decomp.db`
    and is run from that worktree silently answers out of the shadow copy.

    The shadow copy has only the 16 columns `init_database` creates, no
    `excluded`, and -- critically -- no verdicts and no adjudicated
    percentages. So a work-selection query run from a worktree returns
    *nothing*, which reads exactly like "this class is exhausted". Measured
    2026-08-19: main repo 52,547 rows / 34,598 verdicts, a fresh worktree
    48,325 rows / 0 verdicts.

    Say so once, at the moment the trap is set.
    """
    if db_path.exists() or db_path.is_absolute():
        return
    git_dir = Path(".git")
    # In a worktree, .git is a FILE containing "gitdir: <path>"; in the main
    # repo it is a directory.
    if not git_dir.is_file():
        return
    print(f"[db-sync] NOTE: creating a worktree-local {db_path} — it will "
          f"have no verdicts.")
    print(f"[db-sync]       Analysis scripts run from here that default to "
          f"'--db {db_path}' will read")
    print(f"[db-sync]       THIS file, not the main repo's. An empty result "
          f"is not evidence of exhaustion;")
    print(f"[db-sync]       pass --db /path/to/main/repo/decomp.db "
          f"explicitly.")


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

    warn_if_shadow_db(args.db)

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
