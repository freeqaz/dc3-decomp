"""And-split pattern — split && conditions into nested ifs (and reverse).

The compiler generates different branch structures for:
    if (a && b) { body }
vs
    if (a) { if (b) { body } }

Splitting can fix CONTROL_FLOW diff_ops where the original used nested branches
instead of short-circuit evaluation (or vice versa).

Example:
    if (a && b) { foo(); }
    ->
    if (a) { if (b) { foo(); } }
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_if_else, get_indent, walk
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class AndSplitPattern(Pattern):
    name = "and_split"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when control flow differs
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: multiple branch diffs + clusters (structural control flow change)
        branch_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES
        )
        if branch_count >= 2 and len(diagnosis.clusters) >= 2:
            return 0.7
        if diagnosis.clusters:
            return 0.4
        return 0.2  # branch diffs only — could be simpler polarity flip

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            # Split: if (a && b) -> if (a) { if (b) }
            for variant in _split_and_conditions(stmt, ctx, counter):
                yield variant
                counter += 1
            # Merge: if (a) { if (b) { body } } -> if (a && b) { body }
            for variant in _merge_nested_ifs(stmt, ctx, counter):
                yield variant
                counter += 1


def _split_and_conditions(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find if (a && b) and split into nested ifs."""
    source = ctx.file_source

    for node in walk(stmt):
        if node.type != "if_statement":
            continue

        condition = node.child_by_field_name("condition")
        consequence = node.child_by_field_name("consequence")
        alternative = node.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue

        # Get the inner expression from condition_clause
        inner = _get_inner_expr(condition)
        if inner is None or inner.type != "binary_expression":
            continue

        op = inner.child_by_field_name("operator")
        if op is None or op.text != b"&&":
            continue

        left = inner.child_by_field_name("left")
        right = inner.child_by_field_name("right")
        if left is None or right is None:
            continue

        left_text = source[left.start_byte:left.end_byte]
        right_text = source[right.start_byte:right.end_byte]
        cons_text = source[consequence.start_byte:consequence.end_byte]
        indent = get_indent(source, node)

        if alternative is None:
            # Simple case: if (a && b) { body }
            # -> if (a) { if (b) { body } }
            inner_if = indent + b"    " + b"if (" + right_text + b") " + cons_text
            new_body = b"{\n" + inner_if + b"\n" + indent + b"}"

            new_source = (
                source[:condition.start_byte]
                + b"(" + left_text + b")"
                + source[condition.end_byte:consequence.start_byte]
                + new_body
                + source[consequence.end_byte:]
            )
        else:
            # With else: if (a && b) { body } else { alt }
            # -> if (a) { if (b) { body } else { alt } } else { alt }
            # This duplicates the else — skip for now, too complex
            continue

        yield Variant(
            name=f"andsplit_{counter}",
            pattern_name="and_split",
            description=f"Split && into nested if: ({left_text.decode(errors='replace')}) && ({right_text.decode(errors='replace')})",
            source=new_source,
        )
        counter += 1


def _merge_nested_ifs(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find if (a) { if (b) { body } } and merge into if (a && b) { body }."""
    source = ctx.file_source

    for node in walk(stmt):
        if node.type != "if_statement":
            continue

        condition = node.child_by_field_name("condition")
        consequence = node.child_by_field_name("consequence")
        alternative = node.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue
        # Only merge when outer has no else
        if alternative is not None:
            continue

        # Check if consequence is { if (b) { body } } with nothing else
        inner_if = _get_sole_inner_if(consequence)
        if inner_if is None:
            continue

        inner_cond = inner_if.child_by_field_name("condition")
        inner_cons = inner_if.child_by_field_name("consequence")
        inner_alt = inner_if.child_by_field_name("alternative")

        if inner_cond is None or inner_cons is None:
            continue
        # Skip if inner has else
        if inner_alt is not None:
            continue

        outer_expr = _get_inner_expr(condition)
        inner_expr = _get_inner_expr(inner_cond)
        if outer_expr is None or inner_expr is None:
            continue

        outer_text = source[outer_expr.start_byte:outer_expr.end_byte]
        inner_text = source[inner_expr.start_byte:inner_expr.end_byte]
        inner_cons_text = source[inner_cons.start_byte:inner_cons.end_byte]

        # Merge: if (outer && inner) { inner_body }
        new_source = (
            source[:condition.start_byte]
            + b"(" + outer_text + b" && " + inner_text + b")"
            + source[condition.end_byte:consequence.start_byte]
            + inner_cons_text
            + source[consequence.end_byte:]
        )

        yield Variant(
            name=f"andsplit_{counter}",
            pattern_name="and_split",
            description=f"Merge nested ifs: ({outer_text.decode(errors='replace')}) && ({inner_text.decode(errors='replace')})",
            source=new_source,
        )
        counter += 1


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _get_sole_inner_if(compound_stmt: Node) -> Node | None:
    """Check if a compound_statement contains exactly one if_statement."""
    if compound_stmt.type != "compound_statement":
        return None

    stmts = [c for c in compound_stmt.named_children if c.type != "comment"]
    if len(stmts) == 1 and stmts[0].type == "if_statement":
        return stmts[0]
    return None
