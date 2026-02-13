"""Signed/unsigned cast pattern — wrap comparison operands in casts.

Win rate: ~30% from attempt database.

Finds binary_expression nodes with comparison operators and generates variants
wrapping each operand in (int), (unsigned int), or (unsigned long) casts.
Also tries swapping != 0 <-> > 0 for unsigned comparisons.

Example:
    if (ptr != 0)
    ->
    if ((int)ptr != 0)
    if ((unsigned int)ptr != 0)
    if (ptr > 0)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import FunctionContext, Variant

_COMPARISON_OPS = {"==", "!=", "<", ">", "<=", ">="}
_CAST_TYPES = [b"(int)", b"(unsigned int)", b"(unsigned long)"]
_NULL_LITERALS = {"nullptr", "NULL"}


class SignedUnsignedPattern(Pattern):
    name = "signed_unsigned"

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for cmp_node in _find_comparisons(stmt):
                left = cmp_node.child_by_field_name("left")
                right = cmp_node.child_by_field_name("right")
                op_node = cmp_node.child_by_field_name("operator")
                if left is None or right is None:
                    continue

                op_text = ctx.source_text(op_node) if op_node else None

                # Skip cast variants for likely-pointer comparisons
                if not _is_likely_pointer(left, right, ctx):
                    # Cast left operand
                    for cast in _CAST_TYPES:
                        new_source = _wrap_cast(ctx.file_source, left, cast)
                        cast_str = cast.decode()
                        yield Variant(
                            name=f"signunsign_{counter}",
                            pattern_name=self.name,
                            description=f"Cast left of '{op_text}' to {cast_str}",
                            source=new_source,
                        )
                        counter += 1

                    # Cast right operand
                    for cast in _CAST_TYPES:
                        new_source = _wrap_cast(ctx.file_source, right, cast)
                        cast_str = cast.decode()
                        yield Variant(
                            name=f"signunsign_{counter}",
                            pattern_name=self.name,
                            description=f"Cast right of '{op_text}' to {cast_str}",
                            source=new_source,
                        )
                        counter += 1

                # Swap != 0 <-> > 0 (always worth trying, 0 is ambiguous)
                right_text = ctx.file_source[
                    right.start_byte : right.end_byte
                ]
                if right_text.strip() == b"0" and op_text in ("!=", ">"):
                    new_op = b">" if op_text == "!=" else b"!="
                    # Replace the operator, preserving surrounding whitespace
                    if op_node is not None:
                        new_source = (
                            ctx.file_source[: op_node.start_byte]
                            + new_op
                            + ctx.file_source[op_node.end_byte :]
                        )
                        swap_desc = (
                            "!= 0 -> > 0" if op_text == "!=" else "> 0 -> != 0"
                        )
                        yield Variant(
                            name=f"signunsign_{counter}",
                            pattern_name=self.name,
                            description=f"Swap comparison: {swap_desc}",
                            source=new_source,
                        )
                        counter += 1


def _is_likely_pointer(left: Node, right: Node, ctx: FunctionContext) -> bool:
    """Heuristic: return True if this comparison likely involves pointers.

    Casting pointers to (int) causes build failures, so we skip those.
    """
    for operand in (left, right):
        # address-of expression: &foo
        if operand.type == "unary_expression":
            op = operand.child_by_field_name("operator")
            if op and op.text == b"&":
                return True

        # nullptr or NULL literal
        text = ctx.source_text(operand)
        if text in _NULL_LITERALS:
            return True

        # Call expression (likely returns pointer)
        if operand.type == "call_expression":
            return True

        # Arrow/dot member access: obj->field, obj.field (likely pointer context)
        if operand.type == "field_expression":
            return True

        # Pointer dereference: *ptr
        if operand.type == "pointer_expression":
            return True

    # Check if whole comparison is X != nullptr / X == NULL pattern
    left_text = ctx.source_text(left)
    right_text = ctx.source_text(right)
    if left_text in _NULL_LITERALS or right_text in _NULL_LITERALS:
        return True

    return False


def _find_comparisons(node: Node) -> Iterator[Node]:
    """Recursively find binary_expression nodes with comparison operators."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text and op.text.decode("utf-8") in _COMPARISON_OPS:
            yield node

    for child in node.children:
        yield from _find_comparisons(child)


def _wrap_cast(source: bytes, node: Node, cast: bytes) -> bytes:
    """Wrap a node's text in a cast expression."""
    return (
        source[: node.start_byte]
        + cast
        + source[node.start_byte : node.end_byte]
        + source[node.end_byte :]
    )
