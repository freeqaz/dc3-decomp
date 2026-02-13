"""SourceEditor — accumulate byte-level edits and apply them in one pass.

Collects replace, insert, delete, and swap operations on tree-sitter nodes
or raw byte ranges, then applies them all at once (in reverse order) to
produce a new source bytes object.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol, runtime_checkable


@runtime_checkable
class _HasByteRange(Protocol):
    """Duck-type for tree-sitter Node (start_byte / end_byte)."""

    @property
    def start_byte(self) -> int: ...

    @property
    def end_byte(self) -> int: ...


@dataclass
class _Edit:
    start: int
    end: int  # exclusive
    replacement: bytes


class SourceEditor:
    """Accumulate edits against a source buffer and apply them atomically."""

    def __init__(self, source: bytes) -> None:
        self._source = source
        self._edits: list[_Edit] = []

    # -- node-level operations ------------------------------------------------

    def replace_node(self, node: _HasByteRange, replacement: bytes) -> None:
        """Replace a node's byte range with *replacement*."""
        self._edits.append(_Edit(node.start_byte, node.end_byte, replacement))

    def insert_before(self, node: _HasByteRange, text: bytes) -> None:
        """Insert *text* immediately before *node*."""
        self._edits.append(_Edit(node.start_byte, node.start_byte, text))

    def insert_after(self, node: _HasByteRange, text: bytes) -> None:
        """Insert *text* immediately after *node*."""
        self._edits.append(_Edit(node.end_byte, node.end_byte, text))

    def delete_node(self, node: _HasByteRange) -> None:
        """Delete the bytes covered by *node*."""
        self._edits.append(_Edit(node.start_byte, node.end_byte, b""))

    def swap_nodes(self, a: _HasByteRange, b: _HasByteRange) -> None:
        """Swap the byte content of two non-overlapping nodes.

        Nodes are sorted internally by start_byte — callers don't need to
        know which comes first in the source.
        """
        if a.start_byte > b.start_byte:
            a, b = b, a
        a_text = self._source[a.start_byte:a.end_byte]
        b_text = self._source[b.start_byte:b.end_byte]
        self._edits.append(_Edit(a.start_byte, a.end_byte, b_text))
        self._edits.append(_Edit(b.start_byte, b.end_byte, a_text))

    # -- range-level operations -----------------------------------------------

    def replace_range(self, start: int, end: int, replacement: bytes) -> None:
        """Replace bytes in [start, end) with *replacement*."""
        self._edits.append(_Edit(start, end, replacement))

    def insert_at(self, offset: int, text: bytes) -> None:
        """Insert *text* at an arbitrary byte offset (zero-width)."""
        self._edits.append(_Edit(offset, offset, text))

    def delete_range(self, start: int, end: int) -> None:
        """Delete bytes in [start, end)."""
        self._edits.append(_Edit(start, end, b""))

    # -- apply ----------------------------------------------------------------

    def apply(self) -> bytes:
        """Apply all accumulated edits and return the new source.

        Raises ValueError if any edits overlap (except zero-width inserts at
        the same offset, which are allowed).
        """
        if not self._edits:
            return self._source

        # Sort by start descending, then by end descending (so wider edits
        # come first at the same start — catches overlaps).
        edits = sorted(self._edits, key=lambda e: (e.start, e.end), reverse=True)

        # Validate no overlaps: after sorting descending by start, each
        # edit's start must be >= the next edit's end.
        for i in range(len(edits) - 1):
            cur = edits[i]
            nxt = edits[i + 1]
            # Zero-width inserts at the same offset are fine
            if cur.start == cur.end and nxt.start == nxt.end and cur.start == nxt.start:
                continue
            if cur.start < nxt.end:
                raise ValueError(
                    f"Overlapping edits: [{nxt.start}:{nxt.end}) and [{cur.start}:{cur.end})"
                )

        result = self._source
        for edit in edits:
            result = result[:edit.start] + edit.replacement + result[edit.end:]
        return result
