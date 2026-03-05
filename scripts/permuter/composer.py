"""Composition layer — apply patterns in sequence (2-stage and N-stage).

Enables multi-step fixes: e.g. extract a call into `auto` (variable_extraction),
then reorder that new declaration (declaration_reorder). Each pattern sees a
fresh AST from re-parsing the previous stage's output.

N-stage chains use beam search: at each stage, generate variants from the
current beam, prune to beam_width by source diversity, reparse for next stage.
"""

from __future__ import annotations

import sqlite3
import sys
from pathlib import Path
from typing import Iterator

from .extractor import reparse_variant
from .patterns.base import Pattern
from .types import ChainSpec, Diagnosis, FunctionContext, RoundHints, Variant

_DEFAULT_PAIRS: list[tuple[str, str]] = [
    ("variable_extraction", "declaration_reorder"),
    ("inline_assignment", "comparison_flip"),
    ("comparison_equivalence", "signed_unsigned"),
]

# Domain knowledge: which patterns are effective follow-ups to a given pattern.
_FOLLOW_UP_MAP: dict[str, list[str]] = {
    "variable_extraction": ["declaration_reorder", "inline_assignment"],
    "inline_assignment": ["comparison_flip", "comparison_equivalence"],
    "declaration_reorder": ["variable_extraction", "prologue_pressure"],
    "parameter_live_range": ["declaration_reorder", "prologue_pressure"],
    "comparison_equivalence": ["signed_unsigned", "comparison_flip"],
    "branch_polarity": ["comparison_flip", "early_return_merge"],
    "temp_elimination": ["declaration_reorder", "variable_extraction"],
    "member_ref_bind": ["declaration_reorder"],
    "reference_elimination": ["declaration_reorder", "temp_elimination"],
    "subscript_ref_bind": ["declaration_reorder"],
    "prologue_pressure": ["declaration_reorder", "parameter_live_range"],
    "signed_unsigned": ["comparison_equivalence", "comparison_flip"],
    "early_return_merge": ["branch_polarity", "guard_to_nested"],
    "guard_to_nested": ["early_return_merge", "branch_polarity"],
    "null_guard_elimination": ["branch_polarity", "comparison_flip"],
    "statement_reorder": ["declaration_reorder", "assignment_reorder"],
    "assignment_reorder": ["statement_reorder", "declaration_reorder"],
}

_CACHE_DB = Path(__file__).resolve().parent.parent.parent / "permuter_cache.db"


def compose_variants(
    ctx: FunctionContext,
    stage_a: Pattern,
    stage_b: Pattern,
    max_per_stage: int = 10,
    max_total: int = 50,
) -> Iterator[Variant]:
    """Apply pattern B to each output of pattern A.

    1. Generate up to max_per_stage variants from stage_a.
    2. For each, reparse_variant() to get a fresh AST.
    3. Run stage_b on the re-parsed context.
    4. Yield combined Variant with name like "varext_0+declreorder_3".
    5. Stop after max_total total composed variants.

    Args:
        ctx: Original function context.
        stage_a: First pattern to apply.
        stage_b: Second pattern to apply.
        max_per_stage: Max variants to take from stage_a.
        max_total: Max total composed variants to yield.
    """
    total = 0
    a_count = 0

    for a_variant in stage_a.generate(ctx):
        if a_count >= max_per_stage:
            break
        a_count += 1

        # Re-parse the stage A output to get fresh AST nodes
        try:
            reparsed_ctx = reparse_variant(ctx, a_variant.source)
        except ValueError:
            # Stage A produced invalid syntax — skip silently
            continue

        # Check if stage B is relevant for the re-parsed context
        if reparsed_ctx.diagnosis and not stage_b.relevant(reparsed_ctx.diagnosis):
            continue

        b_count = 0
        for b_variant in stage_b.generate(reparsed_ctx):
            if b_count >= max_per_stage:
                break
            b_count += 1

            yield Variant(
                name=f"{a_variant.name}+{b_variant.name}",
                pattern_name=f"compose:{stage_a.name}+{stage_b.name}",
                description=f"{a_variant.description} then {b_variant.description}",
                source=b_variant.source,
            )
            total += 1
            if total >= max_total:
                return

    return


def chain_variants(
    ctx: FunctionContext,
    chain: ChainSpec,
    pattern_map: dict[str, Pattern],
    beam_width: int = 5,
    max_per_stage: int = 8,
    max_total: int = 30,
) -> Iterator[Variant]:
    """Apply N patterns in sequence via beam search.

    At each stage:
    1. Generate variants from each beam entry using the stage's pattern.
    2. Prune to beam_width using source diversity heuristic.
    3. Reparse survivors for the next stage.

    Final stage yields all generated variants for scoring.

    Args:
        ctx: Original function context.
        chain: ChainSpec with ordered pattern names.
        pattern_map: Name->Pattern lookup.
        beam_width: Max beam entries carried between stages.
        max_per_stage: Max variants generated per beam entry per stage.
        max_total: Max total variants yielded.
    """
    stages = chain.stages
    if not stages:
        return

    # Verify all patterns exist
    for name in stages:
        if name not in pattern_map:
            return

    # Initial beam: the original context
    beam: list[tuple[FunctionContext, str, str]] = [
        (ctx, "", "")  # (context, accumulated_name, accumulated_desc)
    ]

    total_yielded = 0

    for stage_idx, pattern_name in enumerate(stages):
        pattern = pattern_map[pattern_name]
        is_final = stage_idx == len(stages) - 1
        candidates: list[tuple[Variant, FunctionContext | None, str, str]] = []

        for beam_ctx, acc_name, acc_desc in beam:
            count = 0
            for variant in pattern.generate(beam_ctx):
                count += 1
                if count > max_per_stage:
                    break

                # Build accumulated name chain
                if acc_name:
                    new_name = f"{acc_name}+{variant.name}"
                    new_desc = f"{acc_desc} then {variant.description}"
                else:
                    new_name = variant.name
                    new_desc = variant.description

                if is_final:
                    # Final stage — yield directly
                    chain_name = "+".join(stages)
                    yield Variant(
                        name=new_name,
                        pattern_name=f"chain:{chain_name}",
                        description=new_desc,
                        source=variant.source,
                    )
                    total_yielded += 1
                    if total_yielded >= max_total:
                        return
                else:
                    # Intermediate stage — collect for beam pruning
                    candidates.append((variant, None, new_name, new_desc))

        if is_final:
            return

        # Prune beam for next stage
        pruned = _prune_beam(candidates, ctx.file_source, beam_width)

        # Reparse pruned candidates for the next stage
        beam = []
        for variant, _, acc_name, acc_desc in pruned:
            try:
                reparsed = reparse_variant(ctx, variant.source)
                beam.append((reparsed, acc_name, acc_desc))
            except ValueError:
                continue  # Skip variants with syntax errors

        if not beam:
            return  # No valid candidates survived


def _prune_beam(
    candidates: list[tuple[Variant, object, str, str]],
    original_source: bytes,
    beam_width: int,
) -> list[tuple[Variant, object, str, str]]:
    """Prune candidates to beam_width by source diversity.

    Uses byte-level diff size from original as a diversity proxy.
    Selects candidates that are maximally different from each other.
    """
    if len(candidates) <= beam_width:
        return candidates

    # Score each candidate by edit distance from original (cheap proxy)
    scored: list[tuple[int, int, tuple]] = []
    for i, (variant, ctx_unused, acc_name, acc_desc) in enumerate(candidates):
        # Simple diversity metric: count differing bytes
        diff_count = _byte_diff_count(original_source, variant.source)
        scored.append((diff_count, i, (variant, ctx_unused, acc_name, acc_desc)))

    # Sort by diff size (most different first) and take diverse subset
    scored.sort(key=lambda x: -x[0])

    # Greedy diverse selection: take the most different, then spread out
    selected: list[tuple[Variant, object, str, str]] = []
    selected_diffs: list[int] = []

    for diff_count, _, entry in scored:
        if len(selected) >= beam_width:
            break
        # Accept if sufficiently different from already-selected
        if not selected_diffs or all(
            abs(diff_count - d) > 5 for d in selected_diffs
        ):
            selected.append(entry)
            selected_diffs.append(diff_count)

    # Fill remaining slots if greedy selection was too strict
    if len(selected) < beam_width:
        selected_set = {id(s[0]) for s in selected}
        for _, _, entry in scored:
            if len(selected) >= beam_width:
                break
            if id(entry[0]) not in selected_set:
                selected.append(entry)

    return selected


def _byte_diff_count(a: bytes, b: bytes) -> int:
    """Count differing bytes between two byte strings (cheap diversity proxy)."""
    min_len = min(len(a), len(b))
    diff = abs(len(a) - len(b))
    # Sample every 4th byte for speed on large files
    for i in range(0, min_len, 4):
        if a[i] != b[i]:
            diff += 1
    return diff


def build_adaptive_chains(
    diagnosis: Diagnosis | None,
    patterns: list[Pattern],
    hints: RoundHints | None,
    max_depth: int = 3,
    max_chains: int = 5,
) -> list[ChainSpec]:
    """Build data-driven chain specifications for N-stage composition.

    Priority layers:
    1. Follow-up chains from last winner (domain knowledge)
    2. Promising patterns (positive delta in previous rounds)
    3. Historical effective pairs from DB
    4. Diagnosis-driven chains

    Args:
        diagnosis: Current mismatch diagnosis.
        patterns: Available pattern instances.
        hints: Round history (None on first round).
        max_depth: Maximum chain depth.
        max_chains: Maximum chains to return.
    """
    available = {p.name for p in patterns}
    chains: list[ChainSpec] = []
    seen_stages: set[tuple[str, ...]] = set()

    def _add_chain(stages: list[str], reason: str, budget: int = 10) -> None:
        # Filter to available patterns
        valid = [s for s in stages if s in available]
        if len(valid) < 2:
            return
        key = tuple(valid[:max_depth])
        if key in seen_stages:
            return
        seen_stages.add(key)
        chains.append(ChainSpec(
            stages=list(key),
            reason=reason,
            budget=budget,
        ))

    # Layer 1: Follow-up chains from last winner
    if hints and hints.last_winner:
        for base_name in _split_for_lookup(hints.last_winner):
            follow_ups = _FOLLOW_UP_MAP.get(base_name, [])
            for follow_up in follow_ups:
                # 2-stage: winner -> follow_up
                _add_chain(
                    [base_name, follow_up],
                    f"follow-up: {base_name} won last round",
                )
                # 3-stage: winner -> follow_up -> follow_up's follow_ups
                for ff in _FOLLOW_UP_MAP.get(follow_up, [])[:1]:
                    _add_chain(
                        [base_name, follow_up, ff],
                        f"deep follow-up from {base_name}",
                    )

    # Layer 2: Promising patterns (had positive delta before)
    if hints:
        promising = hints.promising_patterns()
        for p in promising[:3]:
            follow_ups = _FOLLOW_UP_MAP.get(p, [])
            for f in follow_ups[:2]:
                _add_chain(
                    [p, f],
                    f"promising: {p} had positive delta",
                )

    # Layer 3: Historical effective pairs from DB
    effective_pairs = _query_effective_pairs()
    for p1, p2 in effective_pairs[:3]:
        _add_chain([p1, p2], f"historical: {p1}+{p2} won before")

    # Layer 4: Diagnosis-driven chains
    if diagnosis:
        for chain_spec in _diagnosis_driven_chains(diagnosis, available):
            stages_key = tuple(chain_spec.stages)
            if stages_key not in seen_stages:
                seen_stages.add(stages_key)
                chains.append(chain_spec)

    return chains[:max_chains]


def _split_for_lookup(pattern_name: str) -> list[str]:
    """Extract base pattern names from a possibly prefixed pattern name."""
    if pattern_name.startswith("compose:") or pattern_name.startswith("chain:"):
        _, parts = pattern_name.split(":", 1)
        return parts.split("+")
    return [pattern_name]


def _query_effective_pairs() -> list[tuple[str, str]]:
    """Query pattern_runs DB for composed patterns with wins."""
    try:
        conn = sqlite3.connect(str(_CACHE_DB))
        conn.row_factory = sqlite3.Row

        exists = conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='pattern_runs'"
        ).fetchone()
        if not exists:
            conn.close()
            return []

        rows = conn.execute("""
            SELECT pattern, SUM(won) as wins, COUNT(*) as runs
            FROM pattern_runs
            WHERE pattern LIKE 'compose:%' AND won = 1
            GROUP BY pattern
            HAVING wins > 0
            ORDER BY wins DESC
            LIMIT 10
        """).fetchall()
        conn.close()

        pairs = []
        for r in rows:
            # Parse "compose:a+b" -> (a, b)
            name = r["pattern"]
            if name.startswith("compose:"):
                parts = name[len("compose:"):].split("+")
                if len(parts) == 2:
                    pairs.append((parts[0], parts[1]))
        return pairs
    except Exception:
        return []


def _diagnosis_driven_chains(
    diagnosis: Diagnosis,
    available: set[str],
) -> list[ChainSpec]:
    """Generate chains based on diagnosis patterns."""
    chains: list[ChainSpec] = []

    # GPR regswaps -> register allocation chain
    if diagnosis.reg_swap_pairs:
        gpr_swaps = sum(
            1 for (a, b) in diagnosis.reg_swap_pairs
            if a.startswith("r") and b.startswith("r")
        )
        if gpr_swaps > 0:
            chains.append(ChainSpec(
                stages=["declaration_reorder", "prologue_pressure", "parameter_live_range"],
                reason=f"regalloc: {gpr_swaps} GPR swap pairs",
            ))
            chains.append(ChainSpec(
                stages=["member_ref_bind", "declaration_reorder"],
                reason=f"regalloc: bind members to fix register order",
            ))

    # Prologue mismatch -> pressure chain
    if diagnosis.has_prologue_mismatch:
        delta = diagnosis.gpr_save_delta
        if delta > 0:
            chains.append(ChainSpec(
                stages=["prologue_pressure", "declaration_reorder"],
                reason=f"prologue: target needs {delta} more GPR saves",
            ))
        elif delta < 0:
            chains.append(ChainSpec(
                stages=["temp_elimination", "reference_elimination"],
                reason=f"prologue: target needs {-delta} fewer GPR saves",
            ))

    # Comparison mismatches -> comparison chain
    cmp_mismatches = sum(
        1 for op in diagnosis.diff_ops
        if op.target_opcode.startswith("cmp") or op.base_opcode.startswith("cmp")
    )
    if cmp_mismatches > 0:
        chains.append(ChainSpec(
            stages=["comparison_equivalence", "signed_unsigned", "comparison_flip"],
            reason=f"comparison: {cmp_mismatches} cmp mismatches",
        ))

    # Branch mismatches -> branch chain
    branch_mismatches = sum(
        1 for op in diagnosis.diff_ops
        if op.target_opcode.startswith("b") or op.base_opcode.startswith("b")
    )
    if branch_mismatches > 0:
        chains.append(ChainSpec(
            stages=["branch_polarity", "comparison_flip", "early_return_merge"],
            reason=f"branch: {branch_mismatches} branch mismatches",
        ))

    # Filter to available patterns
    filtered = []
    for chain in chains:
        valid = [s for s in chain.stages if s in available]
        if len(valid) >= 2:
            filtered.append(ChainSpec(
                stages=valid,
                reason=chain.reason,
                budget=chain.budget,
            ))

    return filtered
