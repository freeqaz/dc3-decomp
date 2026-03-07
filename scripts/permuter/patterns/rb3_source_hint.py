"""RB3 source hint pattern — targeted ternary/if-else swaps guided by RB3 reference.

Unlike ternary_swap (which brute-forces every possible conversion), this pattern
consults the RB3 reference source to identify only the specific locations where
RB3's implementation uses a different form than DC3. Only those cross-form
mismatches are emitted as variants.

RB3 shares the Milo engine with DC3, so for shared functions the RB3 source is
the strongest signal for what the DC3 target "should" look like structurally.
"""

from __future__ import annotations

from typing import Iterator

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser, Node

from .base import Pattern
from .ternary_swap import (
    _if_to_ternary,
    _ternary_to_if,
    _return_ternary_to_if,
    _if_return_to_ternary,
    _find_ternary_assignment,
)
from ..types import Diagnosis, FunctionContext, Variant

_CPP_LANGUAGE = Language(tscpp.language())
_PARSER = Parser(_CPP_LANGUAGE)


class Rb3SourceHintPattern(Pattern):
    name = "rb3_source_hint"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if diagnosis.clusters:
            return True
        if diagnosis.reg_swap_pairs:
            return True
        branch_ops = {"beq", "bne", "bge", "ble", "bgt", "blt"}
        for d in diagnosis.diff_ops:
            if d.target_opcode in branch_ops or d.base_opcode in branch_ops:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        return 0.8 if self.relevant(diagnosis) else 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        if not ctx.rb3_source:
            return

        rb3_stmts = _parse_rb3_body(ctx.rb3_source)
        if rb3_stmts is None:
            return

        rb3_ternary_vars = _collect_ternary_lhs(rb3_stmts)
        rb3_ifelse_vars = _collect_ifelse_lhs(rb3_stmts)
        rb3_has_return_ternary = _has_return_ternary(rb3_stmts)
        rb3_has_return_ifelse = _has_return_ifelse(rb3_stmts)

        counter = 0
        for stmt in ctx.statements:
            # DC3 ternary → RB3 if/else: emit ternary-to-if
            ternary_info = _find_ternary_assignment(stmt, ctx)
            if ternary_info:
                var_name = ternary_info[0]
                if var_name in rb3_ifelse_vars and var_name not in rb3_ternary_vars:
                    for v in _ternary_to_if(stmt, ctx, counter):
                        yield _retag(v, counter)
                        counter += 1

            # DC3 if/else → RB3 ternary: emit if-to-ternary
            if_lhs = _find_ifelse_lhs(stmt)
            if if_lhs and if_lhs in rb3_ternary_vars and if_lhs not in rb3_ifelse_vars:
                for v in _if_to_ternary(stmt, ctx, counter):
                    yield _retag(v, counter)
                    counter += 1

            # DC3 return-ternary → RB3 if/else return
            if stmt.type == "return_statement" and _has_ternary_in_return(stmt):
                if rb3_has_return_ifelse and not rb3_has_return_ternary:
                    for v in _return_ternary_to_if(stmt, ctx, counter):
                        yield _retag(v, counter)
                        counter += 1

            # DC3 if/else return → RB3 return-ternary
            if _is_ifelse_return(stmt):
                if rb3_has_return_ternary and not rb3_has_return_ifelse:
                    for v in _if_return_to_ternary(stmt, ctx, counter):
                        yield _retag(v, counter)
                        counter += 1


# ---------------------------------------------------------------------------
# Helpers: parse RB3 body
# ---------------------------------------------------------------------------

def _parse_rb3_body(rb3_source: str) -> list[Node] | None:
    """Parse rb3_source and return top-level statements of the function body."""
    tree = _PARSER.parse(rb3_source.encode())
    root = tree.root_node

    # Find the first compound_statement (function body)
    body = _find_first(root, "compound_statement")
    if body is None:
        return None
    return [c for c in body.named_children]


def _find_first(node: Node, kind: str) -> Node | None:
    if node.type == kind:
        return node
    for child in node.children:
        result = _find_first(child, kind)
        if result is not None:
            return result
    return None


# ---------------------------------------------------------------------------
# Helpers: collect ternary/if-else LHS variable names from RB3 statements
# ---------------------------------------------------------------------------

def _collect_ternary_lhs(stmts: list[Node]) -> set[str]:
    """Walk statements; collect LHS vars assigned via conditional_expression."""
    result: set[str] = set()
    for stmt in stmts:
        _walk_ternary_lhs(stmt, result)
    return result


def _walk_ternary_lhs(node: Node, out: set[str]) -> None:
    if node.type == "expression_statement":
        for child in node.named_children:
            if child.type == "assignment_expression":
                right = child.child_by_field_name("right")
                left = child.child_by_field_name("left")
                if right is not None and right.type == "conditional_expression":
                    if left is not None and left.text:
                        out.add(left.text.decode("utf-8", errors="replace"))
    elif node.type == "declaration":
        declarator = node.child_by_field_name("declarator")
        if declarator is not None and declarator.type == "init_declarator":
            value = declarator.child_by_field_name("value")
            name_node = declarator.child_by_field_name("declarator")
            if value is not None and value.type == "conditional_expression":
                if name_node is not None and name_node.text:
                    out.add(name_node.text.decode("utf-8", errors="replace"))
    for child in node.children:
        _walk_ternary_lhs(child, out)


def _collect_ifelse_lhs(stmts: list[Node]) -> set[str]:
    """Walk statements; collect LHS vars from if/else with single assignments."""
    result: set[str] = set()
    for stmt in stmts:
        _walk_ifelse_lhs(stmt, result)
    return result


def _walk_ifelse_lhs(node: Node, out: set[str]) -> None:
    if node.type == "if_statement":
        cons = node.child_by_field_name("consequence")
        alt = node.child_by_field_name("alternative")
        if cons is not None and alt is not None:
            lhs = _single_assignment_lhs(cons)
            if lhs:
                alt_body = _alt_compound(alt)
                if alt_body is not None:
                    alt_lhs = _single_assignment_lhs(alt_body)
                    if alt_lhs and alt_lhs == lhs:
                        out.add(lhs)
    for child in node.children:
        _walk_ifelse_lhs(child, out)


def _single_assignment_lhs(compound: Node) -> str | None:
    """Return the LHS variable name if compound has exactly one assignment."""
    named = [c for c in compound.named_children]
    if len(named) != 1:
        return None
    stmt = named[0]
    if stmt.type != "expression_statement":
        return None
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            left = child.child_by_field_name("left")
            if left is not None and left.text:
                return left.text.decode("utf-8", errors="replace")
    return None


def _alt_compound(alt: Node) -> Node | None:
    for child in alt.children:
        if child.type == "compound_statement":
            return child
        if child.type == "if_statement":
            return None  # else-if
    return None


# ---------------------------------------------------------------------------
# Helpers: detect return forms
# ---------------------------------------------------------------------------

def _has_return_ternary(stmts: list[Node]) -> bool:
    for stmt in stmts:
        if _check_return_ternary(stmt):
            return True
    return False


def _check_return_ternary(node: Node) -> bool:
    if node.type == "return_statement":
        for child in node.named_children:
            if child.type == "conditional_expression":
                return True
    for child in node.children:
        if _check_return_ternary(child):
            return True
    return False


def _has_return_ifelse(stmts: list[Node]) -> bool:
    for stmt in stmts:
        if _check_return_ifelse(stmt):
            return True
    return False


def _check_return_ifelse(node: Node) -> bool:
    if node.type == "if_statement":
        cons = node.child_by_field_name("consequence")
        alt = node.child_by_field_name("alternative")
        if cons is not None and alt is not None:
            alt_body = _alt_compound(alt)
            if alt_body is not None:
                named_cons = [c for c in cons.named_children]
                named_alt = [c for c in alt_body.named_children]
                if (len(named_cons) == 1 and named_cons[0].type == "return_statement"
                        and len(named_alt) == 1 and named_alt[0].type == "return_statement"):
                    return True
    for child in node.children:
        if _check_return_ifelse(child):
            return True
    return False


# ---------------------------------------------------------------------------
# Helpers: check DC3 statement forms
# ---------------------------------------------------------------------------

def _find_ifelse_lhs(node: Node) -> str | None:
    """Return LHS variable name if node is if/else with matching single assignments."""
    if node.type != "if_statement":
        return None
    cons = node.child_by_field_name("consequence")
    alt = node.child_by_field_name("alternative")
    if cons is None or alt is None:
        return None
    lhs = _single_assignment_lhs(cons)
    if lhs is None:
        return None
    alt_body = _alt_compound(alt)
    if alt_body is None:
        return None
    alt_lhs = _single_assignment_lhs(alt_body)
    if alt_lhs and alt_lhs == lhs:
        return lhs
    return None


def _has_ternary_in_return(node: Node) -> bool:
    """True if return_statement directly contains a conditional_expression."""
    for child in node.named_children:
        if child.type == "conditional_expression":
            return True
    return False


def _is_ifelse_return(node: Node) -> bool:
    """True if node is if/else with single returns in both branches."""
    if node.type != "if_statement":
        return False
    cons = node.child_by_field_name("consequence")
    alt = node.child_by_field_name("alternative")
    if cons is None or alt is None:
        return False
    alt_body = _alt_compound(alt)
    if alt_body is None:
        return False
    named_cons = [c for c in cons.named_children]
    named_alt = [c for c in alt_body.named_children]
    return (len(named_cons) == 1 and named_cons[0].type == "return_statement"
            and len(named_alt) == 1 and named_alt[0].type == "return_statement")


# ---------------------------------------------------------------------------
# Helper: retag variant with rb3_source_hint pattern name
# ---------------------------------------------------------------------------

def _retag(v: Variant, counter: int) -> Variant:
    return Variant(
        name=f"rb3hint_{counter}",
        pattern_name="rb3_source_hint",
        description=v.description,
        source=v.source,
        edits=v.edits,
    )
