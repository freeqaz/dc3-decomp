"""Tests for _execute_session() extraction and delegation.

Tests that the shared session flow works correctly and that
run_single() and run_rb3_merge_single() properly delegate to it.
"""

import unittest
from unittest.mock import AsyncMock, MagicMock, patch, call
from pathlib import Path
import logging

from scripts.orchestrator.core import DecompOrchestrator
from scripts.orchestrator.types import AgentRunResult


class TestExecuteSession(unittest.IsolatedAsyncioTestCase):
    """Tests for _execute_session() private method."""

    def setUp(self):
        """Set up test orchestrator with mocked dependencies."""
        self.logger = logging.getLogger("test_execute_session")
        self.logger.addHandler(logging.NullHandler())

        with patch('scripts.orchestrator.core.WorktreePool'), \
             patch('scripts.orchestrator.core.AgentRunner'), \
             patch('scripts.orchestrator.core.PatchApplier'), \
             patch('scripts.orchestrator.core._setup_logging', return_value=self.logger):

            self.orch = DecompOrchestrator(
                db_path="/fake/decomp.db",
                pool_dir=Path("/fake/pool"),
                main_repo=Path("/fake/repo"),
                logs_dir=Path("/fake/logs"),
            )

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    async def test_full_session_flow(
        self, mock_escalation, mock_select_model, mock_update_status,
        mock_record, mock_collect_context, mock_unlock, mock_lock
    ):
        """Test happy path: agent runs, patch extracted, attempt recorded."""
        # Setup
        func = {
            "id": 1,
            "symbol": "test_symbol",
            "demangled": "test_function",
            "unit": "test_unit",
            "current_percent": 80.0,
        }

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {"match_percent": "80.0", "verdict": "LIKELY_FIXABLE"}

        # Mock worktree pool
        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value="fake patch")

        # Mock agent runner
        agent_result = AgentRunResult(
            exit_code=0,
            status="complete",
            percent=95.0,
            notes="Fixed register allocation",
            verdict="LIKELY_FIXABLE",
            total_cost_usd=0.05,
            duration_ms=5000,
            usage={"input_tokens": 1000, "output_tokens": 500},
        )
        self.orch.runner.run = AsyncMock(return_value=agent_result)

        # Mock quota check
        self.orch._check_quota = AsyncMock()

        # Mock patch applier
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        # Custom prompt builder
        def prompt_builder(func, worktree_dir, context):
            return f"Test prompt for {func['symbol']}"

        # Execute
        result = await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model=None,
            verbose=False,
            dry_run=False,
            use_incremental=True,
            prompt_builder=prompt_builder,
        )

        # Verify
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["start_percent"], 80.0)
        self.assertEqual(result["end_percent"], 95.0)
        self.assertEqual(result["verdict"], "LIKELY_FIXABLE")
        self.assertEqual(result["patch"], "fake patch")
        self.assertEqual(result["notes"], "Fixed register allocation")
        self.assertEqual(result["actual_cost_usd"], 0.05)

        # Verify function was locked and unlocked
        mock_lock.assert_called_once()
        mock_unlock.assert_called_once()

        # Verify worktree lifecycle
        self.orch.worktree_pool.acquire.assert_called_once()
        self.orch.worktree_pool.release.assert_called_once()

        # Verify attempt was recorded
        mock_record.assert_called_once()
        record_call = mock_record.call_args
        self.assertEqual(record_call.kwargs["function_id"], 1)
        self.assertEqual(record_call.kwargs["start_percent"], 80.0)
        self.assertEqual(record_call.kwargs["end_percent"], 95.0)
        self.assertEqual(record_call.kwargs["exit_status"], "complete")

        # Verify status was updated
        mock_update_status.assert_called_once()

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    async def test_dry_run_returns_early(self, mock_escalation, mock_select_model, mock_unlock, mock_lock):
        """Test dry run returns without running agent."""
        func = {
            "id": 1,
            "symbol": "test_symbol",
            "current_percent": 80.0,
        }

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch._check_quota = AsyncMock()

        # Mock runner should NOT be called
        self.orch.runner.run = AsyncMock()

        def prompt_builder(func, worktree_dir, context):
            return "test prompt"

        result = await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model=None,
            verbose=False,
            dry_run=True,
            use_incremental=True,
            prompt_builder=prompt_builder,
        )

        # Verify dry run result
        self.assertEqual(result["status"], "dry_run")
        self.assertEqual(result["function"], func)

        # Verify agent was NOT run
        self.orch.runner.run.assert_not_called()

        # Verify cleanup still happened
        mock_unlock.assert_called_once()
        self.orch.worktree_pool.release.assert_called_once()

    @patch('scripts.orchestrator.core.lock_function')
    async def test_lock_failure_raises(self, mock_lock):
        """Test that lock failure raises RuntimeError."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = False
        self.orch._check_quota = AsyncMock()

        def prompt_builder(func, worktree_dir, context):
            return "test prompt"

        with self.assertRaises(RuntimeError) as ctx:
            await self.orch._execute_session(
                func=func,
                session_id=None,
                pre_locked=False,
                model=None,
                verbose=False,
                dry_run=False,
                use_incremental=True,
                prompt_builder=prompt_builder,
            )

        self.assertIn("Could not lock function", str(ctx.exception))

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    async def test_worktree_unavailable_raises(self, mock_unlock, mock_lock):
        """Test that worktree unavailability raises RuntimeError and unlocks."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        self.orch.worktree_pool.acquire = MagicMock(return_value=None)
        self.orch._check_quota = AsyncMock()

        def prompt_builder(func, worktree_dir, context):
            return "test prompt"

        with self.assertRaises(RuntimeError) as ctx:
            await self.orch._execute_session(
                func=func,
                session_id=None,
                pre_locked=False,
                model=None,
                verbose=False,
                dry_run=False,
                use_incremental=True,
                prompt_builder=prompt_builder,
            )

        self.assertIn("No worktrees available", str(ctx.exception))

        # Verify unlock was called despite failure
        mock_unlock.assert_called_once_with(1, db_path="/fake/decomp.db")

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    async def test_cleanup_runs_on_exception(
        self, mock_collect_context, mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test that finally block releases lock and worktree on error."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {}

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch._check_quota = AsyncMock()

        # Mock agent runner to raise exception
        self.orch.runner.run = AsyncMock(side_effect=RuntimeError("Agent crashed"))

        def prompt_builder(func, worktree_dir, context):
            return "test prompt"

        with self.assertRaises(RuntimeError):
            await self.orch._execute_session(
                func=func,
                session_id=None,
                pre_locked=False,
                model=None,
                verbose=False,
                dry_run=False,
                use_incremental=True,
                prompt_builder=prompt_builder,
            )

        # Verify cleanup happened despite exception
        mock_unlock.assert_called_once()
        self.orch.worktree_pool.release.assert_called_once()

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    async def test_prompt_builder_called_with_context(
        self, mock_update_status, mock_record, mock_collect_context,
        mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test that custom prompt_builder receives correct args."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"

        context_data = {"match_percent": "80.0", "verdict": "LIKELY_FIXABLE", "key_patterns": ["register"]}
        mock_collect_context.return_value = context_data

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value="patch")
        self.orch._check_quota = AsyncMock()

        agent_result = AgentRunResult(
            exit_code=0, status="complete", percent=85.0, notes="done", verdict=None
        )
        self.orch.runner.run = AsyncMock(return_value=agent_result)
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        # Track what prompt_builder receives
        builder_called_with = {}

        def prompt_builder(func, worktree_dir, context):
            builder_called_with["func"] = func
            builder_called_with["worktree_dir"] = worktree_dir
            builder_called_with["context"] = context
            return "custom prompt"

        await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model=None,
            verbose=False,
            dry_run=False,
            use_incremental=True,
            prompt_builder=prompt_builder,
        )

        # Verify prompt_builder was called with correct args
        self.assertEqual(builder_called_with["func"], func)
        self.assertEqual(builder_called_with["worktree_dir"], "/fake/worktree")
        self.assertEqual(builder_called_with["context"], context_data)

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    async def test_notes_prefix_applied(
        self, mock_update_status, mock_record, mock_collect_context,
        mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test that notes_prefix is correctly prepended to agent notes."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {}

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value="patch")
        self.orch._check_quota = AsyncMock()

        agent_result = AgentRunResult(
            exit_code=0, status="complete", percent=85.0,
            notes="Agent notes here", verdict=None
        )
        self.orch.runner.run = AsyncMock(return_value=agent_result)
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        def prompt_builder(func, worktree_dir, context):
            return "prompt"

        result = await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model=None,
            verbose=False,
            dry_run=False,
            use_incremental=True,
            prompt_builder=prompt_builder,
            notes_prefix="RB3-merge: ",
        )

        # Verify notes have prefix
        self.assertEqual(result["notes"], "RB3-merge: Agent notes here")

        # Verify recorded attempt has prefix too
        record_call = mock_record.call_args
        self.assertEqual(record_call.kwargs["notes"], "RB3-merge: Agent notes here")


class TestRunSingleDelegation(unittest.IsolatedAsyncioTestCase):
    """Tests that run_single() properly delegates to _execute_session()."""

    def setUp(self):
        """Set up test orchestrator with mocked dependencies."""
        self.logger = logging.getLogger("test_run_single")
        self.logger.addHandler(logging.NullHandler())

        with patch('scripts.orchestrator.core.WorktreePool'), \
             patch('scripts.orchestrator.core.AgentRunner'), \
             patch('scripts.orchestrator.core.PatchApplier'), \
             patch('scripts.orchestrator.core._setup_logging', return_value=self.logger):

            self.orch = DecompOrchestrator(
                db_path="/fake/decomp.db",
                pool_dir=Path("/fake/pool"),
                main_repo=Path("/fake/repo"),
                logs_dir=Path("/fake/logs"),
            )

    @patch('scripts.orchestrator.core.get_function_by_symbol')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    async def test_run_single_delegates_to_execute_session(
        self, mock_escalation, mock_select_model, mock_get_func
    ):
        """Test that run_single() properly calls _execute_session()."""
        func = {
            "id": 1,
            "symbol": "test_symbol",
            "demangled": "test_function",
            "unit": "test_unit",
            "current_percent": 80.0,
        }

        mock_get_func.return_value = func
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"

        # Mock _execute_session
        expected_result = {
            "status": "complete",
            "start_percent": 80.0,
            "end_percent": 95.0,
            "verdict": "LIKELY_FIXABLE",
            "patch": "fake patch",
            "notes": "Fixed it",
            "model": "haiku",
            "session_id": "test-session",
            "patch_applied": False,
        }
        self.orch._execute_session = AsyncMock(return_value=expected_result)

        result = await self.orch.run_single(
            symbol="test_symbol",
            model="haiku",
            verbose=False,
            dry_run=False,
            use_incremental=True,
        )

        # Verify _execute_session was called
        self.orch._execute_session.assert_called_once()
        call_kwargs = self.orch._execute_session.call_args.kwargs

        self.assertEqual(call_kwargs["func"], func)
        self.assertIsNone(call_kwargs["session_id"])
        self.assertEqual(call_kwargs["model"], "haiku")
        self.assertFalse(call_kwargs["verbose"])
        self.assertFalse(call_kwargs["dry_run"])
        self.assertTrue(call_kwargs["use_incremental"])
        self.assertEqual(call_kwargs["session_prefix"], "single")
        self.assertEqual(call_kwargs["notes_prefix"], "")

        # Verify result is returned correctly
        self.assertEqual(result, expected_result)

    @patch('scripts.orchestrator.core.get_function_by_symbol')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    async def test_run_single_accepts_refactor_param(
        self, mock_escalation, mock_select_model, mock_get_func
    ):
        """Test that run_single() accepts refactor parameter."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_get_func.return_value = func
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"

        # Return a complete result dict
        self.orch._execute_session = AsyncMock(return_value={
            "status": "complete",
            "start_percent": 80.0,
            "end_percent": 95.0,
            "verdict": "LIKELY_FIXABLE",
            "patch": "patch",
            "notes": "done",
            "model": "haiku",
            "session_id": "test-session",
            "patch_applied": False,
        })

        await self.orch.run_single(
            symbol="test_symbol",
            refactor=True,  # New parameter
            verbose=False,  # Disable verbose to avoid print output in test
        )

        # Verify refactor was passed to _execute_session
        call_kwargs = self.orch._execute_session.call_args.kwargs
        self.assertTrue(call_kwargs["refactor"])


class TestRunRb3MergeDelegation(unittest.IsolatedAsyncioTestCase):
    """Tests that run_rb3_merge_single() properly delegates to _execute_session()."""

    def setUp(self):
        """Set up test orchestrator with mocked dependencies."""
        self.logger = logging.getLogger("test_rb3_merge")
        self.logger.addHandler(logging.NullHandler())

        with patch('scripts.orchestrator.core.WorktreePool'), \
             patch('scripts.orchestrator.core.AgentRunner'), \
             patch('scripts.orchestrator.core.PatchApplier'), \
             patch('scripts.orchestrator.core._setup_logging', return_value=self.logger):

            self.orch = DecompOrchestrator(
                db_path="/fake/decomp.db",
                pool_dir=Path("/fake/pool"),
                main_repo=Path("/fake/repo"),
                logs_dir=Path("/fake/logs"),
            )

    @patch('scripts.orchestrator.core.get_function_by_symbol')
    async def test_rb3_merge_delegates_to_execute_session(self, mock_get_func):
        """Test that run_rb3_merge_single() properly calls _execute_session()."""
        func = {
            "id": 1,
            "symbol": "test_symbol",
            "demangled": "test_function",
            "current_percent": 80.0,
        }

        mock_get_func.return_value = func

        # Mock _execute_session
        expected_result = {
            "status": "complete",
            "start_percent": 80.0,
            "end_percent": 95.0,
            "verdict": "LIKELY_FIXABLE",
            "patch": "fake patch",
            "notes": "RB3-merge: Fixed it",
            "model": "haiku",
            "session_id": "rb3-test",
            "patch_applied": False,
        }
        self.orch._execute_session = AsyncMock(return_value=expected_result)

        result = await self.orch.run_rb3_merge_single(
            symbol="test_symbol",
            rb3_source="RB3 source code here",
            model="haiku",
            verbose=False,
            dry_run=False,
        )

        # Verify _execute_session was called
        self.orch._execute_session.assert_called_once()
        call_kwargs = self.orch._execute_session.call_args.kwargs

        self.assertEqual(call_kwargs["func"], func)
        self.assertEqual(call_kwargs["model"], "haiku")
        self.assertFalse(call_kwargs["verbose"])
        self.assertFalse(call_kwargs["dry_run"])
        self.assertEqual(call_kwargs["session_prefix"], "rb3merge")
        self.assertEqual(call_kwargs["notes_prefix"], "RB3-merge: ")

        # Verify result has mode field
        self.assertEqual(result["mode"], "rb3_merge")


if __name__ == "__main__":
    unittest.main(verbosity=2)
