#!/usr/bin/env python3
"""Apply a unicorn refresh results DB into the live decomp.db (orchestrator only).

Wave-3 Lane B. The refresh sweep (refresh_frontier.py) writes a worktree-local
results DB; THIS script is the single-writer apply step the orchestrator runs on
main after the lane merges. It:

  1. Idempotently adds two source-freshness columns to functions:
       unicorn_source_hash      TEXT  -- per-fn codegen fingerprint at test time
       unicorn_source_hash_at   TEXT  -- when that hash/verdict was recorded
     (these make a verdict detectably stale the moment the function's codegen
      changes — `reconcile`-able by re-hashing the obj, not just by date.)
  2. Updates the unicorn_* verdict columns for every refreshed function with the
     fresh verdict, class, confidence, reason, signal_version, schedule_hash,
     tested_at, AND the source_hash.

DRY-RUN BY DEFAULT. `--apply` writes. Refuses to write unless `--apply` is given.
Rule 2: the lane never runs this against the live DB; the orchestrator does, on
main, as the single writer, after merging this branch.

Usage (orchestrator, on main):
    python3 scripts/unicorn/apply_refresh.py \\
        --results /path/to/unicorn_refresh.db            # dry-run preview
    python3 scripts/unicorn/apply_refresh.py \\
        --results /path/to/unicorn_refresh.db --apply    # write live decomp.db
"""
from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path

LIVE_DB = "/home/free/code/milohax/dc3-decomp/decomp.db"

NEW_COLUMNS = [
    ("unicorn_source_hash", "TEXT"),
    ("unicorn_source_hash_at", "TEXT"),
]


def existing_columns(conn, table):
    return {row[1] for row in conn.execute(f"PRAGMA table_info({table})")}


def migrate(conn, apply):
    actions = []
    cols = existing_columns(conn, "functions")
    for name, typ in NEW_COLUMNS:
        if name in cols:
            actions.append(f"column {name}: present (skip)")
            continue
        actions.append(f"column {name} {typ}: ADD")
        if apply:
            conn.execute(f"ALTER TABLE functions ADD COLUMN {name} {typ}")
    return actions


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results", required=True,
                    help="Refresh results DB (unicorn_refresh table).")
    ap.add_argument("--db", default=LIVE_DB, help=f"Target live DB (default {LIVE_DB}).")
    ap.add_argument("--apply", action="store_true", help="Write (default: dry-run).")
    ap.add_argument("--only-fresh-source", action="store_true",
                    help="Only update rows whose live source_hash differs/absent "
                         "(skip rows already carrying this exact hash).")
    args = ap.parse_args()

    if not Path(args.results).exists():
        print(f"Error: results DB not found: {args.results}", file=sys.stderr)
        return 2
    if not Path(args.db).exists():
        print(f"Error: target DB not found: {args.db}", file=sys.stderr)
        return 2

    res = sqlite3.connect(args.results)
    res.row_factory = sqlite3.Row
    rows = res.execute(
        "SELECT symbol, verdict, class, confidence, reason, source_hash, "
        "signal_version, probe_schedule_hash, tested_at FROM unicorn_refresh "
        "WHERE verdict IN ('EQUIVALENT','DIVERGENT')"
    ).fetchall()
    res.close()
    print(f"Results rows (EQUIVALENT/DIVERGENT): {len(rows)}")

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row
    if args.apply and Path(args.db).resolve() == Path(LIVE_DB).resolve():
        print("WARNING: --apply targets the LIVE decomp.db (orchestrator single-writer).",
              file=sys.stderr)

    print("=== migration ===")
    for a in migrate(conn, args.apply):
        print(f"  {a}")
    has_src_col = "unicorn_source_hash" in existing_columns(conn, "functions")

    updated = 0
    missing = 0
    skipped_fresh = 0
    for r in rows:
        if args.only_fresh_source and has_src_col:
            cur = conn.execute(
                "SELECT unicorn_source_hash FROM functions WHERE symbol=?",
                (r["symbol"],)).fetchone()
            if cur and cur[0] == r["source_hash"]:
                skipped_fresh += 1
                continue
        if args.apply:
            c = conn.execute(
                "UPDATE functions SET "
                "unicorn_verdict=?, unicorn_class=?, unicorn_confidence=?, "
                "unicorn_reason=?, unicorn_tested_at=?, unicorn_signal_version=?, "
                "unicorn_probe_schedule_hash=?, unicorn_source_hash=?, "
                "unicorn_source_hash_at=?, updated_at=CURRENT_TIMESTAMP "
                "WHERE symbol=?",
                (r["verdict"], r["class"], r["confidence"], r["reason"],
                 r["tested_at"], r["signal_version"], r["probe_schedule_hash"],
                 r["source_hash"], r["tested_at"], r["symbol"]),
            )
            if c.rowcount > 0:
                updated += 1
            else:
                missing += 1
        else:
            ex = conn.execute("SELECT 1 FROM functions WHERE symbol=?",
                              (r["symbol"],)).fetchone()
            if ex:
                updated += 1
            else:
                missing += 1

    if args.apply:
        conn.commit()
    conn.close()

    print()
    print(f"  {'UPDATED' if args.apply else 'WOULD UPDATE'}: {updated}")
    print(f"  not in live DB:        {missing}")
    if args.only_fresh_source:
        print(f"  skipped (hash unchanged): {skipped_fresh}")
    if not args.apply:
        print()
        print("  (dry-run — pass --apply to write)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
