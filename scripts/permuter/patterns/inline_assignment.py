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
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Max statements gap between assignment and use to try inlining across
_MAX_GAP = 3


class InlineAssignmentPattern(Pattern):
    name = "inline_assignment"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return bool(diagnosis.clusters)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Look for assignment stmt_a with uses in stmt_b up to _MAX_GAP statements later
        for i in range(len(ctx.statements)):
            stmt_a = ctx.statements[i]

            assign_info = _extract_assignment(stmt_a, ctx.file_source)
            if assign_info is None:
                continue

            var_name, rhs_text, assign_node = assign_info

            # Search up to _MAX_GAP statements ahead for uses
            for gap in range(1, min(_MAX_GAP + 1, len(ctx.statements) - i)):
                stmt_b = ctx.statements[i + gap]

                # Check intervening statements don't modify or use the variable
                # (only relevant when gap > 1)
                if gap > 1:
                    conflict = False
                    for k in range(1, gap):
                        mid_stmt = ctx.statements[i + k]
                        if _stmt_references_var(mid_stmt, var_name):
                            conflict = True
                            break
                    if conflict:
                        break  # Can't skip past a conflicting statement

                # Find uses in call arguments, binary expressions, return statements
                uses = list(_find_var_uses(stmt_b, var_name, ctx.file_source))
                if not uses:
                    continue

                # Generate one variant per use site
                for use_node in uses:
                    # Build "var = rhs" as the inline expression
                    inline_expr = var_name + b" = " + rhs_text

                    source = ctx.file_source

                    # Remove statement A (including trailing newline)
                    stmt_a_end = stmt_a.end_byte
                    while stmt_a_end < len(source) and source[stmt_a_end : stmt_a_end + 1] in (b"\n", b"\r"):
                        stmt_a_end += 1

                    # Use SourceEditor: delete stmt A, replace use with inline expr
                    ed = SourceEditor(source)
                    ed.delete_range(stmt_a.start_byte, stmt_a_end)
                    ed.replace_node(use_node, inline_expr)
                    new_source = ed.apply()

                    var_str = var_name.decode("utf-8", errors="replace")
                    context = "expression" if gap == 1 else f"expression ({gap} stmts apart)"
                    yield Variant(
                        name=f"inline_{counter}",
                        pattern_name=self.name,
                        description=f"Inline assignment of '{var_str}' into {context}",
                        source=new_source,
                    )
                    counter += 1


def _extract_assignment(
    stmt: Node, source: bytes,
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
    if var_name is None:
        return None

    # Use byte-range slicing for consistency with SourceEditor and other patterns
    rhs_text = source[right.start_byte:right.end_byte]

    return var_name, rhs_text, expr


# Contexts where inlining an assignment is valid C++ (parenthesized assignment in expr)
_INLINE_CONTEXTS = {
    "argument_list",
    "binary_expression",
    "return_statement",
    "parenthesized_expression",
    "condition_clause",
    "assignment_expression",
}


def _find_var_uses(
    stmt: Node, var_name: bytes, source: bytes
) -> Iterator[Node]:
    """Find uses of var_name in inlinable contexts within stmt.

    Looks in call arguments, binary expressions, return statements, and
    other contexts where `(var = expr)` is valid.
    """
    for node in walk(stmt):
        if node.type == "identifier" and node.text == var_name:
            parent = node.parent
            while parent is not None and parent != stmt:
                if parent.type in _INLINE_CONTEXTS:
                    yield node
                    break
                parent = parent.parent


def _stmt_references_var(stmt: Node, var_name: bytes) -> bool:
    """Check if a statement reads or writes the given variable."""
    for node in walk(stmt):
        if node.type == "identifier" and node.text == var_name:
            return True
    return False
