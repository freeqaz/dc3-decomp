"""Evolutionary optimizer — population-based search for decomp matching.

Explores multiple variant paths simultaneously via a population that
competes, crosses over (merging non-overlapping edits), and mutates
over generations. Avoids local optima that trap the greedy hill climber.

Usage (via hill_climber):
    python -m scripts.permuter.hill_climber \
        --symbol "?Poll@LabelNumberTicker@@UAAXXZ" --evolutionary
"""

from __future__ import annotations

import random
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from .composer import available_context_keys, build_adaptive_chains, get_compose_pairs
from .extractor import extract_function, reparse_variant
from .file_util import apply_file_updates, atomic_write_bytes, restore_tracked_files
from .generator import generate_variants
from .merge import EditSpan, edits_overlap, extract_edit_spans, merge_variants
from .scorer import Scorer
from .types import (
    ChainSpec,
    FunctionContext,
    HillClimbResult,
    RoundHints,
    RoundResult,
    ScoreResult,
    Variant,
    merge_auxiliary_file_sets,
    variant_file_updates,
    variant_identity_bytes,
)


@dataclass
class Individual:
    """A single member of the evolutionary population."""

    variant: Variant
    fitness: float  # match_percent (0-100)
    build_success: bool
    edit_spans: list[EditSpan] | None = None  # cached, computed lazily
    generation: int = 0

    @property
    def source_hash(self) -> str:
        return variant_identity_bytes(Path("/tmp/evolutionary.cpp"), self.variant).hex()


def _tournament_select(population: list[Individual], k: int = 3) -> Individual:
    """Pick k random individuals, return the fittest."""
    contestants = random.sample(population, min(k, len(population)))
    return max(contestants, key=lambda ind: ind.fitness)


def _get_edit_spans(ind: Individual, original: bytes) -> list[EditSpan]:
    """Get edit spans for an individual, caching the result."""
    if ind.edit_spans is None:
        ind.edit_spans = extract_edit_spans(original, ind.variant.source)
    return ind.edit_spans


def _crossover(
    original: bytes,
    parent_a: Individual,
    parent_b: Individual,
) -> Variant | None:
    """Attempt to merge two parents' edits. Returns None if overlapping."""
    spans_a = _get_edit_spans(parent_a, original)
    spans_b = _get_edit_spans(parent_b, original)

    if not spans_a or not spans_b:
        return None
    if edits_overlap(spans_a, spans_b):
        return None

    return merge_variants(
        original, parent_a.variant, parent_b.variant, spans_a, spans_b,
    )


def _mutate(
    original_ctx: FunctionContext,
    individual: Individual,
    patterns: list,
) -> Variant | None:
    """Apply a random pattern to an individual's source."""
    try:
        reparsed = reparse_variant(original_ctx, individual.variant.source)
    except ValueError:
        return None

    pattern = random.choice(patterns)
    try:
        variants = list(pattern.generate(reparsed))
    except Exception:
        return None

    if not variants:
        return None

    chosen = random.choice(variants)
    auxiliary_files = merge_auxiliary_file_sets(
        individual.variant.auxiliary_files,
        chosen.auxiliary_files,
    )
    if auxiliary_files is None:
        return None
    return Variant(
        name=f"evo_mut:{individual.variant.name}+{pattern.name}",
        pattern_name=f"evo_mut:{individual.variant.pattern_name}+{pattern.name}",
        description=f"Mutated: {chosen.description}",
        source=chosen.source,
        tags=individual.variant.tags | chosen.tags,
        auxiliary_files=auxiliary_files,
    )


def evolve(
    symbol: str,
    source_path: Path,
    function_name: str,
    patterns: list,
    population_size: int = 50,
    generations: int = 20,
    elite_count: int = 2,
    tournament_k: int = 3,
    crossover_rate: float = 0.7,
    mutation_rate: float = 0.3,
    stagnation_limit: int = 5,
    apply: bool = True,
    unit: str | None = None,
    workers: int = 6,
    ghidra: bool = False,
    chain: bool = False,
    chain_depth: int = 3,
    adaptive: bool = False,
    constrained: bool = False,
) -> HillClimbResult:
    """Run evolutionary optimization for a single function.

    Returns HillClimbResult for compatibility with existing reporting.
    """
    from .hill_climber import (
        _add_banner,
        _interrupted,
        _strip_banner,
        _verify_build,
        _verify_match,
    )

    if chain:
        compose = True
    else:
        compose = bool(patterns)  # always True if we have patterns

    start_time = time.time()
    original_source = source_path.read_bytes()
    rounds: list[RoundResult] = []
    initial_percent = 0.0
    best_ever_fitness = 0.0
    best_ever_individual: Individual | None = None
    stopped_reason = "max_generations"
    total_evaluations = 0
    stagnation_count = 0

    _add_banner(source_path, function_name)

    try:
        # Extract function context
        ctx = extract_function(source_path, function_name)

        with Scorer(source_path, symbol, unit=unit) as scorer:
            baseline = scorer.get_baseline(guided=True, ghidra=ghidra)
            initial_percent = baseline
            best_ever_fitness = baseline

            print(f"Baseline: {baseline:.2f}%", file=sys.stderr)

            # Wire context metadata
            ctx.symbol = symbol
            if scorer.ghidra_code:
                ctx.ghidra_code = scorer.ghidra_code
                ctx.ghidra_ast = scorer.ghidra_ast
            if scorer.diagnosis:
                ctx.diagnosis = scorer.diagnosis

            if baseline >= 100.0:
                print("Already at 100%!", file=sys.stderr)
                stopped_reason = "perfect"
                return _make_result(
                    symbol, function_name, source_path, initial_percent,
                    baseline, rounds, stopped_reason, start_time,
                )

            # Build compose pairs and chains for seeding
            compose_pairs = None
            if compose:
                compose_pairs = get_compose_pairs(
                    diagnosis=ctx.diagnosis,
                    patterns=patterns,
                    available_context=available_context_keys(ctx),
                )
            round_chains = None
            if chain and ctx.diagnosis:
                round_chains = build_adaptive_chains(
                    diagnosis=ctx.diagnosis,
                    patterns=patterns,
                    hints=None,
                    available_context=available_context_keys(ctx),
                    max_depth=chain_depth,
                )

            # Seed initial population
            print(
                f"Seeding population of {population_size}...",
                file=sys.stderr,
            )
            seed_variants = list(generate_variants(
                ctx, patterns, population_size,
                compose_pairs=compose_pairs,
                chains=round_chains,
            ))

            if not seed_variants:
                print("No variants generated — stopping.", file=sys.stderr)
                stopped_reason = "no_variants"
                return _make_result(
                    symbol, function_name, source_path, initial_percent,
                    baseline, rounds, stopped_reason, start_time,
                )

            # Score initial population
            seed_results = scorer.score_batch(seed_variants, workers=workers)
            total_evaluations += len(seed_results)

            population = _results_to_population(seed_results, generation=0)
            population = _dedup_population(population)

            # Track best from seed
            for ind in population:
                if ind.fitness > best_ever_fitness:
                    best_ever_fitness = ind.fitness
                    best_ever_individual = ind

            _log_generation(
                0, population, best_ever_fitness, initial_percent,
                prev_best=baseline,
            )

            if best_ever_fitness >= 100.0:
                stopped_reason = "perfect"
                rounds.append(_gen_to_round(0, baseline, best_ever_individual, population))
            else:
                rounds.append(_gen_to_round(0, baseline, best_ever_individual, population))

                # Track previous gen's top for stagnation detection
                prev_gen_top = max(
                    (ind.fitness for ind in population), default=baseline,
                )

                # Generation loop
                for gen in range(1, generations + 1):
                    if _interrupted:
                        stopped_reason = "interrupted"
                        print("\nInterrupted.", file=sys.stderr)
                        break

                    if best_ever_fitness >= 100.0:
                        stopped_reason = "perfect"
                        break

                    next_gen: list[Individual] = []
                    seen_hashes: set[str] = set()

                    # Elitism: keep top N
                    elites = sorted(
                        population, key=lambda i: i.fitness, reverse=True,
                    )[:elite_count]
                    for e in elites:
                        h = e.source_hash
                        if h not in seen_hashes:
                            seen_hashes.add(h)
                            next_gen.append(Individual(
                                variant=e.variant, fitness=e.fitness,
                                build_success=e.build_success,
                                edit_spans=e.edit_spans, generation=gen,
                            ))

                    # Crossover + mutation to fill population
                    new_variants: list[Variant] = []
                    crossover_attempts = 0
                    crossover_successes = 0
                    mutation_count = 0
                    max_attempts = population_size * 3

                    while len(new_variants) + len(next_gen) < population_size and crossover_attempts < max_attempts:
                        crossover_attempts += 1

                        if random.random() < crossover_rate and len(population) >= 2:
                            # Crossover
                            p_a = _tournament_select(population, tournament_k)
                            p_b = _tournament_select(population, tournament_k)
                            if p_a is p_b:
                                continue
                            child = _crossover(
                                original_source, p_a, p_b,
                            )
                            if child is None:
                                continue
                            crossover_successes += 1
                        else:
                            # Clone a parent for mutation
                            parent = _tournament_select(population, tournament_k)
                            child = parent.variant

                        # Maybe mutate
                        if random.random() < mutation_rate:
                            tmp_ind = Individual(
                                variant=child, fitness=0, build_success=True,
                            )
                            mutated = _mutate(ctx, tmp_ind, patterns)
                            if mutated is not None:
                                child = mutated
                                mutation_count += 1

                        h = variant_identity_bytes(source_path, child).hex()
                        if h not in seen_hashes:
                            seen_hashes.add(h)
                            new_variants.append(child)

                    # Fill remaining slots with fresh variants
                    fill_needed = population_size - len(next_gen) - len(new_variants)
                    if fill_needed > 0:
                        fill_variants = list(generate_variants(
                            ctx, patterns, fill_needed,
                            compose_pairs=compose_pairs,
                            chains=round_chains,
                        ))
                        for v in fill_variants:
                            h = variant_identity_bytes(source_path, v).hex()
                            if h not in seen_hashes:
                                seen_hashes.add(h)
                                new_variants.append(v)
                                if len(new_variants) + len(next_gen) >= population_size:
                                    break

                    # Score new variants
                    if new_variants:
                        new_results = scorer.score_batch(
                            new_variants, workers=workers,
                        )
                        total_evaluations += len(new_results)
                        new_inds = _results_to_population(
                            new_results, generation=gen,
                        )
                        next_gen.extend(new_inds)

                    population = _dedup_population(next_gen)

                    # Track best
                    gen_best = max(population, key=lambda i: i.fitness) if population else None
                    prev_best = best_ever_fitness
                    cur_gen_top = gen_best.fitness if gen_best else 0.0
                    if gen_best and gen_best.fitness > best_ever_fitness:
                        best_ever_fitness = gen_best.fitness
                        best_ever_individual = gen_best

                    # Stagnation: reset if this gen's top beats previous
                    # gen's top (not just best_ever — that's too strict)
                    if cur_gen_top > prev_gen_top + 0.01:
                        stagnation_count = 0
                    else:
                        stagnation_count += 1
                    prev_gen_top = cur_gen_top

                    _log_generation(
                        gen, population, best_ever_fitness, initial_percent,
                        prev_best=prev_best,
                        crossover_successes=crossover_successes,
                        mutations=mutation_count,
                    )

                    rounds.append(_gen_to_round(
                        gen, baseline, best_ever_individual, population,
                    ))

                    if stagnation_count >= stagnation_limit:
                        stopped_reason = "stagnation"
                        print(
                            f"Stagnation ({stagnation_limit} generations) — stopping.",
                            file=sys.stderr,
                        )
                        break

    except KeyboardInterrupt:
        stopped_reason = "interrupted"
    except Exception as e:
        import traceback
        stopped_reason = "error"
        print(f"Error: {e}", file=sys.stderr)
        traceback.print_exc()
    finally:
        # Apply best or restore
        final_percent = initial_percent
        if best_ever_individual and best_ever_fitness > initial_percent and apply:
            originals: dict[Path, bytes | None] = {
                source_path.resolve(): original_source,
            }
            apply_file_updates(
                variant_file_updates(source_path, best_ever_individual.variant),
                originals,
            )
            _strip_banner(source_path)

            # Verify
            build_ok, build_err = _verify_build(source_path)
            if not build_ok:
                print(
                    f"\nVERIFICATION FAILED: doesn't compile — restoring.",
                    file=sys.stderr,
                )
                restore_tracked_files(originals)
            else:
                actual_pct = _verify_match(symbol)
                if actual_pct < initial_percent - 0.01:
                    print(
                        f"\nVERIFICATION FAILED: regression — restoring.",
                        file=sys.stderr,
                    )
                    restore_tracked_files(originals)
                else:
                    final_percent = actual_pct
                    print(
                        f"Applied best: {best_ever_individual.variant.name} "
                        f"({initial_percent:.2f}% -> {final_percent:.2f}%)",
                        file=sys.stderr,
                    )
        else:
            atomic_write_bytes(source_path, original_source)
            if not apply and best_ever_individual and best_ever_fitness > initial_percent:
                final_percent = initial_percent  # Not applied
                print(
                    f"Best: {best_ever_individual.variant.name} "
                    f"({initial_percent:.2f}% -> {best_ever_fitness:.2f}%) [NOT APPLIED]",
                    file=sys.stderr,
                )

    elapsed = time.time() - start_time
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


def _results_to_population(
    results: list[ScoreResult], generation: int,
) -> list[Individual]:
    """Convert ScoreResults to Individuals."""
    return [
        Individual(
            variant=r.variant,
            fitness=r.match_percent,
            build_success=r.build_success,
            generation=generation,
        )
        for r in results
        if r.build_success
    ]


def _dedup_population(population: list[Individual]) -> list[Individual]:
    """Remove duplicate sources, keeping the first occurrence."""
    seen: set[str] = set()
    result: list[Individual] = []
    for ind in population:
        h = ind.source_hash
        if h not in seen:
            seen.add(h)
            result.append(ind)
    return result


def _gen_to_round(
    gen: int,
    baseline: float,
    best_individual: Individual | None,
    population: list[Individual],
) -> RoundResult:
    """Convert generation state to a RoundResult."""
    best_score = best_individual.fitness if best_individual else baseline
    return RoundResult(
        round_num=gen,
        baseline=baseline,
        best_name=best_individual.variant.name if best_individual else None,
        best_pattern=best_individual.variant.pattern_name if best_individual else None,
        best_score=best_score,
        delta=best_score - baseline,
        num_variants=len(population),
        improved=best_score > baseline,
    )


def _log_generation(
    gen: int,
    population: list[Individual],
    best_ever: float,
    initial_percent: float,
    prev_best: float = 0.0,
    crossover_successes: int = 0,
    mutations: int = 0,
):
    """Print generation summary."""
    if not population:
        print(f"  Gen {gen}: empty population", file=sys.stderr)
        return

    fitnesses = [ind.fitness for ind in population]
    avg = sum(fitnesses) / len(fitnesses)
    top = max(fitnesses)
    total_delta = best_ever - initial_percent
    gen_delta = best_ever - prev_best

    parts = [
        f"Gen {gen}: pop={len(population)}",
        f"avg={avg:.1f}%",
        f"top={top:.1f}%",
        f"best_ever={best_ever:.1f}% (+{total_delta:.1f}% total",
    ]
    if gen_delta > 0.01:
        parts[-1] += f", +{gen_delta:.1f}% this gen)"
    else:
        parts[-1] += ")"
    if crossover_successes:
        parts.append(f"cross={crossover_successes}")
    if mutations:
        parts.append(f"mut={mutations}")

    print(f"  {', '.join(parts)}", file=sys.stderr)


def _make_result(
    symbol, function_name, source_path, initial_percent, final_percent,
    rounds, stopped_reason, start_time,
) -> HillClimbResult:
    """Build a HillClimbResult (shorthand for early returns)."""
    return HillClimbResult(
        symbol=symbol,
        function_name=function_name,
        source_path=str(source_path),
        initial_percent=initial_percent,
        final_percent=final_percent,
        total_delta=final_percent - initial_percent,
        rounds=rounds,
        stopped_reason=stopped_reason,
        elapsed_seconds=round(time.time() - start_time, 2),
    )
