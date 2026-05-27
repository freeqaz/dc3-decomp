"""compound_or_widening_drop — swap narrow-type ``m |= x`` <-> ``m = m | x``.

Background (see `feedback_compound_or_assign.md`):
    For ``unsigned short member`` (or other narrow-type lvalues) OR-assigned
    with an ``int``, MWCC emits an extra ``clrlwi`` mask in the compound
    form because the int operand is materialized first and then narrowed,
    whereas the expanded form reads the lvalue via ``lhz`` (already
    zero-extended) and OR-folds without narrowing.

        // COMPOUND (extra clrlwi):   m |= x;
        // EXPANDED (clean lhz|or|sth): m = m | x;

    The same widening cost applies to ``&=`` and ``^=`` on narrow-typed
    lvalues. We emit BOTH directions so the pattern works whichever
    polarity the target uses.

Trigger (heuristic):
    1. Compound assign with ``|=`` / ``&=`` / ``^=`` whose LHS is an
       identifier or field access AND looks narrow-typed (u8/u16); OR
    2. Plain ``=`` whose RHS is ``lhs <op> something`` for the same lhs,
       again narrow-typed.

Type detection:
    a) When ``clang_types`` is available, ask libclang for the LHS's
       canonical type and check that ``size <= 2`` and it's unsigned-int
       kind.
    b) Heuristic fallback: scan the surrounding function + the whole file
       for ``unsigned short`` / ``unsigned char`` / ``u8`` / ``u16`` /
       ``uint16_t`` / ``uint8_t`` decls naming the LHS's terminal field/
       identifier.

Asm signal (relevant()):
    ``clrlwi`` (any width) on either side of a diff_op, OR any cluster
    that has ``clrlwi`` in its inserts/deletes, OR a u16-shaped
    ``lhz``/``sth`` mismatch.
"""

from __future__ import annotations

import re
from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_by_type, get_indent
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Compound operators we handle.
_COMPOUND_OPS = {b"|=", b"&=", b"^="}
# Their non-compound equivalents.
_NONCOMPOUND_FOR = {b"|=": b"|", b"&=": b"&", b"^=": b"^"}
_COMPOUND_FOR = {b"|": b"|=", b"&": b"&=", b"^": b"^="}

# Narrow-type keywords we'll look for in declarations (heuristic).
_NARROW_TYPE_TOKENS = {
    "unsigned short", "unsigned char",
    "u8", "u16", "uchar", "ushort",
    "uint8_t", "uint16_t",
    "uchar8", "ushort16",
    "byte",
}

# Asm signals.
_WIDENING_OPCODES = {"clrlwi", "clrlslwi", "extlwi"}
_NARROW_LS_OPCODES = {"lhz", "sth", "lbz", "stb"}


class CompoundOrWideningDropPattern(Pattern):
    """Swap narrow-type ``m |= x`` <-> ``m = m | x`` (both polarities)."""

    name = "compound_or_widening_drop"
    safety_tier = "conservative"
    structural_domain = "expr_shape"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if not diagnosis.diff_ops and not diagnosis.clusters:
            return False
        for d in diagnosis.diff_ops:
            if d.target_opcode in _WIDENING_OPCODES or d.base_opcode in _WIDENING_OPCODES:
                return True
            if d.target_opcode in _NARROW_LS_OPCODES or d.base_opcode in _NARROW_LS_OPCODES:
                return True
        for c in diagnosis.clusters:
            ops = set(c.target_opcodes) | set(c.base_opcodes)
            if ops & _WIDENING_OPCODES:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong signal: clrlwi in a diff_op (the exact mask we're chasing).
        for d in diagnosis.diff_ops:
            if "clrlwi" in (d.target_opcode, d.base_opcode):
                return 0.5
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        narrow_names = _collect_narrow_names(ctx)
        counter = 0
        for stmt in find_by_type(ctx.body_node, "expression_statement"):
            assign = _assignment_inside(stmt)
            if assign is None:
                continue
            for variant in _try_swap_compound(
                ctx, stmt, assign, narrow_names, counter,
            ):
                yield variant
                counter += 1


# ---------------------------------------------------------------------------
# AST + matching helpers
# ---------------------------------------------------------------------------

def _assignment_inside(expr_stmt: Node) -> Optional[Node]:
    for child in expr_stmt.named_children:
        if child.type == "assignment_expression":
            return child
    return None


def _try_swap_compound(
    ctx: FunctionContext,
    stmt: Node,
    assign: Node,
    narrow_names: set[bytes],
    counter: int,
) -> Iterator[Variant]:
    source = ctx.file_source
    op_node = assign.child_by_field_name("operator")
    lhs = assign.child_by_field_name("left")
    rhs = assign.child_by_field_name("right")
    if op_node is None or lhs is None or rhs is None:
        return

    op_text = op_node.text if op_node.text else b""
    lhs_text = source[lhs.start_byte:lhs.end_byte]
    if not _is_narrow_lvalue(lhs, lhs_text, narrow_names):
        return

    # Forward: m |= x  ->  m = m | x
    if op_text in _COMPOUND_OPS:
        plain = _NONCOMPOUND_FOR[op_text]
        # Need to wrap RHS in parens iff its top-level operator is lower-
        # precedence than the binary op (e.g. comma). Conservatively wrap
        # when RHS is itself an assignment/comma.
        rhs_text = source[rhs.start_byte:rhs.end_byte]
        if rhs.type in ("assignment_expression", "comma_expression"):
            rhs_text = b"(" + rhs_text + b")"
        new_expr = (
            lhs_text + b" = " + lhs_text + b" " + plain + b" " + rhs_text + b";"
        )
        ed = SourceEditor(source)
        ed.replace_range(stmt.start_byte, stmt.end_byte, new_expr)
        try:
            new_source = ed.apply()
        except ValueError:
            return
        yield Variant(
            name=f"compound_or_expand_{counter}",
            pattern_name="compound_or_widening_drop",
            description=(
                f"Expand {op_text.decode()} on narrow-type "
                f"{lhs_text.decode('utf-8', errors='replace')} "
                f"to '= ... | ...' form"
            ),
            source=new_source,
        )
        return

    # Reverse: m = m | x  ->  m |= x
    if op_text == b"=":
        compound_op = _detect_collapsible(lhs_text, rhs, source)
        if compound_op is None:
            return
        compound, other_text = compound_op
        new_expr = lhs_text + b" " + compound + b" " + other_text + b";"
        ed = SourceEditor(source)
        ed.replace_range(stmt.start_byte, stmt.end_byte, new_expr)
        try:
            new_source = ed.apply()
        except ValueError:
            return
        yield Variant(
            name=f"compound_or_collapse_{counter}",
            pattern_name="compound_or_widening_drop",
            description=(
                f"Collapse '{lhs_text.decode('utf-8', errors='replace')} = "
                f"{lhs_text.decode('utf-8', errors='replace')} ... ...' to "
                f"compound {compound.decode()}"
            ),
            source=new_source,
        )


def _detect_collapsible(
    lhs_text: bytes, rhs: Node, source: bytes,
) -> Optional[tuple[bytes, bytes]]:
    """When ``rhs`` is ``lhs <op> other`` (any side), return
    ``(compound_op, other_text)``; else None.

    Wraps `other_text` in parens iff stripping the binary node would change
    the resulting parse precedence.
    """
    # Unwrap a single layer of parens.
    if rhs.type == "parenthesized_expression":
        for c in rhs.named_children:
            if c.type != "comment":
                rhs = c
                break
    if rhs.type != "binary_expression":
        return None
    op = rhs.child_by_field_name("operator")
    left = rhs.child_by_field_name("left")
    right = rhs.child_by_field_name("right")
    if op is None or left is None or right is None:
        return None
    if op.text not in (b"|", b"&", b"^"):
        return None

    left_text = source[left.start_byte:left.end_byte]
    right_text = source[right.start_byte:right.end_byte]
    if left_text == lhs_text:
        other_node, other_text = right, right_text
    elif right_text == lhs_text:
        other_node, other_text = left, left_text
    else:
        return None

    # Conservative paren wrap: if other_node is itself a lower-precedence
    # expression than the bit-op, wrap it. assignment/comma definitely.
    if other_node.type in ("assignment_expression", "comma_expression"):
        other_text = b"(" + other_text + b")"
    return (_COMPOUND_FOR[op.text], other_text)


def _is_narrow_lvalue(
    lhs: Node, lhs_text: bytes, narrow_names: set[bytes],
) -> bool:
    """True when the LHS resolves to a narrow (8- or 16-bit) integer type.

    Uses the heuristic name-set built from declarations in the surrounding
    file. (libclang resolution is intentionally NOT attempted here: it
    requires file path + source + a compile-args context that this
    pattern doesn't receive, and the call sites where the rule is useful
    almost always have a visible narrow-type declaration in the same TU.)
    """
    leaf = _terminal_identifier(lhs)
    if leaf is None:
        return False
    return leaf in narrow_names


def _terminal_identifier(lhs: Node) -> Optional[bytes]:
    """Return the terminal identifier text for ``identifier`` or
    ``field_expression`` LHS. Returns None for anything else."""
    if lhs.type == "identifier":
        return lhs.text
    if lhs.type == "field_expression":
        field = lhs.child_by_field_name("field")
        if field is not None and field.text:
            return field.text
    return None


# ---------------------------------------------------------------------------
# Narrow-name collection
# ---------------------------------------------------------------------------

_DECL_RE = re.compile(
    r"\b(?:"
    r"unsigned\s+short|unsigned\s+char"
    r"|u8|u16|uchar|ushort|byte"
    r"|uint8_t|uint16_t"
    r")\b"
    r"[^\n;{}]*?"            # type modifiers / commas
    r"\b([A-Za-z_]\w*)\b"    # captured variable / field name
    r"\s*(?:[\[;=,)\}])"
)


def _collect_narrow_names(ctx: FunctionContext) -> set[bytes]:
    """Scan the whole file for identifiers declared with a narrow uint type.

    Returns a set of identifier bytes. Cheap regex scan — covers
    ``unsigned short`` member declarations, ``u16 x`` locals, etc. Field
    declarations inside a class body and free decls both match.
    """
    text = ctx.file_source.decode("utf-8", errors="replace")
    names: set[bytes] = set()
    for m in _DECL_RE.finditer(text):
        names.add(m.group(1).encode("utf-8"))
    return names
