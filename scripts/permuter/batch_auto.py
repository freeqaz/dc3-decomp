"""Batch automation pipeline — sweep workable functions with hill_climber.

Queries decomp.db for workable functions, runs hill_climber per function
(grouped by source file), tracks progress in a resume file, and generates
aggregate reports.

Usage:
    # Sweep all workable functions
    python -m scripts.permuter.batch_auto --target workable --max-rounds 5

    # Target specific unit
    python -m scripts.permuter.batch_auto --target unit --unit "system/rndobj/Shader"

    # Dry run — show triage without running
    python -m scripts.permuter.batch_auto --target workable --dry-run --limit 20

    # Resume from previous run
    python -m scripts.permuter.batch_auto --resume logs/permuter/auto_20260303_120000
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import sys
import time
from collections import defaultdict
from datetime import datetime
from pathlib import Path

from .hill_climber import hill_climb
from .patterns import get_all_patterns

# Repo root
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
OBJDIFF_JSON = REPO_ROOT / "objdiff.json"
DECOMP_DB = REPO_ROOT / "decomp.db"

# Regex to extract qualified C++ name from demangled signature
QUALIFIED_NAME_RE = re.compile(r"([\w~][\w:~]*(?:::[\w~]+)+)\s*\(")

# Mismatch types that are unlikely to be fixable by the permuter
SKIP_PATTERNS = frozenset([
    "merged_",     # ICF merged symbols
    "fn_",         # Anonymous functions
    "??__E",       # Dynamic initializers
    "??_B",        # Guard variables
    "??_9",        # vcall thunks
])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.batch_auto",
        description="Batch hill-climbing sweep for decomp functions.",
    )
    parser.add_argument(
        "--target", choices=["workable", "unit"], default="workable",
        help="Target scope: 'workable' (all remaining), 'unit' (specific unit)",
    )
    parser.add_argument(
        "--unit",
        help="Unit glob pattern (e.g. 'system/rndobj/Shader') — required when --target unit",
    )
    parser.add_argument(
        "--limit", type=int, default=0,
        help="Maximum functions to process (0 = unlimited)",
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
        "--min-pct", type=float, default=0,
        help="Minimum match percentage (default: 0)",
    )
    parser.add_argument(
        "--max-pct", type=float, default=99.99,
        help="Maximum match percentage (default: 99.99)",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Show triage without running hill_climber",
    )
    parser.add_argument(
        "--no-apply", action="store_true",
        help="Do not apply improvements to source",
    )
    parser.add_argument(
        "--no-compose", action="store_true",
        help="Disable two-step pattern composition",
    )
    parser.add_argument(
        "--resume", type=Path,
        help="Resume from a previous run's log directory",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output final report as JSON",
    )
    return parser.parse_args()


def load_unit_source_map() -> dict[str, str]:
    """Load objdiff.json and return unit name -> source_path mapping."""
    with open(OBJDIFF_JSON) as f:
        data = json.load(f)

    mapping = {}
    for unit in data.get("units", []):
        name = unit.get("name", "")
        source_path = unit.get("metadata", {}).get("source_path")
        if name and source_path:
            mapping[name] = source_path
    return mapping


def query_workable(
    unit_source_map: dict[str, str],
    min_pct: float,
    max_pct: float,
    limit: int,
    unit_pattern: str | None = None,
) -> list[dict]:
    """Query decomp.db for workable functions."""
    conn = sqlite3.connect(str(DECOMP_DB))
    conn.row_factory = sqlite3.Row

    query = """
        SELECT symbol, demangled, unit, current_percent, verdict
        FROM functions
        WHERE current_percent >= ? AND current_percent <= ?
          AND (verdict IS NULL OR verdict NOT IN ('AT_LIMIT', 'COMPLETE'))
          AND symbol NOT LIKE 'merged_%'
          AND symbol NOT LIKE 'fn_%'
          AND demangled NOT LIKE '%stlpmtx_std::%'
    """
    params: list = [min_pct, max_pct]

    if unit_pattern:
        # Convert glob pattern to SQL LIKE
        like_pattern = unit_pattern.replace("*", "%")
        query += " AND unit LIKE ?"
        params.append(f"%{like_pattern}%")

    query += " ORDER BY current_percent DESC"

    rows = conn.execute(query, params).fetchall()
    conn.close()

    candidates = []
    for row in rows:
        row_dict = dict(row)
        unit = row_dict["unit"]
        demangled = row_dict.get("demangled", "")
        symbol = row_dict["symbol"]

        # Skip boilerplate symbols
        if any(symbol.startswith(p) or symbol.startswith("?" + p) for p in SKIP_PATTERNS):
            continue

        # Must have a source path
        source_path = unit_source_map.get(unit)
        if not source_path:
            continue

        # Must have a parseable source file
        if not Path(REPO_ROOT / source_path).exists():
            continue

        # Extract qualified C++ name from demangled
        m = QUALIFIED_NAME_RE.search(demangled or "")
        if not m:
            continue

        qualified_name = m.group(1)
        row_dict["source_path"] = source_path
        row_dict["qualified_name"] = qualified_name
        candidates.append(row_dict)

    if limit > 0:
        candidates = candidates[:limit]

    return candidates


def triage_candidate(candidate: dict) -> str:
    """Classify a candidate for quick triage.

    Returns one of: 'run', 'skip_boilerplate', 'skip_no_source'.
    """
    symbol = candidate["symbol"]

    # Skip boilerplate
    for p in SKIP_PATTERNS:
        if p in symbol:
            return "skip_boilerplate"

    return "run"


def load_progress(log_dir: Path) -> set[str]:
    """Load completed symbols from a progress file."""
    progress_file = log_dir / "progress.json"
    if not progress_file.exists():
        return set()

    with open(progress_file) as f:
        data = json.load(f)
    return set(data.get("completed", []))


def save_progress(log_dir: Path, completed: set[str]):
    """Save completed symbols to progress file."""
    progress_file = log_dir / "progress.json"
    with open(progress_file, "w") as f:
        json.dump({"completed": sorted(completed)}, f, indent=2)


def main():
    args = parse_args()

    if args.target == "unit" and not args.unit:
        print("Error: --unit is required when --target is 'unit'", file=sys.stderr)
        sys.exit(1)

    # Set up log directory
    if args.resume:
        log_dir = args.resume
        if not log_dir.exists():
            print(f"Error: resume directory not found: {log_dir}", file=sys.stderr)
            sys.exit(1)
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_dir = REPO_ROOT / "logs" / "permuter" / f"auto_{timestamp}"

    log_dir.mkdir(parents=True, exist_ok=True)

    # Load progress for resume
    completed_symbols = load_progress(log_dir)
    if completed_symbols:
        print(f"Resuming: {len(completed_symbols)} already completed", file=sys.stderr)

    # Load mappings
    print("Loading objdiff.json...", file=sys.stderr)
    unit_source_map = load_unit_source_map()
    print(f"  {len(unit_source_map)} units with source paths", file=sys.stderr)

    # Query candidates
    print(f"Querying decomp.db...", file=sys.stderr)
    candidates = query_workable(
        unit_source_map, args.min_pct, args.max_pct,
        args.limit, args.unit,
    )
    print(f"  {len(candidates)} candidates found", file=sys.stderr)

    if not candidates:
        print("No candidates found.", file=sys.stderr)
        sys.exit(0)

    # Filter out already-completed
    candidates = [c for c in candidates if c["symbol"] not in completed_symbols]
    print(f"  {len(candidates)} remaining (after resume filter)", file=sys.stderr)

    # Group by source file for sequential processing within file
    by_source: dict[str, list[dict]] = defaultdict(list)
    for c in candidates:
        by_source[c["source_path"]].append(c)

    # Triage
    triage_counts: dict[str, int] = defaultdict(int)
    runnable: list[dict] = []
    for c in candidates:
        verdict = triage_candidate(c)
        triage_counts[verdict] += 1
        if verdict == "run":
            runnable.append(c)

    print(f"\nTriage: {dict(triage_counts)}", file=sys.stderr)
    print(f"  Will process: {len(runnable)} functions across {len(by_source)} source files",
          file=sys.stderr)

    if args.dry_run:
        print(f"\n--- Dry run: would process ---", file=sys.stderr)
        for i, c in enumerate(runnable[:50]):
            print(
                f"  [{i+1}] {c['qualified_name']} ({c['current_percent']:.1f}%) "
                f"in {c['source_path']}",
                file=sys.stderr,
            )
        if len(runnable) > 50:
            print(f"  ... and {len(runnable) - 50} more", file=sys.stderr)
        sys.exit(0)

    # Run hill_climber on each function
    patterns = get_all_patterns()
    start_time = time.time()

    stats = {
        "total": len(runnable),
        "processed": 0,
        "improved": 0,
        "perfect": 0,
        "no_change": 0,
        "errors": 0,
        "total_delta": 0.0,
        "improvements": [],
    }

    for i, candidate in enumerate(runnable):
        symbol = candidate["symbol"]
        source_path = candidate["source_path"]
        func_name = candidate["qualified_name"]
        pct = candidate["current_percent"]

        print(
            f"\n[{i+1}/{len(runnable)}] {func_name} ({pct:.1f}%) in {source_path}",
            file=sys.stderr,
        )

        try:
            result = hill_climb(
                symbol=symbol,
                source_path=Path(REPO_ROOT / source_path),
                function_name=func_name,
                patterns=patterns,
                max_rounds=args.max_rounds,
                max_variants=args.max_variants,
                plateau_limit=args.plateau_limit,
                compose=not args.no_compose,
                apply=not args.no_apply,
            )

            stats["processed"] += 1
            delta = result.total_delta

            if delta > 0:
                stats["improved"] += 1
                stats["total_delta"] += delta
                improvement = {
                    "symbol": symbol,
                    "function": func_name,
                    "source": source_path,
                    "initial": result.initial_percent,
                    "final": result.final_percent,
                    "delta": delta,
                    "rounds": len(result.rounds),
                    "reason": result.stopped_reason,
                }
                stats["improvements"].append(improvement)
                print(
                    f"  -> IMPROVED +{delta:.2f}% "
                    f"({result.initial_percent:.1f}% -> {result.final_percent:.1f}%)",
                    file=sys.stderr,
                )

                if result.final_percent >= 100.0:
                    stats["perfect"] += 1
            else:
                stats["no_change"] += 1
                print(
                    f"  -> no change ({result.stopped_reason})",
                    file=sys.stderr,
                )

            # Save per-function result
            safe_name = re.sub(r"[^a-zA-Z0-9_]", "_", func_name)[:60]
            result_file = log_dir / f"{i:03d}_{safe_name}.json"
            from dataclasses import asdict
            with open(result_file, "w") as f:
                json.dump(asdict(result), f, indent=2, default=str)

        except Exception as e:
            stats["errors"] += 1
            print(f"  -> ERROR: {e}", file=sys.stderr)

        # Update progress
        completed_symbols.add(symbol)
        save_progress(log_dir, completed_symbols)

    elapsed = time.time() - start_time
    stats["elapsed_seconds"] = round(elapsed, 1)

    # Save summary
    with open(log_dir / "summary.json", "w") as f:
        json.dump(stats, f, indent=2)

    # Print report
    if args.json_output:
        print(json.dumps(stats, indent=2))
    else:
        _print_report(stats, log_dir)


def _print_report(stats: dict, log_dir: Path):
    """Print human-readable summary."""
    print(f"\n{'=' * 60}", file=sys.stderr)
    print("BATCH AUTO RESULTS", file=sys.stderr)
    print(f"{'=' * 60}", file=sys.stderr)
    print(f"  Total candidates: {stats['total']}", file=sys.stderr)
    print(f"  Processed:        {stats['processed']}", file=sys.stderr)
    print(f"  Improved:         {stats['improved']}", file=sys.stderr)
    print(f"  Perfect (100%):   {stats['perfect']}", file=sys.stderr)
    print(f"  No change:        {stats['no_change']}", file=sys.stderr)
    print(f"  Errors:           {stats['errors']}", file=sys.stderr)
    print(f"  Total delta:      +{stats['total_delta']:.2f}%", file=sys.stderr)
    print(f"  Elapsed:          {stats['elapsed_seconds']}s", file=sys.stderr)
    print(f"  Logs:             {log_dir}", file=sys.stderr)

    improvements = stats.get("improvements", [])
    if improvements:
        print(f"\n  Improvements:", file=sys.stderr)
        for imp in improvements:
            print(
                f"    {imp['function']}: {imp['initial']:.1f}% -> {imp['final']:.1f}% "
                f"(+{imp['delta']:.2f}%, {imp['rounds']} rounds, {imp['reason']})",
                file=sys.stderr,
            )


if __name__ == "__main__":
    main()
