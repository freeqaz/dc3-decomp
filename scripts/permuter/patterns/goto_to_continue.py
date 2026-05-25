"""goto_to_continue — replace `goto L;` with `continue;` when L marks the end
of an enclosing loop body.

Recognizes the common decomp shape where a goto at the end of an iteration is
used to skip the remaining statements in the loop body:

    for (...) {              ->   for (...) {
        ...                            ...
        if (cond) goto L;                  if (cond) continue;
        ...                            ...
    L:;                            }
    }

The pattern fires when:
  * Label L's body is empty (`;`)
  * L is the LAST top-level statement of an enclosing loop body (for/while/do)
  * Every incoming goto to L is inside that same loop (transitively)
  * The label has at least one incoming goto

The label is then stripped entirely, since after replacing the gotos with
`continue;` it has no incoming control flow.

Use case: BandWardrobe::MostImportantHuman has exactly this idiom with `next:;`
as the last statement of a `for (int i = 0; i < 4; i++)` body.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant


_LOOP_NODES = frozenset({"for_statement", "while_statement", "do_statement"})


class GotoToContinue(Pattern):
    name = "goto_to_continue"
    safety_tier = "conservative"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source

        # Find all labeled_statements where the label sits at the end of an
        # enclosing loop body with an empty body.
        candidates: list[tuple[bytes, Node, Node]] = []
        for labeled, loop in _find_end_of_loop_labels(ctx.body_node):
            lbl = labeled.child_by_field_name("label")
            if lbl is None:
                continue
            label_name = source[lbl.start_byte:lbl.end_byte].strip()
            candidates.append((label_name, labeled, loop))

        if not candidates:
            return

        counter = 0
        for label_name, labeled, loop in candidates:
            gotos = list(_find_gotos_to(label_name, ctx.body_node, source))
            if not gotos:
                continue
            # All gotos must be inside the enclosing loop.
            if not all(_is_descendant(g, loop) for g in gotos):
                continue

            # Build edits: replace each goto with `continue;`, then strip label.
            edits: list[tuple[int, int, bytes]] = []
            for g in gotos:
                edits.append((g.start_byte, g.end_byte, b"continue;"))
            edits.append((labeled.start_byte, labeled.end_byte, b""))
            edits.sort(key=lambda e: e[0], reverse=True)

            new_source = source
            for start, end, replacement in edits:
                new_source = new_source[:start] + replacement + new_source[end:]

            yield Variant(
                name=f"goto_cont_{counter}",
                pattern_name="goto_to_continue",
                description=f"Replace `goto {label_name.decode()}` with continue (end-of-loop label)",
                source=new_source,
            )
            counter += 1


def _find_end_of_loop_labels(root: Node) -> Iterator[tuple[Node, Node]]:
    """Yield (labeled_statement, enclosing_loop) pairs where:
      * the labeled_statement is the last named child of a loop body, AND
      * the label's body is empty (`;`).
    """
    stack: list[Node] = [root]
    while stack:
        n = stack.pop()
        if n.type in _LOOP_NODES:
            body = n.child_by_field_name("body")
            if body is not None and body.type == "compound_statement":
                stmts = noncomment_named_children(body)
                if stmts:
                    last = stmts[-1]
                    if last.type == "labeled_statement" and _has_empty_body(last):
                        yield (last, n)
        stack.extend(n.children)


def _has_empty_body(labeled: Node) -> bool:
    """Return True if `labeled`'s body is empty (`;`)."""
    body = _labeled_body_node(labeled)
    if body is None:
        return True
    if body.type == "expression_statement":
        return body.text is not None and body.text.strip() == b";"
    return False


def _labeled_body_node(labeled: Node) -> Node | None:
    seen_colon = False
    for child in labeled.children:
        if child.type == ":":
            seen_colon = True
            continue
        if seen_colon and child.is_named:
            return child
    return None


def _find_gotos_to(label_name: bytes, root: Node, source: bytes) -> Iterator[Node]:
    stack: list[Node] = [root]
    while stack:
        n = stack.pop()
        if n.type == "goto_statement":
            lbl = n.child_by_field_name("label")
            if lbl is not None and source[lbl.start_byte:lbl.end_byte].strip() == label_name:
                yield n
        stack.extend(n.children)


def _is_descendant(node: Node, ancestor: Node) -> bool:
    # tree-sitter wraps the same underlying node as distinct Python objects
    # on each traversal, so identity comparison fails. Compare by id instead.
    cur = node.parent
    while cur is not None:
        if cur.id == ancestor.id:
            return True
        cur = cur.parent
    return False
