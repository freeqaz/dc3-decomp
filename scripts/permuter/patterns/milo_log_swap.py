"""MILO_WARN / MILO_NOTIFY / MILO_LOG / MILO_FAIL macro swapping.

Milo engine logging macros generate different code:
- MILO_WARN includes file/line metadata
- MILO_NOTIFY is lighter weight (DC3 only — RB3 does not define this macro)
- MILO_LOG is the simplest
- MILO_FAIL triggers an assertion path
- MILO_NOTIFY_ONCE wraps a body in a {static guard; ...} brace block

Swapping between these can fix instruction count mismatches and branch
differences when the original code used a different log level than what
we've guessed.

Macro availability is project-specific. RB3 (this project) only defines
MILO_LOG / MILO_WARN / MILO_FAIL / MILO_NOTIFY_ONCE / MILO_NOTIFY_BETA /
MILO_NOTIFY_ONCE_BETA / MILO_LOG_ONCE. DC3 additionally defines
MILO_NOTIFY. The original pattern emitted MILO_NOTIFY unconditionally,
which made every variant fail to compile on RB3 (13/13 = 100% failure,
0 wins, prompting opt_in = True).

The generator now probes the project's `src/system/os/Debug.h` to discover
which MILO_* macros are #define'd, and only emits swaps whose target is
known to compile in this project.

Transformations (subject to macro availability):
    MILO_WARN(...)        -> MILO_LOG(...) | MILO_NOTIFY(...) [DC3]
    MILO_NOTIFY(...)      -> MILO_WARN(...) | MILO_LOG(...)   [DC3]
    MILO_LOG(...)         -> MILO_WARN(...) | MILO_NOTIFY(...) [DC3]
    MILO_FAIL(...)        -> MILO_WARN(...)
    MILO_NOTIFY_ONCE(...) -> MILO_NOTIFY(...) [DC3] | MILO_WARN | MILO_LOG

MILO_NOTIFY_ONCE expands to a brace-block statement; swap targets that
are also single statements (MILO_LOG/WARN/NOTIFY/FAIL) substitute cleanly
because the original call site already ends in `;`.

Detection signals:
    - Insert/delete clusters near string references
    - Scope counter mismatches (NOTIFY_ONCE static guard)
    - Extra/missing branch instructions
"""

from __future__ import annotations

import re
from functools import lru_cache
from pathlib import Path
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Candidate swap targets per source macro. Targets are filtered at
# generate() time against the macros actually defined in this project's
# Debug.h (see _available_macros).
_LOG_MACROS: dict[bytes, list[bytes]] = {
    b"MILO_WARN": [b"MILO_LOG", b"MILO_NOTIFY"],
    b"MILO_NOTIFY": [b"MILO_WARN", b"MILO_LOG"],
    b"MILO_LOG": [b"MILO_WARN", b"MILO_NOTIFY"],
    b"MILO_FAIL": [b"MILO_WARN", b"MILO_LOG"],
    b"MILO_NOTIFY_ONCE": [b"MILO_NOTIFY", b"MILO_LOG", b"MILO_WARN"],
}

_ALL_MACROS = set(_LOG_MACROS.keys())

# Macros that should always be considered "available" if anything is —
# they are part of every Milo Debug.h variant we've seen and the probe
# falling back to this set keeps the pattern functional even when the
# header lookup fails (e.g. unusual project layout).
_FALLBACK_AVAILABLE = frozenset({b"MILO_LOG", b"MILO_WARN", b"MILO_FAIL"})

_MACRO_DEFINE_RE = re.compile(
    rb"^\s*#\s*define\s+(MILO_[A-Z_]+)\b", re.MULTILINE
)


def _project_root_for(path: Path) -> Path:
    resolved = path.resolve()
    for candidate in (resolved.parent, *resolved.parents):
        if (candidate / ".git").exists():
            return candidate
    return resolved.parent


@lru_cache(maxsize=8)
def _available_macros_cached(debug_h: Path) -> frozenset[bytes]:
    try:
        text = debug_h.read_bytes()
    except OSError:
        return frozenset()
    return frozenset(_MACRO_DEFINE_RE.findall(text))


def _available_macros(source_path: Path) -> frozenset[bytes]:
    """Return MILO_* macro names #define'd in the project's Debug.h.

    Looks for `<project_root>/src/system/os/Debug.h`. Returns a permissive
    fallback set (LOG/WARN/FAIL) if the file can't be found or read so the
    pattern still emits *something* in unusual layouts.
    """
    root = _project_root_for(source_path)
    debug_h = root / "src" / "system" / "os" / "Debug.h"
    found = _available_macros_cached(debug_h)
    if not found:
        return _FALLBACK_AVAILABLE
    return found


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

        # Insert/delete differences (check via clusters)
        if any(c.inserts > 0 or c.deletes > 0 for c in diagnosis.clusters):
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        # Low priority — log macro swaps are rare fixes
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        available = _available_macros(ctx.file_path)

        # Find all MILO_* macro call sites
        macro_sites = _find_milo_macros(body, source)

        for call_node, macro_name, start, end in macro_sites:
            if counter >= 8:
                break

            # Filter swap targets to macros that are actually defined in
            # this project — emitting an undefined macro guarantees a
            # compile failure (the historical 13/13 failure mode).
            targets = [
                t for t in _LOG_MACROS.get(macro_name, []) if t in available
            ]
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
