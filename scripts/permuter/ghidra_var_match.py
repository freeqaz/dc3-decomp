"""Match Ghidra variable names to source variable names.

Ghidra uses names like `iVar2`, `fVar3`, `local_38` which don't correspond
to our source names. This module matches them via:

1. Type prefix: i=int, f=float, p=pointer, u=unsigned
2. Positional: i-th variable in Ghidra -> i-th callee-saved register
3. Usage pattern: return value of same call, argument position

The key insight: Ghidra's variable first-use order reflects the target
binary's register allocation order. For callee-saved GPRs, this maps to:
  1st variable -> r31, 2nd -> r30, etc.

By comparing this order to our source's declaration order, we can infer
which declarations need to be reordered to fix register swaps.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass

from .ghidra_ast import VarInfo


@dataclass
class VarMapping:
    """Mapping between a Ghidra variable and its inferred register."""

    ghidra_name: str
    inferred_register: str  # e.g. "r31", "f30"
    type_prefix: str  # 'i', 'f', 'p', 'u', ''
    confidence: float  # 0.0-1.0


def infer_target_register_order(
    ghidra_vars: list[VarInfo],
    gpr_save_count: int | None = None,
) -> list[VarMapping]:
    """Infer which register each Ghidra variable maps to.

    Uses the callee-saved allocation rule:
    - 1st int-like variable in first-use order -> r31
    - 2nd int-like variable -> r30
    - 1st float variable -> f31
    - 2nd float variable -> f30

    Args:
        ghidra_vars: Variables from extract_variable_first_use_order()
        gpr_save_count: Number of saved GPRs (from __savegprlr_N)

    Returns:
        List of VarMapping sorted by first-use order.
    """
    int_vars: list[VarInfo] = []
    float_vars: list[VarInfo] = []

    for v in ghidra_vars:
        if v.type_prefix == "f" or _is_float_type(v.decl_type):
            float_vars.append(v)
        else:
            int_vars.append(v)

    mappings: list[VarMapping] = []

    # Map int-like variables to callee-saved GPRs (r31, r30, ...)
    max_gpr = gpr_save_count if gpr_save_count else len(int_vars)
    for i, v in enumerate(int_vars):
        if i >= max_gpr:
            break
        reg = f"r{31 - i}"
        if 31 - i < 13:
            break  # Below r13 is not callee-saved
        confidence = 0.7 if gpr_save_count else 0.5
        mappings.append(VarMapping(
            ghidra_name=v.name,
            inferred_register=reg,
            type_prefix=v.type_prefix,
            confidence=confidence,
        ))

    # Map float variables to callee-saved FPRs (f31, f30, ...)
    for i, v in enumerate(float_vars):
        reg = f"f{31 - i}"
        if 31 - i < 14:
            break  # Below f14 is not callee-saved
        mappings.append(VarMapping(
            ghidra_name=v.name,
            inferred_register=reg,
            type_prefix=v.type_prefix,
            confidence=0.6,
        ))

    return mappings


def ghidra_guided_reorder(
    ghidra_vars: list[VarInfo],
    source_decl_names: list[str],
    swap_pairs: list[tuple[str, str]],
    gpr_save_count: int | None = None,
) -> list[list[str]]:
    """Generate targeted declaration reorders using Ghidra variable order.

    Algorithm:
    1. Ghidra var order -> target register allocation (1st var -> r31, etc.)
    2. Source decl order -> our register allocation (same rule)
    3. For each swap pair, identify which source vars need to swap positions
    4. Generate only those targeted swaps

    Falls back gracefully if mapping confidence is too low.

    Args:
        ghidra_vars: Variables from Ghidra in first-use order
        source_decl_names: Our source variable names in declaration order
        swap_pairs: Register swap pairs from objdiff
        gpr_save_count: GPR save count from Ghidra __savegprlr_N

    Returns:
        List of candidate declaration orderings.
    """
    if len(source_decl_names) < 2 or not swap_pairs:
        return []

    # Infer target register assignments from Ghidra
    target_mappings = infer_target_register_order(ghidra_vars, gpr_save_count)
    if not target_mappings:
        return []

    # Build target: register -> Ghidra position (index in int/float var list)
    target_reg_to_pos: dict[str, int] = {}
    for i, m in enumerate(target_mappings):
        target_reg_to_pos[m.inferred_register] = i

    # Our source: position -> register (1st decl -> r31, etc.)
    # We need to separate int-like and float decls to match Ghidra's separation
    n_vars = len(source_decl_names)
    base_order = list(range(n_vars))
    candidates: list[list[str]] = []
    seen: set[tuple[str, ...]] = set()

    def _add_candidate(order: list[int]) -> None:
        candidate = [source_decl_names[k] for k in order]
        key = tuple(candidate)
        if key not in seen and list(order) != base_order:
            seen.add(key)
            candidates.append(candidate)

    # For each swap pair (rA, rB), find which position indices to swap
    targeted_swaps: list[tuple[int, int]] = []

    for rA, rB in swap_pairs:
        if not (rA.startswith("r") and rB.startswith("r")):
            continue

        # Under callee-saved rule: r31 = index 0, r30 = index 1, etc.
        idxA = 31 - int(rA[1:]) if rA.startswith("r") else None
        idxB = 31 - int(rB[1:]) if rB.startswith("r") else None

        if idxA is not None and idxB is not None:
            if 0 <= idxA < n_vars and 0 <= idxB < n_vars:
                pair = (min(idxA, idxB), max(idxA, idxB))
                if pair not in targeted_swaps:
                    targeted_swaps.append(pair)

    if not targeted_swaps:
        return []

    # Generate targeted swaps
    for i, j in targeted_swaps:
        new_order = list(base_order)
        new_order[i], new_order[j] = new_order[j], new_order[i]
        _add_candidate(new_order)

    # +-1 neighbor variants
    for i, j in targeted_swaps:
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                ni, nj = i + di, j + dj
                if ni == nj or ni < 0 or nj < 0 or ni >= n_vars or nj >= n_vars:
                    continue
                if di == 0 and dj == 0:
                    continue
                new_order = list(base_order)
                new_order[ni], new_order[nj] = new_order[nj], new_order[ni]
                _add_candidate(new_order)

    # Multi-swap: apply all targeted swaps simultaneously
    if len(targeted_swaps) > 1:
        new_order = list(base_order)
        for i, j in targeted_swaps:
            new_order[i], new_order[j] = new_order[j], new_order[i]
        _add_candidate(new_order)

    return candidates


def _is_float_type(decl_type: str) -> bool:
    """Check if a Ghidra declaration type represents a float."""
    t = decl_type.lower().strip()
    return t in ("float", "double", "float10")
