"""Per-project source-synthesis settings (decomp-synth.json).

The compiler/toolchain now lives in the unified project descriptor
(``project.ProjectConfig``, loaded from ``decomp-synth.json`` with a
``permuter.json`` fallback).  This module is a thin compatibility layer so
existing callers keep using ``get_compiler()`` while there is a single source
of truth.

CLI flags override config; config overrides built-in defaults.

Example decomp-synth.json at repo root:
    {
      "name": "dc3",
      "compiler": "msvc",
      "build_id": "373307D9"
    }
"""

from __future__ import annotations

from .project import get_project_config as _get_proj
from .project import _get_project_config_cached as _cached


def get_compiler() -> str:
    """Return the project's compiler/toolchain dialect ("mwcc" | "msvc")."""
    return _get_proj().toolchain


def load_config() -> dict:
    """Back-compat shim returning a dict with at least the ``compiler`` key."""
    proj = _get_proj()
    return {"compiler": proj.toolchain, "name": proj.name, "build_id": proj.build_id}


def reset_cache() -> None:
    """Used by tests to force re-read of the project config."""
    _cached.cache_clear()
