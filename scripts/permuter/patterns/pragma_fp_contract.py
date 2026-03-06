"""Pragma fp_contract pattern — insert/remove #pragma fp_contract(off).

The Xbox 360 compiler generates fused multiply-add instructions (fmadds,
fmsubs, fnmadds, fnmsubs) when #pragma fp_contract is ON (the default).
Adding #pragma fp_contract(off) before a function prevents FMA fusion,
generating separate fmuls+fadds instead.

Example:
    void func() { float r = a * b + c; }
    ->
    #pragma fp_contract(off)
    void func() { float r = a * b + c; }
"""

from __future__ import annotations

import re
from typing import Iterator

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant

_FMA_OPS = {"fmadds", "fmsubs", "fnmadds", "fnmsubs", "fmadd", "fmsub", "fnmadd", "fnmsub"}
_SEPARATE_OPS = {"fmuls", "fadds", "fsubs", "fmul", "fadd", "fsub"}

_PRAGMA_RE = re.compile(
    rb"^[ \t]*#\s*pragma\s+fp_contract\s*\(\s*(on|off)\s*\)\s*$",
    re.MULTILINE,
)


class PragmaFpContractPattern(Pattern):
    name = "pragma_fp_contract"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FMA_OPS or d.base_opcode in _FMA_OPS:
                return True
            if d.target_opcode in _SEPARATE_OPS or d.base_opcode in _SEPARATE_OPS:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: FMA op in one side, separate ops in the other
        has_fma = any(
            d.target_opcode in _FMA_OPS or d.base_opcode in _FMA_OPS
            for d in diagnosis.diff_ops
        )
        has_separate = any(
            d.target_opcode in _SEPARATE_OPS or d.base_opcode in _SEPARATE_OPS
            for d in diagnosis.diff_ops
        )
        if has_fma and has_separate:
            return 0.9  # classic fp_contract mismatch
        if has_fma:
            return 0.6
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        func_start = ctx.func_node.start_byte

        # Find existing pragma before the function definition
        existing_pragma = _find_pragma_before(source, func_start)

        if existing_pragma is not None:
            # Direction: remove existing pragma
            pstart, pend = existing_pragma
            # Remove the pragma line (including trailing newline)
            end = pend
            if end < len(source) and source[end:end + 1] == b"\n":
                end += 1
            new_source = source[:pstart] + source[end:]
            yield Variant(
                name="fppragma_0",
                pattern_name="pragma_fp_contract",
                description="Remove #pragma fp_contract before function",
                source=new_source,
            )

            # Direction: flip pragma (off -> on, on -> off)
            match = _PRAGMA_RE.search(source[pstart:pend])
            if match:
                current = match.group(1)
                flipped = b"on" if current == b"off" else b"off"
                pragma_line = b"#pragma fp_contract(" + flipped + b")"
                new_source = source[:pstart] + pragma_line + source[pend:]
                yield Variant(
                    name="fppragma_1",
                    pattern_name="pragma_fp_contract",
                    description=f"Flip #pragma fp_contract to {flipped.decode()}",
                    source=new_source,
                )
        else:
            # Direction: insert #pragma fp_contract(off) before function
            # Find the start of the line containing the function definition
            line_start = source.rfind(b"\n", 0, func_start)
            insert_pos = line_start + 1 if line_start >= 0 else 0

            pragma_line = b"#pragma fp_contract(off)\n"
            new_source = source[:insert_pos] + pragma_line + source[insert_pos:]
            yield Variant(
                name="fppragma_0",
                pattern_name="pragma_fp_contract",
                description="Insert #pragma fp_contract(off) before function",
                source=new_source,
            )

            # Also try inserting fp_contract(on) explicitly
            pragma_line_on = b"#pragma fp_contract(on)\n"
            new_source_on = source[:insert_pos] + pragma_line_on + source[insert_pos:]
            yield Variant(
                name="fppragma_1",
                pattern_name="pragma_fp_contract",
                description="Insert #pragma fp_contract(on) before function",
                source=new_source_on,
            )


def _find_pragma_before(source: bytes, func_start: int) -> tuple[int, int] | None:
    """Find a #pragma fp_contract line immediately before the function.

    Searches backwards from func_start, skipping blank lines and comments.
    Returns (start_byte, end_byte) of the pragma line, or None.
    """
    # Search in the region before the function (up to 500 bytes back)
    region_start = max(0, func_start - 500)
    region = source[region_start:func_start]

    # Find all pragma matches in the region
    last_match = None
    for m in _PRAGMA_RE.finditer(region):
        last_match = m

    if last_match is None:
        return None

    # Check that only whitespace/comments exist between pragma and function
    between = region[last_match.end():]
    stripped = between.strip()
    if stripped and not stripped.startswith(b"//") and not stripped.startswith(b"/*"):
        # There's non-whitespace/comment content between pragma and function
        # Check each line
        for line in between.split(b"\n"):
            line = line.strip()
            if line and not line.startswith(b"//") and not line.startswith(b"/*"):
                return None

    return (region_start + last_match.start(), region_start + last_match.end())
