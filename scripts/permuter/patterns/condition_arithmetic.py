"""Condition arithmetic pattern — swap between comparison and arithmetic boolean forms.

MSVC PPC generates completely different codegen for `if (k - 1)` vs `if (k != 1)`:
  - `k - 1` -> subi + cntlzw + extrwi (arithmetic boolean)
  - `k != 1` -> cmpwi + beq/bne (comparison branch)

Covers conditions in if/while/for statements, return expressions, and boolean
subscript transforms.

Transformations (conditions & returns):
  if/while/return (x != 0) <-> if/while/return (x)
  if/while/return (x == 0) <-> if/while/return (!x)
  if/while/return (x != N) <-> if/while/return (x - N)       (N is literal)
  if/while/return (x == N) <-> if/while/return (!(x - N))    (N is literal)

Boolean subscript transforms:
  arr[x == 0] <-> arr[1 - x]
  arr[!x]     <-> arr[x == 0]
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Strong signals: these opcodes are distinctive to arithmetic-boolean codegen
_STRONG_OPCODES = {"cntlzw", "extrwi"}
# Weaker signals: appear in arithmetic-boolean but also other contexts
_WEAK_OPCODES = {"subi", "rlwinm"}


def _strip_dot(opcode: str) -> str:
    """Strip PPC record-bit suffix ('.' at end) for opcode matching.

    objdiff reports `extrwi.`, `rlwinm.`, `cntlzw.` etc. with the dot suffix
    when the instruction sets CR0. We need to match these against base names.
    """
    return opcode.rstrip(".")


class ConditionArithmeticPattern(Pattern):
    name = "condition_arithmetic"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        has_strong = False
        weak_count = 0
        for d in diagnosis.diff_ops:
            t = _strip_dot(d.target_opcode)
            b = _strip_dot(d.base_opcode)
            if t in _STRONG_OPCODES or b in _STRONG_OPCODES:
                has_strong = True
            if t in _WEAK_OPCODES or b in _WEAK_OPCODES:
                weak_count += 1
        # Strong signal alone is enough; weak signals need at least two co-occurring
        # (subi or rlwinm alone is too common; together they suggest cntlzw pattern)
        return has_strong or weak_count >= 2

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.0
        for d in diagnosis.diff_ops:
            t = _strip_dot(d.target_opcode)
            b = _strip_dot(d.base_opcode)
            if t in _STRONG_OPCODES or b in _STRONG_OPCODES:
                score += 0.3
            elif t in _WEAK_OPCODES or b in _WEAK_OPCODES:
                score += 0.1
        return min(max(score, 0.3), 0.6)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for node in walk(stmt):
                for v in _generate_condition_transforms(node, ctx, counter):
                    yield v
                    counter += 1
                for v in _generate_return_transforms(node, ctx, counter):
                    yield v
                    counter += 1
                for v in _generate_subscript_transforms(node, ctx, counter):
                    yield v
                    counter += 1


# ---------------------------------------------------------------------------
# Condition extraction (if / while / for)
# ---------------------------------------------------------------------------

def _get_condition_expr(node: Node) -> Node | None:
    """Get the boolean expression from an if/while/for statement's condition."""
    if node.type in ("if_statement", "while_statement"):
        # These wrap the condition in a condition_clause
        cond = node.child_by_field_name("condition")
        if cond is None:
            return None
        if cond.type == "condition_clause":
            for child in cond.named_children:
                if child.type != "comment":
                    return child
            return None
        # Bare expression (shouldn't happen but be safe)
        return cond
    elif node.type == "for_statement":
        # for_statement has condition as a direct field (no wrapper)
        cond = node.child_by_field_name("condition")
        return cond
    return None


def _is_zero_literal(node: Node) -> bool:
    return node.type == "number_literal" and node.text == b"0"


def _is_number_literal(node: Node) -> bool:
    return node.type == "number_literal"


def _parse_int(node: Node) -> int | None:
    if node.type != "number_literal" or node.text is None:
        return None
    try:
        return int(node.text.decode("utf-8"), 0)
    except ValueError:
        return None


# ---------------------------------------------------------------------------
# Core expression transforms (shared between conditions and returns)
# ---------------------------------------------------------------------------

def _transform_bool_expr(
    expr: Node, source: bytes, counter: int, context_label: str,
) -> Iterator[Variant]:
    """Generate arithmetic-boolean transform variants for a boolean expression.

    Works for any expression in boolean context: if/while/for conditions,
    return values, etc. The context_label is used in variant descriptions.
    """
    if expr.type == "binary_expression":
        op_node = expr.child_by_field_name("operator")
        left = expr.child_by_field_name("left")
        right = expr.child_by_field_name("right")
        if op_node is None or left is None or right is None:
            return
        op = op_node.text.decode("utf-8") if op_node.text else ""

        if op == "!=" and _is_zero_literal(right):
            # x != 0 -> x
            left_text = node_text(source, left)
            ed = SourceEditor(source)
            ed.replace_node(expr, left_text)
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description=f"{context_label}: != 0 -> implicit bool",
                source=ed.apply(),
            )

        elif op == "==" and _is_zero_literal(right):
            # x == 0 -> !x
            left_text = node_text(source, left)
            ed = SourceEditor(source)
            ed.replace_node(expr, b"!" + left_text)
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description=f"{context_label}: == 0 -> !expr",
                source=ed.apply(),
            )

        elif op == "!=" and _is_number_literal(right):
            # x != N -> x - N
            val = _parse_int(right)
            if val is not None and val != 0:
                left_text = node_text(source, left)
                right_text = node_text(source, right)
                ed = SourceEditor(source)
                ed.replace_node(expr, left_text + b" - " + right_text)
                yield Variant(
                    name=f"condarith_{counter}",
                    pattern_name="condition_arithmetic",
                    description=f"{context_label}: != {val} -> subtract",
                    source=ed.apply(),
                )

        elif op == "==" and _is_number_literal(right):
            # x == N -> !(x - N)
            val = _parse_int(right)
            if val is not None and val != 0:
                left_text = node_text(source, left)
                right_text = node_text(source, right)
                ed = SourceEditor(source)
                ed.replace_node(expr, b"!(" + left_text + b" - " + right_text + b")")
                yield Variant(
                    name=f"condarith_{counter}",
                    pattern_name="condition_arithmetic",
                    description=f"{context_label}: == {val} -> !subtract",
                    source=ed.apply(),
                )

        elif op == "-" and _is_number_literal(right):
            # x - N -> x != N
            val = _parse_int(right)
            if val is not None:
                left_text = node_text(source, left)
                right_text = node_text(source, right)
                ed = SourceEditor(source)
                ed.replace_node(expr, left_text + b" != " + right_text)
                yield Variant(
                    name=f"condarith_{counter}",
                    pattern_name="condition_arithmetic",
                    description=f"{context_label}: subtract -> != {val}",
                    source=ed.apply(),
                )

    # Bare identifier/expression -> x != 0
    elif expr.type in ("identifier", "field_expression", "subscript_expression", "call_expression"):
        expr_text = node_text(source, expr)
        ed = SourceEditor(source)
        ed.replace_node(expr, expr_text + b" != 0")
        yield Variant(
            name=f"condarith_{counter}",
            pattern_name="condition_arithmetic",
            description=f"{context_label}: implicit bool -> != 0",
            source=ed.apply(),
        )

    # !x -> x == 0, and !(x - N) -> x == N
    elif expr.type == "unary_expression":
        op_node = expr.child_by_field_name("operator")
        arg = expr.child_by_field_name("argument")
        if not (op_node and op_node.text == b"!" and arg):
            return
        if arg.type not in ("identifier", "field_expression", "subscript_expression",
                            "call_expression", "parenthesized_expression"):
            return

        # !(x - N) -> x == N
        if arg.type == "parenthesized_expression" and arg.named_child_count == 1:
            inner = arg.named_children[0]
            if inner.type == "binary_expression":
                inner_op = inner.child_by_field_name("operator")
                if inner_op and inner_op.text == b"-":
                    inner_left = inner.child_by_field_name("left")
                    inner_right = inner.child_by_field_name("right")
                    if inner_left and inner_right and _is_number_literal(inner_right):
                        left_text = node_text(source, inner_left)
                        right_text = node_text(source, inner_right)
                        ed = SourceEditor(source)
                        ed.replace_node(expr, left_text + b" == " + right_text)
                        yield Variant(
                            name=f"condarith_{counter}",
                            pattern_name="condition_arithmetic",
                            description=f"{context_label}: !(subtract) -> == N",
                            source=ed.apply(),
                        )
                        # Don't return — also yield the simpler !x -> x == 0
                        # variant below, since we can't know which form matches

        # !x -> x == 0
        arg_text = node_text(source, arg)
        ed = SourceEditor(source)
        ed.replace_node(expr, arg_text + b" == 0")
        yield Variant(
            name=f"condarith_{counter}",
            pattern_name="condition_arithmetic",
            description=f"{context_label}: !expr -> == 0",
            source=ed.apply(),
        )


# ---------------------------------------------------------------------------
# Condition transforms (if / while / for)
# ---------------------------------------------------------------------------

def _generate_condition_transforms(
    node: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate condition form transforms for if/while/for statements."""
    expr = _get_condition_expr(node)
    if expr is None:
        return
    yield from _transform_bool_expr(expr, ctx.file_source, counter, "Condition")


# ---------------------------------------------------------------------------
# Return expression transforms
# ---------------------------------------------------------------------------

def _get_return_expr(node: Node) -> Node | None:
    """Get the expression from a return statement."""
    if node.type != "return_statement":
        return None
    for child in node.named_children:
        if child.type != "comment":
            return child
    return None


def _generate_return_transforms(
    node: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate arithmetic-boolean transforms for return expressions.

    Covers `return state == 2` generating branchless subi+cntlzw+extrwi
    vs `return state - 2` or explicit if/else generating compare-and-branch.
    """
    expr = _get_return_expr(node)
    if expr is None:
        return
    yield from _transform_bool_expr(expr, ctx.file_source, counter, "Return")


# ---------------------------------------------------------------------------
# Boolean subscript transforms
# ---------------------------------------------------------------------------

def _generate_subscript_transforms(
    node: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate boolean subscript transforms: arr[x == 0] <-> arr[1 - x] <-> arr[!x]."""
    if node.type != "subscript_expression":
        return

    source = ctx.file_source

    # subscript_expression has: identifier, subscript_argument_list
    arg_list = None
    for child in node.named_children:
        if child.type == "subscript_argument_list":
            arg_list = child
            break
    if arg_list is None:
        return

    # Get the actual index expression from inside the argument list
    index = None
    for child in arg_list.named_children:
        index = child
        break
    if index is None:
        return

    if index.type == "binary_expression":
        op_node = index.child_by_field_name("operator")
        left = index.child_by_field_name("left")
        right = index.child_by_field_name("right")
        if not (op_node and left and right):
            return
        op = op_node.text.decode("utf-8") if op_node.text else ""

        if op == "==" and _is_zero_literal(right):
            # arr[x == 0] -> arr[1 - x]
            left_text = node_text(source, left)
            ed = SourceEditor(source)
            ed.replace_node(index, b"1 - " + left_text)
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description="Bool subscript: == 0 -> 1 - x",
                source=ed.apply(),
            )
            counter += 1
            # arr[x == 0] -> arr[!x]
            ed2 = SourceEditor(source)
            ed2.replace_node(index, b"!" + left_text)
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description="Bool subscript: == 0 -> !x",
                source=ed2.apply(),
            )

        elif op == "-" and left.type == "number_literal" and left.text == b"1":
            # arr[1 - x] -> arr[x == 0]
            right_text = node_text(source, right)
            ed = SourceEditor(source)
            ed.replace_node(index, right_text + b" == 0")
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description="Bool subscript: 1 - x -> == 0",
                source=ed.apply(),
            )
            counter += 1
            # arr[1 - x] -> arr[!x]
            ed2 = SourceEditor(source)
            ed2.replace_node(index, b"!" + right_text)
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description="Bool subscript: 1 - x -> !x",
                source=ed2.apply(),
            )

    elif index.type == "unary_expression":
        # arr[!x] -> arr[x == 0] and arr[!x] -> arr[1 - x]
        op_node = index.child_by_field_name("operator")
        arg = index.child_by_field_name("argument")
        if op_node and op_node.text == b"!" and arg:
            arg_text = node_text(source, arg)
            ed = SourceEditor(source)
            ed.replace_node(index, arg_text + b" == 0")
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description="Bool subscript: !x -> == 0",
                source=ed.apply(),
            )
            counter += 1
            ed2 = SourceEditor(source)
            ed2.replace_node(index, b"1 - " + arg_text)
            yield Variant(
                name=f"condarith_{counter}",
                pattern_name="condition_arithmetic",
                description="Bool subscript: !x -> 1 - x",
                source=ed2.apply(),
            )
