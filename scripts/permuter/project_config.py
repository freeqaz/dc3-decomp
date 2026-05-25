"""Per-project permuter configuration (permuter.json).

Lets each decomp project pin defaults like compiler dialect. CLI flags
override config; config overrides built-in defaults.

Example permuter.json at repo root:
    {
      "compiler": "mwcc",
      "max_variants_default": 50
    }
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

from .project import get_project_config as _get_proj

_DEFAULTS = {
    "compiler": "mwcc",  # mwcc | msvc
}


_CACHED: Optional[dict] = None


def load_config() -> dict:
    """Read permuter.json from the project root.

    Returns a dict merged with built-in defaults. Missing file = defaults only.
    """
    global _CACHED
    if _CACHED is not None:
        return _CACHED

    root = _get_proj().repo_root
    path = root / "permuter.json"
    cfg = dict(_DEFAULTS)
    if path.exists():
        try:
            data = json.loads(path.read_text())
            if isinstance(data, dict):
                cfg.update(data)
        except (OSError, json.JSONDecodeError):
            pass

    # Normalize compiler value
    c = str(cfg.get("compiler", "mwcc")).lower()
    if c not in ("mwcc", "msvc"):
        c = "mwcc"
    cfg["compiler"] = c

    _CACHED = cfg
    return _CACHED


def get_compiler() -> str:
    return load_config()["compiler"]


def reset_cache() -> None:
    """Used by tests to force re-read of permuter.json."""
    global _CACHED
    _CACHED = None
