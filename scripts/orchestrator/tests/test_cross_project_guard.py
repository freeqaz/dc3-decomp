"""
Regression tests for the orchestrator MCP cross-project guard.

Background: `project_dir` selects a *worktree of this project* so builds test the
agent's edits. It does NOT select a different decomp project -- the database,
report.json, struct DB, symbol suggestions and linker map all come from the
server's own project root.

Before the guard, passing a foreign repo's path (../rb3-xenon) returned a
plausible-but-wrong number: objdiff reported "Symbol not found" in the foreign
tree and the handler fell through to `_suggest_similar_symbols`, which printed
the SAME symbol name annotated with DC3's percentage (ObjectDir::Iterate ->
"100.0%", DC3's value, while rb3-xenon's is ~60%). Two lanes wrote those numbers
into commit messages as fact.

These tests pin: a foreign project_dir RAISES, and every in-project case
(omitted, main repo, sibling worktree, REPO_ROOT) still resolves exactly as
before.
"""

import asyncio
import json
import os
import unittest
from pathlib import Path
from unittest import mock

from scripts.orchestrator.mcp_server import (
    CrossProjectError,
    DecompMCPServer,
    _discover_title_id,
)


def _make_project(root: Path, title_id: str, *, objdiff_json: bool = True,
                  build_dir: bool = True) -> Path:
    """Create a minimal decomp-project-shaped tree."""
    root.mkdir(parents=True, exist_ok=True)
    if build_dir:
        (root / "build" / title_id).mkdir(parents=True, exist_ok=True)
    if objdiff_json:
        (root / "objdiff.json").write_text(json.dumps({
            "min_version": "2.0.0",
            "units": [{
                "name": "default/Foo",
                "target_path": f"build/{title_id}/obj/Foo.obj",
                "base_path": f"build/{title_id}/src/Foo.obj",
            }],
        }))
    return root


class TestDiscoverTitleId(unittest.TestCase):
    """The title ID is derived from the tree, never hardcoded."""

    def setUp(self):
        self._tmp = __import__("tempfile").TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_derives_from_objdiff_json(self):
        root = _make_project(self.tmp / "dc3", "373307D9", build_dir=False)
        self.assertEqual(_discover_title_id(root), "373307D9")

    def test_derives_from_build_dir_when_no_objdiff_json(self):
        root = _make_project(self.tmp / "rb3x", "45410914", objdiff_json=False)
        self.assertEqual(_discover_title_id(root), "45410914")

    def test_ignores_non_title_build_subdirs(self):
        root = self.tmp / "unconfigured"
        (root / "build" / "compilers").mkdir(parents=True)
        (root / "build" / "tools").mkdir(parents=True)
        self.assertIsNone(_discover_title_id(root))

    def test_non_hex_title_ids_are_still_detected(self):
        # RB3 (MetroWerks/Wii) uses 'SZBE69_B8', not an 8-hex Xbox title ID.
        root = _make_project(self.tmp / "rb3", "SZBE69_B8", build_dir=False)
        self.assertEqual(_discover_title_id(root), "SZBE69_B8")

    def test_returns_none_for_non_project(self):
        root = self.tmp / "plain"
        root.mkdir()
        self.assertIsNone(_discover_title_id(root))


class TestCrossProjectGuard(unittest.TestCase):
    """A foreign project_dir must raise, never return a number."""

    def setUp(self):
        self._tmp = __import__("tempfile").TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

        self.main = _make_project(self.tmp / "dc3-decomp", "373307D9")
        self.foreign = _make_project(self.tmp / "rb3-xenon", "45410914")
        self.worktree = _make_project(self.tmp / "dc3-lane0", "373307D9")

        self.server = DecompMCPServer(db_path=str(self.main / "decomp.db"))
        self.server.project_root = self.main
        self.server._title_id_cache.clear()

    # ---- the defect ----

    def test_foreign_project_dir_raises(self):
        with self.assertRaises(CrossProjectError) as ctx:
            self.server._resolve_project_dir(str(self.foreign))
        message = str(ctx.exception)
        # Names both paths and both title IDs so the caller can see the mixup.
        self.assertIn(str(self.foreign), message)
        self.assertIn(str(self.main), message)
        self.assertIn("45410914", message)
        self.assertIn("373307D9", message)

    def test_foreign_project_dir_points_at_sibling_orchestrator(self):
        sibling = self.foreign / "scripts" / "orchestrator"
        sibling.mkdir(parents=True)
        (sibling / "mcp_server.py").write_text("# stub\n")
        with self.assertRaises(CrossProjectError) as ctx:
            self.server._resolve_project_dir(str(self.foreign))
        self.assertIn(str(sibling / "mcp_server.py"), str(ctx.exception))

    def test_run_objdiff_raises_instead_of_returning_a_percentage(self):
        """The end-to-end path an agent hits -- must not produce a match%."""
        with self.assertRaises(CrossProjectError):
            asyncio.run(self.server._run_objdiff({
                "symbol": "?Iterate@ObjectDir@@IAAXPAVDataArray@@_N@Z",
                "project_dir": str(self.foreign),
            }))

    def test_run_analyze_function_raises(self):
        with self.assertRaises(CrossProjectError):
            asyncio.run(self.server._run_analyze_function({
                "symbol": "?Iterate@ObjectDir@@IAAXPAVDataArray@@_N@Z",
                "project_dir": str(self.foreign),
            }))

    def test_run_diff_inspect_raises(self):
        with self.assertRaises(CrossProjectError):
            asyncio.run(self.server._run_diff_inspect({
                "symbol": "?Iterate@ObjectDir@@IAAXPAVDataArray@@_N@Z",
                "mode": "diagnose",
                "project_dir": str(self.foreign),
            }))

    def test_non_project_directory_raises(self):
        stranger = self.tmp / "not-a-decomp"
        stranger.mkdir()
        with self.assertRaises(CrossProjectError):
            self.server._resolve_project_dir(str(stranger))

    # ---- the default case must be untouched ----

    def test_omitted_project_dir_uses_main_repo(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("REPO_ROOT", None)
            self.assertEqual(self.server._resolve_project_dir(None), self.main)

    def test_main_repo_project_dir_allowed(self):
        self.assertEqual(
            self.server._resolve_project_dir(str(self.main)), Path(str(self.main))
        )

    def test_sibling_worktree_of_same_project_allowed(self):
        self.assertEqual(
            self.server._resolve_project_dir(str(self.worktree)),
            Path(str(self.worktree)),
        )

    def test_repo_root_env_var_still_honoured(self):
        with mock.patch.dict(os.environ, {"REPO_ROOT": str(self.worktree)}):
            self.assertEqual(
                self.server._resolve_project_dir(None), Path(str(self.worktree))
            )

    def test_repo_root_env_var_pointing_at_foreign_repo_raises(self):
        with mock.patch.dict(os.environ, {"REPO_ROOT": str(self.foreign)}):
            with self.assertRaises(CrossProjectError):
                self.server._resolve_project_dir(None)

    def test_missing_project_dir_reports_missing_not_cross_project(self):
        with self.assertRaises(FileNotFoundError):
            self.server._resolve_project_dir(str(self.tmp / "does-not-exist"))

    def test_unconfigured_worktree_of_same_repo_allowed(self):
        """A fresh worktree before configure.py has no title ID -- git decides."""
        fresh = self.tmp / "dc3-fresh"
        fresh.mkdir()
        common = str(self.main / ".git")
        with mock.patch("scripts.orchestrator.mcp_server._git_common_dir",
                        return_value=common):
            self.assertEqual(
                self.server._resolve_project_dir(str(fresh)), Path(str(fresh))
            )


class TestRealRepoIdentity(unittest.TestCase):
    """Sanity check against the real checkout this test runs in."""

    def test_this_repo_resolves_to_its_own_title_id(self):
        server = DecompMCPServer(db_path="decomp.db")
        self.assertEqual(server.project_title_id, "373307D9")
        self.assertEqual(
            server._resolve_project_dir(str(server.project_root)),
            Path(str(server.project_root)),
        )


if __name__ == "__main__":
    unittest.main()
