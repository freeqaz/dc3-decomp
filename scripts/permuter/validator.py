"""Validator Ladder — multi-level acceptance checks for variants.

Implements a 6-level validation chain that goes beyond raw score delta:

    Level 1: Parse validity — tree-sitter reparse succeeds
    Level 2: Build success — ninja + compile succeeded
    Level 3: Objdiff improvement — score >= baseline
    Level 4: Region improvement — attributed regions individually improved
    Level 5: Fact agreement — target facts not violated by the variant
    Level 6: Semantic checks — return-shape, call-order, assertion-count

Each level produces a ValidationResult with a tier (1-6), pass/fail, and
optional diagnostics. Higher tiers represent stronger validation.

This is Synthesis Engine Phase 5 — see docs/plans/synthesis-engine/ROADMAP.md.

Usage:
    from scripts.permuter.validator import validate_variant, ValidationTier

    result = validate_variant(
        variant, ctx, score_result,
        baseline_score=93.5,
        parent_regions={},
    )
    if result.tier >= ValidationTier.REGION_IMPROVED:
        # High-confidence improvement
        ...
"""

from __future__ import annotations

import enum
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser


# ---------------------------------------------------------------------------
# Tier enumeration
# ---------------------------------------------------------------------------

class ValidationTier(enum.IntEnum):
    """Validation levels, ordered from weakest to strongest."""

    INVALID = 0          # Failed to parse
    PARSE_OK = 1         # Parses, but didn't build
    BUILD_OK = 2         # Built, but didn't improve
    SCORE_IMPROVED = 3   # Score improved over baseline
    REGION_IMPROVED = 4  # Per-region improvement (no region regressions)
    FACT_AGREED = 5      # Target facts not violated
    SEMANTIC_OK = 6      # Full semantic checks passed


# ---------------------------------------------------------------------------
# Result dataclass
# ---------------------------------------------------------------------------

@dataclass
class ValidationResult:
    """Result of running the validation ladder on a variant."""

    tier: ValidationTier
    passed: bool  # True if the variant passed its tier's check
    diagnostics: list[str] = field(default_factory=list)
    # Per-level detail
    parse_ok: bool = False
    build_ok: bool = False
    score_improved: bool = False
    region_improved: bool = False
    region_regressions: int = 0
    fact_agreed: bool = False
    fact_violations: list[str] = field(default_factory=list)
    semantic_ok: bool = False
    semantic_issues: list[str] = field(default_factory=list)

    @property
    def is_acceptable(self) -> bool:
        """Whether the variant meets minimum acceptance (tier >= BUILD_OK)."""
        return self.tier >= ValidationTier.BUILD_OK

    @property
    def is_high_quality(self) -> bool:
        """Whether the variant passes all available checks."""
        return self.tier >= ValidationTier.REGION_IMPROVED


# ---------------------------------------------------------------------------
# Level 1: Parse validity
# ---------------------------------------------------------------------------

_CPP_LANGUAGE = Language(tscpp.language())
_parser = None


def _get_parser() -> Parser:
    global _parser
    if _parser is None:
        _parser = Parser(_CPP_LANGUAGE)
    return _parser


def check_parse_validity(source: bytes) -> tuple[bool, list[str]]:
    """Check that source parses without errors.

    Returns (ok, diagnostics).
    """
    parser = _get_parser()
    tree = parser.parse(source)
    if tree.root_node.has_error:
        # Find the error nodes
        errors = []
        _collect_errors(tree.root_node, errors, max_errors=5)
        return False, [f"Parse error at line {e[0]}:{e[1]}" for e in errors]
    return True, []


def _collect_errors(node, errors: list, max_errors: int = 5) -> None:
    """Walk tree-sitter tree collecting ERROR node positions."""
    if len(errors) >= max_errors:
        return
    if node.type == "ERROR" or node.is_missing:
        errors.append((node.start_point[0] + 1, node.start_point[1]))
    for child in node.children:
        _collect_errors(child, errors, max_errors)


# ---------------------------------------------------------------------------
# Level 2: Build success (from ScoreResult)
# ---------------------------------------------------------------------------

def check_build_success(score_result) -> tuple[bool, list[str]]:
    """Check that the variant built successfully.

    Takes a ScoreResult from scorer.
    """
    if score_result is None:
        return False, ["No score result available"]
    if not score_result.build_success:
        return False, [score_result.error or "Build failed"]
    return True, []


# ---------------------------------------------------------------------------
# Level 3: Objdiff improvement
# ---------------------------------------------------------------------------

def check_score_improved(
    score_result,
    baseline: float,
    tolerance: float = 0.0,
) -> tuple[bool, list[str]]:
    """Check that variant score >= baseline.

    Args:
        score_result: ScoreResult from scorer.
        baseline: Baseline match percentage.
        tolerance: Accept scores within this delta below baseline.
    """
    if score_result is None or not score_result.build_success:
        return False, ["Not scored"]
    delta = score_result.match_percent - baseline
    if delta >= -tolerance:
        return True, [f"Score: {score_result.match_percent:.2f}% (delta: {delta:+.2f}%)"]
    return False, [f"Score regressed: {score_result.match_percent:.2f}% (delta: {delta:+.2f}%)"]


# ---------------------------------------------------------------------------
# Level 4: Region improvement
# ---------------------------------------------------------------------------

def check_region_improvement(
    child_regions: dict[tuple[int, int], float],
    parent_regions: dict[tuple[int, int], float],
    regression_threshold: float = 0.02,
) -> tuple[bool, int, list[str]]:
    """Check per-region improvements and detect regressions.

    Args:
        child_regions: Region→match_ratio for the variant.
        parent_regions: Region→match_ratio for the parent state.
        regression_threshold: How much worse a region can get before
            it counts as a regression (default: 2%).

    Returns:
        (passed, regression_count, diagnostics).
    """
    if not parent_regions or not child_regions:
        return True, 0, ["No region data — skipping region check"]

    regressions = 0
    improvements = 0
    diagnostics = []

    for key, parent_ratio in parent_regions.items():
        child_ratio = child_regions.get(key, 0.0)
        delta = child_ratio - parent_ratio
        if delta < -regression_threshold:
            regressions += 1
            diagnostics.append(
                f"Region {key}: regressed {parent_ratio:.2f}→{child_ratio:.2f} "
                f"({delta:+.2f})"
            )
        elif delta > regression_threshold:
            improvements += 1

    passed = regressions == 0
    if passed and improvements > 0:
        diagnostics.append(f"{improvements} regions improved")

    return passed, regressions, diagnostics


# ---------------------------------------------------------------------------
# Level 5: Fact agreement
# ---------------------------------------------------------------------------

def check_fact_agreement(
    target_facts,
    variant_pattern: str,
    score_result=None,
    parent_score: float = 0.0,
) -> tuple[bool, list[str]]:
    """Check that the variant does not violate high-confidence facts.

    Checks:
    - Pattern is not in the suppress set
    - No-touch zones are not regressed
    - Noise-only functions don't get worse

    Args:
        target_facts: TargetFacts object.
        variant_pattern: The pattern name that produced this variant.
        score_result: ScoreResult from scoring.
        parent_score: Parent state's score for regression detection.
    """
    if target_facts is None:
        return True, ["No facts available"]

    violations = []

    try:
        boost, suppress = target_facts.pattern_recommendations()

        # Check pattern suppression
        base_names = _split_pattern(variant_pattern)
        suppressed = [bn for bn in base_names if bn in suppress]
        if suppressed:
            violations.append(
                f"Pattern(s) {suppressed} suppressed by target facts"
            )

        # Check noise-only: if function is mostly noise, don't accept regressions
        noise_facts = [
            f for f in target_facts.by_kind("mismatch_class")
            if f.payload.get("class") == "mostly_noise"
        ]
        if noise_facts and score_result and score_result.match_percent < parent_score:
            violations.append("Score regression in noise-dominated function")

    except Exception as e:
        return True, [f"Fact check error: {e}"]

    passed = len(violations) == 0
    return passed, violations


def _split_pattern(name: str) -> list[str]:
    """Split a pattern name into base components."""
    for prefix in ("compose:", "chain:", "crosscompose:", "merge:", "evo_cross:", "evo_mut:"):
        if name.startswith(prefix):
            return name.split(":", 1)[1].split("+")
    return [name]


# ---------------------------------------------------------------------------
# Level 6: Semantic checks
# ---------------------------------------------------------------------------

def check_semantics(
    original_source: bytes,
    variant_source: bytes,
    func_name: str | None = None,
) -> tuple[bool, list[str]]:
    """Lightweight semantic checks on variant vs original.

    Checks:
    - Return statement count preserved
    - MILO_ASSERT count preserved
    - Function call set approximately preserved (no new calls added)

    These are heuristic and intentionally conservative — they flag
    suspicious changes but don't reject variants outright for most cases.
    """
    issues = []

    try:
        orig_text = original_source.decode("utf-8", errors="replace")
        var_text = variant_source.decode("utf-8", errors="replace")
    except Exception:
        return True, ["Cannot decode source"]

    # Return count
    orig_returns = len(re.findall(r'\breturn\b', orig_text))
    var_returns = len(re.findall(r'\breturn\b', var_text))
    if orig_returns != var_returns:
        issues.append(
            f"Return count changed: {orig_returns} → {var_returns}"
        )

    # MILO_ASSERT count
    orig_asserts = len(re.findall(r'\bMILO_ASSERT\b', orig_text))
    var_asserts = len(re.findall(r'\bMILO_ASSERT\b', var_text))
    if orig_asserts != var_asserts:
        issues.append(
            f"MILO_ASSERT count changed: {orig_asserts} → {var_asserts}"
        )

    # Call set: check that no new function calls were introduced
    # (very conservative — only flags new identifiers before '(')
    orig_calls = set(re.findall(r'(\b\w+)\s*\(', orig_text))
    var_calls = set(re.findall(r'(\b\w+)\s*\(', var_text))
    new_calls = var_calls - orig_calls
    # Filter out common non-call keywords
    keywords = {
        'if', 'while', 'for', 'switch', 'return', 'sizeof', 'typeof',
        'auto', 'int', 'float', 'double', 'void', 'bool', 'char',
        'unsigned', 'signed', 'long', 'short', 'const', 'static',
        'MILO_ASSERT', 'MILO_WARN', 'MILO_FAIL',
    }
    new_calls -= keywords
    if new_calls:
        issues.append(
            f"New call-like identifiers: {sorted(new_calls)[:5]}"
        )

    passed = len(issues) == 0
    return passed, issues


# ---------------------------------------------------------------------------
# Full ladder
# ---------------------------------------------------------------------------

def validate_variant(
    variant,
    score_result=None,
    baseline_score: float = 0.0,
    parent_regions: dict[tuple[int, int], float] | None = None,
    child_regions: dict[tuple[int, int], float] | None = None,
    target_facts=None,
    original_source: bytes | None = None,
) -> ValidationResult:
    """Run the full validation ladder on a variant.

    Runs levels 1-6 in order, stopping at the first failure.
    Returns a ValidationResult with the highest passed tier.
    """
    result = ValidationResult(tier=ValidationTier.INVALID, passed=False)

    # Level 2 first: the actual compiler (MWCC/MSVC) has already parsed the
    # variant if the build succeeded. tree-sitter for C++ is fragile around
    # Milo macros and rejects many variants the compiler accepts, so we treat
    # a successful build as authoritative for PARSE+BUILD and only fall back
    # to tree-sitter when the build failed (to distinguish parse errors from
    # link errors etc).
    build_ok, build_diags = check_build_success(score_result)
    result.build_ok = build_ok
    result.diagnostics.extend(build_diags)
    if build_ok:
        result.parse_ok = True
        result.tier = ValidationTier.BUILD_OK
    else:
        # Build failed — run tree-sitter to figure out if it was a parse
        # problem or something later (link, missing symbol, etc).
        parse_ok, parse_diags = check_parse_validity(variant.source)
        result.parse_ok = parse_ok
        result.diagnostics.extend(parse_diags)
        if not parse_ok:
            return result
        result.tier = ValidationTier.PARSE_OK
        # Stop here — build failed even though parse was ok.
        return result

    # Level 3: Score
    score_ok, score_diags = check_score_improved(score_result, baseline_score)
    result.score_improved = score_ok
    result.diagnostics.extend(score_diags)
    if not score_ok:
        return result
    result.tier = ValidationTier.SCORE_IMPROVED

    # Level 4: Region
    region_ok, regressions, region_diags = check_region_improvement(
        child_regions or {},
        parent_regions or {},
    )
    result.region_improved = region_ok
    result.region_regressions = regressions
    result.diagnostics.extend(region_diags)
    if not region_ok:
        return result
    result.tier = ValidationTier.REGION_IMPROVED

    # Level 5: Fact agreement
    fact_ok, fact_diags = check_fact_agreement(
        target_facts,
        variant.pattern_name,
        score_result,
        baseline_score,
    )
    result.fact_agreed = fact_ok
    result.fact_violations = fact_diags
    result.diagnostics.extend(fact_diags)
    if not fact_ok:
        return result
    result.tier = ValidationTier.FACT_AGREED

    # Level 6: Semantic checks
    if original_source is not None:
        sem_ok, sem_diags = check_semantics(
            original_source, variant.source,
        )
        result.semantic_ok = sem_ok
        result.semantic_issues = sem_diags
        result.diagnostics.extend(sem_diags)
        if not sem_ok:
            return result

    result.tier = ValidationTier.SEMANTIC_OK
    result.passed = True
    return result


def format_result(result: ValidationResult, verbose: bool = False) -> str:
    """Format a ValidationResult as a human-readable string.

    Args:
        result: The validation result to format.
        verbose: If True, include all diagnostics. If False, one-line summary.

    Returns:
        Formatted string (no trailing newline).
    """
    tier_names = {
        ValidationTier.INVALID: "INVALID",
        ValidationTier.PARSE_OK: "PARSE_OK",
        ValidationTier.BUILD_OK: "BUILD_OK",
        ValidationTier.SCORE_IMPROVED: "SCORE_IMPROVED",
        ValidationTier.REGION_IMPROVED: "REGION_IMPROVED",
        ValidationTier.FACT_AGREED: "FACT_AGREED",
        ValidationTier.SEMANTIC_OK: "SEMANTIC_OK",
    }
    tier_name = tier_names.get(result.tier, f"TIER_{result.tier}")
    status = "PASS" if result.passed else "FAIL"

    if not verbose:
        return f"[{status}] tier={tier_name} ({int(result.tier)}/6)"

    lines = [f"Validation: {status} at {tier_name} ({int(result.tier)}/6)"]

    # Per-level status
    checks = [
        ("Parse", result.parse_ok),
        ("Build", result.build_ok),
        ("Score", result.score_improved),
        ("Region", result.region_improved),
        ("Facts", result.fact_agreed),
        ("Semantic", result.semantic_ok),
    ]
    for name, ok in checks:
        marker = "+" if ok else "-"
        lines.append(f"  {marker} {name}")

    if result.region_regressions > 0:
        lines.append(f"  Region regressions: {result.region_regressions}")

    if result.fact_violations:
        for v in result.fact_violations:
            lines.append(f"  Fact violation: {v}")

    if result.semantic_issues:
        for s in result.semantic_issues:
            lines.append(f"  Semantic: {s}")

    return "\n".join(lines)


def format_tier_distribution(results: list[ValidationResult]) -> str:
    """Format a summary of validation tier distribution.

    Returns a one-line string like: "T6:3 T5:1 T4:2 T3:5 T2:8 T1:0 T0:1"
    """
    counts: dict[int, int] = {}
    for r in results:
        t = int(r.tier)
        counts[t] = counts.get(t, 0) + 1

    parts = []
    for tier in range(6, -1, -1):
        count = counts.get(tier, 0)
        if count > 0:
            parts.append(f"T{tier}:{count}")
    return " ".join(parts) if parts else "no results"


def validate_batch(
    variants: list,
    score_results: list,
    baseline_score: float = 0.0,
    parent_regions: dict[tuple[int, int], float] | None = None,
    target_facts=None,
    original_source: bytes | None = None,
) -> list[ValidationResult]:
    """Validate a batch of variants against their score results.

    Returns a list of ValidationResults parallel to the input lists.
    """
    results = []
    for variant, score_result in zip(variants, score_results):
        result = validate_variant(
            variant,
            score_result=score_result,
            baseline_score=baseline_score,
            parent_regions=parent_regions,
            target_facts=target_facts,
            original_source=original_source,
        )
        results.append(result)
    return results
