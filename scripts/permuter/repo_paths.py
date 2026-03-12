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


_FALLBACK_ROOT = Path(__file__).resolve().parents[2]
_ENV_DB_ROOT = "PERMUTER_DB_ROOT"


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
    """Return the shared root used for persistent permuter databases."""
    env_root = _resolve_env_path(_ENV_DB_ROOT)
    if env_root is not None:
        return env_root

    common_dir = _git_common_dir(_FALLBACK_ROOT)
    if common_dir is not None:
        if common_dir.name == ".git":
            return common_dir.parent.resolve()
        if common_dir.parent.name == ".git":
            return common_dir.parent.parent.resolve()
        return common_dir.resolve()

    return _FALLBACK_ROOT


def get_decomp_db_path() -> Path:
    """Return the shared decomp.db path."""
    return get_db_root() / "decomp.db"


def get_cache_db_path() -> Path:
    """Return the shared permuter_cache.db path."""
    return get_db_root() / "permuter_cache.db"
