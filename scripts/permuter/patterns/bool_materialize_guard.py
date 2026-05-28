"""Wrap bool subexpressions in ``!!(...)`` to force 0/1 materialization.

MWCC / MSVC emit different code for a bool operand inside an arithmetic
expression depending on whether the bool is explicitly *materialized* as a
0/1 integer first.  Without the explicit coercion the compiler picks a
branch-on-cr lowering for the bool->int conversion; with ``!!(...)`` it
emits the canonical 4-instruction ``li 0; beq; li 1; clrlwi.`` boolean
materialize sequence and then participates in the surrounding add/sub.

Example (the originating site, ``HamNavList::UpdateGestures`` cluster 4
at idx 266-269):

    // base: cmpwi cr6, r11, 0 ; bne ... ; (no materialize)
    selected + (gathering - firstShowing)

    // target: li 0 ; beq ; li 1 ; clrlwi.
    selected + (!!gathering - firstShowing)

This pattern walks ``+ - * == !=`` binary expressions and, when one
operand is a *bool-yielding* subexpression (member field that looks like a
bool flag, an existing ``!`` expression, a comparison, or an ``Is*`` /
``Has*`` predicate call), wraps that operand in ``!!(...)``.  It also
emits the inverse: stripping an existing ``!!`` wrap.

Relation to existing patterns:
    - ``bool_cast`` adds ``bool(...)`` casts around return / assignment /
      condition expressions to coerce the BOOL_MASK ``clrlwi r,r,24``
      shape.  It does *not* fire on arithmetic operands.
    - ``bool_materialize`` rewrites ``a && (x > 1)`` into
      ``a && (bool)(x > 1)`` (or ``a & (x > 1)``) — it targets *short-
      circuit* boolean control flow, never arithmetic with bool members.
    - This pattern fills the gap for ``int_op + bool_subexpr`` shapes:
      the bool isn't a return, isn't an assignment RHS, and isn't behind a
      ``&&``, so neither existing pattern triggers.

Priority is held at 0.5 — high enough to surface when the asm signal is
clearly bool-materialization (``mfcr``, ``crand``, ``cror``, ``extrwi``,
or a ``beq``/``bne`` cluster overlapping an arithmetic ``add``/``subf``/
``addi``/``subi``), low enough to defer to the strongly proven
``signed_unsigned`` / ``bool_materialize`` / ``signed_unsigned_cast_polarity``
family when their signals also fire.
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Arithmetic / equality operators whose operands we consider wrapping.
# Only operators where a 0/1 materialize on a bool operand actually changes
# the lowering.  Pointer / float arithmetic is excluded (handled below).
_ARITH_OPS: frozenset[bytes] = frozenset({b"+", b"-", b"*", b"==", b"!="})

# Opcodes that strongly suggest a bool was materialized via the cr -> gpr path.
_BOOL_MATERIALIZE_OPCODES: frozenset[str] = frozenset({
    "mfcr",
    "crand", "crandc", "cror", "crorc", "crxor", "crnand", "crnor", "creqv",
    "extrwi",
})

# Branch opcodes that — when sitting next to an arithmetic op in a cluster —
# may indicate the bool-materialize codegen difference this pattern targets.
_BOOL_BRANCH_OPCODES: frozenset[str] = frozenset({"beq", "bne"})

# Arithmetic opcodes (PPC) that we use as proximity evidence for the
# branch-near-arithmetic relevance gate.
_ARITH_OPCODES: frozenset[str] = frozenset({
    "add", "addi", "addic", "addis",
    "subf", "subfc", "subfic", "subi", "subic",
    "mullw", "mulli",
})

# Identifiers we explicitly do NOT treat as bool-yielding even if they pass
# the member-naming heuristic.  Member names like ``mElapsed``, ``mTime``,
# ``mCount`` are integers in this codebase, and wrapping them would force a
# *wrong* materialize on every multiply / add.
_DEFINITELY_NOT_BOOL: frozenset[str] = frozenset({
    "mCount", "mSize", "mIndex", "mIdx", "mElapsed", "mTime",
    "mDuration", "mLength", "mWidth", "mHeight", "mDepth",
    "mNumElems", "mNumFrames", "mFrame", "mPos", "mX", "mY", "mZ",
})

# Identifier-name regex for the member-flag heuristic: ``m`` + uppercase + ...
# Examples that match: ``mGathering``, ``mIsActive``, ``mHasFocus``.
_MEMBER_FLAG_RE = re.compile(r"^m[A-Z]\w*$")

# Bool-predicate call-name heuristic: ``Is...`` / ``Has...`` / ``Can...``.
# These are the canonical naming conventions for bool returns in this codebase.
_BOOL_PREDICATE_RE = re.compile(r"^(Is|Has|Can)[A-Z]\w*$")

# Comparison ops whose result is unambiguously bool.
_COMPARISON_OPS: frozenset[bytes] = frozenset({
    b"<", b"<=", b">", b">=", b"==", b"!=",
})


class BoolMaterializeGuardPattern(Pattern):
    """Wrap a bool operand of arithmetic in ``!!(...)`` (and the reverse)."""

    name = "bool_materialize_guard"
    safety_tier = "normal"
    structural_domain = "expr_shape"

    # -----------------------------------------------------------------------
    # Relevance gate — keep tight so we don't fire on every diff.
    # -----------------------------------------------------------------------

    def relevant(self, diagnosis: Diagnosis) -> bool:
        """Fire only when the asm clearly shows bool-materialization codegen.

        Two acceptance paths:
          1. Any diff_op opcode lives in :data:`_BOOL_MATERIALIZE_OPCODES`
             (mfcr / crand / cror / extrwi).  Strong direct signal.
          2. A cluster contains both a ``beq``/``bne`` AND an arithmetic
             opcode (add / subf / addi / ...).  Proximity signal that the
             materialize shape may be hiding inside an arithmetic expression.
        """
        # Path 1: direct opcode signal
        for d in diagnosis.diff_ops:
            t = _strip_dot(d.target_opcode)
            b = _strip_dot(d.base_opcode)
            if t in _BOOL_MATERIALIZE_OPCODES or b in _BOOL_MATERIALIZE_OPCODES:
                return True

        # Path 2: branch-near-arithmetic cluster signal
        for cluster in diagnosis.clusters:
            ops = set(cluster.target_opcodes) | set(cluster.base_opcodes)
            ops = {_strip_dot(o) for o in ops}
            if (ops & _BOOL_BRANCH_OPCODES) and (ops & _ARITH_OPCODES):
                return True

        return False

    # -----------------------------------------------------------------------
    # Priority — fixed at 0.5 (high-leverage but speculative).
    # -----------------------------------------------------------------------

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        return 0.5

    # -----------------------------------------------------------------------
    # Generation
    # -----------------------------------------------------------------------

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        max_variants = 6
        counter = 0
        emitted: set[tuple[int, int, bytes]] = set()

        for stmt in ctx.statements:
            if counter >= max_variants:
                return
            if not ctx.node_in_mismatch_region(stmt):
                continue
            for variant in _generate_for_statement(stmt, ctx, counter, emitted):
                yield variant
                counter += 1
                if counter >= max_variants:
                    return


# ---------------------------------------------------------------------------
# Variant emission
# ---------------------------------------------------------------------------


def _generate_for_statement(
    stmt: Node,
    ctx: FunctionContext,
    start_counter: int,
    emitted: set[tuple[int, int, bytes]],
) -> Iterator[Variant]:
    source = ctx.file_source
    counter = start_counter

    for node in walk(stmt):
        if node.type != "binary_expression":
            continue

        op_node = node.child_by_field_name("operator")
        if op_node is None or op_node.text not in _ARITH_OPS:
            continue

        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if left is None or right is None:
            continue

        op_text = op_node.text.decode("utf-8", errors="replace")

        # Skip if EITHER operand looks pointer-typed.  ``+`` on a pointer
        # is array indexing, not bool-into-int.
        if _is_pointer_like(left, source) or _is_pointer_like(right, source):
            continue

        # Skip if EITHER operand looks float-typed (string suffix ``f``,
        # ``.``, etc).  Float arithmetic is unrelated to bool materialize.
        if _looks_float(left, source) or _looks_float(right, source):
            continue

        for operand, side_label in ((left, "left"), (right, "right")):
            key = (operand.start_byte, operand.end_byte, op_node.text)
            if key in emitted:
                continue

            for variant in _emit_wrap_or_unwrap(
                ctx, node, operand, side_label, op_text, counter,
            ):
                emitted.add(key)
                yield variant
                counter += 1


def _emit_wrap_or_unwrap(
    ctx: FunctionContext,
    expr: Node,
    operand: Node,
    side: str,
    op_text: str,
    counter: int,
) -> Iterator[Variant]:
    """Emit either the wrap-with-!! variant or the strip-!! variant."""
    source = ctx.file_source

    # Case A: operand is already ``!!(...)`` — emit the strip-out variant.
    inner = _peel_double_bang(operand)
    if inner is not None:
        inner_text = source[inner.start_byte:inner.end_byte]
        # Drop the outer parens if peel returned a parenthesized expression
        ed = SourceEditor(source)
        ed.replace_node(operand, inner_text)
        try:
            new_source = ed.apply()
        except ValueError:
            return
        preview = _preview(source, operand)
        yield Variant(
            name=f"boolmatguard_{counter}",
            pattern_name="bool_materialize_guard",
            description=f"Strip !! from {side} of '{op_text}': {preview}",
            source=new_source,
        )
        return

    # Case B: operand looks bool-yielding — emit the wrap-with-!! variant.
    if not _is_bool_yielding(operand, source):
        return

    operand_text = source[operand.start_byte:operand.end_byte]
    # Skip if operand is already a bool literal — wrapping ``true`` /
    # ``false`` / ``0`` / ``1`` is pointless.
    if operand_text in (b"true", b"false", b"0", b"1"):
        return

    ed = SourceEditor(source)
    ed.replace_node(operand, b"!!(" + operand_text + b")")
    try:
        new_source = ed.apply()
    except ValueError:
        return

    preview = _preview(source, operand)
    yield Variant(
        name=f"boolmatguard_{counter}",
        pattern_name="bool_materialize_guard",
        description=f"Wrap {side} of '{op_text}' in !!: {preview}",
        source=new_source,
    )


# ---------------------------------------------------------------------------
# Bool-yield detection
# ---------------------------------------------------------------------------


def _is_bool_yielding(node: Node, source: bytes) -> bool:
    """True when ``node`` plausibly evaluates to a bool.

    No libclang — purely syntactic heuristics:
      - ``!expr``                              (unary !)
      - comparison binary expression           (a < b, a == b, ...)
      - identifier matching ``m[A-Z]\\w*``      (member-flag naming)
      - call to ``Is*`` / ``Has*`` / ``Can*``  (predicate naming convention)
      - parenthesized version of any of the above
    """
    # Peel a single layer of parens.
    if node.type == "parenthesized_expression":
        for child in node.named_children:
            if child.type != "comment":
                return _is_bool_yielding(child, source)
        return False

    # ``!expr`` — already bool.
    if node.type == "unary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"!":
            return True
        return False

    # Comparison binary_expression.
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text in _COMPARISON_OPS:
            return True
        return False

    # Identifier with member-flag naming, NOT in the known-not-bool list.
    if node.type == "identifier":
        text = source[node.start_byte:node.end_byte].decode("utf-8", errors="replace")
        if text in _DEFINITELY_NOT_BOOL:
            return False
        return bool(_MEMBER_FLAG_RE.match(text))

    # ``this->mFoo`` / ``obj.mFoo`` — only when the field name passes the
    # member-flag heuristic.  Be careful: don't fire on chained accesses
    # whose head is unknown.
    if node.type == "field_expression":
        field = node.child_by_field_name("field")
        if field is None:
            return False
        ftext = source[field.start_byte:field.end_byte].decode(
            "utf-8", errors="replace"
        )
        if ftext in _DEFINITELY_NOT_BOOL:
            return False
        return bool(_MEMBER_FLAG_RE.match(ftext))

    # Predicate call ``IsFoo()`` / ``HasBar()`` / ``CanBaz()``.
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func is None:
            return False
        # Extract the trailing identifier (handles obj.IsFoo() and Class::IsFoo()).
        name = _trailing_identifier(func, source)
        if name is None:
            return False
        return bool(_BOOL_PREDICATE_RE.match(name))

    return False


def _trailing_identifier(node: Node, source: bytes) -> str | None:
    """Get the rightmost identifier in a function-reference expression."""
    if node.type == "identifier":
        return source[node.start_byte:node.end_byte].decode(
            "utf-8", errors="replace"
        )
    if node.type == "field_expression":
        field = node.child_by_field_name("field")
        if field is not None:
            return source[field.start_byte:field.end_byte].decode(
                "utf-8", errors="replace"
            )
    if node.type == "qualified_identifier":
        # Last named child is typically the trailing name.
        named = [c for c in node.named_children if c.type != "comment"]
        if named:
            return _trailing_identifier(named[-1], source)
    return None


def _peel_double_bang(node: Node) -> Node | None:
    """If ``node`` is ``!!(inner)`` or ``!! inner``, return ``inner``; else None.

    Tree-sitter typically parses ``!!x`` as ``unary(!, unary(!, x))``.
    """
    if node.type != "unary_expression":
        return None
    op = node.child_by_field_name("operator")
    if op is None or op.text != b"!":
        return None
    arg = node.child_by_field_name("argument")
    if arg is None or arg.type != "unary_expression":
        return None
    inner_op = arg.child_by_field_name("operator")
    if inner_op is None or inner_op.text != b"!":
        return None
    return arg.child_by_field_name("argument")


# ---------------------------------------------------------------------------
# Operand-shape guards
# ---------------------------------------------------------------------------


def _is_pointer_like(node: Node, source: bytes) -> bool:
    """Reject obviously-pointer operands (pointer arithmetic, &x, NULL)."""
    if node.type == "unary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text in (b"&", b"*"):
            return True
    if node.type == "pointer_expression":
        return True
    if node.type == "subscript_expression":
        # arr[i] is typically a value, not a pointer — but the arithmetic
        # then operates on a value, which is fine to wrap.  Don't filter.
        return False
    text = source[node.start_byte:node.end_byte]
    if text in (b"nullptr", b"NULL"):
        return True
    return False


def _looks_float(node: Node, source: bytes) -> bool:
    """Reject obvious float-literal operands.

    We don't try to be exhaustive — this is a guard against the most common
    false positives (``x + 1.0f``, ``y * 0.5``).
    """
    if node.type == "number_literal":
        text = source[node.start_byte:node.end_byte]
        if b"." in text or text.endswith(b"f") or text.endswith(b"F"):
            return True
    return False


# ---------------------------------------------------------------------------
# Misc helpers
# ---------------------------------------------------------------------------


def _strip_dot(opcode: str) -> str:
    """Strip the PPC record-bit ``.`` suffix for opcode matching."""
    return opcode.rstrip(".")


def _preview(source: bytes, node: Node) -> str:
    text = source[node.start_byte:node.end_byte].decode("utf-8", errors="replace")
    if len(text) > 40:
        text = text[:37] + "..."
    return text
