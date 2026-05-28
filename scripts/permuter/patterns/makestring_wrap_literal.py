"""Wrap bare string literals in `TheDebug << "..."` with `MakeString(...)`.

Win rate: untested (new pattern; proven on RndText::UpdateScrolling 69.5->71.9% in
one edit — see `feedback_makestring_inline_constant.md` in MEMORY).

When the target prologue allocates a FormatString-shaped stack slot (~0x82C
bytes — the inline `MakeString(const char *)` body contains a `FormatString`
which holds `char mFmtBuf[2048]` plus a handful of pointers) and our build
doesn't, the target source called `MakeString("literal")` for one of its
debug-only writes. Wrapping a bare literal in `MakeString()` instantiates the
inline body in the caller, growing the caller frame by ~0x82C bytes to match.

All multi-arg `MakeString` templates are NOT marked inline; with
`-inline noauto` they are emitted as separate `bl MakeString<...>` weak
symbols and FormatString lives inside the callee. Only the single-arg form
`inline const char *MakeString(const char *c)` in `utl/MakeString.h` is
unconditionally inlined.

Transform: `TheDebug << "abc"` -> `TheDebug << MakeString("abc")` (per call
site, one variant per site, capped at 8 variants per function).

Safety: pure no-op at runtime (single-arg MakeString returns its argument
unchanged); only the caller's stack-frame layout changes.

Detection signals (relevant):
  - Stack-frame size short by ~0x800-0x870 bytes (replace_real on `stwu r1`
    / `addi r1, r1, ...` prologue/epilogue), OR
  - target_facts mentions a missing FormatString slot, OR
  - Generic fallback: the source contains at least one `TheDebug << "..."`
    line and the function is below 100% — there are only ~30 such sites
    repo-wide, so bounded.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Stream-debug receivers we recognise as "MakeString site" entrypoints.
# Conservative: only `TheDebug << "lit"` for now. The MEMORY note explicitly
# scopes this pattern to TheDebug; other Hmx debug families (`TheLog`, etc.)
# may have different prologue shapes.
_DEBUG_RECEIVERS = frozenset({b"TheDebug"})

# Approximate FormatString size: char mFmtBuf[2048] + a handful of pointer
# fields. The MEMORY note observed a 0x82C delta on RndText::UpdateScrolling;
# we accept anything in the broader 0x7F0-0x880 window to absorb alignment
# padding and other small locals.
_FRAME_SLOT_MIN = 0x7F0
_FRAME_SLOT_MAX = 0x880


class MakeStringWrapLiteralPattern(Pattern):
    name = "makestring_wrap_literal"
    safety_tier = "safe"
    structural_domain = "stack_frame"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Strongest signal: a prologue/epilogue replace on stwu/addi r1 whose
        # immediate looks like the FormatString stack slot.
        if _has_frame_slot_signal(diagnosis):
            return True
        # Fallback: target_facts may carry a normalized stack-slot delta.
        facts = getattr(diagnosis, "target_facts", None)
        delta = _frame_delta_from_facts(facts)
        if delta is not None and _FRAME_SLOT_MIN <= delta <= _FRAME_SLOT_MAX:
            return True
        # Weak fallback: any structural replace + diff_ops on stack-pointer
        # arithmetic still hints at a frame-size mismatch.
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("stwu", "addi", "subi") and d.target_opcode == d.base_opcode:
                # Same opcode, different immediate — likely a frame-size diff.
                return True
        # Repo-wide there are only ~30 candidate sites; if relevance signals
        # aren't conclusive, leave gating to context_priority where the AST
        # check decides cheaply.
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if _has_frame_slot_signal(diagnosis):
            return 0.7
        facts = getattr(diagnosis, "target_facts", None)
        delta = _frame_delta_from_facts(facts)
        if delta is not None and _FRAME_SLOT_MIN <= delta <= _FRAME_SLOT_MAX:
            return 0.7
        return 0.2 if self.relevant(diagnosis) else 0.0

    def context_priority(
        self, diagnosis: Diagnosis, ctx: FunctionContext
    ) -> float:
        """Skip entirely if no candidate `TheDebug << "lit"` site exists."""
        base = self.priority(diagnosis)
        if not _has_candidate_site(ctx):
            return 0.0
        # If diagnosis was unsure but the AST has a candidate, give it a
        # small floor so the budget allocator still tries it.
        return max(base, 0.2)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0
        seen_byte_ranges: set[tuple[int, int]] = set()

        for stmt in ctx.statements:
            if counter >= 8:
                break
            for node in walk(stmt):
                if counter >= 8:
                    break
                if node.type != "binary_expression":
                    continue
                hit = _match_debug_literal(node, source)
                if hit is None:
                    continue
                literal_node = hit
                key = (literal_node.start_byte, literal_node.end_byte)
                if key in seen_byte_ranges:
                    continue
                seen_byte_ranges.add(key)

                # Region filter: skip sites the attribution pipeline says
                # aren't in a mismatch region (avoids wasting variants on
                # already-matching call sites).
                if not ctx.node_in_mismatch_region(literal_node):
                    continue

                literal_text = source[literal_node.start_byte:literal_node.end_byte]
                ed = SourceEditor(source)
                ed.replace_node(
                    literal_node,
                    b"MakeString(" + literal_text + b")",
                )

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                preview = literal_text[:32].decode("utf-8", errors="replace")
                yield Variant(
                    name=f"makestr_{counter}",
                    pattern_name=self.name,
                    description=f"Wrap TheDebug literal in MakeString(): {preview}",
                    source=new_source,
                )
                counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _match_debug_literal(node: Node, source: bytes) -> Node | None:
    """If `node` is `<receiver_chain> << "literal"`, return the literal node.

    Accepts the left-associative chain shape produced by tree-sitter for
    `TheDebug << x << "lit" << y`. The check requires:
      - `node.type == "binary_expression"` with operator `<<`
      - right operand is a `string_literal`
      - the chain's left-most leaf identifier is in `_DEBUG_RECEIVERS`
    """
    op = node.child_by_field_name("operator")
    if op is None:
        return None
    if source[op.start_byte:op.end_byte] != b"<<":
        return None
    left = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    if left is None or right is None:
        return None
    if right.type != "string_literal":
        return None
    if not _chain_root_is_debug(left, source):
        return None
    return right


def _chain_root_is_debug(node: Node, source: bytes) -> bool:
    """Walk down the left-associative `<<` chain to its left-most identifier."""
    current = node
    # Allow up to a small depth — production debug streams chain dozens of
    # `<<`; cap at 32 so a pathological AST can't spin.
    for _ in range(32):
        if current.type != "binary_expression":
            break
        inner_op = current.child_by_field_name("operator")
        if inner_op is None or source[inner_op.start_byte:inner_op.end_byte] != b"<<":
            break
        left = current.child_by_field_name("left")
        if left is None:
            break
        current = left
    if current.type != "identifier":
        return False
    return source[current.start_byte:current.end_byte] in _DEBUG_RECEIVERS


def _has_candidate_site(ctx: FunctionContext) -> bool:
    """Fast pre-check: does the function body contain at least one candidate?"""
    body = ctx.body_node
    source = ctx.file_source
    for node in walk(body):
        if node.type != "binary_expression":
            continue
        if _match_debug_literal(node, source) is not None:
            return True
    return False


def _has_frame_slot_signal(diagnosis: Diagnosis) -> bool:
    """True when diff_ops show a stack-frame-size delta of ~0x82C."""
    for d in diagnosis.diff_ops:
        if d.target_opcode not in ("stwu", "addi", "subi"):
            continue
        if d.target_opcode != d.base_opcode:
            continue
        t_imm = _extract_immediate(d.target_arg)
        b_imm = _extract_immediate(d.base_arg)
        if t_imm is None or b_imm is None:
            continue
        delta = abs(abs(t_imm) - abs(b_imm))
        if _FRAME_SLOT_MIN <= delta <= _FRAME_SLOT_MAX:
            return True
    return False


def _extract_immediate(arg: str) -> int | None:
    """Pull a signed int out of an objdiff arg string like `r1, r1, -0x830`."""
    if not arg:
        return None
    # Try the rightmost token first (immediate operand position).
    for token in reversed(arg.replace(",", " ").split()):
        token = token.strip()
        if not token:
            continue
        try:
            if token.startswith(("0x", "-0x", "+0x")):
                return int(token, 16)
            return int(token, 10)
        except ValueError:
            continue
    return None


def _frame_delta_from_facts(facts: object | None) -> int | None:
    """Best-effort: pull a frame-size delta off a TargetFacts-shaped object.

    TargetFacts schema isn't load-bearing for this pattern — we only consult
    it opportunistically. Returns the absolute delta if visible, else None.
    """
    if facts is None:
        return None
    for attr in ("frame_size_delta", "stack_frame_delta", "target_frame_delta"):
        value = getattr(facts, attr, None)
        if isinstance(value, (int, float)):
            return abs(int(value))
    return None
