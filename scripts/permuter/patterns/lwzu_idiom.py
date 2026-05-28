"""lwzu_idiom — rewrite separate load-then-advance into the lwzu reference-cast.

Win source (`feedback_lwzu_idiom.md`):
    `Synth::returnMasterKey` 94.5% -> 99.9% by collapsing
    ``unsigned int word = *(unsigned int*)p; p += 4;`` into
    ``unsigned int word = *((unsigned int *&)p)++;``.

MWCC only emits `lwzu` / `lhzu` / `lbzu` (load-with-update) for that one
specific source shape — the obvious `*(uint*)p; p += sizeof(uint);` form
splits into separate `lwz` + `addi`.

Trigger (AST):
    Two consecutive statements where statement ``i`` reads through a casted
    pointer ``*(unsigned <int|short|char|long>*)p`` (either as an
    assignment RHS, an initializer, or a bare expression) and statement
    ``i+1`` is ``p += K;`` where ``K`` matches ``sizeof(unsigned <T>)``
    (4 / 2 / 1 / 4).  The pointer name on both sides must match.

Transform:
    Combine into one statement using the reference-cast post-increment idiom:
        ``<lhs> = *((unsigned <T> *&)p)++;``  (or bare expr-statement form)
    Delete the now-redundant ``p += K;``.

Asm signal (`relevant()`):
    Any diff_op where the target opcode is one of ``lwzu`` / ``lhzu`` /
    ``lbzu`` and base is not (or vice-versa). Cheap and unambiguous.

Safety:
    Very narrow pattern; at most a handful of variants per function.  Only
    fires when the cast type/width agrees with the ``+=`` constant, so we
    won't accidentally collapse a mismatched-size pair.
"""

from __future__ import annotations

from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_by_type, get_indent
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Map of (unsigned C type) -> bytes per element (matches sizeof on Wii/PPC).
_TYPE_SIZES: dict[bytes, int] = {
    b"int":  4,
    b"long": 4,
    b"short": 2,
    b"char": 1,
}


class LwzuIdiomPattern(Pattern):
    """Collapse `*(uint*)p; p+=4;` -> `*((uint*&)p)++` for lwzu emission."""

    name = "lwzu_idiom"
    safety_tier = "conservative"
    structural_domain = "expr_shape"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            t, b = d.target_opcode, d.base_opcode
            if t in ("lwzu", "lhzu", "lbzu") and b not in ("lwzu", "lhzu", "lbzu"):
                return True
            if b in ("lwzu", "lhzu", "lbzu") and t not in ("lwzu", "lhzu", "lbzu"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("lwzu", "lhzu", "lbzu"):
                return 0.8
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Find all expression_statement / declaration sequences within the
        # function body. We look across *every* compound_statement scope, not
        # just the top level, because the load+advance pair often lives
        # inside a loop body.
        for block in _find_blocks(ctx.body_node):
            kids = list(block.named_children)
            for i in range(len(kids) - 1):
                load_info = _match_pointer_load(kids[i], ctx.file_source)
                if load_info is None:
                    continue
                cast_type, ptr_name, lhs_bytes, decl_prefix = load_info

                bump = _match_pointer_bump(kids[i + 1], ctx.file_source, ptr_name)
                if bump is None:
                    continue
                bump_size = bump

                expected = _TYPE_SIZES.get(cast_type)
                if expected is None or expected != bump_size:
                    continue

                for variant in _emit_variant(
                    ctx, kids[i], kids[i + 1],
                    cast_type, ptr_name, lhs_bytes, decl_prefix, counter,
                ):
                    yield variant
                    counter += 1
                    if counter >= 6:
                        return


# ---------------------------------------------------------------------------
# AST helpers
# ---------------------------------------------------------------------------

def _find_blocks(root: Node) -> Iterator[Node]:
    """Yield every compound_statement inside (and including) ``root``."""
    yield from find_by_type(root, "compound_statement")


def _match_pointer_load(
    stmt: Node, source: bytes,
) -> Optional[tuple[bytes, bytes, Optional[bytes], Optional[bytes]]]:
    """Match ``<lhs>? = *(unsigned <T>*)p;`` (assignment, declaration, or bare expr).

    Returns ``(cast_type_bytes, ptr_name_bytes, lhs_bytes_or_None,
    decl_prefix_bytes_or_None)``.

    ``decl_prefix`` is the leading ``<type-spec> <name> = `` portion when the
    statement is a declaration; we keep it so we can rebuild the statement
    after the rewrite.
    """
    if stmt.type == "expression_statement":
        for child in stmt.named_children:
            if child.type == "assignment_expression":
                return _from_assignment(child, source)
            if child.type in ("pointer_expression", "unary_expression"):
                # Bare ``*(uint*)p;`` — uncommon but legal.
                got = _from_deref_cast(child, source)
                if got is not None:
                    cast_type, ptr_name = got
                    return (cast_type, ptr_name, None, None)
        return None

    if stmt.type == "declaration":
        return _from_declaration(stmt, source)

    return None


def _from_assignment(
    assign: Node, source: bytes,
) -> Optional[tuple[bytes, bytes, bytes, None]]:
    op = assign.child_by_field_name("operator")
    if op is None or op.text != b"=":
        return None
    left = assign.child_by_field_name("left")
    right = assign.child_by_field_name("right")
    if left is None or right is None:
        return None
    got = _from_deref_cast(right, source)
    if got is None:
        return None
    cast_type, ptr_name = got
    lhs_bytes = source[left.start_byte:left.end_byte]
    return (cast_type, ptr_name, lhs_bytes, None)


def _from_declaration(
    decl: Node, source: bytes,
) -> Optional[tuple[bytes, bytes, bytes, bytes]]:
    """Handle ``unsigned int word = *(unsigned int*)p;`` style declarations.

    Returns ``(cast_type, ptr_name, lhs_name, decl_prefix)`` where
    ``decl_prefix`` is the full leading ``unsigned int word = `` slice so the
    rewriter can keep the local declaration intact while replacing the RHS.
    """
    # Tree-sitter shape: declaration -> init_declarator(declarator, value=...)
    init_decl = None
    for child in decl.named_children:
        if child.type == "init_declarator":
            init_decl = child
            break
    if init_decl is None:
        return None
    value = init_decl.child_by_field_name("value")
    if value is None:
        return None
    got = _from_deref_cast(value, source)
    if got is None:
        return None
    cast_type, ptr_name = got
    # decl_prefix = everything from decl start up to (but not including)
    # the value node.
    decl_prefix = source[decl.start_byte:value.start_byte]
    lhs_name_node = init_decl.child_by_field_name("declarator")
    lhs_name = source[lhs_name_node.start_byte:lhs_name_node.end_byte] if lhs_name_node else b""
    return (cast_type, ptr_name, lhs_name, decl_prefix)


def _from_deref_cast(
    expr: Node, source: bytes,
) -> Optional[tuple[bytes, bytes]]:
    """Match ``*(unsigned <T>*)<ptr>`` -> ``(<T>, <ptr>)``.

    Tree-sitter renders this as either ``pointer_expression`` (preferred) or
    ``unary_expression`` with operator ``*`` depending on grammar version.
    """
    # Unwrap a leading paren if present.
    if expr.type == "parenthesized_expression":
        for c in expr.named_children:
            if c.type != "comment":
                expr = c
                break

    # The dereference can show up as either "pointer_expression" or a
    # unary_expression with op "*". Normalize.
    operand: Optional[Node] = None
    if expr.type == "pointer_expression":
        operand = expr.child_by_field_name("argument")
    elif expr.type == "unary_expression":
        op = expr.child_by_field_name("operator")
        if op is not None and op.type == "*":
            operand = expr.child_by_field_name("argument")
    if operand is None:
        return None

    # operand must be a cast_expression: (unsigned <T>*)<ptr>
    if operand.type == "parenthesized_expression":
        for c in operand.named_children:
            if c.type != "comment":
                operand = c
                break

    if operand.type != "cast_expression":
        return None

    cast_type_node = operand.child_by_field_name("type")
    value_node = operand.child_by_field_name("value")
    if cast_type_node is None or value_node is None:
        return None

    cast_text = source[cast_type_node.start_byte:cast_type_node.end_byte]
    # Must look like ``unsigned <T> *`` (possibly with whitespace).
    norm = b" ".join(cast_text.split())
    cast_type = _parse_unsigned_pointer_cast(norm)
    if cast_type is None:
        return None

    # ptr expression must be a simple identifier (we won't try to be clever
    # with arbitrary expressions — they'd break the reference-cast idiom).
    if value_node.type == "parenthesized_expression":
        for c in value_node.named_children:
            if c.type != "comment":
                value_node = c
                break
    if value_node.type != "identifier":
        return None
    ptr_name = source[value_node.start_byte:value_node.end_byte]
    return (cast_type, ptr_name)


def _parse_unsigned_pointer_cast(text: bytes) -> Optional[bytes]:
    """Return the inner unsigned type if ``text`` is ``unsigned <T> *``.

    Accepts ``unsigned int *``, ``unsigned int*``, ``unsigned short *`` etc.
    Rejects signed types, plain ``unsigned`` alone, or qualified casts like
    ``const unsigned int *``.
    """
    parts = text.split()
    # Strip a trailing '*' that may be attached to the last token.
    if not parts:
        return None
    if parts[-1] == b"*":
        body = parts[:-1]
    elif parts[-1].endswith(b"*"):
        body = parts[:-1] + [parts[-1][:-1].rstrip(b"*")]
        body = [p for p in body if p]
    else:
        return None

    if len(body) != 2 or body[0] != b"unsigned":
        return None
    if body[1] not in _TYPE_SIZES:
        return None
    return body[1]


def _match_pointer_bump(stmt: Node, source: bytes, ptr_name: bytes) -> Optional[int]:
    """Match ``p += <K>;`` for the named pointer; return K or None.

    Accepts both literal integers (``4``) and ``sizeof(...)`` expressions
    matching the cast type. For sizeof, we hand back the value when it lines
    up with one of the supported widths.
    """
    if stmt.type != "expression_statement":
        return None
    for child in stmt.named_children:
        if child.type != "assignment_expression":
            continue
        op = child.child_by_field_name("operator")
        if op is None or op.text != b"+=":
            return None
        left = child.child_by_field_name("left")
        right = child.child_by_field_name("right")
        if left is None or right is None:
            return None
        if left.type != "identifier":
            return None
        if source[left.start_byte:left.end_byte] != ptr_name:
            return None

        # Plain integer literal: 1 / 2 / 4
        if right.type == "number_literal":
            txt = source[right.start_byte:right.end_byte].strip()
            try:
                value = int(txt, 0)
            except ValueError:
                return None
            if value in (1, 2, 4):
                return value
            return None

        # sizeof(unsigned <T>) — pull the bracketed type and look it up.
        if right.type == "sizeof_expression":
            ty = right.child_by_field_name("type")
            if ty is None:
                return None
            txt = b" ".join(source[ty.start_byte:ty.end_byte].split())
            inner = _parse_unsigned_pointer_cast(txt + b"*")
            if inner is None:
                # try without forcing a star (raw type)
                parts = txt.split()
                if len(parts) == 2 and parts[0] == b"unsigned" and parts[1] in _TYPE_SIZES:
                    return _TYPE_SIZES[parts[1]]
                return None
            return _TYPE_SIZES[inner]
        return None
    return None


# ---------------------------------------------------------------------------
# Variant emission
# ---------------------------------------------------------------------------

def _emit_variant(
    ctx: FunctionContext,
    load_stmt: Node,
    bump_stmt: Node,
    cast_type: bytes,
    ptr_name: bytes,
    lhs_bytes: Optional[bytes],
    decl_prefix: Optional[bytes],
    counter: int,
) -> Iterator[Variant]:
    """Emit the combined ``*((<T> *&)p)++`` variant."""
    source = ctx.file_source
    # The new RHS that triggers MWCC's lwzu / lhzu / lbzu recognizer.
    new_rhs = b"*((unsigned " + cast_type + b" *&)" + ptr_name + b")++"

    if decl_prefix is not None:
        # Declaration form: keep the type+name prefix, swap the RHS, drop ;.
        # decl_prefix ends right before the original value expression, so we
        # append our new RHS and a closing semicolon.
        new_load = decl_prefix + new_rhs + b";"
    elif lhs_bytes is not None:
        new_load = lhs_bytes + b" = " + new_rhs + b";"
    else:
        # Bare expression statement (rare): just the new expr + ;
        new_load = new_rhs + b";"

    ed = SourceEditor(source)
    ed.replace_range(load_stmt.start_byte, load_stmt.end_byte, new_load)
    # Delete the bump statement and any trailing newline+indent left over.
    bump_end = bump_stmt.end_byte
    while bump_end < len(source) and source[bump_end:bump_end + 1] in (b" ", b"\t"):
        bump_end += 1
    if bump_end < len(source) and source[bump_end:bump_end + 1] == b"\n":
        bump_end += 1
    # Also pull the leading whitespace before the bump so we don't leave a
    # bare indent line behind.
    bump_start = bump_stmt.start_byte
    line_start = bump_start
    while line_start > 0 and source[line_start - 1:line_start] in (b" ", b"\t"):
        line_start -= 1
    ed.delete_range(line_start, bump_end)

    try:
        new_source = ed.apply()
    except ValueError:
        return

    yield Variant(
        name=f"lwzu_{counter}",
        pattern_name="lwzu_idiom",
        description=(
            f"Collapse *(unsigned {cast_type.decode()}*)"
            f"{ptr_name.decode()}; {ptr_name.decode()} += "
            f"{_TYPE_SIZES[cast_type]}; into post-increment reference cast"
        ),
        source=new_source,
        func_byte_range=ctx.func_byte_range,
        original_source=ctx.file_source,
        tags=frozenset({"lwzu_idiom"}),
    )
