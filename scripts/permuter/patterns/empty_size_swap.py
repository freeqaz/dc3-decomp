"""Empty/size swap pattern — swap between empty() and size() == 0.

Targets the documented codegen difference (TECHNICAL_NOTES line 465-480):
empty() generates pointer comparison, size() == 0 generates division.

Direction detection:
    divw/divwu in TARGET means target uses size() -> swap empty() to size()
    cmplw in TARGET means target uses empty() -> swap size() to empty()

Transformations:
    x.empty()        -> x.size() == 0
    !x.empty()       -> x.size() != 0  (and > 0 variant)
    x.size() == 0    -> x.empty()
    x.size() != 0    -> !x.empty()
    x.size() > 0     -> !x.empty()

Example:
    if (mList.empty())
    ->
    if (mList.size() == 0)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant


class EmptySizeSwapPattern(Pattern):
    name = "empty_size_swap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return self._detect_direction(diagnosis) is not None

    def _detect_direction(self, diagnosis: Diagnosis) -> str | None:
        """Detect which direction to swap based on opcode mismatches.

        Returns "to_size" if target uses division/multiply (size()), "to_empty" if
        target uses pointer comparison (empty()), or None if no signal.

        Only fires on divw/divwu/mulli opcodes — the actual codegen signature of
        size() on STL containers. The previous subf secondary heuristic was removed
        because subf appears in unrelated contexts (loop conditions, binary search).
        """
        _SIZE_OPS = {"divw", "divwu", "mulli"}

        target_has_size = any(
            d.target_opcode in _SIZE_OPS for d in diagnosis.diff_ops
        )
        base_has_size = any(
            d.base_opcode in _SIZE_OPS for d in diagnosis.diff_ops
        )
        if target_has_size and not base_has_size:
            return "to_size"  # target uses division/multiply, swap empty→size
        if base_has_size and not target_has_size:
            return "to_empty"  # we use division/multiply, target uses pointer cmp

        return None

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        direction = None
        if ctx.diagnosis is not None:
            direction = self._detect_direction(ctx.diagnosis)
        # If no direction detected (shouldn't happen since relevant() checks),
        # stay silent rather than generating both directions
        if direction is None:
            return

        counter = 0
        for stmt in ctx.statements:
            for variant in _find_swaps(stmt, ctx, counter, direction):
                yield variant
                counter += 1


def _find_swaps(
    node: Node, ctx: FunctionContext, counter: int, direction: str
) -> Iterator[Variant]:
    """Find empty()/size() swap opportunities recursively."""
    source = ctx.file_source

    # Pattern 1: x.empty() -> x.size() == 0
    # Pattern 2: !x.empty() -> x.size() != 0 / x.size() > 0
    if node.type == "call_expression" and direction == "to_size":
        func = node.child_by_field_name("function")
        args = node.child_by_field_name("arguments")
        if (
            func is not None
            and func.type == "field_expression"
            and args is not None
        ):
            field = func.child_by_field_name("field")
            obj = func.child_by_field_name("argument")
            if (
                field is not None
                and field.text == b"empty"
                and obj is not None
                and _arg_count(args) == 0
            ):
                obj_text = source[obj.start_byte : obj.end_byte]
                # Determine the operator between obj and field (. or ->)
                op = _get_field_operator(func, source)

                # Check if this call is negated: !x.empty()
                parent = node.parent
                is_negated = (
                    parent is not None
                    and parent.type == "unary_expression"
                    and _get_unary_op(parent) == b"!"
                )

                if is_negated:
                    # !x.empty() -> x.size() != 0
                    replace_node = parent
                    new_expr = obj_text + op + b"size() != 0"
                    new_source = (
                        source[: replace_node.start_byte]
                        + new_expr
                        + source[replace_node.end_byte :]
                    )
                    yield Variant(
                        name=f"emptysize_{counter}",
                        pattern_name="empty_size_swap",
                        description="!x.empty() -> x.size() != 0",
                        source=new_source,
                    )
                    counter += 1

                    # !x.empty() -> x.size() > 0
                    new_expr2 = obj_text + op + b"size() > 0"
                    new_source2 = (
                        source[: replace_node.start_byte]
                        + new_expr2
                        + source[replace_node.end_byte :]
                    )
                    yield Variant(
                        name=f"emptysize_{counter}",
                        pattern_name="empty_size_swap",
                        description="!x.empty() -> x.size() > 0",
                        source=new_source2,
                    )
                    counter += 1
                else:
                    # x.empty() -> x.size() == 0
                    new_expr = obj_text + op + b"size() == 0"
                    new_source = (
                        source[: node.start_byte]
                        + new_expr
                        + source[node.end_byte :]
                    )
                    yield Variant(
                        name=f"emptysize_{counter}",
                        pattern_name="empty_size_swap",
                        description="x.empty() -> x.size() == 0",
                        source=new_source,
                    )
                    counter += 1

                return  # Don't recurse into this call's children

    # Pattern 3: x.size() == 0 -> x.empty()
    # Pattern 4: x.size() != 0 -> !x.empty()
    # Pattern 5: x.size() > 0 -> !x.empty()
    if node.type == "binary_expression" and direction == "to_empty":
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        op_node = node.child_by_field_name("operator")
        if left is not None and right is not None and op_node is not None:
            op_text = op_node.text
            right_text = right.text

            if (
                right_text is not None
                and right_text.strip() == b"0"
                and left.type == "call_expression"
            ):
                func = left.child_by_field_name("function")
                args = left.child_by_field_name("arguments")
                if (
                    func is not None
                    and func.type == "field_expression"
                    and args is not None
                ):
                    field = func.child_by_field_name("field")
                    obj = func.child_by_field_name("argument")
                    if (
                        field is not None
                        and field.text == b"size"
                        and obj is not None
                        and _arg_count(args) == 0
                    ):
                        obj_text = source[obj.start_byte : obj.end_byte]
                        member_op = _get_field_operator(func, source)

                        if op_text == b"==":
                            # x.size() == 0 -> x.empty()
                            new_expr = obj_text + member_op + b"empty()"
                            new_source = (
                                source[: node.start_byte]
                                + new_expr
                                + source[node.end_byte :]
                            )
                            yield Variant(
                                name=f"emptysize_{counter}",
                                pattern_name="empty_size_swap",
                                description="x.size() == 0 -> x.empty()",
                                source=new_source,
                            )
                            counter += 1

                        elif op_text in (b"!=", b">"):
                            # x.size() != 0 -> !x.empty()
                            # x.size() > 0 -> !x.empty()
                            new_expr = b"!" + obj_text + member_op + b"empty()"
                            new_source = (
                                source[: node.start_byte]
                                + new_expr
                                + source[node.end_byte :]
                            )
                            op_str = op_text.decode("utf-8")
                            yield Variant(
                                name=f"emptysize_{counter}",
                                pattern_name="empty_size_swap",
                                description=f"x.size() {op_str} 0 -> !x.empty()",
                                source=new_source,
                            )
                            counter += 1

                        return  # Don't recurse into this comparison's children

    # Recurse into children
    for child in node.children:
        for variant in _find_swaps(child, ctx, counter, direction):
            yield variant
            counter += 1


def _arg_count(args_node: Node) -> int:
    """Count non-punctuation children in an argument_list."""
    return sum(1 for c in args_node.named_children)


def _get_field_operator(field_expr: Node, source: bytes) -> bytes:
    """Get the . or -> operator from a field_expression."""
    # The operator is between the argument and field children
    arg = field_expr.child_by_field_name("argument")
    field = field_expr.child_by_field_name("field")
    if arg is not None and field is not None:
        between = source[arg.end_byte : field.start_byte]
        if b"->" in between:
            return b"->"
        return b"."
    return b"."


def _get_unary_op(node: Node) -> bytes | None:
    """Get the operator of a unary_expression."""
    op = node.child_by_field_name("operator")
    return op.text if op is not None else None
