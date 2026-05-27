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
    3. Emit Ghidra-order-driven candidates (independent of objdiff swap_pairs)
       that re-shape the source decl order toward Ghidra's first-use order.
    4. If swap_pairs are present, also emit targeted swaps and neighbor variants.

    Falls back gracefully if mapping confidence is too low.

    Args:
        ghidra_vars: Variables from Ghidra in first-use order
        source_decl_names: Our source variable names in declaration order
        swap_pairs: Register swap pairs from objdiff (may be empty)
        gpr_save_count: GPR save count from Ghidra __savegprlr_N

    Returns:
        List of candidate declaration orderings.
    """
    # Note: do NOT early-return on empty swap_pairs — the Ghidra-order-driven
    # candidate path below is meant to fire exactly when swap detection is
    # noisy/empty but Ghidra's order is reliable (C2 fix).
    if len(source_decl_names) < 2:
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

    def _add_named_candidate(candidate: list[str]) -> None:
        key = tuple(candidate)
        if key not in seen and candidate != source_decl_names:
            seen.add(key)
            candidates.append(candidate)

    # ------------------------------------------------------------------
    # Name-matched candidate (highest preference when names overlap).
    # ------------------------------------------------------------------
    # When the decompiler's local names overlap our source decl names — the
    # case when debug info (DWARF/PDB) gives both the same identifiers, or when
    # comparing two of OUR hypotheses against OUR source — we can pick the EXACT
    # target permutation: reorder source decls into the decompiler's first-use
    # order, keeping any source-only names in their relative position. This is
    # the original C2 vision ("reorder declarations to match the decompiler's
    # first-use order") and the lever that makes a Ghidra-vs-m2c *disagreement*
    # yield two genuinely different reorders (each names a different order).
    ghidra_name_order = [v.name for v in ghidra_vars]
    overlap = [n for n in ghidra_name_order if n in set(source_decl_names)]
    if len(overlap) >= 2:
        # Build target order: walk source positions; positions whose name is in
        # the overlap get filled from the decompiler's order, others stay put.
        overlap_iter = iter(overlap)
        named_order: list[str] = []
        overlap_set = set(overlap)
        for name in source_decl_names:
            if name in overlap_set:
                named_order.append(next(overlap_iter))
            else:
                named_order.append(name)
        _add_named_candidate(named_order)

    # ------------------------------------------------------------------
    # Ghidra-order-driven candidates (additive; fires with no swap_pairs)
    # ------------------------------------------------------------------
    # When objdiff swap-pair detection is empty/noisy (>=3-way rotations,
    # mixed GPR/FPR, partial diffs) Ghidra's first-use order is often still
    # reliable. Without a Ghidra-name-to-source-name map, we can't pick the
    # exact permutation, but we can emit a small set of plausible "Ghidra
    # disagrees with our order" reorders, scoped to as many positions as
    # Ghidra inferred mappings for (typically a callee-saved prefix).
    #
    # target_reg_to_pos has rXX keys mapping to int-position indices and fXX
    # keys mapping to float-position indices. We use the *count* of int-keys
    # to decide how many leading source positions are reasonable to permute
    # (since callee-saved GPRs are r31, r30, ... allocated in source decl
    # order; only those positions can be re-shuffled to change the binding).
    int_reg_count = sum(1 for reg in target_reg_to_pos if reg.startswith("r"))
    permute_window = min(int_reg_count, n_vars)
    if permute_window >= 2:
        # Candidate A: reverse the leading window. Highest-signal "opposite
        # order" guess — covers the most common regswap shape.
        rev_order = list(base_order)
        rev_window = list(reversed(rev_order[:permute_window]))
        rev_order[:permute_window] = rev_window
        _add_candidate(rev_order)

        # Candidate B-set: adjacent-pair swaps in the leading window. These
        # cover localized "two-adjacent-vars are mis-ordered" shapes that
        # the swap_pairs path normally handles, when swap_pairs is empty.
        for i in range(permute_window - 1):
            adj_order = list(base_order)
            adj_order[i], adj_order[i + 1] = adj_order[i + 1], adj_order[i]
            _add_candidate(adj_order)

        # Candidate C: rotate left by 1 (everyone shifts down a register).
        # Helps when source has one *extra* leading decl vs Ghidra.
        if permute_window >= 3:
            rot_order = list(base_order)
            head = rot_order[:permute_window]
            rot_order[:permute_window] = head[1:] + head[:1]
            _add_candidate(rot_order)

    # ------------------------------------------------------------------
    # Swap-pair-driven candidates (existing logic; unchanged behavior)
    # ------------------------------------------------------------------
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
        return candidates  # may be empty or only contain Ghidra-order candidates

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


# ---------------------------------------------------------------------------
# Combining two independent decompilers' variable orders (Ghidra + m2c)
# ---------------------------------------------------------------------------
#
# Ghidra and m2c are independent views of the same target binary. Neither names
# locals the way our source does, so we can't cross-map by name — but each
# produces a *first-use order* that reflects the target's register-allocation
# order. When BOTH are present we treat their relationship as a signal:
#
#   * agree     -> high confidence; the shared ordering is the preferred guess.
#   * disagree  -> two competing hypotheses; we should try BOTH orderings rather
#                  than silently preferring one (the search then covers both).
#   * one-only  -> use that one (the historical behavior).


@dataclass
class VarOrderConsensus:
    """Result of combining Ghidra and m2c variable first-use orders.

    ``verdict`` is one of:
        "agree"        — both decompilers present and consistent on shared vars
        "disagree"     — both present but the shared-var relative order differs
        "ghidra_only"  — only Ghidra produced an order
        "m2c_only"     — only m2c produced an order
        "none"         — neither produced a usable order

    ``orders`` is the ordered list of VarInfo orderings to try, most-preferred
    first. For "agree"/single-source it holds one order; for "disagree" it holds
    both (Ghidra first, then m2c) so callers can emit a hypothesis per order.

    ``high_confidence`` is True only for "agree" — a clean signal the decl-order
    constraint should win on priority / be the preferred synthesized candidate.
    """

    verdict: str
    orders: list[list[VarInfo]]
    high_confidence: bool


def _shared_relative_order_matches(
    a: list[VarInfo], b: list[VarInfo],
) -> bool:
    """Do two var orders agree on the relative order of the locals they share?

    We can only compare by *name*, and Ghidra/m2c invent different local names,
    so in practice the shared set is the subset of names that happen to coincide
    (e.g. user-named locals when debug info is present, or identical sp/stack
    slot names). When the shared set is empty there is nothing to disagree about,
    so we conservatively treat that as agreement (no conflict signal) — the
    callers still get both orders to try via the single-source path.
    """
    a_names = [v.name for v in a]
    b_names = [v.name for v in b]
    shared = set(a_names) & set(b_names)
    if len(shared) < 2:
        return True  # nothing comparable -> no conflict
    a_seq = [n for n in a_names if n in shared]
    b_seq = [n for n in b_names if n in shared]
    return a_seq == b_seq


def combine_var_orders(
    ghidra_vars: list[VarInfo] | None,
    m2c_vars: list[VarInfo] | None,
) -> VarOrderConsensus:
    """Combine Ghidra + m2c variable first-use orders into a consensus.

    This is the join point that turns "m2c is a fallback" into "Ghidra and m2c
    are two independent views we exploit together":

      * Both present & agree on shared locals -> high-confidence single order
        (prefer Ghidra's VarInfo list, which carries richer type info).
      * Both present & disagree                -> return BOTH orders so the
        caller can synthesize a candidate per hypothesis (bounded: at most 2).
      * Exactly one present                    -> that one (preserved behavior).
      * Neither                                 -> empty.
    """
    g = ghidra_vars or []
    m = m2c_vars or []

    if g and m:
        if _shared_relative_order_matches(g, m):
            # Agreement: one preferred order. Prefer Ghidra's VarInfo because it
            # carries decl_type/type_prefix from the AST; m2c's is text-derived.
            return VarOrderConsensus(
                verdict="agree",
                orders=[g],
                high_confidence=True,
            )
        # Disagreement: emit both hypotheses, Ghidra first (slightly preferred
        # as the historically primary source), m2c second.
        return VarOrderConsensus(
            verdict="disagree",
            orders=[g, m],
            high_confidence=False,
        )

    if g:
        return VarOrderConsensus(
            verdict="ghidra_only", orders=[g], high_confidence=False,
        )
    if m:
        return VarOrderConsensus(
            verdict="m2c_only", orders=[m], high_confidence=False,
        )
    return VarOrderConsensus(verdict="none", orders=[], high_confidence=False)
