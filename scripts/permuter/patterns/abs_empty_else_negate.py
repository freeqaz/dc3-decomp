"""abs_empty_else_negate — replace the empty-then `if(x>0){}else{x=-x}` float-abs
idiom with `Abs(x)` (from `math/Utl.h`).

Background (see `feedback_abs_vs_empty_else_negate.md`):
    When source computes a float absolute value as

        if (x > 0.0f) {} else { x = -x; }

    MWCC materializes the comparison through `mfcr` / `cror` / `extrwi.` and
    a real branch, producing visible insert/delete noise next to an `fcmpo`
    or `fneg` opcode mismatch. The target's compiler emits the cleaner
    `fcmpo` / `ble` / `b` / `fneg` shape that `Abs<float>(x)` (a one-liner
    template in `math/Utl.h`) compiles into.

Variants emitted:
    if (x > 0.0f) {} else { x = -x; }     ->   x = Abs(x);
    if (x >= 0.0f) {} else { x = -x; }    ->   x = Abs(x);
    if (x < 0.0f) { x = -x; }             ->   x = Abs(x);
    if (x <= 0.0f) { x = -x; }            ->   x = Abs(x);

Asm signal (relevant()):
    diff_op pairs around fcmpo / fneg / extrwi. / cror / mfcr.
"""

from __future__ import annotations

from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_by_type, get_indent
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Asm opcodes that suggest a real-branch boolean materialization sits next to
# an fneg/fcmpo. Any one of these on either side of a diff_op is enough.
_FLOAT_ABS_SIGNALS = {
    "fcmpo", "fcmpu", "fneg", "fnegs",
    "extrwi.", "cror", "crnor", "mfcr",
}

# Conditions where the negate lives in the ELSE clause (then is empty).
_ELSE_NEGATE_OPS = {">", ">="}

# Conditions where the negate lives in the THEN clause (no else needed).
_THEN_NEGATE_OPS = {"<", "<="}


class AbsEmptyElseNegatePattern(Pattern):
    """Rewrite `if(x>0){}else{x=-x}` to `x = Abs(x);`."""

    name = "abs_empty_else_negate"
    safety_tier = "conservative"
    structural_domain = "expr_shape"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Cheap and safe — also run on broad noise so it gets a chance.
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FLOAT_ABS_SIGNALS:
                return True
            if d.base_opcode in _FLOAT_ABS_SIGNALS:
                return True
        # Run anyway if there are any clusters — the AST trigger is so
        # narrow that we won't generate spurious variants.
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong signal: fneg/fcmpo mismatch in diff_ops.
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("fneg", "fnegs", "fcmpo"):
                return 0.6
            if d.base_opcode in ("fneg", "fnegs", "fcmpo"):
                return 0.6
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for if_node in find_by_type(ctx.body_node, "if_statement"):
            target = _match_abs_idiom(if_node, ctx.file_source)
            if target is None:
                continue
            var_text, replacement_text = target
            yield _emit_variant(ctx, if_node, replacement_text, var_text, counter)
            counter += 1


# ---------------------------------------------------------------------------
# Idiom recognition
# ---------------------------------------------------------------------------

def _match_abs_idiom(
    if_node: Node, source: bytes,
) -> Optional[tuple[bytes, bytes]]:
    """Return ``(var_text, replacement_text)`` when ``if_node`` is an abs
    idiom, else ``None``.

    ``replacement_text`` is the full text that should replace the if-statement
    (preserving the caller's indentation in the surrounding splice).
    """
    condition = if_node.child_by_field_name("condition")
    consequence = if_node.child_by_field_name("consequence")
    alternative = if_node.child_by_field_name("alternative")
    if condition is None or consequence is None:
        return None

    cond_expr = _condition_inner(condition)
    if cond_expr is None or cond_expr.type != "binary_expression":
        return None

    op_node = cond_expr.child_by_field_name("operator")
    left = cond_expr.child_by_field_name("left")
    right = cond_expr.child_by_field_name("right")
    if op_node is None or left is None or right is None:
        return None

    op = op_node.text.decode("utf-8", errors="replace") if op_node.text else ""
    # Only float-abs candidates: comparison against a zero literal, lhs is
    # the variable being negated.
    if not _is_zero_literal(right, source):
        return None
    if left.type not in ("identifier", "field_expression"):
        return None

    var_text = source[left.start_byte:left.end_byte]

    # Case A: `if (var >= 0) {} else { var = -var; }`
    if op in _ELSE_NEGATE_OPS:
        if not _is_empty_block(consequence):
            return None
        if alternative is None:
            return None
        alt_block = _else_block(alternative)
        if alt_block is None:
            return None
        if not _is_self_negate_block(alt_block, var_text, source):
            return None
    # Case B: `if (var < 0) { var = -var; }` (no else, or empty else)
    elif op in _THEN_NEGATE_OPS:
        if not _is_self_negate_block(consequence, var_text, source):
            return None
        if alternative is not None:
            alt_block = _else_block(alternative)
            if alt_block is not None and not _is_empty_block(alt_block):
                return None
    else:
        return None

    replacement = var_text + b" = Abs(" + var_text + b");"
    return (var_text, replacement)


def _condition_inner(condition: Node) -> Optional[Node]:
    """Strip the ``(...)`` of a ``condition_clause`` and return the expr."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _else_block(alternative: Node) -> Optional[Node]:
    """Extract the compound_statement of an `else_clause` (or a bare
    `if_statement` for `else if`, returning None in that case)."""
    if alternative.type == "compound_statement":
        return alternative
    if alternative.type == "else_clause":
        for child in alternative.children:
            if child.type == "compound_statement":
                return child
        return None
    return None


def _is_empty_block(block: Node) -> bool:
    """True when a compound_statement has no real children (allow comments)."""
    if block.type != "compound_statement":
        return False
    for child in block.named_children:
        if child.type == "comment":
            continue
        return False
    return True


def _is_self_negate_block(block: Node, var_text: bytes, source: bytes) -> bool:
    """True when ``block`` is exactly ``{ var = -var; }`` for the same var."""
    if block.type != "compound_statement":
        return False
    real_children = [c for c in block.named_children if c.type != "comment"]
    if len(real_children) != 1:
        return False
    stmt = real_children[0]
    if stmt.type != "expression_statement":
        return False
    real_subchildren = [c for c in stmt.named_children if c.type != "comment"]
    if not real_subchildren:
        return False
    assign = real_subchildren[0]
    if assign.type != "assignment_expression":
        return False
    op_node = assign.child_by_field_name("operator")
    if op_node is None or op_node.text != b"=":
        return False
    lhs = assign.child_by_field_name("left")
    rhs = assign.child_by_field_name("right")
    if lhs is None or rhs is None:
        return False
    if source[lhs.start_byte:lhs.end_byte] != var_text:
        return False
    if rhs.type != "unary_expression":
        return False
    rop = rhs.child_by_field_name("operator")
    if rop is None or rop.text != b"-":
        return False
    arg = rhs.child_by_field_name("argument")
    if arg is None:
        return False
    return source[arg.start_byte:arg.end_byte] == var_text


def _is_zero_literal(node: Node, source: bytes) -> bool:
    """True when ``node`` is a float/int zero literal (0, 0.0, 0.0f, ...)."""
    if node.type != "number_literal":
        return False
    text = source[node.start_byte:node.end_byte].decode(
        "utf-8", errors="replace"
    ).strip()
    text = text.rstrip("fFlLuU")
    if text in ("0", "0.", ".0", "0.0", "0x0", "0X0"):
        return True
    try:
        return float(text) == 0.0
    except ValueError:
        return False


# ---------------------------------------------------------------------------
# Variant emission
# ---------------------------------------------------------------------------

def _emit_variant(
    ctx: FunctionContext,
    if_node: Node,
    replacement: bytes,
    var_text: bytes,
    counter: int,
) -> Variant:
    """Splice ``replacement`` over the if-statement's byte range."""
    # Replace from the if-statement's start_byte through its end_byte. The
    # existing indentation immediately before start_byte is preserved by the
    # surrounding source — we only swap the statement itself.
    source = ctx.file_source
    indent = get_indent(source, if_node)
    # If the replacement spans the original line, preserve the leading indent
    # (``get_indent`` returns the bytes between the line start and the node
    # start; splicing keeps it). No newline insertion needed.
    _ = indent  # currently informational; reserved for future multi-line shapes
    ed = SourceEditor(source)
    ed.replace_range(if_node.start_byte, if_node.end_byte, replacement)
    new_source = ed.apply()
    return Variant(
        name=f"abs_negate_{counter}",
        pattern_name="abs_empty_else_negate",
        description=f"Rewrite if/else float-abs of {var_text.decode('utf-8', errors='replace')} as Abs()",
        source=new_source,
    )
