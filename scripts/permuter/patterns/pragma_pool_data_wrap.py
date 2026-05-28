"""pragma_pool_data_wrap — bracket a function in ``#pragma pool_data off``.

When MWCC pools several BSS/static addresses into a callee-saved register
(typically ``r29``/``r30``/``r31``) at function entry, our base often loads
one MORE callee-saved than the target does, cascading into a wider
prologue and an r-pair regswap throughout the body.  Wrapping the function
with ``#pragma pool_data off`` / ``#pragma pool_data reset`` disables the
pooling pass for the enclosed function and drops the extra preload.

Wins on record (`feedback_pragma_pool_data_off.md`):
    * ``CacheWii::WriteAsync`` 85.5% -> 100% (drops one __savegpr_ slot).

Inverse polarity (`feedback_customizepanel_rotatepatch_pool.md`):
    Inside an outer ``pool_data off`` block, individual functions that use
    each pooled constant exactly once want ``#pragma pool_data on`` (so
    each constant addresses its own weak symbol).  We model that as a
    second variant.

Trigger (AST):
    Any function definition whose preceding line(s) do NOT already include
    a ``#pragma pool_data`` directive.  We emit two variants per call: one
    wrapping in ``off``/``reset`` and one wrapping in ``on``/``reset``.

Gating:
    Set ``opt_in = True``.  Wrapping is a known structural fix used
    surgically; it can regress sibling functions in the same TU (callees
    that legitimately share the pool).  ``pattern_scan`` and explicit
    ``--patterns pragma_pool_data_wrap`` are the intended entry points,
    not default batch sweeps.

Safety:
    The wrap only affects the bracketed function thanks to the matching
    ``reset`` directive.  No semantic changes — pure codegen control.
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Where ``#pragma pool_data`` already appears earlier in the file (anywhere
# in the few thousand bytes leading up to the function), bail — the function
# is already covered by an outer block and re-wrapping is almost always a
# regression.  We deliberately scan only a bounded window so a pragma at the
# very top of a huge TU doesn't suppress every function in the file.
_PRAGMA_SCAN_WINDOW = 4096
_PRAGMA_RE = re.compile(rb"#\s*pragma\s+pool_data\s+(off|on|reset)\b")

# Callee-saved GPRs (used by the asm-signal heuristic).
_CALLEE_SAVED_GPRS = frozenset(
    {f"r{n}" for n in range(13, 32)}
)


class PragmaPoolDataWrapPattern(Pattern):
    """Wrap a function in ``#pragma pool_data off`` / ``reset``."""

    name = "pragma_pool_data_wrap"
    opt_in = True  # Surgical fix — only run when explicitly requested.
    safety_tier = "aggressive"
    structural_domain = "codegen_control"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Strongest hint: prologue GPR-save delta (target saves fewer regs).
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            return True
        # Soft hint: any addi/lis cluster suggesting BSS-base pooling
        # contention.  Cheap accept here because the pattern is opt-in.
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("addi", "addis", "lis") or d.base_opcode in ("addi", "addis", "lis"):
                return True
        if diagnosis.clusters:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Prologue delta in target's favor is the strongest signal we have.
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            return 0.8
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        func_start = ctx.func_node.start_byte
        func_end = ctx.func_node.end_byte

        # Bail if there's already a pragma pool_data directive in the window
        # of source immediately preceding the function.
        scan_start = max(0, func_start - _PRAGMA_SCAN_WINDOW)
        preamble = source[scan_start:func_start]
        if _PRAGMA_RE.search(preamble):
            return

        # The insertion happens AT the start of the line that the function
        # signature occupies, so the pragma lives on its own line.
        line_start = func_start
        while line_start > 0 and source[line_start - 1:line_start] != b"\n":
            line_start -= 1
        # The reset directive goes on a new line right after the closing
        # brace of the function body.
        # Find the byte position immediately after the function definition's
        # trailing newline (if any).
        tail = func_end
        if tail < len(source) and source[tail:tail + 1] == b"\n":
            tail += 1

        for polarity, label in ((b"off", "off"), (b"on", "on")):
            ed = SourceEditor(source)
            ed.insert_at(
                line_start,
                b"#pragma pool_data " + polarity + b"\n",
            )
            ed.insert_at(
                tail,
                b"#pragma pool_data reset\n",
            )
            try:
                new_source = ed.apply()
            except ValueError:
                continue
            yield Variant(
                name=f"pool_data_{label}",
                pattern_name="pragma_pool_data_wrap",
                description=(
                    f"Wrap function in #pragma pool_data {label} / reset"
                ),
                source=new_source,
                tags=frozenset({"pragma_pool_data_wrap", label}),
            )
