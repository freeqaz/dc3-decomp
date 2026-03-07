"""Initializer literal normalization — normalize literal forms in initializer lists.

Constructor initializer lists may use different literal forms that produce
different codegen: 0.0f vs 0, false vs 0, NULL vs 0, nullptr vs 0.

Example:
    Foo() : mVal(0.0f), mFlag(false) {}
    ->
    Foo() : mVal(0), mFlag(0) {}
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant


# Literal forms to try swapping between
_ZERO_FORMS = [b"0", b"0.0f", b"0.0", b"false", b"NULL", b"nullptr"]


class InitializerLiteralPattern(Pattern):
    name = "initializer_literal"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Only relevant when float load/store mismatches are present
        _FP_OPS = {"lfs", "stfs", "lfd", "stfd"}
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FP_OPS or d.base_opcode in _FP_OPS:
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        for stmt in ctx.statements:
            for node in walk(stmt):
                # Look in field_initializer_list (constructor init lists)
                if node.type == "field_initializer_list":
                    for variant in _swap_literals_in_node(node, source, counter):
                        yield variant
                        counter += 1

                # Also handle regular initializers in declarations
                if node.type == "init_declarator":
                    for variant in _swap_literals_in_node(node, source, counter):
                        yield variant
                        counter += 1


def _swap_literals_in_node(
    node: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Find zero-like literals and swap to alternative forms."""
    for child in walk(node):
        if child.type == "number_literal":
            text = child.text
            if text in (b"0", b"0.0f", b"0.0"):
                for replacement in _ZERO_FORMS:
                    if replacement != text:
                        new_source = (
                            source[:child.start_byte]
                            + replacement
                            + source[child.end_byte:]
                        )
                        yield Variant(
                            name=f"initlit_{counter}",
                            pattern_name="initializer_literal",
                            description=f"Replace {text.decode()} with {replacement.decode()} in initializer",
                            source=new_source,
                        )
                        counter += 1

        elif child.type in ("false", "true"):
            text = child.text
            if text == b"false":
                new_source = (
                    source[:child.start_byte]
                    + b"0"
                    + source[child.end_byte:]
                )
                yield Variant(
                    name=f"initlit_{counter}",
                    pattern_name="initializer_literal",
                    description="Replace false with 0 in initializer",
                    source=new_source,
                )
                counter += 1

        elif child.type == "null" or (child.type == "identifier" and child.text in (b"NULL", b"nullptr")):
            text = child.text
            new_source = (
                source[:child.start_byte]
                + b"0"
                + source[child.end_byte:]
            )
            yield Variant(
                name=f"initlit_{counter}",
                pattern_name="initializer_literal",
                description=f"Replace {text.decode()} with 0 in initializer",
                source=new_source,
            )
            counter += 1
