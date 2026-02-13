"""Shared dataclasses for the permuter."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from tree_sitter import Node


@dataclass
class FunctionContext:
    """Parsed function ready for pattern application."""

    file_path: Path
    file_source: bytes  # Full file content
    func_node: Node  # tree-sitter function_definition node
    body_node: Node  # The compound_statement body
    statements: list[Node]  # Top-level named children in the body
    func_byte_range: tuple[int, int]  # (start_byte, end_byte)

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
