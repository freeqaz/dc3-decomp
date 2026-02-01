"""Variant generator — applies patterns to a function context."""

from __future__ import annotations

from typing import Iterator

from .types import FunctionContext, Variant
from .patterns.base import Pattern


def generate_variants(
    ctx: FunctionContext,
    patterns: list[Pattern],
    max_variants: int = 100,
) -> Iterator[Variant]:
    """Apply patterns to a function context and yield variants.

    Each pattern generates variants independently from the original source.
    Stops after max_variants total.
    """
    count = 0
    for pattern in patterns:
        for variant in pattern.generate(ctx):
            yield variant
            count += 1
            if count >= max_variants:
                return
