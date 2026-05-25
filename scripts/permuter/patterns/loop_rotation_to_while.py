"""loop_rotation_to_while — convert MWCC's `do-while` rotated-with-goto idiom
to a structured `while (true) { ...; if (!cond) break; ... }` loop.

The decomp sometimes preserves the literal asm shape by emitting:

    goto check;
    do {
        <post>             // post-iteration work, runs after every iter except first
    check:
        <pre>              // executed before checking cond on every iter (incl first)
    } while (cond);

This is semantically equivalent to:

    while (true) {
        <pre>
        if (!cond) break;
        <post>
    }

The latter is much more readable: it reads top-down, the loop condition is at
the natural top, and there's no goto. CW's optimizer will rotate back into the
do-while form anyway during codegen.

The pattern fires when:
  * A `goto LABEL;` statement is immediately followed by a `do_statement`
    at the same scope.
  * The do-body's compound_statement contains a `LABEL:` labeled_statement
    among its top-level children.
  * That label has exactly ONE incoming goto in the function (the one we are
    rewriting).

Generates a `while (true) { ... }` variant. Whether this preserves match% is
function-specific — the rewrite is offered for the scorer to validate.

Use case: Movie::Terminate, PatchSticker::FinishLoad, StreamReceiver::Play,
BandWardrobe::FlagString, RandomGroupSeqInst::Poll.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import iter_compound_statements, noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant


class LoopRotationToWhile(Pattern):
    name = "loop_rotation_to_while"
    safety_tier = "normal"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        for compound in iter_compound_statements(ctx.body_node):
            stmts = noncomment_named_children(compound)
            for i in range(len(stmts) - 1):
                goto = stmts[i]
                do_stmt = stmts[i + 1]
                if goto.type != "goto_statement":
                    continue
                if do_stmt.type != "do_statement":
                    continue

                lbl = goto.child_by_field_name("label")
                if lbl is None:
                    continue
                label_name = source[lbl.start_byte:lbl.end_byte].strip()

                do_body = do_stmt.child_by_field_name("body")
                if do_body is None or do_body.type != "compound_statement":
                    continue

                inner_stmts = noncomment_named_children(do_body)
                label_idx = _find_label_idx(inner_stmts, label_name, source)
                if label_idx is None:
                    continue

                # Verify exactly one incoming goto for this label
                if _count_gotos_to(label_name, ctx.body_node, source) != 1:
                    continue

                pre_stmts = inner_stmts[label_idx + 1:]  # after the label
                # Include the labeled_statement's own body as the first <pre> item
                # if it has any (most often it's `;` or has actual content).
                labeled = inner_stmts[label_idx]
                label_body_node = _labeled_body_node(labeled)
                pre_chunks: list[tuple[bytes, int]] = []
                if label_body_node is not None and _is_meaningful(label_body_node, source):
                    pre_chunks.append((
                        source[label_body_node.start_byte:label_body_node.end_byte],
                        _line_indent_at(source, label_body_node.start_byte),
                    ))
                for s in pre_stmts:
                    pre_chunks.append((
                        source[s.start_byte:s.end_byte],
                        _line_indent_at(source, s.start_byte),
                    ))

                post_stmts = inner_stmts[:label_idx]
                post_chunks: list[tuple[bytes, int]] = [
                    (source[s.start_byte:s.end_byte], _line_indent_at(source, s.start_byte))
                    for s in post_stmts
                ]

                cond_node = do_stmt.child_by_field_name("condition")
                if cond_node is None:
                    continue
                # condition is parenthesized_expression — extract inner expr
                cond_expr = _paren_inner(cond_node, source)

                indent = _line_indent(source, goto.start_byte)
                body_indent = indent + b"    "

                lines: list[bytes] = []
                lines.append(b"while (true) {")
                for chunk, base_col in pre_chunks:
                    lines.append(_reindent(chunk, body_indent, base_col))
                lines.append(body_indent + b"if (!(" + cond_expr + b")) break;")
                for chunk, base_col in post_chunks:
                    lines.append(_reindent(chunk, body_indent, base_col))
                lines.append(indent + b"}")
                new_block = b"\n".join(lines)

                # Replace byte range from goto.start to do_stmt.end with new_block.
                new_source = (
                    source[:goto.start_byte]
                    + new_block
                    + source[do_stmt.end_byte:]
                )

                yield Variant(
                    name=f"loop_rot_{counter}",
                    pattern_name="loop_rotation_to_while",
                    description=f"Rewrite `goto {label_name.decode()}; do {{...}} while(...)` as `while (true) {{...}}`",
                    source=new_source,
                )
                counter += 1


def _find_label_idx(stmts: list[Node], label: bytes, source: bytes) -> int | None:
    for i, s in enumerate(stmts):
        if s.type == "labeled_statement":
            lbl = s.child_by_field_name("label")
            if lbl is not None and source[lbl.start_byte:lbl.end_byte].strip() == label:
                return i
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
        if body.text is not None and body.text.strip() == b";":
            return False
    return True


def _paren_inner(paren: Node, source: bytes) -> bytes:
    # parenthesized_expression: `(` <expr> `)`
    for child in paren.named_children:
        if child.type != "comment":
            return source[child.start_byte:child.end_byte]
    # Fallback: strip outer parens textually
    text = source[paren.start_byte:paren.end_byte].strip()
    if text.startswith(b"(") and text.endswith(b")"):
        return text[1:-1].strip()
    return text


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


def _line_indent_at(source: bytes, byte_off: int) -> int:
    """Return the column (0-based) at which the byte at `byte_off` sits on its
    source line. Used to recover the original indentation of a statement whose
    `start_byte` points past the line's leading whitespace.
    """
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

    `base_col` is the column at which `chunk`'s first line started in the
    original source. Lines after the first have their leading whitespace
    trimmed to remove `base_col` columns, then `indent` is prepended.
    """
    lines = chunk.split(b"\n")
    if not lines:
        return chunk

    out_lines: list[bytes] = [indent + lines[0].lstrip()]
    for line in lines[1:]:
        if not line.strip():
            out_lines.append(b"")
            continue
        # Drop up to `base_col` columns of leading whitespace.
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
