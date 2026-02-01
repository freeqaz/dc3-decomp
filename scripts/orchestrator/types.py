"""Typed dataclasses for orchestrator module boundaries.

Replaces dict[str, Any] at key interfaces with validated, documented types.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


# Default tool lists for agent configurations
DEFAULT_DECOMP_TOOLS = [
    "Read", "Write", "Edit", "Bash", "Glob", "Grep",
    "mcp__orchestrator__report_result",
    "mcp__orchestrator__run_objdiff",
    "mcp__orchestrator__run_analyze_function",
    "mcp__orchestrator__lookup_struct_offset",
    "mcp__orchestrator__lookup_merged_symbol",
]

REFACTOR_TOOLS = [
    "Read", "Write", "Edit", "Bash", "Glob", "Grep",
    "mcp__orchestrator__run_objdiff",
]


@dataclass
class AgentRunConfig:
    """Immutable configuration for a single agent execution."""

    session_id: str
    worktree: Path
    prompt: str
    model: str  # "haiku", "sonnet", "opus"
    verbose: int = 1  # 0=quiet, 1=normal, 2=verbose
    max_turns: int = 300
    allowed_tools: list[str] | None = None  # None = default decomp toolset
    disallowed_tools: list[str] | None = None

    @property
    def effective_tools(self) -> list[str]:
        """Resolved tool list (default if None)."""
        if self.allowed_tools is not None:
            return list(self.allowed_tools)
        return list(DEFAULT_DECOMP_TOOLS)


@dataclass
class AgentRunResult:
    """What came back from a single agent execution."""

    exit_code: int
    status: str  # "complete", "at_limit", "stuck", "unknown", "error"
    percent: float | None
    notes: str
    verdict: str | None
    total_cost_usd: float | None = None
    duration_ms: int | None = None
    usage: dict[str, int] | None = None
    messages: list[Any] = field(default_factory=list)
    output: str = ""

    @property
    def succeeded(self) -> bool:
        return self.exit_code == 0 and self.status not in ("error", "unknown")

    @property
    def has_cost_data(self) -> bool:
        return self.total_cost_usd is not None

    def merge_cost(self, other: AgentRunResult) -> None:
        """Accumulate cost/usage from another result (e.g. refactor pass)."""
        if other.total_cost_usd is not None:
            self.total_cost_usd = (self.total_cost_usd or 0) + other.total_cost_usd
        if other.duration_ms is not None:
            self.duration_ms = (self.duration_ms or 0) + other.duration_ms
        if other.usage and self.usage:
            for k, v in other.usage.items():
                if not isinstance(v, (int, float)):
                    continue
                prev = self.usage.get(k, 0)
                self.usage[k] = (prev if isinstance(prev, (int, float)) else 0) + v


@dataclass
class SessionResult:
    """What run_single() returns to callers."""

    status: str
    start_percent: float
    end_percent: float
    verdict: str | None
    patch: str | None
    notes: str
    model: str
    session_id: str
    patch_applied: bool = False
    actual_cost_usd: float | None = None
    duration_ms: int | None = None
    usage: dict[str, int] | None = None
