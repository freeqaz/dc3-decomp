"""Bit-pack OR chain reorder — sort `A | B | C | ...` terms by descending shift.

When a function packs multiple bit-fields into a single integer through a
chain of `|` operations, MWCC schedules the OR-chain in source order and the
resulting register allocation is sensitive to which shifted term comes first.
Low-to-high order forces a `clrlslwi` per field; high-bit-first order lets
MWCC emit a cleaner `slwi` + `rlwimi` sequence that matches the typical target.

This pattern finds top-level `|` chains containing at least two terms with
left-shift (`<<`) operators and yields variants whose terms are sorted by
descending shift amount (highest bit first), with non-shifted terms placed
last. Terms without obvious shift constants keep their relative order.

Example:
    // Mismatches — low-to-high evaluation forces clrlslwi per field:
    unsigned int packed = (elapsed | ((but << 23) & 0x0F800000)) |
                          (((pad << 28) & 0x70000000) | (state << 31));

    // Matches — high-to-low, MWCC emits slwi+rlwimi:
    unsigned int packed = (state << 31) | ((pad << 28) & 0x70000000) |
                          ((but << 23) & 0x0F800000) | elapsed;

Real-world win: `UIStats::EventLog` 97.9 -> 99.8% (+1.9pp) after sorting
the four-term OR-chain high-to-low, which collapsed an r28<->r31 cascade.
"""

from __future__ import annotations

import re
from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant


_SHIFT_AMOUNT_RE = re.compile(rb"<<\s*(0[xX][0-9a-fA-F]+|\d+)")

# Matches the LHS-identifier of a `<<` expression. Captures a simple
# identifier (possibly member-access like `obj.field` / `a->b`).
_SHIFT_LHS_RE = re.compile(
    rb"([A-Za-z_][\w.\->]*)\s*<<\s*(?:0[xX][0-9a-fA-F]+|\d+)"
)

# Codegen opcodes that suggest a bit-pack reorder may be useful.
_BITPACK_OPCODES = {
    "rlwimi", "rlwinm", "rlwnm",
    "slwi", "slw", "srwi", "srw",
    "clrlwi", "clrlslwi", "clrrwi",
    "or", "ori", "oris",
}


class BitPackOrReorderPattern(Pattern):
    """Reorder OR-chain terms by descending shift amount (high bits first)."""

    name = "bitpack_or_reorder"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if not diagnosis.diff_ops and not diagnosis.clusters:
            return False
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BITPACK_OPCODES or d.base_opcode in _BITPACK_OPCODES:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strongest signal: target uses rlwimi (canonical bit-pack instr)
        for d in diagnosis.diff_ops:
            if d.target_opcode == "rlwimi" or d.base_opcode == "rlwimi":
                return 0.6
        # Weaker: shift/or mismatches present
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BITPACK_OPCODES or d.base_opcode in _BITPACK_OPCODES:
                return 0.3
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source
        seen_chains: set[tuple[int, int]] = set()

        for stmt in ctx.statements:
            for node in walk(stmt):
                if node.type != "binary_expression":
                    continue
                if not _is_top_of_or_chain(node):
                    continue

                terms = _collect_or_chain(node)
                if terms is None or len(terms) < 2:
                    continue

                # Dedup by chain byte range — the walk yields nested binary_expression
                # nodes; `_is_top_of_or_chain` already filters, but guard anyway.
                key = (node.start_byte, node.end_byte)
                if key in seen_chains:
                    continue
                seen_chains.add(key)

                shift_terms = [t for t in terms if _shift_amount(t, source) is not None]
                if len(shift_terms) < 2:
                    continue

                # Guard: skip broadcast-byte chains where every `<<` operand
                # is the same identifier (e.g. `(b<<24)|(b<<16)|(b<<8)|b`).
                # Per feedback_bitpack_or_high_to_low.md this pattern caused
                # a proven regression on Spotlight::BuildNGSheet — reordering
                # is meaningless when all shifted operands are the same value.
                if _is_broadcast_byte_chain(shift_terms, source):
                    continue

                reordered = _sort_terms_high_to_low(terms, source)
                already_sorted = reordered is None
                if already_sorted:
                    reordered = terms

                original_chain = source[node.start_byte:node.end_byte]
                new_chain = _build_or_chain(reordered, source)

                if not already_sorted and new_chain != original_chain:
                    new_source = (
                        source[:node.start_byte]
                        + new_chain
                        + source[node.end_byte:]
                    )
                    yield Variant(
                        name=f"bitpack_or_{counter}",
                        pattern_name=self.name,
                        description=(
                            f"Reorder OR-chain high-to-low "
                            f"({len(shift_terms)} shifted terms)"
                        ),
                        source=new_source,
                    )
                    counter += 1

                # Also yield the inverse (low-to-high) so the pattern can hunt
                # in either direction — useful when the target uses the opposite
                # order from the current source.
                reversed_chain = _build_or_chain(list(reversed(reordered)), source)
                if reversed_chain != original_chain:
                    new_source = (
                        source[:node.start_byte]
                        + reversed_chain
                        + source[node.end_byte:]
                    )
                    yield Variant(
                        name=f"bitpack_or_{counter}",
                        pattern_name=self.name,
                        description=(
                            f"Reorder OR-chain low-to-high "
                            f"({len(shift_terms)} shifted terms)"
                        ),
                        source=new_source,
                    )
                    counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _is_or_binary(node: Node) -> bool:
    if node.type != "binary_expression":
        return False
    op = node.child_by_field_name("operator")
    return op is not None and op.text == b"|"


def _is_top_of_or_chain(node: Node) -> bool:
    """True when `node` is a `|` chain whose parent is not also `|`."""
    if not _is_or_binary(node):
        return False
    parent = node.parent
    while parent is not None and parent.type == "parenthesized_expression":
        parent = parent.parent
    if parent is None:
        return True
    return not _is_or_binary(parent)


def _collect_or_chain(node: Node) -> Optional[list[Node]]:
    """Flatten a left-/right-nested `|` chain into ordered leaf terms."""
    if not _is_or_binary(node):
        return None

    left = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    if left is None or right is None:
        return None

    terms: list[Node] = []
    for side in (left, right):
        unwrapped = _unwrap_parens(side)
        sub_chain = _collect_or_chain(unwrapped)
        if sub_chain is not None:
            terms.extend(sub_chain)
        else:
            terms.append(side)
    return terms


def _unwrap_parens(node: Node) -> Node:
    """Strip parenthesized_expression wrappers to expose the inner expression."""
    current = node
    while current.type == "parenthesized_expression":
        inner = None
        for child in current.named_children:
            if child.type != "comment":
                inner = child
                break
        if inner is None:
            return current
        current = inner
    return current


def _is_broadcast_byte_chain(shift_terms: list[Node], source: bytes) -> bool:
    """Return True when every shifted term has the same LHS identifier.

    Per feedback_bitpack_or_high_to_low.md, broadcast-byte chains like
    ``(b<<24) | (b<<16) | (b<<8) | b`` are NOT reorderable — every operand
    of `<<` is the same source value, so high-to-low order does nothing
    useful and the pattern has been observed to regress codegen.

    Implementation: collect the LHS identifier of each `<<` in each term
    via regex. If every term has the same identifier (and there are >=2
    such terms), this is a broadcast.
    """
    if len(shift_terms) < 2:
        return False
    identifiers: set[bytes] = set()
    for term in shift_terms:
        text = source[term.start_byte:term.end_byte]
        match = _SHIFT_LHS_RE.search(text)
        if match is None:
            return False  # Can't be sure — bail out (don't broadcast-skip)
        identifiers.add(match.group(1))
        if len(identifiers) > 1:
            return False
    return len(identifiers) == 1


def _shift_amount(node: Node, source: bytes) -> Optional[int]:
    """Find the largest `<< K` constant in a term's source text.

    Operates on text to keep the heuristic robust against parenthesization
    and bit-masking (`(x << 28) & 0xMASK`). Returns None when no shift is
    found.
    """
    text = source[node.start_byte:node.end_byte]
    best: Optional[int] = None
    for match in _SHIFT_AMOUNT_RE.finditer(text):
        raw = match.group(1)
        try:
            amount = int(raw, 0)
        except ValueError:
            continue
        if best is None or amount > best:
            best = amount
    return best


def _sort_terms_high_to_low(terms: list[Node], source: bytes) -> Optional[list[Node]]:
    """Return `terms` sorted by descending shift amount; unshifted terms last.

    Stable: terms with equal (or absent) shifts keep their relative order.
    Returns None when nothing would change.
    """
    indexed = list(enumerate(terms))
    # Sort key: (no_shift_flag, -shift_amount, original_index)
    # no_shift_flag pushes unshifted terms to the end.
    def key(item: tuple[int, Node]) -> tuple[int, int, int]:
        idx, t = item
        amount = _shift_amount(t, source)
        if amount is None:
            return (1, 0, idx)
        return (0, -amount, idx)

    ordered = sorted(indexed, key=key)
    if [i for i, _ in ordered] == list(range(len(terms))):
        return None
    return [t for _, t in ordered]


def _build_or_chain(terms: list[Node], source: bytes) -> bytes:
    """Render an OR-chain from leaf nodes, preserving each term's own parens."""
    pieces = [source[t.start_byte:t.end_byte] for t in terms]
    return b" | ".join(pieces)
