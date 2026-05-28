"""store_then_compound_add — split ``member = base + Call(args);`` into
``member = base; member += Call(args);`` (and the symmetric reverse).

Background (see `feedback_store_then_compound_add.md`):
    When source is ``member = base + Call(args);`` and the target stores
    ``base`` to ``member`` *before* the call, then reloads ``member`` and
    adds the call's return value, the single-expression form keeps ``base``
    live across the call in a register; the target spills it early and
    reloads. Splitting the source into two statements forces the
    early-store + reload-and-add ordering, eliminating the
    callee-saved register live across the ``bl``.

    Real-world: CharClipDriver::Evaluate, 95.8 -> 99.8% (both wrap branches).

Trigger:
    ``member`` (or ``obj->member``) ``= base_expr + call_expr(args);``
    OR ``call_expr(args) + base_expr`` (commute order — also emitted).

Asm signal (relevant()):
    A cluster around a ``bl`` instruction where target has a ``stw`` /
    ``stfs`` before the ``bl`` and our base has the store after. We can't
    cheaply pin this from `Diagnosis` alone, so we accept any diff_op
    involving stw/stfs/lwz/lfs near a `bl`-class cluster, plus a fallback
    of "any clusters present".
"""

from __future__ import annotations

from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_by_type, get_indent
from ..classifier import is_fpr_cascade_dominated
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Opcodes we care about for the asm signal.
_STORE_OPCODES = {"stw", "stfs", "stfd", "sth", "stb"}
_LOAD_OPCODES = {"lwz", "lfs", "lfd", "lhz", "lhza", "lbz"}
_ADD_OPCODES = {"add", "addi", "addis", "fadd", "fadds"}


class StoreThenCompoundAddPattern(Pattern):
    """Split ``m = base + Call();`` into ``m = base; m += Call();``."""

    name = "store_then_compound_add"
    safety_tier = "conservative"
    structural_domain = "expr_shape"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if not diagnosis.diff_ops and not diagnosis.clusters:
            return False
        # Dominant-cascade veto: this transform changes a single expression's
        # store/reload ordering around a `bl`. It is powerless against a wall of
        # floating-point register swaps (e.g. GetNoteSliceWeight: 13 multi-instr
        # FPR pairs — the store_compound_0 sweep returned exactly baseline).
        if is_fpr_cascade_dominated(diagnosis):
            return False
        # Look for store/load/add mismatches near where a `bl` might sit.
        for d in diagnosis.diff_ops:
            t, b = d.target_opcode, d.base_opcode
            if t in _STORE_OPCODES or b in _STORE_OPCODES:
                return True
            if t in _LOAD_OPCODES or b in _LOAD_OPCODES:
                return True
            if t in _ADD_OPCODES or b in _ADD_OPCODES:
                return True
            if t == "bl" or b == "bl":
                return True
        # Clusters with target-only stw (we drop the early store).
        for c in diagnosis.clusters:
            if any(op in _STORE_OPCODES for op in c.target_opcodes):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: target-only stw next to a bl in any cluster.
        for c in diagnosis.clusters:
            target_ops = set(c.target_opcodes)
            base_ops = set(c.base_opcodes)
            if (target_ops & _STORE_OPCODES) and "bl" in (target_ops | base_ops):
                return 0.5
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Walk only top-level expression_statement children — splitting a
        # nested assignment (e.g. inside an `if (...)` condition) doesn't
        # make sense because we'd produce two statements where one expr is
        # required.
        for stmt in _expression_statements(ctx.body_node):
            assign = _assignment_inside(stmt)
            if assign is None:
                continue
            target = _match_store_add(assign, ctx.file_source)
            if target is None:
                continue
            lhs_text, base_expr, call_expr = target
            for variant in _emit_variants(
                ctx, stmt, lhs_text, base_expr, call_expr, counter,
            ):
                yield variant
                counter += 1


# ---------------------------------------------------------------------------
# AST helpers
# ---------------------------------------------------------------------------

def _expression_statements(body: Node) -> Iterator[Node]:
    """Yield every expression_statement node anywhere under ``body``."""
    yield from find_by_type(body, "expression_statement")


def _assignment_inside(expr_stmt: Node) -> Optional[Node]:
    """Return the assignment_expression inside an expression_statement, or None."""
    for child in expr_stmt.named_children:
        if child.type == "assignment_expression":
            return child
    return None


def _match_store_add(
    assign: Node, source: bytes,
) -> Optional[tuple[bytes, bytes, bytes]]:
    """Return ``(lhs_text, base_expr_text, call_expr_text)`` for a matching
    ``lhs = base + Call(args)`` shape. Returns ``None`` if the AST doesn't
    match the trigger.
    """
    op = assign.child_by_field_name("operator")
    if op is None or op.text != b"=":
        return None
    lhs = assign.child_by_field_name("left")
    rhs = assign.child_by_field_name("right")
    if lhs is None or rhs is None:
        return None
    if lhs.type not in ("identifier", "field_expression"):
        return None

    # Unwrap a single layer of parens on the RHS.
    if rhs.type == "parenthesized_expression":
        for c in rhs.named_children:
            if c.type != "comment":
                rhs = c
                break

    if rhs.type != "binary_expression":
        return None
    add_op = rhs.child_by_field_name("operator")
    if add_op is None or add_op.text not in (b"+", b"-"):
        # ``-`` not requested by the feedback, but a clean split is the same
        # transform; restrict to ``+`` for now to keep the trigger narrow.
        if add_op is None or add_op.text != b"+":
            return None

    left_side = rhs.child_by_field_name("left")
    right_side = rhs.child_by_field_name("right")
    if left_side is None or right_side is None:
        return None

    # Identify a `call_expression` operand and treat the other as the "base".
    left_call = _contains_call(left_side)
    right_call = _contains_call(right_side)
    if left_call == right_call:
        # Both or neither contain a call — ambiguous; skip.
        return None

    if right_call:
        base_node, call_node = left_side, right_side
    else:
        base_node, call_node = right_side, left_side

    # base must be a "simple" expression — identifier, field access,
    # subscript, or a parenthesized one of those. We DO NOT split when both
    # operands have side effects (the second statement would have to repeat
    # them).
    if not _is_simple_lvalue_like(base_node):
        return None
    # ``base`` must not itself reference ``lhs`` (would produce
    # ``lhs = lhs; lhs += call;`` which is semantically fine but the asm
    # shape is no longer the documented win).
    lhs_text = source[lhs.start_byte:lhs.end_byte]
    if source[base_node.start_byte:base_node.end_byte] == lhs_text:
        return None

    base_text = source[base_node.start_byte:base_node.end_byte]
    call_text = source[call_node.start_byte:call_node.end_byte]
    return (lhs_text, base_text, call_text)


def _contains_call(node: Node) -> bool:
    """True when ``node`` is or contains a call_expression."""
    for n in _walk(node):
        if n.type == "call_expression":
            return True
    return False


def _walk(node: Node) -> Iterator[Node]:
    yield node
    for c in node.children:
        yield from _walk(c)


def _is_simple_lvalue_like(node: Node) -> bool:
    """Accept simple operands: identifiers, member access, subscripts."""
    if node.type in (
        "identifier", "field_expression", "subscript_expression",
        "number_literal", "this",
    ):
        return True
    if node.type == "parenthesized_expression":
        for c in node.named_children:
            if c.type != "comment":
                return _is_simple_lvalue_like(c)
    # Unary `-x` / `*p` / `&x` are also fine when their operand is simple.
    if node.type == "unary_expression":
        arg = node.child_by_field_name("argument")
        if arg is not None:
            return _is_simple_lvalue_like(arg)
    return False


# ---------------------------------------------------------------------------
# Variant emission
# ---------------------------------------------------------------------------

def _emit_variants(
    ctx: FunctionContext,
    stmt: Node,
    lhs_text: bytes,
    base_text: bytes,
    call_text: bytes,
    counter: int,
) -> Iterator[Variant]:
    """Yield the split-and-compound variant for this statement."""
    source = ctx.file_source
    indent = get_indent(source, stmt)

    # Build replacement: ``lhs = base; <newline+indent> lhs += call;``
    replacement = (
        lhs_text + b" = " + base_text + b";\n"
        + indent + lhs_text + b" += " + call_text + b";"
    )

    ed = SourceEditor(source)
    ed.replace_range(stmt.start_byte, stmt.end_byte, replacement)
    new_source = ed.apply()

    yield Variant(
        name=f"store_compound_{counter}",
        pattern_name="store_then_compound_add",
        description=(
            f"Split {lhs_text.decode('utf-8', errors='replace')} = "
            f"{base_text.decode('utf-8', errors='replace')} + call(...) "
            "into store-then-compound-add"
        ),
        source=new_source,
    )
