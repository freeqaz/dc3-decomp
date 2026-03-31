"""Resolve shared DB paths for the permuter.

Worktrees have their own checkout root, but we want decomp/permuter history
to live in one shared location by default. Resolve the shared root from git's
common dir, with an environment override for tests and one-off runs.
"""

from __future__ import annotations

import os
import subprocess
from functools import lru_cache
from pathlib import Path


_FILE_ROOT = Path(__file__).resolve().parents[2]
_ENV_DB_ROOT = "PERMUTER_DB_ROOT"


def _detect_repo_root() -> Path:
    """Detect repo root from cwd, falling back to __file__ location.

    Same logic as project._resolve_repo_root() but without importing project
    to avoid circular imports (project.py may import repo_paths).
    """
    cwd = Path.cwd().resolve()
    for candidate in [cwd] + list(cwd.parents):
        if (candidate / "config" / "SZBE69_B8").is_dir():
            return candidate
        if (candidate / "build" / "373307D9").is_dir():
            return candidate
        if (candidate / "objdiff.json").is_file():
            return candidate
    return _FILE_ROOT


def _resolve_env_path(name: str) -> Path | None:
    value = os.environ.get(name)
    if not value:
        return None
    return Path(value).expanduser().resolve()


def _git_common_dir(repo_root: Path) -> Path | None:
    try:
        proc = subprocess.run(
            [
                "git",
                "-C",
                str(repo_root),
                "rev-parse",
                "--path-format=absolute",
                "--git-common-dir",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except Exception:
        return None

    output = proc.stdout.strip()
    if not output:
        return None
    return Path(output).resolve()


@lru_cache(maxsize=1)
def get_db_root() -> Path:
    """Return the shared root used for persistent permuter databases.

    The env var PERMUTER_DB_ROOT is only honoured when the detected repo root
    (from CWD) is the *same* project as the env var points to.  This prevents
    a stale DC3 env var from being used when the user is running from an RB3
    checkout (or vice-versa).
    """
    repo_root = _detect_repo_root()

    env_root = _resolve_env_path(_ENV_DB_ROOT)
    if env_root is not None:
        # Only use the env var when the repo root is *inside* (or equal to)
        # the env-specified path, i.e. we are actually in that project.
        try:
            repo_root.relative_to(env_root)
            return env_root
        except ValueError:
            pass  # repo_root is not under env_root — ignore the env var

    common_dir = _git_common_dir(repo_root)
    if common_dir is not None:
        if common_dir.name == ".git":
            return common_dir.parent.resolve()
        if common_dir.parent.name == ".git":
            return common_dir.parent.parent.resolve()
        return common_dir.resolve()

    return repo_root


def get_decomp_db_path() -> Path:
    """Return the shared decomp.db path."""
    return get_db_root() / "decomp.db"


def get_cache_db_path() -> Path:
    """Return the shared permuter_cache.db path."""
    return get_db_root() / "permuter_cache.db"
