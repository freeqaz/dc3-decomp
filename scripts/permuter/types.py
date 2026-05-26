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

# Matches MWCC-style demangled free functions: "FuncName(" at start, no class qualifier.
# Example: "MakeColor(float, float, float, Hmx::Color&)" — no return type, no __cdecl,
# the leading token IS the function name.
_MWCC_FREE_FUNC_RE = re.compile(
    r"^([\w~][\w]*)\s*\("
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

    # MWCC-style: "FuncName(" with no return type / __cdecl prefix.
    m = _MWCC_FREE_FUNC_RE.match(demangled)
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
    """A contiguous group of insert/delete instructions.

    ``target_opcodes`` lists the opcodes of TARGET-ONLY ``delete`` instructions
    in source order (code present in the target but not in our output).
    ``base_opcodes`` lists the opcodes of OUR-only ``insert`` instructions
    (code we emit that the target does not). Patterns can use these to detect
    structural signatures (e.g. inlined ``(end - begin) / sizeof(T)`` for
    ``deque::size()``: target-only ``subf + srawi + addze``).
    """

    start_idx: int
    end_idx: int
    size: int
    inserts: int
    deletes: int
    target_opcodes: tuple[str, ...] = ()
    base_opcodes: tuple[str, ...] = ()


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

    @property
    def has_gpr_fpr_type_conflict(self) -> bool:
        """True if GPR and FPR deltas have opposite signs.

        Indicates the target caches a value differently from our compiler:
        e.g., target uses GPR for a float literal address (GPR up, FPR down)
        while our compiler caches the float value in FPR (FPR up, GPR down).
        """
        gd = self.gpr_save_delta
        fd = self.fpr_save_delta
        if gd == 0 or fd == 0:
            return False
        return (gd > 0) != (fd > 0)

    @property
    def offset_swap_count(self) -> int:
        """Sum of instructions involved in mirror-paired offset shifts.

        A slot inversion appears as two large entries in `offset_deltas`:
        one positive delta D and one negative delta -D, both with high
        counts. This sums those mirror pairs across all symmetric deltas.

        For RndText::WrapText: -192 (87 inst) + +192 (52 inst) = 139.
        Threshold checked by `scope_widening` etc. is typically 10.
        """
        total = 0
        seen: set[int] = set()
        for delta, count in self.offset_deltas.items():
            if delta == 0 or delta in seen:
                continue
            mirror = self.offset_deltas.get(-delta, 0)
            if mirror > 0:
                total += count + mirror
                seen.add(delta)
                seen.add(-delta)
        return total

    @property
    def dominant_slot_pair(self) -> tuple[int, int] | None:
        """The largest mirror-paired offset delta as (smaller, larger).

        For WrapText's -192/+192 cascade, returns the absolute delta
        (192). Useful for patterns that need to know the slot-inversion
        magnitude. Returns None if no mirror pair detected.
        """
        best_delta = 0
        best_total = 0
        for delta, count in self.offset_deltas.items():
            if delta <= 0:
                continue
            mirror = self.offset_deltas.get(-delta, 0)
            if mirror == 0:
                continue
            total = count + mirror
            if total > best_total:
                best_total = total
                best_delta = delta
        if best_delta == 0:
            return None
        return (best_delta, best_total)


@dataclass
class PreprocRegion:
    """Preprocessor region within a function byte range."""

    start: int
    end: int
    kind: str  # ifdef/ifndef/if
    macro: str
    has_else: bool = False


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
    m2c_code: Optional[str] = None  # Raw m2c decompilation text
    target_var_order: Optional[list] = None  # Variable first-use order from Ghidra
    target_gpr_saves: Optional[int] = None  # GPR save count from __savegprlr_N
    # ASM listing path (for Ghidra+ASM crossref)
    asm_listing_path: Optional[Path] = None
    # In-function preprocessor regions (#if/#ifdef/#else/#endif)
    preproc_regions: list[PreprocRegion] = field(default_factory=list)
    # Extracted RB3 method body (same function), if available
    rb3_source: Optional[str] = None
    # Instruction attribution: mismatch regions from /FAs listing join
    mismatch_regions: list = field(default_factory=list)  # list[MismatchRegion]
    # Target facts: normalized evidence from diagnosis, atlas, guidance
    target_facts: object | None = None  # TargetFacts or None
    # When True, line/node_in_mismatch_region() always return True
    # (used when all mismatch regions have low confidence)
    blind_generation_mode: bool = False
    # Compiler dialect (mwcc | msvc). Patterns that emit C++11+ syntax (`auto`,
    # `decltype`, etc.) must check this — mwcc is C++98 and rejects them.
    # Populated by __main__ / batch entry points from CLI flag / permuter.json.
    compiler_dialect: str = "mwcc"

    def source_text(self, node: Node) -> str:
        """Extract source text for a tree-sitter node."""
        return self.file_source[node.start_byte : node.end_byte].decode("utf-8")

    def line_in_mismatch_region(self, line: int) -> bool:
        """Check if a source line falls within any mismatch region.

        Returns True if no regions are available (don't filter when no data).
        Also returns True when blind_generation_mode is set (low-confidence
        regions — don't trust region boundaries).
        """
        if not self.mismatch_regions:
            return True  # No attribution data — allow everything
        if self.blind_generation_mode:
            return True  # Low-confidence regions — allow everything
        for region in self.mismatch_regions:
            if region.start_line <= line <= region.end_line:
                return True
        return False

    def node_in_mismatch_region(self, node: Node, margin: int = 2) -> bool:
        """Check if a tree-sitter node overlaps any mismatch region.

        Uses the node's start/end line (1-based) with an optional margin.
        Returns True if no regions are available (don't filter when no data).
        Also returns True when blind_generation_mode is set (low-confidence
        regions — don't trust region boundaries).
        """
        if not self.mismatch_regions:
            return True  # No attribution data — allow everything
        if self.blind_generation_mode:
            return True  # Low-confidence regions — allow everything
        # tree-sitter uses 0-based lines; /FAs uses 1-based
        node_start = node.start_point[0] + 1
        node_end = node.end_point[0] + 1
        for region in self.mismatch_regions:
            if (node_start - margin <= region.end_line and
                    node_end + margin >= region.start_line):
                return True
        return False


@dataclass(frozen=True)
class AuxiliaryFile:
    """An additional file update carried by a variant."""

    path: Path
    content: bytes


@dataclass
class Variant:
    """A source variation to test."""

    name: str
    pattern_name: str
    description: str
    source: bytes  # Full modified file content
    edits: list | None = None  # Edits applied to produce this variant
    tags: frozenset[str] = field(default_factory=frozenset)
    auxiliary_files: tuple[AuxiliaryFile, ...] = field(default_factory=tuple)
    # Scope isolation: track the function byte range so we can verify
    # that only the target function was modified
    func_byte_range: tuple[int, int] | None = None
    original_source: bytes | None = None


def merge_auxiliary_file_sets(
    *groups: tuple[AuxiliaryFile, ...],
) -> tuple[AuxiliaryFile, ...] | None:
    """Merge auxiliary file sets, returning None on conflicting writes."""
    merged: dict[Path, bytes] = {}
    for group in groups:
        for entry in group:
            path = entry.path.resolve()
            existing = merged.get(path)
            if existing is not None and existing != entry.content:
                return None
            merged[path] = entry.content

    return tuple(
        AuxiliaryFile(path=path, content=content)
        for path, content in sorted(merged.items(), key=lambda item: str(item[0]))
    )


def variant_file_updates(primary_path: Path, variant: Variant) -> dict[Path, bytes]:
    """Return the exact file writes implied by a variant.

    If the variant carries scope-isolation metadata (func_byte_range +
    original_source), validates that only bytes within the target function
    were modified.  Raises ValueError on out-of-scope modifications.

    Exception: patterns whose `structural_domain` is "cross_unit" (e.g.
    accessor_outline) legitimately insert wrapper functions outside the
    target's byte range. For those we skip the pre-function check.
    """
    # Patterns that may legitimately write outside the target function's range.
    # Keep this list small — defaults remain strict.
    _CROSS_UNIT_PATTERNS = {"accessor_outline", "helper_inline"}
    skip_scope_check = variant.pattern_name in _CROSS_UNIT_PATTERNS

    # Scope isolation check: verify only target function bytes changed
    if (variant.func_byte_range and variant.original_source
            and not skip_scope_check):
        func_start, func_end = variant.func_byte_range
        orig = variant.original_source
        mod = variant.source
        # Check bytes BEFORE the function
        orig_before = orig[:func_start]
        mod_before = mod[:func_start] if len(mod) >= func_start else mod
        if orig_before != mod_before:
            # Find first differing byte for diagnostic
            for i in range(min(len(orig_before), len(mod_before))):
                if orig_before[i] != mod_before[i]:
                    raise ValueError(
                        f"Variant '{variant.name}' ({variant.pattern_name}) modified "
                        f"byte {i} BEFORE target function (func starts at {func_start})"
                    )
            raise ValueError(
                f"Variant '{variant.name}' ({variant.pattern_name}) modified "
                f"content before target function (length mismatch)"
            )
        # Check bytes AFTER the function — must account for size changes
        # within the function that shift everything after it
        func_size_orig = func_end - func_start
        func_size_mod = len(mod) - len(orig) + func_size_orig
        mod_after_start = func_start + func_size_mod
        orig_after = orig[func_end:]
        mod_after = mod[mod_after_start:] if mod_after_start <= len(mod) else b""
        if orig_after != mod_after:
            for i in range(min(len(orig_after), len(mod_after))):
                if orig_after[i] != mod_after[i]:
                    raise ValueError(
                        f"Variant '{variant.name}' ({variant.pattern_name}) modified "
                        f"byte {func_end + i} AFTER target function "
                        f"(func ends at {func_end})"
                    )
            raise ValueError(
                f"Variant '{variant.name}' ({variant.pattern_name}) modified "
                f"content after target function (length mismatch)"
            )
    elif skip_scope_check:
        # Cross-unit patterns: skip the strict scope check but still detect
        # accidental no-op variants (source unchanged from original).
        if (variant.original_source is not None
                and variant.source == variant.original_source):
            raise ValueError(
                f"Variant '{variant.name}' ({variant.pattern_name}) produced "
                f"no change to source"
            )

    updates = {primary_path.resolve(): variant.source}
    for entry in variant.auxiliary_files:
        resolved = entry.path.resolve()
        if resolved in updates and updates[resolved] != entry.content:
            raise ValueError(f"Conflicting writes for {resolved}")
        updates[resolved] = entry.content
    return updates


def variant_identity_bytes(primary_path: Path, variant: Variant) -> bytes:
    """Stable byte representation of a variant's full file update set."""
    updates = variant_file_updates(primary_path, variant)
    chunks: list[bytes] = []
    for path, content in sorted(updates.items(), key=lambda item: str(item[0])):
        path_bytes = str(path).encode("utf-8", errors="surrogateescape")
        chunks.append(len(path_bytes).to_bytes(4, "big"))
        chunks.append(path_bytes)
        chunks.append(len(content).to_bytes(8, "big"))
        chunks.append(content)
    return b"".join(chunks)


@dataclass
class ScoreResult:
    """Result of building and scoring a variant."""

    variant: Variant
    match_percent: float
    build_success: bool
    error: Optional[str] = None
    execution_equivalent: Optional[bool] = None
    canonical_il_hash: Optional[str] = None


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
    winning_pattern: Optional[str] = None
    ghidra_stats: Optional[object] = None  # GhidraRunStats or None
    validation_tier: int = 0  # Highest validation tier reached (0-6)
    validation_distribution: dict[int, int] = field(default_factory=dict)  # tier -> count across all variants
    shape_facts_enabled: bool = True
    codegen_shapes: list[str] = field(default_factory=list)
    fact_boost_patterns: list[str] = field(default_factory=list)
    fact_suppress_patterns: list[str] = field(default_factory=list)
    il_analyzed_variants: int = 0
    il_unique_buckets: int = 0
    il_duplicate_buckets: int = 0
    il_pattern_metrics: dict[str, dict[str, int]] = field(default_factory=dict)


@dataclass
class ChainSpec:
    """N-stage pattern chain specification."""

    stages: list[str]  # Pattern names in order
    reason: str  # Why selected
    budget: int = 10  # Max final variants from this chain
    priority: float = 0.0  # Higher = tried first (used for sorting before truncation)


@dataclass
class RoundHints:
    """Per-round carry-forward state for adaptive hill climbing."""

    pattern_deltas: dict[str, list[tuple[float, int]]] = field(default_factory=dict)
    tag_deltas: dict[str, list[tuple[float, int]]] = field(default_factory=dict)
    pattern_wins: dict[str, int] = field(default_factory=dict)
    tag_wins: dict[str, int] = field(default_factory=dict)
    pattern_failures: dict[str, int] = field(default_factory=dict)
    pattern_positive_tags: dict[str, set[str]] = field(default_factory=dict)
    last_winner: str | None = None
    last_winner_tags: frozenset[str] = field(default_factory=frozenset)
    last_diagnosis: Optional[Diagnosis] = None
    # Patterns where ALL variants failed to build (100% build failure rate)
    build_failed_patterns: set[str] = field(default_factory=set)
    # Atlas-derived pattern boost/suppress (from compiler_atlas lookups)
    atlas_boost_patterns: set[str] = field(default_factory=set)
    atlas_suppress_patterns: set[str] = field(default_factory=set)
    # IL-analysis feedback from previous rounds/depths
    il_duplicate_patterns: set[str] = field(default_factory=set)
    il_unique_patterns: set[str] = field(default_factory=set)
    # Composition pairs suppressed due to historical ineffectiveness
    suppress_pairs: set[tuple[str, str]] = field(default_factory=set)
    # Learned pattern effectiveness from historical DB data
    # Maps pattern name -> (win_rate, avg_delta)
    learned_effectiveness: dict[str, tuple[float, float]] = field(default_factory=dict)
    # When True, skip loading learned effectiveness data
    no_learned_priority: bool = False

    def force_pattern(self, pattern_name: str) -> bool:
        """Return True when guidance should override diagnosis gating."""
        return (
            pattern_name in self.atlas_boost_patterns and
            pattern_name not in self.atlas_suppress_patterns
        )

    def priority_floor(self, pattern_name: str) -> float:
        """Return a minimum search priority for guidance-boosted patterns."""
        if self.force_pattern(pattern_name):
            return 0.35
        return 0.0

    def record_round(
        self,
        round_num: int,
        variant_results: list[ScoreResult],
        baseline: float,
        winner_pattern: str | None,
        winner_variant: Variant | None = None,
    ) -> None:
        """Update hints from a completed round's results."""
        # Track per-pattern best delta
        by_pattern: dict[str, float] = {}
        by_tag: dict[str, float] = {}
        for result in variant_results:
            pname = result.variant.pattern_name
            delta = result.match_percent - baseline
            # Strip compose: prefix to credit both patterns
            for p in _split_pattern_name(pname):
                if p not in by_pattern or delta > by_pattern[p]:
                    by_pattern[p] = delta
                if delta > 0.0 and result.variant.tags:
                    self.pattern_positive_tags.setdefault(p, set()).update(
                        result.variant.tags
                    )
            for tag in result.variant.tags:
                if tag not in by_tag or delta > by_tag[tag]:
                    by_tag[tag] = delta

        for p, delta in by_pattern.items():
            self.pattern_deltas.setdefault(p, []).append((delta, round_num))
        for tag, delta in by_tag.items():
            self.tag_deltas.setdefault(tag, []).append((delta, round_num))

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
        if winner_variant:
            self.last_winner_tags = winner_variant.tags
            for tag in winner_variant.tags:
                self.tag_wins[tag] = self.tag_wins.get(tag, 0) + 1

        # Track build failures: patterns where ALL variants failed to compile
        self.build_failed_patterns = _collect_build_failed_patterns(variant_results)

    def suppression_factor(self, pattern_name: str) -> float:
        """Get suppression multiplier for a pattern.

        Returns 1.0 (no suppression) down to 0.1 over 3 consecutive failures.
        """
        failures = self.pattern_failures.get(pattern_name, 0)
        if failures == 0:
            base = 1.0
        elif failures == 1:
            base = 0.7
        elif failures == 2:
            base = 0.3
        else:
            base = 0.1
        if (
            pattern_name in self.il_duplicate_patterns and
            pattern_name not in self.il_unique_patterns
        ):
            base *= 0.55
        return base

    def adaptive_priority_boost(self, pattern_name: str) -> float:
        """Get a positive multiplier from past wins, tags, and atlas."""
        boost = 1.0

        wins = self.pattern_wins.get(pattern_name, 0)
        if wins > 0:
            boost += min(0.4, wins * 0.1)

        # Atlas-derived boost (from compiler_atlas lookups)
        if pattern_name in self.atlas_boost_patterns:
            boost += 0.3
        if pattern_name in self.atlas_suppress_patterns:
            boost *= 0.3  # Strong suppression for atlas-negative patterns

        if pattern_name in self.il_unique_patterns:
            boost += 0.12
        if (
            pattern_name in self.il_duplicate_patterns and
            pattern_name not in self.il_unique_patterns
        ):
            boost *= 0.7

        tags = self.promising_tags_for_pattern(pattern_name)
        if not tags:
            return boost

        if tags & self.last_winner_tags:
            boost += 0.2

        tag_win_bonus = sum(self.tag_wins.get(tag, 0) for tag in tags)
        if tag_win_bonus > 0:
            boost += min(0.4, tag_win_bonus * 0.08)
        elif any(tag in self.promising_tags() for tag in tags):
            boost += 0.1

        return boost

    def promising_patterns(self) -> list[str]:
        """Return patterns that had positive delta in any round."""
        result = []
        for p, deltas in self.pattern_deltas.items():
            if any(d > 0.0 for d, _ in deltas):
                result.append(p)
        return result

    def promising_tags(self) -> list[str]:
        """Return structural tags that had positive delta in any round."""
        result = []
        for tag, deltas in self.tag_deltas.items():
            if any(d > 0.0 for d, _ in deltas):
                result.append(tag)
        return result

    def promising_tags_for_pattern(self, pattern_name: str) -> frozenset[str]:
        """Return historically positive tags associated with a pattern."""
        return frozenset(self.pattern_positive_tags.get(pattern_name, set()))


def _split_pattern_name(name: str) -> list[str]:
    """Split a pattern name into component patterns.

    Handles 'compose:a+b', 'chain:a+b+c', and 'crosscompose:a+b' prefixed names.
    """
    for prefix in ("compose:", "chain:", "crosscompose:", "merge:", "evo_cross:", "evo_mut:"):
        if name.startswith(prefix):
            _, parts = name.split(":", 1)
            return parts.split("+")
    return [name]


def _collect_build_failed_patterns(
    results: list[ScoreResult],
) -> set[str]:
    """Identify base patterns where ALL variants failed to compile.

    A pattern is flagged only if it had at least 1 variant and every
    variant failed to build. Only tracks base patterns (not compose:/chain:).
    """
    by_pattern: dict[str, tuple[int, int]] = {}  # name -> (total, failures)
    for result in results:
        name = result.variant.pattern_name
        # Only track base patterns (Phase 1)
        if ":" in name:
            continue
        total, fails = by_pattern.get(name, (0, 0))
        total += 1
        if not result.build_success:
            fails += 1
        by_pattern[name] = (total, fails)

    return {
        name for name, (total, fails) in by_pattern.items()
        if total > 0 and fails == total
    }


@dataclass
class BeamState:
    """A single state in the beam search.

    Represents a reparsed source state with metadata about its quality,
    provenance, and guidance agreement.
    """

    source: bytes  # Full modified file content
    score: float  # Match percent from scoring
    diagnosis: Diagnosis | None = None
    tags: frozenset[str] = field(default_factory=frozenset)
    applied_patterns: list[str] = field(default_factory=list)
    generation: int = 0  # Depth in the search tree
    stagnation_count: int = 0  # Rounds without score improvement
    build_fail_count: int = 0  # Cumulative build failures
    guidance_agreement: int = 0  # -1 to +2 (conflict to full agreement)
    provenance: list[str] = field(default_factory=list)  # Variant names
    # Auxiliary file edits carried from the variant
    auxiliary_files: tuple[AuxiliaryFile, ...] = field(default_factory=tuple)
    # Pattern-level build failure tracking within this lineage
    lineage_build_failures: dict[str, int] = field(default_factory=dict)
    # Per-region match ratios from /FAs attribution (line_range → ratio)
    # e.g. {(500, 502): 0.68, (540, 544): 0.69}
    region_scores: dict[tuple[int, int], float] = field(default_factory=dict)
    # Target facts: normalized evidence carried per-state
    target_facts: object | None = None  # TargetFacts or None
    # Fact agreement score: how many high-confidence facts this state satisfies
    fact_agreement: int = 0
    # Validation tier reached (0-6, from validator ladder)
    validation_tier: int = 0
    # Canonical IL hash for analysis/ranking only (not an equivalence oracle)
    canonical_il_hash: str | None = None
    # Small positive tiebreak when this state's analyzed IL bucket is unique
    il_diversity_bonus: int = 0
    # IL-guided pattern pressure carried along this lineage
    il_duplicate_patterns: frozenset[str] = field(default_factory=frozenset)
    il_unique_patterns: frozenset[str] = field(default_factory=frozenset)

    @property
    def ranking_key(self) -> tuple:
        """Lexicographic ranking tuple (higher = better).

        Order: match%, validation_tier, -build_fails, guidance,
               fact_agreement, il_diversity_bonus, -stagnation, -chain_length.
        """
        return (
            self.score,
            self.validation_tier,
            -self.build_fail_count,
            self.guidance_agreement,
            self.fact_agreement,
            self.il_diversity_bonus,
            -self.stagnation_count,
            -len(self.provenance),
        )

    def region_improvement_count(self, parent_regions: dict[tuple[int, int], float]) -> int:
        """Count how many regions improved compared to a parent state."""
        if not self.region_scores or not parent_regions:
            return 0
        count = 0
        for key, ratio in self.region_scores.items():
            parent_ratio = parent_regions.get(key, 0.0)
            if ratio > parent_ratio + 0.01:  # 1% threshold
                count += 1
        return count


@dataclass
class BeamConfig:
    """Configuration for beam search."""

    width: int = 8  # Max survivors per depth
    depth: int = 4  # Max expansion depths
    expand: int = 24  # Variants per state per depth
    escape: int = 4  # Perturbation budget when stalled
    diversity: int = 3  # Min distinct pattern families in beam
    reserve_size: int = 3  # Pruned states kept as reserve for stagnation recovery
    workers: int = 0  # Parallel compile workers (0 = cpu_count)
    auto_width: bool = True  # Auto-size width/expand based on function complexity


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
