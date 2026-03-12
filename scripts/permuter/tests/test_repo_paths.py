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

    def test_env_override_wins(self) -> None:
        os.environ["PERMUTER_DB_ROOT"] = "/tmp/permuter-db-root"
        self.assertEqual(repo_paths.get_db_root(), Path("/tmp/permuter-db-root"))

    def test_git_common_dir_maps_to_shared_root(self) -> None:
        os.environ.pop("PERMUTER_DB_ROOT", None)
        mock_proc = mock.Mock(stdout="/tmp/main-repo/.git\n")
        with mock.patch.object(repo_paths.subprocess, "run", return_value=mock_proc):
            self.assertEqual(repo_paths.get_db_root(), Path("/tmp/main-repo"))

    def test_fallback_uses_local_repo_root_when_git_unavailable(self) -> None:
        os.environ.pop("PERMUTER_DB_ROOT", None)
        with mock.patch.object(repo_paths.subprocess, "run", side_effect=OSError):
            self.assertEqual(repo_paths.get_db_root(), repo_paths._FALLBACK_ROOT)


if __name__ == "__main__":
    unittest.main()
