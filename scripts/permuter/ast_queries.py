"""Shared AST walkers and text utilities for permuter patterns.

Eliminates duplicate _find_comparisons, _find_calls, _find_if_else,
_get_indent, _get_line_start, _walk, and _collect_identifiers helpers
that were copy-pasted across multiple pattern modules.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

# Default comparison operators
_ALL_CMP_OPS = {"<", ">", "<=", ">=", "==", "!="}


# ---------------------------------------------------------------------------
# Generic walkers
# ---------------------------------------------------------------------------

def walk(node: Node) -> Iterator[Node]:
    """Depth-first walk of all nodes (including anonymous)."""
    yield node
    for child in node.children:
        yield from walk(child)


def walk_named(node: Node) -> Iterator[Node]:
    """Depth-first walk of named nodes only."""
    yield node
    for child in node.named_children:
        yield from walk_named(child)


def find_by_type(node: Node, type_name: str) -> Iterator[Node]:
    """Recursively find nodes of a specific type."""
    if node.type == type_name:
        yield node
    for child in node.children:
        yield from find_by_type(child, type_name)


# ---------------------------------------------------------------------------
# Domain-specific finders
# ---------------------------------------------------------------------------

def find_comparisons(node: Node, ops: set[str] | None = None) -> Iterator[Node]:
    """Find binary_expression nodes with comparison operators.

    Args:
        node: Root node to search from.
        ops: Set of operator strings to match. Defaults to all six
             comparison operators: < > <= >= == !=
    """
    if ops is None:
        ops = _ALL_CMP_OPS

    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text and op.text.decode("utf-8") in ops:
            yield node

    for child in node.children:
        yield from find_comparisons(child, ops)


def find_calls(node: Node) -> Iterator[Node]:
    """Recursively find call_expression nodes."""
    if node.type == "call_expression":
        yield node
    for child in node.children:
        yield from find_calls(child)


def find_if_else(node: Node) -> Iterator[Node]:
    """Find if_statement nodes that have both consequence and alternative."""
    if node.type == "if_statement":
        consequence = node.child_by_field_name("consequence")
        alternative = node.child_by_field_name("alternative")
        if consequence is not None and alternative is not None:
            yield node
    for child in node.children:
        yield from find_if_else(child)


# ---------------------------------------------------------------------------
# Text utilities
# ---------------------------------------------------------------------------

def get_line_start(source: bytes, node: Node) -> int:
    """Get the byte offset of the start of the line containing *node*."""
    pos = node.start_byte
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def get_indent(source: bytes, node: Node) -> bytes:
    """Get the leading whitespace of the line containing *node*."""
    pos = get_line_start(source, node)
    indent = b""
    for i in range(pos, node.start_byte):
        ch = source[i:i + 1]
        if ch in (b" ", b"\t"):
            indent += ch
        else:
            break
    return indent


def node_text(source: bytes, node: Node) -> bytes:
    """Extract the raw bytes for *node*."""
    return source[node.start_byte:node.end_byte]


def identifiers_in(node: Node) -> set[str]:
    """Collect all identifier names in *node*'s subtree."""
    ids: set[str] = set()
    _collect_identifiers(node, ids)
    return ids


def _collect_identifiers(node: Node, ids: set[str]) -> None:
    """Recursively collect identifier names from an expression subtree."""
    if node.type == "identifier" and node.text:
        ids.add(node.text.decode("utf-8", errors="replace"))
    for child in node.children:
        _collect_identifiers(child, ids)
