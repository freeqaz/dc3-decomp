#!/usr/bin/env python3
"""Unicorn Function Runner — CLI entry point.

Compares function behavior between decomp and original .obj files
by executing both in Unicorn PPC32 BE and comparing observable output.
"""

import argparse
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor

from .coff import COFFParser
from .extractor import (
    extract_from_decomp, extract_from_original,
    has_indirect_branch, classify_indirect_branch, has_ppc64_insns,
)
from .patcher import assign_addresses, patch_function, rewrite_ppc64_insns, prepare_switch_tables
from .memory_map import CODE_BASE
from .engine import execute_function
from .comparator import compare, format_result

# Exit codes
EXIT_EQUIVALENT = 0
EXIT_DIVERGENT = 1
EXIT_ERROR = 2
EXIT_SKIPPED = 3


def resolve_unit(unit_name, project_root=None):
    """Resolve unit name to (decomp_obj_path, orig_obj_path) via objdiff.json.

    Returns (target_path, base_path) or raises ValueError.
    """
    if project_root is None:
        project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    objdiff_path = os.path.join(project_root, "objdiff.json")
    with open(objdiff_path) as f:
        config = json.load(f)

    # Find unit where name ends with the given unit_name
    for entry in config.get("units", []):
        name = entry.get("name", "")
        if name.endswith("/" + unit_name) or name == unit_name:
            target_path = entry.get("target_path")
            base_path = entry.get("base_path")
            if not target_path:
                raise ValueError(f"Unit '{unit_name}' has no target_path")
            if not base_path:
                raise ValueError(f"Unit '{unit_name}' has no base_path (original .obj)")
            return (
                os.path.join(project_root, target_path),
                os.path.join(project_root, base_path),
            )

    raise ValueError(f"Unit '{unit_name}' not found in objdiff.json")


def list_functions(decomp_path, orig_path, decomp_coff=None, orig_coff=None):
    """List eligible functions in both .obj files."""
    if decomp_coff is None:
        decomp_coff = COFFParser(decomp_path)
    if orig_coff is None:
        orig_coff = COFFParser(orig_path)

    # Build sets of function symbols from each side
    decomp_syms = set()
    for sym in decomp_coff.symbols:
        if sym['section'] > 0:
            sec = decomp_coff.sections[sym['section'] - 1]
            if sec['name'].startswith('.text'):
                decomp_syms.add(sym['name'])

    orig_syms = set()
    for sym in orig_coff.symbols:
        if sym['section'] > 0:
            sec = orig_coff.sections[sym['section'] - 1]
            if sec['name'].startswith('.text'):
                orig_syms.add(sym['name'])

    # Find symbols present in both
    common = sorted(decomp_syms & orig_syms)

    eligible = []
    skipped = []
    for sym_name in common:
        # Extract from both sides
        d_bytes, d_relocs = extract_from_decomp(decomp_coff, sym_name)
        o_bytes, o_relocs = extract_from_original(orig_coff, sym_name)

        if d_bytes is None or o_bytes is None or len(d_bytes) == 0 or len(o_bytes) == 0:
            continue

        # Classify indirect branches
        d_class = classify_indirect_branch(d_bytes, d_relocs, decomp_coff)
        o_class = classify_indirect_branch(o_bytes, o_relocs, orig_coff)

        eligible.append((sym_name, len(d_bytes), len(o_bytes)))

    print(f"Eligible functions ({len(eligible)}):")
    for name, d_size, o_size in eligible:
        size_match = "=" if d_size == o_size else "!"
        print(f"  {name}  (decomp={d_size}B, orig={o_size}B) {size_match}")

    if skipped:
        print(f"\nSkipped ({len(skipped)}, indirect branches):")
        for name, reason, d_size, o_size in skipped:
            print(f"  {name}  ({reason}, decomp={d_size}B, orig={o_size}B)")

    return eligible


def run_comparison_inner(symbol, decomp_coff, orig_coff, verbose=False, timeout=5_000_000):
    """Core comparison logic operating on pre-parsed COFF instances.

    Returns (exit_code, output_text) without printing anything.
    """
    # Extract function bytes and relocations
    decomp_bytes, decomp_relocs = extract_from_decomp(decomp_coff, symbol)
    orig_bytes, orig_relocs = extract_from_original(orig_coff, symbol)

    if decomp_bytes is None:
        return EXIT_SKIPPED, f"SKIPPED: Symbol '{symbol}' not found in decomp .obj"
    if orig_bytes is None:
        return EXIT_SKIPPED, f"SKIPPED: Symbol '{symbol}' not found in original .obj"
    if len(decomp_bytes) == 0 or len(orig_bytes) == 0:
        return EXIT_SKIPPED, f"SKIPPED: Symbol '{symbol}' has zero size"

    lines = []
    if verbose:
        lines.append(f"Symbol: {symbol}")
        lines.append(f"  Decomp: {len(decomp_bytes)} bytes, {len(decomp_relocs)} relocs")
        lines.append(f"  Original: {len(orig_bytes)} bytes, {len(orig_relocs)} relocs")

    # Classify indirect branches
    d_class = classify_indirect_branch(decomp_bytes, decomp_relocs, decomp_coff)
    o_class = classify_indirect_branch(orig_bytes, orig_relocs, orig_coff)
    has_switch = d_class == "bctr_switch" or o_class == "bctr_switch"

    # Rewrite PPC64 std/ld instructions to PPC32 stw/lwz
    d_rewrite = rewrite_ppc64_insns(decomp_bytes)
    o_rewrite = rewrite_ppc64_insns(orig_bytes)
    if verbose and (d_rewrite or o_rewrite):
        lines.append(f"  PPC64 rewrites: decomp={d_rewrite}, orig={o_rewrite}")

    # Prepare switch table data (if needed)
    d_rdata_bytes = None
    o_rdata_bytes = None
    d_rdata_override = {}
    o_rdata_override = {}

    if has_switch:
        if d_class == "bctr_switch":
            d_rdata_bytes, d_rdata_override = prepare_switch_tables(
                decomp_coff, symbol, decomp_relocs, CODE_BASE)
            if d_rdata_bytes is None:
                d_rdata_override = {}
        if o_class == "bctr_switch":
            o_rdata_bytes, o_rdata_override = prepare_switch_tables(
                orig_coff, symbol, orig_relocs, CODE_BASE)
            if o_rdata_bytes is None:
                o_rdata_override = {}

        if verbose and (d_rdata_bytes or o_rdata_bytes):
            d_sz = len(d_rdata_bytes) if d_rdata_bytes else 0
            o_sz = len(o_rdata_bytes) if o_rdata_bytes else 0
            lines.append(f"  Switch tables: decomp={d_sz}B, orig={o_sz}B")

    # Patch both sides independently
    try:
        d_trampolines, d_globals = assign_addresses(decomp_relocs)
        o_trampolines, o_globals = assign_addresses(orig_relocs)

        # Override globals for .rdata symbols with actual RDATA_BASE addresses
        d_globals.update(d_rdata_override)
        o_globals.update(o_rdata_override)

        d_code = bytearray(decomp_bytes)
        o_code = bytearray(orig_bytes)

        patch_function(d_code, decomp_relocs, d_trampolines, d_globals, CODE_BASE)
        patch_function(o_code, orig_relocs, o_trampolines, o_globals, CODE_BASE)
    except Exception as e:
        return EXIT_ERROR, f"ERROR: Patching failed: {e}"

    if verbose:
        lines.append(f"  Decomp trampolines: {len(d_trampolines)}")
        lines.append(f"  Decomp globals: {len(d_globals)}")
        lines.append(f"  Original trampolines: {len(o_trampolines)}")
        lines.append(f"  Original globals: {len(o_globals)}")

    # Execute both sides
    try:
        decomp_result = execute_function(
            d_code, d_trampolines, len(d_code),
            timeout=timeout, verbose=verbose, rdata_bytes=d_rdata_bytes)
        orig_result = execute_function(
            o_code, o_trampolines, len(o_code),
            timeout=timeout, verbose=verbose, rdata_bytes=o_rdata_bytes)
    except Exception as e:
        return EXIT_ERROR, f"ERROR: Execution failed: {e}"

    # Compare
    result = compare(decomp_result, orig_result, decomp_relocs, orig_relocs)

    # Format output
    output = format_result(
        result, decomp_result, orig_result,
        decomp_relocs, orig_relocs, verbose=verbose)
    if lines:
        output = "\n".join(lines) + "\n" + output

    if result.verdict == "EQUIVALENT":
        return EXIT_EQUIVALENT, output
    else:
        return EXIT_DIVERGENT, output


def run_comparison(symbol, decomp_path, orig_path, verbose=False, timeout=5_000_000,
                   decomp_coff=None, orig_coff=None):
    """Run the full comparison pipeline for a single function.

    Returns exit code. Accepts optional pre-parsed COFF instances.
    """
    # Parse COFF files if not provided
    if decomp_coff is None or orig_coff is None:
        try:
            if decomp_coff is None:
                decomp_coff = COFFParser(decomp_path)
            if orig_coff is None:
                orig_coff = COFFParser(orig_path)
        except Exception as e:
            print(f"ERROR: Failed to parse .obj files: {e}", file=sys.stderr)
            return EXIT_ERROR

    code, output = run_comparison_inner(symbol, decomp_coff, orig_coff,
                                        verbose=verbose, timeout=timeout)
    if code == EXIT_SKIPPED:
        print(output, file=sys.stderr)
    else:
        print(output)
    return code


def run_batch(decomp_path, orig_path, verbose=False, timeout=5_000_000, quiet=False):
    """Run comparison for all eligible functions in a unit.

    Parses COFF files once and reuses them for all functions.
    If quiet=True, suppresses per-function output (for multiprocessing).

    Returns (equivalent, divergent, errors, skipped) counts.
    """
    decomp_coff = COFFParser(decomp_path)
    orig_coff = COFFParser(orig_path)

    # Suppress list_functions' stdout
    import io
    old_stdout = sys.stdout
    sys.stdout = io.StringIO()
    try:
        eligible = list_functions(decomp_path, orig_path,
                                  decomp_coff=decomp_coff, orig_coff=orig_coff)
    finally:
        sys.stdout = old_stdout

    equivalent = 0
    divergent = 0
    errors = 0
    skipped = 0

    for sym_name, d_size, o_size in eligible:
        code, _output = run_comparison_inner(sym_name, decomp_coff, orig_coff,
                                             verbose=False, timeout=timeout)

        if code == EXIT_EQUIVALENT:
            equivalent += 1
            status = "EQUIVALENT"
        elif code == EXIT_DIVERGENT:
            divergent += 1
            status = "DIVERGENT"
        elif code == EXIT_SKIPPED:
            skipped += 1
            status = "SKIPPED"
        else:
            errors += 1
            status = "ERROR"

        if not quiet:
            print(f"  {status:11s}  {sym_name}")

    return equivalent, divergent, errors, skipped


def get_all_units(project_root=None):
    """Get all units from objdiff.json that have both target_path and base_path."""
    if project_root is None:
        project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    objdiff_path = os.path.join(project_root, "objdiff.json")
    with open(objdiff_path) as f:
        config = json.load(f)

    units = []
    for entry in config.get("units", []):
        target_path = entry.get("target_path")
        base_path = entry.get("base_path")
        if target_path and base_path:
            units.append((
                entry.get("name", ""),
                os.path.join(project_root, target_path),
                os.path.join(project_root, base_path),
            ))

    return units


def _process_unit(args):
    """Worker function for multiprocessing batch-all.

    Must be top-level (not a closure) so it can be pickled.
    Returns (name, equivalent, divergent, errors, skipped, tested).
    """
    name, decomp_path, orig_path, timeout = args
    if not os.path.exists(decomp_path) or not os.path.exists(orig_path):
        return (name, 0, 0, 0, 0, False)
    try:
        eq, div, err, sk = run_batch(decomp_path, orig_path,
                                      timeout=timeout, quiet=True)
        return (name, eq, div, err, sk, True)
    except Exception as e:
        return (name, 0, 0, 1, 0, True)


def main():
    parser = argparse.ArgumentParser(
        description="Unicorn Function Runner — compare decomp vs original function behavior")
    parser.add_argument("--symbol", help="Mangled C++ symbol name")
    parser.add_argument("--decomp-obj", help="Path to decomp .obj file")
    parser.add_argument("--orig-obj", help="Path to original .obj file")
    parser.add_argument("--unit", help="Unit name (resolves paths from objdiff.json)")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print detailed execution trace")
    parser.add_argument("--list-functions", action="store_true", help="List eligible functions in the unit")
    parser.add_argument("--batch", action="store_true",
                       help="Run comparison for all eligible functions in the unit")
    parser.add_argument("--batch-all", action="store_true",
                       help="Run batch comparison across all units in objdiff.json")
    parser.add_argument("--timeout", type=int, default=5_000_000,
                       help="Execution timeout in microseconds (default: 5000000)")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 8,
                       help="Number of parallel workers for --batch-all (default: cpu_count)")

    args = parser.parse_args()

    # Batch-all mode: iterate all units
    if args.batch_all:
        units = get_all_units()
        work = [(name, dp, op, args.timeout) for name, dp, op in units]

        print(f"Batch-all: {len(units)} units with both target and base paths")
        print(f"Workers: {args.jobs}\n")

        total_equiv = 0
        total_div = 0
        total_err = 0
        total_skip = 0
        units_tested = 0
        done = 0

        def _handle_result(result):
            nonlocal total_equiv, total_div, total_err, total_skip, units_tested, done
            name, eq, div, err, sk, tested = result
            done += 1
            if not tested:
                return
            total = eq + div + err + sk
            if total == 0:
                return
            units_tested += 1
            total_equiv += eq
            total_div += div
            total_err += err
            total_skip += sk
            print(f"  [{done}/{len(work)}] {name}: {eq}eq {div}div {err}err {sk}sk",
                  flush=True)

        if args.jobs == 1:
            for w in work:
                _handle_result(_process_unit(w))
        else:
            with ProcessPoolExecutor(max_workers=args.jobs) as pool:
                for result in pool.map(_process_unit, work):
                    _handle_result(result)

        total_funcs = total_equiv + total_div + total_err + total_skip
        print(f"\n=== BATCH-ALL SUMMARY ===")
        print(f"Units tested: {units_tested}")
        print(f"Functions: {total_funcs} total")
        print(f"  Equivalent: {total_equiv}")
        print(f"  Divergent:  {total_div}")
        print(f"  Errors:     {total_err}")
        print(f"  Skipped:    {total_skip}")

        if total_div > 0:
            return EXIT_DIVERGENT
        if total_err > 0:
            return EXIT_ERROR
        return EXIT_EQUIVALENT

    # Resolve paths
    if args.unit:
        try:
            decomp_path, orig_path = resolve_unit(args.unit)
        except ValueError as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return EXIT_ERROR
    elif args.decomp_obj and args.orig_obj:
        decomp_path = args.decomp_obj
        orig_path = args.orig_obj
    else:
        parser.error("Must provide either --unit or both --decomp-obj and --orig-obj")
        return EXIT_ERROR

    # Check files exist
    if not os.path.exists(decomp_path):
        print(f"ERROR: Decomp .obj not found: {decomp_path}", file=sys.stderr)
        return EXIT_ERROR
    if not os.path.exists(orig_path):
        print(f"ERROR: Original .obj not found: {orig_path}", file=sys.stderr)
        return EXIT_ERROR

    # List functions mode
    if args.list_functions:
        list_functions(decomp_path, orig_path)
        return EXIT_EQUIVALENT

    # Batch mode: all eligible functions in the unit
    if args.batch:
        print(f"Batch: {decomp_path}")
        eq, div, err, sk = run_batch(
            decomp_path, orig_path,
            verbose=args.verbose, timeout=args.timeout)
        total = eq + div + err + sk
        print(f"\nSummary: {eq} equivalent, {div} divergent, {err} errors, {sk} skipped ({total} total)")
        if div > 0:
            return EXIT_DIVERGENT
        if err > 0:
            return EXIT_ERROR
        return EXIT_EQUIVALENT

    # Single function comparison
    if not args.symbol:
        parser.error("--symbol is required (unless using --list-functions, --batch, or --batch-all)")
        return EXIT_ERROR

    return run_comparison(
        args.symbol, decomp_path, orig_path,
        verbose=args.verbose, timeout=args.timeout)


if __name__ == "__main__":
    sys.exit(main())
