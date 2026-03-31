"""Batch sweep — feed triage results through the hill climber.

Loads a triage report, filters to target categories (REGSWAP_ONLY,
REGSWAP_PLUS by default), groups by source file for serialization,
and runs the hill climber on each candidate.

Usage:
    python -m scripts.permuter.batch_sweep \
        --triage-report report.json \
        --categories REGSWAP_ONLY,REGSWAP_PLUS \
        --jobs 4 --max-rounds 5 --json -o sweep_results.json
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import asdict
from pathlib import Path

from .types import HillClimbResult

# Repo root — uses project detection for multi-project support
from .project import get_project_config as _get_project_config
REPO_ROOT = _get_project_config().repo_root


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Batch hill-climbing sweep from triage report.",
    )
    parser.add_argument(
        "--triage-report", type=Path, required=True,
        help="Path to triage JSON report",
    )
    parser.add_argument(
        "--categories", default="REGSWAP_ONLY,REGSWAP_PLUS",
        help="Comma-separated categories to process (default: REGSWAP_ONLY,REGSWAP_PLUS)",
    )
    parser.add_argument(
        "--max-rounds", type=int, default=5,
        help="Max hill-climbing rounds per function (default: 5)",
    )
    parser.add_argument(
        "--max-variants", type=int, default=100,
        help="Max variants per round (default: 100)",
    )
    parser.add_argument(
        "--plateau-limit", type=int, default=3,
        help="Stop after N rounds without improvement (default: 3)",
    )
    parser.add_argument(
        "--compose", action="store_true", default=True,
        help="Enable composition (default: True)",
    )
    parser.add_argument(
        "--no-compose", action="store_false", dest="compose",
        help="Disable composition",
    )
    parser.add_argument(
        "--no-apply", action="store_true",
        help="Do not apply improvements (dry run)",
    )
    parser.add_argument(
        "--jobs", type=int, default=1,
        help="Parallel jobs for different source files (default: 1)",
    )
    parser.add_argument(
        "--limit", type=int, default=0,
        help="Max functions to process (0 = unlimited)",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output results as JSON",
    )
    parser.add_argument(
        "-o", "--output", type=Path, default=None,
        help="Output file (default: stdout for JSON, stderr for text)",
    )
    return parser.parse_args()


def load_triage_report(path: Path) -> list[dict]:
    """Load and return the results list from a triage report."""
    with open(path) as f:
        data = json.load(f)
    return data.get("results", [])


def filter_candidates(
    results: list[dict],
    categories: set[str],
    limit: int,
) -> list[dict]:
    """Filter triage results to target categories."""
    filtered = [r for r in results if r.get("category") in categories and not r.get("error")]
    if limit > 0:
        filtered = filtered[:limit]
    return filtered


def _run_one(
    symbol: str,
    source_path: str,
    function_name: str,
    max_rounds: int,
    max_variants: int,
    plateau_limit: int,
    compose: bool,
    apply: bool,
    unit: str | None,
) -> dict:
    """Run hill climber for one function (in subprocess worker).

    Returns the HillClimbResult as a dict for serialization across processes.
    """
    from .hill_climber import hill_climb
    from .patterns import get_all_patterns

    patterns = get_all_patterns()
    result = hill_climb(
        symbol=symbol,
        source_path=Path(source_path),
        function_name=function_name,
        patterns=patterns,
        max_rounds=max_rounds,
        max_variants=max_variants,
        plateau_limit=plateau_limit,
        compose=compose,
        apply=apply,
        unit=unit,
    )
    return asdict(result)


def main():
    args = parse_args()
    categories = set(c.strip() for c in args.categories.split(","))

    # Load triage report
    print(f"Loading triage report: {args.triage_report}", file=sys.stderr)
    results = load_triage_report(args.triage_report)
    print(f"  {len(results)} total functions in report", file=sys.stderr)

    # Filter to target categories
    candidates = filter_candidates(results, categories, args.limit)
    print(
        f"  {len(candidates)} candidates in {', '.join(sorted(categories))}",
        file=sys.stderr,
    )

    if not candidates:
        print("No candidates to process.", file=sys.stderr)
        sys.exit(0)

    # Group by source file — same-file functions must run sequentially
    by_source: dict[str, list[dict]] = defaultdict(list)
    for c in candidates:
        by_source[c["source_path"]].append(c)

    print(
        f"  {len(by_source)} source files, "
        f"max {max(len(v) for v in by_source.values())} functions in one file",
        file=sys.stderr,
    )

    # Run hill climber
    start_time = time.time()
    all_results: list[dict] = []
    total_improved = 0
    total_delta = 0.0

    if args.jobs <= 1:
        # Sequential execution
        for i, candidate in enumerate(candidates):
            func = candidate["qualified_name"]
            pct = candidate["current_percent"]
            print(
                f"\n[{i + 1}/{len(candidates)}] {func} ({pct:.1f}%)",
                file=sys.stderr,
            )

            result_dict = _run_one(
                symbol=candidate["symbol"],
                source_path=candidate["source_path"],
                function_name=candidate["qualified_name"],
                max_rounds=args.max_rounds,
                max_variants=args.max_variants,
                plateau_limit=args.plateau_limit,
                compose=args.compose,
                apply=not args.no_apply,
                unit=None,
            )
            all_results.append(result_dict)

            delta = result_dict["total_delta"]
            if delta > 0:
                total_improved += 1
                total_delta += delta
                print(
                    f"  => IMPROVED +{delta:.2f}% "
                    f"({result_dict['initial_percent']:.2f}% -> {result_dict['final_percent']:.2f}%)",
                    file=sys.stderr,
                )
            else:
                print(
                    f"  => no improvement ({result_dict['stopped_reason']})",
                    file=sys.stderr,
                )
    else:
        # Parallel execution by source file groups
        # Each source file group runs sequentially within, but different files run in parallel
        def _run_source_group(source_path: str, funcs: list[dict]) -> list[dict]:
            group_results = []
            for candidate in funcs:
                result_dict = _run_one(
                    symbol=candidate["symbol"],
                    source_path=candidate["source_path"],
                    function_name=candidate["qualified_name"],
                    max_rounds=args.max_rounds,
                    max_variants=args.max_variants,
                    plateau_limit=args.plateau_limit,
                    compose=args.compose,
                    apply=not args.no_apply,
                    unit=None,
                )
                group_results.append(result_dict)
            return group_results

        with ProcessPoolExecutor(max_workers=args.jobs) as executor:
            futures = {}
            for source_path, funcs in by_source.items():
                future = executor.submit(_run_source_group, source_path, funcs)
                futures[future] = source_path

            completed = 0
            for future in as_completed(futures):
                source_path = futures[future]
                try:
                    group_results = future.result()
                    all_results.extend(group_results)
                    for r in group_results:
                        completed += 1
                        delta = r["total_delta"]
                        if delta > 0:
                            total_improved += 1
                            total_delta += delta
                        print(
                            f"[{completed}/{len(candidates)}] "
                            f"{r['function_name']}: "
                            f"{r['initial_percent']:.2f}% -> {r['final_percent']:.2f}% "
                            f"({r['stopped_reason']})",
                            file=sys.stderr,
                        )
                except Exception as e:
                    print(
                        f"Error processing {source_path}: {e}",
                        file=sys.stderr,
                    )

    elapsed = time.time() - start_time

    # Build sweep report
    report = {
        "metadata": {
            "triage_report": str(args.triage_report),
            "categories": sorted(categories),
            "total_candidates": len(candidates),
            "max_rounds": args.max_rounds,
            "compose": args.compose,
            "apply": not args.no_apply,
            "jobs": args.jobs,
            "elapsed_seconds": round(elapsed, 1),
        },
        "summary": {
            "total_processed": len(all_results),
            "total_improved": total_improved,
            "improvement_rate": (
                f"{100 * total_improved / len(all_results):.1f}%"
                if all_results else "N/A"
            ),
            "total_delta": round(total_delta, 2),
            "avg_delta": (
                round(total_delta / total_improved, 2)
                if total_improved > 0 else 0
            ),
            "by_stop_reason": _count_stop_reasons(all_results),
        },
        "results": all_results,
    }

    # Output
    output_text = json.dumps(report, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output_text)
        print(f"\nResults written to: {args.output}", file=sys.stderr)
    elif args.json_output:
        print(output_text)

    # Print summary
    _print_summary(report["summary"], elapsed)


def _count_stop_reasons(results: list[dict]) -> dict[str, int]:
    """Count stop reasons across all results."""
    from collections import Counter
    return dict(Counter(r.get("stopped_reason", "unknown") for r in results).most_common())


def _print_summary(summary: dict, elapsed: float):
    """Print human-readable sweep summary."""
    print(f"\n{'=' * 60}", file=sys.stderr)
    print("SWEEP SUMMARY", file=sys.stderr)
    print(f"{'=' * 60}", file=sys.stderr)
    print(f"  Processed:       {summary['total_processed']}", file=sys.stderr)
    print(f"  Improved:        {summary['total_improved']} ({summary['improvement_rate']})", file=sys.stderr)
    print(f"  Total delta:     +{summary['total_delta']:.2f}%", file=sys.stderr)
    print(f"  Avg delta:       +{summary['avg_delta']:.2f}%", file=sys.stderr)
    print(f"  Elapsed:         {elapsed:.1f}s", file=sys.stderr)

    stop_reasons = summary.get("by_stop_reason", {})
    if stop_reasons:
        print(f"\n  Stop reasons:", file=sys.stderr)
        for reason, count in stop_reasons.items():
            print(f"    {reason:20s}: {count}", file=sys.stderr)


if __name__ == "__main__":
    main()
