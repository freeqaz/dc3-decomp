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
    replace_noise: int = 0   # Symbol-reloc replaces (unfixable)
    replace_real: int = 0    # Real structural replaces (actionable)


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


@dataclass
class TriageResult:
    """Classification of a single function's mismatch profile."""

    symbol: str
    demangled: str
    unit: str
    source_path: str
    qualified_name: str
    current_percent: float
    category: str  # REGSWAP_ONLY, REGSWAP_PLUS, STRUCTURAL, NOISE_ONLY, UNFIXABLE, MIXED
    gpr_swap_pairs: list  # [{pair: [rA, rB], count: N}, ...]
    diff_op_count: int
    cluster_count: int
    total_instructions: int
    error: Optional[str] = None


@dataclass
class RoundResult:
    """Result of a single hill-climbing round."""

    round_num: int
    baseline: float
    best_name: Optional[str]
    best_pattern: Optional[str]
    best_score: float
    delta: float
    num_variants: int
    improved: bool


@dataclass
class HillClimbResult:
    """Result of a full hill-climbing session for one function."""

    symbol: str
    function_name: str
    source_path: str
    initial_percent: float
    final_percent: float
    total_delta: float
    rounds: list  # list[RoundResult]
    stopped_reason: str  # "perfect", "plateau", "max_rounds", "no_variants", "noise_only", "error"
    elapsed_seconds: float
