#!/usr/bin/env python3
"""Reclassify 'logic' divergent functions with fine-grained sub-categories.

Cross-references unicorn behavioral data with objdiff pattern data to produce
a comprehensive breakdown of why each logic-divergent function actually diverges.

Usage:
    python3 scripts/unicorn/reclassify_logic.py                # dry-run report
    python3 scripts/unicorn/reclassify_logic.py --apply        # update DB
    python3 scripts/unicorn/reclassify_logic.py --rerun        # re-run unicorn for fresh classification
    python3 scripts/unicorn/reclassify_logic.py --rerun --unit system/char/CharBones
"""

import argparse
import os
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, PROJECT_ROOT)

from scripts.orchestrator.database import get_connection, init_database

# Sub-classes that are unfixable (should be AT_LIMIT)
UNFIXABLE_CLASSES = {"merged_call", "merged_arg", "fpr_precision", "build_env"}

# Sub-classes that are hard to fix but maybe possible
HARD_CLASSES = {"stack_layout", "object_memory", "regalloc"}

# Sub-classes that represent real bugs (investigation candidates)
FIXABLE_CLASSES = {"error", "call_count", "call_arg", "return_value"}


def get_logic_functions(conn, unit_filter=None):
    """Get all functions currently classified as unicorn_class='logic'."""
    query = """
        SELECT f.id, f.symbol, f.demangled, f.unit, f.current_percent,
               f.unicorn_verdict, f.unicorn_class, f.unicorn_reason,
               f.verdict, f.has_linker_merged, f.primary_pattern
        FROM functions f
        WHERE f.unicorn_class = 'logic'
          AND f.unicorn_verdict = 'DIVERGENT'
    """
    params = []
    if unit_filter:
        query += " AND f.unit GLOB ?"
        params.append(f"*{unit_filter}*")
    query += " ORDER BY f.current_percent DESC"
    return [dict(r) for r in conn.execute(query, params).fetchall()]


def reclassify_from_db(conn, functions):
    """Reclassify functions using existing DB data (no re-running unicorn).

    Uses stored unicorn_reason + objdiff patterns to infer a better class.
    """
    reclassified = {}

    for f in functions:
        reason = f.get("unicorn_reason")
        has_merged = f.get("has_linker_merged", 0)
        pattern = f.get("primary_pattern") or ""
        new_class = None

        # Cross-reference reason with objdiff merged status
        if reason == "call_count_mismatch":
            if has_merged:
                new_class = "merged_call"
            else:
                new_class = "call_count"

        elif reason == "call_arg_mismatch":
            if has_merged:
                new_class = "merged_arg"
            else:
                new_class = "call_arg"

        elif reason == "fpr_return_mismatch":
            new_class = "fpr_precision"

        elif reason == "return_value_mismatch":
            new_class = "return_value"

        elif reason == "memory_mismatch":
            new_class = "object_memory"

        elif reason in ("error_mismatch", "decomp_error", "orig_error"):
            new_class = "error"

        # If we have objdiff merged pattern but no reason stored
        elif reason is None and has_merged:
            new_class = "merged_call"  # best guess

        if new_class and new_class != "logic":
            reclassified[f["id"]] = {
                "function": f,
                "old_class": "logic",
                "new_class": new_class,
            }

    return reclassified


def reclassify_with_rerun(conn, functions, unit_filter=None, timeout=5_000_000):
    """Re-run unicorn on logic-divergent functions for fresh classification.

    This uses the updated classify_divergence which returns fine-grained classes.
    """
    from scripts.unicorn_runner.run import (
        get_all_units, _find_common_text_symbols, _run_comparison_core,
        EXIT_DIVERGENT,
    )
    from scripts.unicorn_runner.coff import COFFParser
    from scripts.unicorn_runner.comparator import classify_divergence

    # Build symbol -> function mapping
    symbol_map = {f["symbol"]: f for f in functions}

    # Group by unit
    units_needed = set()
    for f in functions:
        if f["unit"]:
            units_needed.add(f["unit"])

    all_units = get_all_units()
    reclassified = {}
    tested = 0

    for name, dp, op in all_units:
        if name not in units_needed:
            continue
        if not os.path.exists(dp) or not os.path.exists(op):
            continue

        try:
            decomp_coff = COFFParser(dp)
            orig_coff = COFFParser(op)
        except Exception:
            continue

        common = _find_common_text_symbols(decomp_coff, orig_coff)

        for sym in common:
            if sym not in symbol_map:
                continue

            f = symbol_map[sym]
            try:
                exit_code, bundle, _, _ = _run_comparison_core(
                    sym, decomp_coff, orig_coff, timeout=timeout)
            except Exception:
                continue

            tested += 1
            if exit_code == EXIT_DIVERGENT and bundle is not None:
                new_class = classify_divergence(
                    bundle.result, bundle.decomp_result, bundle.orig_result,
                    bundle.decomp_relocs, bundle.orig_relocs)
                reason = bundle.result.details.get("reason")

                if new_class != "logic":
                    reclassified[f["id"]] = {
                        "function": f,
                        "old_class": "logic",
                        "new_class": new_class,
                        "reason": reason,
                    }

    print(f"  Re-tested {tested} functions", file=sys.stderr)
    return reclassified


def print_summary(reclassified, total_logic):
    """Print a summary of reclassification results."""
    # Group by new class
    by_class = {}
    for info in reclassified.values():
        cls = info["new_class"]
        by_class.setdefault(cls, []).append(info)

    print(f"\n{'='*60}")
    print(f"Reclassification Summary")
    print(f"{'='*60}")
    print(f"Total logic-divergent: {total_logic}")
    print(f"Reclassified:          {len(reclassified)}")
    print(f"Remaining as logic:    {total_logic - len(reclassified)}")

    print(f"\nNew class breakdown:")
    print(f"  {'Class':<20s} {'Count':>6s}  {'Fixable?':<12s}")
    print(f"  {'-'*20} {'-'*6}  {'-'*12}")

    for cls in sorted(by_class.keys(), key=lambda c: -len(by_class[c])):
        count = len(by_class[cls])
        if cls in UNFIXABLE_CLASSES:
            fixable = "No"
        elif cls in HARD_CLASSES:
            fixable = "Hard"
        elif cls in FIXABLE_CLASSES:
            fixable = "YES"
        else:
            fixable = "Unknown"
        print(f"  {cls:<20s} {count:>6d}  {fixable:<12s}")

    # Show fixable candidates
    fixable_funcs = []
    for info in reclassified.values():
        if info["new_class"] in FIXABLE_CLASSES:
            fixable_funcs.append(info)

    if fixable_funcs:
        fixable_funcs.sort(key=lambda x: -(x["function"].get("current_percent") or 0))
        print(f"\nFixable candidates ({len(fixable_funcs)}):")
        for info in fixable_funcs[:30]:
            f = info["function"]
            pct = f.get("current_percent") or 0
            name = f.get("demangled") or f["symbol"]
            if len(name) > 60:
                name = name[:57] + "..."
            print(f"  {pct:6.2f}%  {info['new_class']:<14s}  {name}")
        if len(fixable_funcs) > 30:
            print(f"  ... ({len(fixable_funcs) - 30} more)")


def main():
    parser = argparse.ArgumentParser(
        description="Reclassify 'logic' divergent functions with fine-grained sub-categories"
    )
    parser.add_argument("--apply", action="store_true",
                        help="Update unicorn_class in DB (default: dry-run)")
    parser.add_argument("--rerun", action="store_true",
                        help="Re-run unicorn for fresh classification (slower but more accurate)")
    parser.add_argument("--unit", type=str, default=None,
                        help="Filter by unit pattern")
    parser.add_argument("--db", type=str, default="decomp.db",
                        help="Database path")
    parser.add_argument("--timeout", type=int, default=5_000_000,
                        help="Unicorn timeout in microseconds")
    args = parser.parse_args()

    conn = init_database(args.db)

    # Get current logic-divergent functions
    functions = get_logic_functions(conn, args.unit)
    print(f"Found {len(functions)} logic-divergent functions", file=sys.stderr)

    if not functions:
        print("No logic-divergent functions to reclassify.")
        return

    if args.rerun:
        print("Re-running unicorn with updated classifier...", file=sys.stderr)
        t0 = time.monotonic()
        reclassified = reclassify_with_rerun(
            conn, functions, args.unit, args.timeout)
        elapsed = time.monotonic() - t0
        print(f"  Completed in {elapsed:.1f}s", file=sys.stderr)
    else:
        print("Reclassifying from stored DB data...", file=sys.stderr)
        reclassified = reclassify_from_db(conn, functions)

    print_summary(reclassified, len(functions))

    if args.apply and reclassified:
        print(f"\nApplying {len(reclassified)} reclassifications to DB...")
        for fid, info in reclassified.items():
            reason = info.get("reason")
            conn.execute(
                """UPDATE functions SET
                    unicorn_class = ?,
                    unicorn_reason = COALESCE(?, unicorn_reason),
                    updated_at = CURRENT_TIMESTAMP
                WHERE id = ?""",
                (info["new_class"], reason, fid),
            )
        conn.commit()
        print(f"  Done. Updated {len(reclassified)} functions.")

        # Show new class distribution
        print("\nUpdated unicorn_class distribution (DIVERGENT only):")
        for row in conn.execute("""
            SELECT unicorn_class, COUNT(*) as cnt
            FROM functions
            WHERE unicorn_verdict = 'DIVERGENT'
            GROUP BY unicorn_class
            ORDER BY cnt DESC
        """):
            print(f"  {row['unicorn_class'] or '(null)':<20s} {row['cnt']}")
    elif not args.apply and reclassified:
        print(f"\nDry run. Use --apply to commit {len(reclassified)} changes.")


if __name__ == "__main__":
    main()
