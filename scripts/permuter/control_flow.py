"""Shared control-flow helpers for block-structured permuter patterns."""

from __future__ import annotations

from collections.abc import Callable, Iterator

from tree_sitter import Node


def iter_compound_statements(node: Node) -> Iterator[Node]:
    """Yield all nested compound_statement nodes, including the root if applicable."""
    if node.type == "compound_statement":
        yield node
    for child in node.children:
        yield from iter_compound_statements(child)


def noncomment_named_children(node: Node) -> list[Node]:
    """Return a node's direct named children excluding comments."""
    return [child for child in node.named_children if child.type != "comment"]


def else_compound_body(alternative: Node) -> Node | None:
    """Get the compound body of an else clause, skipping else-if chains."""
    for child in alternative.children:
        if child.type == "compound_statement":
            return child
        if child.type == "if_statement":
            return None
    return None


def trailing_run(
    stmts: list[Node],
    predicate: Callable[[Node], bool],
) -> list[Node]:
    """Return the maximal trailing run of statements matching predicate."""
    end = len(stmts)
    start = end
    for i in range(end - 1, -1, -1):
        if predicate(stmts[i]):
            start = i
        else:
            break
    return stmts[start:end]


def is_bare_return_statement(node: Node, source: bytes) -> bool:
    """Check whether a node is a bare `return;` statement."""
    if node.type != "return_statement":
        return False
    return source[node.start_byte:node.end_byte].strip() in (b"return;", b"return ;")
