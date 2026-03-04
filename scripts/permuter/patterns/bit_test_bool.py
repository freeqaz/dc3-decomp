"""Bit test bool pattern — extract bitwise AND test to bool local.

The compiler generates different masking code (clrlwi/rlwinm/extrwi) when
a bitwise AND result is used directly in a condition vs when it's first
extracted to a bool variable.

Example:
    if ((flags & MASK) && other) { ... }
    ->
    bool b = (flags & MASK) != 0;
    if (b && other) { ... }
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent
from ..types import Diagnosis, FunctionContext, Variant


class BitTestBoolPattern(Pattern):
    name = "bit_test_bool"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if diagnosis.replace_real > 0:
            return True
        for d in diagnosis.diff_ops:
            for op in (d.target_opcode, d.base_opcode):
                if op in ("clrlwi", "rlwinm", "extrwi"):
                    return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: rlwinm/clrlwi/extrwi — bit masking instruction mismatch
        for d in diagnosis.diff_ops:
            for op in (d.target_opcode, d.base_opcode):
                if op in ("clrlwi", "rlwinm", "extrwi"):
                    return 0.7
        if diagnosis.replace_real > 0:
            return 0.4
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        for stmt in ctx.statements:
            for node in walk(stmt):
                if node.type != "if_statement":
                    continue

                condition = node.child_by_field_name("condition")
                if condition is None:
                    continue

                inner = _get_inner_expr(condition)
                if inner is None:
                    continue

                # Find bitwise & in conditions
                bit_ands = list(_find_bitwise_and(inner))
                if not bit_ands:
                    continue

                for bit_and in bit_ands:
                    bit_text = source[bit_and.start_byte:bit_and.end_byte]
                    indent = get_indent(source, node)

                    var_name = f"_bit{counter}".encode()

                    # Replace the bitwise & expression in the condition with the bool var
                    new_cond = (
                        source[condition.start_byte:bit_and.start_byte]
                        + var_name
                        + source[bit_and.end_byte:condition.end_byte]
                    )

                    # Insert bool extraction before the if
                    new_source = (
                        source[:node.start_byte]
                        + b"bool " + var_name + b" = (" + bit_text + b") != 0;\n"
                        + indent + b"if " + new_cond + b" "
                        + source[condition.end_byte + 1:]  # skip the space after condition
                    )

                    # Simpler approach: just insert before the if, replace inline
                    new_source = (
                        source[:node.start_byte]
                        + b"bool " + var_name + b" = (" + bit_text + b") != 0;\n"
                        + indent
                        + source[node.start_byte:bit_and.start_byte]
                        + var_name
                        + source[bit_and.end_byte:]
                    )

                    yield Variant(
                        name=f"bittest_{counter}",
                        pattern_name="bit_test_bool",
                        description=f"Extract ({bit_text.decode(errors='replace')}) to bool local",
                        source=new_source,
                    )
                    counter += 1


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _find_bitwise_and(node: Node) -> Iterator[Node]:
    """Find binary_expression nodes with & operator (bitwise AND)."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"&":
            # Make sure it's not && (that would be parsed differently, but just in case)
            yield node
            return  # Don't recurse into this node's children

    for child in node.children:
        yield from _find_bitwise_and(child)
