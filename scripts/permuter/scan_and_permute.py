"""Scan + permute pipeline — AST scan to find pattern hits, then hill-climb only matching patterns.

Combines pattern_scan (fast, no build) with hill_climber (build + score) for
targeted permutation. Instead of running all 46 patterns on every function,
this only runs the patterns that the scan identified as relevant.

Usage:
    # Scan + permute a specific pattern on incomplete functions
    python -m scripts.permuter.scan_and_permute \
        --patterns null_guard_elimination --max-pct 99

    # Multiple patterns, specific unit
    python -m scripts.permuter.scan_and_permute \
        --patterns null_guard_elimination,reference_elimination \
        --unit "system/obj/*" --max-rounds 3

    # Dry run — scan only, show what would be permuted
    python -m scripts.permuter.scan_and_permute \
        --patterns null_guard_elimination --dry-run

    # Limit to N functions, no source apply (preview mode)
    python -m scripts.permuter.scan_and_permute \
        --patterns reference_elimination --limit 5 --no-apply

    # Parallel execution — 4 source files at once
    python -m scripts.permuter.scan_and_permute \
        --patterns null_guard_elimination --max-pct 99 --jobs 4
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import asdict
from datetime import datetime
from pathlib import Path

import importlib

from .hill_climber import hill_climb, install_signal_handler
from .pattern_scan import _load_source_files, _load_match_info, _scan_file, ScanHit
from .patterns import get_pattern, list_patterns, get_all_patterns
from .types import extract_qualified_name

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DECOMP_DB = REPO_ROOT / "decomp.db"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.scan_and_permute",
        description="Scan for pattern hits then hill-climb only matching patterns.",
    )
    parser.add_argument(
        "--patterns",
        help="Comma-separated pattern names to scan for (omit to list available patterns)",
    )
    parser.add_argument(
        "--unit",
        help="Unit glob pattern (e.g. 'system/obj/*')",
    )
    parser.add_argument(
        "--max-pct", type=float, default=99.99,
        help="Only process functions below this match %% (default: 99.99)",
    )
    parser.add_argument(
        "--min-pct", type=float, default=0.0,
        help="Only process functions above this match %% (default: 0)",
    )
    parser.add_argument(
        "--max-rounds", type=int, default=5,
        help="Max hill-climbing rounds per function (default: 5)",
    )
    parser.add_argument(
        "--max-variants", type=int, default=50,
        help="Max variants per round (default: 50)",
    )
    parser.add_argument(
        "--plateau-limit", type=int, default=2,
        help="Stop after N rounds without improvement (default: 2)",
    )
    parser.add_argument(
        "--compose", action="store_true", default=True,
        help="Enable pattern composition (default: True)",
    )
    parser.add_argument(
        "--no-compose", action="store_false", dest="compose",
    )
    parser.add_argument(
        "--no-apply", action="store_true",
        help="Do not apply improvements to source (dry run scoring)",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Scan only — show hits without running hill_climber",
    )
    parser.add_argument(
        "--limit", type=int, default=0,
        help="Max functions to process (0 = unlimited)",
    )
    parser.add_argument(
        "--jobs", "-j", type=int, default=1,
        help="Parallel jobs for different source files (default: 1). "
             "Functions in the same file run sequentially.",
    )
    parser.add_argument(
        "--scan", action="store_true",
        help="When listing patterns (no --patterns), scan codebase and show hit counts (~30s)",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output results as JSON",
    )
    parser.add_argument(
        "--ghidra", action="store_true",
        help="Enable Ghidra-guided patterns (lookup decomp.db cache)",
    )
    parser.add_argument(
        "--constrained", action="store_true",
        help="Enable constraint-directed synthesis pre-pass (implies --ghidra)",
    )
    parser.add_argument(
        "--chain", action="store_true",
        help="Enable N-stage pattern chains via beam search (implies --compose)",
    )
    parser.add_argument(
        "--chain-depth", type=int, default=3,
        help="Maximum chain depth for N-stage composition (default: 3)",
    )
    parser.add_argument(
        "--adaptive", action="store_true",
        help="Enable adaptive per-round pattern suppression/boosting",
    )
    return parser.parse_args()


def _resolve_symbols(
    hits: list[ScanHit],
    min_pct: float = 0.0,
    max_pct: float = 99.99,
) -> list[dict]:
    """Resolve scan hits to mangled symbols via decomp.db.

    Filters by current_percent from the database (authoritative source).
    Returns list of dicts with: symbol, function_name, source_path, unit,
    match_percent, patterns (list of pattern names that matched).
    """
    if not DECOMP_DB.exists():
        print("Error: decomp.db not found", file=sys.stderr)
        return []

    conn = sqlite3.connect(str(DECOMP_DB))
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        "SELECT symbol, demangled, unit, current_percent "
        "FROM functions WHERE current_percent IS NOT NULL"
    ).fetchall()
    conn.close()

    # Build qualified_name -> (symbol, unit, pct) mapping
    name_to_info: dict[str, tuple[str, str, float]] = {}
    for row in rows:
        qname = extract_qualified_name(row["demangled"] or "")
        if qname:
            pct = row["current_percent"]
            if pct < min_pct or pct >= max_pct:
                continue
            name_to_info[qname] = (row["symbol"], row["unit"], pct)

    # Group hits by function, collecting all matching patterns
    by_func: dict[str, dict] = {}
    for hit in hits:
        key = f"{hit.source_path}::{hit.function_name}"
        if key not in by_func:
            info = name_to_info.get(hit.function_name)
            if info is None:
                continue  # Can't resolve to a symbol
            by_func[key] = {
                "symbol": info[0],
                "function_name": hit.function_name,
                "source_path": hit.source_path,
                "unit": info[1],
                "match_percent": info[2],
                "patterns": [],
            }
        by_func[key]["patterns"].append(hit.pattern_name)

    # Deduplicate pattern lists
    for entry in by_func.values():
        entry["patterns"] = sorted(set(entry["patterns"]))

    return list(by_func.values())


def _get_pattern_description(pattern) -> str:
    """Get the first line of a pattern's module docstring."""
    mod = importlib.import_module(type(pattern).__module__)
    doc = (mod.__doc__ or "").strip()
    first_line = doc.split("\n")[0] if doc else ""
    # Strip the "Name — " prefix if present (e.g. "Null guard elimination — remove...")
    if " — " in first_line:
        first_line = first_line.split(" — ", 1)[1]
    # Capitalize first letter
    if first_line:
        first_line = first_line[0].upper() + first_line[1:]
    return first_line


def _scan_all_counts(unit_glob: str | None = None) -> dict[str, int]:
    """Run a quick AST scan of all patterns and return hit counts per pattern.

    Uses SQLite cache keyed by file content hash — only re-scans changed files.
    """
    from .scan_cache import _get_conn, hash_file, get_cached, store_hits_batch

    patterns = get_all_patterns()
    pattern_names = [p.name for p in patterns]
    files = _load_source_files(unit_glob)
    match_info = _load_match_info()

    conn = _get_conn()
    counts: dict[str, int] = defaultdict(int)
    cache_hits = 0
    cache_misses = 0

    # Batch pending writes for one commit at the end
    pending_stores: list[tuple[str, str, list[tuple[str, int]]]] = []

    for unit_name, source_path in files:
        path = Path(source_path)
        fhash = hash_file(path)
        if fhash is None:
            continue

        # Check cache for ALL patterns at once
        all_cached = True
        cached_counts: dict[str, int] = {}
        for pname in pattern_names:
            cached = get_cached(conn, fhash, pname)
            if cached is None:
                all_cached = False
                break
            # Count functions with hits (not total variant count)
            cached_counts[pname] = sum(1 for _, vc in cached if vc > 0)

        if all_cached:
            for pname, count in cached_counts.items():
                counts[pname] += count
            cache_hits += 1
            continue

        # Cache miss — scan the file
        cache_misses += 1
        hits = _scan_file(
            path, patterns, unit_name,
            match_info, show_variants=False,
        )

        # Group hits by pattern for caching
        # Each ScanHit is one function×pattern — count unique functions per pattern
        by_pattern: dict[str, list[tuple[str, int]]] = defaultdict(list)
        seen: set[tuple[str, str]] = set()
        for hit in hits:
            by_pattern[hit.pattern_name].append(
                (hit.function_name, hit.variant_count)
            )
            key = (hit.pattern_name, hit.function_name)
            if key not in seen:
                seen.add(key)
                counts[hit.pattern_name] += 1

        # Queue for batch write
        for pname in pattern_names:
            pending_stores.append((fhash, pname, by_pattern.get(pname, [])))

    # Single transaction for all cache writes
    if pending_stores:
        store_hits_batch(conn, pending_stores)

    conn.close()

    if cache_hits > 0 or cache_misses > 0:
        print(
            f"  Cache: {cache_hits} hit, {cache_misses} miss "
            f"({len(files)} files)",
            file=sys.stderr,
        )

    return dict(counts)


def _print_pattern_table(counts: dict[str, int] | None = None):
    """Print a formatted table of all available patterns.

    Args:
        counts: Optional dict of pattern_name -> hit count from a scan.
    """
    patterns = sorted(get_all_patterns(), key=lambda p: p.name)
    has_counts = counts is not None

    # Gather data
    rows = []
    for p in patterns:
        desc = _get_pattern_description(p)
        rows.append((p.name, desc, counts.get(p.name, 0) if has_counts else 0))

    # Sort by hit count descending when counts are available
    if has_counts:
        rows.sort(key=lambda r: (-r[2], r[0]))

    # Column widths
    name_w = max(len(r[0]) for r in rows)
    desc_w = max(len(r[1]) for r in rows)

    if has_counts:
        hits_w = max(len(str(r[2])) for r in rows)
        hits_w = max(hits_w, 4)  # "Hits" header

        # Header
        total_w = name_w + 3 + hits_w + 3 + desc_w
        print(f"\n{'=' * (total_w + 4)}")
        print(f"  AVAILABLE PATTERNS ({len(rows)}) — "
              f"{sum(r[2] for r in rows):,} total hits across codebase")
        print(f"{'=' * (total_w + 4)}")
        print(f"  {'Pattern':<{name_w}} | {'Hits':>{hits_w}} | Description")
        print(f"  {'─' * name_w}─┼─{'─' * hits_w}─┼─{'─' * desc_w}")

        for name, desc, count in rows:
            count_str = f"{count:>{hits_w},}" if count > 0 else f"{'—':>{hits_w}}"
            print(f"  {name:<{name_w}} | {count_str} | {desc}")

        print(f"  {'─' * name_w}─┴─{'─' * hits_w}─┴─{'─' * desc_w}")
    else:
        total_w = name_w + 3 + desc_w
        print(f"\n{'=' * (total_w + 4)}")
        print(f"  AVAILABLE PATTERNS ({len(rows)})")
        print(f"{'=' * (total_w + 4)}")
        print(f"  {'Pattern':<{name_w}} | Description")
        print(f"  {'─' * name_w}─┼─{'─' * desc_w}")

        for name, desc, _ in rows:
            print(f"  {name:<{name_w}} | {desc}")

        print(f"  {'─' * name_w}─┴─{'─' * desc_w}")

    print(f"\nUsage: python -m scripts.permuter.scan_and_permute "
          f"--patterns <name>[,<name>,...] [options]")
    print(f"       python -m scripts.permuter.scan_and_permute "
          f"--patterns all [options]")
    if not has_counts:
        print(f"\nTip: add --scan to show hit counts per pattern (~30s)")


def _climb_one(
    candidate: dict,
    patterns_map: dict,
    args: argparse.Namespace,
    total: int,
    index: int,
) -> dict:
    """Run hill_climber on one candidate. Returns result dict."""
    from .hill_climber import _interrupted

    symbol = candidate["symbol"]
    source_path = candidate["source_path"]
    func_name = candidate["function_name"]
    pct = candidate["match_percent"]
    func_patterns = [patterns_map[p] for p in candidate["patterns"]
                     if p in patterns_map]

    if _interrupted:
        return {
            "function": func_name, "symbol": symbol, "source": source_path,
            "initial": 0, "final": 0, "delta": 0,
            "patterns": candidate["patterns"], "error": "interrupted",
        }

    print(
        f"\n[{index+1}/{total}] {func_name} ({pct:.1f}%) "
        f"— {len(func_patterns)} pattern(s): {', '.join(candidate['patterns'])}",
        file=sys.stderr,
    )

    try:
        result = hill_climb(
            symbol=symbol,
            source_path=Path(REPO_ROOT / source_path),
            function_name=func_name,
            patterns=func_patterns,
            max_rounds=args.max_rounds,
            max_variants=args.max_variants,
            plateau_limit=args.plateau_limit,
            compose=args.compose,
            apply=not args.no_apply,
            ghidra=getattr(args, "ghidra", False),
            chain=getattr(args, "chain", False),
            chain_depth=getattr(args, "chain_depth", 3),
            adaptive=getattr(args, "adaptive", False),
        )
        delta = result.final_percent - result.initial_percent
        if delta > 0:
            print(
                f"  IMPROVED: {result.initial_percent:.2f}% -> "
                f"{result.final_percent:.2f}% (+{delta:.2f}%)",
                file=sys.stderr,
            )
        return {
            "function": func_name,
            "symbol": symbol,
            "source": source_path,
            "initial": result.initial_percent,
            "final": result.final_percent,
            "delta": delta,
            "patterns": candidate["patterns"],
            "error": None,
            "ghidra_stats": result.ghidra_stats,
        }
    except Exception as e:
        print(f"  ERROR: {e}", file=sys.stderr)
        return {
            "function": func_name,
            "symbol": symbol,
            "source": source_path,
            "initial": 0,
            "final": 0,
            "delta": 0,
            "patterns": candidate["patterns"],
            "error": str(e),
        }


def _climb_source_group(
    funcs: list[dict],
    pattern_names: list[str],
    args: argparse.Namespace,
) -> list[dict]:
    """Run hill_climber on all functions in one source file (sequentially).

    Runs in a subprocess worker — must re-import patterns since they
    can't be pickled across processes.
    """
    from .patterns import get_pattern
    patterns_map = {name: get_pattern(name) for name in pattern_names}

    results = []
    for candidate in funcs:
        result = _climb_one(candidate, patterns_map, args, len(funcs), len(results))
        results.append(result)
    return results


def _accumulate_result(stats: dict, result: dict):
    """Accumulate a single result into the stats dict."""
    if result["error"]:
        stats["errors"] += 1
        return

    stats["processed"] += 1
    if result["delta"] > 0:
        stats["improved"] += 1
        stats["total_delta"] += result["delta"]
        stats["improvements"].append(result)
    if result["final"] >= 100.0:
        stats["perfect"] += 1
    elif result["delta"] <= 0:
        stats["no_change"] += 1

    # Accumulate Ghidra batch stats
    ghidra_batch = stats.get("ghidra_batch")
    ghidra_run = result.get("ghidra_stats")
    if ghidra_batch and ghidra_run:
        ghidra_batch.accumulate(ghidra_run, result["delta"])


def main():
    args = parse_args()
    prev_handler = install_signal_handler()

    # No patterns specified — show table and exit
    if not args.patterns:
        counts = None
        if args.scan:
            print("Scanning codebase for all patterns...", file=sys.stderr)
            scan_start = time.time()
            counts = _scan_all_counts(args.unit)
            print(f"  Done in {time.time() - scan_start:.1f}s", file=sys.stderr)
        _print_pattern_table(counts)
        sys.exit(0)

    # Validate patterns
    available_default = list_patterns()
    available_all = list_patterns(include_opt_in=True)
    if args.patterns.strip() == "all":
        pattern_names = available_default
    else:
        pattern_names = [p.strip() for p in args.patterns.split(",")]

    patterns_map = {}
    for name in pattern_names:
        if name not in available_all:
            print(f"Error: unknown pattern '{name}'", file=sys.stderr)
            _print_pattern_table()
            sys.exit(1)
        patterns_map[name] = get_pattern(name)

    # Phase 1: AST scan
    print(f"Phase 1: Scanning for patterns: {', '.join(pattern_names)}", file=sys.stderr)
    scan_start = time.time()

    files = _load_source_files(args.unit)
    if not files:
        print("No source files found.", file=sys.stderr)
        sys.exit(0)

    match_info = _load_match_info()
    all_hits: list[ScanHit] = []

    for unit_name, source_path in files:
        hits = _scan_file(
            Path(source_path), list(patterns_map.values()), unit_name,
            match_info, show_variants=False,
        )
        for hit in hits:
            # Filter by match percentage
            if hit.match_percent is not None:
                if hit.match_percent >= args.max_pct:
                    continue
                if hit.match_percent < args.min_pct:
                    continue
            all_hits.append(hit)

    scan_elapsed = time.time() - scan_start
    print(
        f"  Found {len(all_hits)} hits in {len(files)} files ({scan_elapsed:.1f}s)",
        file=sys.stderr,
    )

    if not all_hits:
        print("No hits found.", file=sys.stderr)
        sys.exit(0)

    # Phase 2: Resolve to symbols
    print("Phase 2: Resolving symbols from decomp.db...", file=sys.stderr)
    candidates = _resolve_symbols(all_hits, min_pct=args.min_pct, max_pct=args.max_pct)
    print(f"  Resolved {len(candidates)} functions with symbols", file=sys.stderr)

    if not candidates:
        print("No resolvable candidates.", file=sys.stderr)
        sys.exit(0)

    # Sort by match% descending (closest to 100% first — most likely to succeed)
    candidates.sort(key=lambda c: c["match_percent"], reverse=True)

    if args.limit > 0:
        candidates = candidates[:args.limit]

    # Dry run — just print what would be processed
    if args.dry_run:
        print(f"\n{'=' * 70}", file=sys.stderr)
        print(f"SCAN RESULTS — {len(candidates)} functions to permute", file=sys.stderr)
        print(f"{'=' * 70}", file=sys.stderr)
        for i, c in enumerate(candidates):
            pats = ", ".join(c["patterns"])
            print(
                f"  [{i+1}] {c['function_name']} ({c['match_percent']:.1f}%) "
                f"— patterns: {pats}",
                file=sys.stderr,
            )
            print(f"      {c['source_path']}", file=sys.stderr)
        sys.exit(0)

    # Phase 3: Hill-climb each candidate with only the relevant patterns
    # Group by source file — same-file functions must run sequentially
    by_source: dict[str, list[dict]] = defaultdict(list)
    for c in candidates:
        by_source[c["source_path"]].append(c)

    print(
        f"\nPhase 3: Hill-climbing {len(candidates)} functions "
        f"across {len(by_source)} source files "
        f"({args.jobs} job{'s' if args.jobs != 1 else ''})...",
        file=sys.stderr,
    )
    climb_start = time.time()

    # Initialize Ghidra batch stats if enabled
    ghidra_batch = None
    if getattr(args, "ghidra", False):
        from .ghidra_stats import GhidraBatchStats
        ghidra_batch = GhidraBatchStats()

    stats = {
        "total": len(candidates),
        "processed": 0,
        "improved": 0,
        "perfect": 0,
        "no_change": 0,
        "errors": 0,
        "total_delta": 0.0,
        "improvements": [],
        "ghidra_batch": ghidra_batch,
    }

    if args.jobs <= 1:
        # Sequential execution
        for i, candidate in enumerate(candidates):
            from .hill_climber import _interrupted
            if _interrupted:
                print(f"\nSkipping remaining {len(candidates) - i} functions.",
                      file=sys.stderr)
                break
            result_dict = _climb_one(
                candidate, patterns_map, args, len(candidates), i,
            )
            _accumulate_result(stats, result_dict)
    else:
        # Parallel execution — different source files run concurrently,
        # functions within the same file run sequentially
        with ProcessPoolExecutor(max_workers=args.jobs) as executor:
            futures = {}
            for source_path, funcs in by_source.items():
                future = executor.submit(
                    _climb_source_group, funcs, pattern_names, args,
                )
                futures[future] = source_path

            for future in as_completed(futures):
                source_path = futures[future]
                try:
                    group_results = future.result()
                    for r in group_results:
                        _accumulate_result(stats, r)
                except KeyboardInterrupt:
                    print("\nInterrupted — cancelling remaining jobs...",
                          file=sys.stderr)
                    for f in futures:
                        f.cancel()
                    break
                except Exception as e:
                    stats["errors"] += 1
                    print(f"  ERROR in {source_path}: {e}", file=sys.stderr)

    total_elapsed = time.time() - scan_start

    # Restore previous signal handler
    import signal
    signal.signal(signal.SIGINT, prev_handler)

    # Summary
    from .hill_climber import _interrupted as was_interrupted
    print(f"\n{'=' * 70}", file=sys.stderr)
    label = "SCAN + PERMUTE INTERRUPTED" if was_interrupted else "SCAN + PERMUTE COMPLETE"
    print(label, file=sys.stderr)
    print(f"{'=' * 70}", file=sys.stderr)
    print(f"  Scanned: {len(files)} files in {scan_elapsed:.1f}s", file=sys.stderr)
    print(f"  Climbed: {stats['processed']}/{stats['total']} functions "
          f"in {time.time() - climb_start:.1f}s", file=sys.stderr)
    print(f"  Improved: {stats['improved']} functions (+{stats['total_delta']:.2f}% total)",
          file=sys.stderr)
    print(f"  Perfect: {stats['perfect']}", file=sys.stderr)
    print(f"  Errors: {stats['errors']}", file=sys.stderr)
    print(f"  Total time: {total_elapsed:.1f}s", file=sys.stderr)

    # Ghidra stats summary
    if ghidra_batch:
        for line in ghidra_batch.summary_lines():
            print(line, file=sys.stderr)

    if args.json_output:
        # Strip non-serializable fields before JSON output
        json_stats = {k: v for k, v in stats.items() if k != "ghidra_batch"}
        # Strip ghidra_stats from improvements (not serializable)
        for imp in json_stats.get("improvements", []):
            imp.pop("ghidra_stats", None)
        ghidra_json = {}
        if ghidra_batch and ghidra_batch.functions_with_ghidra > 0:
            ghidra_json = {
                "functions_total": ghidra_batch.functions_total,
                "functions_with_ghidra": ghidra_batch.functions_with_ghidra,
                "functions_with_ghidra_variants": ghidra_batch.functions_with_ghidra_variants,
                "functions_with_ghidra_wins": ghidra_batch.functions_with_ghidra_wins,
                "total_ghidra_variants": ghidra_batch.total_ghidra_variants,
                "total_variants": ghidra_batch.total_variants,
                "total_delta_ghidra": round(ghidra_batch.total_delta_ghidra, 4),
                "total_delta_other": round(ghidra_batch.total_delta_other, 4),
            }
        output = {
            "scan": {
                "patterns": pattern_names,
                "files_scanned": len(files),
                "hits": len(all_hits),
                "resolved": len(candidates),
            },
            "climb": json_stats,
            "ghidra": ghidra_json,
            "elapsed_seconds": round(total_elapsed, 2),
        }
        print(json.dumps(output, indent=2))

    if stats["improvements"]:
        print(f"\nImprovements:", file=sys.stderr)
        for imp in stats["improvements"]:
            ghidra_tag = ""
            gs = imp.get("ghidra_stats")
            if gs and gs.ghidra_winner:
                ghidra_tag = " [GHIDRA]"
            print(
                f"  {imp['function']}: {imp['initial']:.1f}% -> {imp['final']:.1f}% "
                f"(+{imp['delta']:.1f}%) via {', '.join(imp['patterns'])}{ghidra_tag}",
                file=sys.stderr,
            )


if __name__ == "__main__":
    main()
