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
import os
import re
import sqlite3
import sys
import time
from collections import defaultdict
from datetime import datetime
from pathlib import Path

from .hill_climber import hill_climb
from .patterns import get_all_patterns
from .repo_paths import get_decomp_db_path

# Repo root — uses project detection for multi-project support
from .project import get_project_config as _get_project_config
_project = _get_project_config()
REPO_ROOT = _project.repo_root
OBJDIFF_JSON = REPO_ROOT / "objdiff.json"
DECOMP_DB = get_decomp_db_path()

from .types import extract_qualified_name

# STL namespace prefixes used to identify STL-internal functions.
# A function IS an STL internal when its *own* scope starts with one of these
# prefixes (the scope is the demangled name up to the first '(' argument list).
# User functions that merely accept STL types as parameters do NOT match this
# because the STL namespace only appears inside the '(...)' argument list.
_STL_NAMESPACES = ("stlpmtx_std::", "std::")

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
        "--include-at-limit", action="store_true",
        help="Include AT_LIMIT functions (default: excluded)",
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
        "--workers", type=int, default=0,
        help="Parallel variant-scoring workers (default: 0 = min(nproc-2, 16))",
    )
    parser.add_argument(
        "--resume", type=Path,
        help="Resume from a previous run's log directory",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output final report as JSON",
    )
    parser.add_argument(
        "--no-db-sync", action="store_true",
        help="Skip syncing decomp.db current_percent from report.json at startup "
             "(by default the DB is refreshed so candidate selection isn't stale)",
    )
    return parser.parse_args()


def sync_db_from_report() -> None:
    """Refresh decomp.db current_percent from the latest report.json.

    Without this, query_workable selects on stale percentages and the sweep
    wastes time re-attacking functions that are already at 100% / their final %
    (see the run21 incident: 25 'wins', 0 genuinely new). report.json is
    regenerated by builds; nothing else updates the DB, so we ingest here.
    """
    report_path = _project.repo_root / _project.build_prefix / "report.json"
    if not report_path.exists():
        print(f"  (skipping DB sync — no report at {report_path})", file=sys.stderr)
        return
    try:
        from scripts.orchestrator.database import ingest_report
        r = ingest_report(str(report_path), str(DECOMP_DB), update_existing=True)
        print(f"  DB synced from report: {r['updated']} updated, "
              f"{r['inserted']} inserted", file=sys.stderr)
    except Exception as e:
        print(f"  (DB sync failed, continuing with existing DB: {e})", file=sys.stderr)


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
    include_at_limit: bool = False,
) -> list[dict]:
    """Query decomp.db for workable functions."""
    conn = sqlite3.connect(str(DECOMP_DB))
    conn.row_factory = sqlite3.Row

    if include_at_limit:
        verdict_clause = "AND (verdict IS NULL OR verdict NOT IN ('COMPLETE'))"
    else:
        verdict_clause = "AND (verdict IS NULL OR verdict NOT IN ('AT_LIMIT', 'COMPLETE'))"

    query = f"""
        SELECT symbol, demangled, unit, current_percent, verdict
        FROM functions
        WHERE current_percent >= ? AND current_percent <= ?
          {verdict_clause}
          AND symbol NOT LIKE 'merged_%'
          AND symbol NOT LIKE 'fn_%'
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

        # Skip STL-internal functions — those whose own scope starts with an STL
        # namespace prefix.  The old SQL `NOT LIKE '%stlpmtx_std::%'` was too
        # broad: it also excluded user functions whose *parameter types* mention
        # STL types (e.g. ChordShapeGenerator::BuildSpan takes a stlpmtx_std::map).
        # Instead, extract the scope portion (everything before the first '(' that
        # opens the argument list) and test for an STL-namespace prefix only there.
        _dem = demangled or ""
        _paren = _dem.find("(")
        _scope = _dem[:_paren] if _paren != -1 else _dem
        if any(_scope.startswith(ns) for ns in _STL_NAMESPACES):
            continue

        # Must have a source path
        source_path = unit_source_map.get(unit)
        if not source_path:
            continue

        # Must have a parseable source file
        if not Path(REPO_ROOT / source_path).exists():
            continue

        # Extract qualified C++ name from demangled
        qualified_name = extract_qualified_name(demangled or "")
        if not qualified_name:
            continue
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
    """Save completed symbols to progress file.

    Defensive: long sweeps have been killed by concurrent `git clean` /
    `rm -rf logs/` racing with the write here. Re-ensure the dir exists
    before opening so we don't crash mid-sweep just because the log
    directory got swept out from under us.
    """
    log_dir.mkdir(parents=True, exist_ok=True)
    progress_file = log_dir / "progress.json"
    try:
        with open(progress_file, "w") as f:
            json.dump({"completed": sorted(completed)}, f, indent=2)
    except OSError as e:
        # Last-ditch: log + swallow. Losing progress checkpoints is a
        # smaller harm than crashing the whole sweep.
        print(f"  warn: save_progress failed: {e}", file=sys.stderr)


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

    # Refresh DB from the latest report so candidate selection isn't stale.
    if not args.no_db_sync:
        print("Syncing decomp.db from report.json...", file=sys.stderr)
        sync_db_from_report()

    # Query candidates
    print(f"Querying decomp.db...", file=sys.stderr)
    candidates = query_workable(
        unit_source_map, args.min_pct, args.max_pct,
        args.limit, args.unit, args.include_at_limit,
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

    # Resolve worker count: 0 = auto-derive from nproc (the scorer is now
    # actually parallel after the source_lock removal in scorer.py).
    workers = args.workers
    if workers <= 0:
        nproc = os.cpu_count() or 4
        workers = max(2, min(nproc - 2, 16))
    print(f"[batch_auto] using {workers} variant-scoring workers", file=sys.stderr)

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
                workers=workers,
                unit=candidate.get("unit"),
            )

            stats["processed"] += 1
            delta = result.total_delta

            # Stopped reasons that hill_climb sets when it caught an exception
            # internally (instead of letting it bubble up to the outer except).
            # Without this, batch_auto would count these as "no_change" but
            # log "(error)", producing a summary.json that under-reports errors.
            _ERROR_REASONS = {"error", "ghidra_down", "verification_failed"}

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
            elif result.stopped_reason in _ERROR_REASONS:
                stats["errors"] += 1
                print(
                    f"  -> ERROR ({result.stopped_reason})",
                    file=sys.stderr,
                )
            else:
                stats["no_change"] += 1
                print(
                    f"  -> no change ({result.stopped_reason})",
                    file=sys.stderr,
                )

            # Save per-function result. Wrap so a vanished log_dir doesn't
            # kill the whole sweep — re-create defensively, then try.
            log_dir.mkdir(parents=True, exist_ok=True)
            safe_name = re.sub(r"[^a-zA-Z0-9_]", "_", func_name)[:60]
            result_file = log_dir / f"{i:03d}_{safe_name}.json"
            from dataclasses import asdict
            try:
                with open(result_file, "w") as f:
                    json.dump(asdict(result), f, indent=2, default=str)
            except OSError as e:
                print(f"  warn: result-file write failed: {e}", file=sys.stderr)

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
