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
import signal
import sys
import time
from pathlib import Path

from .composer import _DEFAULT_PAIRS, build_adaptive_chains
from .diagnosis import format_diagnosis_summary, is_all_noise
from .extractor import extract_function
from .generator import generate_variants
from .patterns import get_all_patterns, get_pattern
from .scorer import Scorer
from .types import ChainSpec, HillClimbResult, RoundHints, RoundResult, ScoreResult

# Graceful interrupt flag — set by SIGINT handler, checked between rounds
_interrupted = False


def _sigint_handler(signum, frame):
    """Handle Ctrl+C by setting flag for graceful shutdown."""
    global _interrupted
    if _interrupted:
        # Second Ctrl+C — force exit
        print("\nForce quit.", file=sys.stderr)
        raise KeyboardInterrupt
    _interrupted = True
    print("\nInterrupt received — finishing current operation and restoring source...",
          file=sys.stderr)


def install_signal_handler():
    """Install the graceful SIGINT handler. Returns the previous handler."""
    global _interrupted
    _interrupted = False
    return signal.signal(signal.SIGINT, _sigint_handler)


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
    parser.add_argument(
        "--ghidra", action="store_true",
        help="Enable Ghidra-guided patterns (lookup decomp.db cache)",
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
    parser.add_argument(
        "--constrained", action="store_true",
        help="Enable constraint-directed synthesis pre-pass (implies --ghidra)",
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
    ghidra: bool = False,
    chain: bool = False,
    chain_depth: int = 3,
    adaptive: bool = False,
    constrained: bool = False,
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
        ghidra: Enable Ghidra-guided patterns.
        chain: Enable N-stage pattern chains via beam search.
        chain_depth: Maximum chain depth for N-stage composition.
        adaptive: Enable adaptive per-round pattern suppression/boosting.
        constrained: Enable constraint-directed synthesis pre-pass.

    Returns:
        HillClimbResult with full session history.
    """
    # --constrained implies --ghidra
    if constrained:
        ghidra = True
    # --chain implies --compose
    if chain:
        compose = True
    compose_pairs = _DEFAULT_PAIRS if compose else None

    # Create adaptive hints tracker when chain or adaptive is enabled
    round_hints: RoundHints | None = None
    if chain or adaptive:
        round_hints = RoundHints()
    start_time = time.time()
    rounds: list[RoundResult] = []
    initial_percent = 0.0
    current_percent = 0.0
    plateau_count = 0
    stopped_reason = "max_rounds"

    # Pattern stats tracking
    from .pattern_stats import RunStatsAccumulator
    pattern_accumulator = RunStatsAccumulator()

    # Ghidra stats tracking
    ghidra_run_stats = None
    if ghidra:
        from .ghidra_stats import GhidraRunStats
        ghidra_run_stats = GhidraRunStats()

    # Save original source for rollback if not applying
    original_source = source_path.read_bytes()

    try:
        for round_num in range(1, max_rounds + 1):
            if _interrupted:
                stopped_reason = "interrupted"
                print("\nInterrupted — stopping gracefully.", file=sys.stderr)
                break

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
                baseline = scorer.get_baseline(guided=True, ghidra=ghidra)

                if round_num == 1:
                    initial_percent = baseline
                current_percent = baseline

                print(f"Baseline: {baseline:.2f}%", file=sys.stderr)

                # Wire symbol, diagnosis, and Ghidra data into context
                ctx.symbol = symbol
                if scorer.ghidra_code:
                    ctx.ghidra_code = scorer.ghidra_code
                    ctx.ghidra_ast = scorer.ghidra_ast
                    if scorer.ghidra_ast:
                        from .ghidra_ast import (
                            extract_variable_first_use_order,
                            extract_savegpr_count,
                        )
                        ctx.target_var_order = extract_variable_first_use_order(
                            scorer.ghidra_ast
                        )
                        ctx.target_gpr_saves = extract_savegpr_count(
                            scorer.ghidra_code
                        )
                    # Wire ASM listing path for Ghidra+ASM crossref
                    if scorer.asm_listing_path:
                        ctx.asm_listing_path = scorer.asm_listing_path
                    # Track Ghidra stats (only on first round)
                    if ghidra_run_stats and round_num == 1:
                        ghidra_run_stats.ghidra_available = True
                        ghidra_run_stats.ghidra_code_bytes = len(scorer.ghidra_code)
                        if ctx.target_var_order:
                            ghidra_run_stats.ghidra_vars_count = len(ctx.target_var_order)
                        ghidra_run_stats.ghidra_gpr_saves = ctx.target_gpr_saves
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

                # Ghidra preflight check
                if ghidra and ctx.ghidra_ast and round_num == 1:
                    from .ghidra_preflight import run_preflight
                    preflight = run_preflight(
                        ctx.ghidra_ast, ctx.func_node, ctx.file_source,
                        diagnosis=scorer.diagnosis,
                        symbol=symbol,
                    )
                    if preflight.skip_reason:
                        print(
                            f"  [GHIDRA PREFLIGHT] {preflight.skip_reason} "
                            f"(confidence={preflight.confidence:.1f})",
                            file=sys.stderr,
                        )
                    if ghidra_run_stats:
                        ghidra_run_stats.preflight_flagged = preflight.skip_reason is not None
                        ghidra_run_stats.preflight_reason = preflight.skip_reason
                        ghidra_run_stats.preflight_confidence = preflight.confidence
                    # Hard skip: very high confidence unfixable
                    if preflight.confidence >= 0.9:
                        print(
                            f"  [GHIDRA PREFLIGHT] Unfixable — skipping",
                            file=sys.stderr,
                        )
                        stopped_reason = "unfixable"
                        break
                    # High-confidence: reduce rounds
                    if preflight.confidence >= 0.6:
                        max_rounds = min(max_rounds, 2)
                        print(
                            f"  [GHIDRA PREFLIGHT] Reducing to {max_rounds} rounds",
                            file=sys.stderr,
                        )

                # Constraint-directed synthesis pre-pass (round 1 only)
                if constrained and round_num == 1 and ctx.ghidra_ast is not None:
                    from .constraint_solver import synthesize
                    synthesis = synthesize(ctx)

                    if synthesis.skip_reason:
                        print(
                            f"  [SYNTH] Unfixable: {synthesis.skip_reason}",
                            file=sys.stderr,
                        )
                        stopped_reason = "unfixable"
                        break

                    if synthesis.variants:
                        print(
                            f"  [SYNTH] {len(synthesis.variants)} candidates "
                            f"({synthesis.deterministic_edit_count} resolved, "
                            f"{synthesis.free_variable_count} free)",
                            file=sys.stderr,
                        )
                        synth_results = scorer.score_batch(
                            synthesis.variants, workers=workers,
                        )
                        synth_best = max(
                            synth_results, key=lambda r: r.match_percent,
                        )
                        if synth_best.match_percent > baseline:
                            if apply:
                                source_path.write_bytes(synth_best.variant.source)
                            print(
                                f"  [SYNTH] {baseline:.2f}% -> "
                                f"{synth_best.match_percent:.2f}%",
                                file=sys.stderr,
                            )
                            if synth_best.match_percent >= 100.0:
                                stopped_reason = "perfect"
                                current_percent = 100.0
                                rounds.append(RoundResult(
                                    round_num=0,
                                    baseline=baseline,
                                    best_name=synth_best.variant.name,
                                    best_pattern="constraint_solver",
                                    best_score=100.0,
                                    delta=100.0 - baseline,
                                    num_variants=len(synthesis.variants),
                                    improved=True,
                                ))
                                break
                            # Update baseline for subsequent rounds
                            baseline = synth_best.match_percent
                            current_percent = baseline
                        else:
                            print(
                                f"  [SYNTH] No improvement from synthesis",
                                file=sys.stderr,
                            )

                # Perfect match check
                if baseline >= 100.0:
                    print("Already at 100%!", file=sys.stderr)
                    stopped_reason = "perfect"
                    break

                # Build adaptive chains for this round
                round_chains: list[ChainSpec] | None = None
                if chain and ctx.diagnosis:
                    round_chains = build_adaptive_chains(
                        diagnosis=ctx.diagnosis,
                        patterns=patterns,
                        hints=round_hints,
                        max_depth=chain_depth,
                    )
                    if round_chains:
                        print(
                            f"Built {len(round_chains)} chains: "
                            + ", ".join(
                                f"[{'+'.join(c.stages)}]" for c in round_chains
                            ),
                            file=sys.stderr,
                        )

                # Generate variants
                variants = list(generate_variants(
                    ctx, patterns, max_variants,
                    compose_pairs=compose_pairs,
                    chains=round_chains,
                    round_hints=round_hints if adaptive else None,
                ))
                print(
                    f"Generated {len(variants)} variants",
                    file=sys.stderr,
                )

                # Track Ghidra variant counts
                if ghidra_run_stats:
                    ghidra_run_stats.total_variants += len(variants)
                    ghidra_run_stats.ghidra_variants_generated += sum(
                        1 for v in variants if v.name.startswith("ghidra_")
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

                    # Record pattern stats
                    pattern_accumulator.record_variant(
                        pattern_name=result.variant.pattern_name,
                        variant_name=result.variant.name,
                        match_pct=result.match_percent,
                        baseline=baseline,
                        build_success=result.build_success,
                    )

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

            # Update adaptive round hints
            if round_hints:
                round_hints.record_round(
                    round_num=round_num,
                    variant_results=batch_results,
                    baseline=baseline,
                    winner_pattern=(
                        best_result.variant.pattern_name
                        if best_result and best_score > baseline
                        else None
                    ),
                )
                if ctx.diagnosis:
                    round_hints.last_diagnosis = ctx.diagnosis

            if improved:
                plateau_count = 0
                current_percent = best_score

                # Track winning pattern
                if best_result:
                    pattern_accumulator.mark_winner(best_result.variant.pattern_name)

                # Track if Ghidra-guided variant won
                if ghidra_run_stats and best_result:
                    if best_result.variant.name.startswith("ghidra_"):
                        ghidra_run_stats.ghidra_winner = True
                        ghidra_run_stats.winning_variant = best_result.variant.name
                        ghidra_run_stats.winning_pattern = best_result.variant.pattern_name

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
    except KeyboardInterrupt:
        stopped_reason = "interrupted"
        print("\nInterrupted — restoring source and stopping.", file=sys.stderr)
    except Exception as e:
        import traceback
        stopped_reason = "error"
        print(f"Error: {e}", file=sys.stderr)
        traceback.print_exc()
    finally:
        # If not applying or interrupted, restore original source
        if not apply or stopped_reason == "interrupted":
            source_path.write_bytes(original_source)

    elapsed = time.time() - start_time
    final_percent = current_percent if apply else initial_percent

    # Store pattern stats to DB
    try:
        from .pattern_stats import store_run as store_pattern_run
        store_pattern_run(
            accumulator=pattern_accumulator,
            symbol=symbol,
            function_name=function_name,
            source_path=str(source_path),
            initial_pct=initial_percent,
            final_pct=final_percent,
            unit=unit,
            caller="hill_climber",
        )
    except Exception:
        pass  # Don't fail the run if stats storage fails

    # Store Ghidra stats to DB
    if ghidra_run_stats:
        try:
            from .ghidra_stats import store_run
            store_run(
                symbol=symbol,
                function_name=function_name,
                run=ghidra_run_stats,
                initial_pct=initial_percent,
                final_pct=final_percent,
            )
        except Exception:
            pass  # Don't fail the run if stats storage fails

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
        ghidra_stats=ghidra_run_stats,
    )


def main():
    args = parse_args()
    install_signal_handler()

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
        ghidra=args.ghidra,
        chain=args.chain,
        chain_depth=args.chain_depth,
        adaptive=args.adaptive,
        constrained=args.constrained,
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

    # Ghidra stats
    gs = result.ghidra_stats
    if gs:
        parts = []
        if gs.ghidra_available:
            parts.append(f"cache={gs.ghidra_code_bytes}B")
            parts.append(f"vars={gs.ghidra_vars_count}")
            if gs.ghidra_gpr_saves is not None:
                parts.append(f"gpr_saves={gs.ghidra_gpr_saves}")
        else:
            parts.append("no cached decompilation")
        if gs.ghidra_variants_generated > 0:
            parts.append(f"guided_variants={gs.ghidra_variants_generated}/{gs.total_variants}")
        if gs.ghidra_winner:
            parts.append(f"WINNER={gs.winning_variant}")
        print(f"  Ghidra:     {', '.join(parts)}", file=sys.stderr)

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
