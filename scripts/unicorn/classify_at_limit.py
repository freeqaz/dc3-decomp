#!/usr/bin/env python3
"""Bulk AT_LIMIT classification using unicorn + objdiff signals.

Rules:
  1. Unicorn EQUIVALENT + objdiff < 100% — behavior matches but assembly differs
     (register swaps, scheduling). Current percent IS the theoretical max.
  2. unicorn_class = 'build_env' — differences from __FILE__ strings or merged
     symbols. Unfixable from source.
  3. reachable_100 = 0 AND has_linker_merged = 1 — already flagged by
     detect_patterns but not yet marked AT_LIMIT.

Also cleans up corrupted verdict values (hallucinated strings from agents).

Safety: never reclassifies COMPLETE functions.
"""

import argparse
import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, PROJECT_ROOT)

from scripts.orchestrator.database import get_connection, init_database

# Valid verdicts — anything not in this set is corrupted
VALID_VERDICTS = {"COMPLETE", "AT_LIMIT", "NEAR_COMPLETE", None}


def find_corrupted_verdicts(conn):
    """Find functions with corrupted (hallucinated) verdict values."""
    rows = conn.execute(
        """SELECT id, symbol, demangled, verdict, current_percent
           FROM functions
           WHERE verdict IS NOT NULL
             AND verdict NOT IN ('COMPLETE', 'AT_LIMIT', 'NEAR_COMPLETE')"""
    ).fetchall()
    return [dict(r) for r in rows]


def find_rule1_candidates(conn):
    """Rule 1: Unicorn EQUIVALENT + objdiff < 100%.

    Behavior matches but assembly doesn't — register swaps, scheduling diffs.
    """
    rows = conn.execute(
        """SELECT id, symbol, demangled, current_percent, unicorn_verdict, unicorn_confidence
           FROM functions
           WHERE unicorn_verdict = 'EQUIVALENT'
             AND current_percent IS NOT NULL
             AND current_percent < 100
             AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"""
    ).fetchall()
    return [dict(r) for r in rows]


def find_rule2_candidates(conn):
    """Rule 2: unicorn_class = 'build_env'.

    __FILE__ string or merged symbol differences — unfixable from source.
    """
    rows = conn.execute(
        """SELECT id, symbol, demangled, current_percent, unicorn_class
           FROM functions
           WHERE unicorn_class = 'build_env'
             AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"""
    ).fetchall()
    return [dict(r) for r in rows]


def find_rule2b_candidates(conn):
    """Rule 2b: Unfixable unicorn sub-classes.

    merged_call, merged_arg, fpr_precision — all unfixable from source.
    """
    rows = conn.execute(
        """SELECT id, symbol, demangled, current_percent, unicorn_class
           FROM functions
           WHERE unicorn_class IN ('merged_call', 'merged_arg', 'fpr_precision')
             AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"""
    ).fetchall()
    return [dict(r) for r in rows]


def find_rule3_candidates(conn):
    """Rule 3: reachable_100 = 0 AND has_linker_merged = 1.

    Already flagged by detect_patterns but not yet marked AT_LIMIT.
    """
    rows = conn.execute(
        """SELECT id, symbol, demangled, current_percent, primary_pattern
           FROM functions
           WHERE reachable_100 = 0
             AND has_linker_merged = 1
             AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
             AND excluded = 0"""
    ).fetchall()
    return [dict(r) for r in rows]


def main():
    parser = argparse.ArgumentParser(
        description="Bulk classify functions as AT_LIMIT using unicorn + objdiff signals"
    )
    parser.add_argument(
        "--apply", action="store_true",
        help="Apply changes to DB (default: dry-run)"
    )
    parser.add_argument(
        "--db", type=str, default="decomp.db",
        help="Path to orchestrator database"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Show per-function details"
    )

    args = parser.parse_args()
    dry_run = not args.apply

    if dry_run:
        print("=== DRY RUN (use --apply to commit changes) ===\n")

    conn = init_database(args.db)

    # Phase 0: Clean up corrupted verdicts
    corrupted = find_corrupted_verdicts(conn)
    print(f"Corrupted verdicts: {len(corrupted)}")
    if corrupted and args.verbose:
        for f in corrupted[:20]:
            print(f"  {f['verdict']:20s}  {f['current_percent'] or '?':>6}%  {f['demangled'] or f['symbol']}")
    if corrupted and not dry_run:
        for f in corrupted:
            conn.execute(
                "UPDATE functions SET verdict = NULL, updated_at = CURRENT_TIMESTAMP WHERE id = ?",
                (f["id"],),
            )
        conn.commit()
        print(f"  -> Cleared {len(corrupted)} corrupted verdicts")

    # Rule 1: Unicorn EQUIVALENT + < 100%
    rule1 = find_rule1_candidates(conn)
    print(f"\nRule 1 (unicorn EQUIVALENT + <100%): {len(rule1)} candidates")
    if rule1 and args.verbose:
        for f in rule1[:20]:
            print(f"  {f['current_percent']:6.2f}%  {f['demangled'] or f['symbol']}")
        if len(rule1) > 20:
            print(f"  ... ({len(rule1) - 20} more)")
    if rule1 and not dry_run:
        for f in rule1:
            conn.execute(
                """UPDATE functions SET
                    verdict = 'AT_LIMIT',
                    verdict_reason = 'unicorn_equivalent_asm_mismatch',
                    updated_at = CURRENT_TIMESTAMP
                WHERE id = ?""",
                (f["id"],),
            )
        conn.commit()
        print(f"  -> Marked {len(rule1)} as AT_LIMIT")

    # Rule 2: build_env class
    rule2 = find_rule2_candidates(conn)
    print(f"\nRule 2 (unicorn build_env class): {len(rule2)} candidates")
    if rule2 and args.verbose:
        for f in rule2[:20]:
            print(f"  {f['current_percent'] or '?':>6}%  {f['demangled'] or f['symbol']}")
        if len(rule2) > 20:
            print(f"  ... ({len(rule2) - 20} more)")
    if rule2 and not dry_run:
        for f in rule2:
            conn.execute(
                """UPDATE functions SET
                    verdict = 'AT_LIMIT',
                    verdict_reason = 'unicorn_build_env',
                    updated_at = CURRENT_TIMESTAMP
                WHERE id = ?""",
                (f["id"],),
            )
        conn.commit()
        print(f"  -> Marked {len(rule2)} as AT_LIMIT")

    # Rule 2b: Unfixable unicorn sub-classes (merged_call, merged_arg, fpr_precision)
    rule2b = find_rule2b_candidates(conn)
    print(f"\nRule 2b (unfixable unicorn sub-classes): {len(rule2b)} candidates")
    if rule2b and args.verbose:
        for f in rule2b[:20]:
            print(f"  {f['current_percent'] or '?':>6}%  [{f['unicorn_class']}]  {f['demangled'] or f['symbol']}")
        if len(rule2b) > 20:
            print(f"  ... ({len(rule2b) - 20} more)")
    if rule2b and not dry_run:
        for f in rule2b:
            conn.execute(
                """UPDATE functions SET
                    verdict = 'AT_LIMIT',
                    verdict_reason = ?,
                    updated_at = CURRENT_TIMESTAMP
                WHERE id = ?""",
                (f"unicorn_{f['unicorn_class']}", f["id"]),
            )
        conn.commit()
        print(f"  -> Marked {len(rule2b)} as AT_LIMIT")

    # Rule 3: unreachable + linker merged
    rule3 = find_rule3_candidates(conn)
    print(f"\nRule 3 (unreachable + linker merged): {len(rule3)} candidates")
    if rule3 and args.verbose:
        for f in rule3[:20]:
            print(f"  {f['current_percent'] or '?':>6}%  {f['demangled'] or f['symbol']}  [{f['primary_pattern']}]")
        if len(rule3) > 20:
            print(f"  ... ({len(rule3) - 20} more)")
    if rule3 and not dry_run:
        for f in rule3:
            conn.execute(
                """UPDATE functions SET
                    verdict = 'AT_LIMIT',
                    verdict_reason = 'unreachable_linker_merged',
                    updated_at = CURRENT_TIMESTAMP
                WHERE id = ?""",
                (f["id"],),
            )
        conn.commit()
        print(f"  -> Marked {len(rule3)} as AT_LIMIT")

    # Summary
    total_candidates = len(rule1) + len(rule2) + len(rule2b) + len(rule3)
    print(f"\n{'=' * 40}")
    print(f"Total candidates: {total_candidates}")
    print(f"Corrupted verdicts: {len(corrupted)}")

    if dry_run:
        print("\nRun with --apply to commit changes.")
    else:
        # Show updated verdict distribution
        rows = conn.execute(
            "SELECT verdict, COUNT(*) FROM functions GROUP BY verdict ORDER BY COUNT(*) DESC"
        ).fetchall()
        print("\nUpdated verdict distribution:")
        for row in rows:
            v = row[0] or "(null)"
            print(f"  {v:16s}  {row[1]}")


if __name__ == "__main__":
    main()
