"""goto_to_return — replace `goto L;` with the return statement at label L.

Recognizes the common decomp shape where the only purpose of a goto is to
short-circuit the function with a fixed return value:

    if (cond) goto L;        ->   if (cond) return ret;
    A;                            A;
    B;                            B;
    L:                            return ret;
    return ret;

The pattern fires when:
  * Label L is at the FUNCTION-BODY level (top-level compound of the function)
  * L's body is exactly ONE statement of the form `return;` or `return EXPR;`
  * Label L has exactly ONE incoming goto (the one being rewritten)

It substitutes the return at every goto site (only one such site by constraint)
and strips the label, keeping the trailing return as the natural fall-through
terminator.

Use case: cleans up the goto idiom in functions like NextSongPanel::Exiting and
TrackWatcherImpl::ClosestUnplayedGem where the decomp has a single `goto done;`
or `goto oh;` pointing at a label whose body is just `return X;`.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant


class GotoToReturn(Pattern):
    name = "goto_to_return"
    safety_tier = "conservative"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        body_stmts = noncomment_named_children(ctx.body_node)
        source = ctx.file_source
        counter = 0

        # Build a map: label name -> (labeled_statement node, return-body node)
        # ONLY for labels whose body is `return [EXPR];`
        return_labels: dict[bytes, tuple[Node, Node]] = {}
        for stmt in body_stmts:
            if stmt.type != "labeled_statement":
                continue
            lbl = stmt.child_by_field_name("label")
            if lbl is None:
                continue
            return_body = _labeled_return_body(stmt)
            if return_body is None:
                continue
            label_name = source[lbl.start_byte:lbl.end_byte].strip()
            return_labels[label_name] = (stmt, return_body)

        if not return_labels:
            return

        # Walk the function body and find all goto_statements targeting one of
        # those labels. For each, check the label has exactly one incoming goto.
        gotos_by_label: dict[bytes, list[Node]] = {}
        for goto in _walk_gotos(ctx.body_node):
            lbl = goto.child_by_field_name("label")
            if lbl is None:
                continue
            name = source[lbl.start_byte:lbl.end_byte].strip()
            if name in return_labels:
                gotos_by_label.setdefault(name, []).append(goto)

        for label_name, (labeled_stmt, return_body) in return_labels.items():
            gotos = gotos_by_label.get(label_name, [])
            if len(gotos) != 1:
                continue
            goto = gotos[0]
            return_text = source[return_body.start_byte:return_body.end_byte]
            if not return_text.rstrip().endswith(b";"):
                return_text = return_text.rstrip() + b";"

            # Build the edit: replace goto with return statement, strip the label.
            # Two non-overlapping edits; apply in reverse order so byte offsets
            # remain stable.
            edits = [
                (goto.start_byte, goto.end_byte, return_text),
                (labeled_stmt.start_byte, labeled_stmt.end_byte, return_text),
            ]
            edits.sort(key=lambda e: e[0], reverse=True)
            new_source = source
            for start, end, replacement in edits:
                new_source = new_source[:start] + replacement + new_source[end:]

            yield Variant(
                name=f"goto_ret_{counter}",
                pattern_name="goto_to_return",
                description=f"Replace `goto {label_name.decode()}` with its target return",
                source=new_source,
            )
            counter += 1


def _labeled_return_body(labeled: Node) -> Node | None:
    """If `labeled` is `L: return [EXPR];`, return the return_statement node.

    Returns None for any other label-body shape (block, compound, multi-stmt,
    non-return).
    """
    body = _labeled_body_node(labeled)
    if body is None:
        return None
    if body.type == "return_statement":
        return body
    # Some decomp wraps the return in a compound_statement: `L: { return X; }`
    if body.type == "compound_statement":
        inner = noncomment_named_children(body)
        if len(inner) == 1 and inner[0].type == "return_statement":
            return inner[0]
    return None


def _labeled_body_node(labeled: Node) -> Node | None:
    """Return the statement node following `LABEL:` of a labeled_statement.

    tree-sitter children: statement_identifier, `:`, <body-statement>.
    """
    seen_colon = False
    for child in labeled.children:
        if child.type == ":":
            seen_colon = True
            continue
        if seen_colon and child.is_named:
            return child
    return None


def _walk_gotos(root: Node) -> Iterator[Node]:
    stack: list[Node] = [root]
    while stack:
        n = stack.pop()
        if n.type == "goto_statement":
            yield n
        stack.extend(n.children)
