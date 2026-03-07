"""Variant generator — applies patterns to a function context."""

from __future__ import annotations

import sys
from typing import Iterator

from .types import ChainSpec, Diagnosis, FunctionContext, RoundHints, Variant
from .patterns.base import Pattern, get_pattern
from .composer import compose_variants, chain_variants

_MIN_BUDGET = 3  # minimum variants per relevant pattern


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
    priorities: dict[str, float] = {}
    relevant: list[Pattern] = []

    for pattern in patterns:
        if diagnosis:
            p = pattern.priority(diagnosis)
        else:
            p = 1.0

        # Apply suppression from round hints
        if round_hints and p > 0.0:
            p *= round_hints.suppression_factor(pattern.name)

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
    - Both: 60/20/20 independent/composed/chains

    Args:
        ctx: Parsed function context.
        patterns: List of pattern instances to apply.
        max_variants: Maximum total variants to generate.
        compose_pairs: Optional list of (pattern_a_name, pattern_b_name) pairs
            for two-step composition. None = no 2-stage composition.
        chains: Optional list of ChainSpec for N-stage beam search.
            None = no chain composition.
        round_hints: Optional adaptive hints for suppression/boosting.
    """
    # Budget split
    if compose_pairs and chains:
        independent_budget = int(max_variants * 0.6)
        compose_budget = int(max_variants * 0.2)
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

    total = 0
    skipped: list[str] = []

    # Phase 1: Independent variants with per-pattern budgets
    for pattern in patterns:
        budget = budgets.get(pattern.name, 0)
        if budget == 0:
            skipped.append(pattern.name)
            continue
        count = 0
        for variant in pattern.generate(ctx):
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

            stage_a = pattern_map.get(name_a)
            stage_b = pattern_map.get(name_b)
            if stage_a is None or stage_b is None:
                continue

            for variant in compose_variants(
                ctx, stage_a, stage_b,
                max_per_stage=10,
                max_total=remaining,
            ):
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

            per_chain = min(remaining, chain.budget)
            count = 0

            for variant in chain_variants(
                ctx, chain, pattern_map,
                beam_width=5,
                max_per_stage=8,
                max_total=per_chain,
            ):
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
