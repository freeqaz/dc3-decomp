"""Tests for refactor-staff second pass functionality.

Tests the three new methods and refactor pass integration in _execute_session():
- _worktree_has_changes()
- _build_refactor_prompt()
- Refactor pass wiring in _execute_session()
"""

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch
import logging

from scripts.orchestrator.core import DecompOrchestrator
from scripts.orchestrator.types import AgentRunResult


class TestWorktreeHasChanges(unittest.TestCase):
    """Test _worktree_has_changes() with real temp git repos."""

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp_dir.name)
        subprocess.run(["git", "init"], cwd=self.repo, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.email", "test@test.com"], cwd=self.repo, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.name", "Test"], cwd=self.repo, capture_output=True, check=True)
        # Create initial commit
        (self.repo / "src").mkdir()
        (self.repo / "src" / "test.cpp").write_text("// hello")
        subprocess.run(["git", "add", "."], cwd=self.repo, capture_output=True, check=True)
        subprocess.run(["git", "commit", "-m", "init"], cwd=self.repo, capture_output=True, check=True)

        # Create orchestrator with mocked dependencies
        self.logger = logging.getLogger("test_worktree_has_changes")
        self.logger.addHandler(logging.NullHandler())

        with patch('scripts.orchestrator.core.WorktreePool'), \
             patch('scripts.orchestrator.core.AgentRunner'), \
             patch('scripts.orchestrator.core.PatchApplier'), \
             patch('scripts.orchestrator.core._setup_logging', return_value=self.logger):
            self.orch = DecompOrchestrator(
                db_path="/fake/decomp.db",
                pool_dir=Path("/fake/pool"),
                main_repo=Path("/fake/repo"),
            )

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_returns_false_for_clean_worktree(self):
        """Test that clean worktree returns False."""
        has_changes = self.orch._worktree_has_changes(self.repo)
        self.assertFalse(has_changes)

    def test_returns_true_for_modified_file(self):
        """Test that modified file in src/ returns True."""
        (self.repo / "src" / "test.cpp").write_text("// modified")
        has_changes = self.orch._worktree_has_changes(self.repo)
        self.assertTrue(has_changes)

    def test_returns_true_for_new_staged_file(self):
        """Test that new staged file in src/ returns True."""
        (self.repo / "src" / "new.cpp").write_text("// new content")
        subprocess.run(["git", "add", "src/new.cpp"], cwd=self.repo, capture_output=True, check=True)
        has_changes = self.orch._worktree_has_changes(self.repo)
        self.assertTrue(has_changes)


class TestBuildRefactorPrompt(unittest.TestCase):
    """Test _build_refactor_prompt() string construction."""

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.main_repo = Path(self.temp_dir.name)
        self.worktree = Path(self.temp_dir.name) / "worktree"
        self.worktree.mkdir()

        # Create skill file
        skill_dir = self.main_repo / ".claude" / "skills" / "refactor-staff"
        skill_dir.mkdir(parents=True)
        skill_file = skill_dir / "SKILL.md"
        skill_file.write_text("# Refactor Staff Skill\n\nThis is the skill content.")

        # Initialize git repo in worktree
        subprocess.run(["git", "init"], cwd=self.worktree, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.email", "test@test.com"], cwd=self.worktree, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.name", "Test"], cwd=self.worktree, capture_output=True, check=True)
        (self.worktree / "test.cpp").write_text("int main() {}")
        subprocess.run(["git", "add", "."], cwd=self.worktree, capture_output=True, check=True)
        subprocess.run(["git", "commit", "-m", "init"], cwd=self.worktree, capture_output=True, check=True)

        # Create orchestrator
        self.logger = logging.getLogger("test_build_refactor_prompt")
        self.logger.addHandler(logging.NullHandler())

        with patch('scripts.orchestrator.core.WorktreePool'), \
             patch('scripts.orchestrator.core.AgentRunner'), \
             patch('scripts.orchestrator.core.PatchApplier'), \
             patch('scripts.orchestrator.core._setup_logging', return_value=self.logger):
            self.orch = DecompOrchestrator(
                db_path="/fake/decomp.db",
                pool_dir=Path("/fake/pool"),
                main_repo=self.main_repo,
            )

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_prompt_includes_skill_content(self):
        """Test that prompt includes skill content."""
        func = {"symbol": "test_symbol", "demangled": "test_function"}
        prompt = self.orch._build_refactor_prompt(func, self.worktree, 95.5)
        self.assertIn("This is the skill content.", prompt)

    def test_prompt_includes_symbol_name(self):
        """Test that prompt includes symbol name."""
        func = {"symbol": "_Z10test_funcv", "demangled": "test_func()"}
        prompt = self.orch._build_refactor_prompt(func, self.worktree, 95.5)
        self.assertIn("_Z10test_funcv", prompt)
        self.assertIn("test_func()", prompt)

    def test_prompt_includes_match_percent(self):
        """Test that prompt includes match percentage."""
        func = {"symbol": "test_symbol", "demangled": "test_function"}
        prompt = self.orch._build_refactor_prompt(func, self.worktree, 87.3)
        self.assertIn("87.3%", prompt)

    def test_prompt_includes_modified_files(self):
        """Test that prompt includes modified files."""
        func = {"symbol": "test_symbol", "demangled": "test_function"}
        # Modify a file
        (self.worktree / "test.cpp").write_text("int main() { return 0; }")
        prompt = self.orch._build_refactor_prompt(func, self.worktree, 95.5)
        self.assertIn("test.cpp", prompt)

    def test_prompt_includes_objdiff_instruction(self):
        """Test that prompt includes objdiff-cli instructions."""
        func = {"symbol": "test_symbol", "demangled": "test_function"}
        prompt = self.orch._build_refactor_prompt(func, self.worktree, 95.5)
        self.assertIn("./bin/objdiff-cli diff", prompt)
        self.assertIn(f"project_dir={self.worktree}", prompt)


class TestRefactorPassIntegration(unittest.IsolatedAsyncioTestCase):
    """Tests refactor wiring inside _execute_session()."""

    def setUp(self):
        """Set up test orchestrator with mocked dependencies."""
        self.logger = logging.getLogger("test_refactor_integration")
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
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    async def test_refactor_runs_when_changes_exist(
        self, mock_update_status, mock_record, mock_collect_context,
        mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test refactor pass runs when refactor=True and changes exist."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "sonnet"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {}

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value="patch")
        self.orch._check_quota = AsyncMock()
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        # First pass result (with changes)
        first_result = AgentRunResult(
            exit_code=0, status="complete", percent=90.0, notes="first pass",
            verdict=None, total_cost_usd=0.10, duration_ms=10000,
            usage={"input_tokens": 1000, "output_tokens": 500}
        )

        # Refactor pass result (improved slightly)
        refactor_result = AgentRunResult(
            exit_code=0, status="complete", percent=91.0, notes="refactor pass",
            verdict=None, total_cost_usd=0.02, duration_ms=2000,
            usage={"input_tokens": 200, "output_tokens": 100}
        )

        # Mock runner to return different results for first vs refactor pass
        call_count = 0

        async def mock_run(config):
            nonlocal call_count
            call_count += 1
            if call_count == 1:
                return first_result
            else:
                return refactor_result

        self.orch.runner.run = AsyncMock(side_effect=mock_run)

        # Mock worktree has changes after first pass
        self.orch._worktree_has_changes = MagicMock(return_value=True)
        self.orch._build_refactor_prompt = MagicMock(return_value="refactor prompt")

        def prompt_builder(func, worktree_dir, context):
            return "first pass prompt"

        result = await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model="sonnet",
            verbose=False,
            dry_run=False,
            use_incremental=True,
            prompt_builder=prompt_builder,
            refactor=True,
        )

        # Verify refactor methods were called
        # _worktree_has_changes is called twice: once for pre-refactor patch extraction (9a),
        # once for the refactor gate (9b)
        self.assertEqual(self.orch._worktree_has_changes.call_count, 2)
        self.orch._build_refactor_prompt.assert_called_once()

        # Verify runner was called twice (first pass + refactor)
        self.assertEqual(self.orch.runner.run.call_count, 2)

        # Verify second call was refactor config
        second_call = self.orch.runner.run.call_args_list[1]
        refactor_config = second_call.args[0]
        self.assertEqual(refactor_config.model, "haiku")
        self.assertEqual(refactor_config.max_turns, 30)
        self.assertIn("Read", refactor_config.allowed_tools)
        self.assertIn("mcp__orchestrator__run_objdiff", refactor_config.allowed_tools)

        # Verify costs were merged
        self.assertEqual(result["end_percent"], 91.0)  # Updated from refactor
        self.assertAlmostEqual(result["actual_cost_usd"], 0.12, places=5)  # 0.10 + 0.02

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    async def test_refactor_skipped_when_no_changes(
        self, mock_update_status, mock_record, mock_collect_context,
        mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test refactor pass skipped when no changes from first pass."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {}

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value=None)
        self.orch._check_quota = AsyncMock()
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        first_result = AgentRunResult(
            exit_code=0, status="complete", percent=80.0, notes="no changes", verdict=None
        )
        self.orch.runner.run = AsyncMock(return_value=first_result)

        # Mock worktree has NO changes
        self.orch._worktree_has_changes = MagicMock(return_value=False)
        self.orch._build_refactor_prompt = MagicMock(return_value="refactor prompt")

        def prompt_builder(func, worktree_dir, context):
            return "prompt"

        await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model=None,
            verbose=False,
            dry_run=False,
            use_incremental=True,
            prompt_builder=prompt_builder,
            refactor=True,
        )

        # Verify refactor was NOT run
        # _worktree_has_changes is called twice: once for pre-refactor patch extraction (9a),
        # once for the refactor gate (9b) — both return False
        self.assertEqual(self.orch._worktree_has_changes.call_count, 2)
        self.orch._build_refactor_prompt.assert_not_called()
        self.assertEqual(self.orch.runner.run.call_count, 1)  # Only first pass

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    async def test_refactor_skipped_when_disabled(
        self, mock_update_status, mock_record, mock_collect_context,
        mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test refactor pass skipped when refactor=False."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {}

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value="patch")
        self.orch._check_quota = AsyncMock()
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        first_result = AgentRunResult(
            exit_code=0, status="complete", percent=90.0, notes="done", verdict=None
        )
        self.orch.runner.run = AsyncMock(return_value=first_result)

        self.orch._worktree_has_changes = MagicMock(return_value=True)
        self.orch._build_refactor_prompt = MagicMock()

        def prompt_builder(func, worktree_dir, context):
            return "prompt"

        await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model=None,
            verbose=False,
            dry_run=False,
            use_incremental=True,
            prompt_builder=prompt_builder,
            refactor=False,  # Disabled
        )

        # Verify refactor was NOT run
        self.orch._worktree_has_changes.assert_not_called()
        self.orch._build_refactor_prompt.assert_not_called()
        self.assertEqual(self.orch.runner.run.call_count, 1)

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    @patch('scripts.orchestrator.core.subprocess.run')
    async def test_refactor_regression_reverts_changes(
        self, mock_subprocess_run, mock_update_status, mock_record, mock_collect_context,
        mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test that refactor regression triggers git checkout revert."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {}

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value="patch")
        self.orch._check_quota = AsyncMock()
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        # First pass: 90%
        first_result = AgentRunResult(
            exit_code=0, status="complete", percent=90.0, notes="first pass", verdict=None
        )

        # Refactor pass: regressed to 85% (worse!)
        refactor_result = AgentRunResult(
            exit_code=0, status="complete", percent=85.0, notes="refactor pass", verdict=None
        )

        call_count = 0

        async def mock_run(config):
            nonlocal call_count
            call_count += 1
            return first_result if call_count == 1 else refactor_result

        self.orch.runner.run = AsyncMock(side_effect=mock_run)
        self.orch._worktree_has_changes = MagicMock(return_value=True)
        self.orch._build_refactor_prompt = MagicMock(return_value="refactor prompt")

        def prompt_builder(func, worktree_dir, context):
            return "prompt"

        await self.orch._execute_session(
            func=func,
            session_id=None,
            pre_locked=False,
            model=None,
            verbose=False,
            dry_run=False,
            use_incremental=True,
            prompt_builder=prompt_builder,
            refactor=True,
        )

        # Verify git checkout was called to revert
        checkout_calls = [call for call in mock_subprocess_run.call_args_list
                          if call[0][0][:2] == ["git", "checkout"]]
        self.assertEqual(len(checkout_calls), 1)
        self.assertEqual(checkout_calls[0][0][0], ["git", "checkout", "--", "src/", "include/"])

    @patch('scripts.orchestrator.core.lock_function')
    @patch('scripts.orchestrator.core.unlock_function')
    @patch('scripts.orchestrator.core.select_model')
    @patch('scripts.orchestrator.core.get_escalation_reason')
    @patch('scripts.orchestrator.core.collect_pre_run_context')
    @patch('scripts.orchestrator.core.record_attempt')
    @patch('scripts.orchestrator.core.update_function_status')
    async def test_refactor_cost_merged_into_result(
        self, mock_update_status, mock_record, mock_collect_context,
        mock_escalation, mock_select_model, mock_unlock, mock_lock
    ):
        """Test that refactor cost is merged into final result."""
        func = {"id": 1, "symbol": "test_symbol", "current_percent": 80.0}

        mock_lock.return_value = True
        mock_select_model.return_value = "haiku"
        mock_escalation.return_value = "first attempt"
        mock_collect_context.return_value = {}

        self.orch.worktree_pool.acquire = MagicMock(return_value=Path("/fake/worktree"))
        self.orch.worktree_pool.release = MagicMock()
        self.orch.worktree_pool.extract_patch = MagicMock(return_value="patch")
        self.orch._check_quota = AsyncMock()
        self.orch.patch_applier.maybe_apply = MagicMock(return_value={"applied": False})

        # First pass cost
        first_result = AgentRunResult(
            exit_code=0, status="complete", percent=90.0, notes="first",
            verdict=None, total_cost_usd=0.15, duration_ms=12000,
            usage={"input_tokens": 1500, "output_tokens": 800}
        )

        # Refactor pass cost
        refactor_result = AgentRunResult(
            exit_code=0, status="complete", percent=91.0, notes="refactor",
            verdict=None, total_cost_usd=0.03, duration_ms=3000,
            usage={"input_tokens": 300, "output_tokens": 150}
        )

        call_count = 0

        async def mock_run(config):
            nonlocal call_count
            call_count += 1
            return first_result if call_count == 1 else refactor_result

        self.orch.runner.run = AsyncMock(side_effect=mock_run)
        self.orch._worktree_has_changes = MagicMock(return_value=True)
        self.orch._build_refactor_prompt = MagicMock(return_value="refactor prompt")

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
            refactor=True,
        )

        # Verify costs were merged
        self.assertEqual(result["actual_cost_usd"], 0.18)  # 0.15 + 0.03
        # Duration should be merged via record_attempt
        # (we check that it was recorded correctly)
        record_call = mock_record.call_args
        # Duration is from agent_result which should have merged costs
        # Check the result dict directly
        self.assertIsNotNone(result["duration_ms"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
