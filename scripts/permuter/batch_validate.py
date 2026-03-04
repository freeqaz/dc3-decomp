"""Batch validation runner for the permuter.

Selects candidate functions from decomp.db, runs the permuter on each,
and reports aggregate improvement statistics. Saves per-function logs
to ./logs/permuter/<timestamp>/.

Usage:
    venv/bin/python -m scripts.permuter.batch_validate [--limit 75] [--min-pct 50] [--max-pct 99]
"""

from __future__ import annotations

import argparse
import json
import random
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

# Repo root (script lives in scripts/permuter/)
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
OBJDIFF_JSON = REPO_ROOT / "objdiff.json"
DECOMP_DB = REPO_ROOT / "decomp.db"

from .types import extract_qualified_name


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Batch-run the permuter on candidate functions and report results.",
    )
    parser.add_argument(
        "--jobs", type=int, default=1,
        help="Parallel jobs for processing functions (default: 1)",
    )
    parser.add_argument(
        "--limit", type=int, default=75,
        help="Max functions to test (default: 75)",
    )
    parser.add_argument(
        "--min-pct", type=float, default=50,
        help="Minimum match percentage (default: 50)",
    )
    parser.add_argument(
        "--max-pct", type=float, default=99,
        help="Maximum match percentage (default: 99)",
    )
    parser.add_argument(
        "--no-apply", action="store_true",
        help="Do not apply improvements (default: apply all improvements found)",
    )
    parser.add_argument(
        "--no-guided", action="store_true",
        help="Disable diagnosis-guided filtering (pass through to permuter)",
    )
    parser.add_argument(
        "--timeout", type=int, default=120,
        help="Timeout per function in seconds (default: 120)",
    )
    parser.add_argument(
        "--log-dir", type=Path, default=None,
        help="Log directory (default: logs/permuter/<timestamp>)",
    )
    parser.add_argument(
        "--no-log", action="store_true",
        help="Disable logging to disk",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output final report as JSON",
    )
    parser.add_argument(
        "--no-compose", action="store_true",
        help="Disable two-step pattern composition (pass through to permuter)",
    )
    parser.add_argument(
        "--include-at-limit", action="store_true",
        help="Include functions with AT_LIMIT verdict (excluded by default)",
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


def query_candidates(
    unit_source_map: dict[str, str],
    min_pct: float,
    max_pct: float,
    limit: int,
    include_at_limit: bool = False,
) -> list[dict]:
    """Query decomp.db for candidate functions."""
    import sqlite3

    conn = sqlite3.connect(str(DECOMP_DB))
    conn.row_factory = sqlite3.Row

    excluded = ("COMPLETE",) if include_at_limit else ("AT_LIMIT", "COMPLETE")
    placeholders = ",".join("?" for _ in excluded)

    rows = conn.execute(
        f"""
        SELECT symbol, demangled, unit, current_percent, verdict
        FROM functions
        WHERE current_percent >= ? AND current_percent <= ?
          AND (verdict IS NULL OR verdict NOT IN ({placeholders}))
          AND symbol NOT LIKE 'merged_%'
          AND symbol NOT LIKE 'fn_%'
          AND demangled NOT LIKE '%stlpmtx_std::%'
        ORDER BY current_percent DESC
        """,
        (min_pct, max_pct, *excluded),
    ).fetchall()
    conn.close()

    candidates = []
    for row in rows:
        row_dict = dict(row)
        unit = row_dict["unit"]
        demangled = row_dict.get("demangled", "")

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

    # Shuffle to avoid bias from unit ordering
    random.shuffle(candidates)

    return candidates[:limit]


def setup_log_dir(args: argparse.Namespace) -> Path | None:
    """Create and return the log directory, or None if logging is disabled."""
    if args.no_log:
        return None

    if args.log_dir:
        log_dir = args.log_dir
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_dir = REPO_ROOT / "logs" / "permuter" / timestamp

    log_dir.mkdir(parents=True, exist_ok=True)
    return log_dir


def save_function_log(
    log_dir: Path | None,
    index: int,
    candidate: dict,
    result: dict,
    stderr_text: str,
    elapsed_s: float,
):
    """Save per-function log as JSON."""
    if log_dir is None:
        return

    # Use a safe filename from the function name
    func = candidate["qualified_name"]
    safe_name = re.sub(r'[^a-zA-Z0-9_]', '_', func)[:60]
    filename = f"{index:03d}_{safe_name}.json"

    log_entry = {
        "index": index,
        "symbol": candidate["symbol"],
        "function": func,
        "demangled": candidate.get("demangled", ""),
        "source_path": candidate["source_path"],
        "db_percent": candidate["current_percent"],
        "baseline": result.get("baseline", 0),
        "error": result.get("error"),
        "timed_out": result.get("timed_out", False),
        "elapsed_seconds": round(elapsed_s, 2),
        "stderr": stderr_text,
        "num_variants": len(result.get("results", [])),
        "results": result.get("results", []),
    }

    # Compute summary stats
    variants = result.get("results", [])
    baseline = result.get("baseline", 0)
    if variants:
        build_fails = sum(1 for v in variants if not v.get("build_success", True))
        improved = [v for v in variants if v.get("build_success", True) and v.get("match_percent", 0) > baseline]
        best_delta = max((v["match_percent"] - baseline for v in improved), default=0)
        log_entry["build_failures"] = build_fails
        log_entry["num_improved"] = len(improved)
        log_entry["best_delta"] = round(best_delta, 4)

        # Per-pattern breakdown
        by_pattern: dict[str, dict] = {}
        for v in variants:
            p = v.get("pattern", "unknown")
            if p not in by_pattern:
                by_pattern[p] = {"count": 0, "build_fails": 0, "improved": 0, "best_delta": 0}
            by_pattern[p]["count"] += 1
            if not v.get("build_success", True):
                by_pattern[p]["build_fails"] += 1
            elif v.get("match_percent", 0) > baseline:
                by_pattern[p]["improved"] += 1
                delta = v["match_percent"] - baseline
                by_pattern[p]["best_delta"] = max(by_pattern[p]["best_delta"], delta)
        log_entry["by_pattern"] = by_pattern

    with open(log_dir / filename, "w") as f:
        json.dump(log_entry, f, indent=2, default=str)


def run_permuter(
    symbol: str,
    source_path: str,
    function_name: str,
    timeout: int,
    apply: bool = False,
    no_guided: bool = False,
    no_compose: bool = False,
    workers: int = 1,
) -> tuple[dict, str]:
    """Run the permuter subprocess for a single function.

    Returns (result_dict, stderr_text) where result_dict has:
        - baseline: float
        - results: list of variant results
        - error: str or None
        - timed_out: bool
    """
    cmd = [
        sys.executable, "-m", "scripts.permuter",
        "--symbol", symbol,
        "--source", source_path,
        "--function", function_name,
        "--workers", str(workers),
        "--json",
    ]
    if not apply:
        cmd.append("--no-apply")
    if no_guided:
        cmd.append("--no-guided")
    if no_compose:
        cmd.append("--no-compose")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=str(REPO_ROOT),
        )
    except subprocess.TimeoutExpired as e:
        stderr_raw = getattr(e, "stderr", None) or ""
        stderr_text = stderr_raw.decode("utf-8", errors="replace") if isinstance(stderr_raw, bytes) else str(stderr_raw)
        return {"baseline": 0, "results": [], "error": "timeout", "timed_out": True}, stderr_text

    stderr_text = result.stderr or ""

    # JSON output goes to stdout, progress to stderr
    stdout = result.stdout.strip()
    if not stdout:
        # No JSON output — likely an extraction error
        stderr_snippet = stderr_text.strip()[-200:] if stderr_text else ""
        return {
            "baseline": 0,
            "results": [],
            "error": f"no output (exit {result.returncode}): {stderr_snippet}",
            "timed_out": False,
        }, stderr_text

    try:
        data = json.loads(stdout)
        data["error"] = None
        data["timed_out"] = False
        return data, stderr_text
    except json.JSONDecodeError as e:
        return {
            "baseline": 0,
            "results": [],
            "error": f"JSON parse error: {e}",
            "timed_out": False,
        }, stderr_text


def main():
    args = parse_args()

    # Set up logging
    log_dir = setup_log_dir(args)
    if log_dir:
        print(f"Logging to: {log_dir}", file=sys.stderr)

    # Load mappings
    print("Loading objdiff.json...", file=sys.stderr)
    unit_source_map = load_unit_source_map()
    print(f"  {len(unit_source_map)} units with source paths", file=sys.stderr)

    # Query candidates
    print(f"Querying decomp.db for candidates ({args.min_pct}-{args.max_pct}%)...", file=sys.stderr)
    candidates = query_candidates(unit_source_map, args.min_pct, args.max_pct, args.limit, args.include_at_limit)
    print(f"  {len(candidates)} candidates selected", file=sys.stderr)

    if not candidates:
        print("No candidates found.", file=sys.stderr)
        sys.exit(0)

    # Run permuter on each
    print(f"\nRunning permuter on {len(candidates)} functions...\n", file=sys.stderr)

    total_tested = 0
    total_variants = 0
    total_build_failures = 0
    total_improvements = 0
    total_timeouts = 0
    total_errors = 0
    total_no_variants = 0
    total_early_skip = 0
    improvements = []
    per_function_logs: list[dict] = []
    start_time = time.time()

    import os
    workers_per_job = max(1, (os.cpu_count() or 1) // args.jobs)

    def process_candidate(candidate_info):
        i, candidate = candidate_info
        symbol = candidate["symbol"]
        source = candidate["source_path"]
        func = candidate["qualified_name"]
        
        func_start = time.time()
        result, stderr_text = run_permuter(
            symbol, source, func,
            timeout=args.timeout,
            apply=not args.no_apply,
            no_guided=args.no_guided,
            no_compose=args.no_compose,
            workers=workers_per_job,
        )
        func_elapsed = time.time() - func_start
        
        # Save per-function log
        save_function_log(log_dir, i, candidate, result, stderr_text, func_elapsed)
        return i, candidate, result, stderr_text, func_elapsed

    from concurrent.futures import ThreadPoolExecutor, as_completed
    
    # We must not run functions from the same source file concurrently if apply is True!
    # Let's just group them by source or use a lock if we want true concurrent apply.
    # The permuter modifies the source file. If two permuters modify the same file concurrently, they will corrupt it.
    # Therefore, we group by source file and use ThreadPoolExecutor on the groups, or just don't process same-source concurrently.

    from collections import defaultdict
    by_source = defaultdict(list)
    for i, c in enumerate(candidates):
        by_source[c["source_path"]].append((i, c))

    def process_group(source_path, funcs):
        results = []
        for c_info in funcs:
            i, candidate = c_info
            pct = candidate["current_percent"]
            func = candidate["qualified_name"]
            
            # Since output is concurrent, we format a single string to print
            print(f"[{i + 1}/{len(candidates)}] {func} ({pct:.1f}%) ... ", end="", flush=True, file=sys.stderr)
            
            res_tuple = process_candidate(c_info)
            results.append(res_tuple)
            
            i_ret, candidate_ret, result, stderr_text, func_elapsed = res_tuple
            baseline = result.get("baseline", 0)
            variants = result.get("results", [])
            
            if result["timed_out"]:
                print(f"TIMEOUT ({func_elapsed:.0f}s)", file=sys.stderr)
            elif result["error"]:
                if "Nothing to permute" in stderr_text:
                    print("SKIP (all noise)", file=sys.stderr)
                else:
                    print(f"ERROR: {result['error'][:60]}", file=sys.stderr)
            elif not variants:
                print(f"no variants (baseline {baseline:.1f}%)", file=sys.stderr)
            else:
                improved = [v for v in variants if v.get("build_success", True) and v.get("match_percent", 0) > baseline]
                if improved:
                    best = max(improved, key=lambda v: v["match_percent"])
                    delta = best["match_percent"] - baseline
                    print(f"IMPROVED +{delta:.2f}% ({baseline:.1f}% -> {best['match_percent']:.1f}%) [{best.get('pattern', '?')}]", file=sys.stderr)
                else:
                    build_fails = sum(1 for v in variants if not v.get("build_success", True))
                    exec_broken = sum(1 for v in variants if v.get("execution_equivalent") is False)
                    suffix = f" ({exec_broken} exec-broken)" if exec_broken else ""
                    print(f"no improvement ({len(variants)} variants, {build_fails} build fails{suffix})", file=sys.stderr)

        return results

    if args.jobs <= 1:
        for source_path, funcs in by_source.items():
            res = process_group(source_path, funcs)
            for i, candidate, result, stderr_text, func_elapsed in res:
                # Same aggregation logic
                pass # We do the aggregation below
    else:
        all_results = []
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = []
            for source_path, funcs in by_source.items():
                futures.append(executor.submit(process_group, source_path, funcs))
            
            for future in as_completed(futures):
                all_results.extend(future.result())
        
        # Sort by original index to process linearly
        all_results.sort(key=lambda x: x[0])
        
        for i, candidate, result, stderr_text, func_elapsed in all_results:
            if result["timed_out"]:
                total_timeouts += 1
                continue
            
            if result["error"]:
                if "Nothing to permute" in stderr_text:
                    total_early_skip += 1
                else:
                    total_errors += 1
                continue
            
            total_tested += 1
            variants = result.get("results", [])
            baseline = result.get("baseline", 0)
            
            if not variants:
                total_no_variants += 1
                continue
            
            total_variants += len(variants)
            build_fails = sum(1 for v in variants if not v.get("build_success", True))
            total_build_failures += build_fails
            
            improved = [v for v in variants if v.get("build_success", True) and v.get("match_percent", 0) > baseline]
            if improved:
                best = max(improved, key=lambda v: v["match_percent"])
                delta = best["match_percent"] - baseline
                total_improvements += 1
                improvements.append({
                    "symbol": candidate["symbol"],
                    "function": candidate["qualified_name"],
                    "source": candidate["source_path"],
                    "baseline": baseline,
                    "new_pct": best["match_percent"],
                    "delta": delta,
                    "variant": best.get("name", "?"),
                    "pattern": best.get("pattern", "?"),
                    "description": best.get("description", "?"),
                    "applied": not args.no_apply,
                })

    elapsed = time.time() - start_time

    # Build report
    report = {
        "total_candidates": len(candidates),
        "total_tested": total_tested,
        "total_variants": total_variants,
        "total_build_failures": total_build_failures,
        "build_failure_rate": (
            f"{100 * total_build_failures / total_variants:.1f}%"
            if total_variants > 0 else "N/A"
        ),
        "total_improvements": total_improvements,
        "improvement_rate": (
            f"{100 * total_improvements / total_tested:.1f}%"
            if total_tested > 0 else "N/A"
        ),
        "total_timeouts": total_timeouts,
        "total_errors": total_errors,
        "total_early_skip": total_early_skip,
        "total_no_variants": total_no_variants,
        "elapsed_seconds": round(elapsed, 1),
        "improvements": improvements,
    }

    # Save summary report to log dir
    if log_dir:
        with open(log_dir / "summary.json", "w") as f:
            json.dump(report, f, indent=2)
        print(f"\nLogs saved to: {log_dir}", file=sys.stderr)

    if args.json_output:
        print(json.dumps(report, indent=2))
    else:
        _print_report(report)


def _print_report(report: dict):
    """Print a human-readable summary report."""
    print(f"\n{'=' * 70}", file=sys.stderr)
    print("BATCH PERMUTER RESULTS", file=sys.stderr)
    print(f"{'=' * 70}", file=sys.stderr)

    print(f"  Candidates:       {report['total_candidates']}", file=sys.stderr)
    print(f"  Tested:           {report['total_tested']}", file=sys.stderr)
    print(f"  Total variants:   {report['total_variants']}", file=sys.stderr)
    print(f"  Build failures:   {report['total_build_failures']} ({report['build_failure_rate']})", file=sys.stderr)
    print(f"  Improvements:     {report['total_improvements']} ({report['improvement_rate']})", file=sys.stderr)
    print(f"  Timeouts:         {report['total_timeouts']}", file=sys.stderr)
    print(f"  Errors:           {report['total_errors']}", file=sys.stderr)
    early_skip = report.get('total_early_skip', 0)
    if early_skip:
        print(f"  Early skip:       {early_skip} (all noise)", file=sys.stderr)
    print(f"  No variants:      {report['total_no_variants']}", file=sys.stderr)
    print(f"  Elapsed:          {report['elapsed_seconds']}s", file=sys.stderr)

    improvements = report.get("improvements", [])
    if improvements:
        print(f"\n{'=' * 70}", file=sys.stderr)
        print("IMPROVEMENTS FOUND", file=sys.stderr)
        print(f"{'=' * 70}", file=sys.stderr)

        # Per-pattern breakdown
        by_pattern: dict[str, list] = {}
        total_delta = 0
        for imp in improvements:
            applied = " [APPLIED]" if imp.get("applied") else ""
            print(
                f"  {imp['function']}: {imp['baseline']:.1f}% -> {imp['new_pct']:.1f}% "
                f"(+{imp['delta']:.2f}%){applied}",
                file=sys.stderr,
            )
            print(f"    variant: {imp['variant']}: {imp['description']}", file=sys.stderr)
            print(f"    source: {imp['source']}", file=sys.stderr)
            total_delta += imp["delta"]
            pattern = imp.get("pattern", "unknown")
            by_pattern.setdefault(pattern, []).append(imp)

        print(f"\n  Average delta: +{total_delta / len(improvements):.2f}%", file=sys.stderr)

        # Pattern effectiveness summary
        print(f"\n  By pattern:", file=sys.stderr)
        for pattern, imps in sorted(by_pattern.items(), key=lambda x: -len(x[1])):
            avg = sum(i["delta"] for i in imps) / len(imps)
            print(f"    {pattern:25s}: {len(imps)} wins, avg +{avg:.2f}%", file=sys.stderr)
    else:
        print("\nNo improvements found.", file=sys.stderr)


if __name__ == "__main__":
    main()
