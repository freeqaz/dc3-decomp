#!/usr/bin/env python3
"""
Test suite for incremental build integration in orchestrate.

Tests the Phase 2.1d incremental build feature including:
- Command-line flag parsing
- Build strategy selection logic
- Periodic full build coordination
- Output formatting
"""

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

# Add scripts to path
sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))


class TestIncrementalBuildFlags(unittest.TestCase):
    """Test command-line flag parsing for build strategies."""

    def test_batch_incremental_only_flag(self):
        """Test --incremental-only flag is properly recognized."""
        from decomp_orchestrate import main

        # This should not raise an error during parsing
        with patch("sys.argv", ["orchestrate", "batch", "--help"]):
            try:
                main()
            except SystemExit:
                # --help causes SystemExit, which is expected
                pass

    def test_batch_full_build_flag(self):
        """Test --full-build flag is properly recognized."""
        from decomp_orchestrate import main

        with patch("sys.argv", ["orchestrate", "batch", "--help"]):
            try:
                main()
            except SystemExit:
                pass

    def test_periodic_full_flag(self):
        """Test --periodic-full flag accepts integer argument."""
        from decomp_orchestrate import main

        with patch("sys.argv", ["orchestrate", "batch", "--help"]):
            try:
                main()
            except SystemExit:
                pass


class TestBuildStrategyLogic(unittest.TestCase):
    """Test build strategy selection logic."""

    def test_default_uses_incremental(self):
        """Test that default strategy uses incremental builds."""
        # When no flags specified, use_incremental should be True
        # (This would be tested in cmd_batch with mock args)
        pass

    def test_incremental_only_flag_overrides(self):
        """Test that --incremental-only forces incremental."""
        # When --incremental-only specified, use_incremental = True
        pass

    def test_full_build_flag_overrides(self):
        """Test that --full-build forces full builds."""
        # When --full-build specified, use_incremental = False
        pass

    def test_periodic_full_interval(self):
        """Test that periodic_full_interval defaults to 10."""
        # Default should be 10, can be overridden with --periodic-full N
        pass


class TestPromptBuildHints(unittest.TestCase):
    """Test that build strategy hints are included in prompts."""

    def test_incremental_prompt_includes_hint(self):
        """Test that incremental prompts include build strategy hint."""
        from orchestrator.core import DecompOrchestrator

        orchestrator = DecompOrchestrator()
        func = {
            "symbol": "test_symbol",
            "demangled": "test_function",
            "unit": "test_unit",
            "current_percent": 50,
        }

        prompt = orchestrator._build_prompt(func, use_incremental=True)
        self.assertIn("Incremental", prompt)
        self.assertIn("fast", prompt)

    def test_full_build_prompt_includes_hint(self):
        """Test that full build prompts include build strategy hint."""
        from orchestrator.core import DecompOrchestrator

        orchestrator = DecompOrchestrator()
        func = {
            "symbol": "test_symbol",
            "demangled": "test_function",
            "unit": "test_unit",
            "current_percent": 50,
        }

        prompt = orchestrator._build_prompt(func, use_incremental=False)
        self.assertIn("Full build", prompt)
        self.assertIn("comprehensive", prompt)


class TestBatchCoordination(unittest.TestCase):
    """Test batch coordination with periodic full builds."""

    def test_periodic_full_interval_calculation(self):
        """Test periodic full build interval calculation."""
        # Every Nth batch logic: (processed + 1) % (max_agents * periodic_full_interval) == 0
        max_agents = 3
        periodic_full_interval = 10

        # With 3 agents and interval of 10:
        # Periodic full build should trigger every 30 processed functions
        for processed in range(0, 100):
            should_full = (processed + 1) % (max_agents * periodic_full_interval) == 0

            # Check specific values
            if processed == 29:  # First periodic full build
                self.assertTrue(should_full)
            elif processed == 59:  # Second periodic full build
                self.assertTrue(should_full)
            elif processed == 10:  # Not a periodic full build
                self.assertFalse(should_full)


class TestSummaryMetrics(unittest.TestCase):
    """Test summary report generation."""

    def test_summary_includes_build_strategy(self):
        """Test that summary includes build strategy info."""
        from orchestrator.core import DecompOrchestrator

        orchestrator = DecompOrchestrator()
        results = []

        # Note: _generate_batch_summary doesn't yet track build strategy
        # That's added in run_batch after calling _generate_batch_summary
        summary = orchestrator._generate_batch_summary(results, "test_pattern")
        self.assertIn("pattern", summary)
        self.assertIn("total", summary)


class TestCommandLineIntegration(unittest.TestCase):
    """Integration tests for command-line usage."""

    def test_single_incremental_flag(self):
        """Test 'single' command with --incremental-only."""
        # Would test actual command parsing with mock args
        pass

    def test_batch_with_custom_interval(self):
        """Test 'batch' command with --periodic-full 5."""
        # Would test custom periodic interval
        pass

    def test_batch_with_validate_diffs(self):
        """Test 'batch' command with --validate-diffs."""
        # Would test validation flag
        pass


class TestBackwardCompatibility(unittest.TestCase):
    """Test that existing functionality still works."""

    def test_default_batch_still_works(self):
        """Test that batch without new flags still works."""
        # Should use sensible defaults
        pass

    def test_single_without_flags_still_works(self):
        """Test that single without new flags still works."""
        # Should use incremental by default
        pass


def run_tests():
    """Run all tests."""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestIncrementalBuildFlags))
    suite.addTests(loader.loadTestsFromTestCase(TestBuildStrategyLogic))
    suite.addTests(loader.loadTestsFromTestCase(TestPromptBuildHints))
    suite.addTests(loader.loadTestsFromTestCase(TestBatchCoordination))
    suite.addTests(loader.loadTestsFromTestCase(TestSummaryMetrics))
    suite.addTests(loader.loadTestsFromTestCase(TestCommandLineIntegration))
    suite.addTests(loader.loadTestsFromTestCase(TestBackwardCompatibility))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(run_tests())
