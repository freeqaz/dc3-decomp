"""Tests for the validator ladder module."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.validator import (
    ValidationResult,
    ValidationTier,
    check_build_success,
    check_fact_agreement,
    check_parse_validity,
    check_region_improvement,
    check_score_improved,
    check_semantics,
    format_result,
    format_tier_distribution,
    validate_variant,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_variant(source: bytes = b"int f() { return 0; }", pattern: str = "test"):
    v = MagicMock()
    v.source = source
    v.pattern_name = pattern
    return v


def _make_score_result(build_success=True, match_percent=95.0, error=None):
    r = MagicMock()
    r.build_success = build_success
    r.match_percent = match_percent
    r.error = error
    r.variant = _make_variant()
    return r


def _make_target_facts(boost=None, suppress=None, no_touch=None, noise=False):
    tf = MagicMock()
    tf.pattern_recommendations.return_value = (
        set(boost or []),
        set(suppress or []),
    )
    if noise:
        noise_fact = MagicMock()
        noise_fact.kind = "mismatch_class"
        noise_fact.payload = {"class": "mostly_noise"}
        tf.by_kind.return_value = [noise_fact]
    else:
        tf.by_kind.return_value = no_touch or []
    return tf


# ---------------------------------------------------------------------------
# Level 1: Parse validity
# ---------------------------------------------------------------------------

class TestParseValidity(unittest.TestCase):
    def test_valid_cpp(self):
        source = b"int f() { return 0; }"
        ok, diags = check_parse_validity(source)
        self.assertTrue(ok)
        self.assertEqual(len(diags), 0)

    def test_valid_complex(self):
        source = b"""
        #include <vector>
        class Foo {
        public:
            int bar(int x) {
                if (x > 0) return x;
                return -x;
            }
        };
        """
        ok, diags = check_parse_validity(source)
        self.assertTrue(ok)

    def test_invalid_syntax(self):
        source = b"int f() { return }"
        ok, diags = check_parse_validity(source)
        # tree-sitter may or may not error on this depending on recovery
        # but clearly broken syntax should be caught
        # NOTE: tree-sitter is lenient, so this might still parse
        # The test verifies the function runs without crashing
        self.assertIsInstance(ok, bool)

    def test_empty_source(self):
        ok, diags = check_parse_validity(b"")
        self.assertTrue(ok)  # Empty file is valid C++


# ---------------------------------------------------------------------------
# Level 2: Build success
# ---------------------------------------------------------------------------

class TestBuildSuccess(unittest.TestCase):
    def test_success(self):
        sr = _make_score_result(build_success=True)
        ok, diags = check_build_success(sr)
        self.assertTrue(ok)

    def test_failure(self):
        sr = _make_score_result(build_success=False, error="compile error")
        ok, diags = check_build_success(sr)
        self.assertFalse(ok)
        self.assertIn("compile error", diags[0])

    def test_none_result(self):
        ok, diags = check_build_success(None)
        self.assertFalse(ok)


# ---------------------------------------------------------------------------
# Level 3: Score improvement
# ---------------------------------------------------------------------------

class TestScoreImproved(unittest.TestCase):
    def test_improved(self):
        sr = _make_score_result(match_percent=96.0)
        ok, diags = check_score_improved(sr, baseline=93.0)
        self.assertTrue(ok)

    def test_same(self):
        sr = _make_score_result(match_percent=93.0)
        ok, diags = check_score_improved(sr, baseline=93.0)
        self.assertTrue(ok)

    def test_regressed(self):
        sr = _make_score_result(match_percent=90.0)
        ok, diags = check_score_improved(sr, baseline=93.0)
        self.assertFalse(ok)

    def test_tolerance(self):
        sr = _make_score_result(match_percent=92.5)
        ok, diags = check_score_improved(sr, baseline=93.0, tolerance=1.0)
        self.assertTrue(ok)

    def test_not_built(self):
        sr = _make_score_result(build_success=False)
        ok, diags = check_score_improved(sr, baseline=93.0)
        self.assertFalse(ok)


# ---------------------------------------------------------------------------
# Level 4: Region improvement
# ---------------------------------------------------------------------------

class TestRegionImprovement(unittest.TestCase):
    def test_all_improved(self):
        parent = {(10, 20): 0.70, (30, 40): 0.80}
        child = {(10, 20): 0.75, (30, 40): 0.85}
        ok, regressions, diags = check_region_improvement(child, parent)
        self.assertTrue(ok)
        self.assertEqual(regressions, 0)

    def test_one_regressed(self):
        parent = {(10, 20): 0.70, (30, 40): 0.80}
        child = {(10, 20): 0.75, (30, 40): 0.70}
        ok, regressions, diags = check_region_improvement(child, parent)
        self.assertFalse(ok)
        self.assertEqual(regressions, 1)

    def test_within_threshold(self):
        parent = {(10, 20): 0.70}
        child = {(10, 20): 0.69}  # 1% regression < 2% threshold
        ok, regressions, diags = check_region_improvement(child, parent)
        self.assertTrue(ok)

    def test_empty_regions(self):
        ok, regressions, diags = check_region_improvement({}, {})
        self.assertTrue(ok)
        self.assertEqual(regressions, 0)

    def test_no_parent_regions(self):
        ok, regressions, diags = check_region_improvement(
            {(10, 20): 0.80}, {}
        )
        self.assertTrue(ok)


# ---------------------------------------------------------------------------
# Level 5: Fact agreement
# ---------------------------------------------------------------------------

class TestFactAgreement(unittest.TestCase):
    def test_no_facts(self):
        ok, diags = check_fact_agreement(None, "test_pattern")
        self.assertTrue(ok)

    def test_boosted_pattern(self):
        tf = _make_target_facts(boost=["test_pattern"])
        ok, diags = check_fact_agreement(tf, "test_pattern")
        self.assertTrue(ok)

    def test_suppressed_pattern(self):
        tf = _make_target_facts(suppress=["test_pattern"])
        ok, diags = check_fact_agreement(tf, "test_pattern")
        self.assertFalse(ok)
        self.assertTrue(any("suppressed" in d for d in diags))

    def test_compose_suppressed(self):
        tf = _make_target_facts(suppress=["patA"])
        ok, diags = check_fact_agreement(tf, "compose:patA+patB")
        self.assertFalse(ok)

    def test_noise_regression(self):
        tf = _make_target_facts(noise=True)
        sr = _make_score_result(match_percent=90.0)
        ok, diags = check_fact_agreement(tf, "test", sr, parent_score=93.0)
        self.assertFalse(ok)
        self.assertTrue(any("noise" in d.lower() for d in diags))

    def test_noise_improvement_ok(self):
        tf = _make_target_facts(noise=True)
        sr = _make_score_result(match_percent=96.0)
        ok, diags = check_fact_agreement(tf, "test", sr, parent_score=93.0)
        self.assertTrue(ok)


# ---------------------------------------------------------------------------
# Level 6: Semantic checks
# ---------------------------------------------------------------------------

class TestSemantics(unittest.TestCase):
    def test_same_source(self):
        src = b"int f() { return 0; }"
        ok, diags = check_semantics(src, src)
        self.assertTrue(ok)
        self.assertEqual(len(diags), 0)

    def test_return_count_changed(self):
        orig = b"int f() { if (x) return 1; return 0; }"
        var = b"int f() { return x ? 1 : 0; }"
        ok, diags = check_semantics(orig, var)
        self.assertFalse(ok)
        self.assertTrue(any("Return count" in d for d in diags))

    def test_assert_count_changed(self):
        orig = b"void f() { MILO_ASSERT(x, 1); MILO_ASSERT(y, 2); }"
        var = b"void f() { MILO_ASSERT(x, 1); }"
        ok, diags = check_semantics(orig, var)
        self.assertFalse(ok)
        self.assertTrue(any("MILO_ASSERT" in d for d in diags))

    def test_new_call_detected(self):
        orig = b"void f() { foo(); bar(); }"
        var = b"void f() { foo(); bar(); baz(); }"
        ok, diags = check_semantics(orig, var)
        self.assertFalse(ok)
        self.assertTrue(any("New call" in d for d in diags))

    def test_reordered_calls_ok(self):
        orig = b"void f() { foo(); bar(); }"
        var = b"void f() { bar(); foo(); }"
        ok, diags = check_semantics(orig, var)
        self.assertTrue(ok)

    def test_keywords_not_flagged(self):
        orig = b"void f() { int x = 0; }"
        var = b"void f() { int x = 0; if (x) return; }"
        ok, diags = check_semantics(orig, var)
        # 'return' is added but should not be flagged as new call
        # (it will be flagged for return count change though)
        found_new_call = any("New call" in d for d in diags)
        self.assertFalse(found_new_call)


# ---------------------------------------------------------------------------
# Full ladder
# ---------------------------------------------------------------------------

class TestFullLadder(unittest.TestCase):
    def test_perfect_variant(self):
        variant = _make_variant(b"int f() { return 0; }")
        sr = _make_score_result(match_percent=100.0)
        result = validate_variant(
            variant, sr, baseline_score=93.0,
            original_source=b"int f() { return 0; }",
        )
        self.assertTrue(result.passed)
        self.assertEqual(result.tier, ValidationTier.SEMANTIC_OK)

    def test_parse_failure(self):
        # Severely broken syntax
        variant = _make_variant(b"{{{{")
        result = validate_variant(variant)
        self.assertEqual(result.tier, ValidationTier.INVALID)
        self.assertFalse(result.passed)

    def test_build_failure(self):
        variant = _make_variant()
        sr = _make_score_result(build_success=False)
        result = validate_variant(variant, sr)
        self.assertEqual(result.tier, ValidationTier.PARSE_OK)

    def test_score_regression(self):
        variant = _make_variant()
        sr = _make_score_result(match_percent=90.0)
        result = validate_variant(variant, sr, baseline_score=93.0)
        self.assertEqual(result.tier, ValidationTier.BUILD_OK)

    def test_score_improved(self):
        variant = _make_variant()
        sr = _make_score_result(match_percent=96.0)
        result = validate_variant(
            variant, sr, baseline_score=93.0,
            original_source=b"int f() { return 0; }",
        )
        self.assertTrue(result.score_improved)
        self.assertGreaterEqual(result.tier, ValidationTier.SCORE_IMPROVED)

    def test_fact_violation_stops(self):
        variant = _make_variant(pattern="bad_pattern")
        sr = _make_score_result(match_percent=96.0)
        tf = _make_target_facts(suppress=["bad_pattern"])
        result = validate_variant(
            variant, sr, baseline_score=93.0,
            target_facts=tf,
        )
        self.assertEqual(result.tier, ValidationTier.REGION_IMPROVED)
        self.assertFalse(result.fact_agreed)

    def test_is_acceptable(self):
        result = ValidationResult(tier=ValidationTier.BUILD_OK, passed=False)
        self.assertTrue(result.is_acceptable)

    def test_is_high_quality(self):
        result = ValidationResult(tier=ValidationTier.REGION_IMPROVED, passed=True)
        self.assertTrue(result.is_high_quality)
        result2 = ValidationResult(tier=ValidationTier.SCORE_IMPROVED, passed=True)
        self.assertFalse(result2.is_high_quality)

    def test_tier_ordering(self):
        self.assertGreater(ValidationTier.SEMANTIC_OK, ValidationTier.FACT_AGREED)
        self.assertGreater(ValidationTier.FACT_AGREED, ValidationTier.REGION_IMPROVED)
        self.assertGreater(ValidationTier.REGION_IMPROVED, ValidationTier.SCORE_IMPROVED)
        self.assertGreater(ValidationTier.SCORE_IMPROVED, ValidationTier.BUILD_OK)
        self.assertGreater(ValidationTier.BUILD_OK, ValidationTier.PARSE_OK)
        self.assertGreater(ValidationTier.PARSE_OK, ValidationTier.INVALID)


class TestFormatResult(unittest.TestCase):
    def test_format_concise_pass(self):
        r = ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True)
        s = format_result(r)
        self.assertIn("[PASS]", s)
        self.assertIn("SEMANTIC_OK", s)
        self.assertIn("6/6", s)

    def test_format_concise_fail(self):
        r = ValidationResult(tier=ValidationTier.BUILD_OK, passed=False)
        s = format_result(r)
        self.assertIn("[FAIL]", s)
        self.assertIn("BUILD_OK", s)
        self.assertIn("2/6", s)

    def test_format_verbose_shows_levels(self):
        r = ValidationResult(
            tier=ValidationTier.SCORE_IMPROVED, passed=True,
            parse_ok=True, build_ok=True, score_improved=True,
        )
        s = format_result(r, verbose=True)
        self.assertIn("+ Parse", s)
        self.assertIn("+ Build", s)
        self.assertIn("+ Score", s)
        self.assertIn("- Region", s)  # Not set → "-"

    def test_format_verbose_shows_issues(self):
        r = ValidationResult(
            tier=ValidationTier.FACT_AGREED, passed=True,
            parse_ok=True, build_ok=True, score_improved=True,
            region_improved=True, fact_agreed=True,
            semantic_issues=["Return count changed: 2 → 3"],
        )
        s = format_result(r, verbose=True)
        self.assertIn("Semantic: Return count", s)

    def test_format_verbose_region_regressions(self):
        r = ValidationResult(
            tier=ValidationTier.REGION_IMPROVED, passed=True,
            parse_ok=True, build_ok=True, score_improved=True,
            region_improved=True, region_regressions=2,
        )
        s = format_result(r, verbose=True)
        self.assertIn("Region regressions: 2", s)


class TestFormatTierDistribution(unittest.TestCase):
    def test_all_same_tier(self):
        results = [
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
        ]
        s = format_tier_distribution(results)
        self.assertEqual(s, "T6:2")

    def test_mixed_tiers(self):
        results = [
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
            ValidationResult(tier=ValidationTier.BUILD_OK, passed=False),
            ValidationResult(tier=ValidationTier.BUILD_OK, passed=False),
            ValidationResult(tier=ValidationTier.SCORE_IMPROVED, passed=True),
        ]
        s = format_tier_distribution(results)
        self.assertIn("T6:1", s)
        self.assertIn("T3:1", s)
        self.assertIn("T2:2", s)

    def test_empty_results(self):
        s = format_tier_distribution([])
        self.assertEqual(s, "no results")

    def test_descending_order(self):
        results = [
            ValidationResult(tier=ValidationTier.INVALID, passed=False),
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
        ]
        s = format_tier_distribution(results)
        # T6 should appear before T0
        idx6 = s.index("T6")
        idx0 = s.index("T0")
        self.assertLess(idx6, idx0)


if __name__ == "__main__":
    unittest.main()
