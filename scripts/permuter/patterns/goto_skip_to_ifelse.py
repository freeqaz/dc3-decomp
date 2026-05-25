"""goto_skip_to_ifelse — convert forward-skip gotos to if/else.

Recognizes the "skip-forward" goto idiom that decomp often produces as a
literal translation of the asm, and rewrites it to the equivalent if/else:

    if (cond) goto L;        ->   if (!cond) {
    A;                                  A;
    L:                              }
    B;                              B;

Also handles the variant where the goto is wrapped in a compound_statement:

    if (cond) {              ->   if (!cond) {
        goto L;                       A;
    }                             }
    A;                            B;
    L:
    B;

Only fires when the label has exactly one incoming goto (the one being
rewritten) within the function, so deleting both is safe. The intervening
statements must not transfer control out of the block (no other gotos,
returns, labels at the top level).

Use case: cleanup of decomp scaffolding without regressing match%. Run after
a function reaches 100% to test whether the if/else form ALSO matches; run
before 100% to try the rewrite as one path toward closing the gap.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import iter_compound_statements, noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant


_NEGATABLE_OPS = {"<": ">=", ">": "<=", "<=": ">", ">=": "<", "==": "!=", "!=": "=="}


class GotoSkipToIfElse(Pattern):
    name = "goto_skip_to_ifelse"
    safety_tier = "conservative"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = [0]
        for compound in iter_compound_statements(ctx.body_node):
            stmts = noncomment_named_children(compound)
            yield from _generate_for_block(stmts, ctx, counter)


def _goto_label(stmt: Node, source: bytes) -> bytes | None:
    """If `stmt` is a `goto LABEL;`, return the label name. Else None."""
    if stmt.type != "goto_statement":
        return None
    lbl = stmt.child_by_field_name("label")
    if lbl is None:
        return None
    return source[lbl.start_byte:lbl.end_byte].strip()


def _extract_goto_from_if(if_node: Node, source: bytes) -> bytes | None:
    """If if_node is `if (cond) goto L;` (no else), return label name."""
    if if_node.type != "if_statement":
        return None
    if if_node.child_by_field_name("alternative") is not None:
        return None
    cons = if_node.child_by_field_name("consequence")
    if cons is None:
        return None
    if cons.type == "goto_statement":
        return _goto_label(cons, source)
    if cons.type == "compound_statement":
        inner = noncomment_named_children(cons)
        if len(inner) == 1 and inner[0].type == "goto_statement":
            return _goto_label(inner[0], source)
    return None


def _find_label_index(stmts: list[Node], start_idx: int, label: bytes) -> int | None:
    """Find the index of `labeled_statement` with the given label in stmts[start_idx:]."""
    for i in range(start_idx, len(stmts)):
        s = stmts[i]
        if s.type == "labeled_statement":
            lbl = s.child_by_field_name("label")
            if lbl is not None and source_bytes_of(lbl).strip() == label:
                return i
    return None


def source_bytes_of(node: Node) -> bytes:
    """Helper — return the node's raw bytes via .text."""
    return node.text if node.text is not None else b""


def _count_gotos_to(label: bytes, root: Node, source: bytes) -> int:
    """Count goto statements targeting `label` anywhere within `root`."""
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


def _block_has_top_level_exit(stmts: list[Node]) -> bool:
    """Return True if any top-level statement transfers control out (goto,
    return) or introduces another label. Nested control flow inside if/loops
    is fine — we don't break out of those when we wrap with if/else.
    """
    for s in stmts:
        if s.type in ("goto_statement", "return_statement", "labeled_statement"):
            return True
    return False


def _negate_condition(condition: Node, source: bytes) -> bytes:
    """Return the byte string for a negated `condition_clause` like `(cond)`.

    Tries (in order):
      1. Operator inversion for binary comparisons (<, >, ==, etc.)
      2. Strip a leading `!` if present
      3. Wrap with `!(...)`
    """
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
            if op_str in _NEGATABLE_OPS:
                new_op = _NEGATABLE_OPS[op_str].encode("utf-8")
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
    # Identifiers and simple calls don't need a redundant inner paren.
    if inner.type in ("identifier", "field_expression", "call_expression",
                      "subscript_expression", "parenthesized_expression"):
        return b"(!" + inner_text + b")"
    return b"(!(" + inner_text + b"))"


def _labeled_body_text(labeled: Node, source: bytes) -> bytes:
    """Return the text following the `LABEL:` of a labeled_statement, stripped
    of leading whitespace. Returns b"" if the label has only a `;` body.
    """
    # labeled_statement children: statement_identifier, ":", <body>
    colon_end = None
    for child in labeled.children:
        if child.type == ":":
            colon_end = child.end_byte
            break
    if colon_end is None:
        return b""
    body = source[colon_end:labeled.end_byte].lstrip()
    # Drop pure-`;` body (the `L:;` idiom)
    if body.strip() == b";":
        return b""
    return body


def _line_indent(source: bytes, byte_off: int) -> bytes:
    """Return the indent string (whitespace) preceding the line at byte_off."""
    start = source.rfind(b"\n", 0, byte_off)
    if start < 0:
        start = 0
    else:
        start += 1
    end = byte_off
    out = []
    for b in source[start:end]:
        if b in (0x20, 0x09):
            out.append(b)
        else:
            break
    return bytes(out)


def _generate_for_block(
    stmts: list[Node], ctx: FunctionContext, counter: list
) -> Iterator[Variant]:
    source = ctx.file_source
    n = len(stmts)
    for i, stmt in enumerate(stmts):
        if stmt.type != "if_statement":
            continue
        label = _extract_goto_from_if(stmt, source)
        if label is None:
            continue

        label_idx = _find_label_index(stmts, i + 1, label)
        if label_idx is None:
            continue

        between = stmts[i + 1:label_idx]
        if not between:
            continue
        if _block_has_top_level_exit(between):
            continue

        n_gotos = _count_gotos_to(label, ctx.body_node, source)
        if n_gotos != 1:
            continue

        condition = stmt.child_by_field_name("condition")
        if condition is None:
            continue

        labeled = stmts[label_idx]
        labeled_body = _labeled_body_text(labeled, source)
        # Skip when label body is just `;` and there are no following statements
        # (the rewrite would still be valid, but it's typically pointless).

        between_text = source[between[0].start_byte:between[-1].end_byte]

        indent = _line_indent(source, stmt.start_byte)
        body_indent = indent + b"    "

        # Re-indent each non-empty line of `between_text` to body_indent.
        between_lines = between_text.split(b"\n")
        reindented: list[bytes] = []
        for idx, line in enumerate(between_lines):
            if idx == 0:
                reindented.append(body_indent + line.lstrip())
            else:
                stripped = line.lstrip()
                if not stripped:
                    reindented.append(b"")
                else:
                    reindented.append(body_indent + stripped)
        between_block = b"\n".join(reindented)

        neg_cond = _negate_condition(condition, source)

        # Build the replacement: byte range from stmt.start_byte through
        # labeled.end_byte gets rewritten to:
        #   if <neg_cond> {
        #       <between_block>
        #   }
        #   <labeled_body>
        new_block = (
            b"if " + neg_cond + b" {\n"
            + between_block + b"\n"
            + indent + b"}"
        )
        if labeled_body.strip():
            new_block += b"\n" + indent + labeled_body
        new_source = (
            source[:stmt.start_byte]
            + new_block
            + source[labeled.end_byte:]
        )

        yield Variant(
            name=f"goto_skip_{counter[0]}",
            pattern_name="goto_skip_to_ifelse",
            description=f"Eliminate `goto {label.decode()}` skip with negated if",
            source=new_source,
        )
        counter[0] += 1
