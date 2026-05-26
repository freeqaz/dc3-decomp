"""Slot pad pattern — insert a function-top dummy local to shift slot allocations.

Companion to `scope_widening`. When a function has an OFFSET_SWAP (two locals
of same type at mirror-paired stack offsets), MWCC's slot allocator may pick
the inverted order vs the target. Inserting a dummy local at function top
can shift slot allocations enough to break the inversion.

Strategies tried (one variant each):
- `char _pad[N];` — claims an N-byte slot. N derived from `dominant_slot_pair`.
- `volatile int _vk = 0;` — volatile prevents DCE, claims 4 bytes + may force
  callee-saved promotion.
- A typed dummy matching the suspected slot-inverted type. Without knowing
  the type from diagnose data, this falls back to `int _dummy = 0;`.

Trade-off: every variant grows the stack frame. If frame growth costs more
than the slot-inversion fix saves, the scorer will reject it.

Safety: dummy is named with leading underscore to avoid colliding with
user-named locals.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant


_DUMMY_TEMPLATES = (
    # (label, source line to insert)
    ("char_pad", "    char _slotpad[{n}]; (void)_slotpad;\n"),
    ("volatile_int", "    volatile int _slotpad = 0; (void)_slotpad;\n"),
)


class SlotPadPattern(Pattern):
    name = "slot_pad"
    safety_tier = "moderate"
    structural_domain = "data_flow"
    follow_ups = ("declaration_reorder", "scope_widening")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Only relevant when there's a meaningful offset-swap cascade.
        return getattr(diagnosis, "offset_swap_count", 0) > 20

    def priority(self, diagnosis: Diagnosis) -> float:
        cnt = getattr(diagnosis, "offset_swap_count", 0)
        if cnt > 100:
            return 0.7
        if cnt > 20:
            return 0.4
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        # Derive pad size from dominant slot-pair magnitude.
        dom = getattr(ctx, "diagnosis", None)
        pad_size = 96  # default: sizeof(Line) for WrapText
        if dom is not None:
            pair = getattr(dom, "dominant_slot_pair", None)
            if pair is not None:
                pad_size = pair[0]

        body_node = ctx.body_node
        if body_node.type != "compound_statement":
            return
        # Insertion point: right after the opening brace of the function body.
        insert_pos = body_node.start_byte + 1
        source = ctx.file_source
        if insert_pos < len(source) and source[insert_pos:insert_pos + 1] == b"\n":
            insert_pos += 1

        for label, template in _DUMMY_TEMPLATES:
            line = template.format(n=pad_size).encode()
            new_source = (
                source[:insert_pos]
                + line
                + source[insert_pos:]
            )
            yield Variant(
                name=f"slot_pad_{label}",
                pattern_name=self.name,
                description=(
                    f"Insert {label} dummy ({pad_size}b) at function top "
                    f"to shift slot allocation"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=ctx.file_source,
                tags=frozenset({"slot_pad", label}),
            )
