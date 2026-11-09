#!/usr/bin/env python3
"""Batch unicorn testing -> DB writer.

Runs unicorn comparison across all functions and writes verdicts directly
to the orchestrator database (functions.unicorn_verdict, unicorn_class,
unicorn_confidence, unicorn_tested_at).

Usage:
    python3 scripts/unicorn/batch_to_db.py                     # all units, default parallelism
    python3 scripts/unicorn/batch_to_db.py --unit system/char/CharBones  # single unit
    python3 scripts/unicorn/batch_to_db.py -j 8                # 8 workers
    python3 scripts/unicorn/batch_to_db.py --force             # re-test everything
"""

import argparse
import json
import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime, timezone

# Add project root to path
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, PROJECT_ROOT)

from scripts.orchestrator.database import get_connection, init_database, DEFAULT_DB_PATH
from scripts.unicorn_runner.run import (
    get_all_units, resolve_unit,
    _find_common_text_symbols, _run_comparison_core,
    EXIT_EQUIVALENT, EXIT_DIVERGENT, EXIT_ERROR, EXIT_SKIPPED,
)
from scripts.unicorn_runner.coff import COFFParser
from scripts.unicorn_runner.comparator import classify_divergence
from scripts.unicorn_runner.memory_map import FILL_BYTE
from scripts.unicorn_runner.signal_version import SIGNAL_VERSION, compute_schedule_hash


# The schedule batch_to_db runs: two fills (zero, 0xCD), zero args, no
# typed memory, no hostile mocks. Hashing it once is fine — every result
# this script writes shares the same hash.
_BATCH_TO_DB_SCHEDULE = [
    {"fill_pattern": None, "fixture_type": "fill",
     "arg_r4": 0, "arg_r5": 0, "arg_r6": 0},
    {"fill_pattern": FILL_BYTE, "fixture_type": "fill",
     "arg_r4": 0, "arg_r5": 0, "arg_r6": 0},
]
_BATCH_TO_DB_HASH = compute_schedule_hash(_BATCH_TO_DB_SCHEDULE)


def process_unit(name, decomp_path, orig_path, timeout=5_000_000, only=None):
    """Process a unit's functions.

    `only`, when given, is a set of symbol names to restrict to. That is how a
    targeted refresh (say, the native frontier's non-100 rows) reaches the same
    classifier and the same DB writer as a full sweep, instead of growing a
    second implementation of either.

    Returns list of result dicts with keys: symbol, verdict, class, confidence.
    """
    results = []

    if not os.path.exists(decomp_path) or not os.path.exists(orig_path):
        return results

    try:
        decomp_coff = COFFParser(decomp_path)
        orig_coff = COFFParser(orig_path)
    except Exception as e:
        print(f"  ERROR parsing COFF for {name}: {e}", file=sys.stderr)
        return results

    common = _find_common_text_symbols(decomp_coff, orig_coff)

    for sym_name in common:
        if only is not None and sym_name not in only:
            continue
        verdict = None
        div_class = None
        confidence = None

        # Run zero-fill comparison
        try:
            exit_code, bundle, _, error_msg = _run_comparison_core(
                sym_name, decomp_coff, orig_coff, timeout=timeout)
        except Exception as e:
            results.append({
                "symbol": sym_name,
                "verdict": "ERROR",
                "class": None,
                "confidence": None,
            })
            continue

        reason = None

        unmapped_fp = None

        if exit_code == EXIT_EQUIVALENT:
            verdict = "EQUIVALENT"
            if bundle is not None:
                # Both sides shared the same fingerprint; persist it so
                # downstream queries can find "EQUIV functions that
                # touched null page" for audit.
                unmapped_fp = bundle.result.details.get("unmapped_fingerprint")
        elif exit_code == EXIT_DIVERGENT:
            verdict = "DIVERGENT"
            if bundle is not None:
                div_class = classify_divergence(
                    bundle.result, bundle.decomp_result, bundle.orig_result,
                    bundle.decomp_relocs, bundle.orig_relocs)
                reason = bundle.result.details.get("reason")
                # If divergence WAS unmapped_access_mismatch, record the
                # decomp-side fingerprint (the side we'd compare against
                # a future ground truth).
                if reason == "unmapped_access_mismatch":
                    unmapped_fp = bundle.result.details.get("decomp_fingerprint")
        elif exit_code == EXIT_SKIPPED:
            verdict = "SKIPPED"
        else:
            verdict = "ERROR"

        # Dual fixture for confidence scoring (EQUIVALENT and DIVERGENT only)
        if exit_code in (EXIT_EQUIVALENT, EXIT_DIVERGENT):
            try:
                code2, _, _, _ = _run_comparison_core(
                    sym_name, decomp_coff, orig_coff, timeout=timeout,
                    fill_pattern=FILL_BYTE)
                if exit_code == code2:
                    confidence = "high" if exit_code == EXIT_EQUIVALENT else "stable_divergent"
                else:
                    confidence = "input_sensitive"
            except Exception:
                confidence = None

        results.append({
            "symbol": sym_name,
            "verdict": verdict,
            "class": div_class,
            "confidence": confidence,
            "reason": reason,
            "unmapped_fingerprint": unmapped_fp,
        })

    return results


def _worker(args):
    """Top-level worker for ProcessPoolExecutor (must be picklable)."""
    name, decomp_path, orig_path, timeout, only = args
    try:
        results = process_unit(name, decomp_path, orig_path, timeout, only)
        return (name, results, None)
    except Exception as e:
        return (name, [], str(e))


def write_results_to_db(conn, results, now_str):
    """Write a batch of results to the database.

    Matches symbols in results to rows in the functions table and updates
    unicorn_verdict, unicorn_class, unicorn_confidence, unicorn_tested_at,
    unicorn_signal_version, unicorn_probe_schedule_hash.
    """
    updated = 0
    missing = 0

    for r in results:
        cursor = conn.execute(
            """
            UPDATE functions SET
                unicorn_verdict = ?,
                unicorn_class = ?,
                unicorn_confidence = ?,
                unicorn_reason = ?,
                unicorn_tested_at = ?,
                unicorn_signal_version = ?,
                unicorn_probe_schedule_hash = ?,
                unicorn_unmapped_pages_fingerprint = ?,
                updated_at = CURRENT_TIMESTAMP
            WHERE symbol = ?
            """,
            (r["verdict"], r["class"], r["confidence"], r.get("reason"),
             now_str, SIGNAL_VERSION, _BATCH_TO_DB_HASH,
             r.get("unmapped_fingerprint"),
             r["symbol"]),
        )
        if cursor.rowcount > 0:
            updated += 1
        else:
            missing += 1

    conn.commit()
    return updated, missing


def main():
    parser = argparse.ArgumentParser(
        description="Run unicorn comparisons across all functions and write results to DB")
    parser.add_argument("--unit", type=str, default=None,
                        help="Test a single unit (e.g. system/char/CharBones)")
    parser.add_argument("--db", type=str, default=DEFAULT_DB_PATH,
                        help=f"Database path (default: {DEFAULT_DB_PATH})")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4,
                        help="Number of parallel workers (default: cpu_count)")
    parser.add_argument("--force", action="store_true",
                        help="Re-test all functions (ignore existing DB verdicts)")
    parser.add_argument("--timeout", type=int, default=5_000_000,
                        help="Unicorn execution timeout in microseconds (default: 5000000)")
    parser.add_argument("--symbols", type=str, default=None,
                        help="Restrict to the symbols listed in this file (one per line, "
                             "or a JSONL of records with a 'symbol' key). Units are derived "
                             "from the DB, so only the units those symbols live in are opened.")
    args = parser.parse_args()

    only = None
    if args.symbols:
        only = set()
        with open(args.symbols) as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                if line.startswith("{"):
                    try:
                        rec = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if rec.get("symbol"):
                        only.add(rec["symbol"])
                else:
                    only.add(line)
        if not only:
            print(f"ERROR: --symbols {args.symbols} contained no symbols", file=sys.stderr)
            return 1
        print(f"Restricting to {len(only)} symbols from {args.symbols}", file=sys.stderr)

    # Initialize DB (runs migrations if needed)
    conn = init_database(args.db)

    # Build unit list
    if args.unit:
        try:
            decomp_path, orig_path = resolve_unit(args.unit)
        except ValueError as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return 1
        # Find the full unit name from objdiff.json
        all_units = get_all_units()
        unit_name = args.unit
        for name, dp, op in all_units:
            if dp == decomp_path and op == orig_path:
                unit_name = name
                break
        units = [(unit_name, decomp_path, orig_path)]
    elif only is not None:
        unit_of = {}
        for sym in only:
            row = conn.execute(
                "SELECT unit FROM functions WHERE symbol = ?", (sym,)).fetchone()
            if row and row[0]:
                unit_of[sym] = row[0]
        units = [u for u in get_all_units() if u[0] in set(unit_of.values())]
        covered = {u[0] for u in units}
        # A symbol whose DB unit names no objdiff unit would be requested and
        # then never run -- reported as silence, which reads as "nothing left
        # to do". Say so instead of returning a clean-looking summary.
        unresolved = sorted(s for s, u in unit_of.items() if u not in covered)
        resolved = len(only) - len(unresolved)
        print(f"  {resolved}/{len(only)} symbols resolved to {len(units)} units",
              file=sys.stderr)
        if unresolved:
            print(f"  WARNING: {len(unresolved)} requested symbols name no testable "
                  f"unit and will not be run", file=sys.stderr)
            for sym in unresolved[:10]:
                print(f"    {sym}", file=sys.stderr)
    else:
        units = get_all_units()

    # --symbols implies an explicit re-test request; a unit filter on
    # "has untested functions" would drop exactly the rows we asked for.
    if only is not None:
        args.force = True

    # If not --force, filter out units where all functions already have verdicts
    if not args.force:
        filtered = []
        for name, dp, op in units:
            # Check if any function in this unit lacks a unicorn verdict
            row = conn.execute(
                "SELECT COUNT(*) FROM functions WHERE unit = ? AND unicorn_verdict IS NULL",
                (name,),
            ).fetchone()
            if row[0] > 0:
                filtered.append((name, dp, op))
            else:
                # Also include if no functions exist for this unit (new unit)
                total = conn.execute(
                    "SELECT COUNT(*) FROM functions WHERE unit = ?",
                    (name,),
                ).fetchone()
                if total[0] == 0:
                    filtered.append((name, dp, op))

        skipped_units = len(units) - len(filtered)
        if skipped_units > 0:
            print(f"Skipping {skipped_units} units (all functions already tested). "
                  f"Use --force to re-test.", file=sys.stderr)
        units = filtered

    if not units:
        print("No units to test.", file=sys.stderr)
        return 0

    print(f"Unicorn batch -> DB", file=sys.stderr)
    print(f"  Units: {len(units)}", file=sys.stderr)
    print(f"  Workers: {args.jobs}", file=sys.stderr)
    print(f"  DB: {args.db}", file=sys.stderr)
    print(f"  Force: {args.force}", file=sys.stderr)
    print(file=sys.stderr)

    now_str = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    t0 = time.monotonic()

    total_equiv = 0
    total_div = 0
    total_err = 0
    total_skip = 0
    total_updated = 0
    total_missing = 0
    done = 0
    tested = set()

    work = [(name, dp, op, args.timeout, only) for name, dp, op in units]

    if args.jobs == 1:
        # Single-threaded: write to DB after each unit
        for name, dp, op, timeout, only_ in work:
            results = process_unit(name, dp, op, timeout, only_)
            done += 1

            if not results:
                print(f"  [{done}/{len(work)}] {name}: no eligible functions",
                      file=sys.stderr)
                continue

            # Count verdicts
            tested.update(r["symbol"] for r in results)
            eq = sum(1 for r in results if r["verdict"] == "EQUIVALENT")
            div = sum(1 for r in results if r["verdict"] == "DIVERGENT")
            err = sum(1 for r in results if r["verdict"] == "ERROR")
            sk = sum(1 for r in results if r["verdict"] == "SKIPPED")

            # Write to DB
            updated, missing = write_results_to_db(conn, results, now_str)

            total_equiv += eq
            total_div += div
            total_err += err
            total_skip += sk
            total_updated += updated
            total_missing += missing

            print(f"  [{done}/{len(work)}] {name}: "
                  f"{eq}eq {div}div {err}err {sk}sk "
                  f"({updated} written, {missing} not in DB)",
                  file=sys.stderr)
    else:
        # Multi-threaded: collect results per unit, write to DB in main thread
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            for name, results, error in pool.map(_worker, work):
                done += 1

                if error:
                    print(f"  [{done}/{len(work)}] {name}: ERROR {error}",
                          file=sys.stderr)
                    continue

                if not results:
                    print(f"  [{done}/{len(work)}] {name}: no eligible functions",
                          file=sys.stderr)
                    continue

                tested.update(r["symbol"] for r in results)
                eq = sum(1 for r in results if r["verdict"] == "EQUIVALENT")
                div = sum(1 for r in results if r["verdict"] == "DIVERGENT")
                err = sum(1 for r in results if r["verdict"] == "ERROR")
                sk = sum(1 for r in results if r["verdict"] == "SKIPPED")

                # Write to DB (main thread only — safe)
                updated, missing = write_results_to_db(conn, results, now_str)

                total_equiv += eq
                total_div += div
                total_err += err
                total_skip += sk
                total_updated += updated
                total_missing += missing

                print(f"  [{done}/{len(work)}] {name}: "
                      f"{eq}eq {div}div {err}err {sk}sk "
                      f"({updated} written, {missing} not in DB)",
                      file=sys.stderr)

    elapsed = time.monotonic() - t0
    total_funcs = total_equiv + total_div + total_err + total_skip

    print(file=sys.stderr)
    print(f"=== SUMMARY ===", file=sys.stderr)
    print(f"Time: {elapsed:.1f}s", file=sys.stderr)
    print(f"Functions tested: {total_funcs}", file=sys.stderr)
    print(f"  EQUIVALENT:  {total_equiv}", file=sys.stderr)
    print(f"  DIVERGENT:   {total_div}", file=sys.stderr)
    print(f"  ERROR:       {total_err}", file=sys.stderr)
    print(f"  SKIPPED:     {total_skip}", file=sys.stderr)
    print(f"DB writes: {total_updated} updated, {total_missing} not found in DB", file=sys.stderr)

    # A requested symbol that never ran keeps whatever verdict it already had,
    # with its original unicorn_tested_at -- indistinguishable from a row this
    # run re-confirmed unless it is named. Silence here reads as "nothing left
    # to do", which is the opposite of the truth for an unpaired name.
    if only is not None:
        not_run = sorted(set(only) - tested)
        print(file=sys.stderr)
        print(f"Requested: {len(only)}  ran: {len(tested)}  not run: {len(not_run)}",
              file=sys.stderr)
        if not_run:
            print("NOT RUN -- these keep their previous (stale) verdict:", file=sys.stderr)
            for sym in not_run:
                print(f"  {sym}", file=sys.stderr)

    # Print verdict/class distribution from DB
    print(file=sys.stderr)
    print(f"DB distribution:", file=sys.stderr)
    for row in conn.execute("""
        SELECT unicorn_verdict, unicorn_class, COUNT(*) as cnt
        FROM functions
        WHERE unicorn_verdict IS NOT NULL
        GROUP BY unicorn_verdict, unicorn_class
        ORDER BY cnt DESC
    """):
        v = row["unicorn_verdict"]
        c = row["unicorn_class"] or "-"
        n = row["cnt"]
        print(f"  {v:12s} {c:16s} {n}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
