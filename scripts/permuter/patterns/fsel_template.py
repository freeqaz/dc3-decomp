"""fsel template pattern — replace branched float conditionals with Min/Max/Clamp.

PowerPC fsel is a branchless conditional select. The Xbox 360 compiler generates
it for Min/Max/Clamp float template specializations but NOT for if-statements
or ternaries. Also tries the reverse direction.

Example:
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    ->
    val = Clamp(0.0f, 1.0f, val);

    // or reverse:
    val = Max(val, 0.0f);
    ->
    if (val < 0.0f) val = 0.0f;
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class FselTemplatePattern(Pattern):
    name = "fsel_template"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if "fsel" in d.target_opcode or "fsel" in d.base_opcode:
                return True
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        for d in diagnosis.diff_ops:
            if "fsel" in d.target_opcode or "fsel" in d.base_opcode:
                return 0.9  # fsel is the direct signal for this pattern
        if diagnosis.clusters:
            return 0.2
        return 0.1

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source
        stmts = ctx.statements

        # Try to find consecutive clamp patterns: if (v < lo) v = lo; if (v > hi) v = hi;
        for variant in _find_clamp_pairs(stmts, source, counter):
            yield variant
            counter += 1

        # Single if (v < x) v = x -> v = Max(v, x)
        for stmt in stmts:
            for variant in _single_to_template(stmt, source, counter):
                yield variant
                counter += 1


def _find_clamp_pairs(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Find consecutive if (v < lo) v = lo; if (v > hi) v = hi; -> Clamp()."""
    for i in range(len(stmts) - 1):
        info1 = _extract_float_guard(stmts[i], source)
        info2 = _extract_float_guard(stmts[i + 1], source)
        if info1 is None or info2 is None:
            continue

        var1, op1, bound1 = info1
        var2, op2, bound2 = info2

        # Same variable being clamped
        if var1 != var2:
            continue

        indent = get_indent(source, stmts[i])

        # Pattern: if (v < lo) v = lo; if (v > hi) v = hi; -> v = Clamp(lo, hi, v)
        if op1 in (b"<", b"<=") and op2 in (b">", b">="):
            new_source = (
                source[:stmts[i].start_byte]
                + indent + var1 + b" = Clamp(" + bound1 + b", " + bound2 + b", " + var1 + b");"
                + source[stmts[i + 1].end_byte:]
            )
            yield Variant(
                name=f"fsel_{counter}",
                pattern_name="fsel_template",
                description=f"Clamp({bound1.decode(errors='replace')}, {bound2.decode(errors='replace')}, {var1.decode(errors='replace')})",
                source=new_source,
            )
            counter += 1

        # Reversed: if (v > hi) v = hi; if (v < lo) v = lo; -> v = Clamp(lo, hi, v)
        elif op1 in (b">", b">=") and op2 in (b"<", b"<="):
            new_source = (
                source[:stmts[i].start_byte]
                + indent + var1 + b" = Clamp(" + bound2 + b", " + bound1 + b", " + var1 + b");"
                + source[stmts[i + 1].end_byte:]
            )
            yield Variant(
                name=f"fsel_{counter}",
                pattern_name="fsel_template",
                description=f"Clamp({bound2.decode(errors='replace')}, {bound1.decode(errors='replace')}, {var1.decode(errors='replace')})",
                source=new_source,
            )
            counter += 1


def _single_to_template(
    stmt: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Convert single if (v < x) v = x -> Max(v, x) or if (v > x) v = x -> Min(v, x)."""
    info = _extract_float_guard(stmt, source)
    if info is None:
        return

    var, op, bound = info
    indent = get_indent(source, stmt)

    if op in (b"<", b"<="):
        # if (v < bound) v = bound -> v = Max(v, bound)
        new_source = (
            source[:stmt.start_byte]
            + indent + var + b" = Max(" + var + b", " + bound + b");"
            + source[stmt.end_byte:]
        )
        yield Variant(
            name=f"fsel_{counter}",
            pattern_name="fsel_template",
            description=f"Max({var.decode(errors='replace')}, {bound.decode(errors='replace')})",
            source=new_source,
        )
        counter += 1

    elif op in (b">", b">="):
        # if (v > bound) v = bound -> v = Min(v, bound)
        new_source = (
            source[:stmt.start_byte]
            + indent + var + b" = Min(" + var + b", " + bound + b");"
            + source[stmt.end_byte:]
        )
        yield Variant(
            name=f"fsel_{counter}",
            pattern_name="fsel_template",
            description=f"Min({var.decode(errors='replace')}, {bound.decode(errors='replace')})",
            source=new_source,
        )
        counter += 1


def _extract_float_guard(stmt: Node, source: bytes) -> tuple[bytes, bytes, bytes] | None:
    """Extract (var, op, bound) from `if (var OP bound) var = bound;`."""
    if stmt.type != "if_statement":
        return None

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    alternative = stmt.child_by_field_name("alternative")

    if condition is None or consequence is None:
        return None
    if alternative is not None:
        return None

    inner = _get_inner_expr(condition)
    if inner is None or inner.type != "binary_expression":
        return None

    op = inner.child_by_field_name("operator")
    left = inner.child_by_field_name("left")
    right = inner.child_by_field_name("right")
    if op is None or left is None or right is None:
        return None

    op_text = op.text
    if op_text not in (b"<", b">", b"<=", b">="):
        return None

    # Get the assignment
    assign = _get_sole_assignment(consequence)
    if assign is None:
        return None

    assign_lhs = assign.child_by_field_name("left")
    assign_rhs = assign.child_by_field_name("right")
    if assign_lhs is None or assign_rhs is None:
        return None

    left_text = source[left.start_byte:left.end_byte]
    right_text = source[right.start_byte:right.end_byte]
    lhs_text = source[assign_lhs.start_byte:assign_lhs.end_byte]
    rhs_text = source[assign_rhs.start_byte:assign_rhs.end_byte]

    # if (var < bound) var = bound
    if lhs_text == left_text and rhs_text == right_text:
        return left_text, op_text, right_text

    return None


def _get_inner_expr(condition: Node) -> Node | None:
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _get_sole_assignment(compound_stmt: Node) -> Node | None:
    if compound_stmt.type == "compound_statement":
        stmts = [c for c in compound_stmt.named_children if c.type != "comment"]
        if len(stmts) != 1:
            return None
        stmt = stmts[0]
    elif compound_stmt.type == "expression_statement":
        stmt = compound_stmt
    else:
        return None

    if stmt.type == "expression_statement":
        for child in stmt.named_children:
            if child.type == "assignment_expression":
                return child
    return None
