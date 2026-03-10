"""Hill climber — iterative improve-apply-repeat loop for a single function.

Each round: extract function, get baseline with diagnosis, generate variants
(with composition), score all, apply best improvement, repeat. Stops on
100% match, plateau (N rounds without improvement), max rounds, or all noise.

By default enables beam search, Ghidra, m2c, chains, adaptive, constrained,
compose, and BSF-guided declaration reorder. Use --no-* flags to disable.

Usage:
    python -m scripts.permuter.hill_climber \
        --symbol "?Poll@LabelNumberTicker@@UAAXXZ"

    python -m scripts.permuter.hill_climber \
        --symbol "?Poll@LabelNumberTicker@@UAAXXZ" \
        --no-ghidra --no-beam --json
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from .classifier import classify_mismatches, format_classifications
from .composer import _DEFAULT_PAIRS, available_context_keys, build_adaptive_chains, get_compose_pairs
from .diagnosis import format_diagnosis_summary, is_all_noise
from .extractor import extract_function
from .file_util import (
    apply_file_updates,
    atomic_write_bytes,
    restore_tracked_files,
    SourceFileLock,
)
from .generator import generate_variants
from .patterns import get_all_patterns, get_pattern
from .scorer import Scorer
from .types import (
    ChainSpec,
    HillClimbResult,
    RoundHints,
    RoundResult,
    ScoreResult,
    variant_file_updates,
)
from .validator import (
    ValidationResult,
    ValidationTier,
    format_result as format_validation_result,
    format_tier_distribution,
    validate_batch,
    validate_variant as run_validation,
)

# Re-export so the except clause can reference it without a deferred import
from .ghidra_cache import GhidraCircuitOpen as _GhidraCircuitOpen

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


REPO_ROOT = Path(__file__).resolve().parent.parent.parent

_BANNER_START = b"/* ===== PERMUTER LOCK"
_BANNER_END = b"===== */\n"


def _verify_build(source_path: Path) -> tuple[bool, str | None]:
    """Verify source compiles via ninja. Returns (success, error_output)."""
    try:
        rel = source_path.relative_to(REPO_ROOT)
    except ValueError:
        rel = source_path
    obj_target = f"build/373307D9/{rel.with_suffix('.obj')}"
    result = subprocess.run(
        ["ninja", obj_target],
        capture_output=True, text=True,
        cwd=str(REPO_ROOT),
    )
    if result.returncode != 0:
        return False, (result.stderr or result.stdout).strip()
    return True, None


def _verify_match(symbol: str) -> float:
    """Run objdiff and return match%. Used for regression checking."""
    cmd = [
        "bin/objdiff-cli", "diff", "-p", ".", symbol,
        "-c", "functionRelocDiffs=none", "-f", "json",
    ]
    result = subprocess.run(
        cmd, capture_output=True, text=True, cwd=str(REPO_ROOT),
    )
    try:
        data = json.loads(result.stdout)
        return data.get("fuzzy_match_percent", 0.0)
    except (json.JSONDecodeError, KeyError):
        return 0.0


def _make_banner(function_name: str) -> bytes:
    """Build the lock banner comment block."""
    now = datetime.now().strftime("%Y-%m-%d %H:%M")
    return (
        f"/* ===== PERMUTER LOCK — DO NOT EDIT =====\n"
        f" * The source permuter is actively working on: {function_name}\n"
        f" * Started: {now} (stale after 5 minutes)\n"
        f" * This banner is temporary and will be removed automatically.\n"
        f" ===== */\n"
    ).encode()


def _add_banner(path: Path, function_name: str) -> None:
    """Add a permuter lock banner to the top of a source file."""
    content = path.read_bytes()
    if _BANNER_START in content:
        return  # Already has a banner
    atomic_write_bytes(path, _make_banner(function_name) + content)


def _strip_banner(path: Path) -> None:
    """Remove the permuter lock banner from a file if present."""
    content = path.read_bytes()
    if _BANNER_START not in content:
        return
    start = content.index(_BANNER_START)
    end = content.index(_BANNER_END, start) + len(_BANNER_END)
    atomic_write_bytes(path, content[:start] + content[end:])


def _strip_banner_bytes(data: bytes) -> bytes:
    """Remove the permuter lock banner from source bytes."""
    if _BANNER_START not in data:
        return data
    start = data.index(_BANNER_START)
    end = data.index(_BANNER_END, start) + len(_BANNER_END)
    return data[:start] + data[end:]


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
        "--compose", action="store_true", default=True,
        help="Enable two-step pattern composition (default: True)",
    )
    parser.add_argument(
        "--no-compose", action="store_false", dest="compose",
        help="Disable two-step pattern composition",
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
        "--ghidra", action="store_true", default=True,
        help="Enable Ghidra-guided patterns (default: True)",
    )
    parser.add_argument(
        "--no-ghidra", action="store_false", dest="ghidra",
        help="Disable Ghidra-guided patterns",
    )
    parser.add_argument(
        "--m2c", action="store_true", default=True,
        help="Enable m2c-guided context loading (default: True)",
    )
    parser.add_argument(
        "--no-m2c", action="store_false", dest="m2c",
        help="Disable m2c-guided context loading",
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
        "--constrained", action="store_true", default=True,
        help="Enable constraint-directed synthesis pre-pass (default: True)",
    )
    parser.add_argument(
        "--no-constrained", action="store_false", dest="constrained",
        help="Disable constraint-directed synthesis",
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
    parser.add_argument(
        "--beam", action="store_true", default=True,
        help="Use beam search (default: True). Use --no-beam for greedy hill climbing.",
    )
    parser.add_argument(
        "--no-beam", action="store_false", dest="beam",
        help="Disable beam search, use greedy hill climbing",
    )
    parser.add_argument(
        "--beam-width", type=int, default=8,
        help="Beam width — survivors per depth (default: 8)",
    )
    parser.add_argument(
        "--beam-depth", type=int, default=4,
        help="Beam depth — expansion rounds (default: 4)",
    )
    parser.add_argument(
        "--beam-expand", type=int, default=24,
        help="Proposals per state per depth (default: 24)",
    )
    parser.add_argument(
        "--beam-escape", type=int, default=4,
        help="Escape budget for stagnating beam slots (default: 4)",
    )
    parser.add_argument(
        "--beam-diversity", type=int, default=3,
        help="Min distinct pattern families in beam (default: 3)",
    )
    parser.add_argument(
        "--validate", action="store_true", default=True,
        help="Show per-variant validation tiers and tier distribution summary (default: True)",
    )
    parser.add_argument(
        "--no-validate", action="store_false", dest="validate",
        help="Disable validation tier display",
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
    m2c: bool = False,
    chain: bool = False,
    chain_depth: int = 3,
    adaptive: bool = False,
    constrained: bool = False,
    validate: bool = True,
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
        validate: Show per-variant validation tiers in output.

    Returns:
        HillClimbResult with full session history.
    """
    # --constrained implies --ghidra
    if constrained:
        ghidra = True
    # --chain implies --compose
    if chain:
        compose = True
    # compose_pairs are now generated dynamically per round (see below)

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
    last_validation_tier = 0
    result_codegen_shapes: list[str] = []
    result_fact_boosts: list[str] = []
    result_fact_suppresses: list[str] = []
    all_validation_results: list[ValidationResult] = []

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
    applied_file_originals: dict[Path, bytes | None] = {
        source_path.resolve(): original_source,
    }
    applied_file_paths: set[Path] = {source_path.resolve()}

    # Cache RB3 source once before the round loop (avoids per-round file I/O)
    _rb3_source_cache: str | None = None
    _parts = symbol.split('@') if symbol else []
    _rb3_method = _parts[0].lstrip('?') if _parts else ''
    _rb3_class = _parts[1] if len(_parts) >= 2 else ''
    if _rb3_class and _rb3_method:
        try:
            from scripts.orchestrator import rb3_pairing
            _rb3_file = rb3_pairing.find_rb3_file(str(source_path))
            if _rb3_file:
                _rb3_text = _rb3_file.read_text(errors='replace')
                _rb3_source_cache = rb3_pairing.extract_rb3_method(
                    _rb3_text, _rb3_class, _rb3_method
                )
                if _rb3_source_cache:
                    print(
                        f"RB3 hint: {_rb3_class}::{_rb3_method}"
                        f" ({len(_rb3_source_cache)} chars)",
                        file=sys.stderr,
                    )
        except Exception:
            pass

    # Add lock banner so other agents know this file is being permuted
    _add_banner(source_path, function_name)

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
                baseline = scorer.get_baseline(guided=True, ghidra=ghidra, m2c=m2c)

                if round_num == 1:
                    initial_percent = baseline
                current_percent = baseline

                print(f"Baseline: {baseline:.2f}%", file=sys.stderr)

                # Wire symbol, diagnosis, and Ghidra data into context
                ctx.symbol = symbol
                ctx.rb3_source = _rb3_source_cache
                ctx.m2c_code = scorer.m2c_code
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
                    if round_num == 1:
                        classifications = classify_mismatches(scorer.diagnosis)
                        if classifications:
                            print(
                                format_classifications(classifications),
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

                # Target facts from attribution, atlas, and derived PPC shape facts
                try:
                    from .target_facts import extract_facts
                    from .compiler_atlas import lookup_for_diagnosis

                    atlas_entries = []
                    if scorer.diagnosis:
                        atlas_entries = lookup_for_diagnosis(
                            diff_ops=getattr(scorer.diagnosis, 'diff_ops', None),
                            reg_swap_pairs=getattr(scorer.diagnosis, 'reg_swap_pairs', None),
                            has_prologue_mismatch=getattr(scorer.diagnosis, 'has_prologue_mismatch', False),
                        )

                    attrib_regions = None
                    try:
                        attrib_regions = scorer.get_attribution()
                    except Exception:
                        pass
                    if attrib_regions is not None:
                        ctx.mismatch_regions = attrib_regions

                    shape_facts = None
                    try:
                        shape_facts = scorer.get_shape_facts()
                    except Exception:
                        pass

                    ctx.target_facts = extract_facts(
                        diagnosis=scorer.diagnosis,
                        regions=attrib_regions,
                        atlas_entries=atlas_entries or None,
                        shape_facts=shape_facts,
                        ghidra_ast=ctx.ghidra_ast,
                        rb3_source=ctx.rb3_source,
                    )
                    if ctx.target_facts is not None:
                        result_codegen_shapes = [
                            fact.payload.get("shape_category")
                            for fact in ctx.target_facts.by_kind("codegen_shape")
                            if fact.payload.get("shape_category")
                        ]
                        boost, suppress = ctx.target_facts.pattern_recommendations()
                        result_fact_boosts = sorted(boost)
                        result_fact_suppresses = sorted(suppress)
                    if round_num == 1 and ctx.target_facts is not None:
                        for line in ctx.target_facts.summary_lines():
                            print(line, file=sys.stderr)
                except Exception:
                    ctx.target_facts = None

                # Ghidra preflight check
                if ghidra and ctx.ghidra_ast and round_num == 1:
                    from .ghidra_preflight import run_preflight
                    preflight = run_preflight(
                        ctx.ghidra_ast, ctx.func_node, ctx.file_source,
                        diagnosis=scorer.diagnosis,
                        symbol=symbol,
                        file_path=ctx.file_path,
                    )
                    has_preflight_signals = (
                        bool(preflight.struct_offset_mismatches)
                        or bool(preflight.extra_calls)
                        or bool(preflight.missing_calls)
                        or bool(preflight.dead_variables)
                        or preflight.prologue_mismatch
                        or preflight.volatile_regswap_only
                    )
                    if preflight.skip_reason:
                        print(
                            f"  [GHIDRA PREFLIGHT] {preflight.skip_reason} "
                            f"(confidence={preflight.confidence:.1f})",
                            file=sys.stderr,
                        )
                    if ghidra_run_stats:
                        ghidra_run_stats.preflight_flagged = has_preflight_signals
                        ghidra_run_stats.preflight_reason = preflight.skip_reason
                        ghidra_run_stats.preflight_confidence = preflight.confidence
                        ghidra_run_stats.preflight_struct_offsets = len(
                            preflight.struct_offset_mismatches
                        )
                        ghidra_run_stats.preflight_extra_calls = len(preflight.extra_calls)
                        ghidra_run_stats.preflight_missing_calls = len(
                            preflight.missing_calls
                        )
                        ghidra_run_stats.preflight_dead_vars = len(preflight.dead_variables)
                        ghidra_run_stats.preflight_prologue_mismatch = (
                            preflight.prologue_mismatch
                        )
                        ghidra_run_stats.preflight_volatile_only = (
                            preflight.volatile_regswap_only
                        )
                        ghidra_run_stats.preflight_hard_skip = preflight.hard_skip
                    if has_preflight_signals:
                        print(
                            "  [GHIDRA PREFLIGHT DETAIL] "
                            f"offsets={len(preflight.struct_offset_mismatches)} "
                            f"extra_calls={len(preflight.extra_calls)} "
                            f"missing_calls={len(preflight.missing_calls)} "
                            f"dead_vars={len(preflight.dead_variables)} "
                            f"prologue={int(preflight.prologue_mismatch)} "
                            f"volatile_only={int(preflight.volatile_regswap_only)} "
                            f"hard_skip={int(preflight.hard_skip)}",
                            file=sys.stderr,
                        )
                    # Hard skip: very high confidence unfixable
                    if preflight.hard_skip:
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

                # Ghidra structural diff (round 1 diagnostic)
                if ghidra and ctx.ghidra_code and ctx.body_node and round_num == 1:
                    try:
                        from .ghidra_source_diff import (
                            diff_ghidra_vs_source,
                            format_source_diff,
                        )
                        sdiff = diff_ghidra_vs_source(
                            ctx.ghidra_code, ctx.file_source, ctx.body_node,
                        )
                        if (sdiff.missing_calls or sdiff.extra_calls
                                or sdiff.guard_diffs or sdiff.control_flow_diff):
                            print(
                                f"  [GHIDRA DIFF]\n"
                                + "\n".join(
                                    f"    {line}"
                                    for line in format_source_diff(sdiff).splitlines()
                                ),
                                file=sys.stderr,
                            )
                    except Exception:
                        pass  # Non-critical diagnostic

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
                                atomic_write_bytes(source_path, synth_best.variant.source)
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

                # Build dynamic compose pairs for this round
                compose_pairs: list[tuple[str, str]] | None = None
                if compose:
                    compose_pairs = get_compose_pairs(
                        diagnosis=ctx.diagnosis,
                        patterns=patterns,
                        hints=round_hints if adaptive else None,
                        available_context=available_context_keys(ctx),
                    )

                # Build adaptive chains for this round
                round_chains: list[ChainSpec] | None = None
                if chain and ctx.diagnosis:
                    round_chains = build_adaptive_chains(
                        diagnosis=ctx.diagnosis,
                        patterns=patterns,
                        hints=round_hints,
                        available_context=available_context_keys(ctx),
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

                # Collect build-failed patterns from previous round
                build_failed: set[str] | None = None
                if round_hints and round_hints.build_failed_patterns:
                    build_failed = round_hints.build_failed_patterns
                    print(
                        f"Suppressing {len(build_failed)} build-failed patterns "
                        f"from compose/chain: {', '.join(sorted(build_failed))}",
                        file=sys.stderr,
                    )

                # Generate variants
                variants = list(generate_variants(
                    ctx, patterns, max_variants,
                    compose_pairs=compose_pairs,
                    chains=round_chains,
                    round_hints=round_hints if adaptive else None,
                    failed_patterns=build_failed,
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

                # Validate all variants when --validate is enabled
                round_validation_results: list[ValidationResult] = []
                if validate:
                    round_validation_results = validate_batch(
                        variants, batch_results,
                        baseline_score=baseline,
                        original_source=original_source,
                        target_facts=ctx.target_facts,
                    )

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

                    # Append validation tier to the line when enabled
                    vtier_str = ""
                    if validate and i < len(round_validation_results):
                        vr = round_validation_results[i]
                        vtier_str = f" [{vr.tier.name}]"

                    print(
                        f"  [{i + 1}/{len(batch_results)}] "
                        f"{result.variant.name}: "
                        f"{result.match_percent:.2f}%{marker}{vtier_str}",
                        file=sys.stderr,
                    )

                # --- Phase 4: Cross-variant composition ---
                from .composer import select_improvers, cross_compose_variants
                improvers = select_improvers(batch_results, baseline, max_k=5)
                if len(improvers) >= 2:
                    crosscompose_budget = min(30, max_variants // 3)
                    phase4_variants = list(cross_compose_variants(
                        original_ctx=ctx,
                        improvers=improvers,
                        patterns=patterns,
                        phase1_results=batch_results,
                        baseline=baseline,
                        max_per_improver=max(1, crosscompose_budget // len(improvers)),
                        max_total=crosscompose_budget,
                    ))
                    if phase4_variants:
                        print(
                            f"Phase 4: cross-composing {len(phase4_variants)} "
                            f"variants from {len(improvers)} improvers...",
                            file=sys.stderr,
                        )
                        phase4_results = scorer.score_batch(
                            phase4_variants, workers=workers,
                        )
                        batch_results.extend(phase4_results)
                        for result in phase4_results:
                            if result.match_percent > best_score:
                                best_result = result
                                best_score = result.match_percent
                                print(
                                    f"  Phase 4 NEW BEST: {result.variant.name}: "
                                    f"{result.match_percent:.2f}%",
                                    file=sys.stderr,
                                )
                            pattern_accumulator.record_variant(
                                pattern_name=result.variant.pattern_name,
                                variant_name=result.variant.name,
                                match_pct=result.match_percent,
                                baseline=baseline,
                                build_success=result.build_success,
                            )

                # --- Phase 5: Multi-variant merge ---
                improving_count = sum(
                    1 for r in batch_results
                    if r.build_success and r.match_percent > baseline
                )
                if improving_count >= 2:
                    from .merge import find_merge_candidates
                    merge_candidates = find_merge_candidates(
                        original=ctx.file_source,
                        results=batch_results,
                        baseline=baseline,
                        max_merge_attempts=15,
                    )
                    if merge_candidates:
                        print(
                            f"Phase 5: merging {len(merge_candidates)} "
                            f"candidates from {improving_count} improving variants...",
                            file=sys.stderr,
                        )
                        merge_results = scorer.score_batch(
                            merge_candidates, workers=workers,
                        )
                        batch_results.extend(merge_results)
                        for result in merge_results:
                            if result.match_percent > best_score:
                                best_result = result
                                best_score = result.match_percent
                                print(
                                    f"  Phase 5 NEW BEST: {result.variant.name}: "
                                    f"{result.match_percent:.2f}%",
                                    file=sys.stderr,
                                )
                            pattern_accumulator.record_variant(
                                pattern_name=result.variant.pattern_name,
                                variant_name=result.variant.name,
                                match_pct=result.match_percent,
                                baseline=baseline,
                                build_success=result.build_success,
                            )

                # Accumulate validation results for summary
                if validate and round_validation_results:
                    all_validation_results.extend(round_validation_results)

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
                    winner_variant=(
                        best_result.variant
                        if best_result and best_score > baseline
                        else None
                    ),
                )
                if ctx.diagnosis:
                    round_hints.last_diagnosis = ctx.diagnosis

            if improved:
                plateau_count = 0
                current_percent = best_score

                # Validate the winner (advisory — logs warnings but doesn't block)
                if best_result:
                    try:
                        vr = run_validation(
                            best_result.variant,
                            score_result=best_result,
                            baseline_score=baseline,
                            original_source=original_source,
                        )
                        last_validation_tier = int(vr.tier)
                        if validate:
                            print(
                                f"  [VALIDATE] {format_validation_result(vr)}",
                                file=sys.stderr,
                            )
                    except Exception:
                        pass  # Validation is advisory

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
                    applied_file_paths = apply_file_updates(
                        variant_file_updates(source_path, best_result.variant),
                        applied_file_originals,
                        current_paths=applied_file_paths,
                    )
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
    except _GhidraCircuitOpen:
        stopped_reason = "ghidra_down"
    except Exception as e:
        import traceback
        stopped_reason = "error"
        print(f"Error: {e}", file=sys.stderr)
        traceback.print_exc()
    finally:
        # If not applying or interrupted, restore original source
        if not apply or stopped_reason == "interrupted":
            restore_tracked_files(applied_file_originals)
        else:
            # Strip the lock banner from the kept (improved) source
            _strip_banner(source_path)

            # Post-apply verification: ensure applied changes compile and
            # don't regress below the initial match percentage.
            if current_percent > initial_percent:
                build_ok, build_err = _verify_build(source_path)
                if not build_ok:
                    print(
                        f"\nVERIFICATION FAILED: applied source doesn't compile "
                        f"— restoring original.\n  {(build_err or '')[:300]}",
                        file=sys.stderr,
                    )
                    restore_tracked_files(applied_file_originals)
                    current_percent = initial_percent
                    stopped_reason = "verification_failed"
                else:
                    actual_pct = _verify_match(symbol)
                    if actual_pct < initial_percent - 0.01:
                        print(
                            f"\nVERIFICATION FAILED: regression detected "
                            f"({actual_pct:.2f}% < initial {initial_percent:.2f}%) "
                            f"— restoring original.",
                            file=sys.stderr,
                        )
                        restore_tracked_files(applied_file_originals)
                        current_percent = initial_percent
                        stopped_reason = "verification_failed"
                    else:
                        current_percent = actual_pct

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
    except Exception as e:
        print(f"WARNING: pattern stats storage failed: {e}", file=sys.stderr)

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

    # Find the pattern that produced the last improvement
    winning_pattern = None
    for r in reversed(rounds):
        if r.improved and r.best_pattern:
            winning_pattern = r.best_pattern
            break

    # Build validation tier distribution
    validation_dist: dict[int, int] = {}
    if validate and all_validation_results:
        for vr in all_validation_results:
            t = int(vr.tier)
            validation_dist[t] = validation_dist.get(t, 0) + 1

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
        winning_pattern=winning_pattern,
        ghidra_stats=ghidra_run_stats,
        validation_tier=last_validation_tier,
        validation_distribution=validation_dist,
        codegen_shapes=result_codegen_shapes,
        fact_boost_patterns=result_fact_boosts,
        fact_suppress_patterns=result_fact_suppresses,
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

    if args.beam:
        from .beam_search import beam_search, BeamConfig
        config = BeamConfig(
            width=args.beam_width,
            depth=args.beam_depth,
            expand=args.beam_expand,
            escape=args.beam_escape,
            diversity=args.beam_diversity,
            workers=workers,
        )
        result = beam_search(
            symbol=args.symbol,
            source_path=args.source,
            function_name=args.function,
            patterns=patterns,
            config=config,
            apply=not args.no_apply,
            unit=args.unit,
            ghidra=args.ghidra,
            m2c=args.m2c,
            constrained=args.constrained,
            validate=args.validate,
        )
    elif args.evolutionary:
        from .evolutionary import evolve
        result = evolve(
            symbol=args.symbol,
            source_path=args.source,
            function_name=args.function,
            patterns=patterns,
            population_size=args.population_size,
            generations=args.generations,
            apply=not args.no_apply,
            unit=args.unit,
            workers=workers,
            ghidra=args.ghidra,
            m2c=args.m2c,
            chain=args.chain,
            chain_depth=args.chain_depth,
            adaptive=args.adaptive,
            constrained=args.constrained,
        )
    else:
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
            m2c=args.m2c,
            chain=args.chain,
            chain_depth=args.chain_depth,
            adaptive=args.adaptive,
            constrained=args.constrained,
            validate=args.validate,
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
    if result.codegen_shapes:
        print(f"  Shapes:     {', '.join(result.codegen_shapes)}", file=sys.stderr)
    if result.fact_boost_patterns:
        print(f"  Boosts:     {', '.join(result.fact_boost_patterns)}", file=sys.stderr)
    if result.fact_suppress_patterns:
        print(f"  Suppress:   {', '.join(result.fact_suppress_patterns)}", file=sys.stderr)

    # Validation tier (winner)
    _tier_names = {
        0: "INVALID", 1: "PARSE_OK", 2: "BUILD_OK", 3: "SCORE_IMPROVED",
        4: "REGION_IMPROVED", 5: "FACT_AGREED", 6: "SEMANTIC_OK",
    }
    if result.validation_tier > 0:
        tier_name = _tier_names.get(result.validation_tier, f"TIER_{result.validation_tier}")
        print(f"  Validation: {tier_name} ({result.validation_tier}/6)", file=sys.stderr)

    # Validation tier distribution across all variants
    if result.validation_distribution:
        parts = []
        for tier in range(6, -1, -1):
            count = result.validation_distribution.get(tier, 0)
            if count > 0:
                parts.append(f"{_tier_names.get(tier, f'T{tier}')}:{count}")
        if parts:
            print(f"  Tier dist:  {' '.join(parts)}", file=sys.stderr)

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
