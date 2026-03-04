"""fabs/fabsf/std::fabs variant swapping.

Win rate: untested (new pattern).

The compiler generates different instructions for fabs() vs fabsf() vs std::fabs():
- fabs(x) with double argument generates lfd (64-bit load) + fabs
- fabsf(x) with float generates lfs (32-bit load) + fabs
- std::fabs(x) may resolve to either depending on overload

Swapping between these variants can fix instruction width mismatches.

Transformations:
    fabs(x)       -> fabsf(x)
    fabsf(x)      -> fabs(x)
    std::fabs(x)  -> fabsf(x)
    fabsf(x)      -> std::fabs(x)

Detection signals:
    - lfd vs lfs mismatches (float width)
    - fabs instruction present in diff
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# All fabs variants we want to swap between
_FABS_VARIANTS = [b"fabs", b"fabsf", b"std::fabs"]


class FabsVariantPattern(Pattern):
    name = "fabs_variant"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # lfd vs lfs mismatches suggest float width issue
        for d in diagnosis.diff_ops:
            if (d.target_opcode == "lfd" and d.base_opcode == "lfs") or \
               (d.target_opcode == "lfs" and d.base_opcode == "lfd"):
                return True
            # fabs instruction differences
            if "fabs" in (d.target_opcode or "") or "fabs" in (d.base_opcode or ""):
                return True

        # Also trigger on any replace mismatches (broad but fabs is cheap to try)
        if diagnosis.replace_real > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        for d in diagnosis.diff_ops:
            if (d.target_opcode == "lfd" and d.base_opcode == "lfs") or \
               (d.target_opcode == "lfs" and d.base_opcode == "lfd"):
                return 0.7
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find all fabs/fabsf/std::fabs call sites
        call_sites = _find_fabs_calls(body, source)

        for call_node, current_name, start, end in call_sites:
            if counter >= 6:
                break

            # Generate variants swapping to each other form
            for replacement in _FABS_VARIANTS:
                if replacement == current_name:
                    continue

                ed = SourceEditor(source)
                ed.replace_range(start, end, replacement)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                cur = current_name.decode("utf-8", errors="replace")
                rep = replacement.decode("utf-8", errors="replace")
                yield Variant(
                    name=f"fabs_{counter}",
                    pattern_name=self.name,
                    description=f"Swap {cur}() -> {rep}()",
                    source=new_source,
                )
                counter += 1


def _find_fabs_calls(
    node: Node, source: bytes
) -> list[tuple[Node, bytes, int, int]]:
    """Find call_expression nodes calling fabs/fabsf/std::fabs.

    Returns [(call_node, current_name_bytes, name_start, name_end), ...]
    """
    results = []
    for n in walk(node):
        if n.type != "call_expression":
            continue

        func = n.child_by_field_name("function")
        if func is None:
            continue

        func_text = source[func.start_byte:func.end_byte]
        if func_text in _FABS_VARIANTS:
            results.append((n, func_text, func.start_byte, func.end_byte))

    return results
