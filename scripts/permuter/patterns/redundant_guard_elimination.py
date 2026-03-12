"""Redundant guard elimination — remove exhaustive else-if/if-or guards.

Win rate: proven on HamListRibbon::StartFrame and HamListRibbon::EndFrame
(both 93%->100%).

When an ``else if (A || B)`` guard wraps inner conditions that are
collectively exhaustive (e.g. ``(A && !B)``, ``(!A && B)``, ``(A && B)``),
the outer OR check generates redundant comparison + branch instructions.
Replacing ``else if (A || B) { ... }`` with bare ``else { ... }`` eliminates
the guard while preserving semantics.

Similarly, a top-level ``if (A || B) { body }`` where the body's inner
branches exhaustively handle A and B cases can be replaced with just
``{ body }``.

Transformations:
    else if (A || B) { body }   -> else { body }
    if (A || B) { body }        -> { body }   (when inner branches are exhaustive)

Detection signals:
    - Insert clusters of 4-6 instructions (redundant variable re-testing)
    - ``else if`` with ``||`` in the condition
    - Branch opcode mismatches (from redundant guard branches)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class RedundantGuardEliminationPattern(Pattern):
    name = "redundant_guard_elimination"
    safety_tier = "normal"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "comparison_flip")
    cross_unit_modes = ("inline_header",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Insert clusters of 4-6 instructions (re-testing variables)
        for c in diagnosis.clusters:
            if 4 <= c.inserts <= 6:
                return True

        # Branch opcode mismatches
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True

        # Any clusters at all (structural mismatch)
        if diagnosis.clusters:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Higher priority for insert clusters in the 4-6 range
        for c in diagnosis.clusters:
            if 4 <= c.inserts <= 6:
                return 0.6
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        # Region filter: only consider statements in mismatch regions
        region_stmts = [s for s in ctx.statements if ctx.node_in_mismatch_region(s)]

        # Strategy 1: else-if-to-else — find else if with || and convert to bare else
        for stmt in region_stmts:
            if counter >= 10:
                return
            for variant in _else_if_to_else(stmt, source, counter):
                yield variant
                counter += 1
                if counter >= 10:
                    return

        # Strategy 2: if-guard-removal — find if (A || B) with exhaustive inner branches
        for stmt in region_stmts:
            if counter >= 10:
                return
            for variant in _if_guard_removal(stmt, source, counter):
                yield variant
                counter += 1
                if counter >= 10:
                    return


def _has_or_condition(condition: Node, source: bytes) -> bool:
    """Check if a condition contains a || operator at the top level."""
    inner = _get_inner_expr(condition)
    if inner is None:
        return False
    return _is_or_expression(inner)


def _is_or_expression(node: Node) -> bool:
    """Check if a node is a binary_expression with || operator."""
    if node.type != "binary_expression":
        return False
    op = node.child_by_field_name("operator")
    return op is not None and op.text == b"||"


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause or parenthesized_expression."""
    current = condition
    while current.type in ("condition_clause", "parenthesized_expression"):
        children = [c for c in current.named_children if c.type != "comment"]
        if len(children) == 1:
            current = children[0]
        else:
            break
    if current.id == condition.id:
        for child in condition.named_children:
            if child.type != "comment":
                return child
        return None
    return current


def _body_has_nested_conditions(consequence: Node) -> bool:
    """Check if a compound_statement body contains nested if/else checking variables.

    This is a heuristic: if the body contains at least one if_statement, the
    inner branches might be exhaustive over the variables in the outer guard.
    """
    if consequence.type != "compound_statement":
        return False
    for child in consequence.named_children:
        if child.type == "if_statement":
            return True
    return False


def _find_else_if_nodes(stmt: Node) -> Iterator[tuple[Node, Node]]:
    """Walk an if-statement chain and yield (if_node, else_if_node) pairs.

    For each ``else if`` in the chain, yields the parent if_statement and
    the else-if if_statement node.
    """
    if stmt.type != "if_statement":
        return

    alternative = stmt.child_by_field_name("alternative")
    if alternative is None:
        return

    # The alternative node contains the else clause. For "else if", the
    # alternative's children include an if_statement.
    for child in alternative.children:
        if child.type == "if_statement":
            yield (stmt, child)
            # Recurse into the else-if chain
            yield from _find_else_if_nodes(child)


def _else_if_to_else(
    stmt: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Convert ``else if (condition) { body }`` to ``else { body }``.

    Walks the if/else-if chain in stmt. For each ``else if`` whose condition
    contains ``||``, generates a variant with the condition removed (bare else).
    """
    if stmt.type != "if_statement":
        return

    for parent_if, else_if in _find_else_if_nodes(stmt):
        condition = else_if.child_by_field_name("condition")
        consequence = else_if.child_by_field_name("consequence")
        alternative = else_if.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue

        # Look for || in the condition
        if not _has_or_condition(condition, source):
            continue

        # Get the body text
        body_text = source[consequence.start_byte:consequence.end_byte]

        # Build replacement: remove the condition, keep just "else { body }"
        # We need to replace from the start of "if" to end of consequence
        # (and preserve any further else chain after this else-if).

        # The else-if node spans: "if (condition) { body } [else ...]"
        # We want to replace it with just "{ body } [else ...]"
        if alternative is not None:
            # Has further else chain: preserve it
            alt_text = source[alternative.start_byte:alternative.end_byte]
            replacement = body_text + b" " + alt_text
        else:
            replacement = body_text

        new_source = (
            source[:else_if.start_byte]
            + replacement
            + source[else_if.end_byte:]
        )

        cond_text = source[condition.start_byte:condition.end_byte]
        cond_str = cond_text.decode("utf-8", errors="replace")
        if len(cond_str) > 40:
            cond_str = cond_str[:37] + "..."

        yield Variant(
            name=f"redguard_{counter}",
            pattern_name="redundant_guard_elimination",
            description=f"Remove redundant else-if guard: {cond_str}",
            source=new_source,
        )
        counter += 1


def _if_guard_removal(
    stmt: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Remove ``if (A || B) { body }`` when body has exhaustive inner branches.

    Replaces the guarded block with just the body (braces preserved).
    Only applies when:
    1. The condition uses ``||``
    2. The body contains nested if/else statements (suggesting exhaustive coverage)
    3. There is no else clause on the outer if
    """
    if stmt.type != "if_statement":
        return

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    alternative = stmt.child_by_field_name("alternative")

    if condition is None or consequence is None:
        return

    # Must not have an else clause (otherwise it's not a pure guard)
    if alternative is not None:
        return

    # Condition must contain ||
    if not _has_or_condition(condition, source):
        return

    # Body must have nested conditions (exhaustive check heuristic)
    if not _body_has_nested_conditions(consequence):
        return

    # Get the body text (the compound_statement with braces)
    body_text = source[consequence.start_byte:consequence.end_byte]
    indent = get_indent(source, stmt)

    new_source = (
        source[:stmt.start_byte]
        + indent + body_text
        + source[stmt.end_byte:]
    )

    cond_text = source[condition.start_byte:condition.end_byte]
    cond_str = cond_text.decode("utf-8", errors="replace")
    if len(cond_str) > 40:
        cond_str = cond_str[:37] + "..."

    yield Variant(
        name=f"redguard_{counter}",
        pattern_name="redundant_guard_elimination",
        description=f"Remove redundant if guard: {cond_str}",
        source=new_source,
    )
