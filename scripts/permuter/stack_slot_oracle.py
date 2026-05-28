"""Stack-slot oracle — derive declaration-order constraints from MWCC DWARF.

A common, otherwise-blind mismatch class is *stack-slot assignment / ordering*:
the target spills a local to a slot the base keeps in a register, or two locals
occupy swapped slots. The existing constraint_solver resolves declaration order
from Ghidra/m2c *first-use order* — a proxy. This oracle adds a **ground-truth**
signal: it reads the base build's own DWARF (variable name -> r1 stack offset)
and the target-vs-base slot-layout diff (which offsets SWAPPED / DIFFER), then
maps the affected slots back to their *named source locals* and proposes the
declaration reorder (or force-to-stack) that aligns the base layout to target.

Pipeline
--------
    1. stack_layout.build_fingerprints + classify_slots   (target vs base slots)
    2. dwarf_locals.extract_locals (CACHED)               (base slot -> var name)
    3. correlate_slots()                                  (pure: rows+names -> recs)

Limits (bail = clean no-op, never crash):
    * Only the BASE side has DWARF names (we recompile *our* source). A SWAPPED
      pair has two base offsets, both nameable -> a concrete decl swap. A DIFFER
      row only names the base-side occupant; we can surface it but can't always
      name its target counterpart, so DIFFER recs are advisory.
    * If a slot maps to a compiler temp / param / unnamed local, skip it.
    * If DWARF extraction returns nothing (no pyelftools, compile failure,
      symbol not found) the whole oracle is a no-op.

The expensive step (DWARF recompile) is cached per-symbol in this process and on
disk by dwarf_locals itself (mtime-keyed .o in /tmp/claude/stack_dwarf).
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# scripts/analysis lives two levels up from this file's package; both it and the
# permuter import it as `scripts.analysis.*` (see diagnosis.py). Import lazily in
# the driver so the pure correlation logic (and its test) needs no heavy deps.


# ---------------------------------------------------------------------------
# Public result type
# ---------------------------------------------------------------------------

@dataclass
class SlotRecommendation:
    """One stack-layout-driven source recommendation.

    kind:
        "swap"         — swap declaration order of two named locals (high
                         confidence: both slots named, fingerprints exchanged).
        "force_stack"  — a target-only slot whose fingerprint matches a base
                         local kept in a register: force that local onto the
                         stack (advisory; the base name may be unknown).
        "differ"       — same offset, different occupant: a reorder candidate
                         naming the base-side local (advisory).
    """

    kind: str
    var_a: str
    var_b: str = ""
    offset_a: Optional[int] = None
    offset_b: Optional[int] = None
    confidence: float = 0.5
    note: str = ""


@dataclass
class SlotOracleResult:
    """Outcome of a stack-slot oracle query."""

    recommendations: list[SlotRecommendation] = field(default_factory=list)
    available: bool = False        # DWARF names were extracted
    skip_reason: str = ""

    @property
    def swap_pairs(self) -> list[tuple[str, str]]:
        """High-confidence (name_a, name_b) decl-swap pairs only."""
        return [
            (r.var_a, r.var_b)
            for r in self.recommendations
            if r.kind == "swap" and r.var_a and r.var_b
        ]


# ---------------------------------------------------------------------------
# Pure correlation logic (no I/O — unit-tested directly)
# ---------------------------------------------------------------------------

def correlate_slots(rows, base_names: dict) -> list[SlotRecommendation]:
    """Correlate classified stack-layout rows to named source locals.

    Args:
        rows: iterable of stack_layout.Row (or any object exposing
            ``verdict``, ``tgt_off``, ``base_off``, ``callee_save``). Only the
            attributes used here are required, so tests may pass lightweight
            stand-ins.
        base_names: {r1_offset: LocalInfo} from dwarf_locals.extract_locals
            (LocalInfo exposes ``.name`` and ``.is_param``). The mapping is for
            the BASE (our build) side only.

    Returns:
        A list of SlotRecommendation, most-confident (swaps) first. Pure — no
        side effects, deterministic ordering.
    """
    def name_of(off):
        if off is None:
            return None
        info = base_names.get(off)
        if info is None:
            return None
        # Skip params and unnamed/compiler temps — reordering those is unsafe.
        if getattr(info, "is_param", False):
            return None
        nm = getattr(info, "name", "") or ""
        if not nm or nm.startswith("_") or nm.startswith("$"):
            return None
        return nm

    swaps: list[SlotRecommendation] = []
    differs: list[SlotRecommendation] = []
    force_stack: list[SlotRecommendation] = []

    seen_swap_keys: set[tuple[int, int]] = set()

    for r in rows:
        verdict = getattr(r, "verdict", "")
        if getattr(r, "callee_save", False):
            continue  # prologue/epilogue slots — not source-reorderable

        if verdict == "SWAPPED":
            # The Row.note records the partner offset as "with 0x...". Both
            # slots are on the base side (same offsets, exchanged fingerprints),
            # so both are nameable from base_names.
            a_off = r.base_off if r.base_off is not None else r.tgt_off
            partner = _parse_partner_offset(getattr(r, "note", ""))
            if a_off is None or partner is None:
                continue
            key = tuple(sorted((a_off, partner)))
            if key in seen_swap_keys:
                continue
            name_a = name_of(a_off)
            name_b = name_of(partner)
            if not name_a or not name_b or name_a == name_b:
                continue
            seen_swap_keys.add(key)
            swaps.append(SlotRecommendation(
                kind="swap",
                var_a=name_a, var_b=name_b,
                offset_a=a_off, offset_b=partner,
                confidence=0.9,
                note=f"slots 0x{a_off:x} <-> 0x{partner:x} fingerprints exchanged",
            ))

        elif verdict == "DIFFER":
            off = r.base_off if r.base_off is not None else r.tgt_off
            name_a = name_of(off)
            if not name_a:
                continue
            differs.append(SlotRecommendation(
                kind="differ",
                var_a=name_a,
                offset_a=off,
                confidence=0.5,
                note=f"slot 0x{off:x} holds a different variable on target",
            ))

        elif verdict == "TGT_ONLY":
            # Target spills a local our build keeps in a register. We can't name
            # the target occupant directly (no target DWARF), but if a base
            # local of the same nature is kept in a register, forcing it to the
            # stack closes the gap. We surface the offset advisorily.
            off = r.tgt_off
            if off is None:
                continue
            force_stack.append(SlotRecommendation(
                kind="force_stack",
                var_a="",
                offset_a=off,
                confidence=0.3,
                note=f"target spills slot 0x{off:x}; our build keeps it in a register",
            ))

    # Deterministic order: swaps (most actionable), then differs, then advisory.
    return swaps + differs + force_stack


def _parse_partner_offset(note: str) -> Optional[int]:
    """Extract the partner offset from a SWAPPED Row.note ("with 0x...")."""
    import re
    m = re.search(r"0x([0-9a-fA-F]+)", note or "")
    if not m:
        return None
    try:
        return int(m.group(1), 16)
    except ValueError:
        return None


# ---------------------------------------------------------------------------
# Driver: obtain slot diff + DWARF names, then correlate (cached, I/O)
# ---------------------------------------------------------------------------

# In-process cache: symbol -> SlotOracleResult. The underlying DWARF recompile
# is also disk-cached by dwarf_locals (mtime-keyed). We additionally cache the
# whole result here so re-firing the solver every hill-climb round is free.
_RESULT_CACHE: dict[str, SlotOracleResult] = {}


def clear_cache() -> None:
    """Drop the in-process result cache (call after the source file changes)."""
    _RESULT_CACHE.clear()


def stack_slot_recommendations(
    symbol: str,
    unit: Optional[str] = None,
    project_dir: Optional[str] = None,
    objdiff_json: Optional[dict] = None,
    cache_key: Optional[str] = None,
) -> SlotOracleResult:
    """Run the stack-slot oracle for a function. Cached; never raises.

    Args:
        symbol: mangled function symbol.
        unit: objdiff unit (for disambiguation when re-running objdiff).
        project_dir: project root (defaults to repo root inferred by helpers).
        objdiff_json: optional pre-parsed objdiff diff JSON (with
            "instructions"). When provided we reuse it instead of re-invoking
            objdiff — the solver already has this from the baseline run.
        cache_key: override the cache key (default: symbol). Pass a key that
            varies with source content if you want per-source caching.

    Returns:
        SlotOracleResult. On any failure (no DWARF, no slot diff, missing deps)
        returns an empty, unavailable result — a clean no-op for the solver.
    """
    key = cache_key or symbol
    cached = _RESULT_CACHE.get(key)
    if cached is not None:
        return cached

    result = _compute(symbol, unit, project_dir, objdiff_json)
    _RESULT_CACHE[key] = result
    return result


def _compute(
    symbol: str,
    unit: Optional[str],
    project_dir: Optional[str],
    objdiff_json: Optional[dict],
) -> SlotOracleResult:
    try:
        from scripts.analysis import stack_layout as sl
    except Exception as exc:  # pragma: no cover - import guard
        return SlotOracleResult(skip_reason=f"stack_layout import failed: {exc}")

    # 1. Obtain the diff JSON (reuse the solver's if given; else run objdiff).
    instrs = None
    if objdiff_json and isinstance(objdiff_json, dict):
        instrs = objdiff_json.get("instructions")
    if not instrs:
        try:
            import json
            from scripts.analysis.diff_inspect import run_objdiff_for_symbol
            json_path = run_objdiff_for_symbol(
                symbol, project_dir=project_dir, unit=unit)
            with open(json_path) as f:
                instrs = json.load(f).get("instructions")
        except SystemExit as exc:
            # run_objdiff_for_symbol calls sys.exit(1) on build/unit-not-found
            # errors — catch the resulting SystemExit so the oracle stays a
            # clean no-op rather than killing the whole permuter process.
            return SlotOracleResult(skip_reason=f"objdiff exited: {exc}")
        except Exception as exc:
            return SlotOracleResult(skip_reason=f"objdiff failed: {exc}")
    if not instrs:
        return SlotOracleResult(skip_reason="no instructions in diff")

    # 2. Build target/base slot fingerprints + classify (SWAPPED/DIFFER/...).
    try:
        tgt_slots = sl.build_fingerprints("target", instrs)
        base_slots = sl.build_fingerprints("base", instrs)
        tgt_prol = sl.parse_prologue(instrs, "target")
        base_prol = sl.parse_prologue(instrs, "base")
        dominant_delta = sl.dominant_delta_from_rows(tgt_slots, base_slots)
        rows = sl.classify_slots(
            tgt_slots, base_slots, dominant_delta,
            tgt_prol.callee_save_slots, base_prol.callee_save_slots,
        )
    except Exception as exc:
        return SlotOracleResult(skip_reason=f"slot classification failed: {exc}")

    # Cheap early-out: if nothing actionable, skip the expensive DWARF recompile.
    actionable = [r for r in rows
                  if r.verdict in ("SWAPPED", "DIFFER", "TGT_ONLY")
                  and not r.callee_save]
    if not actionable:
        return SlotOracleResult(skip_reason="no SWAPPED/DIFFER/TGT_ONLY user slots")

    # 3. Extract base-side DWARF names (EXPENSIVE — recompile). Cached on disk.
    try:
        from scripts.analysis import dwarf_locals
        base_names = dwarf_locals.extract_locals(symbol, project_dir)
    except Exception as exc:
        return SlotOracleResult(skip_reason=f"dwarf extraction failed: {exc}")

    if not base_names:
        return SlotOracleResult(skip_reason="no DWARF locals extracted")

    # 4. Correlate (pure).
    recs = correlate_slots(rows, base_names)
    return SlotOracleResult(
        recommendations=recs,
        available=True,
        skip_reason="" if recs else "names extracted but no slot correlated",
    )


# ---------------------------------------------------------------------------
# CLI for manual inspection
# ---------------------------------------------------------------------------

def main() -> None:  # pragma: no cover - manual tool
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("symbol")
    parser.add_argument("--unit", default=None)
    parser.add_argument("--project-dir", default=None)
    args = parser.parse_args()

    res = stack_slot_recommendations(
        args.symbol, unit=args.unit, project_dir=args.project_dir)
    if not res.available:
        print(f"[stack_slot_oracle] no recommendations: {res.skip_reason}",
              file=sys.stderr)
        sys.exit(1)
    print(f"{len(res.recommendations)} recommendation(s):")
    for r in res.recommendations:
        loc = f"0x{r.offset_a:x}" if r.offset_a is not None else "?"
        if r.kind == "swap":
            print(f"  [swap  conf={r.confidence:.1f}] {r.var_a} <-> {r.var_b}  "
                  f"({r.note})")
        else:
            print(f"  [{r.kind:11s} conf={r.confidence:.1f}] {r.var_a or loc}  "
                  f"({r.note})")


if __name__ == "__main__":
    main()
