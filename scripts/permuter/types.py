"""Shared dataclasses for the permuter."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from tree_sitter import Node


@dataclass
class SwapInfo:
    """Register swap pair information."""

    count: int
    first_idx: int
    last_idx: int


@dataclass
class DiffOp:
    """An opcode mismatch between target and base."""

    index: int
    target_opcode: str
    base_opcode: str


@dataclass
class Cluster:
    """A contiguous group of insert/delete instructions."""

    start_idx: int
    end_idx: int
    size: int
    inserts: int
    deletes: int


@dataclass
class Diagnosis:
    """Structured diagnosis of objdiff mismatches."""

    total_instructions: int
    match_counts: dict[str, int]
    reg_swap_pairs: dict[tuple[str, str], SwapInfo]
    offset_deltas: dict[int, int]
    diff_ops: list[DiffOp]
    clusters: list[Cluster]
    noise_explained: int
    noise_total: int


@dataclass
class FunctionContext:
    """Parsed function ready for pattern application."""

    file_path: Path
    file_source: bytes  # Full file content
    func_node: Node  # tree-sitter function_definition node
    body_node: Node  # The compound_statement body
    statements: list[Node]  # Top-level named children in the body
    func_byte_range: tuple[int, int]  # (start_byte, end_byte)
    diagnosis: Optional[Diagnosis] = None

    def source_text(self, node: Node) -> str:
        """Extract source text for a tree-sitter node."""
        return self.file_source[node.start_byte : node.end_byte].decode("utf-8")


@dataclass
class Variant:
    """A source variation to test."""

    name: str
    pattern_name: str
    description: str
    source: bytes  # Full modified file content


@dataclass
class ScoreResult:
    """Result of building and scoring a variant."""

    variant: Variant
    match_percent: float
    build_success: bool
    error: Optional[str] = None
    execution_equivalent: Optional[bool] = None
