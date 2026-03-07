"""Bool cast pattern — add bool() cast or extract bool to local variable.

The MSVC PowerPC compiler generates `clrlwi rN, rN, 24` (mask to 8 bits)
when it knows a value is bool. Missing this instruction causes BOOL_MASK
mismatches. Adding an explicit `bool()` cast or extracting to a `bool`
local variable forces the compiler to emit the mask.

Example:
    return ptr->IsActive();
    ->
    bool active = ptr->IsActive();
    return active;

    // or:
    return bool(ptr->IsActive());
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk, find_calls
from ..types import Diagnosis, FunctionContext, Variant


class BoolCastPattern(Pattern):
    name = "bool_cast"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are replace instructions (clrlwi vs no clrlwi)
        # or diff_ops suggesting bool handling differences
        if diagnosis.replace_real > 0:
            return True
        if diagnosis.clusters:
            return True
        for d in diagnosis.diff_ops:
            # rlwinm is the generic form, clrlwi is a pseudo-op
            if "rlwinm" in d.target_opcode or "rlwinm" in d.base_opcode:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        for d in diagnosis.diff_ops:
            if "rlwinm" in d.target_opcode or "rlwinm" in d.base_opcode:
                return 0.6
        if diagnosis.replace_real > 0:
            return 0.4
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for variant in _generate_bool_casts(stmt, ctx, counter):
                yield variant
                counter += 1


def _generate_bool_casts(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate bool cast variants for function calls that might return bool."""
    source = ctx.file_source

    for node in walk(stmt):
        # Pattern 1: bool() cast around call in return statement
        if node.type == "return_statement":
            expr = None
            for child in node.named_children:
                if child.type != "comment":
                    expr = child
                    break
            if expr is None:
                continue

            # Skip if already a bool cast
            if _is_bool_cast(expr):
                continue

            # Only wrap calls or method calls
            if expr.type in ("call_expression",):
                expr_text = source[expr.start_byte:expr.end_byte]
                indent = get_indent(source, node)

                # Variant A: wrap with bool()
                new_source = (
                    source[:expr.start_byte]
                    + b"bool(" + expr_text + b")"
                    + source[expr.end_byte:]
                )
                yield Variant(
                    name=f"boolcast_{counter}",
                    pattern_name="bool_cast",
                    description=f"Wrap return call with bool(): {expr_text.decode(errors='replace')[:40]}",
                    source=new_source,
                )
                counter += 1

        # Pattern 2: Extract condition call to bool local
        if node.type == "if_statement":
            # Skip if this if is part of an else-if chain (parent is else clause)
            # Inserting a declaration between "else" and "if" is invalid syntax
            if node.parent and node.parent.type == "else_clause":
                continue
            condition = node.child_by_field_name("condition")
            if condition is None:
                continue
            inner = _get_inner_expr(condition)
            if inner is None:
                continue

            # Skip if already a bool cast or simple identifier
            if inner.type in ("identifier", "true", "false"):
                continue
            if _is_bool_cast(inner):
                continue

            # Only for call expressions or negated calls
            call = inner
            if inner.type == "unary_expression":
                op = inner.child_by_field_name("operator")
                if op and op.text == b"!":
                    call = inner.child_by_field_name("argument")

            if call is not None and call.type == "call_expression":
                inner_text = source[inner.start_byte:inner.end_byte]
                indent = get_indent(source, node)

                # Extract to bool local before the if
                new_source = (
                    source[:node.start_byte]
                    + b"bool _cond = " + inner_text + b";\n"
                    + indent + b"if (_cond)"
                    + source[condition.end_byte:]
                )
                yield Variant(
                    name=f"boolcast_{counter}",
                    pattern_name="bool_cast",
                    description=f"Extract condition to bool local: {inner_text.decode(errors='replace')[:40]}",
                    source=new_source,
                )
                counter += 1

            # Pattern 4: Extract comparison to bool local in if-condition
            # Only when one side is a call (e.g. da->Size() > 1) — simple
            # identifier comparisons don't benefit from bool intermediates
            elif inner.type == "binary_expression":
                op_node = inner.child_by_field_name("operator")
                if op_node and op_node.text.decode() in {"<", ">", "<=", ">=", "==", "!="}:
                    left = inner.child_by_field_name("left")
                    right = inner.child_by_field_name("right")
                    has_call = (
                        (left is not None and _has_call(left))
                        or (right is not None and _has_call(right))
                    )
                    if not has_call:
                        continue

                    inner_text = source[inner.start_byte:inner.end_byte]
                    indent = get_indent(source, node)

                    new_source = (
                        source[:node.start_byte]
                        + indent + b"bool _cond = " + inner_text + b";\n"
                        + indent + b"if (_cond)"
                        + source[condition.end_byte:]
                    )
                    yield Variant(
                        name=f"boolcast_{counter}",
                        pattern_name="bool_cast",
                        description=f"Extract comparison to bool local: {inner_text.decode(errors='replace')[:40]}",
                        source=new_source,
                    )
                    counter += 1

        # Pattern 3: Bool cast in assignment
        if node.type == "expression_statement":
            for child in node.named_children:
                if child.type != "assignment_expression":
                    continue
                right = child.child_by_field_name("right")
                if right is None or right.type != "call_expression":
                    continue
                if _is_bool_cast(right):
                    continue

                right_text = source[right.start_byte:right.end_byte]
                new_source = (
                    source[:right.start_byte]
                    + b"bool(" + right_text + b")"
                    + source[right.end_byte:]
                )
                yield Variant(
                    name=f"boolcast_{counter}",
                    pattern_name="bool_cast",
                    description=f"Wrap assignment RHS with bool(): {right_text.decode(errors='replace')[:40]}",
                    source=new_source,
                )
                counter += 1


def _is_bool_cast(node: Node) -> bool:
    """Check if node is already a bool() cast."""
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func and func.text == b"bool":
            return True
    return False


def _has_call(node: Node) -> bool:
    """Check if node contains a call_expression anywhere."""
    if node.type == "call_expression":
        return True
    for child in node.named_children:
        if _has_call(child):
            return True
    return False


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None
