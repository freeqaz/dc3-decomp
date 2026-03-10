"""Comparison flip pattern — swap comparison operands.

Swapping `a < b` to `b > a` changes which value is loaded into which
register and can affect comparison instruction selection.

Example:
    if (a < b)  ->  if (b > a)
    if (a == b) ->  if (b == a)
"""

from __future__ import annotations

from typing import Iterator

from .base import Pattern
from ..ast_queries import find_comparisons
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Map operator to its flipped equivalent when operands are swapped
_FLIP_MAP = {
    "<": ">",
    ">": "<",
    "<=": ">=",
    ">=": "<=",
    "==": "==",
    "!=": "!=",
}

_CMP_OPCODES = {"cmpw", "cmpwi", "cmplw", "cmplwi", "cmpd", "cmpdi", "cmpld", "cmpldi"}


class ComparisonFlipPattern(Pattern):
    name = "comparison_flip"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when diff_ops include comparison opcodes
        for d in diagnosis.diff_ops:
            if d.target_opcode in _CMP_OPCODES or d.base_opcode in _CMP_OPCODES:
                return True
        # Also relevant if there are register mismatches near comparisons
        if diagnosis.reg_swap_pairs:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        cmp_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _CMP_OPCODES or d.base_opcode in _CMP_OPCODES
        )
        if cmp_count > 0:
            return 0.6
        return 0.2  # regswap only — weaker signal

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            # Region filter: skip statements outside mismatch regions
            if not ctx.node_in_mismatch_region(stmt):
                continue
            for cmp_node in find_comparisons(stmt):
                left = cmp_node.child_by_field_name("left")
                right = cmp_node.child_by_field_name("right")
                op_node = cmp_node.child_by_field_name("operator")
                if left is None or right is None or op_node is None:
                    continue

                op_text = op_node.text.decode("utf-8") if op_node.text else None
                if op_text not in _FLIP_MAP:
                    continue

                left_text = ctx.file_source[left.start_byte:left.end_byte]
                right_text = ctx.file_source[right.start_byte:right.end_byte]

                # Skip if operands are identical
                if left_text == right_text:
                    continue

                new_op = _FLIP_MAP[op_text].encode("utf-8")

                # Swap operands and flip operator
                ed = SourceEditor(ctx.file_source)
                ed.swap_nodes(left, right)
                ed.replace_node(op_node, new_op)
                new_source = ed.apply()

                left_str = left_text.decode("utf-8", errors="replace")[:20]
                right_str = right_text.decode("utf-8", errors="replace")[:20]
                new_op_str = _FLIP_MAP[op_text]

                yield Variant(
                    name=f"cmpflip_{counter}",
                    pattern_name=self.name,
                    description=f"Flip comparison: {left_str} {op_text} {right_str} -> {right_str} {new_op_str} {left_str}",
                    source=new_source,
                )
                counter += 1
