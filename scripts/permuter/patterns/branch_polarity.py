"""Branch polarity pattern — invert condition and swap if/else bodies.

Inverting an if/else condition changes branch opcodes (beq<->bne, etc.)
and reorders basic blocks in the generated code.

Example:
    if (x > 0) { a(); } else { b(); }
    ->
    if (!(x > 0)) { b(); } else { a(); }
    // or equivalently:
    if (x <= 0) { b(); } else { a(); }
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_if_else
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}

# Condition inversions for direct flipping (no ! wrapper needed)
_INVERSIONS = {
    "<": ">=",
    ">": "<=",
    "<=": ">",
    ">=": "<",
    "==": "!=",
    "!=": "==",
}


class BranchPolarityPattern(Pattern):
    name = "branch_polarity"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        # Also relevant if there are insert/delete clusters (block reordering)
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: beq↔bne or blt↔bge swap with few clusters (simple polarity)
        polarity_swaps = 0
        for d in diagnosis.diff_ops:
            pair = frozenset((d.target_opcode.rstrip("+-"), d.base_opcode.rstrip("+-")))
            if pair in ({"beq", "bne"}, {"blt", "bge"}, {"ble", "bgt"}):
                polarity_swaps += 1
        if polarity_swaps > 0 and len(diagnosis.clusters) <= 1:
            return 0.8
        if polarity_swaps > 0:
            return 0.4
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for if_node in find_if_else(stmt):
                for variant in _generate_inversions(if_node, ctx, counter):
                    yield variant
                    counter += 1


def _generate_inversions(
    if_node: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate branch polarity inversions for an if/else statement."""
    source = ctx.file_source
    condition = if_node.child_by_field_name("condition")
    consequence = if_node.child_by_field_name("consequence")
    alternative = if_node.child_by_field_name("alternative")

    if condition is None or consequence is None or alternative is None:
        return

    # Get the else clause body (skip 'else' keyword)
    alt_body = None
    for child in alternative.children:
        if child.type == "compound_statement":
            alt_body = child
            break
        elif child.type == "if_statement":
            # else if — skip, too complex
            return

    if alt_body is None:
        return

    cons_text = source[consequence.start_byte:consequence.end_byte]
    alt_body_text = source[alt_body.start_byte:alt_body.end_byte]

    # Method 1: Add ! wrapper to condition, swap bodies
    # condition is a condition_clause like (expr), get the inner expression
    cond_text = source[condition.start_byte:condition.end_byte]

    # Swap consequence and alternative bodies
    # Build: if (<negated_cond>) <alt_body> else <cons>
    new_source = (
        source[:condition.start_byte]
        + b"(!" + cond_text + b")"
        + source[condition.end_byte:consequence.start_byte]
        + alt_body_text
        + source[consequence.end_byte:alt_body.start_byte]
        + cons_text
        + source[alt_body.end_byte:]
    )

    yield Variant(
        name=f"brpol_{counter}",
        pattern_name="branch_polarity",
        description="Negate condition with !, swap if/else bodies",
        source=new_source,
    )
    counter += 1

    # Method 2: Try direct operator inversion (cleaner code)
    inner_expr = _get_condition_expr(condition)
    if inner_expr is not None and inner_expr.type == "binary_expression":
        op_node = inner_expr.child_by_field_name("operator")
        if op_node and op_node.text:
            op_str = op_node.text.decode("utf-8")
            if op_str in _INVERSIONS:
                new_op = _INVERSIONS[op_str].encode("utf-8")

                # Build new condition with inverted operator
                new_cond = (
                    source[condition.start_byte:op_node.start_byte]
                    + new_op
                    + source[op_node.end_byte:condition.end_byte]
                )

                new_source = (
                    source[:condition.start_byte]
                    + new_cond
                    + source[condition.end_byte:consequence.start_byte]
                    + alt_body_text
                    + source[consequence.end_byte:alt_body.start_byte]
                    + cons_text
                    + source[alt_body.end_byte:]
                )

                yield Variant(
                    name=f"brpol_{counter}",
                    pattern_name="branch_polarity",
                    description=f"Invert {op_str} -> {_INVERSIONS[op_str]}, swap if/else bodies",
                    source=new_source,
                )


def _get_condition_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause (parenthesized)."""
    # condition_clause -> ( expr )
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None
