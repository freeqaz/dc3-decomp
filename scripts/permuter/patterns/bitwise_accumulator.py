"""Bitwise accumulator pattern — replace && with & for bool accumulation.

The compiler generates `and` (bitwise) for `&` but short-circuit branches
for `&&`. When the target uses `and`, switching to bitwise `&` matches.

Also tries the reverse: `&` to `&&`.

Example:
    allRestricted = allRestricted && userIsRestricted;
    ->
    allRestricted = allRestricted & userIsRestricted;
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class BitwiseAccumulatorPattern(Pattern):
    name = "bitwise_accumulator"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when control flow differs or there are clusters
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong signal: diff has `and` instruction (bitwise AND in target)
        for d in diagnosis.diff_ops:
            if d.target_opcode == "and" or d.base_opcode == "and":
                return 0.7
        # Weaker: branch diffs that could be && vs &
        if diagnosis.clusters:
            return 0.3
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        for stmt in ctx.statements:
            for node in walk(stmt):
                if node.type != "binary_expression":
                    continue

                op = node.child_by_field_name("operator")
                if op is None:
                    continue

                op_text = op.text

                # && -> & (logical to bitwise)
                if op_text == b"&&":
                    new_source = (
                        source[:op.start_byte]
                        + b"&"
                        + source[op.end_byte:]
                    )
                    yield Variant(
                        name=f"bitacc_{counter}",
                        pattern_name="bitwise_accumulator",
                        description="Replace && with & (bitwise accumulator)",
                        source=new_source,
                    )
                    counter += 1

                # & -> && (bitwise to logical)
                elif op_text == b"&":
                    # Skip if this looks like a pointer/address operation
                    left = node.child_by_field_name("left")
                    right = node.child_by_field_name("right")
                    if left is None or right is None:
                        continue
                    # Skip if either side is a number literal (likely bitmask)
                    if left.type == "number_literal" or right.type == "number_literal":
                        continue
                    # Skip hex literals embedded in casts
                    left_text = source[left.start_byte:left.end_byte]
                    right_text = source[right.start_byte:right.end_byte]
                    if b"0x" in left_text or b"0x" in right_text:
                        continue

                    new_source = (
                        source[:op.start_byte]
                        + b"&&"
                        + source[op.end_byte:]
                    )
                    yield Variant(
                        name=f"bitacc_{counter}",
                        pattern_name="bitwise_accumulator",
                        description="Replace & with && (logical accumulator)",
                        source=new_source,
                    )
                    counter += 1

                # Also handle compound assignment: &= <-> &&=  doesn't exist,
                # but we can handle: x = x && y -> x = x & y (caught above)
                # and: x &= y -> x = x & y (no change needed, same codegen)
