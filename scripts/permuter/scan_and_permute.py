"""Scan + permute pipeline — AST scan to find pattern hits, then hill-climb only matching patterns.

Combines pattern_scan (fast, no build) with hill_climber (build + score) for
targeted permutation. Instead of running all 46 patterns on every function,
this only runs the patterns that the scan identified as relevant.

By default runs all patterns with Ghidra, chains, adaptive, and constrained
synthesis enabled. Use --no-* flags to disable individual features.

Usage:
    # Run with all defaults (all patterns, ghidra, chain, adaptive, constrained)
    python -m scripts.permuter.scan_and_permute

    # Specific patterns only
    python -m scripts.permuter.scan_and_permute \
        --patterns null_guard_elimination,reference_elimination

    # Specific unit, no Ghidra
    python -m scripts.permuter.scan_and_permute \
        --unit "system/obj/*" --no-ghidra

    # Dry run — scan only, show what would be permuted
    python -m scripts.permuter.scan_and_permute --dry-run

    # Parallel execution — 4 source files at once
    python -m scripts.permuter.scan_and_permute --jobs 4
"""

from __future__ import annotations

import argparse
import fnmatch
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
        "--patterns", default="all",
        help="Comma-separated pattern names to scan for, or 'all' (default: all). "
             "Omit value to list available patterns: --patterns \"\"",
    )
    parser.add_argument(
        "--unit",
        help="Unit glob pattern (e.g. 'system/obj/*')",
    )
    parser.add_argument(
        "--symbol",
        action="append",
        default=[],
        help="Target specific function symbol(s) (repeatable, mangled or qualified name). "
             "Examples: --symbol '?DrawBlacklight@RndText@@SAXXZ' "
             "--symbol 'RndText::DrawBlacklight'",
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
        "--ghidra", action="store_true", default=True,
        help="Enable Ghidra-guided patterns (default: True)",
    )
    parser.add_argument(
        "--no-ghidra", action="store_false", dest="ghidra",
        help="Disable Ghidra-guided patterns",
    )
    parser.add_argument(
        "--constrained", action="store_true", default=True,
        help="Enable constraint-directed synthesis pre-pass (default: True)",
    )
    parser.add_argument(
        "--no-constrained", action="store_false", dest="constrained",
        help="Disable constraint-directed synthesis",
    )
    parser.add_argument(
        "--chain", action="store_true", default=True,
        help="Enable N-stage pattern chains via beam search (default: True)",
    )
    parser.add_argument(
        "--no-chain", action="store_false", dest="chain",
        help="Disable N-stage pattern chains",
    )
    parser.add_argument(
        "--chain-depth", type=int, default=5,
        help="Maximum chain depth for N-stage composition (default: 5)",
    )
    parser.add_argument(
        "--adaptive", action="store_true", default=True,
        help="Enable adaptive per-round pattern suppression/boosting (default: True)",
    )
    parser.add_argument(
        "--no-adaptive", action="store_false", dest="adaptive",
        help="Disable adaptive pattern suppression/boosting",
    )
    parser.add_argument(
        "--evolutionary", action="store_true", default=False,
        help="Use evolutionary optimizer instead of greedy hill climbing",
    )
    parser.add_argument(
        "--population-size", type=int, default=50,
        help="Population size for evolutionary optimizer (default: 50)",
    )
    parser.add_argument(
        "--generations", type=int, default=20,
        help="Max generations for evolutionary optimizer (default: 20)",
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


def _normalize_unit(unit: str | None) -> str:
    if not unit:
        return ""
    if unit.startswith("default/"):
        return unit[len("default/"):]
    return unit


def _parse_symbol_tokens(raw_symbols: list[str]) -> list[str]:
    tokens: list[str] = []
    for raw in raw_symbols:
        if not raw:
            continue
        parts = [p.strip() for p in raw.split(",")]
        for p in parts:
            if p:
                tokens.append(p)
    return tokens


def _resolve_target_functions(
    symbol_tokens: list[str],
    min_pct: float = 0.0,
    max_pct: float = 99.99,
    unit_glob: str | None = None,
) -> list[dict]:
    """Resolve explicit symbol targets (mangled, qualified, or glob) from decomp.db.

    When symbols are explicitly requested, percentage filters are ignored
    so that the user always gets the function they asked for.
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

    resolved: list[dict] = []
    seen_symbols: set[str] = set()

    for row in rows:
        symbol = row["symbol"] or ""
        demangled = row["demangled"] or ""
        unit = row["unit"] or ""
        pct = row["current_percent"]

        norm_unit = _normalize_unit(unit)
        if unit_glob and not fnmatch.fnmatch(norm_unit, unit_glob):
            continue

        qname = extract_qualified_name(demangled)
        for token in symbol_tokens:
            is_match = (
                token == symbol or
                (qname and token == qname) or
                fnmatch.fnmatch(symbol, token) or
                (qname and fnmatch.fnmatch(qname, token))
            )
            if not is_match:
                continue
            if symbol in seen_symbols:
                continue
            seen_symbols.add(symbol)
            resolved.append({
                "symbol": symbol,
                "function_name": qname or demangled or symbol,
                "unit": unit,
                "match_percent": pct,
            })
            break

    return resolved


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
        if getattr(args, "evolutionary", False):
            from .evolutionary import evolve
            result = evolve(
                symbol=symbol,
                source_path=Path(REPO_ROOT / source_path),
                function_name=func_name,
                patterns=func_patterns,
                population_size=getattr(args, "population_size", 50),
                generations=getattr(args, "generations", 20),
                apply=not args.no_apply,
                unit=candidate.get("unit"),
                ghidra=args.ghidra,
                chain=args.chain,
                chain_depth=args.chain_depth,
                adaptive=args.adaptive,
                constrained=args.constrained,
            )
        else:
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
                ghidra=args.ghidra,
                chain=args.chain,
                chain_depth=args.chain_depth,
                adaptive=args.adaptive,
                constrained=args.constrained,
            )
        delta = result.final_percent - result.initial_percent
        if delta > 0:
            print(
                f"  IMPROVED: {result.initial_percent:.2f}% -> "
                f"{result.final_percent:.2f}% (+{delta:.2f}%)",
                file=sys.stderr,
            )
        preflight = None
        if result.ghidra_stats and result.ghidra_stats.preflight_flagged:
            preflight = {
                "reason": result.ghidra_stats.preflight_reason,
                "confidence": round(result.ghidra_stats.preflight_confidence, 4),
                "struct_offsets": result.ghidra_stats.preflight_struct_offsets,
                "extra_calls": result.ghidra_stats.preflight_extra_calls,
                "missing_calls": result.ghidra_stats.preflight_missing_calls,
                "dead_vars": result.ghidra_stats.preflight_dead_vars,
                "prologue_mismatch": result.ghidra_stats.preflight_prologue_mismatch,
                "volatile_only": result.ghidra_stats.preflight_volatile_only,
                "hard_skip": result.ghidra_stats.preflight_hard_skip,
            }
        # Extract winning rounds — what actually caused each improvement step
        winning_rounds = []
        for r in result.rounds:
            if r.improved and r.best_pattern:
                winning_rounds.append({
                    "round": r.round_num,
                    "pattern": r.best_pattern,
                    "variant": r.best_name,
                    "baseline": r.baseline,
                    "score": r.best_score,
                    "delta": r.delta,
                })
        return {
            "function": func_name,
            "symbol": symbol,
            "source": source_path,
            "unit": candidate.get("unit", ""),
            "initial": result.initial_percent,
            "final": result.final_percent,
            "delta": delta,
            "patterns": candidate["patterns"],
            "winning_rounds": winning_rounds,
            "stopped_reason": result.stopped_reason,
            "elapsed": result.elapsed_seconds,
            "error": None,
            "ghidra_stats": result.ghidra_stats,
            "preflight": preflight,
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
            "preflight": None,
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
        # Check Ghidra circuit breaker between functions
        if args.ghidra:
            from .ghidra_cache import ghidra_circuit_tripped
            if ghidra_circuit_tripped():
                break
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

        # Track which patterns actually won rounds (for leaderboard)
        for wr in result.get("winning_rounds", []):
            pname = wr["pattern"]
            pw = stats["pattern_wins"]
            if pname not in pw:
                pw[pname] = {"wins": 0, "delta": 0.0, "perfects": 0}
            pw[pname]["wins"] += 1
            pw[pname]["delta"] += wr["delta"]
            if wr["score"] >= 100.0:
                pw[pname]["perfects"] += 1

    if result["final"] >= 100.0:
        stats["perfect"] += 1
    elif result["delta"] <= 0:
        stats["no_change"] += 1

    # Accumulate Ghidra batch stats
    ghidra_batch = stats.get("ghidra_batch")
    ghidra_run = result.get("ghidra_stats")
    if ghidra_batch and ghidra_run:
        ghidra_batch.accumulate(ghidra_run, result["delta"])


_IMPROVEMENT_SCHEMA = """
CREATE TABLE IF NOT EXISTS improvement_runs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       REAL NOT NULL,
    symbol          TEXT NOT NULL,
    function_name   TEXT,
    source_path     TEXT,
    unit            TEXT,
    initial_pct     REAL NOT NULL,
    final_pct       REAL NOT NULL,
    delta           REAL NOT NULL,
    rounds_used     INTEGER NOT NULL,
    stopped_reason  TEXT,
    elapsed_seconds REAL,
    winning_rounds  TEXT,
    caller          TEXT NOT NULL DEFAULT 'scan_and_permute'
);

CREATE INDEX IF NOT EXISTS idx_improvement_runs_symbol
ON improvement_runs (symbol);

CREATE INDEX IF NOT EXISTS idx_improvement_runs_delta
ON improvement_runs (delta DESC);
"""

_IMPROVEMENT_DB = REPO_ROOT / "permuter_cache.db"


def _store_improvement_runs(improvements: list[dict]) -> None:
    """Persist improvement details to permuter_cache.db."""
    import sqlite3

    conn = sqlite3.connect(str(_IMPROVEMENT_DB))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.executescript(_IMPROVEMENT_SCHEMA)

    now = time.time()
    rows = []
    for imp in improvements:
        winning_rounds = imp.get("winning_rounds", [])
        # Strip non-serializable fields for JSON
        clean_rounds = [
            {k: v for k, v in wr.items()} for wr in winning_rounds
        ]
        rows.append((
            now,
            imp["symbol"],
            imp["function"],
            imp["source"],
            imp.get("unit", ""),
            imp["initial"],
            imp["final"],
            imp["delta"],
            len(winning_rounds),
            imp.get("stopped_reason", ""),
            imp.get("elapsed", 0),
            json.dumps(clean_rounds),
            "scan_and_permute",
        ))

    conn.executemany(
        "INSERT INTO improvement_runs "
        "(timestamp, symbol, function_name, source_path, unit, "
        "initial_pct, final_pct, delta, rounds_used, stopped_reason, "
        "elapsed_seconds, winning_rounds, caller) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        rows,
    )
    conn.commit()
    conn.close()


def main():
    args = parse_args()
    prev_handler = install_signal_handler()

    # Empty patterns string — show table and exit
    if args.patterns is not None and args.patterns.strip() == "":
        counts = None
        if args.scan:
            print("Scanning codebase for all patterns...", file=sys.stderr)
            scan_start = time.time()
            counts = _scan_all_counts(args.unit)
            print(f"  Done in {time.time() - scan_start:.1f}s", file=sys.stderr)
        _print_pattern_table(counts)
        sys.exit(0)

    # Validate patterns
    default_available = list_patterns()
    all_available = list_patterns(include_opt_in=True)
    if args.patterns.strip() == "all":
        # Keep historical behavior: `all` excludes opt-in patterns.
        pattern_names = default_available
    else:
        pattern_names = [p.strip() for p in args.patterns.split(",")]

    patterns_map = {}
    for name in pattern_names:
        if name not in all_available:
            print(f"Error: unknown pattern '{name}'", file=sys.stderr)
            _print_pattern_table()
            sys.exit(1)
        patterns_map[name] = get_pattern(name)
    opt_in_pattern_names = {
        name for name, pat in patterns_map.items() if getattr(pat, "opt_in", False)
    }

    files = _load_source_files(args.unit)
    if not files:
        print("No source files found.", file=sys.stderr)
        sys.exit(0)

    source_by_unit: dict[str, str] = {}
    for unit_name, source_path in files:
        source_by_unit[unit_name] = source_path
        source_by_unit[f"default/{unit_name}"] = source_path

    target_tokens = _parse_symbol_tokens(args.symbol)
    target_funcs: list[dict] = []
    if target_tokens:
        print("Phase 0: Resolving explicit symbol targets...", file=sys.stderr)
        target_funcs = _resolve_target_functions(
            target_tokens,
            min_pct=args.min_pct,
            max_pct=args.max_pct,
            unit_glob=args.unit,
        )
        if not target_funcs:
            print("No matching functions found for --symbol target(s).", file=sys.stderr)
            sys.exit(1)
        print(f"  Resolved {len(target_funcs)} target function(s)", file=sys.stderr)

    # Phase 1: AST scan
    print(f"Phase 1: Scanning for patterns: {', '.join(pattern_names)}", file=sys.stderr)
    scan_start = time.time()

    files_to_scan = files
    if target_funcs:
        target_unit_set = {_normalize_unit(t["unit"]) for t in target_funcs}
        scoped = [
            (unit_name, source_path)
            for unit_name, source_path in files
            if _normalize_unit(unit_name) in target_unit_set
        ]
        if scoped:
            files_to_scan = scoped
            print(
                f"  Scoped scan to {len(files_to_scan)} target file(s)",
                file=sys.stderr,
            )

    match_info = _load_match_info()
    all_hits: list[ScanHit] = []

    for unit_name, source_path in files_to_scan:
        hits = _scan_file(
            Path(source_path), list(patterns_map.values()), unit_name,
            match_info, show_variants=False,
        )
        for hit in hits:
            # Filter by match percentage (skip filter for explicit --symbol targets)
            if not target_funcs and hit.match_percent is not None:
                if hit.match_percent >= args.max_pct:
                    continue
                if hit.match_percent < args.min_pct:
                    continue
            all_hits.append(hit)

    scan_elapsed = time.time() - scan_start
    print(
        f"  Found {len(all_hits)} hits in {len(files_to_scan)} files ({scan_elapsed:.1f}s)",
        file=sys.stderr,
    )

    if not all_hits and not target_funcs:
        print("No hits found.", file=sys.stderr)
        sys.exit(0)

    # Phase 2: Resolve to symbols
    print("Phase 2: Resolving symbols from decomp.db...", file=sys.stderr)
    candidates = _resolve_symbols(all_hits, min_pct=args.min_pct, max_pct=args.max_pct)
    print(f"  Resolved {len(candidates)} function(s) with symbols", file=sys.stderr)

    if target_funcs:
        scanned_by_symbol = {c["symbol"]: c for c in candidates}
        target_candidates: list[dict] = []
        fallback_count = 0
        missing_count = 0
        for target in target_funcs:
            symbol = target["symbol"]
            scanned = scanned_by_symbol.get(symbol)
            if scanned:
                target_candidates.append(scanned)
                continue

            source_path = source_by_unit.get(target["unit"])
            if not source_path:
                source_path = source_by_unit.get(_normalize_unit(target["unit"]))
            if not source_path:
                print(
                    f"  Warning: could not map source file for {target['function_name']} "
                    f"({target['unit']})",
                    file=sys.stderr,
                )
                missing_count += 1
                continue

            target_candidates.append({
                "symbol": symbol,
                "function_name": target["function_name"],
                "source_path": source_path,
                "unit": target["unit"],
                "match_percent": target["match_percent"],
                "patterns": pattern_names[:],
            })
            fallback_count += 1

        candidates = target_candidates
        print(
            f"  Target mode: {len(candidates)} candidate(s) "
            f"({fallback_count} using full pattern set, {missing_count} skipped)",
            file=sys.stderr,
        )

    if not candidates:
        print("No resolvable candidates.", file=sys.stderr)
        sys.exit(0)

    for candidate in candidates:
        candidate["opt_in_patterns_detected"] = sorted(
            p for p in candidate["patterns"] if p in opt_in_pattern_names
        )

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
            if c["opt_in_patterns_detected"]:
                print(
                    f"      opt-in: {', '.join(c['opt_in_patterns_detected'])}",
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
    if args.ghidra:
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
        "pattern_wins": {},  # pattern_name -> {wins, delta, perfects}
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
            # Check Ghidra circuit breaker between functions
            if args.ghidra:
                from .ghidra_cache import ghidra_circuit_tripped
                if ghidra_circuit_tripped():
                    remaining = len(candidates) - i
                    print(
                        f"\n[GHIDRA] Ghidra is down — stopping batch "
                        f"({remaining} functions remaining).",
                        file=sys.stderr, flush=True,
                    )
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
        non_serializable = {"ghidra_batch", "pattern_wins"}
        json_stats = {k: v for k, v in stats.items() if k not in non_serializable}
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
                "preflight_flagged": ghidra_batch.preflight_flagged,
                "preflight_hard_skips": ghidra_batch.preflight_skipped,
            }
        output = {
            "scan": {
                "patterns": pattern_names,
                "opt_in_patterns_requested": sorted(opt_in_pattern_names),
                "files_scanned": len(files),
                "hits": len(all_hits),
                "resolved": len(candidates),
                "candidates": [
                    {
                        "function": c["function_name"],
                        "symbol": c["symbol"],
                        "opt_in_patterns_detected": c["opt_in_patterns_detected"],
                    }
                    for c in candidates
                ],
            },
            "climb": json_stats,
            "ghidra": ghidra_json,
            "elapsed_seconds": round(total_elapsed, 2),
        }
        print(json.dumps(output, indent=2))

    if stats["improvements"]:
        # Sort improvements: perfects first, then by delta descending
        sorted_imps = sorted(
            stats["improvements"],
            key=lambda x: (x["final"] >= 100.0, x["delta"]),
            reverse=True,
        )
        print(f"\nImprovements:", file=sys.stderr)
        for imp in sorted_imps:
            perfect_tag = " ★" if imp["final"] >= 100.0 else ""
            print(
                f"\n  {imp['function']}: {imp['initial']:.1f}% → "
                f"{imp['final']:.1f}% (+{imp['delta']:.1f}%){perfect_tag}",
                file=sys.stderr,
            )
            winning_rounds = imp.get("winning_rounds", [])
            if winning_rounds:
                for wr in winning_rounds:
                    ghidra_tag = ""
                    if wr["variant"] and wr["variant"].startswith("ghidra_"):
                        ghidra_tag = " [GHIDRA]"
                    # Show compose/chain detail if present
                    variant_detail = ""
                    if wr["variant"] and (
                        wr["pattern"].startswith("compose:")
                        or wr["pattern"].startswith("chain:")
                    ):
                        variant_detail = f" ({wr['variant']})"
                    print(
                        f"    R{wr['round']}: {wr['pattern']}"
                        f" ({wr['baseline']:.1f}% → {wr['score']:.1f}%,"
                        f" +{wr['delta']:.1f}%){variant_detail}{ghidra_tag}",
                        file=sys.stderr,
                    )
            else:
                # Fallback: no round data (shouldn't happen but be safe)
                print(
                    f"    (no round details available)",
                    file=sys.stderr,
                )

        # Pattern leaderboard — which patterns actually drove improvements
        pw = stats["pattern_wins"]
        if pw:
            ranked = sorted(pw.items(), key=lambda x: (-x[1]["delta"], -x[1]["wins"]))
            print(f"\n{'─' * 70}", file=sys.stderr)
            print(f"  Pattern Leaderboard (winning patterns across all improved functions)",
                  file=sys.stderr)
            print(f"{'─' * 70}", file=sys.stderr)
            for pname, pstats in ranked:
                perfect_note = f", {pstats['perfects']} perfect" if pstats["perfects"] else ""
                print(
                    f"    {pname:<35s} {pstats['wins']:>2} win(s)  "
                    f"+{pstats['delta']:.2f}%{perfect_note}",
                    file=sys.stderr,
                )

    # Store improvement runs to DB for historical tracking
    if stats["improvements"]:
        try:
            _store_improvement_runs(stats["improvements"])
        except Exception:
            pass  # Don't fail the run


if __name__ == "__main__":
    main()
