"""CLI entry point: python -m scripts.permuter"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .extractor import extract_function
from .generator import generate_variants
from .scorer import Scorer
from .patterns import get_all_patterns, get_pattern, list_patterns
from .types import ScoreResult


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter",
        description="Generate and score source variations for decomp matching.",
    )
    parser.add_argument(
        "--symbol", help="Mangled symbol name for objdiff"
    )
    parser.add_argument(
        "--source", type=Path, help="Path to .cpp source file"
    )
    parser.add_argument(
        "--function", help="Qualified C++ function name (e.g. RndMesh::BurnXfm)"
    )
    parser.add_argument(
        "--patterns",
        default="all",
        help="Comma-separated pattern names, or 'all' (default: all)",
    )
    parser.add_argument(
        "--max-variants",
        type=int,
        default=100,
        help="Maximum variants to generate (default: 100)",
    )
    parser.add_argument(
        "--stop-on-perfect",
        action="store_true",
        help="Stop scoring when a 100%% match is found",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output", help="Output results as JSON"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Generate and list variants without building/scoring",
    )
    parser.add_argument(
        "--list-patterns",
        action="store_true",
        help="List available patterns and exit",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    if args.list_patterns:
        for name in list_patterns():
            print(f"  {name}")
        return

    # Validate required args
    missing = []
    if not args.symbol:
        missing.append("--symbol")
    if not args.source:
        missing.append("--source")
    if not args.function:
        missing.append("--function")
    if missing:
        print(f"Error: required arguments: {', '.join(missing)}", file=sys.stderr)
        sys.exit(1)

    # Resolve patterns
    if args.patterns == "all":
        patterns = get_all_patterns()
    else:
        pattern_names = [p.strip() for p in args.patterns.split(",")]
        patterns = [get_pattern(name) for name in pattern_names]

    # Extract function
    print(f"Extracting {args.function} from {args.source}...", file=sys.stderr)
    ctx = extract_function(args.source, args.function)
    print(
        f"Found function with {len(ctx.statements)} statements "
        f"({ctx.func_byte_range[1] - ctx.func_byte_range[0]} bytes)",
        file=sys.stderr,
    )

    # Generate variants
    variants = list(generate_variants(ctx, patterns, args.max_variants))
    print(f"Generated {len(variants)} variants", file=sys.stderr)

    if args.dry_run:
        _print_dry_run(variants, args.json_output)
        return

    # Score variants
    results: list[ScoreResult] = []
    with Scorer(args.source, args.symbol) as scorer:
        baseline = scorer.get_baseline()
        print(f"Baseline: {baseline:.2f}%", file=sys.stderr)

        for i, variant in enumerate(variants):
            print(
                f"[{i + 1}/{len(variants)}] {variant.name}: {variant.description}... ",
                end="",
                flush=True,
                file=sys.stderr,
            )
            result = scorer.score(variant)
            results.append(result)

            marker = ""
            if not result.build_success:
                marker = " BUILD FAILED"
            elif result.match_percent > baseline:
                marker = " IMPROVED"
            elif result.match_percent == baseline:
                marker = " same"

            print(f"{result.match_percent:.2f}%{marker}", file=sys.stderr)

            if args.stop_on_perfect and result.match_percent >= 100.0:
                print("Perfect match found!", file=sys.stderr)
                break

    # Sort by match percentage descending
    results.sort(key=lambda r: r.match_percent, reverse=True)

    if args.json_output:
        _print_json(baseline, results)
    else:
        _print_table(baseline, results)


def _print_dry_run(variants, json_output: bool):
    if json_output:
        data = [
            {
                "name": v.name,
                "pattern": v.pattern_name,
                "description": v.description,
            }
            for v in variants
        ]
        print(json.dumps(data, indent=2))
    else:
        for v in variants:
            print(f"  [{v.pattern_name}] {v.name}: {v.description}")


def _print_json(baseline: float, results: list[ScoreResult]):
    data = {
        "baseline": baseline,
        "results": [
            {
                "name": r.variant.name,
                "pattern": r.variant.pattern_name,
                "description": r.variant.description,
                "match_percent": r.match_percent,
                "build_success": r.build_success,
                "error": r.error,
                "delta": r.match_percent - baseline,
            }
            for r in results
        ],
    }
    print(json.dumps(data, indent=2))


def _print_table(baseline: float, results: list[ScoreResult]):
    print(f"\n{'=' * 70}")
    print(f"RESULTS (baseline: {baseline:.2f}%)")
    print(f"{'=' * 70}")
    for r in results:
        delta = r.match_percent - baseline
        marker = ""
        if not r.build_success:
            marker = " BUILD FAILED"
        elif delta > 0:
            marker = f" +{delta:.2f}%"
        elif delta == 0:
            marker = " (same)"
        else:
            marker = f" {delta:.2f}%"

        print(f"  {r.variant.name:25s} {r.match_percent:6.2f}%{marker}")
        print(f"    {r.variant.description}")

    # Summary
    improved = [r for r in results if r.build_success and r.match_percent > baseline]
    if improved:
        best = improved[0]
        print(f"\nBest improvement: {best.variant.name} at {best.match_percent:.2f}%")
        print(f"  {best.variant.description}")
    else:
        print("\nNo improvements found.")


if __name__ == "__main__":
    main()
