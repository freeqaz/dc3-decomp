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

from .builder import prepare_side, prepare_coloaded_side
from .coff import COFFParser
from .coloader import collect_intra_tu_callees, build_coload_layout
from .extractor import (
    extract_from_decomp, extract_from_original,
    classify_indirect_branch,
)
from .memory_map import CODE_BASE, FILL_BYTE
from .engine import execute_function, UnicornEngine
from .comparator import compare, format_result, format_json_result

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


def run_comparison_inner(symbol, decomp_coff, orig_coff, verbose=False, timeout=5_000_000,
                         json_output=False, coload=True, coload_depth=None,
                         fill_pattern=None, engine=None):
    """Core comparison logic operating on pre-parsed COFF instances.

    Returns (exit_code, output_text) without printing anything.
    If json_output=True, output_text is a JSON string.
    If engine is provided, uses it for execution (avoids Uc() init/teardown).
    """
    # 1. Extract function bytes and relocations
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

    # 2. Classify indirect branches
    d_class = classify_indirect_branch(decomp_bytes, decomp_relocs, decomp_coff)
    o_class = classify_indirect_branch(orig_bytes, orig_relocs, orig_coff)

    # 3. Co-load discovery
    layout = None
    coloaded_count = 0

    if coload:
        d_callees = collect_intra_tu_callees(
            decomp_coff, symbol, extract_from_decomp, max_depth=coload_depth)
        o_callees = collect_intra_tu_callees(
            orig_coff, symbol, extract_from_original, max_depth=coload_depth)

        common = set(d_callees.keys()) & set(o_callees.keys())
        if common:
            layout = build_coload_layout(
                symbol, decomp_bytes, common, d_callees, o_callees,
                decomp_coff, orig_coff)

    # 4. Prepare both sides
    if layout:
        coloaded_count = len(layout.coloaded_symbols)
        intra_tu_addrs = {sym: CODE_BASE + off
                          for sym, off in layout.symbol_offsets.items()}

        if verbose:
            lines.append(f"  Co-loaded callees: {coloaded_count} ({layout.total_size}B combined)")
            for csym in layout.coloaded_symbols:
                lines.append(f"    {csym} @ offset 0x{layout.symbol_offsets[csym]:X}")

        try:
            decomp_side = prepare_coloaded_side(
                decomp_bytes, decomp_relocs, decomp_coff, symbol, d_class,
                d_callees, layout, intra_tu_addrs)
            orig_side = prepare_coloaded_side(
                orig_bytes, orig_relocs, orig_coff, symbol, o_class,
                o_callees, layout, intra_tu_addrs)
        except Exception as e:
            return EXIT_ERROR, f"ERROR: Co-load patching failed: {e}"
    else:
        try:
            decomp_side = prepare_side(
                decomp_bytes, decomp_relocs, decomp_coff, symbol, d_class)
            orig_side = prepare_side(
                orig_bytes, orig_relocs, orig_coff, symbol, o_class)
        except Exception as e:
            return EXIT_ERROR, f"ERROR: Patching failed: {e}"

    if verbose:
        lines.append(f"  Decomp trampolines: {len(decomp_side.trampolines)}")
        lines.append(f"  Original trampolines: {len(orig_side.trampolines)}")

    # 5. Execute both sides
    _exec = engine.execute if engine else execute_function
    try:
        decomp_result = _exec(
            decomp_side.code, decomp_side.trampolines, decomp_side.func_size,
            timeout=timeout, verbose=verbose, rdata_bytes=decomp_side.rdata_bytes,
            fill_pattern=fill_pattern)
        orig_result = _exec(
            orig_side.code, orig_side.trampolines, orig_side.func_size,
            timeout=timeout, verbose=verbose, rdata_bytes=orig_side.rdata_bytes,
            fill_pattern=fill_pattern)
    except Exception as e:
        return EXIT_ERROR, f"ERROR: Execution failed: {e}"

    # 6. Compare
    result = compare(decomp_result, orig_result, decomp_relocs, orig_relocs)

    # 7. Format output
    if json_output:
        metadata = {
            "symbol": symbol,
            "decomp_size": len(decomp_bytes),
            "orig_size": len(orig_bytes),
            "coloaded_callees": coloaded_count,
            "combined_code_size": (decomp_side.func_size
                                   if layout
                                   else max(len(decomp_side.code), len(orig_side.code))),
        }
        output = format_json_result(
            result, decomp_result, orig_result, orig_relocs, metadata)
        exit_code = EXIT_EQUIVALENT if result.verdict == "EQUIVALENT" else EXIT_DIVERGENT
        return exit_code, output

    output = format_result(
        result, decomp_result, orig_result,
        decomp_relocs, orig_relocs, verbose=verbose)
    if coloaded_count > 0 and verbose:
        lines.append(f"  Co-loaded: {coloaded_count} callees, {decomp_side.func_size}B combined code")
    if lines:
        output = "\n".join(lines) + "\n" + output

    # 8. Return
    if result.verdict == "EQUIVALENT":
        return EXIT_EQUIVALENT, output
    else:
        return EXIT_DIVERGENT, output


def run_dual_comparison_inner(symbol, decomp_coff, orig_coff, verbose=False,
                               timeout=5_000_000, json_output=False,
                               coload=True, coload_depth=None, engine=None):
    """Run comparison twice (zero + 0xCD fill), combine verdicts with confidence."""
    # Run 1: zero fill (baseline)
    code_zero, output_zero = run_comparison_inner(
        symbol, decomp_coff, orig_coff, verbose=verbose, timeout=timeout,
        json_output=json_output, coload=coload, coload_depth=coload_depth,
        fill_pattern=None, engine=engine)

    # Skip second run for non-comparable results
    if code_zero in (EXIT_ERROR, EXIT_SKIPPED):
        return code_zero, output_zero

    # Run 2: 0xCD fill
    code_cd, _ = run_comparison_inner(
        symbol, decomp_coff, orig_coff, verbose=False, timeout=timeout,
        json_output=False, coload=coload, coload_depth=coload_depth,
        fill_pattern=FILL_BYTE, engine=engine)

    # Combine: both agree → high confidence; disagree → fixture_sensitive
    if code_zero == code_cd:
        confidence = "high"
    else:
        confidence = "fixture_sensitive"

    # Annotate the zero-fill output (always the primary result)
    if json_output:
        data = json.loads(output_zero)
        data["confidence"] = confidence
        data["fixture_mode"] = "dual"
        return code_zero, json.dumps(data)
    else:
        tag = f"[confidence={confidence}] "
        return code_zero, tag + output_zero


def run_comparison(symbol, decomp_path, orig_path, verbose=False, timeout=5_000_000,
                   decomp_coff=None, orig_coff=None, json_output=False,
                   coload=True, coload_depth=None,
                   fill_pattern=None, dual_fixture=False):
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
            if json_output:
                print(json.dumps({"error": str(e)}))
            else:
                print(f"ERROR: Failed to parse .obj files: {e}", file=sys.stderr)
            return EXIT_ERROR

    if dual_fixture:
        code, output = run_dual_comparison_inner(
            symbol, decomp_coff, orig_coff, verbose=verbose, timeout=timeout,
            json_output=json_output, coload=coload, coload_depth=coload_depth)
    else:
        code, output = run_comparison_inner(
            symbol, decomp_coff, orig_coff, verbose=verbose, timeout=timeout,
            json_output=json_output, coload=coload, coload_depth=coload_depth,
            fill_pattern=fill_pattern)
    if code == EXIT_SKIPPED:
        if json_output:
            print(json.dumps({"verdict": "SKIPPED", "message": output}))
        else:
            print(output, file=sys.stderr)
    else:
        print(output)
    return code


def _find_common_text_symbols(decomp_coff, orig_coff):
    """Find symbol names present in .text sections of both COFFs.

    Lightweight alternative to list_functions() — no extraction or classification.
    """
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

    return sorted(decomp_syms & orig_syms)


def run_batch(decomp_path, orig_path, verbose=False, timeout=5_000_000, quiet=False,
              coload=True, coload_depth=None, fill_pattern=None, dual_fixture=False,
              cache=None):
    """Run comparison for all eligible functions in a unit.

    Parses COFF files once and reuses a single Unicorn engine for all functions.
    If quiet=True, suppresses per-function output (for multiprocessing).

    Returns (equivalent, divergent, errors, skipped, cached_count) counts.
    """
    decomp_coff = COFFParser(decomp_path)
    orig_coff = COFFParser(orig_path)

    # Find common symbols directly (avoids redundant extraction in list_functions)
    common = _find_common_text_symbols(decomp_coff, orig_coff)

    equivalent = 0
    divergent = 0
    errors = 0
    skipped = 0
    cached_count = 0

    for sym_name in common:
        # Check cache first
        if cache is not None:
            cached = cache.lookup(sym_name, decomp_path, orig_path)
            if cached is not None:
                code = cached[0]
                cached_count += 1
                if code == EXIT_EQUIVALENT:
                    equivalent += 1
                elif code == EXIT_DIVERGENT:
                    divergent += 1
                elif code == EXIT_SKIPPED:
                    skipped += 1
                else:
                    errors += 1
                if not quiet:
                    status = {EXIT_EQUIVALENT: "EQUIVALENT", EXIT_DIVERGENT: "DIVERGENT",
                              EXIT_SKIPPED: "SKIPPED"}.get(code, "ERROR")
                    print(f"  {status:11s}  {sym_name}  (cached)")
                continue

        if dual_fixture:
            code, _output = run_dual_comparison_inner(
                sym_name, decomp_coff, orig_coff, verbose=False, timeout=timeout,
                coload=coload, coload_depth=coload_depth)
            # Extract confidence from output
            confidence = None
            if _output.startswith("[confidence="):
                tag_end = _output.index("] ")
                confidence = _output[len("[confidence="):tag_end]
        else:
            code, _output = run_comparison_inner(
                sym_name, decomp_coff, orig_coff, verbose=False, timeout=timeout,
                coload=coload, coload_depth=coload_depth, fill_pattern=fill_pattern)
            confidence = None

        # Store in cache
        if cache is not None and code not in (EXIT_ERROR, EXIT_SKIPPED):
            cache.store(sym_name, decomp_path, orig_path, code, confidence)

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

    return equivalent, divergent, errors, skipped, cached_count


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
    Returns (name, equivalent, divergent, errors, skipped, cached, tested).

    For multiprocessing safety, each worker loads a read-only cache snapshot.
    New results are NOT saved by workers — the main process handles saving
    via a follow-up single-threaded pass (or the next run picks them up).
    """
    name, decomp_path, orig_path, timeout, coload, coload_depth, fill_pattern, dual_fixture, cache_path = args
    if not os.path.exists(decomp_path) or not os.path.exists(orig_path):
        return (name, 0, 0, 0, 0, 0, False)
    # Each worker loads a read-only cache snapshot for lookups
    cache = None
    if cache_path:
        from .cache import ResultCache
        cache = ResultCache(cache_path)
    try:
        eq, div, err, sk, cached = run_batch(decomp_path, orig_path,
                                              timeout=timeout, quiet=True,
                                              coload=coload, coload_depth=coload_depth,
                                              fill_pattern=fill_pattern,
                                              dual_fixture=dual_fixture,
                                              cache=cache)
        # Don't save from workers — race condition with other workers.
        # Cache still provides lookup hits from previous runs.
        return (name, eq, div, err, sk, cached, True)
    except Exception as e:
        return (name, 0, 0, 1, 0, 0, True)


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
    parser.add_argument("--json", action="store_true",
                       help="Output structured JSON instead of human-readable text")
    parser.add_argument("--no-coload", action="store_true",
                       help="Disable intra-TU callee co-loading")
    parser.add_argument("--coload-depth", type=int, default=None,
                       help="Limit callee co-loading recursion depth (default: unlimited)")
    parser.add_argument("--fill-pattern", type=lambda x: int(x, 0), default=None,
                       help="Fill memory with byte pattern instead of zeros (e.g., 0xCD)")
    parser.add_argument("--dual-fixture", action="store_true",
                       help="Run twice (zero + 0xCD fill) for confidence scoring")
    parser.add_argument("--no-cache", action="store_true",
                       help="Disable result caching for batch modes")

    args = parser.parse_args()

    coload = not args.no_coload
    coload_depth = args.coload_depth

    # Batch-all mode: iterate all units
    if args.batch_all:
        # Set up cache
        use_cache = not args.no_cache
        cache_path = None
        if use_cache:
            from .cache import ResultCache, DEFAULT_CACHE_PATH
            cache_path = DEFAULT_CACHE_PATH

        units = get_all_units()
        work = [(name, dp, op, args.timeout, coload, coload_depth,
                 args.fill_pattern, args.dual_fixture,
                 cache_path if use_cache else None) for name, dp, op in units]

        cache_label = "enabled" if use_cache else "disabled"
        print(f"Batch-all: {len(units)} units with both target and base paths")
        print(f"Workers: {args.jobs}, Cache: {cache_label}\n")

        total_equiv = 0
        total_div = 0
        total_err = 0
        total_skip = 0
        total_cached = 0
        units_tested = 0
        done = 0

        def _handle_result(result):
            nonlocal total_equiv, total_div, total_err, total_skip, total_cached, units_tested, done
            name, eq, div, err, sk, cached, tested = result
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
            total_cached += cached
            cache_note = f" ({cached} cached)" if cached > 0 else ""
            print(f"  [{done}/{len(work)}] {name}: {eq}eq {div}div {err}err {sk}sk{cache_note}",
                  flush=True)

        if args.jobs == 1:
            # Single-threaded: use a shared cache that saves at the end
            main_cache = None
            if use_cache:
                main_cache = ResultCache(cache_path)
            for name, dp, op, *rest in work:
                if not os.path.exists(dp) or not os.path.exists(op):
                    _handle_result((name, 0, 0, 0, 0, 0, False))
                    continue
                try:
                    eq, div, err, sk, cached = run_batch(
                        dp, op, timeout=args.timeout, quiet=True,
                        coload=coload, coload_depth=coload_depth,
                        fill_pattern=args.fill_pattern,
                        dual_fixture=args.dual_fixture,
                        cache=main_cache)
                    _handle_result((name, eq, div, err, sk, cached, True))
                except Exception:
                    _handle_result((name, 0, 0, 1, 0, 0, True))
            if main_cache is not None:
                main_cache.save()
        else:
            with ProcessPoolExecutor(max_workers=args.jobs) as pool:
                for result in pool.map(_process_unit, work):
                    _handle_result(result)

        total_funcs = total_equiv + total_div + total_err + total_skip
        total_fresh = total_funcs - total_cached
        print(f"\n=== BATCH-ALL SUMMARY ===")
        print(f"Units tested: {units_tested}")
        print(f"Functions: {total_funcs} total ({total_fresh} fresh, {total_cached} cached)")
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
        cache = None
        if not args.no_cache:
            from .cache import ResultCache
            cache = ResultCache()
        print(f"Batch: {decomp_path}")
        eq, div, err, sk, cached = run_batch(
            decomp_path, orig_path,
            verbose=args.verbose, timeout=args.timeout,
            coload=coload, coload_depth=coload_depth,
            fill_pattern=args.fill_pattern, dual_fixture=args.dual_fixture,
            cache=cache)
        if cache is not None:
            cache.save()
        total = eq + div + err + sk
        fresh = total - cached
        cache_note = f", {fresh} fresh, {cached} cached" if cached > 0 else ""
        print(f"\nSummary: {eq} equivalent, {div} divergent, {err} errors, {sk} skipped ({total} total{cache_note})")
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
        verbose=args.verbose, timeout=args.timeout,
        json_output=args.json,
        coload=coload, coload_depth=coload_depth,
        fill_pattern=args.fill_pattern, dual_fixture=args.dual_fixture)


if __name__ == "__main__":
    sys.exit(main())
