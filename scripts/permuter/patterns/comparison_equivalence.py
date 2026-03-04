"""Comparison equivalence pattern — swap equivalent comparison forms.

Targets the documented pattern where `i < 2` vs `i <= 1` generates different
`cmpwi` immediates (TECHNICAL_NOTES line 281-293).

Transformations (integer literal RHS only):
    x < N  -> x <= N-1
    x <= N -> x < N+1
    x > N  -> x >= N+1
    x >= N -> x > N-1

Example:
    if (i < 2)
    ->
    if (i <= 1)
"""

from __future__ import annotations

from typing import Iterator

from .base import Pattern
from ..ast_queries import find_comparisons
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Map: original op -> (new op, delta to add to RHS)
_EQUIVALENCES = {
    "<": ("<=", -1),
    "<=": ("<", +1),
    ">": (">=", +1),
    ">=": (">", -1),
}

# Only look for operators in _EQUIVALENCES (exclude == and !=)
_OPS = set(_EQUIVALENCES.keys())


class ComparisonEquivalencePattern(Pattern):
    name = "comparison_equivalence"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        cmp_opcodes = {"cmpw", "cmpwi", "cmplw", "cmplwi", "cmpd", "cmpdi", "cmpld", "cmpldi"}
        for d in diagnosis.diff_ops:
            if d.target_opcode in cmp_opcodes or d.base_opcode in cmp_opcodes:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: cmpwi with different immediates — exactly what < vs <= fixes
        for d in diagnosis.diff_ops:
            if d.target_opcode == d.base_opcode and d.target_opcode in ("cmpwi", "cmplwi"):
                return 0.7  # same opcode, likely different immediate
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for cmp_node in find_comparisons(stmt, ops=_OPS):
                op_node = cmp_node.child_by_field_name("operator")
                right = cmp_node.child_by_field_name("right")
                if op_node is None or right is None:
                    continue

                op_text = op_node.text.decode("utf-8") if op_node.text else None
                if op_text not in _EQUIVALENCES:
                    continue

                # RHS must be a number literal (integer)
                if right.type != "number_literal":
                    continue

                right_text = right.text
                if right_text is None:
                    continue

                try:
                    value = int(right_text.decode("utf-8"), 0)
                except ValueError:
                    continue

                new_op, delta = _EQUIVALENCES[op_text]
                new_value = value + delta

                # Skip if new value would be negative (unusual in decomp context)
                if new_value < 0:
                    continue

                new_op_bytes = new_op.encode("utf-8")
                new_value_bytes = str(new_value).encode("utf-8")

                # Replace operator and RHS value
                ed = SourceEditor(ctx.file_source)
                ed.replace_node(op_node, new_op_bytes)
                ed.replace_node(right, new_value_bytes)
                new_source = ed.apply()

                yield Variant(
                    name=f"cmpeq_{counter}",
                    pattern_name=self.name,
                    description=f"Comparison equivalence: {op_text} {value} -> {new_op} {new_value}",
                    source=new_source,
                )
                counter += 1
