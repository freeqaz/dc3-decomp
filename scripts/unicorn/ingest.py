#!/usr/bin/env python3
"""Ingest unicorn runner results into the orchestrator database.

Two modes:
  --from-cache   Parse existing unicorn_cache.json (fast, no re-run)
  --from-results Parse a JSON-lines file from --emit-results (has classification)

Updates functions table with unicorn_verdict, unicorn_class, unicorn_confidence.
"""

import argparse
import json
import os
import sys
from datetime import datetime

# Add project root to path for imports
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, PROJECT_ROOT)

from scripts.orchestrator.database import get_connection, init_database

# Map unicorn exit codes to verdict strings
EXIT_CODE_TO_VERDICT = {
    0: "EQUIVALENT",
    1: "DIVERGENT",
    2: "ERROR",
    3: "SKIPPED",
}

DEFAULT_CACHE_PATH = os.path.join(
    PROJECT_ROOT, "build", "373307D9", "unicorn_cache.json"
)


def ingest_from_cache(cache_path, db_path, verbose=False):
    """Parse unicorn_cache.json and update DB.

    Cache keys are "symbol|decomp_mtime|orig_mtime".
    Cache values are {"exit_code": int, "confidence": str|null}.

    Note: cache entries don't have divergence classification (class field),
    since that requires re-running the comparison. Use --from-results for that.
    """
    with open(cache_path) as f:
        cache_data = json.load(f)

    conn = init_database(db_path)
    now = datetime.utcnow().isoformat()

    # Extract unique symbol -> latest result mapping
    # Multiple cache entries may exist for the same symbol (different mtimes);
    # we take the first one found (they're all equivalent for verdict purposes)
    symbol_results = {}
    for key, entry in cache_data.items():
        symbol = key.split("|")[0]
        if symbol not in symbol_results:
            symbol_results[symbol] = entry

    updated = 0
    skipped = 0
    not_found = 0

    for symbol, entry in symbol_results.items():
        exit_code = entry.get("exit_code")
        confidence = entry.get("confidence")
        verdict = EXIT_CODE_TO_VERDICT.get(exit_code, "ERROR")

        # Look up function in DB
        row = conn.execute(
            "SELECT id FROM functions WHERE symbol = ?", (symbol,)
        ).fetchone()

        if row is None:
            not_found += 1
            if verbose:
                print(f"  NOT FOUND: {symbol}")
            continue

        conn.execute(
            """UPDATE functions SET
                unicorn_verdict = ?,
                unicorn_confidence = ?,
                unicorn_tested_at = ?,
                updated_at = CURRENT_TIMESTAMP
            WHERE id = ?""",
            (verdict, confidence, now, row["id"]),
        )
        updated += 1

    conn.commit()

    print(f"Ingested from cache: {cache_path}")
    print(f"  Symbols in cache: {len(symbol_results)}")
    print(f"  Updated: {updated}")
    print(f"  Not in DB: {not_found}")
    print(f"  Skipped: {skipped}")
    return updated


def ingest_from_results(results_path, db_path, verbose=False):
    """Parse JSON-lines emit file and update DB.

    Each line: {"symbol": str, "verdict": str, "class": str|null, "confidence": str|null}
    """
    conn = init_database(db_path)
    now = datetime.utcnow().isoformat()

    updated = 0
    not_found = 0
    line_count = 0

    with open(results_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            line_count += 1

            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                if verbose:
                    print(f"  BAD JSON: {line[:80]}")
                continue

            symbol = entry.get("symbol")
            verdict = entry.get("verdict")
            div_class = entry.get("class")
            confidence = entry.get("confidence")

            if not symbol or not verdict:
                continue

            row = conn.execute(
                "SELECT id FROM functions WHERE symbol = ?", (symbol,)
            ).fetchone()

            if row is None:
                not_found += 1
                if verbose:
                    print(f"  NOT FOUND: {symbol}")
                continue

            conn.execute(
                """UPDATE functions SET
                    unicorn_verdict = ?,
                    unicorn_class = ?,
                    unicorn_confidence = ?,
                    unicorn_tested_at = ?,
                    updated_at = CURRENT_TIMESTAMP
                WHERE id = ?""",
                (verdict, div_class, confidence, now, row["id"]),
            )
            updated += 1

    conn.commit()

    print(f"Ingested from results: {results_path}")
    print(f"  Lines: {line_count}")
    print(f"  Updated: {updated}")
    print(f"  Not in DB: {not_found}")
    return updated


def show_stats(db_path):
    """Print unicorn verdict distribution from DB."""
    conn = get_connection(db_path)

    rows = conn.execute(
        """SELECT unicorn_verdict, COUNT(*) as cnt
           FROM functions
           WHERE unicorn_verdict IS NOT NULL
           GROUP BY unicorn_verdict
           ORDER BY cnt DESC"""
    ).fetchall()

    if not rows:
        print("No unicorn results in database yet.")
        return

    print("\nUnicorn verdict distribution:")
    for row in rows:
        print(f"  {row['unicorn_verdict']:12s}  {row['cnt']}")

    # Class distribution for DIVERGENT
    class_rows = conn.execute(
        """SELECT unicorn_class, COUNT(*) as cnt
           FROM functions
           WHERE unicorn_verdict = 'DIVERGENT' AND unicorn_class IS NOT NULL
           GROUP BY unicorn_class
           ORDER BY cnt DESC"""
    ).fetchall()

    if class_rows:
        print("\nDivergence class distribution:")
        for row in class_rows:
            print(f"  {row['unicorn_class']:16s}  {row['cnt']}")

    total = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE unicorn_verdict IS NOT NULL"
    ).fetchone()[0]
    print(f"\nTotal tested: {total}")


def main():
    parser = argparse.ArgumentParser(
        description="Ingest unicorn runner results into orchestrator DB"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--from-cache", action="store_true",
        help=f"Parse unicorn_cache.json (default: {DEFAULT_CACHE_PATH})"
    )
    mode.add_argument(
        "--from-results", type=str, metavar="PATH",
        help="Parse JSON-lines file from --emit-results"
    )
    mode.add_argument(
        "--stats", action="store_true",
        help="Show unicorn verdict distribution from DB"
    )
    parser.add_argument(
        "--cache-path", type=str, default=DEFAULT_CACHE_PATH,
        help="Path to unicorn_cache.json"
    )
    parser.add_argument(
        "--db", type=str, default="decomp.db",
        help="Path to orchestrator database"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Show per-symbol details"
    )

    args = parser.parse_args()

    if args.stats:
        show_stats(args.db)
    elif args.from_cache:
        if not os.path.exists(args.cache_path):
            print(f"ERROR: Cache file not found: {args.cache_path}", file=sys.stderr)
            sys.exit(1)
        ingest_from_cache(args.cache_path, args.db, verbose=args.verbose)
        show_stats(args.db)
    elif args.from_results:
        if not os.path.exists(args.from_results):
            print(f"ERROR: Results file not found: {args.from_results}", file=sys.stderr)
            sys.exit(1)
        ingest_from_results(args.from_results, args.db, verbose=args.verbose)
        show_stats(args.db)


if __name__ == "__main__":
    main()
