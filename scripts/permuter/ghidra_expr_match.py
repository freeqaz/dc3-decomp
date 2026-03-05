"""Compare expression trees between Ghidra output and source code.

Normalizes both to canonical form, compares operator tree shapes,
and identifies structural differences (parenthesized vs flat, different
term order, etc.).

Used by fma_reorder to generate only variants matching the target structure.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterator

from tree_sitter import Node

from .ghidra_ast import extract_expression_structure, extract_arithmetic_expressions


@dataclass
class ExprDiff:
    """A structural difference between source and target expressions."""

    source_structure: str  # e.g. "(- _ (- _ _))"
    target_structure: str  # e.g. "(+ (- _ _) _)"
    source_node: Node  # The source AST node
    target_node: Node  # The Ghidra AST node
    position: int  # Statement index in function body (for matching)


def compare_arithmetic_expressions(
    source_stmts: list[Node],
    source_bytes: bytes,
    ghidra_ast: object,  # GhidraAST
) -> list[ExprDiff]:
    """Compare arithmetic expression structures between source and Ghidra.

    Matches expressions by position in the function body (i-th arithmetic
    expression in source vs i-th in Ghidra). This is imperfect but works
    well for simple functions where expression order is preserved.

    Returns a list of ExprDiff where the structures differ.
    """
    # Extract arithmetic expressions from source
    source_exprs: list[tuple[Node, str]] = []
    for i, stmt in enumerate(source_stmts):
        _find_arithmetic_in_node(stmt, source_bytes, source_exprs)

    # Extract from Ghidra
    target_exprs = extract_arithmetic_expressions(ghidra_ast)

    if not source_exprs or not target_exprs:
        return []

    diffs: list[ExprDiff] = []

    # Match by position (i-th arithmetic expr in source vs i-th in target)
    for i, (src_node, src_struct) in enumerate(source_exprs):
        if i >= len(target_exprs):
            break
        tgt_node, tgt_struct = target_exprs[i]

        if src_struct != tgt_struct:
            diffs.append(ExprDiff(
                source_structure=src_struct,
                target_structure=tgt_struct,
                source_node=src_node,
                target_node=tgt_node,
                position=i,
            ))

    return diffs


def is_flat_vs_paren(diff: ExprDiff) -> bool:
    """Check if a diff represents a flat-vs-parenthesized expression.

    This is the pattern proven to fix CalcSpline and InterpTangent:
    source has `a - (b - c)` but target has `c - b + a` (flat chain).
    """
    src = diff.source_structure
    tgt = diff.target_structure

    # Source is nested (contains nested operator), target is flat
    # Nested: (- _ (- _ _)) or (- _ (+ _ _))
    # Flat: (+ (- _ _) _) or (- (- _ _) _)
    src_nested = src.count("(") > 1
    tgt_flat = tgt.count("(") <= 2  # One for outer, one for inner sub-expr at most

    return src_nested and tgt_flat


def _find_arithmetic_in_node(
    node: Node, code_bytes: bytes, results: list[tuple[Node, str]]
) -> None:
    """Find top-level arithmetic expressions in a source AST node."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text in (b"+", b"-", b"*", b"/"):
            structure = extract_expression_structure(node, code_bytes)
            results.append((node, structure))
            return

    for child in node.children:
        _find_arithmetic_in_node(child, code_bytes, results)
