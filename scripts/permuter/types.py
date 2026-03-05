"""Shared dataclasses for the permuter."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from tree_sitter import Node

# Regex to extract qualified C++ name from demangled signature.
# Matches Class::Method( and also handles operator overloads like:
#   Class::operator()(...  Class::operator==(... Class::operator<<(...
_QUALIFIED_NAME_RE = re.compile(
    r"([\w~][\w:~]*(?:::[\w~]+)+)"  # Base: Class::Method (requires ::)
    r"("
    r"\(\)"  # operator()
    r"|[!=<>]=?"  # operator==, operator!=, operator<, etc.
    r"|<<|>>"  # operator<<, operator>>
    r"|\[\]"  # operator[]
    r"|\+\+|--"  # operator++, operator--
    r"|[+\-*/&|^%~!]"  # operator+, operator-, etc.
    r")?"  # operator suffix is optional
    r"\s*\("  # opening paren of arg list
)

# Matches free functions: "return_type __cdecl FuncName("
_FREE_FUNC_RE = re.compile(
    r"__cdecl\s+([\w]+)\s*\("
)


def extract_qualified_name(demangled: str) -> str | None:
    """Extract qualified C++ name from MSVC demangled signature.

    Handles operator overloads like operator(), operator==, etc.
    Also handles free functions like op50, PropSync, etc.
    Returns None if no qualified name found.
    """
    m = _QUALIFIED_NAME_RE.search(demangled)
    if m:
        name = m.group(1)
        if m.group(2):
            name += m.group(2)
        return name

    # Try free function pattern
    m = _FREE_FUNC_RE.search(demangled)
    if m:
        return m.group(1)

    return None


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
    # Prologue save counts (from __savegprlr_N / __savefpr_N calls)
    target_gpr_saves: int | None = None  # e.g., 8 for __savegprlr_24 (32-24)
    base_gpr_saves: int | None = None
    target_fpr_saves: int | None = None
    base_fpr_saves: int | None = None

    @property
    def has_prologue_mismatch(self) -> bool:
        """True if target and base differ in prologue save count."""
        if self.target_gpr_saves is not None and self.base_gpr_saves is not None:
            if self.target_gpr_saves != self.base_gpr_saves:
                return True
        if self.target_fpr_saves is not None and self.base_fpr_saves is not None:
            if self.target_fpr_saves != self.base_fpr_saves:
                return True
        return False

    @property
    def gpr_save_delta(self) -> int:
        """Target GPR saves minus base GPR saves. Positive = target needs more."""
        if self.target_gpr_saves is not None and self.base_gpr_saves is not None:
            return self.target_gpr_saves - self.base_gpr_saves
        return 0

    @property
    def fpr_save_delta(self) -> int:
        """Target FPR saves minus base FPR saves. Positive = target needs more."""
        if self.target_fpr_saves is not None and self.base_fpr_saves is not None:
            return self.target_fpr_saves - self.base_fpr_saves
        return 0


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
    symbol: Optional[str] = None  # Mangled symbol name for BSF isolation
    # Ghidra-guided fields (populated when --ghidra is enabled)
    ghidra_code: Optional[str] = None  # Raw Ghidra decompilation text
    ghidra_ast: Optional[object] = None  # Parsed GhidraAST
    target_var_order: Optional[list] = None  # Variable first-use order from Ghidra
    target_gpr_saves: Optional[int] = None  # GPR save count from __savegprlr_N
    # ASM listing path (for Ghidra+ASM crossref)
    asm_listing_path: Optional[Path] = None

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
    edits: list | None = None  # Edits applied to produce this variant


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
    ghidra_stats: Optional[object] = None  # GhidraRunStats or None


@dataclass
class ChainSpec:
    """N-stage pattern chain specification."""

    stages: list[str]  # Pattern names in order
    reason: str  # Why selected
    budget: int = 10  # Max final variants from this chain


@dataclass
class RoundHints:
    """Per-round carry-forward state for adaptive hill climbing."""

    pattern_deltas: dict[str, list[tuple[float, int]]] = field(default_factory=dict)
    pattern_wins: dict[str, int] = field(default_factory=dict)
    pattern_failures: dict[str, int] = field(default_factory=dict)
    last_winner: str | None = None
    last_diagnosis: Optional[Diagnosis] = None

    def record_round(
        self,
        round_num: int,
        variant_results: list[ScoreResult],
        baseline: float,
        winner_pattern: str | None,
    ) -> None:
        """Update hints from a completed round's results."""
        # Track per-pattern best delta
        by_pattern: dict[str, float] = {}
        for result in variant_results:
            pname = result.variant.pattern_name
            # Strip compose: prefix to credit both patterns
            for p in _split_pattern_name(pname):
                delta = result.match_percent - baseline
                if p not in by_pattern or delta > by_pattern[p]:
                    by_pattern[p] = delta

        for p, delta in by_pattern.items():
            self.pattern_deltas.setdefault(p, []).append((delta, round_num))

        # Update wins and failures
        if winner_pattern:
            self.last_winner = winner_pattern
            for p in _split_pattern_name(winner_pattern):
                self.pattern_wins[p] = self.pattern_wins.get(p, 0) + 1
                self.pattern_failures[p] = 0  # Reset failure streak
        else:
            # No winner — increment failure count for all patterns that were tried
            for p in by_pattern:
                self.pattern_failures[p] = self.pattern_failures.get(p, 0) + 1

    def suppression_factor(self, pattern_name: str) -> float:
        """Get suppression multiplier for a pattern.

        Returns 1.0 (no suppression) down to 0.1 over 3 consecutive failures.
        """
        failures = self.pattern_failures.get(pattern_name, 0)
        if failures == 0:
            return 1.0
        elif failures == 1:
            return 0.7
        elif failures == 2:
            return 0.3
        else:
            return 0.1

    def promising_patterns(self) -> list[str]:
        """Return patterns that had positive delta in any round."""
        result = []
        for p, deltas in self.pattern_deltas.items():
            if any(d > 0.0 for d, _ in deltas):
                result.append(p)
        return result


def _split_pattern_name(name: str) -> list[str]:
    """Split a pattern name into component patterns.

    Handles 'compose:a+b' and 'chain:a+b+c' prefixed names.
    """
    if name.startswith("compose:") or name.startswith("chain:"):
        _, parts = name.split(":", 1)
        return parts.split("+")
    return [name]


@dataclass
class ResolvedEdit:
    """A deterministic source edit derived from constraint analysis."""

    category: str       # "decl_order", "cf_direction", "expr_shape", "null_guard"
    description: str
    start: int          # byte offset in file
    end: int            # byte offset in file
    replacement: bytes


@dataclass
class ConstraintSet:
    """Constraints extracted from Ghidra + objdiff for a function."""

    # Deterministic (from Ghidra)
    decl_order: list | None = None             # target var names in register order
    cf_directions: dict = field(default_factory=dict)  # stmt_idx -> tag
    expr_diffs: list = field(default_factory=list)      # list of expression diff info
    null_checks_to_remove: list = field(default_factory=list)  # stmt indices

    # Probabilistic (from objdiff)
    sign_choices: list = field(default_factory=list)    # list[(stmt_idx, "signed"|"unsigned")]

    # Prologue
    target_gpr_saves: int | None = None
    target_fpr_saves: int | None = None
    base_gpr_saves: int | None = None
    base_fpr_saves: int | None = None
    swap_pairs: list = field(default_factory=list)

    # Preflight
    preflight: object | None = None  # PreflightResult
    ghidra_available: bool = False
    diagnosis_available: bool = False

    @property
    def free_variable_count(self) -> int:
        return len(self.sign_choices)

    @property
    def is_provably_unfixable(self) -> bool:
        if self.preflight and self.preflight.confidence >= 0.9:
            return True
        return False

    @property
    def skip_reason(self) -> str | None:
        if self.preflight and self.preflight.confidence >= 0.9:
            return f"preflight: {self.preflight.skip_reason}"
        return None


@dataclass
class SynthesisResult:
    """Result of constraint-directed synthesis for a function."""

    constraints: object  # ConstraintSet
    variants: list       # list[Variant]
    skip_reason: str | None = None
    deterministic_edit_count: int = 0
    free_variable_count: int = 0
