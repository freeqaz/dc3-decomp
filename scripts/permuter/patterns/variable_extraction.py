"""Variable extraction pattern — extract inline calls into auto locals.

Win rate: ~42% from attempt database.

Finds call_expression nodes nested inside argument_list, binary_expression,
or condition_clause at depth > 1. Extracts each into an `auto` local variable
declared before the containing statement.

Example:
    MILO_ASSERT(display < mElements.size(), 0x74);
    ->
    auto _tmp0 = mElements.size();
    MILO_ASSERT(display < _tmp0, 0x74);
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import FunctionContext, Variant

# Node types that indicate a call is nested (not a standalone expression_statement)
_NESTING_TYPES = {
    "argument_list",
    "binary_expression",
    "condition_clause",
    "parenthesized_expression",
    "assignment_expression",
    "return_statement",
}


class VariableExtractionPattern(Pattern):
    name = "variable_extraction"

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Walk all compound_statements to find extractable calls in their direct children
        for compound, stmt, call_node in _find_extractable_calls(ctx.body_node):
            call_text = ctx.file_source[call_node.start_byte : call_node.end_byte]

            indent = _get_indent(ctx.file_source, stmt)
            line_start = _get_line_start(ctx.file_source, stmt)
            var_name = f"_tmp{counter}".encode("utf-8")
            counter += 1

            # Build the declaration line
            decl_line = indent + b"auto " + var_name + b" = " + call_text + b";\n"

            # Splice: insert decl before the line containing stmt,
            # then replace call with var_name within the stmt
            source = ctx.file_source
            new_source = (
                source[:line_start]
                + decl_line
                + source[line_start : call_node.start_byte]
                + var_name
                + source[call_node.end_byte :]
            )

            desc = (
                f"Extract '{call_text.decode('utf-8', errors='replace')}' "
                f"into auto {var_name.decode()}"
            )
            yield Variant(
                name=f"varext_{counter - 1}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
            )


def _find_extractable_calls(
    body_node: Node,
) -> Iterator[tuple[Node, Node, Node]]:
    """Find (compound_statement, containing_statement, call_node) tuples.

    Walks all compound_statements (function body, loop bodies, if/else bodies)
    and for each direct child statement, finds nested call expressions that
    can be extracted to a variable before that statement.
    """
    for stmt in body_node.named_children:
        # For each direct statement in this compound_statement,
        # find nested calls
        for call_node in _find_nested_calls(stmt):
            yield body_node, stmt, call_node

        # Recurse into compound_statements within this statement
        # (for-loop bodies, if/else bodies, while bodies, etc.)
        for compound in _find_compound_children(stmt):
            yield from _find_extractable_calls(compound)


def _find_compound_children(node: Node) -> Iterator[Node]:
    """Find compound_statement children (loop/if/else bodies)."""
    for child in node.children:
        if child.type == "compound_statement":
            yield child
        elif child.type in ("if_statement", "else_clause", "for_statement",
                            "while_statement", "do_statement", "switch_statement"):
            yield from _find_compound_children(child)


def _find_nested_calls(node: Node, depth: int = 0) -> Iterator[Node]:
    """Find call_expression nodes nested inside other expressions.

    Only yields calls where the call is inside a nesting context (argument,
    binary expression, condition), not standalone call statements.
    Does NOT recurse into compound_statement children (those are handled
    by _find_extractable_calls to maintain proper scoping).
    """
    if node.type == "call_expression" and depth > 0:
        parent = node.parent
        if parent is not None and parent.type in _NESTING_TYPES:
            yield node
            return  # Don't recurse deeper into this call's own args

    next_depth = depth
    if node.type in _NESTING_TYPES or node.type == "call_expression":
        next_depth = depth + 1

    for child in node.children:
        # Don't cross compound_statement boundaries — inner scopes
        # are handled by _find_extractable_calls recursion
        if child.type == "compound_statement":
            continue
        yield from _find_nested_calls(child, next_depth)


def _get_line_start(source: bytes, node: Node) -> int:
    """Get the byte offset of the start of the line containing a node."""
    pos = node.start_byte
    while pos > 0 and source[pos - 1 : pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _get_indent(source: bytes, node: Node) -> bytes:
    """Get the whitespace indent of the line containing a node."""
    pos = _get_line_start(source, node)

    indent = b""
    for i in range(pos, node.start_byte):
        ch = source[i : i + 1]
        if ch in (b" ", b"\t"):
            indent += ch
        else:
            break
    return indent
