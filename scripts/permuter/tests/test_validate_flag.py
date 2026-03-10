"""Tests for the --validate flag integration across hill_climber, beam_search, scan_and_permute.

Tests that:
1. The flag exists and defaults to True in all CLIs
2. ValidationResult objects are properly collected and distributed
3. Tier names appear in output when validate=True
4. The distribution dict is populated correctly
5. format_tier_distribution works with HillClimbResult.validation_distribution
"""

from __future__ import annotations

import argparse
import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.types import HillClimbResult, RoundResult
from scripts.permuter.validator import (
    ValidationResult,
    ValidationTier,
    format_tier_distribution,
)


# ---------------------------------------------------------------------------
# CLI flag defaults
# ---------------------------------------------------------------------------

class TestHillClimberCLIValidateFlag(unittest.TestCase):
    """Test that --validate flag exists and defaults to True in hill_climber CLI."""

    def test_validate_default_on(self):
        from scripts.permuter.hill_climber import parse_args
        with patch("sys.argv", ["prog", "--symbol", "test"]):
            args = parse_args()
        self.assertTrue(args.validate)

    def test_no_validate_disables(self):
        from scripts.permuter.hill_climber import parse_args
        with patch("sys.argv", ["prog", "--symbol", "test", "--no-validate"]):
            args = parse_args()
        self.assertFalse(args.validate)

    def test_validate_explicit_on(self):
        from scripts.permuter.hill_climber import parse_args
        with patch("sys.argv", ["prog", "--symbol", "test", "--validate"]):
            args = parse_args()
        self.assertTrue(args.validate)


class TestBeamSearchCLIValidateFlag(unittest.TestCase):
    """Test that --validate flag exists and defaults to True in beam_search CLI."""

    def test_validate_default_on(self):
        from scripts.permuter.beam_search import parse_args
        with patch("sys.argv", ["prog", "--symbol", "test"]):
            args = parse_args()
        self.assertTrue(args.validate)

    def test_no_validate_disables(self):
        from scripts.permuter.beam_search import parse_args
        with patch("sys.argv", ["prog", "--symbol", "test", "--no-validate"]):
            args = parse_args()
        self.assertFalse(args.validate)


class TestScanAndPermuteCLIValidateFlag(unittest.TestCase):
    """Test that --validate flag exists and defaults to True in scan_and_permute CLI."""

    def test_validate_default_on(self):
        from scripts.permuter.scan_and_permute import parse_args
        with patch("sys.argv", ["prog"]):
            args = parse_args()
        self.assertTrue(args.validate)

    def test_no_validate_disables(self):
        from scripts.permuter.scan_and_permute import parse_args
        with patch("sys.argv", ["prog", "--no-validate"]):
            args = parse_args()
        self.assertFalse(args.validate)


# ---------------------------------------------------------------------------
# HillClimbResult validation_distribution field
# ---------------------------------------------------------------------------

class TestHillClimbResultValidationDistribution(unittest.TestCase):
    """Test that HillClimbResult has and uses validation_distribution correctly."""

    def test_default_empty(self):
        result = HillClimbResult(
            symbol="test",
            function_name="Foo::Bar",
            source_path="test.cpp",
            initial_percent=90.0,
            final_percent=95.0,
            total_delta=5.0,
            rounds=[],
            stopped_reason="perfect",
            elapsed_seconds=1.0,
        )
        self.assertEqual(result.validation_distribution, {})

    def test_populated_distribution(self):
        result = HillClimbResult(
            symbol="test",
            function_name="Foo::Bar",
            source_path="test.cpp",
            initial_percent=90.0,
            final_percent=95.0,
            total_delta=5.0,
            rounds=[],
            stopped_reason="perfect",
            elapsed_seconds=1.0,
            validation_distribution={6: 3, 3: 5, 2: 8},
        )
        self.assertEqual(result.validation_distribution[6], 3)
        self.assertEqual(result.validation_distribution[3], 5)
        self.assertEqual(result.validation_distribution[2], 8)

    def test_distribution_from_validation_results(self):
        """Simulate building distribution from a list of ValidationResults."""
        results = [
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
            ValidationResult(tier=ValidationTier.BUILD_OK, passed=False),
            ValidationResult(tier=ValidationTier.SCORE_IMPROVED, passed=True),
            ValidationResult(tier=ValidationTier.INVALID, passed=False),
        ]
        dist: dict[int, int] = {}
        for vr in results:
            t = int(vr.tier)
            dist[t] = dist.get(t, 0) + 1

        self.assertEqual(dist, {6: 2, 2: 1, 3: 1, 0: 1})


# ---------------------------------------------------------------------------
# Tier name display
# ---------------------------------------------------------------------------

class TestTierNameDisplay(unittest.TestCase):
    """Test that tier names are correctly formatted."""

    def test_tier_names_map(self):
        """All ValidationTier values have human-readable names."""
        expected = {
            0: "INVALID",
            1: "PARSE_OK",
            2: "BUILD_OK",
            3: "SCORE_IMPROVED",
            4: "REGION_IMPROVED",
            5: "FACT_AGREED",
            6: "SEMANTIC_OK",
        }
        for tier in ValidationTier:
            self.assertIn(int(tier), expected)
            self.assertEqual(tier.name, expected[int(tier)])

    def test_format_tier_distribution_with_dict(self):
        """format_tier_distribution works with ValidationResult list."""
        results = [
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
            ValidationResult(tier=ValidationTier.SEMANTIC_OK, passed=True),
            ValidationResult(tier=ValidationTier.BUILD_OK, passed=False),
        ]
        s = format_tier_distribution(results)
        self.assertIn("T6:2", s)
        self.assertIn("T2:1", s)


# ---------------------------------------------------------------------------
# validate_batch integration
# ---------------------------------------------------------------------------

class TestValidateBatchIntegration(unittest.TestCase):
    """Test that validate_batch produces correct results for a batch of variants."""

    def test_batch_parallel_results(self):
        from scripts.permuter.validator import validate_batch

        variants = []
        score_results = []
        for i in range(3):
            v = MagicMock()
            v.source = b"int f() { return 0; }"
            v.pattern_name = "test"
            variants.append(v)

            sr = MagicMock()
            sr.build_success = True
            sr.match_percent = 95.0 + i
            sr.error = None
            sr.variant = v
            score_results.append(sr)

        results = validate_batch(
            variants, score_results,
            baseline_score=93.0,
            original_source=b"int f() { return 0; }",
        )

        self.assertEqual(len(results), 3)
        for r in results:
            self.assertIsInstance(r, ValidationResult)
            self.assertGreaterEqual(int(r.tier), int(ValidationTier.SCORE_IMPROVED))

    def test_batch_with_build_failure(self):
        from scripts.permuter.validator import validate_batch

        v1 = MagicMock()
        v1.source = b"int f() { return 0; }"
        v1.pattern_name = "test"

        v2 = MagicMock()
        v2.source = b"int f() { return 0; }"
        v2.pattern_name = "test"

        sr1 = MagicMock()
        sr1.build_success = True
        sr1.match_percent = 95.0
        sr1.error = None
        sr1.variant = v1

        sr2 = MagicMock()
        sr2.build_success = False
        sr2.match_percent = 0.0
        sr2.error = "compile error"
        sr2.variant = v2

        results = validate_batch(
            [v1, v2], [sr1, sr2],
            baseline_score=93.0,
            original_source=b"int f() { return 0; }",
        )

        self.assertEqual(len(results), 2)
        # First should pass build
        self.assertGreaterEqual(int(results[0].tier), int(ValidationTier.BUILD_OK))
        # Second should fail at build
        self.assertEqual(results[1].tier, ValidationTier.PARSE_OK)


# ---------------------------------------------------------------------------
# hill_climb validate parameter
# ---------------------------------------------------------------------------

class TestHillClimbValidateParam(unittest.TestCase):
    """Test that hill_climb function accepts the validate parameter."""

    def test_signature_has_validate(self):
        import inspect
        from scripts.permuter.hill_climber import hill_climb
        sig = inspect.signature(hill_climb)
        self.assertIn("validate", sig.parameters)
        # Default should be True
        self.assertEqual(sig.parameters["validate"].default, True)


class TestBeamSearchValidateParam(unittest.TestCase):
    """Test that beam_search function accepts the validate parameter."""

    def test_signature_has_validate(self):
        import inspect
        from scripts.permuter.beam_search import beam_search
        sig = inspect.signature(beam_search)
        self.assertIn("validate", sig.parameters)
        # Default should be True
        self.assertEqual(sig.parameters["validate"].default, True)


# ---------------------------------------------------------------------------
# Output format for print_result with validation_distribution
# ---------------------------------------------------------------------------

class TestPrintResultValidation(unittest.TestCase):
    """Test that _print_result shows validation tier and distribution."""

    def test_print_result_shows_tier_dist(self):
        from io import StringIO
        from scripts.permuter.hill_climber import _print_result

        result = HillClimbResult(
            symbol="test",
            function_name="Foo::Bar",
            source_path="test.cpp",
            initial_percent=90.0,
            final_percent=95.0,
            total_delta=5.0,
            rounds=[],
            stopped_reason="plateau",
            elapsed_seconds=10.0,
            validation_tier=6,
            validation_distribution={6: 3, 3: 5, 2: 8},
        )

        captured = StringIO()
        old_stderr = sys.stderr
        try:
            sys.stderr = captured
            _print_result(result)
        finally:
            sys.stderr = old_stderr

        output = captured.getvalue()
        self.assertIn("SEMANTIC_OK", output)
        self.assertIn("Tier dist:", output)
        self.assertIn("SEMANTIC_OK:3", output)
        self.assertIn("SCORE_IMPROVED:5", output)
        self.assertIn("BUILD_OK:8", output)

    def test_print_result_no_dist_when_empty(self):
        from io import StringIO
        from scripts.permuter.hill_climber import _print_result

        result = HillClimbResult(
            symbol="test",
            function_name="Foo::Bar",
            source_path="test.cpp",
            initial_percent=90.0,
            final_percent=90.0,
            total_delta=0.0,
            rounds=[],
            stopped_reason="plateau",
            elapsed_seconds=5.0,
        )

        captured = StringIO()
        old_stderr = sys.stderr
        try:
            sys.stderr = captured
            _print_result(result)
        finally:
            sys.stderr = old_stderr

        output = captured.getvalue()
        self.assertNotIn("Tier dist:", output)


if __name__ == "__main__":
    unittest.main()
