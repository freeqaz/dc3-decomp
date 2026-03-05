"""FMA reorder pattern — reorder fused multiply-add expressions.

The PowerPC compiler generates different FMA instructions depending on
expression structure:
    a + b*c  -> fmadds
    a - b*c  -> fnmsubs (or fmsubs)
    b*c + a  -> fmadds (different register allocation)
    b*c - a  -> fmsubs

Reordering the addend vs multiply can fix FMA opcode mismatches.

Also handles parenthesized expansion, which changes FMA selection by
altering which terms the compiler fuses:
    a - (b - c)  -> c - b + a    (fmsubs/fsubs -> fnmsubs/fadds)
    a - (b * c - d)  -> d - b * c + a
    a + (b - c)  -> a + b - c    (removes unnecessary parens)

This was proven to fix CalcSpline (96% -> 100%) and InterpTangent
(98.1% -> 99.6%).

Example:
    float r = 1.0f - x * y;
    ->
    float r = -(x * y) + 1.0f;
    // or: float r = x * y - 1.0f; (negate sense)

    float r = p3 - (p2 * 3.0f - p1x3m0);
    ->
    float r = p1x3m0 - p2 * 3.0f + p3;
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant

_FMA_OPCODES = {"fmadds", "fmsubs", "fnmadds", "fnmsubs",
                "fmadd", "fmsub", "fnmadd", "fnmsub"}
_ADDSUB_OPCODES = {"fadds", "fsubs", "fadd", "fsub"}


class FmaReorderPattern(Pattern):
    name = "fma_reorder"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FMA_OPCODES or d.base_opcode in _FMA_OPCODES:
                return True
            # Also relevant for fadds/fsubs mismatches (paren expansion changes these)
            pair = {d.target_opcode, d.base_opcode}
            if pair & _ADDSUB_OPCODES:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        for d in diagnosis.diff_ops:
            pair = {d.target_opcode, d.base_opcode}
            if len(pair & _FMA_OPCODES) == 2:
                return 0.9  # one FMA op replaced by another
            if pair & _FMA_OPCODES and pair & _ADDSUB_OPCODES:
                return 0.85  # FMA vs separate add/sub — paren expansion candidate
        return 0.6

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for binop in _find_fma_candidates(stmt):
                for variant in _generate_reorders(binop, ctx, counter):
                    yield variant
                    counter += 1
            for binop in _find_paren_sub_candidates(stmt):
                for variant in _generate_paren_expansions(binop, ctx, counter):
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


def _find_paren_sub_candidates(node: Node) -> Iterator[Node]:
    """Find a ± (b ± c) patterns where the right operand is parenthesized
    and contains an addition or subtraction.

    Candidates for algebraic expansion:
        a - (b - c)  ->  c - b + a    (proven: CalcSpline, InterpTangent)
        a - (b + c)  ->  a - b - c
        a + (b - c)  ->  a + b - c    (71 instances in codebase)
    Removing/changing parentheses alters FMA fusion decisions.
    """
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text in (b"-", b"+"):
            right = node.child_by_field_name("right")
            if right is not None:
                # Unwrap parenthesized_expression
                inner = right
                if inner.type == "parenthesized_expression" and inner.named_children:
                    inner = inner.named_children[0]
                # Check if inner contains +/- (worth expanding)
                if inner.type == "binary_expression":
                    inner_op = inner.child_by_field_name("operator")
                    if inner_op and inner_op.text in (b"-", b"+"):
                        yield node

    for child in node.children:
        yield from _find_paren_sub_candidates(child)


def _collect_terms(node: Node, source: bytes, negate: bool = False) -> list[tuple[bytes, bool]]:
    """Flatten a chain of +/- into (term_text, is_negated) pairs.

    For `a - b + c`, returns [(a, False), (b, True), (c, False)].
    The `negate` flag flips all signs (used when distributing a leading minus).
    """
    if node.type == "parenthesized_expression" and node.named_children:
        return _collect_terms(node.named_children[0], source, negate)

    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if op and left and right and op.text in (b"+", b"-"):
            left_terms = _collect_terms(left, source, negate)
            right_negate = negate if op.text == b"+" else (not negate)
            right_terms = _collect_terms(right, source, right_negate)
            return left_terms + right_terms

    text = source[node.start_byte:node.end_byte]
    return [(text, negate)]


def _terms_to_expr(terms: list[tuple[bytes, bool]]) -> bytes:
    """Reassemble (term_text, is_negated) pairs into an expression string."""
    if not terms:
        return b"0"
    parts = []
    for i, (text, neg) in enumerate(terms):
        if i == 0:
            if neg:
                parts.append(b"-" + text)
            else:
                parts.append(text)
        else:
            if neg:
                parts.append(b" - " + text)
            else:
                parts.append(b" + " + text)
    return b"".join(parts)


def _generate_paren_expansions(
    binop: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate algebraic expansions of a - (b - c) patterns.

    Proven fix: a - (b - c) -> c - b + a
    Changes FMA selection from fmsubs/fsubs to fnmsubs/fadds.
    """
    source = ctx.file_source
    op_node = binop.child_by_field_name("operator")
    left = binop.child_by_field_name("left")
    right = binop.child_by_field_name("right")
    if op_node is None or left is None or right is None:
        return

    # Collect terms: left contributes positively, right depends on outer operator
    right_negated = op_node.text == b"-"
    left_terms = _collect_terms(left, source, negate=False)
    right_terms = _collect_terms(right, source, negate=right_negated)
    all_terms = left_terms + right_terms

    if len(all_terms) < 2:
        return

    # Variant 1: reverse order — c - b + a (the proven fix pattern)
    reversed_terms = list(reversed(all_terms))
    reversed_expr = _terms_to_expr(reversed_terms)
    original_expr = source[binop.start_byte:binop.end_byte]
    if reversed_expr != original_expr:
        new_source = (
            source[:binop.start_byte]
            + reversed_expr
            + source[binop.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Expand paren subtraction (reversed): "
                        f"{original_expr.decode('utf-8', errors='replace')[:40]} -> "
                        f"{reversed_expr.decode('utf-8', errors='replace')[:40]}",
            source=new_source,
        )
        counter += 1

    # Variant 2: flat expansion in original order — a - b + c
    flat_expr = _terms_to_expr(all_terms)
    if flat_expr != original_expr and flat_expr != reversed_expr:
        new_source = (
            source[:binop.start_byte]
            + flat_expr
            + source[binop.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Expand paren subtraction (flat): "
                        f"{original_expr.decode('utf-8', errors='replace')[:40]} -> "
                        f"{flat_expr.decode('utf-8', errors='replace')[:40]}",
            source=new_source,
        )
