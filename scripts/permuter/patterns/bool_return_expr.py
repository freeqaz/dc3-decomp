"""Bool return expression pattern — convert if/return to return expression.

Converts patterns like:
    if (cond) return false;
    return true;
->
    return !cond;

And:
    if (a || b) return false;
    return true;
->
    return !a && !b;  (or: return !(a || b);)

Also does the reverse: return expression to if/return.
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

_INVERSIONS = {
    b"<": b">=", b">": b"<=", b"<=": b">", b">=": b"<",
    b"==": b"!=", b"!=": b"==",
}


class BoolReturnExprPattern(Pattern):
    name = "bool_return_expr"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters) or bool(diagnosis.reg_swap_pairs)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source
        stmts = ctx.statements

        # Direction 1: if (cond) return false; return true; -> return !cond;
        for variant in _merge_bool_return(stmts, source, counter):
            yield variant
            counter += 1

        # Direction 2: return expr; -> if (!expr) return false; return true;
        for variant in _split_bool_return(stmts, source, counter):
            yield variant
            counter += 1


def _merge_bool_return(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Find if (cond) return false/true; return true/false; and merge."""
    for i in range(len(stmts) - 1):
        # Current statement: if (cond) return VALUE;
        if stmts[i].type != "if_statement":
            continue

        condition = stmts[i].child_by_field_name("condition")
        consequence = stmts[i].child_by_field_name("consequence")
        alternative = stmts[i].child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue
        if alternative is not None:
            continue

        ret1 = _get_return_from(consequence, source)
        if ret1 is None:
            continue

        # Next statement: return OPPOSITE_VALUE;
        if stmts[i + 1].type != "return_statement":
            continue
        ret2 = _get_return_value(stmts[i + 1], source)
        if ret2 is None:
            continue

        inner = _get_inner_expr(condition)
        if inner is None:
            continue
        cond_text = source[inner.start_byte:inner.end_byte]

        indent = get_indent(source, stmts[i])

        # if (cond) return false; return true; -> return !cond;
        if ret1 == b"false" and ret2 == b"true":
            negated = _negate_condition(inner, source)
            new_source = (
                source[:stmts[i].start_byte]
                + indent + b"return " + negated + b";"
                + source[stmts[i + 1].end_byte:]
            )
            yield Variant(
                name=f"boolret_{counter}",
                pattern_name="bool_return_expr",
                description=f"Merge if/return false + return true -> return !cond",
                source=new_source,
            )
            counter += 1

        # if (cond) return true; return false; -> return cond;
        elif ret1 == b"true" and ret2 == b"false":
            new_source = (
                source[:stmts[i].start_byte]
                + indent + b"return " + cond_text + b";"
                + source[stmts[i + 1].end_byte:]
            )
            yield Variant(
                name=f"boolret_{counter}",
                pattern_name="bool_return_expr",
                description=f"Merge if/return true + return false -> return cond",
                source=new_source,
            )
            counter += 1

        # Also handle: if (cond) return false; return expr; -> return !cond && expr;
        elif ret1 == b"false":
            negated = _negate_condition(inner, source)
            new_source = (
                source[:stmts[i].start_byte]
                + indent + b"return " + negated + b" && " + ret2 + b";"
                + source[stmts[i + 1].end_byte:]
            )
            yield Variant(
                name=f"boolret_{counter}",
                pattern_name="bool_return_expr",
                description=f"Merge if/return false + return expr -> return !cond && expr",
                source=new_source,
            )
            counter += 1

        # if (cond) return true; return expr; -> return cond || expr;
        elif ret1 == b"true":
            new_source = (
                source[:stmts[i].start_byte]
                + indent + b"return " + cond_text + b" || " + ret2 + b";"
                + source[stmts[i + 1].end_byte:]
            )
            yield Variant(
                name=f"boolret_{counter}",
                pattern_name="bool_return_expr",
                description=f"Merge if/return true + return expr -> return cond || expr",
                source=new_source,
            )
            counter += 1


def _split_bool_return(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Find return !cond; and split into if (cond) return false; return true;"""
    for stmt in stmts:
        if stmt.type != "return_statement":
            continue

        expr = None
        for child in stmt.named_children:
            if child.type != "comment":
                expr = child
                break
        if expr is None:
            continue

        indent = get_indent(source, stmt)

        # return !cond; -> if (cond) return false; return true;
        if expr.type == "unary_expression":
            op = expr.child_by_field_name("operator")
            arg = expr.child_by_field_name("argument")
            if op is not None and op.text == b"!" and arg is not None:
                arg_text = source[arg.start_byte:arg.end_byte]
                # Strip outer parens if present
                if arg.type == "parenthesized_expression" and arg.named_child_count == 1:
                    inner = arg.named_children[0]
                    arg_text = source[inner.start_byte:inner.end_byte]

                new_source = (
                    source[:stmt.start_byte]
                    + indent + b"if (" + arg_text + b")\n"
                    + indent + b"    return false;\n"
                    + indent + b"return true;"
                    + source[stmt.end_byte:]
                )
                yield Variant(
                    name=f"boolret_{counter}",
                    pattern_name="bool_return_expr",
                    description="Split return !cond into if/return false + return true",
                    source=new_source,
                )
                counter += 1

        # return a && b; -> if (!a) return false; return b;
        elif expr.type == "binary_expression":
            op = expr.child_by_field_name("operator")
            if op is not None and op.text == b"&&":
                left = expr.child_by_field_name("left")
                right = expr.child_by_field_name("right")
                if left is not None and right is not None:
                    negated = _negate_condition(left, source)
                    right_text = source[right.start_byte:right.end_byte]
                    new_source = (
                        source[:stmt.start_byte]
                        + indent + b"if (" + negated + b")\n"
                        + indent + b"    return false;\n"
                        + indent + b"return " + right_text + b";"
                        + source[stmt.end_byte:]
                    )
                    yield Variant(
                        name=f"boolret_{counter}",
                        pattern_name="bool_return_expr",
                        description="Split return a && b into if (!a) return false; return b;",
                        source=new_source,
                    )
                    counter += 1


def _negate_condition(node: Node, source: bytes) -> bytes:
    """Negate a condition expression, preferring operator inversion."""
    text = source[node.start_byte:node.end_byte]

    # Already negated: !!x -> x
    if node.type == "unary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"!":
            arg = node.child_by_field_name("argument")
            if arg is not None:
                return source[arg.start_byte:arg.end_byte]

    # Binary comparison: invert operator
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text in _INVERSIONS:
            return (
                source[node.start_byte:op.start_byte]
                + _INVERSIONS[op.text]
                + source[op.end_byte:node.end_byte]
            )

    # Fallback: wrap with !()
    return b"!(" + text + b")"


def _get_return_from(node: Node, source: bytes) -> bytes | None:
    """Get return value from a consequence node (compound_statement or bare return)."""
    if node.type == "return_statement":
        return _get_return_value(node, source)
    if node.type == "compound_statement":
        stmts = [c for c in node.named_children if c.type != "comment"]
        if len(stmts) == 1 and stmts[0].type == "return_statement":
            return _get_return_value(stmts[0], source)
    return None


def _get_return_value(ret_stmt: Node, source: bytes) -> bytes | None:
    for child in ret_stmt.named_children:
        if child.type != "comment":
            return source[child.start_byte:child.end_byte]
    return None


def _get_inner_expr(condition: Node) -> Node | None:
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None
