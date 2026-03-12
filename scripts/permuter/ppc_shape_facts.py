"""Extract derived PPC codegen shape facts from MSVC assembly listings.

Provides the bridge between the PPC->IL lifter (msvc-src/tools/ppc_il_lifter.py)
and the permuter's target_facts layer.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any


_PROJECT_ROOT = Path(__file__).resolve().parents[2]
_TOOLS_DIR = _PROJECT_ROOT / "msvc-src" / "tools"
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

from diff_test import parse_asm_listing  # type: ignore
from ppc_il_lifter import (  # type: ignore
    LiftedFunction,
    compute_shape_delta,
    derive_shape_facts,
    detect_argument_materialization,
    detect_sparse_switch,
    lift_function_asm,
)


def _match_function(functions: dict[str, Any], needle: str):
    """Match a function by exact key first, then substring."""
    if needle in functions:
        return functions[needle]
    for name, func in functions.items():
        if needle in name:
            return func
    return None


def extract_shape_facts_from_listing(listing_text: str, function_name: str) -> list[dict[str, Any]]:
    """Parse a PPC listing and derive shape facts for one function."""
    functions = parse_asm_listing(listing_text)
    func = _match_function(functions, function_name)
    if func is None:
        return []
    lifted = lift_function_asm(func)
    return derive_shape_facts(lifted)


def extract_lifted_function(listing_text: str, function_name: str) -> LiftedFunction | None:
    """Parse a PPC listing and return the full lifted function."""
    functions = parse_asm_listing(listing_text)
    func = _match_function(functions, function_name)
    if func is None:
        return None
    return lift_function_asm(func)


def extract_shape_delta(
    listing_text: str,
    function_name: str,
    source_il_ops: list[dict[str, Any]] | None = None,
) -> dict[str, Any] | None:
    """Parse a PPC listing and compute shape delta against optional source IL."""
    lifted = extract_lifted_function(listing_text, function_name)
    if lifted is None:
        return None
    return compute_shape_delta(lifted, source_il_ops)
