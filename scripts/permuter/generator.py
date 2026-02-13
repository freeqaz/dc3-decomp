"""Variant generator — applies patterns to a function context."""

from __future__ import annotations

import sys
from typing import Iterator

from .types import Diagnosis, FunctionContext, Variant
from .patterns.base import Pattern, get_pattern
from .composer import compose_variants

# Historical win rates (from batch validation data)
_WIN_RATES: dict[str, float] = {
    "variable_extraction": 0.42,
    "signed_unsigned": 0.30,
    "inline_assignment": 0.22,
    "declaration_reorder": 0.20,
    "comparison_flip": 0.15,
    "comparison_equivalence": 0.10,
    "branch_polarity": 0.05,
    "ternary_swap": 0.05,
    "argument_swap": 0.05,
    "commutative_swap": 0.02,
    "empty_size_swap": 0.02,
    "fma_reorder": 0.02,
}
_MIN_BUDGET = 3  # minimum variants per relevant pattern


def allocate_budgets(
    patterns: list[Pattern],
    total_budget: int,
    diagnosis: Diagnosis | None,
) -> dict[str, int]:
    """Allocate variant budget proportionally by win rate.

    Each relevant pattern gets at least _MIN_BUDGET to avoid starvation.
    Irrelevant patterns (based on diagnosis) get 0.

    Args:
        patterns: List of pattern instances.
        total_budget: Total number of variants to distribute.
        diagnosis: Current diagnosis for relevance filtering (None = all relevant).

    Returns:
        Dict mapping pattern name to allocated budget.
    """
    # Determine which patterns are relevant
    relevant: list[Pattern] = []
    budgets: dict[str, int] = {}

    for pattern in patterns:
        if diagnosis and not pattern.relevant(diagnosis):
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

    # Distribute remaining budget proportionally by win rate
    remaining = total_budget - min_total
    weights = {p.name: _WIN_RATES.get(p.name, 0.01) for p in relevant}
    total_weight = sum(weights.values())

    for pattern in relevant:
        w = weights[pattern.name]
        extra = int(remaining * w / total_weight) if total_weight > 0 else 0
        budgets[pattern.name] = _MIN_BUDGET + extra

    # Distribute any rounding remainder to the highest-weight pattern
    allocated = sum(budgets[p.name] for p in relevant)
    if allocated < total_budget:
        best = max(relevant, key=lambda p: weights[p.name])
        budgets[best.name] += total_budget - allocated

    return budgets


def generate_variants(
    ctx: FunctionContext,
    patterns: list[Pattern],
    max_variants: int = 100,
    compose_pairs: list[tuple[str, str]] | None = None,
) -> Iterator[Variant]:
    """Apply patterns to a function context and yield variants.

    Phase 1: Independent variants with per-pattern budgets (allocated
    proportionally by historical win rate).

    Phase 2: Composed variants (when compose_pairs is provided), using
    remaining budget after phase 1.

    Args:
        ctx: Parsed function context.
        patterns: List of pattern instances to apply.
        max_variants: Maximum total variants to generate.
        compose_pairs: Optional list of (pattern_a_name, pattern_b_name) pairs
            for two-step composition. None = independent variants only.
    """
    # Budget split: 70% independent, 30% composed (when compose_pairs provided)
    if compose_pairs:
        independent_budget = int(max_variants * 0.7)
    else:
        independent_budget = max_variants

    budgets = allocate_budgets(patterns, independent_budget, ctx.diagnosis)

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

    # Phase 2: Composed variants (fills remaining budget)
    if compose_pairs and total < max_variants:
        remaining = max_variants - total
        pattern_map = {p.name: p for p in patterns}

        for name_a, name_b in compose_pairs:
            if remaining <= 0:
                break

            # Look up patterns — try local list first, fall back to registry
            try:
                stage_a = pattern_map.get(name_a) or get_pattern(name_a)
                stage_b = pattern_map.get(name_b) or get_pattern(name_b)
            except KeyError:
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
