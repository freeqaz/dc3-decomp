"""Composition layer — apply two patterns in sequence.

Enables multi-step fixes: e.g. extract a call into `auto` (variable_extraction),
then reorder that new declaration (declaration_reorder). Each pattern sees a
fresh AST from re-parsing the previous stage's output.
"""

from __future__ import annotations

from typing import Iterator

from .extractor import reparse_variant
from .patterns.base import Pattern
from .types import FunctionContext, Variant

_DEFAULT_PAIRS: list[tuple[str, str]] = [
    ("variable_extraction", "declaration_reorder"),
    ("inline_assignment", "comparison_flip"),
    ("comparison_equivalence", "signed_unsigned"),
]


def compose_variants(
    ctx: FunctionContext,
    stage_a: Pattern,
    stage_b: Pattern,
    max_per_stage: int = 10,
    max_total: int = 50,
) -> Iterator[Variant]:
    """Apply pattern B to each output of pattern A.

    1. Generate up to max_per_stage variants from stage_a.
    2. For each, reparse_variant() to get a fresh AST.
    3. Run stage_b on the re-parsed context.
    4. Yield combined Variant with name like "varext_0+declreorder_3".
    5. Stop after max_total total composed variants.

    Args:
        ctx: Original function context.
        stage_a: First pattern to apply.
        stage_b: Second pattern to apply.
        max_per_stage: Max variants to take from stage_a.
        max_total: Max total composed variants to yield.
    """
    total = 0
    a_count = 0

    for a_variant in stage_a.generate(ctx):
        if a_count >= max_per_stage:
            break
        a_count += 1

        # Re-parse the stage A output to get fresh AST nodes
        try:
            reparsed_ctx = reparse_variant(ctx, a_variant.source)
        except ValueError:
            # Stage A produced invalid syntax — skip silently
            continue

        # Check if stage B is relevant for the re-parsed context
        if reparsed_ctx.diagnosis and not stage_b.relevant(reparsed_ctx.diagnosis):
            continue

        b_count = 0
        for b_variant in stage_b.generate(reparsed_ctx):
            if b_count >= max_per_stage:
                break
            b_count += 1

            yield Variant(
                name=f"{a_variant.name}+{b_variant.name}",
                pattern_name=f"compose:{stage_a.name}+{stage_b.name}",
                description=f"{a_variant.description} then {b_variant.description}",
                source=b_variant.source,
            )
            total += 1
            if total >= max_total:
                return

    return
