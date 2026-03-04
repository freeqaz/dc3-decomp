"""MILO_WARN / MILO_NOTIFY / MILO_LOG / MILO_FAIL macro swapping.

Win rate: untested (new pattern).

Milo engine logging macros generate different code:
- MILO_WARN includes file/line metadata
- MILO_NOTIFY is lighter weight
- MILO_LOG is the simplest
- MILO_FAIL triggers an assertion path

Swapping between these can fix instruction count mismatches and branch
differences when the original code used a different log level than what
we've guessed.

Also handles MILO_NOTIFY vs MILO_NOTIFY_ONCE (adds a static guard variable).

Transformations:
    MILO_WARN(...)        -> MILO_NOTIFY(...)
    MILO_NOTIFY(...)      -> MILO_WARN(...)
    MILO_NOTIFY(...)      -> MILO_LOG(...)
    MILO_NOTIFY_ONCE(...) -> MILO_NOTIFY(...)

Detection signals:
    - Insert/delete clusters near string references
    - Scope counter mismatches (NOTIFY_ONCE has static guard)
    - Extra/missing branch instructions
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Milo log macros and their swap targets
_LOG_MACROS = {
    b"MILO_WARN": [b"MILO_NOTIFY", b"MILO_LOG"],
    b"MILO_NOTIFY": [b"MILO_WARN", b"MILO_LOG"],
    b"MILO_LOG": [b"MILO_WARN", b"MILO_NOTIFY"],
    b"MILO_FAIL": [b"MILO_WARN"],
    b"MILO_NOTIFY_ONCE": [b"MILO_NOTIFY"],
}

_ALL_MACROS = set(_LOG_MACROS.keys())


class MiloLogSwapPattern(Pattern):
    name = "milo_log_swap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Clusters suggest instruction count differences (log macros vary in size)
        if diagnosis.clusters:
            return True

        # Scope counter mismatches (NOTIFY_ONCE static guard)
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("stw", "lwz") or d.base_opcode in ("stw", "lwz"):
                return True

        # Insert/delete differences
        if diagnosis.insert_count > 0 or diagnosis.delete_count > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        # Low priority — log macro swaps are rare fixes
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find all MILO_* macro call sites
        macro_sites = _find_milo_macros(body, source)

        for call_node, macro_name, start, end in macro_sites:
            if counter >= 8:
                break

            targets = _LOG_MACROS.get(macro_name, [])
            for replacement in targets:
                if counter >= 8:
                    break

                ed = SourceEditor(source)
                ed.replace_range(start, end, replacement)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                cur = macro_name.decode("utf-8", errors="replace")
                rep = replacement.decode("utf-8", errors="replace")
                yield Variant(
                    name=f"logswap_{counter}",
                    pattern_name=self.name,
                    description=f"Swap {cur}() -> {rep}()",
                    source=new_source,
                )
                counter += 1


def _find_milo_macros(
    node: Node, source: bytes
) -> list[tuple[Node, bytes, int, int]]:
    """Find call_expression nodes calling MILO_WARN/NOTIFY/LOG/FAIL macros.

    Returns [(call_node, macro_name_bytes, name_start, name_end), ...]
    """
    results = []
    for n in walk(node):
        if n.type != "call_expression":
            continue

        func = n.child_by_field_name("function")
        if func is None:
            continue

        func_text = source[func.start_byte:func.end_byte]
        if func_text in _ALL_MACROS:
            results.append((n, func_text, func.start_byte, func.end_byte))

    return results
