"""FMA reorder pattern — reorder fused multiply-add expressions.

The PowerPC compiler generates different FMA instructions depending on
expression structure:
    a + b*c  -> fmadds
    a - b*c  -> fnmsubs (or fmsubs)
    b*c + a  -> fmadds (different register allocation)
    b*c - a  -> fmsubs

Reordering the addend vs multiply can fix FMA opcode mismatches.

Example:
    float r = 1.0f - x * y;
    ->
    float r = -(x * y) + 1.0f;
    // or: float r = x * y - 1.0f; (negate sense)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant

_FMA_OPCODES = {"fmadds", "fmsubs", "fnmadds", "fnmsubs",
                "fmadd", "fmsub", "fnmadd", "fnmsub"}


class FmaReorderPattern(Pattern):
    name = "fma_reorder"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FMA_OPCODES or d.base_opcode in _FMA_OPCODES:
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for binop in _find_fma_candidates(stmt):
                for variant in _generate_reorders(binop, ctx, counter):
                    yield variant
                    counter += 1


def _find_fma_candidates(node: Node) -> Iterator[Node]:
    """Find binary_expression nodes that look like FMA patterns.

    An FMA candidate is a +/- expression where one operand is a * expression.
    """
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text in (b"+", b"-"):
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            if left and right:
                left_is_mul = (left.type == "binary_expression" and
                               _has_op(left, b"*"))
                right_is_mul = (right.type == "binary_expression" and
                                _has_op(right, b"*"))
                # At least one side must be a multiply
                if left_is_mul or right_is_mul:
                    yield node

    for child in node.children:
        yield from _find_fma_candidates(child)


def _has_op(node: Node, op: bytes) -> bool:
    """Check if a binary_expression has the given operator."""
    op_node = node.child_by_field_name("operator")
    return op_node is not None and op_node.text == op


def _generate_reorders(
    binop: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate FMA expression reorderings."""
    source = ctx.file_source
    op_node = binop.child_by_field_name("operator")
    left = binop.child_by_field_name("left")
    right = binop.child_by_field_name("right")
    if op_node is None or left is None or right is None:
        return

    op_text = op_node.text
    left_text = source[left.start_byte:left.end_byte]
    right_text = source[right.start_byte:right.end_byte]

    if op_text == b"+":
        # a + b*c -> b*c + a (swap operands of addition)
        new_source = (
            source[:left.start_byte]
            + right_text
            + source[left.end_byte:right.start_byte]
            + left_text
            + source[right.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Swap addition operands (FMA reorder)",
            source=new_source,
        )

    elif op_text == b"-":
        # a - b*c -> -(b*c) + a  or  -(b*c - a)
        # Try: swap to b*c - a (negate sense)
        # This only works if we also negate, but the compiler may handle it

        # Variant 1: swap operands of subtraction
        new_source = (
            source[:left.start_byte]
            + right_text
            + source[left.end_byte:right.start_byte]
            + left_text
            + source[right.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Swap subtraction operands (FMA reorder)",
            source=new_source,
        )
        counter += 1

        # Variant 2: negate and rewrite  a - b*c -> -(b*c - a)
        # Wrap the whole expression in negation with swapped operands
        new_expr = b"-(" + right_text + b" - " + left_text + b")"
        new_source = (
            source[:binop.start_byte]
            + new_expr
            + source[binop.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Negate FMA: a - b*c -> -(b*c - a)",
            source=new_source,
        )
