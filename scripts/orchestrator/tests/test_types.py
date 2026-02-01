"""Tests for orchestrator type dataclasses."""

import unittest

from scripts.orchestrator.types import (
    AgentRunConfig,
    AgentRunResult,
    SessionResult,
    DEFAULT_DECOMP_TOOLS,
)
from pathlib import Path


class TestAgentRunConfig(unittest.TestCase):
    """Tests for AgentRunConfig dataclass."""

    def test_effective_tools_returns_default_when_none(self):
        config = AgentRunConfig(
            session_id="test-1",
            worktree=Path("/tmp/wt"),
            prompt="do stuff",
            model="haiku",
        )
        self.assertEqual(config.effective_tools, list(DEFAULT_DECOMP_TOOLS))

    def test_effective_tools_returns_custom_when_set(self):
        custom = ["Read", "Write"]
        config = AgentRunConfig(
            session_id="test-1",
            worktree=Path("/tmp/wt"),
            prompt="do stuff",
            model="haiku",
            allowed_tools=custom,
        )
        self.assertEqual(config.effective_tools, custom)

    def test_effective_tools_returns_copy(self):
        """Modifying effective_tools should not affect the config."""
        config = AgentRunConfig(
            session_id="test-1",
            worktree=Path("/tmp/wt"),
            prompt="do stuff",
            model="haiku",
        )
        tools = config.effective_tools
        tools.append("FakeTool")
        self.assertNotIn("FakeTool", config.effective_tools)

    def test_default_values(self):
        config = AgentRunConfig(
            session_id="test-1",
            worktree=Path("/tmp/wt"),
            prompt="do stuff",
            model="haiku",
        )
        self.assertTrue(config.verbose)
        self.assertEqual(config.max_turns, 300)
        self.assertIsNone(config.allowed_tools)
        self.assertIsNone(config.disallowed_tools)


class TestAgentRunResult(unittest.TestCase):
    """Tests for AgentRunResult dataclass."""

    def test_succeeded_true_for_complete(self):
        result = AgentRunResult(
            exit_code=0, status="complete", percent=85.0,
            notes="done", verdict="LIKELY_FIXABLE",
        )
        self.assertTrue(result.succeeded)

    def test_succeeded_false_for_error(self):
        result = AgentRunResult(
            exit_code=1, status="error", percent=None,
            notes="failed", verdict=None,
        )
        self.assertFalse(result.succeeded)

    def test_succeeded_false_for_unknown_status(self):
        result = AgentRunResult(
            exit_code=0, status="unknown", percent=None,
            notes="", verdict=None,
        )
        self.assertFalse(result.succeeded)

    def test_succeeded_false_for_nonzero_exit(self):
        result = AgentRunResult(
            exit_code=1, status="complete", percent=100.0,
            notes="done", verdict="COMPLETE",
        )
        self.assertFalse(result.succeeded)

    def test_has_cost_data(self):
        result = AgentRunResult(
            exit_code=0, status="complete", percent=85.0,
            notes="", verdict=None, total_cost_usd=0.05,
        )
        self.assertTrue(result.has_cost_data)

    def test_has_cost_data_false(self):
        result = AgentRunResult(
            exit_code=0, status="complete", percent=85.0,
            notes="", verdict=None,
        )
        self.assertFalse(result.has_cost_data)

    def test_merge_cost_adds_values(self):
        first = AgentRunResult(
            exit_code=0, status="complete", percent=85.0,
            notes="", verdict=None,
            total_cost_usd=0.10, duration_ms=5000,
            usage={"input_tokens": 1000, "output_tokens": 500},
        )
        second = AgentRunResult(
            exit_code=0, status="complete", percent=86.0,
            notes="", verdict=None,
            total_cost_usd=0.02, duration_ms=1000,
            usage={"input_tokens": 200, "output_tokens": 100},
        )
        first.merge_cost(second)
        self.assertAlmostEqual(first.total_cost_usd, 0.12)
        self.assertEqual(first.duration_ms, 6000)
        self.assertEqual(first.usage["input_tokens"], 1200)
        self.assertEqual(first.usage["output_tokens"], 600)

    def test_merge_cost_handles_none_base(self):
        first = AgentRunResult(
            exit_code=0, status="complete", percent=85.0,
            notes="", verdict=None,
        )
        second = AgentRunResult(
            exit_code=0, status="complete", percent=86.0,
            notes="", verdict=None,
            total_cost_usd=0.02, duration_ms=1000,
        )
        first.merge_cost(second)
        self.assertAlmostEqual(first.total_cost_usd, 0.02)
        self.assertEqual(first.duration_ms, 1000)

    def test_merge_cost_handles_none_other(self):
        first = AgentRunResult(
            exit_code=0, status="complete", percent=85.0,
            notes="", verdict=None,
            total_cost_usd=0.10, duration_ms=5000,
        )
        second = AgentRunResult(
            exit_code=0, status="complete", percent=86.0,
            notes="", verdict=None,
        )
        first.merge_cost(second)
        # Should remain unchanged
        self.assertAlmostEqual(first.total_cost_usd, 0.10)
        self.assertEqual(first.duration_ms, 5000)


class TestSessionResult(unittest.TestCase):
    """Tests for SessionResult dataclass."""

    def test_default_values(self):
        result = SessionResult(
            status="complete",
            start_percent=80.0,
            end_percent=95.0,
            verdict="LIKELY_FIXABLE",
            patch="diff --git...",
            notes="improved",
            model="sonnet",
            session_id="test-1",
        )
        self.assertFalse(result.patch_applied)
        self.assertIsNone(result.actual_cost_usd)
        self.assertIsNone(result.duration_ms)
        self.assertIsNone(result.usage)


if __name__ == "__main__":
    unittest.main(verbosity=2)
