#!/usr/bin/env python3
"""Batch promote functions in decomp.db based on unicorn + objdiff analysis.

Systematically promotes functions that are behaviorally correct but not yet
marked COMPLETE. Unicorn behavioral testing is the primary verdict driver —
if a function produces identical results despite assembly differences, those
differences are cosmetic and the function is COMPLETE.

Usage:
    python3 scripts/batch_promote.py                        # dry-run, all functions
    python3 scripts/batch_promote.py --apply                # write to DB
    python3 scripts/batch_promote.py --unit 'system/char/*' # filter by unit
    python3 scripts/batch_promote.py --skip-unicorn          # fast, objdiff-only
    python3 scripts/batch_promote.py --verbose -o out.json   # detailed output
"""

import argparse
import fnmatch
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, PROJECT_ROOT)

from scripts.orchestrator.database import init_database, DEFAULT_DB_PATH
from scripts.unicorn_runner.run import (
    _run_comparison_core,
    EXIT_EQUIVALENT, EXIT_DIVERGENT, EXIT_ERROR, EXIT_SKIPPED,
)
from scripts.unicorn_runner.coff import COFFParser
from scripts.unicorn_runner.comparator import classify_divergence
from scripts.unicorn_runner.memory_map import FILL_BYTE
from scripts.unicorn_runner.engine import UnicornEngine

OBJDIFF_CLI = os.path.join(PROJECT_ROOT, "bin", "objdiff-cli")

# Unicorn divergence classes that are unfixable from source → AT_LIMIT
AT_LIMIT_UNICORN_CLASSES = {
    'build_env', 'merged_call', 'merged_arg',
    'fpr_precision', 'regalloc', 'stack_layout',
    'orig_error',  # original code crashes in unicorn — test infra limitation
}

# objdiff patterns that are unfixable from source
UNFIXABLE_PATTERNS = {
    'LINKER_MERGED', 'DEAD_STORE_ELIMINATION', 'ANONYMOUS_NAMESPACE_HASH',
}

# objdiff patterns that may be fixable (function stays workable)
FIXABLE_PATTERNS = {
    'BOOL_MASK', 'CONTROL_FLOW', 'COMPARISON_STYLE',
    'STATIC_GUARD_COUNTER', 'DYNAMIC_CAST_MISMATCH',
    'ALLOCA_MISMATCH', 'SCOPE_COUNTER_MISMATCH',
    'PROLOGUE_MISMATCH', 'COMMUTATIVE_OP_ORDER',
    'OFFSET_SWAP', 'REGISTER_SWAP',
}


@dataclass
class FunctionResult:
    db_id: int
    symbol: str
    demangled: str
    unit: str
    old_verdict: str | None
    old_percent: float | None
    normalized_pct: float | None = None
    raw_pct: float | None = None
    patterns: list = field(default_factory=list)
    unattributed: int = 0
    objdiff_class: str | None = None
    unicorn_verdict: str | None = None
    unicorn_class: str | None = None
    unicorn_confidence: str | None = None
    unicorn_reason: str | None = None
    new_verdict: str | None = None
    verdict_reason: str = ''
    error: str | None = None


def load_objdiff_units():
    """Load unit info from objdiff.json.

    Returns dict mapping unit_name -> {target_path (original), base_path (decomp)}.
    """
    objdiff_path = os.path.join(PROJECT_ROOT, "objdiff.json")
    with open(objdiff_path) as f:
        config = json.load(f)

    units = {}
    for entry in config.get("units", []):
        name = entry.get("name", "")
        target_path = entry.get("target_path")
        base_path = entry.get("base_path")
        if target_path and base_path:
            units[name] = {
                "target_path": os.path.join(PROJECT_ROOT, target_path),  # original obj
                "base_path": os.path.join(PROJECT_ROOT, base_path),      # decomp obj
            }
    return units


def filter_objdiff_units(objdiff_units, unit_pattern):
    """Filter objdiff_units dict by a glob pattern.

    Matches against the full unit name (e.g. 'default/system/char/CharBones').
    Supports partial paths like 'system/char/*' by trying with '*/...' prefix.
    """
    if not unit_pattern:
        return objdiff_units

    matched = {}
    for name in objdiff_units:
        # Try direct match first
        if fnmatch.fnmatch(name, unit_pattern):
            matched[name] = objdiff_units[name]
            continue
        # Try with leading '*/' for partial paths
        if fnmatch.fnmatch(name, '*/' + unit_pattern):
            matched[name] = objdiff_units[name]
            continue
        # Substring match as fallback (for non-glob patterns)
        if '*' not in unit_pattern and '?' not in unit_pattern:
            if unit_pattern in name:
                matched[name] = objdiff_units[name]
    return matched


def load_scope(conn, objdiff_units, min_pct=None, max_pct=None):
    """Query DB for non-complete functions grouped by unit.

    Returns dict mapping unit_name -> list of function row dicts.
    """
    query = """
        SELECT id, symbol, demangled, unit, current_percent, verdict
        FROM functions
        WHERE excluded = 0
          AND NOT (verdict = 'COMPLETE' AND current_percent >= 100)
          AND unit NOT GLOB 'default/xdk/*'
    """
    params = []

    if min_pct is not None:
        query += " AND current_percent >= ?"
        params.append(min_pct)
    if max_pct is not None:
        query += " AND current_percent <= ?"
        params.append(max_pct)

    rows = conn.execute(query, params).fetchall()

    # Group by unit, filtering to only units present in objdiff.json
    by_unit = {}
    for row in rows:
        unit = row['unit']
        if unit not in objdiff_units:
            continue
        if unit not in by_unit:
            by_unit[unit] = []
        by_unit[unit].append(dict(row))

    return by_unit


def run_objdiff_analysis(symbol):
    """Run objdiff-cli --verdict for a single function.

    Returns parsed JSON dict or None on error.
    """
    try:
        result = subprocess.run(
            [OBJDIFF_CLI, "diff", "-p", PROJECT_ROOT, symbol, "--verdict", "-f", "json"],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            return None
        return json.loads(result.stdout)
    except (subprocess.TimeoutExpired, json.JSONDecodeError, FileNotFoundError):
        return None


def run_unicorn_analysis(symbol, decomp_coff, orig_coff, engine, args, enrichment=None):
    """Run dual-fixture unicorn comparison for one function.

    Args:
        enrichment: optional dict with objdiff data (e.g. has_linker_merged)
            passed through to classify_divergence for better classification.

    Returns dict with keys: verdict, class, confidence, reason.
    """
    timeout = args.unicorn_timeout
    coload = not args.no_coload

    # Run 1: zero fill (primary)
    try:
        code1, bundle1, _, err1 = _run_comparison_core(
            symbol, decomp_coff, orig_coff,
            timeout=timeout, coload=coload, engine=engine)
    except Exception as e:
        return {'verdict': 'ERROR', 'class': None, 'confidence': None, 'reason': str(e)}

    if code1 == EXIT_SKIPPED:
        return {'verdict': 'SKIPPED', 'class': None, 'confidence': None, 'reason': err1}
    if code1 == EXIT_ERROR:
        return {'verdict': 'ERROR', 'class': None, 'confidence': None, 'reason': err1}

    # Run 2: 0xCD fill for confidence scoring
    code2 = None
    try:
        code2, _, _, _ = _run_comparison_core(
            symbol, decomp_coff, orig_coff,
            timeout=timeout, coload=coload, engine=engine,
            fill_pattern=FILL_BYTE)
    except Exception:
        pass

    confidence = 'high' if (code2 is not None and code1 == code2) else 'fixture_sensitive'
    verdict = 'EQUIVALENT' if code1 == EXIT_EQUIVALENT else 'DIVERGENT'

    div_class = None
    unicorn_reason = None
    if verdict == 'DIVERGENT' and bundle1 is not None:
        div_class = classify_divergence(
            bundle1.result, bundle1.decomp_result, bundle1.orig_result,
            bundle1.decomp_relocs, bundle1.orig_relocs,
            enrichment=enrichment)
        unicorn_reason = bundle1.result.details.get('reason')

    return {
        'verdict': verdict,
        'class': div_class,
        'confidence': confidence,
        'reason': unicorn_reason,
    }


def decide_from_objdiff_only(result):
    """Fallback decision when unicorn was skipped or errored.

    Conservative — never promotes to COMPLETE.
    Returns (new_verdict, reason).
    """
    if result.normalized_pct is None:
        return (None, 'no_objdiff_data')

    pattern_names = {p['pattern'] for p in result.patterns}

    # Only unfixable patterns + no unattributed mismatches → AT_LIMIT
    if pattern_names and pattern_names <= UNFIXABLE_PATTERNS and result.unattributed == 0:
        return ('AT_LIMIT', f'unfixable_patterns_only:{",".join(sorted(pattern_names))}')

    return (None, f'unicorn_{result.unicorn_verdict or "skipped"}_objdiff_fallback')


def decide_verdict(result):
    """Decision tree: what verdict should this function get?

    Returns (new_verdict, reason) where new_verdict may be None (no change).
    """
    # Unicorn EQUIVALENT → COMPLETE (behavior matches regardless of asm diffs)
    if result.unicorn_verdict == 'EQUIVALENT':
        return ('COMPLETE', f'unicorn_equivalent_{result.unicorn_confidence}')

    # Unicorn DIVERGENT → classify further
    if result.unicorn_verdict == 'DIVERGENT':
        # Unfixable unicorn class → AT_LIMIT
        if result.unicorn_class in AT_LIMIT_UNICORN_CLASSES:
            return ('AT_LIMIT', f'unicorn_{result.unicorn_class}')

        pattern_names = {p['pattern'] for p in result.patterns}

        # Only unfixable objdiff patterns, 0 unattributed → AT_LIMIT
        if pattern_names and pattern_names <= UNFIXABLE_PATTERNS and result.unattributed == 0:
            return ('AT_LIMIT', 'unfixable_patterns_only')

        # Has fixable patterns → stays workable (no change)
        fixable = pattern_names & FIXABLE_PATTERNS
        if fixable:
            return (None, f'fixable:{",".join(sorted(fixable))}')

        # Logic divergence, no clear pattern → needs investigation
        return (None, f'divergent_{result.unicorn_class or "unknown"}')

    # SKIPPED/ERROR → conservative objdiff-only fallback
    return decide_from_objdiff_only(result)


def analyze_function(func, decomp_coff, orig_coff, engine, args):
    """Run full analysis pipeline for a single function.

    Returns a FunctionResult.
    """
    result = FunctionResult(
        db_id=func['id'],
        symbol=func['symbol'],
        demangled=func['demangled'] or func['symbol'],
        unit=func['unit'],
        old_verdict=func['verdict'],
        old_percent=func['current_percent'],
    )

    # Guard: skip already-COMPLETE functions (shouldn't reach here but safety check)
    if func['verdict'] == 'COMPLETE':
        result.verdict_reason = 'already_complete'
        return result

    # Run objdiff analysis
    objdiff = run_objdiff_analysis(func['symbol'])
    if objdiff:
        result.normalized_pct = objdiff.get('normalized_match_percent')
        result.raw_pct = objdiff.get('raw_match_percent')
        analysis = objdiff.get('analysis', {})
        result.patterns = analysis.get('patterns', [])
        result.unattributed = analysis.get('unattributed_mismatches', 0)
        verdict_info = objdiff.get('verdict', {})
        result.objdiff_class = verdict_info.get('classification')

    # If normalized 100% → COMPLETE (no unicorn needed, asm already matches)
    if result.normalized_pct is not None and result.normalized_pct >= 100.0:
        result.new_verdict = 'COMPLETE'
        result.verdict_reason = 'normalized_100'
        return result

    # Run unicorn analysis (unless disabled)
    if not args.skip_unicorn and engine is not None:
        # Pass objdiff enrichment for better divergence classification
        enrichment = {'has_linker_merged': 'LINKER_MERGED' in result.patterns} if result.patterns else None
        unicorn = run_unicorn_analysis(func['symbol'], decomp_coff, orig_coff, engine, args, enrichment=enrichment)
        result.unicorn_verdict = unicorn['verdict']
        result.unicorn_class = unicorn['class']
        result.unicorn_confidence = unicorn['confidence']
        result.unicorn_reason = unicorn['reason']

    # Apply decision tree
    new_verdict, reason = decide_verdict(result)
    result.new_verdict = new_verdict
    result.verdict_reason = reason

    return result


def process_unit(unit_name, funcs, unit_info, args):
    """Process all functions in a unit.

    Parses COFF files once and reuses a single UnicornEngine for the whole unit.
    Returns list of FunctionResult.
    """
    decomp_path = unit_info['base_path']   # base_path = decomp obj
    orig_path = unit_info['target_path']   # target_path = original obj

    if not os.path.exists(decomp_path) or not os.path.exists(orig_path):
        return [
            FunctionResult(
                db_id=f['id'], symbol=f['symbol'],
                demangled=f['demangled'] or f['symbol'],
                unit=unit_name, old_verdict=f['verdict'],
                old_percent=f['current_percent'],
                error=f'obj missing: {decomp_path}',
            )
            for f in funcs
        ]

    try:
        decomp_coff = COFFParser(decomp_path)
        orig_coff = COFFParser(orig_path)
    except Exception as e:
        return [
            FunctionResult(
                db_id=f['id'], symbol=f['symbol'],
                demangled=f['demangled'] or f['symbol'],
                unit=unit_name, old_verdict=f['verdict'],
                old_percent=f['current_percent'],
                error=f'COFF parse failed: {e}',
            )
            for f in funcs
        ]

    # Create one UnicornEngine per unit (reused across all functions)
    engine = None
    if not args.skip_unicorn:
        try:
            engine = UnicornEngine()
        except Exception as e:
            print(f"  WARNING: UnicornEngine init failed for {unit_name}: {e}", file=sys.stderr)

    results = []
    try:
        results = _analyze_unit_functions(
            funcs, decomp_coff, orig_coff, engine, args, unit_name)
    finally:
        # One engine per unit is fine; one LEAKED engine per unit is not.
        # Each holds a multi-MB QEMU translator buffer that only the cycle
        # collector would ever free, and the process dies at ~22 of them.
        if engine is not None:
            engine.close()
    return results


def _analyze_unit_functions(funcs, decomp_coff, orig_coff, engine, args,
                            unit_name):
    results = []
    for func in funcs:
        try:
            r = analyze_function(func, decomp_coff, orig_coff, engine, args)
        except Exception as e:
            r = FunctionResult(
                db_id=func['id'], symbol=func['symbol'],
                demangled=func['demangled'] or func['symbol'],
                unit=unit_name, old_verdict=func['verdict'],
                old_percent=func['current_percent'],
                error=str(e),
            )
        results.append(r)
    return results


def apply_results(conn, results, dry_run):
    """Write verdict promotions and unicorn fields to the database.

    Safety: WHERE clause prevents downgrading COMPLETE functions.
    Returns (verdict_updates, unicorn_updates) counts.
    """
    verdict_updates = 0
    unicorn_updates = 0

    for r in results:
        if r.new_verdict and r.new_verdict != r.old_verdict:
            # Verdict promotion: update verdict + unicorn fields atomically
            if not dry_run:
                conn.execute(
                    """UPDATE functions SET
                        verdict = ?, verdict_reason = ?,
                        current_percent = COALESCE(?, current_percent),
                        unicorn_verdict = COALESCE(?, unicorn_verdict),
                        unicorn_class = COALESCE(?, unicorn_class),
                        unicorn_confidence = COALESCE(?, unicorn_confidence),
                        unicorn_reason = COALESCE(?, unicorn_reason),
                        unicorn_tested_at = CASE WHEN ? IS NOT NULL
                                             THEN CURRENT_TIMESTAMP
                                             ELSE unicorn_tested_at END,
                        updated_at = CURRENT_TIMESTAMP
                    WHERE id = ? AND (verdict IS NULL OR verdict != 'COMPLETE')
                    """,
                    (r.new_verdict, r.verdict_reason, r.normalized_pct,
                     r.unicorn_verdict, r.unicorn_class, r.unicorn_confidence,
                     r.unicorn_reason, r.unicorn_verdict,
                     r.db_id),
                )
            verdict_updates += 1
        elif r.unicorn_verdict and r.unicorn_verdict not in ('SKIPPED', 'ERROR'):
            # No verdict change but update unicorn test results
            if not dry_run:
                conn.execute(
                    """UPDATE functions SET
                        unicorn_verdict = ?, unicorn_class = ?,
                        unicorn_confidence = ?, unicorn_reason = ?,
                        unicorn_tested_at = CURRENT_TIMESTAMP,
                        updated_at = CURRENT_TIMESTAMP
                    WHERE id = ?
                    """,
                    (r.unicorn_verdict, r.unicorn_class,
                     r.unicorn_confidence, r.unicorn_reason, r.db_id),
                )
            unicorn_updates += 1

    if not dry_run:
        conn.commit()

    return verdict_updates, unicorn_updates


def print_report(results, dry_run, verbose, output_path):
    """Print summary report and optionally write JSON output."""
    # Categorize results
    already_complete = [r for r in results if r.old_verdict == 'COMPLETE']
    complete_from_norm = [r for r in results
                          if r.new_verdict == 'COMPLETE' and r.verdict_reason == 'normalized_100']
    complete_from_unicorn = [r for r in results
                              if r.new_verdict == 'COMPLETE' and r.verdict_reason != 'normalized_100']
    at_limit_new = [r for r in results if r.new_verdict == 'AT_LIMIT']
    workable_no_change = [r for r in results
                          if r.new_verdict is None and not r.error and r.old_verdict != 'COMPLETE']
    errors = [r for r in results if r.error]

    mode = "DRY RUN" if dry_run else "APPLIED"
    processed = len(results) - len(already_complete)
    print(f"=== batch_promote [{mode}] ===")
    print(f"Processed: {processed} functions ({len(already_complete)} already complete, skipped)")
    print()

    print(f"Promotions:")
    print(f"  COMPLETE (normalized 100%):    {len(complete_from_norm)}")
    print(f"  COMPLETE (unicorn equivalent): {len(complete_from_unicorn)}")

    if complete_from_unicorn:
        high = sum(1 for r in complete_from_unicorn if r.unicorn_confidence == 'high')
        sens = sum(1 for r in complete_from_unicorn if r.unicorn_confidence == 'fixture_sensitive')
        print(f"    high confidence:             {high}")
        print(f"    fixture_sensitive:           {sens}")

    print(f"  AT_LIMIT (new):                {len(at_limit_new)}")
    if at_limit_new:
        at_limit_by_reason: dict[str, int] = {}
        for r in at_limit_new:
            key = r.verdict_reason.split(':')[0]
            at_limit_by_reason[key] = at_limit_by_reason.get(key, 0) + 1
        for reason, count in sorted(at_limit_by_reason.items(), key=lambda x: -x[1]):
            print(f"    {reason}: {count}")

    print()
    print(f"No change:")
    print(f"  Still workable:                {len(workable_no_change)}")
    if workable_no_change:
        workable_by_reason: dict[str, int] = {}
        for r in workable_no_change:
            key = r.verdict_reason.split(':')[0] if r.verdict_reason else 'unknown'
            workable_by_reason[key] = workable_by_reason.get(key, 0) + 1
        for reason, count in sorted(workable_by_reason.items(), key=lambda x: -x[1]):
            print(f"    {reason}: {count}")
    print(f"  Errors:                        {len(errors)}")

    if verbose:
        print()
        print("Per-function details:")
        # Sort: COMPLETE first, then AT_LIMIT, then workable, then errors
        def sort_key(r):
            v = r.new_verdict or ('z_workable' if not r.error else 'z_error')
            return (v, -(r.normalized_pct or 0))

        for r in sorted(results, key=sort_key):
            if r.old_verdict == 'COMPLETE' and not r.new_verdict:
                continue
            verdict_str = r.new_verdict or ('ERROR' if r.error else 'workable')
            pct_str = f"{r.normalized_pct:.1f}%" if r.normalized_pct is not None else "?%"
            unicorn_str = r.unicorn_verdict or 'skipped'
            pattern_str = ','.join(p['pattern'] for p in r.patterns[:3])
            name = r.demangled if len(r.demangled) <= 55 else r.demangled[:52] + "..."
            if r.error:
                print(f"  {'ERROR':<10s} {pct_str:>7s}  [{unicorn_str:10s}]  {name}")
                print(f"             {r.error[:70]}")
            else:
                print(f"  {verdict_str:<10s} {pct_str:>7s}  [{unicorn_str:10s}]  {name}  {pattern_str}")

    if output_path:
        out = []
        for r in results:
            out.append({
                'db_id': r.db_id,
                'symbol': r.symbol,
                'demangled': r.demangled,
                'unit': r.unit,
                'old_verdict': r.old_verdict,
                'old_percent': r.old_percent,
                'normalized_pct': r.normalized_pct,
                'raw_pct': r.raw_pct,
                'patterns': [p['pattern'] for p in r.patterns],
                'unattributed': r.unattributed,
                'objdiff_class': r.objdiff_class,
                'unicorn_verdict': r.unicorn_verdict,
                'unicorn_class': r.unicorn_class,
                'unicorn_confidence': r.unicorn_confidence,
                'unicorn_reason': r.unicorn_reason,
                'new_verdict': r.new_verdict,
                'verdict_reason': r.verdict_reason,
                'error': r.error,
            })
        with open(output_path, 'w') as f:
            json.dump(out, f, indent=2)
        print(f"\nJSON report written to: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Batch promote functions in decomp.db based on unicorn + objdiff analysis")
    parser.add_argument("--apply", action="store_true",
                        help="Write verdict changes to DB (default: dry-run)")
    parser.add_argument("--unit", type=str, default=None,
                        help="Filter by unit glob (e.g. 'system/char/*' or 'default/system/char/CharBones')")
    parser.add_argument("--min-pct", type=float, default=None, dest="min_pct",
                        help="Only process functions with current_percent >= N")
    parser.add_argument("--max-pct", type=float, default=None, dest="max_pct",
                        help="Only process functions with current_percent <= N")
    parser.add_argument("--skip-unicorn", action="store_true",
                        help="Skip unicorn testing (objdiff-only, fast but conservative)")
    parser.add_argument("--no-coload", action="store_true",
                        help="Disable intra-TU callee co-loading in unicorn")
    parser.add_argument("--unicorn-timeout", type=int, default=5_000_000, dest="unicorn_timeout",
                        help="Unicorn execution timeout in microseconds (default: 5000000)")
    parser.add_argument("--db", type=str, default=DEFAULT_DB_PATH,
                        help=f"Database path (default: {DEFAULT_DB_PATH})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print per-function details in report")
    parser.add_argument("-o", "--output", type=str, default=None,
                        help="Write full JSON report to this file")
    parser.add_argument("--build", action="store_true",
                        help="Run ninja build before analysis")

    args = parser.parse_args()
    dry_run = not args.apply

    if dry_run:
        print("=== DRY RUN (use --apply to commit changes) ===\n")

    # Optional build phase
    if args.build:
        print("Building objects...")
        ret = subprocess.run(["ninja"], cwd=PROJECT_ROOT)
        if ret.returncode != 0:
            print("ERROR: ninja build failed", file=sys.stderr)
            return 1
        print()

    # Initialize DB
    conn = init_database(args.db)

    # Load objdiff.json units, apply unit filter
    print("Loading scope...")
    objdiff_units = load_objdiff_units()
    print(f"  objdiff.json: {len(objdiff_units)} units with both target+base paths")

    if args.unit:
        objdiff_units = filter_objdiff_units(objdiff_units, args.unit)
        print(f"  After unit filter '{args.unit}': {len(objdiff_units)} units")

    # Query DB scope
    by_unit = load_scope(conn, objdiff_units, min_pct=args.min_pct, max_pct=args.max_pct)
    total_funcs = sum(len(v) for v in by_unit.values())
    print(f"  DB scope: {total_funcs} functions across {len(by_unit)} units\n")

    if total_funcs == 0:
        print("No functions to process.")
        return 0

    # Process units sequentially
    all_results: list[FunctionResult] = []
    t0 = time.monotonic()
    units_done = 0
    total_units = len(by_unit)

    for unit_name, funcs in sorted(by_unit.items()):
        units_done += 1
        unit_info = objdiff_units[unit_name]

        print(f"[{units_done}/{total_units}] {unit_name} ({len(funcs)} functions)...")
        sys.stdout.flush()

        results = process_unit(unit_name, funcs, unit_info, args)
        all_results.extend(results)

        # Per-unit summary line
        promoted = sum(1 for r in results
                       if r.new_verdict and r.new_verdict != r.old_verdict)
        if promoted > 0:
            complete = sum(1 for r in results
                           if r.new_verdict == 'COMPLETE' and r.new_verdict != r.old_verdict)
            at_limit = sum(1 for r in results
                           if r.new_verdict == 'AT_LIMIT' and r.new_verdict != r.old_verdict)
            print(f"  -> {promoted} promotions: {complete} COMPLETE, {at_limit} AT_LIMIT")

    elapsed = time.monotonic() - t0
    print(f"\nAnalysis complete in {elapsed:.1f}s\n")

    # Apply results to DB
    verdict_updates, unicorn_updates = apply_results(conn, all_results, dry_run)

    # Print report
    print_report(all_results, dry_run, args.verbose, args.output)

    if dry_run:
        total_promotions = verdict_updates
        print(f"\nDry run: {total_promotions} verdict promotion(s), "
              f"{unicorn_updates} unicorn-only update(s) pending.")
        print("Run with --apply to commit changes.")
    else:
        print(f"\nWrote {verdict_updates} verdict update(s) + "
              f"{unicorn_updates} unicorn-only update(s).")

    return 0


if __name__ == "__main__":
    sys.exit(main())
