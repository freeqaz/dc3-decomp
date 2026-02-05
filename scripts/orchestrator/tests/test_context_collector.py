#!/usr/bin/env python3
"""
Comprehensive unit tests for context_collector.py.

Tests the collect_pre_run_context() function with various scenarios:
- Normal case: function executes and returns correct dict
- Ghidra unavailable: graceful fallback to "(unavailable)"
- No previous attempts: returns "None yet" string
- Xrefs file writing: verifies file created with correct path and format
- Previous attempts formatting: verifies "Attempt N: model X, Y% → Z%" format
- Exception handling: graceful degradation on failures
- Incremental build: verifies incremental=True passed to run_objdiff

Usage:
    python3 -m unittest scripts.orchestrator.test_context_collector -v
    # Or with pytest if available:
    pytest scripts/orchestrator/test_context_collector.py -v
"""

import json
import sqlite3
import sys
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional
from unittest.mock import MagicMock, Mock, patch, mock_open

# Add project to path
PROJECT_DIR = Path(__file__).parent.parent.parent
sys.path.insert(0, str(PROJECT_DIR))

# Import the module we're testing
from scripts.orchestrator.context_collector import (
    collect_pre_run_context,
    extract_key_patterns,
    get_last_attempt,
    get_binary_path,
    extract_callees_from_objdiff,
    resolve_signature,
    resolve_callee_info,
    get_callee_signatures,
    format_callee_signatures,
)


# =============================================================================
# Mock Data Classes and Fixtures
# =============================================================================

@dataclass
class MockObjdiffResult:
    """Mock objdiff result for testing."""
    symbol: str = "?Load@CharMirror@@UAAXAAVBinStream@@@Z"
    fuzzy_match_percent: float = 98.67
    verdict: Dict[str, Any] = None
    analysis: Optional[Dict[str, Any]] = None
    suggestions: List[str] = None
    error: Optional[str] = None

    def __post_init__(self):
        if self.verdict is None:
            self.verdict = {
                "classification": "LIKELY_FIXABLE",
                "explanation": "Some fixable issues found"
            }
        if self.suggestions is None:
            self.suggestions = [
                "Try optimizing register allocation",
                "Check for branch prediction issues"
            ]


class DirectGhidraClientError(Exception):
    """Mock Ghidra client error (mimics real class)."""
    pass


# =============================================================================
# Test Cases
# =============================================================================

class TestCollectPreRunContextSuccess(unittest.TestCase):
    """Test normal case: all context available."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.TemporaryDirectory()
        self.worktree_dir = Path(self.temp_dir.name) / "worktree"
        self.worktree_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = str(PROJECT_DIR)
        self.symbol = "?Load@CharMirror@@UAAXAAVBinStream@@@Z"
        self.unit = "system/char/CharMirror"

    def tearDown(self):
        """Clean up."""
        self.temp_dir.cleanup()

    @patch('scripts.orchestrator.context_collector.get_binary_path')
    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_collect_pre_run_context_success(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff, mock_get_binary):
        """Test successful context collection with all data available."""
        # Setup mocks
        mock_get_binary.return_value = "/fake/path/to/binary.xex"

        objdiff_result = MockObjdiffResult(
            fuzzy_match_percent=98.67,
            verdict={"classification": "LIKELY_FIXABLE"},
            suggestions=["Try if/else swap", "Check register allocation"]
        )
        mock_run_objdiff.return_value = objdiff_result

        # Mock Ghidra client
        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void func() { /* original */ }"
        mock_client.list_cross_references.return_value = (
            ["caller1", "caller2"],
            ["callee1", "callee2"]
        )

        # Mock previous attempts (returns tuple of (formatted_string, count))
        mock_get_attempts.return_value = ("Attempt 1: haiku, 85.5% → 86.2%\nAttempt 2: sonnet, 86.2% → 87.0%", 2)

        # Call function
        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Assertions
        self.assertIsInstance(context, dict)
        self.assertEqual(len(context), 43, "Should have all 43 keys from result dict")
        self.assertAlmostEqual(context["match_percent"], 98.67)
        self.assertEqual(context["verdict"], "LIKELY_FIXABLE")
        self.assertIsInstance(context["key_patterns"], list)
        self.assertIsInstance(context["suggestions"], list)
        self.assertEqual(len(context["suggestions"]), 2)
        # At 98.67% match, decompilation is file-only (not inlined) to save prompt budget
        self.assertIn("file-only", context["decompilation"])
        self.assertIn("Attempt 1", context["previous_attempts"])
        self.assertIn("Attempt 2", context["previous_attempts"])

        # Verify xrefs file was created
        xrefs_path = Path(context["xrefs_path_absolute"])
        self.assertTrue(xrefs_path.exists(), "Xrefs file should be created")
        self.assertIn("xrefs_", xrefs_path.name)

        # Verify xrefs file content
        with open(xrefs_path, 'r') as f:
            content = f.read()
        self.assertIn("Cross-references for", content)
        self.assertIn("Callers (2 total):", content)
        self.assertIn("Callees (2 total):", content)
        self.assertIn("caller1", content)
        self.assertIn("callee1", content)

        # Verify relative path is correct
        self.assertIn("function_analysis", context["xrefs_path_relative"])
        self.assertIn("xrefs_", context["xrefs_path_relative"])

        # Verify preview is populated
        self.assertNotEqual(context["xrefs_preview"], "(unavailable)")
        self.assertIn("Callers", context["xrefs_preview"])

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_incremental_build_flag(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Verify incremental=True is passed to run_objdiff."""
        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void func() {}"
        mock_client.list_cross_references.return_value = ([], [])

        mock_get_attempts.return_value = ("None yet", 0)

        collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Verify run_objdiff was called with incremental=True
        mock_run_objdiff.assert_called_once()
        call_args = mock_run_objdiff.call_args
        self.assertTrue(call_args.kwargs.get('incremental') or call_args[0][-1] == True,
                       "incremental parameter should be True")


class TestCollectContextGhidraUnavailable(unittest.TestCase):
    """Test graceful fallback when Ghidra is unavailable."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.TemporaryDirectory()
        self.worktree_dir = Path(self.temp_dir.name) / "worktree"
        self.worktree_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = str(PROJECT_DIR)
        self.symbol = "?Load@CharMirror@@UAAXAAVBinStream@@@Z"
        self.unit = "system/char/CharMirror"

    def tearDown(self):
        """Clean up."""
        self.temp_dir.cleanup()

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_collect_context_ghidra_unavailable(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Test that function gracefully handles Ghidra initialization failure."""
        # Setup mocks
        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        # Make DirectGhidraClient raise error
        mock_ghidra_class.side_effect = DirectGhidraClientError("Ghidra not available")

        mock_get_attempts.return_value = ("None yet", 0)

        # Call function - should not crash
        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Verify graceful fallback
        self.assertIsInstance(context, dict)
        self.assertEqual(context["decompilation"], "(unavailable)")
        self.assertEqual(context["xrefs_path_absolute"], "(unavailable)")
        self.assertEqual(context["xrefs_path_relative"], "(unavailable)")
        self.assertEqual(context["xrefs_preview"], "(unavailable)")

        # Verify objdiff context is still available
        self.assertNotEqual(context["match_percent"], 0.0)
        self.assertEqual(context["verdict"], "LIKELY_FIXABLE")


class TestCollectContextNoPreviousAttempts(unittest.TestCase):
    """Test handling of functions with no attempt history."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.TemporaryDirectory()
        self.worktree_dir = Path(self.temp_dir.name) / "worktree"
        self.worktree_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = str(PROJECT_DIR)
        self.symbol = "?NewFunction@@UAAXAAVBinStream@@@Z"
        self.unit = "system/new/NewClass"

    def tearDown(self):
        """Clean up."""
        self.temp_dir.cleanup()

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_collect_context_no_previous_attempts(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Test that function handles no previous attempts gracefully."""
        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void func() {}"
        mock_client.list_cross_references.return_value = ([], [])

        # No previous attempts
        mock_get_attempts.return_value = ("None yet", 0)

        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        self.assertEqual(context["previous_attempts"], "None yet")


class TestXrefsFileCreation(unittest.TestCase):
    """Test xrefs file writing and formatting."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.TemporaryDirectory()
        self.worktree_dir = Path(self.temp_dir.name) / "worktree"
        self.worktree_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = str(PROJECT_DIR)
        self.symbol = "?TestFunc@@UAAXXZ"
        self.unit = "system/test/Test"

    def tearDown(self):
        """Clean up."""
        self.temp_dir.cleanup()

    @patch('scripts.orchestrator.context_collector.get_binary_path')
    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_xrefs_file_created(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff, mock_get_binary):
        """Test that xrefs file is created at correct path with correct format."""
        mock_get_binary.return_value = "/fake/path/to/binary.xex"

        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        # Create mock Ghidra client with cross-references
        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void test() {}"
        callers = ["Caller1", "Caller2", "Caller3"]
        callees = ["Callee1", "Callee2"]
        mock_client.list_cross_references.return_value = (callers, callees)

        mock_get_attempts.return_value = ("None yet", 0)

        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Verify file exists
        xrefs_path = Path(context["xrefs_path_absolute"])
        self.assertTrue(xrefs_path.exists())

        # Verify file path structure
        self.assertEqual(xrefs_path.parent.name, "function_analysis")
        self.assertIn("xrefs_", xrefs_path.name)
        self.assertIn(self.symbol, xrefs_path.name)

        # Verify file content
        with open(xrefs_path, 'r') as f:
            content = f.read()

        self.assertIn("Cross-references for", content)
        self.assertIn("=" * 80, content)
        self.assertIn("Callers (3 total):", content)
        self.assertIn("Callees (2 total):", content)

        for caller in callers:
            self.assertIn(caller, content)
        for callee in callees:
            self.assertIn(callee, content)

        # Verify relative path
        expected_relative = f"function_analysis/xrefs_{self.symbol}.txt"
        self.assertEqual(context["xrefs_path_relative"], expected_relative)

    @patch('scripts.orchestrator.context_collector.get_binary_path')
    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_xrefs_preview(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff, mock_get_binary):
        """Test that xrefs preview contains first 20 lines of file."""
        mock_get_binary.return_value = "/fake/path/to/binary.xex"

        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void test() {}"
        # Create many callers and callees to exceed 20 lines
        callers = [f"Caller_{i}" for i in range(15)]
        callees = [f"Callee_{i}" for i in range(15)]
        mock_client.list_cross_references.return_value = (callers, callees)

        mock_get_attempts.return_value = ("None yet", 0)

        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Preview should be first 20 lines (not full file)
        preview = context["xrefs_preview"]
        lines = preview.split('\n')
        self.assertLessEqual(len(lines), 21, "Preview should be first 20 lines")
        self.assertGreater(len(lines), 0)
        self.assertIn("Cross-references for", preview)


class TestPreviousAttemptsFormatting(unittest.TestCase):
    """Test previous attempts formatting."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.TemporaryDirectory()
        self.worktree_dir = Path(self.temp_dir.name) / "worktree"
        self.worktree_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = str(PROJECT_DIR)
        self.symbol = "?TestFunc@@UAAXXZ"
        self.unit = "system/test/Test"

    def tearDown(self):
        """Clean up."""
        self.temp_dir.cleanup()

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_previous_attempts_formatting(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Test that previous attempts are formatted correctly."""
        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void test() {}"
        mock_client.list_cross_references.return_value = ([], [])

        # Format attempts as the real function would
        formatted_attempts = (
            "Attempt 1: haiku, 85.5% → 86.2%\n"
            "Attempt 2: sonnet, 86.2% → 87.5%\n"
            "Attempt 3: opus, 87.5% → 88.0%"
        )
        mock_get_attempts.return_value = (formatted_attempts, 3)

        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Verify format
        attempts_str = context["previous_attempts"]
        self.assertIn("Attempt 1:", attempts_str)
        self.assertIn("haiku", attempts_str)
        self.assertIn("85.5%", attempts_str)
        self.assertIn("86.2%", attempts_str)
        self.assertIn("→", attempts_str)
        self.assertIn("\n", attempts_str)  # Multiple attempts separated by newlines


class TestExceptionHandling(unittest.TestCase):
    """Test exception handling and graceful degradation."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.TemporaryDirectory()
        self.worktree_dir = Path(self.temp_dir.name) / "worktree"
        self.worktree_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = str(PROJECT_DIR)
        self.symbol = "?TestFunc@@UAAXXZ"
        self.unit = "system/test/Test"

    def tearDown(self):
        """Clean up."""
        self.temp_dir.cleanup()

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_collect_context_objdiff_failure(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Test graceful handling of run_objdiff failure."""
        # Make objdiff fail
        mock_run_objdiff.side_effect = Exception("objdiff crashed")

        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void test() {}"
        mock_client.list_cross_references.return_value = ([], [])

        mock_get_attempts.return_value = ("None yet", 0)

        # Should still return a dict (graceful degradation)
        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        self.assertIsInstance(context, dict)
        # Should have default values from exception
        self.assertEqual(context["match_percent"], 0.0)
        self.assertEqual(context["verdict"], "UNKNOWN")

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_collect_context_xrefs_after_client_error(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Test handling when client initialization raises DirectGhidraClientError.

        When DirectGhidraClientError is raised during client init, the entire
        Ghidra block is skipped (not just decompilation). This verifies graceful
        fallback to unavailable.
        """
        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        # Make DirectGhidraClient initialization fail
        mock_ghidra_class.side_effect = DirectGhidraClientError("Client init failed")

        mock_get_attempts.return_value = ("None yet", 0)

        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # When client init fails, everything should be unavailable
        self.assertEqual(context["decompilation"], "(unavailable)")
        self.assertEqual(context["xrefs_path_absolute"], "(unavailable)")
        self.assertEqual(context["xrefs_path_relative"], "(unavailable)")
        self.assertEqual(context["xrefs_preview"], "(unavailable)")

        # But objdiff data should still be available
        self.assertGreater(context["match_percent"], 0)


class TestExtractKeyPatterns(unittest.TestCase):
    """Test key pattern extraction from objdiff results."""

    def test_extract_patterns_with_classification(self):
        """Test pattern extraction with classification."""
        objdiff_result = MockObjdiffResult(
            verdict={"classification": "AT_LIMIT"}
        )
        patterns = extract_key_patterns(objdiff_result)
        self.assertIsInstance(patterns, list)
        self.assertIn("Classification: AT_LIMIT", patterns)

    def test_extract_patterns_with_assert_revs(self):
        """Test pattern extraction with ASSERT_REVS explanation."""
        objdiff_result = MockObjdiffResult(
            verdict={"classification": "AT_LIMIT", "explanation": "ASSERT_REVS detected"}
        )
        patterns = extract_key_patterns(objdiff_result)
        self.assertTrue(any("ASSERT_REVS" in p for p in patterns))

    def test_extract_patterns_with_ltcg(self):
        """Test pattern extraction with LTCG explanation."""
        objdiff_result = MockObjdiffResult(
            verdict={"classification": "MAYBE_FIXABLE", "explanation": "LTCG optimizations applied"}
        )
        patterns = extract_key_patterns(objdiff_result)
        self.assertTrue(any("LTCG" in p for p in patterns))

    def test_extract_patterns_empty(self):
        """Test pattern extraction with no patterns."""
        objdiff_result = MockObjdiffResult(verdict={})
        patterns = extract_key_patterns(objdiff_result)
        self.assertIsInstance(patterns, list)


class TestContextDictStructure(unittest.TestCase):
    """Test the structure and keys of returned context dict."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.TemporaryDirectory()
        self.worktree_dir = Path(self.temp_dir.name) / "worktree"
        self.worktree_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = str(PROJECT_DIR)
        self.symbol = "?TestFunc@@UAAXXZ"
        self.unit = "system/test/Test"

    def tearDown(self):
        """Clean up."""
        self.temp_dir.cleanup()

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_context_dict_has_all_keys(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Test that context dict contains all expected keys."""
        objdiff_result = MockObjdiffResult()
        mock_run_objdiff.return_value = objdiff_result

        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void test() {}"
        mock_client.list_cross_references.return_value = ([], [])

        mock_get_attempts.return_value = ("None yet", 0)

        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Exact set of keys from the result dict initializer.
        # ghidra_file is conditionally added so not included here.
        expected_keys = {
            "match_percent",
            "verdict",
            "key_patterns",
            "suggestions",
            "previous_attempts",
            "previous_attempts_count",
            "decompilation",
            "ghidra_file_path_relative",
            "rb3_reference",
            "rb3_file_path_relative",
            "m2c_decompilation",
            "m2c_file_path",
            "m2c_file_path_relative",
            "m2c_line_count",
            "m2c_method",
            "xrefs_path_absolute",
            "xrefs_path_relative",
            "xrefs_preview",
            "source_file_absolute",
            "header_file_absolute",
            "header_contents",
            "header_line_count",
            "source_contents",
            "source_window_start_line",
            "source_window_end_line",
            "source_total_lines",
            "objdiff_file",
            "objdiff_file_absolute",
            "objdiff_line_count",
            "objdiff_preview",
            "enrichment_flags",
            "pattern_classification",
            "pattern_classification_summary",
            "function_type",
            "function_type_guidance",
            "class_layout",
            "class_layout_summary",
            "attempt_diffs",
            "attempt_diffs_summary",
            "matched_siblings",
            "matched_siblings_summary",
            "callee_signatures",
            "callee_signatures_summary",
        }
        context_keys = set(context.keys())
        missing = expected_keys - context_keys
        extra = context_keys - expected_keys
        self.assertEqual(missing, set(),
                        f"Missing keys: {missing}")
        # ghidra_file may be present conditionally; filter it from extras
        unexpected_extra = extra - {"ghidra_file"}
        self.assertEqual(unexpected_extra, set(),
                        f"Unexpected extra keys: {unexpected_extra}")

    @patch('scripts.orchestrator.context_collector.run_objdiff')
    @patch('scripts.orchestrator.context_collector.DirectGhidraClient')
    @patch('scripts.orchestrator.context_collector.get_last_attempt')
    def test_context_value_types(self, mock_get_attempts, mock_ghidra_class, mock_run_objdiff):
        """Test that context values have correct types."""
        objdiff_result = MockObjdiffResult(
            fuzzy_match_percent=99.5,
            suggestions=["Suggestion 1", "Suggestion 2"]
        )
        mock_run_objdiff.return_value = objdiff_result

        mock_client = MagicMock()
        mock_ghidra_class.return_value = mock_client
        mock_client.decompile_function.return_value = "void test() {}"
        mock_client.list_cross_references.return_value = ([], [])

        mock_get_attempts.return_value = ("None yet", 0)

        context = collect_pre_run_context(
            symbol=self.symbol,
            unit=self.unit,
            project_dir=self.project_dir,
            worktree_dir=str(self.worktree_dir)
        )

        # Verify types
        self.assertIsInstance(context["match_percent"], float)
        self.assertIsInstance(context["verdict"], str)
        self.assertIsInstance(context["key_patterns"], list)
        self.assertIsInstance(context["suggestions"], list)
        self.assertIsInstance(context["previous_attempts"], str)
        self.assertIsInstance(context["decompilation"], str)
        self.assertIsInstance(context["xrefs_path_absolute"], str)
        self.assertIsInstance(context["xrefs_path_relative"], str)
        self.assertIsInstance(context["xrefs_preview"], str)

        # Verify values are non-empty (except when unavailable)
        self.assertGreater(context["match_percent"], 0)
        self.assertGreater(len(context["verdict"]), 0)
        self.assertGreater(len(context["key_patterns"]), 0)
        self.assertEqual(len(context["suggestions"]), 2)


# =============================================================================
# Experiment 6: Callee Signatures Tests
# =============================================================================

class TestCalleeSignatures(unittest.TestCase):
    """Test callee signature extraction and resolution (Experiment 6)."""

    def test_extract_callees_from_objdiff_basic(self):
        """Test extracting callees from objdiff JSON with bl instructions."""
        objdiff_json = {
            'instructions': [
                {'target': {'opcode': 'mflr', 'args': 'r12'}},
                {'target': {'opcode': 'bl', 'args': '__savegprlr_21',
                            'typed_args': [{'type': 'Symbol', 'value': '__savegprlr_21'}]}},
                {'target': {'opcode': 'bl', 'args': '?Load@BinStream@@QAAXPAXH@Z',
                            'typed_args': [{'type': 'Symbol', 'value': '?Load@BinStream@@QAAXPAXH@Z'}]}},
                {'target': {'opcode': 'stw', 'args': 'r3, 0x10(r1)'}},
                {'target': {'opcode': 'bl', 'args': '?Fail@Debug@@QAAXPBDPAX@Z',
                            'typed_args': [{'type': 'Symbol', 'value': '?Fail@Debug@@QAAXPBDPAX@Z'}]}},
            ]
        }

        callees = extract_callees_from_objdiff(objdiff_json)

        # Should find 3 bl instructions
        self.assertEqual(len(callees), 3)
        self.assertIn('__savegprlr_21', callees)
        self.assertIn('?Load@BinStream@@QAAXPAXH@Z', callees)
        self.assertIn('?Fail@Debug@@QAAXPBDPAX@Z', callees)

    def test_extract_callees_from_objdiff_deduplication(self):
        """Test that duplicate callees are deduplicated, preserving first occurrence."""
        objdiff_json = {
            'instructions': [
                {'target': {'opcode': 'bl', 'args': '?Func@Class@@QAAXXZ',
                            'typed_args': [{'type': 'Symbol', 'value': '?Func@Class@@QAAXXZ'}]}},
                {'target': {'opcode': 'bl', 'args': '?Other@Class@@QAAXXZ',
                            'typed_args': [{'type': 'Symbol', 'value': '?Other@Class@@QAAXXZ'}]}},
                {'target': {'opcode': 'bl', 'args': '?Func@Class@@QAAXXZ',
                            'typed_args': [{'type': 'Symbol', 'value': '?Func@Class@@QAAXXZ'}]}},
            ]
        }

        callees = extract_callees_from_objdiff(objdiff_json)

        # Should deduplicate
        self.assertEqual(len(callees), 2)
        # Order should be preserved (first occurrence)
        self.assertEqual(callees[0], '?Func@Class@@QAAXXZ')
        self.assertEqual(callees[1], '?Other@Class@@QAAXXZ')

    def test_extract_callees_from_objdiff_empty(self):
        """Test extracting callees from objdiff JSON with no bl instructions."""
        objdiff_json = {
            'instructions': [
                {'target': {'opcode': 'mflr', 'args': 'r12'}},
                {'target': {'opcode': 'stw', 'args': 'r3, 0x10(r1)'}},
            ]
        }

        callees = extract_callees_from_objdiff(objdiff_json)
        self.assertEqual(len(callees), 0)

    def test_extract_callees_fallback_to_args(self):
        """Test fallback to args string when typed_args is missing."""
        objdiff_json = {
            'instructions': [
                {'target': {'opcode': 'bl', 'args': '?Func@Class@@QAAXXZ'}},
            ]
        }

        callees = extract_callees_from_objdiff(objdiff_json)
        self.assertEqual(len(callees), 1)
        self.assertEqual(callees[0], '?Func@Class@@QAAXXZ')

    def test_resolve_signature_msvc_method(self):
        """Test resolving a standard MSVC method symbol."""
        # Use a symbol that won't be in the database
        sig = resolve_signature('?TestMethod@TestClass@@QAAXXZ', '/nonexistent')
        self.assertEqual(sig, 'TestClass::TestMethod')

    def test_resolve_signature_msvc_constructor(self):
        """Test resolving a constructor symbol."""
        sig = resolve_signature('??0TestClass@@QAA@XZ', '/nonexistent')
        self.assertEqual(sig, 'TestClass::TestClass')

    def test_resolve_signature_msvc_destructor(self):
        """Test resolving a destructor symbol."""
        sig = resolve_signature('??1TestClass@@UAA@XZ', '/nonexistent')
        self.assertEqual(sig, 'TestClass::~TestClass')

    def test_resolve_signature_free_function(self):
        """Test resolving a free function (no class)."""
        sig = resolve_signature('?GlobalFunc@@YAXXZ', '/nonexistent')
        self.assertEqual(sig, 'GlobalFunc')

    def test_resolve_signature_intrinsic_skipped(self):
        """Test that compiler intrinsics are skipped."""
        sig = resolve_signature('__savegprlr_21', '/nonexistent')
        self.assertIsNone(sig)

    def test_get_callee_signatures_no_objdiff(self):
        """Test get_callee_signatures when objdiff JSON is not available."""
        result = get_callee_signatures('test_symbol', None, '.')

        self.assertEqual(result['count'], 0)
        self.assertEqual(result['summary'], '(objdiff data not available)')
        self.assertEqual(result['signatures'], [])

    def test_get_callee_signatures_no_callees(self):
        """Test get_callee_signatures when no bl instructions found."""
        objdiff_json = {
            'instructions': [
                {'target': {'opcode': 'stw', 'args': 'r3, 0x10(r1)'}},
            ]
        }

        result = get_callee_signatures('test_symbol', objdiff_json, '.')

        self.assertEqual(result['count'], 0)
        self.assertEqual(result['summary'], '(no callees found)')

    def test_get_callee_signatures_with_callees(self):
        """Test get_callee_signatures with resolvable callees."""
        objdiff_json = {
            'instructions': [
                {'target': {'opcode': 'bl', 'args': '?Load@BinStream@@QAAXPAXH@Z',
                            'typed_args': [{'type': 'Symbol', 'value': '?Load@BinStream@@QAAXPAXH@Z'}]}},
                {'target': {'opcode': 'bl', 'args': '?Fail@Debug@@QAAXPBDPAX@Z',
                            'typed_args': [{'type': 'Symbol', 'value': '?Fail@Debug@@QAAXPBDPAX@Z'}]}},
                # This should be skipped (intrinsic)
                {'target': {'opcode': 'bl', 'args': '__savegprlr_21',
                            'typed_args': [{'type': 'Symbol', 'value': '__savegprlr_21'}]}},
            ]
        }

        result = get_callee_signatures('test_symbol', objdiff_json, '/nonexistent')

        self.assertEqual(result['count'], 2)
        self.assertIn('Found 2 callee signatures', result['summary'])
        # Verify signatures are resolved
        sigs = [s['signature'] for s in result['signatures']]
        self.assertIn('BinStream::Load', sigs)
        self.assertIn('Debug::Fail', sigs)

    def test_format_callee_signatures_empty(self):
        """Test formatting when no callees found."""
        sig_data = {
            'count': 0,
            'signatures': [],
            'summary': '(no callees found)'
        }

        result = format_callee_signatures(sig_data)
        self.assertEqual(result, '(no callees found)')

    def test_format_callee_signatures_with_data(self):
        """Test formatting callee signatures for prompt injection."""
        sig_data = {
            'count': 2,
            'implemented_count': 0,
            'signatures': [
                {'symbol': '?Load@BinStream@@QAAXPAXH@Z', 'signature': 'BinStream::Load'},
                {'symbol': '?Fail@Debug@@QAAXPBDPAX@Z', 'signature': 'Debug::Fail'},
            ],
            'summary': 'Found 2 callee signatures'
        }

        result = format_callee_signatures(sig_data)

        self.assertIn('## Called Function Signatures', result)
        self.assertIn('Found 2 callees with resolved signatures', result)
        self.assertIn('`BinStream::Load`', result)
        self.assertIn('`Debug::Fail`', result)
        self.assertIn('**Tip**:', result)

    def test_format_callee_signatures_with_source_locations(self):
        """Test formatting callee signatures including source locations."""
        sig_data = {
            'count': 3,
            'implemented_count': 2,
            'signatures': [
                {
                    'symbol': '?Load@CharTest@@QAAXH@Z',
                    'signature': 'CharTest::Load',
                    'match_percent': 100.0,
                    'source_location': 'src/system/char/CharTest.cpp:42',
                },
                {
                    'symbol': '?Save@CharTest@@QAAXH@Z',
                    'signature': 'CharTest::Save',
                    'match_percent': 100.0,
                    'source_location': 'src/system/char/CharTest.cpp:58',
                },
                {
                    'symbol': '?Other@CharTest@@QAAXH@Z',
                    'signature': 'CharTest::Other',
                    'match_percent': 85.5,
                },
            ],
            'summary': 'Found 3 callee signatures (2 with source locations)'
        }

        result = format_callee_signatures(sig_data)

        self.assertIn('## Called Function Signatures', result)
        self.assertIn('2 with 100% match and source location', result)
        self.assertIn('`CharTest::Load` — **100%** at `src/system/char/CharTest.cpp:42`', result)
        self.assertIn('`CharTest::Save` — **100%** at `src/system/char/CharTest.cpp:58`', result)
        self.assertIn('`CharTest::Other` — 86%', result)  # Rounded
        self.assertIn('fully implemented', result)

    def test_resolve_callee_info_returns_dict(self):
        """Test that resolve_callee_info returns proper dict structure."""
        info = resolve_callee_info('?TestMethod@TestClass@@QAAXXZ', '/nonexistent')

        self.assertIsInstance(info, dict)
        self.assertIn('signature', info)
        self.assertIn('match_percent', info)
        self.assertIn('source_location', info)
        self.assertEqual(info['signature'], 'TestClass::TestMethod')
        self.assertIsNone(info['match_percent'])  # Not in database
        self.assertIsNone(info['source_location'])  # Not 100%

    def test_resolve_callee_info_intrinsic_returns_none(self):
        """Test that compiler intrinsics return None."""
        info = resolve_callee_info('__savegprlr_21', '/nonexistent')
        self.assertIsNone(info)

    def test_get_callee_signatures_includes_implemented_count(self):
        """Test that get_callee_signatures tracks implemented functions."""
        objdiff_json = {
            'instructions': [
                {'target': {'opcode': 'bl', 'args': '?Func@Class@@QAAXXZ',
                            'typed_args': [{'type': 'Symbol', 'value': '?Func@Class@@QAAXXZ'}]}},
            ]
        }

        result = get_callee_signatures('test_symbol', objdiff_json, '/nonexistent')

        self.assertIn('implemented_count', result)
        self.assertIsInstance(result['implemented_count'], int)


# =============================================================================
# Run Tests
# =============================================================================

if __name__ == "__main__":
    # Run with verbose output
    unittest.main(verbosity=2)
