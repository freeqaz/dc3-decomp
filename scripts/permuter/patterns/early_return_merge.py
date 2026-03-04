"""Early return merge pattern — combine guard returns into || chain (and reverse).

Multiple `if (cond) return false;` statements generate redundant branch
sequences. Combining them into a single `if (c1 || c2 || c3) return false;`
shares the return target.

Also does the reverse: splits a || chain into sequential guard returns.

Example:
    if (s < f.front) return false;
    if (s < f.back) return false;
    ->
    if (s < f.front || s < f.back) return false;
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


class EarlyReturnMergePattern(Pattern):
    name = "early_return_merge"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Multiple branch diffs + clusters suggest redundant guard branches
        branch_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES
        )
        if branch_count >= 3 and len(diagnosis.clusters) >= 2:
            return 0.6
        if diagnosis.clusters:
            return 0.3
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source
        stmts = ctx.statements

        # Direction 1: Merge consecutive guard returns into || chain
        for variant in _merge_guard_returns(stmts, source, counter):
            yield variant
            counter += 1

        # Direction 2: Split || chain in guard return into separate returns
        for variant in _split_guard_returns(stmts, ctx, counter):
            yield variant
            counter += 1


def _merge_guard_returns(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Find consecutive if (cond) return X; and merge into || chain."""
    # Find runs of consecutive guard returns
    i = 0
    while i < len(stmts):
        run_start = i
        return_value = None
        conditions = []

        while i < len(stmts):
            guard = _extract_guard_return(stmts[i], source)
            if guard is None:
                break
            cond_text, ret_text = guard
            if return_value is None:
                return_value = ret_text
            elif ret_text != return_value:
                break
            conditions.append((stmts[i], cond_text))
            i += 1

        if len(conditions) >= 2:
            # Merge all conditions with ||
            first_stmt = conditions[0][0]
            last_stmt = conditions[-1][0]
            indent = get_indent(source, first_stmt)

            merged_cond = b" || ".join(c for _, c in conditions)
            merged = indent + b"if (" + merged_cond + b")\n" + indent + b"    return " + return_value + b";"

            new_source = (
                source[:first_stmt.start_byte]
                + merged
                + source[last_stmt.end_byte:]
            )
            yield Variant(
                name=f"retmerge_{counter}",
                pattern_name="early_return_merge",
                description=f"Merge {len(conditions)} guard returns into || chain",
                source=new_source,
            )
            counter += 1

        if not conditions:
            i += 1


def _split_guard_returns(
    stmts: list[Node], ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find if (a || b || c) return X; and split into separate guard returns."""
    source = ctx.file_source

    for stmt in stmts:
        if stmt.type != "if_statement":
            continue

        condition = stmt.child_by_field_name("condition")
        consequence = stmt.child_by_field_name("consequence")
        alternative = stmt.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue
        if alternative is not None:
            continue

        # Check consequence is a return statement
        ret_stmt = _get_sole_return(consequence)
        if ret_stmt is None:
            # Could also be a bare return (no compound_statement)
            if consequence.type == "return_statement":
                ret_stmt = consequence
            else:
                continue

        ret_text = _get_return_value(ret_stmt, source)
        if ret_text is None:
            continue

        # Check condition contains ||
        inner = _get_inner_expr(condition)
        if inner is None:
            continue

        # Collect all || operands
        operands = _collect_or_operands(inner, source)
        if len(operands) < 2:
            continue

        indent = get_indent(source, stmt)
        parts = []
        for op_text in operands:
            parts.append(indent + b"if (" + op_text + b")\n" + indent + b"    return " + ret_text + b";")

        new_source = (
            source[:stmt.start_byte]
            + b"\n".join(parts)
            + source[stmt.end_byte:]
        )
        yield Variant(
            name=f"retmerge_{counter}",
            pattern_name="early_return_merge",
            description=f"Split || chain into {len(operands)} separate guard returns",
            source=new_source,
        )
        counter += 1


def _extract_guard_return(stmt: Node, source: bytes) -> tuple[bytes, bytes] | None:
    """Extract (condition_text, return_value_text) from `if (cond) return val;`."""
    if stmt.type != "if_statement":
        return None

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    alternative = stmt.child_by_field_name("alternative")

    if condition is None or consequence is None:
        return None
    if alternative is not None:
        return None

    # Get return statement from consequence
    ret_stmt = None
    if consequence.type == "return_statement":
        ret_stmt = consequence
    elif consequence.type == "compound_statement":
        ret_stmt = _get_sole_return(consequence)
    if ret_stmt is None:
        return None

    ret_text = _get_return_value(ret_stmt, source)
    if ret_text is None:
        return None

    inner = _get_inner_expr(condition)
    if inner is None:
        return None

    cond_text = source[inner.start_byte:inner.end_byte]
    return cond_text, ret_text


def _get_sole_return(compound_stmt: Node) -> Node | None:
    """Get single return statement from compound_statement."""
    if compound_stmt.type != "compound_statement":
        return None
    stmts = [c for c in compound_stmt.named_children if c.type != "comment"]
    if len(stmts) == 1 and stmts[0].type == "return_statement":
        return stmts[0]
    return None


def _get_return_value(ret_stmt: Node, source: bytes) -> bytes | None:
    """Get the return value text from a return_statement."""
    for child in ret_stmt.named_children:
        if child.type != "comment":
            return source[child.start_byte:child.end_byte]
    return None


def _get_inner_expr(condition: Node) -> Node | None:
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _collect_or_operands(node: Node, source: bytes) -> list[bytes]:
    """Recursively collect operands of || chain."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"||":
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            result = []
            if left:
                result.extend(_collect_or_operands(left, source))
            if right:
                result.extend(_collect_or_operands(right, source))
            return result
    return [source[node.start_byte:node.end_byte]]
