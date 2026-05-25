"""bare_label_loop_to_while — convert MWCC's bare-label rotated-loop idiom to a
structured `while (true) { ...; if (!cond) break; ... }` loop.

This is the SIBLING-LABEL variant of ``loop_rotation_to_while``. The latter
fires on the form where the decomp preserved a literal ``do { ... } while ();``
container in the source. The bare-label form lives in functions where MWCC
emitted only labels and gotos at the same scope (no surrounding ``do``):

    goto LBL_CHECK;
  LBL_BODY:
    body_stmts;
  LBL_CHECK:
    pre_stmts;
    if (cond) goto LBL_BODY;

Both forms decompile to the same asm, so the rewrite preserves match% by the
same optimizer rotation that produces the do-while shape:

    while (true) {
        pre_stmts;
        if (!cond) break;
        body_stmts;
    }

The pattern fires when:
  * A `goto LBL_CHECK;` statement is immediately followed by a
    `labeled_statement LBL_BODY:` at the same scope.
  * That labeled body's text contains a `LBL_CHECK:` labeled_statement among
    the same-scope siblings AFTER the body label.
  * Inside the LBL_CHECK section (the labeled body + the statements after it,
    up to and including the trailing `if (cond) goto LBL_BODY;`) the body's
    last statement is an `if (cond) goto LBL_BODY;`.
  * LBL_BODY has EXACTLY ONE incoming goto (the trailing one) and LBL_CHECK
    has EXACTLY ONE incoming goto (the leading one).

Use case: BandWardrobe::FlagString and similar functions where MWCC's
codegen folds the loop check below the body without emitting a `do` keyword
in the decomp.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import iter_compound_statements, noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant


_NEGATABLE_OPS = {"<": ">=", ">": "<=", "<=": ">", ">=": "<", "==": "!=", "!=": "=="}


class BareLabelLoopToWhile(Pattern):
    name = "bare_label_loop_to_while"
    safety_tier = "conservative"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        for compound in iter_compound_statements(ctx.body_node):
            stmts = noncomment_named_children(compound)
            yield from self._generate_for_block(stmts, ctx, compound, source, counter)
            # counter not incremented across blocks; let helper handle naming.

    def _generate_for_block(
        self,
        stmts: list[Node],
        ctx: FunctionContext,
        compound: Node,
        source: bytes,
        counter: int,
    ) -> Iterator[Variant]:
        for i in range(len(stmts) - 1):
            goto = stmts[i]
            body_label = stmts[i + 1]
            if goto.type != "goto_statement":
                continue
            if body_label.type != "labeled_statement":
                continue

            check_label_name = _goto_label(goto, source)
            if check_label_name is None:
                continue

            body_label_name = _labeled_name(body_label, source)
            if body_label_name is None or body_label_name == check_label_name:
                continue

            # Find the LBL_CHECK labeled_statement among siblings after body_label.
            check_idx = None
            for j in range(i + 2, len(stmts)):
                s = stmts[j]
                if s.type == "labeled_statement":
                    nm = _labeled_name(s, source)
                    if nm == check_label_name:
                        check_idx = j
                        break
                    # Refuse if any OTHER label sits between body_label and check_label.
                    return

            if check_idx is None:
                continue

            # Verify exactly one incoming goto for each label in the whole function.
            if _count_gotos_to(check_label_name, ctx.body_node, source) != 1:
                continue
            if _count_gotos_to(body_label_name, ctx.body_node, source) != 1:
                continue

            # Body region: the body_label's own labeled body + siblings up to but
            # excluding the check label.
            body_stmts_chunks: list[tuple[bytes, int]] = []
            body_label_body = _labeled_body_node(body_label)
            if body_label_body is not None and _is_meaningful(body_label_body, source):
                body_stmts_chunks.append((
                    source[body_label_body.start_byte:body_label_body.end_byte],
                    _line_col(source, body_label_body.start_byte),
                ))
            for k in range(i + 2, check_idx):
                s = stmts[k]
                if s.type == "labeled_statement":
                    # Already refused above; defensive.
                    return
                body_stmts_chunks.append((
                    source[s.start_byte:s.end_byte],
                    _line_col(source, s.start_byte),
                ))

            # Check region: the check_label's own labeled body + siblings after it.
            check_label = stmts[check_idx]
            check_label_body = _labeled_body_node(check_label)

            # Collect the check region as a list of nodes. The first element (if
            # present and meaningful) is the inner statement of the labeled_statement
            # (e.g. `setup();` in `loop_check: setup();`). Subsequent elements are
            # the siblings that follow the labeled_statement at the same scope.
            check_region: list[Node] = []
            if check_label_body is not None and _is_meaningful(check_label_body, source):
                check_region.append(check_label_body)
            for k in range(check_idx + 1, len(stmts)):
                check_region.append(stmts[k])

            if not check_region:
                continue

            # Locate the if-goto-to-LBL_BODY in the check region. It must exist
            # exactly once (the body label has exactly one incoming goto, which
            # we've already verified), and we expect to find it within the check
            # region. The statements BEFORE it are pre-iteration work that runs
            # inside the loop. The statements AFTER it are post-loop tail code
            # that stays outside the loop in the rewrite.
            trigger_idx = None
            for k, n in enumerate(check_region):
                if _extract_goto_from_if(n, source) == body_label_name:
                    trigger_idx = k
                    break
            if trigger_idx is None:
                continue
            trigger = check_region[trigger_idx]
            condition = trigger.child_by_field_name("condition")
            if condition is None:
                continue

            # Pre statements: everything in check_region BEFORE the if-goto.
            pre_nodes = check_region[:trigger_idx]
            tail_nodes = check_region[trigger_idx + 1:]
            pre_chunks: list[tuple[bytes, int]] = []
            for n in pre_nodes:
                pre_chunks.append((
                    source[n.start_byte:n.end_byte],
                    _line_col(source, n.start_byte),
                ))

            # Refuse if any pre statement transfers control out at top level
            # (return / goto / further labels).
            if _has_top_level_exit(pre_nodes):
                return

            # Refuse if any body statement transfers control out at top level
            # (other than the goto we are absorbing, which is in the check side).
            # body_stmts_chunks come from non-labeled, non-trailing siblings.
            body_check_nodes: list[Node] = []
            if body_label_body is not None and _is_meaningful(body_label_body, source):
                body_check_nodes.append(body_label_body)
            for k in range(i + 2, check_idx):
                body_check_nodes.append(stmts[k])
            if _has_top_level_exit(body_check_nodes):
                return

            cond_expr = _negate_condition(condition, source)

            indent = _line_indent(source, goto.start_byte)
            body_indent = indent + b"    "

            lines: list[bytes] = []
            lines.append(b"while (true) {")
            for chunk, base_col in pre_chunks:
                lines.append(_reindent(chunk, body_indent, base_col))
            lines.append(body_indent + b"if " + cond_expr + b" break;")
            for chunk, base_col in body_stmts_chunks:
                lines.append(_reindent(chunk, body_indent, base_col))
            lines.append(indent + b"}")
            new_block = b"\n".join(lines)

            # Replace byte range from goto.start_byte through trigger.end_byte;
            # any tail_nodes that followed the if-goto stay in source verbatim.
            new_source = (
                source[:goto.start_byte]
                + new_block
                + source[trigger.end_byte:]
            )

            yield Variant(
                name=f"bare_label_loop_{counter}",
                pattern_name="bare_label_loop_to_while",
                description=(
                    f"Rewrite `goto {check_label_name.decode()}; {body_label_name.decode()}: ...`"
                    f" bare-label loop as `while (true) {{...}}`"
                ),
                source=new_source,
            )
            counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _goto_label(stmt: Node, source: bytes) -> bytes | None:
    if stmt.type != "goto_statement":
        return None
    lbl = stmt.child_by_field_name("label")
    if lbl is None:
        return None
    return source[lbl.start_byte:lbl.end_byte].strip()


def _labeled_name(labeled: Node, source: bytes) -> bytes | None:
    if labeled.type != "labeled_statement":
        return None
    lbl = labeled.child_by_field_name("label")
    if lbl is None:
        return None
    return source[lbl.start_byte:lbl.end_byte].strip()


def _labeled_body_node(labeled: Node) -> Node | None:
    seen_colon = False
    for child in labeled.children:
        if child.type == ":":
            seen_colon = True
            continue
        if seen_colon and child.is_named:
            return child
    return None


def _is_meaningful(body: Node, source: bytes) -> bool:
    if body.type == "expression_statement":
        text = source[body.start_byte:body.end_byte].strip()
        if text == b";":
            return False
    return True


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


def _has_top_level_exit(stmts: list[Node]) -> bool:
    for s in stmts:
        if s.type in ("goto_statement", "return_statement", "labeled_statement"):
            return True
    return False


def _negate_condition(condition: Node, source: bytes) -> bytes:
    """Return text of a negated condition_clause `(expr)`. Always parenthesized."""
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


def _line_col(source: bytes, byte_off: int) -> int:
    start = source.rfind(b"\n", 0, byte_off)
    start = 0 if start < 0 else start + 1
    col = 0
    for b in source[start:byte_off]:
        if b == 0x20:
            col += 1
        elif b == 0x09:
            col += 8 - (col % 8)
        else:
            break
    return col


def _reindent(chunk: bytes, indent: bytes, base_col: int) -> bytes:
    """Reindent a multi-line chunk so its first line starts at `indent`, with
    relative indentation preserved for subsequent lines.
    """
    lines = chunk.split(b"\n")
    if not lines:
        return chunk

    out_lines: list[bytes] = [indent + lines[0].lstrip()]
    for line in lines[1:]:
        if not line.strip():
            out_lines.append(b"")
            continue
        col = 0
        i = 0
        while i < len(line) and col < base_col:
            b = line[i]
            if b == 0x20:
                col += 1
            elif b == 0x09:
                col += 8 - (col % 8)
            else:
                break
            i += 1
        out_lines.append(indent + line[i:])
    return b"\n".join(out_lines)
