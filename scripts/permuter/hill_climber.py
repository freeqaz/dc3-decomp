"""Hill climber — iterative improve-apply-repeat loop for a single function.

Each round: extract function, get baseline with diagnosis, generate variants
(with composition), score all, apply best improvement, repeat. Stops on
100% match, plateau (N rounds without improvement), max rounds, or all noise.

Usage:
    python -m scripts.permuter.hill_climber \
        --symbol "?Poll@LabelNumberTicker@@UAAXXZ" \
        --source src/system/ui/LabelNumberTicker.cpp \
        --function "LabelNumberTicker::Poll" \
        --max-rounds 10 --compose --json
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

from .composer import _DEFAULT_PAIRS
from .diagnosis import format_diagnosis_summary, is_all_noise
from .extractor import extract_function
from .generator import generate_variants
from .patterns import get_all_patterns, get_pattern
from .scorer import Scorer
from .types import HillClimbResult, RoundResult, ScoreResult


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.hill_climber",
        description="Iterative hill-climbing loop for decomp matching.",
    )
    parser.add_argument(
        "--symbol",
        help="Mangled symbol name for objdiff (auto-resolves source/function from DB)",
    )
    parser.add_argument(
        "--source", type=Path,
        help="Path to .cpp source file",
    )
    parser.add_argument(
        "--function",
        help="Qualified C++ function name (e.g. LabelNumberTicker::Poll)",
    )
    parser.add_argument(
        "--max-rounds", type=int, default=10,
        help="Maximum hill-climbing rounds (default: 10)",
    )
    parser.add_argument(
        "--max-variants", type=int, default=100,
        help="Maximum variants per round (default: 100)",
    )
    parser.add_argument(
        "--plateau-limit", type=int, default=3,
        help="Stop after N rounds without improvement (default: 3)",
    )
    parser.add_argument(
        "--compose", action="store_true",
        help="Enable two-step pattern composition",
    )
    parser.add_argument(
        "--patterns", default="all",
        help="Comma-separated pattern names, or 'all' (default: all)",
    )
    parser.add_argument(
        "--no-apply", action="store_true",
        help="Do not apply improvements to source (dry run)",
    )
    parser.add_argument(
        "--unit",
        help="Unit name for unicorn execution guard rail",
    )
    parser.add_argument(
        "--workers", type=int, default=0,
        help="Parallel compile workers for batch scoring (default: CPU count)",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output results as JSON",
    )
    return parser.parse_args()


def hill_climb(
    symbol: str,
    source_path: Path,
    function_name: str,
    patterns: list,
    max_rounds: int = 10,
    max_variants: int = 100,
    plateau_limit: int = 3,
    compose: bool = False,
    apply: bool = True,
    unit: str | None = None,
    workers: int = 6,
) -> HillClimbResult:
    """Run the hill-climbing loop for a single function.

    Args:
        symbol: Mangled symbol for objdiff.
        source_path: Path to .cpp source file.
        function_name: Qualified C++ function name.
        patterns: List of pattern instances to apply.
        max_rounds: Maximum number of hill-climbing rounds.
        max_variants: Maximum variants per round.
        plateau_limit: Stop after this many rounds without improvement.
        compose: Enable two-step pattern composition.
        apply: Whether to apply improvements to source file.
        unit: Unit name for unicorn guard rail.

    Returns:
        HillClimbResult with full session history.
    """
    compose_pairs = _DEFAULT_PAIRS if compose else None
    start_time = time.time()
    rounds: list[RoundResult] = []
    initial_percent = 0.0
    current_percent = 0.0
    plateau_count = 0
    stopped_reason = "max_rounds"

    # Save original source for rollback if not applying
    original_source = source_path.read_bytes()

    try:
        for round_num in range(1, max_rounds + 1):
            print(
                f"\n--- Round {round_num}/{max_rounds} ---",
                file=sys.stderr,
            )

            # Re-extract function each round (source may have changed)
            try:
                ctx = extract_function(source_path, function_name)
            except ValueError as e:
                print(f"Extraction failed: {e}", file=sys.stderr)
                stopped_reason = "error"
                break

            # Fresh scorer per round — context manager restores source on exit
            with Scorer(source_path, symbol, unit=unit) as scorer:
                baseline = scorer.get_baseline(guided=True)

                if round_num == 1:
                    initial_percent = baseline
                current_percent = baseline

                print(f"Baseline: {baseline:.2f}%", file=sys.stderr)

                # Wire symbol and diagnosis into context
                ctx.symbol = symbol
                if scorer.diagnosis:
                    ctx.diagnosis = scorer.diagnosis
                    print(
                        format_diagnosis_summary(scorer.diagnosis),
                        file=sys.stderr,
                    )

                    # Early exit: all noise
                    if is_all_noise(scorer.diagnosis):
                        print(
                            "All mismatches are noise — stopping.",
                            file=sys.stderr,
                        )
                        stopped_reason = "noise_only"
                        break

                # Perfect match check
                if baseline >= 100.0:
                    print("Already at 100%!", file=sys.stderr)
                    stopped_reason = "perfect"
                    break

                # Generate variants
                variants = list(generate_variants(
                    ctx, patterns, max_variants,
                    compose_pairs=compose_pairs,
                ))
                print(
                    f"Generated {len(variants)} variants",
                    file=sys.stderr,
                )

                if not variants:
                    print("No variants generated — stopping.", file=sys.stderr)
                    rounds.append(RoundResult(
                        round_num=round_num,
                        baseline=baseline,
                        best_name=None,
                        best_pattern=None,
                        best_score=baseline,
                        delta=0.0,
                        num_variants=0,
                        improved=False,
                    ))
                    stopped_reason = "no_variants"
                    break

                # Score all variants (parallel compilation)
                best_result: ScoreResult | None = None
                best_score = baseline

                print(
                    f"Scoring {len(variants)} variants "
                    f"({workers} workers)...",
                    file=sys.stderr,
                )
                batch_results = scorer.score_batch(variants, workers=workers)

                for i, result in enumerate(batch_results):
                    marker = ""
                    if result.error in ("source_dedup", "cache_hit", "obj_dedup"):
                        marker = f" [{result.error}]"
                    elif not result.build_success:
                        marker = " BUILD FAILED"
                    elif result.error and result.error != "cached_build_fail":
                        marker = f" ERROR"

                    if result.match_percent > best_score:
                        marker += " NEW BEST"
                        best_result = result
                        best_score = result.match_percent
                    elif result.match_percent > baseline:
                        marker += " improved"

                    print(
                        f"  [{i + 1}/{len(batch_results)}] "
                        f"{result.variant.name}: "
                        f"{result.match_percent:.2f}%{marker}",
                        file=sys.stderr,
                    )

            # After Scorer exits (source restored), record round and apply
            delta = best_score - baseline
            improved = best_result is not None and best_score > baseline

            rounds.append(RoundResult(
                round_num=round_num,
                baseline=baseline,
                best_name=best_result.variant.name if best_result else None,
                best_pattern=best_result.variant.pattern_name if best_result else None,
                best_score=best_score,
                delta=delta,
                num_variants=len(variants),
                improved=improved,
            ))

            if improved:
                plateau_count = 0
                current_percent = best_score

                if apply:
                    # Write the improved source (Scorer already restored original)
                    source_path.write_bytes(best_result.variant.source)
                    print(
                        f"Applied: {best_result.variant.name} "
                        f"({baseline:.2f}% -> {best_score:.2f}%, +{delta:.2f}%)",
                        file=sys.stderr,
                    )
                else:
                    print(
                        f"Best: {best_result.variant.name} "
                        f"({baseline:.2f}% -> {best_score:.2f}%, +{delta:.2f}%) [NOT APPLIED]",
                        file=sys.stderr,
                    )
                    # Without applying, can't iterate further
                    stopped_reason = "no_apply"
                    break

                if best_score >= 100.0:
                    stopped_reason = "perfect"
                    break
            else:
                plateau_count += 1
                print(
                    f"No improvement (plateau {plateau_count}/{plateau_limit})",
                    file=sys.stderr,
                )
                if plateau_count >= plateau_limit:
                    stopped_reason = "plateau"
                    break
    except Exception as e:
        stopped_reason = "error"
        print(f"Error: {e}", file=sys.stderr)
    finally:
        # If not applying, restore original source
        if not apply:
            source_path.write_bytes(original_source)

    elapsed = time.time() - start_time
    final_percent = current_percent if apply else initial_percent

    return HillClimbResult(
        symbol=symbol,
        function_name=function_name,
        source_path=str(source_path),
        initial_percent=initial_percent,
        final_percent=final_percent,
        total_delta=final_percent - initial_percent,
        rounds=rounds,
        stopped_reason=stopped_reason,
        elapsed_seconds=round(elapsed, 2),
    )


def main():
    args = parse_args()

    # Auto-resolve from DB if only --symbol is provided
    if args.symbol and (not args.source or not args.function):
        from .__main__ import resolve_from_db
        resolved = resolve_from_db(args.symbol)
        if resolved:
            mangled, source_path, qualified_name = resolved
            if not args.source:
                args.source = source_path
            if not args.function:
                args.function = qualified_name
            args.symbol = mangled
            print(f"Resolved: {args.symbol} -> {args.function} in {args.source}", file=sys.stderr)
        else:
            print(f"Could not resolve '{args.symbol}' from decomp.db", file=sys.stderr)
            sys.exit(1)

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

    workers = args.workers or os.cpu_count() or 4

    result = hill_climb(
        symbol=args.symbol,
        source_path=args.source,
        function_name=args.function,
        patterns=patterns,
        max_rounds=args.max_rounds,
        max_variants=args.max_variants,
        plateau_limit=args.plateau_limit,
        compose=args.compose,
        apply=not args.no_apply,
        unit=args.unit,
        workers=workers,
    )

    if args.json_output:
        from dataclasses import asdict
        print(json.dumps(asdict(result), indent=2))
    else:
        _print_result(result)


def _print_result(result: HillClimbResult):
    """Print human-readable hill-climbing result."""
    print(f"\n{'=' * 60}", file=sys.stderr)
    print("HILL CLIMB RESULT", file=sys.stderr)
    print(f"{'=' * 60}", file=sys.stderr)
    print(f"  Function:   {result.function_name}", file=sys.stderr)
    print(f"  Source:     {result.source_path}", file=sys.stderr)
    print(f"  Initial:    {result.initial_percent:.2f}%", file=sys.stderr)
    print(f"  Final:      {result.final_percent:.2f}%", file=sys.stderr)
    print(f"  Delta:      +{result.total_delta:.2f}%", file=sys.stderr)
    print(f"  Rounds:     {len(result.rounds)}", file=sys.stderr)
    print(f"  Stopped:    {result.stopped_reason}", file=sys.stderr)
    print(f"  Elapsed:    {result.elapsed_seconds:.1f}s", file=sys.stderr)

    if result.rounds:
        print(f"\n  Round history:", file=sys.stderr)
        for r in result.rounds:
            status = "IMPROVED" if r.improved else "no change"
            name = r.best_name or "-"
            print(
                f"    R{r.round_num}: {r.baseline:.2f}% -> {r.best_score:.2f}% "
                f"({r.num_variants} variants, {status}, {name})",
                file=sys.stderr,
            )


if __name__ == "__main__":
    main()
