"""Tests for shared permuter DB path resolution."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path
from unittest import mock

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter import repo_paths


class TestRepoPaths(unittest.TestCase):
    def setUp(self) -> None:
        repo_paths.get_db_root.cache_clear()
        self._old_env = os.environ.get("PERMUTER_DB_ROOT")

    def tearDown(self) -> None:
        if self._old_env is None:
            os.environ.pop("PERMUTER_DB_ROOT", None)
        else:
            os.environ["PERMUTER_DB_ROOT"] = self._old_env
        repo_paths.get_db_root.cache_clear()

    def test_env_override_wins_when_repo_is_inside_it(self) -> None:
        # Post multi-project refactor, the env var is honoured only when the
        # detected repo root is inside (or equal to) it — this stops a stale
        # DC3 PERMUTER_DB_ROOT from leaking into an RB3 run (and vice-versa).
        os.environ["PERMUTER_DB_ROOT"] = "/tmp/permuter-db-root"
        with mock.patch.object(
            repo_paths, "_detect_repo_root",
            return_value=Path("/tmp/permuter-db-root/checkout"),
        ):
            self.assertEqual(
                repo_paths.get_db_root(), Path("/tmp/permuter-db-root")
            )

    def test_env_override_ignored_when_repo_outside_it(self) -> None:
        # repo_root is NOT under the env path -> env var ignored, falls through
        # to git-common-dir resolution.
        os.environ["PERMUTER_DB_ROOT"] = "/tmp/some-other-project"
        mock_proc = mock.Mock(stdout="/tmp/main-repo/.git\n")
        with mock.patch.object(
            repo_paths, "_detect_repo_root", return_value=Path("/tmp/main-repo")
        ), mock.patch.object(
            repo_paths.subprocess, "run", return_value=mock_proc
        ):
            self.assertEqual(repo_paths.get_db_root(), Path("/tmp/main-repo"))

    def test_git_common_dir_maps_to_shared_root(self) -> None:
        os.environ.pop("PERMUTER_DB_ROOT", None)
        mock_proc = mock.Mock(stdout="/tmp/main-repo/.git\n")
        with mock.patch.object(repo_paths.subprocess, "run", return_value=mock_proc):
            self.assertEqual(repo_paths.get_db_root(), Path("/tmp/main-repo"))

    def test_fallback_uses_local_repo_root_when_git_unavailable(self) -> None:
        # With no env var and git unavailable, get_db_root() falls back to the
        # detected repo root.
        os.environ.pop("PERMUTER_DB_ROOT", None)
        with mock.patch.object(
            repo_paths, "_detect_repo_root", return_value=Path("/tmp/local-repo")
        ), mock.patch.object(repo_paths.subprocess, "run", side_effect=OSError):
            self.assertEqual(repo_paths.get_db_root(), Path("/tmp/local-repo"))


if __name__ == "__main__":
    unittest.main()
