#!/usr/bin/env python3
"""
Re-check AT_LIMIT functions with NULL/0% match to find newly-matching functions.
Uses the project's objdiff-cli with --verdict flag for correct JSON output.

Usage:
    python3 scripts/recheck_stale.py             # Re-check all stale AT_LIMIT entries
    python3 scripts/recheck_stale.py --dry-run    # Preview without updating DB
    python3 scripts/recheck_stale.py -j 8         # Use 8 parallel workers

This script WRITES `verdict='COMPLETE'`
-------------------------------------------
...which makes it a certificate writer, and a certificate written from an
unpatched tree is a measurement of something that does not exist.  It is not
recoverable afterwards either: patch state is CONTENT-keyed (obj_patch_io.py
preserves each object's mtime across the in-place rewrite), so a certificate
minted from a bypassed tree is indistinguishable from an earned one by anything
except re-measuring.  `ninja <one>.obj` skips the five post-compile patchers
entirely -- measured at -1.22 pp of a unit's `matched_functions_percent` -- and
nothing announces it.  So this refuses to run at all unless
`patch_guard.ensure_patched_tree()` vouches for the tree.

Two further defects, filed as F1 in
docs/analysis/2026-08-22-unfalsifiable-instrument-audit.md, are fixed here with
the guard because a guard in front of the wrong ruler is theatre:

  * it read `instruction_summary.equal_percent`, a THIRD ruler that agrees with
    neither report.json nor sync_match_percent.py (99.67 vs 99.98 on
    ObjectDir::Save).  It now reads `batch_check.match_percent_from_diff`, the
    one function in this repo that knows which of objdiff's four numbers is the
    canonical scorer.
  * `except Exception: pass` collapsed every failure -- a renamed JSON key, a
    missing binary, a timeout -- into `(symbol, None)`, counted as one
    anonymous "error".  An objdiff-cli upgrade that renamed the key would have
    made every function return None while the script printed `Errors: 0` and
    exited 0.  Failures are now categorised and PRINTED, and a run in which
    every function failed exits non-zero.
"""

import argparse
import sqlite3
import subprocess
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "scripts"))

DB_PATH = os.path.join(PROJECT_ROOT, "decomp.db")
OBJDIFF_CLI = os.path.join(PROJECT_ROOT, "bin", "objdiff-cli")


def run_objdiff(symbol):
    """Return (symbol, match_pct, failure_reason).

    Exactly one of `match_pct` / `failure_reason` is None.  A failure is never
    reported as a percentage and never as a silent zero.

    `failure_reason == "stub"` is the one non-error failure: objdiff emits NO
    match-percent key at all when `base_size == 0` (nothing has been written
    for the function yet), and its `instruction_summary.equal_percent` is then
    a hard 0.0.  The old code read exactly that 0.0 and filed 249 of this
    tree's 280 stale AT_LIMIT rows as "Unchanged (0%)" -- the right bucket for
    the wrong reason, and byte-identical to what a RENAMED objdiff key would
    have produced.  Splitting the two apart is the point: a stub is a finding,
    a missing key on a function that HAS code is an instrument failure.
    """
    from batch_check import match_percent_from_diff
    try:
        result = subprocess.run(
            [OBJDIFF_CLI, "diff", "-p", PROJECT_ROOT, symbol, "-f", "json", "--verdict"],
            capture_output=True, text=True, timeout=60
        )
    except FileNotFoundError:
        return (symbol, None, "objdiff-cli-missing")
    except subprocess.TimeoutExpired:
        return (symbol, None, "timeout")
    except Exception as exc:  # noqa: BLE001 -- classified, not swallowed
        return (symbol, None, f"spawn-failed:{type(exc).__name__}")

    if result.returncode != 0:
        return (symbol, None, "objdiff-nonzero")
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError:
        return (symbol, None, "invalid-json")

    pct, ruler = match_percent_from_diff(data)
    if pct is None:
        if data.get("base_size") == 0:
            # Unimplemented stub: objdiff has nothing to score. A finding, not
            # an instrument failure -- and never promotable.
            return (symbol, None, "stub")
        # The renamed-key case, on a function that HAS a body. This MUST NOT
        # read as 0% -- 0% is a finding about the code, and this is not.
        return (symbol, None, "no-match-percent-key")
    return (symbol, pct, None)


def main():
    parser = argparse.ArgumentParser(
        description="Re-check AT_LIMIT functions with stale (NULL/0%%) match data"
    )
    parser.add_argument("-j", "--jobs", type=int, default=6, help="Parallel workers (default: 6)")
    parser.add_argument("--dry-run", action="store_true", help="Preview without updating DB")
    parser.add_argument("--db", type=str, default=DB_PATH, help="Database path")
    parser.add_argument("--no-build", action="store_true",
                        help="Verify the tree's patch state without running "
                             "`ninja post-compile` first. Still REFUSES an "
                             "unpatched tree -- it just will not fix it.")
    args = parser.parse_args()

    # REFUSE before reading a single object. A COMPLETE written from a tree
    # that skipped the post-compile patchers describes raw compiler output.
    # This is first so that even --dry-run cannot print a number off a tree
    # nobody can vouch for: the printed number is what a human acts on.
    from orchestrator.patch_guard import UnpatchedTreeError, ensure_patched_tree
    try:
        note = ensure_patched_tree(PROJECT_ROOT, build=not args.no_build)
    except UnpatchedTreeError as exc:
        print(f"REFUSING to re-check: {exc}", file=sys.stderr)
        return 2
    print(f"[patch-guard] {note}", file=sys.stderr)

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
        return 0

    symbols = [row["symbol"] for row in rows]
    complete = 0
    improved = 0
    unchanged = 0
    stubs = 0
    errors = 0
    processed = 0
    failures: dict[str, int] = {}

    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_objdiff, sym): sym for sym in symbols}
        for future in as_completed(futures):
            sym, match_pct, reason = future.result()
            processed += 1

            if match_pct is None:
                if reason == "stub":
                    stubs += 1
                else:
                    errors += 1
                    failures[reason] = failures.get(reason, 0) + 1
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
                      f"unchanged: {unchanged}, stubs: {stubs}, errors: {errors}")
                sys.stdout.flush()

    if not args.dry_run:
        conn.commit()

    print(f"\nDone. Processed {processed}/{len(rows)} functions:")
    print(f"  -> COMPLETE (100%%): {complete}")
    print(f"  -> Improved (>0%%):  {improved}")
    print(f"  -> Unchanged (0%%):  {unchanged}")
    print(f"  -> Stubs (base_size=0, nothing to score): {stubs}")
    print(f"  -> Errors:           {errors}")
    for reason, n in sorted(failures.items(), key=lambda kv: -kv[1]):
        print(f"       {reason}: {n}")
    scored = complete + improved + unchanged
    print(f"  denominator: {scored} scored + {stubs} stubs + {errors} errors "
          f"= {scored + stubs + errors} of {processed} processed")

    conn.close()

    # A run that SCORED NOTHING is a failed run, and it used to be
    # indistinguishable from "nothing improved": `Errors: N`, exit 0. That is
    # exactly the shape an objdiff-cli key rename would take -- every function
    # returning a value the old code read as 0.0%. Stubs do not count towards
    # having measured something; they are the absence of code to measure.
    if processed and scored == 0:
        print(f"\nFAILED: 0 of {processed} functions were SCORED ({stubs} stubs, "
              f"{errors} errors) -- this run measured nothing. Do not read the "
              f"counts above as a result.", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
