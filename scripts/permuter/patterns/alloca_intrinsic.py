"""Alloca intrinsic pattern — swap alloca() <-> _alloca().

MSVC uses _alloca() as the intrinsic name while some code uses alloca().
They generate different call sequences (one is a direct intrinsic, the
other may go through a wrapper).

Example:
    char* buf = (char*)alloca(size);
    ->
    char* buf = (char*)_alloca(size);
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant


class AllocaIntrinsicPattern(Pattern):
    name = "alloca_intrinsic"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Always relevant — cheap text swap
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        for stmt in ctx.statements:
            for node in walk(stmt):
                if node.type != "call_expression":
                    continue

                func = node.child_by_field_name("function")
                if func is None:
                    continue

                func_text = func.text
                if func_text == b"alloca":
                    new_source = (
                        source[:func.start_byte]
                        + b"_alloca"
                        + source[func.end_byte:]
                    )
                    yield Variant(
                        name=f"alloca_{counter}",
                        pattern_name="alloca_intrinsic",
                        description="Replace alloca() with _alloca()",
                        source=new_source,
                    )
                    counter += 1

                elif func_text == b"_alloca":
                    new_source = (
                        source[:func.start_byte]
                        + b"alloca"
                        + source[func.end_byte:]
                    )
                    yield Variant(
                        name=f"alloca_{counter}",
                        pattern_name="alloca_intrinsic",
                        description="Replace _alloca() with alloca()",
                        source=new_source,
                    )
                    counter += 1
