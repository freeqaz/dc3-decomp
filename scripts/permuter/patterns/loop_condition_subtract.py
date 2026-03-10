"""Loop condition subtract pattern — rewrite `a >= b` as `a - b >= 0` in loops.

MSVC PPC can emit `subf.` (subtract-and-record) instead of `cmpw` (compare)
for loop conditions. The `subf.` form sets CR0 as a side effect of the
subtraction, eliminating a separate compare instruction.

Transformations:
    while (a >= b)    ->  while (a - b >= 0)
    while (b <= a)    ->  while (a - b >= 0)
    for (...; a >= b; ...)  ->  for (...; a - b >= 0; ...)

Also generates the reverse (in case the source already has subtraction form):
    while (a - b >= 0)  ->  while (a >= b)

Proven fix: Locale::FindDataIndex 97.2% -> 100.0%
"""

from __future__ import annotations

from typing import Iterator

from .base import Pattern
from ..ast_queries import find_comparisons
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Opcodes that indicate a comparison mismatch worth trying
_CMP_OPCODES = {"cmpw", "cmpwi", "cmplw", "cmplwi"}
_SUBF_OPCODES = {"subf.", "subf", "subfc.", "subfc", "subic.", "subic"}


class LoopConditionSubtractPattern(Pattern):
    name = "loop_condition_subtract"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when we see subf. vs cmpw in replace pairs
        for d in diagnosis.diff_ops:
            tgt = d.target_opcode.rstrip(".")
            base = d.base_opcode.rstrip(".")
            # Target has subf, base has cmpw (or vice versa)
            if (d.target_opcode in _SUBF_OPCODES and d.base_opcode in _CMP_OPCODES):
                return True
            if (d.base_opcode in _SUBF_OPCODES and d.target_opcode in _CMP_OPCODES):
                return True
        # Also relevant for generic cmpw mismatches (might be fixable)
        for d in diagnosis.diff_ops:
            if d.target_opcode in _CMP_OPCODES or d.base_opcode in _CMP_OPCODES:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # High priority if we see subf. vs cmpw specifically
        for d in diagnosis.diff_ops:
            if (d.target_opcode in _SUBF_OPCODES and d.base_opcode in _CMP_OPCODES):
                return 0.8
            if (d.base_opcode in _SUBF_OPCODES and d.target_opcode in _CMP_OPCODES):
                return 0.8
        return 0.3  # generic cmpw mismatch — lower confidence

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            # Find while/for loop conditions containing >= or <=
            for node in _walk(stmt):
                condition = None
                if node.type == "while_statement":
                    condition = node.child_by_field_name("condition")
                elif node.type == "for_statement":
                    condition = node.child_by_field_name("condition")

                if condition is None:
                    continue

                # Try to transform comparisons in this condition
                for variant in self._transform_condition(ctx, condition, counter):
                    yield variant
                    counter += 1

    def _transform_condition(
        self, ctx: FunctionContext, condition, counter: int
    ) -> Iterator[Variant]:
        """Generate variants for a loop condition node."""
        for cmp_node in find_comparisons(condition, ops={">=", "<=", ">", "<"}):
            left = cmp_node.child_by_field_name("left")
            right = cmp_node.child_by_field_name("right")
            op_node = cmp_node.child_by_field_name("operator")
            if left is None or right is None or op_node is None:
                continue

            op_text = op_node.text.decode("utf-8") if op_node.text else None
            left_text = ctx.file_source[left.start_byte:left.end_byte].decode("utf-8")
            right_text = ctx.file_source[right.start_byte:right.end_byte].decode("utf-8")

            # Skip if RHS is 0 and LHS is a subtraction (already in target form)
            # Try: a >= b  ->  a - b >= 0
            if op_text == ">=" and right_text != "0":
                new_expr = f"{left_text} - {right_text} >= 0"
                ed = SourceEditor(ctx.file_source)
                ed.replace_range(cmp_node.start_byte, cmp_node.end_byte,
                                 new_expr.encode("utf-8"))
                yield Variant(
                    name=f"loopsub_{counter}",
                    pattern_name=self.name,
                    description=f"Loop subtract: {left_text} >= {right_text} -> {new_expr}",
                    source=ed.apply(),
                )
                counter += 1

            # Try: b <= a  ->  a - b >= 0
            elif op_text == "<=" and left_text != "0":
                new_expr = f"{right_text} - {left_text} >= 0"
                ed = SourceEditor(ctx.file_source)
                ed.replace_range(cmp_node.start_byte, cmp_node.end_byte,
                                 new_expr.encode("utf-8"))
                yield Variant(
                    name=f"loopsub_{counter}",
                    pattern_name=self.name,
                    description=f"Loop subtract: {left_text} <= {right_text} -> {new_expr}",
                    source=ed.apply(),
                )
                counter += 1

            # Try: a > b  ->  a - b > 0  (less common but possible)
            elif op_text == ">" and right_text != "0":
                new_expr = f"{left_text} - {right_text} > 0"
                ed = SourceEditor(ctx.file_source)
                ed.replace_range(cmp_node.start_byte, cmp_node.end_byte,
                                 new_expr.encode("utf-8"))
                yield Variant(
                    name=f"loopsub_{counter}",
                    pattern_name=self.name,
                    description=f"Loop subtract: {left_text} > {right_text} -> {new_expr}",
                    source=ed.apply(),
                )
                counter += 1

            # Reverse: a - b >= 0  ->  a >= b (if LHS is a subtraction)
            if op_text == ">=" and right_text == "0":
                # Check if left side is "X - Y"
                if left.type == "binary_expression":
                    inner_op = left.child_by_field_name("operator")
                    if inner_op and inner_op.text == b"-":
                        inner_left = left.child_by_field_name("left")
                        inner_right = left.child_by_field_name("right")
                        if inner_left and inner_right:
                            il = ctx.file_source[inner_left.start_byte:inner_left.end_byte].decode("utf-8")
                            ir = ctx.file_source[inner_right.start_byte:inner_right.end_byte].decode("utf-8")
                            new_expr = f"{il} >= {ir}"
                            ed = SourceEditor(ctx.file_source)
                            ed.replace_range(cmp_node.start_byte, cmp_node.end_byte,
                                             new_expr.encode("utf-8"))
                            yield Variant(
                                name=f"loopsub_{counter}",
                                pattern_name=self.name,
                                description=f"Loop subtract reverse: {left_text} >= 0 -> {new_expr}",
                                source=ed.apply(),
                            )
                            counter += 1


def _walk(node):
    """Walk all descendant nodes."""
    yield node
    for child in node.children:
        yield from _walk(child)
