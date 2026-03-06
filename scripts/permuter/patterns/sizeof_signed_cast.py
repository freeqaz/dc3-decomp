"""Sizeof signed cast pattern — add/remove (int) cast on sizeof().

sizeof() returns size_t (unsigned). When used in arithmetic with signed
values, the compiler may generate srwi (unsigned shift) vs srawi/addze
(signed shift). Adding (int)sizeof(X) forces signed arithmetic.

Example:
    int n = total / sizeof(MyStruct);
    ->
    int n = total / (int)sizeof(MyStruct);
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant


class SizeofSignedCastPattern(Pattern):
    name = "sizeof_signed_cast"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Only relevant when shift instruction mismatches are present
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("srwi", "srawi", "addze") or \
               d.base_opcode in ("srwi", "srawi", "addze"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: srwi↔srawi swap — exactly signed vs unsigned shift
        for d in diagnosis.diff_ops:
            pair = {d.target_opcode, d.base_opcode}
            if pair & {"srwi", "srawi"}:
                return 0.9
            if "addze" in pair:
                return 0.7
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        for stmt in ctx.statements:
            for node in walk(stmt):
                if node.type != "sizeof_expression":
                    continue

                # Check if already wrapped in (int) cast
                parent = node.parent
                if parent is not None and parent.type == "cast_expression":
                    # Remove the cast: (int)sizeof(X) -> sizeof(X)
                    type_node = parent.child_by_field_name("type")
                    if type_node is not None and type_node.text == b"int":
                        sizeof_text = source[node.start_byte:node.end_byte]
                        new_source = (
                            source[:parent.start_byte]
                            + sizeof_text
                            + source[parent.end_byte:]
                        )
                        yield Variant(
                            name=f"sizeofcast_{counter}",
                            pattern_name="sizeof_signed_cast",
                            description="Remove (int) cast from sizeof()",
                            source=new_source,
                        )
                        counter += 1
                else:
                    # Add (int) cast: sizeof(X) -> (int)sizeof(X)
                    sizeof_text = source[node.start_byte:node.end_byte]
                    new_source = (
                        source[:node.start_byte]
                        + b"(int)" + sizeof_text
                        + source[node.end_byte:]
                    )
                    yield Variant(
                        name=f"sizeofcast_{counter}",
                        pattern_name="sizeof_signed_cast",
                        description="Add (int) cast to sizeof()",
                        source=new_source,
                    )
                    counter += 1
