"""Variant generator — applies patterns to a function context."""

from __future__ import annotations

import sys
from typing import Iterator

from .types import ChainSpec, Diagnosis, FunctionContext, RoundHints, Variant
from .patterns.base import Pattern, get_pattern
from .composer import compose_variants, chain_variants

_MIN_BUDGET = 3  # minimum variants per relevant pattern
_SAFETY_TIER_WEIGHTS = {
    "conservative": 1.05,
    "normal": 1.0,
    "moderate": 0.95,
    "aggressive": 0.85,
}


def _winner_domains(round_hints: RoundHints | None) -> set[str]:
    """Collect structural domains from the most recent winning pattern(s)."""
    if not round_hints or not round_hints.last_winner:
        return set()

    from .patterns.base import get_pattern

    domains: set[str] = set()
    for name in _split_pattern_names(round_hints.last_winner):
        try:
            domains.add(get_pattern(name).structural_domain)
        except KeyError:
            continue
    return domains


def _split_pattern_names(name: str) -> list[str]:
    """Split composed pattern names into base pattern names."""
    for prefix in ("compose:", "chain:", "crosscompose:", "merge:", "evo_cross:", "evo_mut:"):
        if name.startswith(prefix):
            _, parts = name.split(":", 1)
            return parts.split("+")
    return [name]


def _pattern_priorities(
    patterns: list[Pattern],
    diagnosis: Diagnosis | None,
    round_hints: RoundHints | None = None,
) -> dict[str, float]:
    """Compute effective per-pattern priorities for the current round."""
    priorities: dict[str, float] = {}
    winner_domains = _winner_domains(round_hints)

    for pattern in patterns:
        if diagnosis:
            priority = pattern.priority(diagnosis)
        else:
            priority = 1.0

        priority *= _SAFETY_TIER_WEIGHTS.get(pattern.safety_tier, 1.0)
        if winner_domains and pattern.structural_domain in winner_domains:
            priority *= 1.08

        if round_hints and priority > 0.0:
            priority *= round_hints.suppression_factor(pattern.name)
            priority *= round_hints.adaptive_priority_boost(pattern.name)

        priorities[pattern.name] = priority

    return priorities


def allocate_budgets(
    patterns: list[Pattern],
    total_budget: int,
    diagnosis: Diagnosis | None,
    round_hints: RoundHints | None = None,
) -> dict[str, int]:
    """Allocate variant budget proportionally by pattern priority.

    Uses each pattern's priority(diagnosis) score for budget weighting.
    When round_hints is provided, multiplies by suppression_factor to
    reduce budget for patterns that have failed consecutively.

    Patterns with priority 0.0 are skipped. Remaining patterns get at
    least _MIN_BUDGET variants each, with the rest distributed by
    priority weight.

    Args:
        patterns: List of pattern instances.
        total_budget: Total number of variants to distribute.
        diagnosis: Current diagnosis for priority scoring (None = all get 1.0).
        round_hints: Optional adaptive hints for suppression/boosting.

    Returns:
        Dict mapping pattern name to allocated budget.
    """
    budgets: dict[str, int] = {}
    priorities = _pattern_priorities(
        patterns,
        diagnosis,
        round_hints=round_hints,
    )
    relevant: list[Pattern] = []

    for pattern in patterns:
        p = priorities[pattern.name]
        priorities[pattern.name] = p
        if p <= 0.0:
            budgets[pattern.name] = 0
        else:
            relevant.append(pattern)

    if not relevant:
        return budgets

    # Ensure minimum budget per relevant pattern
    min_total = len(relevant) * _MIN_BUDGET
    if min_total >= total_budget:
        # Not enough budget for minimums — distribute evenly
        per_pattern = max(1, total_budget // len(relevant))
        for pattern in relevant:
            budgets[pattern.name] = per_pattern
        return budgets

    # Distribute remaining budget proportionally by priority
    remaining = total_budget - min_total
    total_weight = sum(priorities[p.name] for p in relevant)

    for pattern in relevant:
        w = priorities[pattern.name]
        extra = int(remaining * w / total_weight) if total_weight > 0 else 0
        budgets[pattern.name] = _MIN_BUDGET + extra

    # Distribute rounding remainder to the highest-priority pattern
    allocated = sum(budgets[p.name] for p in relevant)
    if allocated < total_budget:
        best = max(relevant, key=lambda p: priorities[p.name])
        budgets[best.name] += total_budget - allocated

    return budgets


def generate_variants(
    ctx: FunctionContext,
    patterns: list[Pattern],
    max_variants: int = 100,
    compose_pairs: list[tuple[str, str]] | None = None,
    chains: list[ChainSpec] | None = None,
    round_hints: RoundHints | None = None,
    failed_patterns: set[str] | None = None,
) -> Iterator[Variant]:
    """Apply patterns to a function context and yield variants.

    Phase 1: Independent variants with per-pattern budgets (allocated
    proportionally by pattern priority scores, with optional suppression).

    Phase 2: Composed 2-stage variants (when compose_pairs is provided).

    Phase 3: N-stage chain variants (when chains is provided), using
    beam search via chain_variants().

    Budget split depends on what's enabled:
    - No composition: 100% independent
    - compose_pairs only: 70/30 independent/composed
    - chains only: 80/20 independent/chains
    - Both: 50/20/30 independent/composed/chains

    Args:
        ctx: Parsed function context.
        patterns: List of pattern instances to apply.
        max_variants: Maximum total variants to generate.
        compose_pairs: Optional list of (pattern_a_name, pattern_b_name) pairs
            for two-step composition. None = no 2-stage composition.
        chains: Optional list of ChainSpec for N-stage beam search.
            None = no chain composition.
        round_hints: Optional adaptive hints for suppression/boosting.
        failed_patterns: Optional set of pattern names that failed to build
            in Phase 1. Suppresses these from Phase 2/3 first-stage.
    """
    # Budget split
    if compose_pairs and chains:
        independent_budget = int(max_variants * 0.50)
        compose_budget = int(max_variants * 0.20)
        chain_budget = max_variants - independent_budget - compose_budget
    elif compose_pairs:
        independent_budget = int(max_variants * 0.7)
        compose_budget = max_variants - independent_budget
        chain_budget = 0
    elif chains:
        independent_budget = int(max_variants * 0.8)
        compose_budget = 0
        chain_budget = max_variants - independent_budget
    else:
        independent_budget = max_variants
        compose_budget = 0
        chain_budget = 0

    budgets = allocate_budgets(
        patterns, independent_budget, ctx.diagnosis,
        round_hints=round_hints,
    )
    priorities = _pattern_priorities(
        patterns,
        ctx.diagnosis,
        round_hints=round_hints,
    )

    total = 0
    skipped: list[str] = []

    # Cross-phase source dedup: skip variants with identical source bytes
    seen_sources: set[int] = {hash(ctx.file_source)}  # include baseline
    dedup_count = 0

    # Phase 1: Independent variants with per-pattern budgets
    ordered_patterns = sorted(
        patterns,
        key=lambda pattern: (-priorities.get(pattern.name, 0.0), pattern.name),
    )
    for pattern in ordered_patterns:
        budget = budgets.get(pattern.name, 0)
        if budget == 0:
            skipped.append(pattern.name)
            continue
        count = 0
        for variant in pattern.generate(ctx):
            source_hash = hash(variant.source)
            if source_hash in seen_sources:
                dedup_count += 1
                continue
            seen_sources.add(source_hash)
            yield variant
            count += 1
            total += 1
            if count >= budget:
                break
            if total >= independent_budget:
                break
        if total >= independent_budget:
            break

    if skipped:
        print(
            f"Skipping (not relevant): {', '.join(skipped)}",
            file=sys.stderr,
        )

    # Phase 2: Composed 2-stage variants
    if compose_pairs and compose_budget > 0:
        remaining = compose_budget
        pattern_map = {p.name: p for p in patterns}

        for name_a, name_b in compose_pairs:
            if remaining <= 0:
                break
            # Skip if first-stage pattern failed to build in Phase 1
            if failed_patterns and name_a in failed_patterns:
                continue

            stage_a = pattern_map.get(name_a)
            stage_b = pattern_map.get(name_b)
            if stage_a is None or stage_b is None:
                continue

            for variant in compose_variants(
                ctx, stage_a, stage_b,
                max_per_stage=10,
                max_total=remaining,
            ):
                source_hash = hash(variant.source)
                if source_hash in seen_sources:
                    dedup_count += 1
                    continue
                seen_sources.add(source_hash)
                yield variant
                total += 1
                remaining -= 1
                if remaining <= 0:
                    break

    # Phase 3: N-stage chain variants (beam search)
    if chains and chain_budget > 0:
        pattern_map = {p.name: p for p in patterns}
        remaining = chain_budget

        for chain in chains:
            if remaining <= 0:
                break
            # Skip chains whose first stage failed to build in Phase 1
            if failed_patterns and chain.stages[0] in failed_patterns:
                continue

            per_chain = min(remaining, chain.budget)
            count = 0

            for variant in chain_variants(
                ctx, chain, pattern_map,
                beam_width=max(5, len(chain.stages) * 3),
                max_per_stage=8,
                max_total=per_chain,
            ):
                source_hash = hash(variant.source)
                if source_hash in seen_sources:
                    dedup_count += 1
                    continue
                seen_sources.add(source_hash)
                yield variant
                total += 1
                remaining -= 1
                count += 1
                if remaining <= 0:
                    break

            if count > 0:
                print(
                    f"Chain [{'+'.join(chain.stages)}]: {count} variants "
                    f"({chain.reason})",
                    file=sys.stderr,
                )

    if dedup_count > 0:
        print(
            f"Source dedup: {dedup_count} duplicate variants skipped",
            file=sys.stderr,
        )
