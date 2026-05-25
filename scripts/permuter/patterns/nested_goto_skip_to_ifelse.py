"""nested_goto_skip_to_ifelse — convert nested-if `goto L;` (where L sits in an
outer scope) into a merged-condition `if(!...)` wrapping the skipped statements.

Recognizes the chained-sibling forward-skip idiom that the decomp uses when a
goto skips the rest of an outer if's body. Two shapes are supported:

**Shape A** — sibling stmts AFTER the nested goto, BEFORE the outer if closes:

    if (outerCond) {                  ->   if (outerCond) {
        if (innerCond) {                       if (!innerCond || ... || !deepestCond) {
            ...                                    POST;
            if (deepestCond) goto L;           }
            ...                                }
        }                                  L: (removed)
        POST;                              ...
    }
    L:
    ...

**Shape B** — no POST inside outer; statements between outer-if and L instead:

    if (outerCond) {                  ->   if (!outerCond || !innerCond || ... || !deepestCond) {
        if (innerCond) {                       A;
            ...                            }
            if (deepestCond) goto L;       L: (removed)
            ...                            ...
        }
    }
    A;
    L:
    ...

The pattern fires when:
  * The goto is inside a chain of `if (cond) {...}` blocks (no else) at the
    bottom; each enclosing-if's body contains exactly the next nested if (with
    optional non-goto stmts in the deepest body — but the deepest if must end
    in the goto).
  * The chain bottoms out at an outer if whose body either ends with POST
    stmts (Shape A) or is just the chain (Shape B).
  * The label L is the next labeled_statement sibling of the outer if, at L's
    scope.
  * L has exactly ONE incoming goto.

Use case: OvershellSlot::UpdateState (5 chained gotos), OvershellPanel::ResolveSlotStates,
TrackWatcherImpl::CheckForRolls/CheckForAutoplay, BandDirector::OnMidiShotCategory/OnSelectCamera.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant


class NestedGotoSkipToIfElse(Pattern):
    name = "nested_goto_skip_to_ifelse"
    safety_tier = "normal"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0
        seen_labels: set[bytes] = set()

        for goto in _walk_gotos(ctx.body_node):
            lbl = goto.child_by_field_name("label")
            if lbl is None:
                continue
            label_name = source[lbl.start_byte:lbl.end_byte].strip()
            if label_name in seen_labels:
                continue  # already handled (each label processed once)

            # Walk up the if-chain from the goto. The goto must be the
            # consequence of an `if (cond)` (possibly wrapped in a compound).
            deepest_if = _goto_innermost_if(goto)
            if deepest_if is None:
                continue
            # Walk up: each parent must be either a compound_statement that is
            # the body of another if-statement (no else), OR we've reached an
            # outer if-statement whose body has POST stmts after our chain.
            chain = [deepest_if]
            outer_if, post_stmts = _walk_up_if_chain(deepest_if, source)
            if outer_if is None:
                continue
            chain_top = outer_if

            # Find the label as a sibling AFTER chain_top in chain_top's
            # enclosing compound_statement. If chain_top is wrapped in a
            # labeled_statement (the previous label of a chain like
            # `next17: if (...) { ...goto next3b; } next3b: ...`), walk up
            # through the wrapping labels until we hit the compound.
            wrapper = chain_top
            while wrapper.parent is not None and wrapper.parent.type == "labeled_statement":
                wrapper = wrapper.parent
            parent_compound = wrapper.parent
            if parent_compound is None or parent_compound.type != "compound_statement":
                continue
            siblings = noncomment_named_children(parent_compound)
            try:
                top_idx = next(i for i, s in enumerate(siblings) if s.id == wrapper.id)
            except StopIteration:
                continue

            # Locate the labeled_statement L among siblings after chain_top.
            label_idx = None
            for j in range(top_idx + 1, len(siblings)):
                s = siblings[j]
                if s.type == "labeled_statement":
                    s_lbl = s.child_by_field_name("label")
                    if s_lbl is not None and source[s_lbl.start_byte:s_lbl.end_byte].strip() == label_name:
                        label_idx = j
                        break
                    # A different label in between: refuse for safety.
                    break

            if label_idx is None:
                continue

            # Count incoming gotos for the label
            if _count_gotos_to(label_name, ctx.body_node, source) != 1:
                continue

            # Reconstruct the conditions chain from chain_top down to deepest_if
            conditions = _conditions_chain(chain_top, deepest_if, source)
            if conditions is None:
                continue

            # Build the merged "skip when ALL conds true" condition's NEGATION:
            #   !cond1 || !cond2 || ... || !condN
            negated_terms = [_negate_paren(c, source) for c in conditions]
            merged_cond = b" || ".join(negated_terms)

            # Now decide Shape A vs Shape B based on outer_if body:
            outer_body = outer_if.child_by_field_name("consequence")
            if outer_body is None or outer_body.type != "compound_statement":
                continue
            outer_inner_stmts = noncomment_named_children(outer_body)
            # The first stmt should be the next-level if (chain[1] when len > 1
            # else the deepest_if itself). We don't strictly need to validate
            # this; just check whether there are any non-if stmts AFTER the
            # chain start within outer_body — those are POST stmts (Shape A).
            chain_in_outer = outer_inner_stmts[0]
            post_in_outer = outer_inner_stmts[1:]

            # Outer-if's parent is parent_compound; between outer_if and L
            # there may also be A stmts (Shape B).
            between_outer_and_label = siblings[top_idx + 1:label_idx]

            yield from _emit_variant(
                ctx=ctx,
                source=source,
                outer_if=outer_if,
                outer_body=outer_body,
                chain_in_outer=chain_in_outer,
                post_in_outer=post_in_outer,
                between_outer_and_label=between_outer_and_label,
                labeled_stmt=siblings[label_idx],
                merged_cond=merged_cond,
                outer_conditions=conditions,
                counter=counter,
                label_name=label_name,
            )
            counter += 1
            seen_labels.add(label_name)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _walk_gotos(root: Node) -> Iterator[Node]:
    stack: list[Node] = [root]
    while stack:
        n = stack.pop()
        if n.type == "goto_statement":
            yield n
        stack.extend(n.children)


def _count_gotos_to(label: bytes, root: Node, source: bytes) -> int:
    count = 0
    stack: list[Node] = [root]
    while stack:
        n = stack.pop()
        if n.type == "goto_statement":
            lbl = n.child_by_field_name("label")
            if lbl is not None and source[lbl.start_byte:lbl.end_byte].strip() == label:
                count += 1
        stack.extend(n.children)
    return count


def _goto_innermost_if(goto: Node) -> Node | None:
    """The innermost if-statement whose consequence is just this goto.

    The goto's parent can be:
      * an if_statement (consequence = goto)
      * a compound_statement that is the consequence of an if_statement and
        contains only this goto
    """
    parent = goto.parent
    if parent is None:
        return None
    if parent.type == "if_statement":
        return parent if _if_has_no_alternative(parent) else None
    if parent.type == "compound_statement":
        children = noncomment_named_children(parent)
        if len(children) != 1:
            return None
        if children[0].id != goto.id:
            return None
        grand = parent.parent
        if grand is not None and grand.type == "if_statement" and _if_has_no_alternative(grand):
            return grand
    return None


def _if_has_no_alternative(if_node: Node) -> bool:
    return if_node.child_by_field_name("alternative") is None


def _walk_up_if_chain(deepest_if: Node, source: bytes) -> tuple[Node | None, list[Node]]:
    """Walk up parent if-statements while each parent's body contains exactly
    one statement (the next-deeper if). Return the outermost if and the list
    of POST statements in its body (after the chain start).

    Returns (outer_if, post_stmts). If no valid chain is found, returns (None, []).
    """
    current = deepest_if
    while True:
        parent = current.parent
        if parent is None:
            break
        # parent should be a compound_statement which is the consequence of
        # an enclosing if_statement.
        if parent.type != "compound_statement":
            break
        grand = parent.parent
        if grand is None or grand.type != "if_statement":
            break
        if not _if_has_no_alternative(grand):
            break
        siblings = noncomment_named_children(parent)
        # current must be the FIRST sibling — anything else in the parent
        # compound is potential POST stmts.
        if not siblings or siblings[0].id != current.id:
            break
        if len(siblings) == 1:
            # Parent compound contains exactly the current if; keep climbing.
            current = grand
            continue
        # POST stmts exist; this is our outer if.
        return grand, siblings[1:]
    # Reached the top of the chain with no POST stmts; treat current as
    # the outer if. Caller will detect Shape B based on between-and-label.
    return current, []


def _conditions_chain(outer_if: Node, deepest_if: Node, source: bytes) -> list[Node] | None:
    """Return the list of condition_clause nodes from outer_if down to
    deepest_if. Returns None if the chain is malformed.
    """
    conds: list[Node] = []
    current = outer_if
    while True:
        cond = current.child_by_field_name("condition")
        if cond is None:
            return None
        conds.append(cond)
        if current.id == deepest_if.id:
            return conds
        body = current.child_by_field_name("consequence")
        if body is None:
            return None
        # Find the next if in the chain.
        if body.type == "if_statement":
            current = body
            continue
        if body.type == "compound_statement":
            children = noncomment_named_children(body)
            # The next if must be the first child.
            if not children:
                return None
            first = children[0]
            if first.type != "if_statement":
                return None
            current = first
            continue
        return None


def _negate_paren(condition: Node, source: bytes) -> bytes:
    """Return the negation of a condition_clause `(expr)`. Tries operator
    inversion / unary-! stripping, falls back to `!(...)` wrap.
    """
    _NEGATABLE = {"<": ">=", ">": "<=", "<=": ">", ">=": "<", "==": "!=", "!=": "=="}
    inner = None
    for c in condition.named_children:
        if c.type != "comment":
            inner = c
            break
    if inner is None:
        return source[condition.start_byte:condition.end_byte]

    if inner.type == "binary_expression":
        op = inner.child_by_field_name("operator")
        if op is not None and op.text:
            op_str = op.text.decode("utf-8")
            if op_str in _NEGATABLE:
                new_op = _NEGATABLE[op_str].encode("utf-8")
                return (
                    source[condition.start_byte:op.start_byte]
                    + new_op
                    + source[op.end_byte:condition.end_byte]
                )

    if inner.type == "unary_expression":
        op = inner.child_by_field_name("operator")
        if op is not None and op.text == b"!":
            arg = inner.child_by_field_name("argument")
            if arg is not None:
                return b"(" + source[arg.start_byte:arg.end_byte] + b")"

    inner_text = source[inner.start_byte:inner.end_byte]
    if inner.type in ("identifier", "field_expression", "call_expression",
                      "subscript_expression", "parenthesized_expression"):
        return b"(!" + inner_text + b")"
    return b"(!(" + inner_text + b"))"


def _line_indent(source: bytes, byte_off: int) -> bytes:
    start = source.rfind(b"\n", 0, byte_off)
    start = 0 if start < 0 else start + 1
    out = []
    for b in source[start:byte_off]:
        if b in (0x20, 0x09):
            out.append(b)
        else:
            break
    return bytes(out)


def _emit_variant(
    *,
    ctx: FunctionContext,
    source: bytes,
    outer_if: Node,
    outer_body: Node,
    chain_in_outer: Node,
    post_in_outer: list[Node],
    between_outer_and_label: list[Node],
    labeled_stmt: Node,
    merged_cond: bytes,
    outer_conditions: list[Node],
    counter: int,
    label_name: bytes,
) -> Iterator[Variant]:
    """Generate the rewrite variant. Picks Shape A or B based on which list
    contains the to-be-wrapped statements.
    """
    indent_outer = _line_indent(source, outer_if.start_byte)

    if post_in_outer:
        # Shape A: wrap POST stmts inside the outer if. The merged_cond drops
        # the outermost condition since we keep `if (outerCond)` as is.
        # Recompute: skip the outermost condition (outer_conditions[0]) from
        # the negated chain.
        if len(outer_conditions) <= 1:
            return  # nothing to merge
        inner_terms = [_negate_paren(c, source) for c in outer_conditions[1:]]
        cond = b" || ".join(inner_terms)
        # The outer body becomes: { <chain stripped down>; if (cond) { POST } }
        # Strip the inner if-chain entirely; rewrite as `if (cond) { POST }`.
        body_indent = indent_outer + b"    "
        block_indent = body_indent + b"    "

        post_text = source[post_in_outer[0].start_byte:post_in_outer[-1].end_byte]

        new_block = (
            b"if " + (b"(" + cond + b")") + b" {\n"
            + block_indent + post_text.replace(b"\n", b"\n" + block_indent).rstrip(b" \t") + b"\n"
            + body_indent + b"}"
        )

        # Replace from outer_body's open `{` content start to labeled_stmt end
        # with: <outer_body's open `{` kept> + body_indent + new_block + indent + `}` (close outer body)
        outer_open = outer_body.start_byte  # the `{`
        outer_close = outer_body.end_byte    # past the `}`
        outer_body_text = (
            source[outer_open:outer_open + 1]  # the `{`
            + b"\n" + body_indent + new_block + b"\n"
            + indent_outer + b"}"
        )

        new_source = (
            source[:outer_open]
            + outer_body_text
            + source[outer_close:labeled_stmt.start_byte]
            + source[labeled_stmt.start_byte + len(_label_prefix(labeled_stmt, source)):]
        )
    else:
        # Shape B: between_outer_and_label has stmts to wrap; rewrite the outer
        # if entirely (drop it, replace with `if (merged_cond) { A }`).
        if not between_outer_and_label:
            return  # nothing to wrap

        body_indent = indent_outer + b"    "
        a_text = source[between_outer_and_label[0].start_byte:between_outer_and_label[-1].end_byte]

        new_block = (
            b"if (" + merged_cond + b") {\n"
            + body_indent + a_text.replace(b"\n", b"\n" + body_indent).rstrip(b" \t") + b"\n"
            + indent_outer + b"}"
        )

        new_source = (
            source[:outer_if.start_byte]
            + new_block
            + source[between_outer_and_label[-1].end_byte:labeled_stmt.start_byte]
            + source[labeled_stmt.start_byte + len(_label_prefix(labeled_stmt, source)):]
        )

    yield Variant(
        name=f"nested_goto_{counter}",
        pattern_name="nested_goto_skip_to_ifelse",
        description=f"Merge nested-if conditions to skip past `goto {label_name.decode()}`",
        source=new_source,
    )


def _label_prefix(labeled: Node, source: bytes) -> bytes:
    """Return `LABEL:` prefix (incl colon) as bytes, so caller can strip it
    from the labeled_statement leaving only the body.
    """
    # The labeled_statement starts with statement_identifier, then `:`. Skip past `:`.
    for child in labeled.children:
        if child.type == ":":
            return source[labeled.start_byte:child.end_byte]
    return b""
