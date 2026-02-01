"""Inline assignment pattern — fold variable assignment into call argument.

Win rate: ~22% from attempt database.

Finds consecutive statements where stmt A is `var = expr;` and stmt B uses
`var` as a call argument. Generates a variant folding A into B:

    era = pEra->GetName();
    CampaignEraProgress *p = GetEraProgress(era);
    ->
    CampaignEraProgress *p = GetEraProgress(era = pEra->GetName());
"""

from __future__ import annotations

from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..types import FunctionContext, Variant


class InlineAssignmentPattern(Pattern):
    name = "inline_assignment"

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for i in range(len(ctx.statements) - 1):
            stmt_a = ctx.statements[i]
            stmt_b = ctx.statements[i + 1]

            assign_info = _extract_assignment(stmt_a)
            if assign_info is None:
                continue

            var_name, rhs_text, assign_node = assign_info

            # Find uses of var_name in call arguments of stmt_b
            uses = list(_find_var_in_call_args(stmt_b, var_name, ctx.file_source))
            if not uses:
                continue

            # Generate one variant per use site
            for use_node in uses:
                # Build "var = rhs" as the inline expression
                inline_expr = var_name + b" = " + rhs_text

                # Remove stmt A and replace the use in stmt B
                source = ctx.file_source

                # Remove statement A (including trailing newline)
                stmt_a_end = stmt_a.end_byte
                # Skip any trailing whitespace/newline after stmt A
                while stmt_a_end < len(source) and source[stmt_a_end : stmt_a_end + 1] in (b"\n", b"\r"):
                    stmt_a_end += 1

                # Offset adjustment: removing stmt A shifts everything after it
                removed_len = stmt_a_end - stmt_a.start_byte

                # Build new source: remove stmt A, replace use with inline expr
                # The use_node offsets are relative to original source
                new_source = (
                    source[: stmt_a.start_byte]  # everything before stmt A
                    + source[stmt_a_end : use_node.start_byte]  # between stmt A end and use
                    + inline_expr
                    + source[use_node.end_byte :]  # after use
                )

                var_str = var_name.decode("utf-8", errors="replace")
                yield Variant(
                    name=f"inline_{counter}",
                    pattern_name=self.name,
                    description=f"Inline assignment of '{var_str}' into call argument",
                    source=new_source,
                )
                counter += 1


def _extract_assignment(
    stmt: Node,
) -> Optional[tuple[bytes, bytes, Node]]:
    """Extract (var_name, rhs_text, assignment_node) from an expression_statement
    that is a simple assignment `var = expr;`."""
    if stmt.type != "expression_statement":
        return None

    # The expression_statement should contain an assignment_expression
    expr = None
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            expr = child
            break
    if expr is None:
        return None

    left = expr.child_by_field_name("left")
    right = expr.child_by_field_name("right")
    if left is None or right is None:
        return None

    # Left should be a simple identifier
    if left.type != "identifier":
        return None

    var_name = left.text
    rhs_text = right.text
    if var_name is None or rhs_text is None:
        return None

    return var_name, rhs_text, expr


def _find_var_in_call_args(
    stmt: Node, var_name: bytes, source: bytes
) -> Iterator[Node]:
    """Find uses of var_name inside argument_list nodes within stmt."""
    for node in _walk(stmt):
        if node.type == "identifier" and node.text == var_name:
            # Check that this identifier is inside an argument_list
            parent = node.parent
            while parent is not None and parent != stmt:
                if parent.type == "argument_list":
                    yield node
                    break
                parent = parent.parent


def _walk(node: Node) -> Iterator[Node]:
    """Depth-first walk of all nodes."""
    yield node
    for child in node.children:
        yield from _walk(child)
