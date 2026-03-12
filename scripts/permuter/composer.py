"""Composition layer — apply patterns in sequence (2-stage and N-stage).

Enables multi-step fixes: e.g. extract a call into `auto` (variable_extraction),
then reorder that new declaration (declaration_reorder). Each pattern sees a
fresh AST from re-parsing the previous stage's output.

N-stage chains use beam search: at each stage, generate variants from the
current beam, prune to beam_width by source diversity, reparse for next stage.
"""

from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Iterator

from .extractor import reparse_variant
from .patterns.base import Pattern
from .repo_paths import get_cache_db_path
from .types import (
    ChainSpec,
    Diagnosis,
    FunctionContext,
    RoundHints,
    Variant,
    merge_auxiliary_file_sets,
    variant_identity_bytes,
)

_DEFAULT_PAIRS: list[tuple[str, str]] = [
    ("variable_extraction", "declaration_reorder"),
    ("inline_assignment", "comparison_flip"),
    ("comparison_equivalence", "signed_unsigned"),
]

# Domain knowledge: which patterns are effective follow-ups to a given pattern.
_FOLLOW_UP_MAP: dict[str, list[str]] = {
    # Core extraction/ordering
    "variable_extraction": ["declaration_reorder", "inline_assignment"],
    "inline_assignment": ["comparison_flip", "comparison_equivalence"],
    "declaration_reorder": ["variable_extraction", "prologue_pressure", "declaration_movement"],
    "parameter_live_range": ["declaration_reorder", "prologue_pressure"],
    "statement_reorder": ["declaration_reorder", "assignment_reorder", "declaration_movement"],
    "assignment_reorder": ["statement_reorder", "declaration_reorder"],
    "temp_elimination": ["declaration_reorder", "variable_extraction"],
    "member_ref_bind": ["declaration_reorder"],
    "reference_elimination": ["declaration_reorder", "temp_elimination"],
    "value_address_caching": ["declaration_reorder", "prologue_pressure"],
    "subscript_ref_bind": ["declaration_reorder"],
    "prologue_pressure": ["declaration_reorder", "parameter_live_range"],
    "color_copy_shape": ["statement_reorder", "declaration_reorder"],
    "native_guard_camera_wrap": ["statement_reorder"],

    # Declaration/ordering (newly connected)
    "declaration_movement": ["declaration_reorder", "statement_reorder"],
    "comma_split": ["statement_reorder", "declaration_reorder"],
    "hoist_sret": ["declaration_reorder", "statement_reorder"],
    "alloca_intrinsic": ["declaration_reorder"],
    "commutative_swap": ["declaration_reorder"],
    "initializer_literal": ["declaration_reorder"],
    "scope_narrowing": ["declaration_reorder", "value_address_caching"],

    # Comparison/boolean
    "comparison_equivalence": ["signed_unsigned", "comparison_flip"],
    "signed_unsigned": ["comparison_equivalence", "comparison_flip"],
    "branch_polarity": ["comparison_flip", "early_return_merge", "switch_if_convert"],
    "null_guard_elimination": ["branch_polarity", "comparison_flip"],
    "bool_cast": ["comparison_flip", "signed_unsigned"],
    "bool_return_expr": ["comparison_flip", "branch_polarity"],
    "bool_to_uchar": ["signed_unsigned", "comparison_equivalence"],
    "objptr_bool_extract": ["comparison_flip", "bool_cast"],
    "bit_test_bool": ["comparison_flip", "bool_cast"],

    # Expression-level
    "and_split": ["declaration_reorder", "statement_reorder"],
    "negation_split": ["comparison_flip", "branch_polarity"],
    "max_to_conditional": ["branch_polarity", "comparison_flip"],
    "bitwise_accumulator": ["declaration_reorder", "statement_reorder"],
    "fma_reorder": ["declaration_reorder"],

    # Float/type
    "float_double_literal": ["comparison_equivalence", "fabs_variant"],
    "fabs_variant": ["float_double_literal", "math_return_cast"],
    "math_return_cast": ["fabs_variant", "signed_unsigned"],
    "sizeof_signed_cast": ["signed_unsigned", "comparison_equivalence"],
    "empty_size_swap": ["comparison_equivalence", "signed_unsigned"],

    # MILO macros
    "milo_log_swap": ["milo_str_conv", "milo_call_merge"],
    "milo_str_conv": ["milo_log_swap", "varargs_cast"],
    "milo_call_merge": ["milo_log_swap", "declaration_reorder"],
    "varargs_cast": ["milo_str_conv"],

    # Control flow
    "early_return_merge": ["branch_polarity", "guard_to_nested"],
    "guard_to_nested": ["early_return_merge", "branch_polarity"],
    "single_return": ["branch_polarity", "early_return_merge"],
    "switch_if_convert": ["branch_polarity", "declaration_reorder"],
    "ternary_swap": ["comparison_flip", "branch_polarity"],
    "fsel_template": ["comparison_flip", "branch_polarity"],
    "noreturn_attr": ["branch_polarity"],
    "return_call_merge": ["branch_polarity", "declaration_reorder"],
    "redundant_guard_elimination": ["branch_polarity", "comparison_flip"],

    # Cross-unit / inlining
    "accessor_outline": ["declaration_reorder", "value_address_caching"],

    # Misc
    "argument_swap": ["declaration_reorder", "comparison_flip"],
    "iterator_deref_style": ["member_ref_bind", "declaration_reorder"],
    "const_overload": ["comparison_equivalence"],
    "handler_inline": ["temp_elimination", "declaration_reorder"],
}

_TAG_FOLLOW_UP_MAP: dict[str, list[str]] = {
    "introduced_temp": ["declaration_reorder", "inline_assignment", "statement_reorder"],
    "moved_declaration": ["declaration_reorder", "statement_reorder"],
    "reordered_declarations": ["variable_extraction", "prologue_pressure", "declaration_movement"],
    "reordered_assignments": ["statement_reorder", "declaration_reorder"],
    "reordered_statements": ["declaration_reorder", "assignment_reorder"],
    "merged_return_calls": ["branch_polarity", "declaration_reorder", "early_return_merge"],
    "split_return_calls": ["branch_polarity", "early_return_merge"],
    "reordered_tail_calls": ["branch_polarity", "declaration_reorder"],
    "converted_if_to_switch": ["branch_polarity", "declaration_reorder"],
    "converted_switch_to_if": ["branch_polarity", "ternary_swap"],
}

_CACHE_DB = get_cache_db_path()


def _merged_follow_up_map(patterns: list[Pattern]) -> dict[str, list[str]]:
    """Merge static follow-up wiring with pattern-declared follow-ups."""
    merged: dict[str, list[str]] = {
        name: list(follow_ups) for name, follow_ups in _FOLLOW_UP_MAP.items()
    }

    for pattern in patterns:
        bucket = merged.setdefault(pattern.name, [])
        for follow_up in pattern.follow_ups:
            if follow_up not in bucket:
                bucket.append(follow_up)

    return merged


def available_context_keys(ctx: FunctionContext) -> set[str]:
    """Return auxiliary context keys available for the current function."""
    keys: set[str] = set()
    if ctx.ghidra_code or ctx.ghidra_ast is not None:
        keys.add("ghidra")
    if ctx.m2c_code:
        keys.add("m2c")
    if ctx.asm_listing_path is not None:
        keys.add("asm")
    if ctx.rb3_source:
        keys.add("rb3")
    if ctx.diagnosis is not None:
        keys.add("diagnosis")
    return keys


def _context_score(pattern: Pattern, available_context: set[str] | None) -> float:
    """Return a soft score adjustment from auxiliary context availability."""
    if not pattern.requires_context:
        return 0.0
    if not available_context:
        return -0.2
    required = set(pattern.requires_context)
    if required.issubset(available_context):
        return 0.2
    return -0.2


def _pattern_relevant(
    pattern: Pattern,
    diagnosis: Diagnosis | None,
    round_hints: RoundHints | None = None,
) -> bool:
    """Return True if a pattern should participate for this diagnosis."""
    if diagnosis is None:
        return True
    if round_hints and round_hints.force_pattern(pattern.name):
        return True
    return pattern.relevant(diagnosis)


def compose_variants(
    ctx: FunctionContext,
    stage_a: Pattern,
    stage_b: Pattern,
    max_per_stage: int = 10,
    max_total: int = 50,
    round_hints: RoundHints | None = None,
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
        if not _pattern_relevant(stage_b, reparsed_ctx.diagnosis, round_hints):
            continue

        b_count = 0
        for b_variant in stage_b.generate(reparsed_ctx):
            if b_count >= max_per_stage:
                break
            b_count += 1
            auxiliary_files = merge_auxiliary_file_sets(
                a_variant.auxiliary_files,
                b_variant.auxiliary_files,
            )
            if auxiliary_files is None:
                continue

            yield Variant(
                name=f"{a_variant.name}+{b_variant.name}",
                pattern_name=f"compose:{stage_a.name}+{stage_b.name}",
                description=f"{a_variant.description} then {b_variant.description}",
                source=b_variant.source,
                tags=a_variant.tags | b_variant.tags,
                auxiliary_files=auxiliary_files,
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
    If the beam dies at an intermediate stage, yields partial chain output
    from the last successful stage as shorter chains.

    At intermediate stages (stage > 0, not final), diagnosis is temporarily
    suppressed to prevent pattern relevance checks from killing the beam.

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
    beam: list[tuple[FunctionContext, str, str, frozenset[str]]] = [
        (ctx, "", "", frozenset())  # (context, accumulated_name, accumulated_desc, accumulated_tags)
    ]

    total_yielded = 0
    # Track last stage's candidates for fallback yield on beam death
    last_intermediate: list[tuple[Variant, str, str, frozenset[str]]] = []

    for stage_idx, pattern_name in enumerate(stages):
        pattern = pattern_map[pattern_name]
        is_final = stage_idx == len(stages) - 1
        candidates: list[tuple[Variant, FunctionContext | None, str, str, frozenset[str]]] = []

        for beam_ctx, acc_name, acc_desc, acc_tags in beam:
            # At intermediate stages (not first, not final), suppress
            # diagnosis to prevent pattern relevance filtering from
            # killing the beam. Patterns may not be relevant to the
            # current diagnosis but are useful as stepping stones.
            saved_diagnosis = beam_ctx.diagnosis
            if not is_final and stage_idx > 0:
                beam_ctx.diagnosis = None

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
                new_tags = acc_tags | variant.tags

                if is_final:
                    # Final stage — yield directly
                    chain_name = "+".join(stages)
                    yield Variant(
                        name=new_name,
                        pattern_name=f"chain:{chain_name}",
                        description=new_desc,
                        source=variant.source,
                        tags=new_tags,
                    )
                    total_yielded += 1
                    if total_yielded >= max_total:
                        return
                else:
                    # Intermediate stage — collect for beam pruning
                    candidates.append((variant, None, new_name, new_desc, new_tags))

            # Restore diagnosis
            if not is_final and stage_idx > 0:
                beam_ctx.diagnosis = saved_diagnosis

        if is_final:
            # If final stage produced nothing, fall back to intermediates
            if total_yielded == 0 and last_intermediate:
                partial_name = "+".join(stages[:stage_idx])
                for variant, acc_name, acc_desc, acc_tags in last_intermediate:
                    yield Variant(
                        name=acc_name,
                        pattern_name=f"chain:{partial_name}",
                        description=acc_desc,
                        source=variant.source,
                        tags=acc_tags,
                    )
                    total_yielded += 1
                    if total_yielded >= max_total:
                        return
            return

        # Save intermediate candidates for potential fallback
        last_intermediate = [
            (v, n, d, t) for v, _, n, d, t in candidates
        ]

        # Prune beam for next stage
        pruned = _prune_beam(candidates, ctx.file_source, beam_width)

        # Reparse pruned candidates for the next stage
        beam = []
        for variant, _, acc_name, acc_desc, acc_tags in pruned:
            try:
                reparsed = reparse_variant(ctx, variant.source)
                beam.append((reparsed, acc_name, acc_desc, acc_tags))
            except ValueError:
                continue  # Skip variants with syntax errors

        if not beam:
            # Beam died — yield intermediates as shorter chains
            if last_intermediate and stage_idx >= 1:
                partial_name = "+".join(stages[:stage_idx + 1])
                for variant, acc_name, acc_desc, acc_tags in last_intermediate:
                    yield Variant(
                        name=acc_name,
                        pattern_name=f"chain:{partial_name}",
                        description=acc_desc,
                        source=variant.source,
                        tags=acc_tags,
                    )
                    total_yielded += 1
                    if total_yielded >= max_total:
                        return
            return


def _prune_beam(
    candidates: list[tuple[Variant, object, str, str, frozenset[str]]],
    original_source: bytes,
    beam_width: int,
) -> list[tuple[Variant, object, str, str, frozenset[str]]]:
    """Prune candidates to beam_width by source diversity.

    Uses byte-level diff size from original as a diversity proxy.
    Selects candidates that are maximally different from each other.
    """
    if len(candidates) <= beam_width:
        return candidates

    # Score each candidate by edit distance from original (cheap proxy)
    scored: list[tuple[int, int, tuple]] = []
    for i, entry in enumerate(candidates):
        variant = entry[0]
        # Simple diversity metric: count differing bytes
        diff_count = _byte_diff_count(original_source, variant.source)
        scored.append((diff_count, i, entry))

    # Sort by diff size (most different first) and take diverse subset
    scored.sort(key=lambda x: -x[0])

    # Greedy diverse selection: take the most different, then spread out
    selected: list[tuple[Variant, object, str, str, frozenset[str]]] = []
    selected_diffs: list[int] = []

    for diff_count, _, entry in scored:
        if len(selected) >= beam_width:
            break
        # Accept if sufficiently different from already-selected.
        # Use relative threshold to prevent over-filtering small edits.
        min_gap = max(3, diff_count // 10)
        if not selected_diffs or all(
            abs(diff_count - d) > min_gap for d in selected_diffs
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
    available_context: set[str] | None = None,
    max_depth: int = 3,
    max_chains: int = 10,
) -> list[ChainSpec]:
    """Build data-driven chain specifications for N-stage composition.

    Priority layers:
    1. Follow-up chains from last winner (recursive walk)
    2. Promising patterns (positive delta in previous rounds)
    2.5. Round-1 diagnosis-relevant pairwise combos (when no hints)
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
    follow_map = _merged_follow_up_map(patterns)
    pattern_map = {p.name: p for p in patterns}
    chains: list[ChainSpec] = []
    seen_stages: set[tuple[str, ...]] = set()

    def _add_chain(
        stages: list[str], reason: str, budget: int = 10, priority: float = 0.0,
    ) -> None:
        # Filter to available patterns
        valid = [s for s in stages if s in available]
        if len(valid) < 2:
            return
        key = tuple(valid[:max_depth])
        if key in seen_stages:
            return
        seen_stages.add(key)
        priority += sum(
            _context_score(pattern_map[name], available_context)
            for name in key
            if name in pattern_map
        )
        chains.append(ChainSpec(
            stages=list(key),
            reason=reason,
            budget=budget,
            priority=priority,
        ))

    # Layer 1: Follow-up chains from last winner (recursive walk)
    if hints and hints.last_winner:
        for base_name in _split_for_lookup(hints.last_winner):
            for chain_stages in _walk_followups_seeded(
                base_name,
                _follow_up_names_for(base_name, hints.last_winner_tags),
                follow_map,
                max_depth,
                available,
            ):
                _add_chain(
                    chain_stages,
                    f"follow-up: {base_name} won last round",
                    priority=1.0,
                )

    # Layer 2: Promising patterns (had positive delta before)
    if hints:
        promising = hints.promising_patterns()
        for p in promising[:5]:
            for chain_stages in _walk_followups_seeded(
                p,
                _follow_up_names_for(p, hints.promising_tags_for_pattern(p)),
                follow_map,
                min(3, max_depth),
                available,
            ):
                _add_chain(
                    chain_stages,
                    f"promising: {p} had positive delta",
                    priority=0.8,
                )

    # Layer 2.5: Round-1 diagnosis-relevant combos (no hints yet)
    if not hints and diagnosis:
        relevant_names = [
            p.name for p in patterns if _pattern_relevant(p, diagnosis, hints)
        ]
        for name in relevant_names:
            follow_ups = follow_map.get(name, [])
            for fu in follow_ups:
                if fu in available and fu in {n for n in relevant_names}:
                    _add_chain(
                        [name, fu],
                        f"round1-relevant: {name}+{fu}",
                        priority=0.5,
                    )

    # Layer 3: Historical effective pairs from DB (with data-driven confidence)
    effective_pairs = _query_effective_pairs(hints)
    for (p1, p2), confidence in effective_pairs[:5]:
        # Skip suppressed pairs
        if hints and (p1, p2) in hints.suppress_pairs:
            continue
        _add_chain(
            [p1, p2], f"historical: {p1}+{p2} won before (conf={confidence:.2f})",
            priority=confidence,
        )

    # Layer 4: Diagnosis-driven chains
    if diagnosis:
        for chain_spec in _diagnosis_driven_chains(diagnosis, available):
            stages_key = tuple(chain_spec.stages)
            if stages_key not in seen_stages:
                seen_stages.add(stages_key)
                chains.append(chain_spec)

    # Sort by priority (highest first) and truncate
    chains.sort(key=lambda c: -c.priority)
    return chains[:max_chains]


def _walk_followups(
    start: str,
    follow_map: dict[str, list[str]],
    max_depth: int,
    available: set[str],
    _max_chains: int = 6,
) -> list[list[str]]:
    """BFS walk of follow-up map from start, yielding chains of length 2..max_depth.

    Cycle-safe (tracks visited per chain). Returns shorter chains first.
    Caps at _max_chains to prevent explosion.
    """
    results: list[list[str]] = []
    # BFS queue: (current_chain, visited_set)
    queue: list[tuple[list[str], set[str]]] = [([start], {start})]

    while queue and len(results) < _max_chains:
        chain, visited = queue.pop(0)
        tail = chain[-1]
        follow_ups = follow_map.get(tail, [])
        for fu in follow_ups:
            if fu in visited or fu not in available:
                continue
            new_chain = chain + [fu]
            if len(new_chain) >= 2:
                results.append(new_chain)
                if len(results) >= _max_chains:
                    break
            if len(new_chain) < max_depth:
                queue.append((new_chain, visited | {fu}))

    return results


def _walk_followups_seeded(
    start: str,
    initial_follow_ups: set[str],
    follow_map: dict[str, list[str]],
    max_depth: int,
    available: set[str],
    _max_chains: int = 6,
) -> list[list[str]]:
    """Walk follow-ups, allowing tag-derived first-hop expansions."""
    seeded = [
        follow_up for follow_up in initial_follow_ups
        if follow_up in available and follow_up != start
    ]
    if not seeded:
        return []

    results: list[list[str]] = []
    seen: set[tuple[str, ...]] = set()
    queue: list[tuple[list[str], set[str]]] = []

    for follow_up in seeded:
        chain = [start, follow_up]
        key = tuple(chain)
        if key in seen:
            continue
        seen.add(key)
        results.append(chain)
        if len(results) >= _max_chains:
            return results
        if len(chain) < max_depth:
            queue.append((chain, {start, follow_up}))

    while queue and len(results) < _max_chains:
        chain, visited = queue.pop(0)
        tail = chain[-1]
        for follow_up in follow_map.get(tail, []):
            if follow_up in visited or follow_up not in available:
                continue
            new_chain = chain + [follow_up]
            key = tuple(new_chain)
            if key in seen:
                continue
            seen.add(key)
            results.append(new_chain)
            if len(results) >= _max_chains:
                break
            if len(new_chain) < max_depth:
                queue.append((new_chain, visited | {follow_up}))

    return results


def get_compose_pairs(
    diagnosis: Diagnosis | None,
    patterns: list[Pattern],
    hints: RoundHints | None = None,
    available_context: set[str] | None = None,
    max_pairs: int = 12,
) -> list[tuple[str, str]]:
    """Build dynamic compose pairs from _FOLLOW_UP_MAP + DB history.

    1. Collect all edges from _FOLLOW_UP_MAP where both patterns are available.
    2. Filter by diagnosis relevance (at least one pattern passes relevant()).
    3. Boost pairs that appear in DB win history.
    4. Return top max_pairs.
    """
    available = {p.name for p in patterns}
    pattern_map = {p.name: p for p in patterns}
    follow_map = _merged_follow_up_map(patterns)

    # Collect all valid edges
    candidates: list[tuple[str, str, float]] = []  # (a, b, score)
    for src, dsts in follow_map.items():
        if src not in available:
            continue
        for dst in dsts:
            if dst not in available:
                continue
            # Score: 1.0 base, +0.5 if at least one is diagnosis-relevant
            score = 1.0
            src_pat = pattern_map.get(src)
            dst_pat = pattern_map.get(dst)
            if diagnosis:
                if (src_pat and _pattern_relevant(src_pat, diagnosis, hints)) or \
                   (dst_pat and _pattern_relevant(dst_pat, diagnosis, hints)):
                    score += 0.5
            if src_pat is not None:
                score += _context_score(src_pat, available_context)
            if dst_pat is not None:
                score += _context_score(dst_pat, available_context)
            candidates.append((src, dst, score))

    if hints and hints.last_winner:
        for base_name in _split_for_lookup(hints.last_winner):
            if base_name not in available:
                continue
            for dst in _follow_up_names_for(base_name, hints.last_winner_tags):
                if dst in available:
                    candidates.append((base_name, dst, 2.0))

    if hints:
        for pattern_name in hints.promising_patterns()[:5]:
            if pattern_name not in available:
                continue
            tag_follow_ups = _follow_up_names_for(
                pattern_name, hints.promising_tags_for_pattern(pattern_name),
            )
            for dst in tag_follow_ups:
                if dst in available:
                    candidates.append((pattern_name, dst, 1.5))

    # Boost pairs seen in DB win history, suppress ineffective pairs
    historical_pairs = _query_effective_pairs(hints)
    historical_map = {pair: conf for pair, conf in historical_pairs}
    suppress = hints.suppress_pairs if hints else set()
    for i, (a, b, score) in enumerate(candidates):
        if (a, b) in historical_map:
            candidates[i] = (a, b, score + historical_map[(a, b)])

    # Sort by score descending and deduplicate, skipping suppressed pairs
    candidates.sort(key=lambda x: -x[2])
    seen: set[tuple[str, str]] = set()
    result: list[tuple[str, str]] = []
    for a, b, _ in candidates:
        if (a, b) not in seen and (a, b) not in suppress:
            seen.add((a, b))
            result.append((a, b))
            if len(result) >= max_pairs:
                break

    # Always include the hardcoded defaults if available
    for pair in _DEFAULT_PAIRS:
        if pair not in seen and pair[0] in available and pair[1] in available:
            result.append(pair)

    return result


def _split_for_lookup(pattern_name: str) -> list[str]:
    """Extract base pattern names from a possibly prefixed pattern name."""
    if pattern_name.startswith("compose:") or pattern_name.startswith("chain:"):
        _, parts = pattern_name.split(":", 1)
        return parts.split("+")
    return [pattern_name]


def _follow_up_names_for(pattern_name: str, tags: frozenset[str]) -> set[str]:
    """Collect static and structural-tag-driven follow-up names."""
    companion_names = set(_FOLLOW_UP_MAP.get(pattern_name, []))
    pattern = None
    try:
        from .patterns.base import get_pattern
        pattern = get_pattern(pattern_name)
    except KeyError:
        pattern = None
    if pattern is not None:
        companion_names.update(pattern.follow_ups)
    for tag in tags:
        companion_names.update(_TAG_FOLLOW_UP_MAP.get(tag, []))
    return companion_names


def _query_effective_pairs(
    hints: RoundHints | None = None,
) -> list[tuple[tuple[str, str], float]]:
    """Query pattern_runs DB for composed/chained pattern pair stats.

    Returns pairs with data-driven confidence scores (wins/(wins+fails))
    instead of hardcoded priority. Also identifies suppress pairs (5+ runs,
    0 wins) and stores them in hints.suppress_pairs if available.

    Returns list of ((pattern_a, pattern_b), confidence) sorted by
    confidence descending.
    """
    try:
        conn = sqlite3.connect(str(_CACHE_DB))
        conn.row_factory = sqlite3.Row

        exists = conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='pattern_runs'"
        ).fetchone()
        if not exists:
            conn.close()
            return []

        # Query both wins and failures (not just won=1)
        rows = conn.execute("""
            SELECT pattern, SUM(won) as wins, COUNT(*) as runs
            FROM pattern_runs
            WHERE pattern LIKE 'compose:%' OR pattern LIKE 'chain:%'
            GROUP BY pattern
            ORDER BY wins DESC
            LIMIT 20
        """).fetchall()
        conn.close()

        # Accumulate per-pair stats
        pair_stats: dict[tuple[str, str], dict[str, int]] = {}

        for r in rows:
            name = r["pattern"]
            wins = int(r["wins"] or 0)
            runs = int(r["runs"] or 0)
            fails = runs - wins

            extracted: list[tuple[str, str]] = []
            if name.startswith("compose:"):
                parts = name[len("compose:"):].split("+")
                if len(parts) == 2:
                    extracted.append((parts[0], parts[1]))
            elif name.startswith("chain:"):
                parts = name[len("chain:"):].split("+")
                for i in range(len(parts) - 1):
                    extracted.append((parts[i], parts[i + 1]))

            for pair in extracted:
                stats = pair_stats.setdefault(pair, {"wins": 0, "fails": 0, "total": 0})
                stats["wins"] += wins
                stats["fails"] += fails
                stats["total"] += runs

        # Identify suppress pairs: 5+ total runs, 0 wins
        suppress: set[tuple[str, str]] = set()
        for pair, stats in pair_stats.items():
            if stats["total"] >= 5 and stats["wins"] == 0:
                suppress.add(pair)

        if hints is not None:
            hints.suppress_pairs.update(suppress)

        # Build result with data-driven confidence
        result: list[tuple[tuple[str, str], float]] = []
        for pair, stats in pair_stats.items():
            if stats["wins"] > 0 and pair not in suppress:
                confidence = stats["wins"] / (stats["wins"] + stats["fails"])
                result.append((pair, confidence))

        result.sort(key=lambda x: -x[1])
        return result
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
        fpr_swaps = sum(
            1 for (a, b) in diagnosis.reg_swap_pairs
            if a.startswith("f")
        )
        if gpr_swaps > 0:
            chains.append(ChainSpec(
                stages=["declaration_reorder", "prologue_pressure", "parameter_live_range"],
                reason=f"regalloc: {gpr_swaps} GPR swap pairs",
                priority=0.7,
            ))
            chains.append(ChainSpec(
                stages=["member_ref_bind", "declaration_reorder"],
                reason=f"regalloc: bind members to fix register order",
                priority=0.7,
            ))
        if fpr_swaps > 0:
            chains.append(ChainSpec(
                stages=["declaration_reorder", "float_double_literal"],
                reason=f"regalloc: {fpr_swaps} FPR swap pairs",
                priority=0.7,
            ))

    # Prologue mismatch -> pressure chain
    if diagnosis.has_prologue_mismatch:
        delta = diagnosis.gpr_save_delta
        if delta > 0:
            chains.append(ChainSpec(
                stages=["prologue_pressure", "declaration_reorder"],
                reason=f"prologue: target needs {delta} more GPR saves",
                priority=0.7,
            ))
        elif delta < 0:
            chains.append(ChainSpec(
                stages=["temp_elimination", "reference_elimination"],
                reason=f"prologue: target needs {-delta} fewer GPR saves",
                priority=0.7,
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
            priority=0.7,
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
            priority=0.7,
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


def select_improvers(
    batch_results: list,
    baseline: float,
    max_k: int = 5,
) -> list:
    """Select top-K improving variants for cross-composition.

    Filters to build_success + match_percent > baseline, skips compose:/chain:
    prefixed patterns, deduplicates by source hash, returns top-K by match%.

    Args:
        batch_results: List of ScoreResult from the main scoring phase.
        baseline: Current match percentage.
        max_k: Maximum improvers to return.

    Returns:
        List of ScoreResult objects sorted by match_percent descending.
    """
    from hashlib import md5

    candidates = []
    seen_hashes: set[str] = set()

    for r in batch_results:
        if not r.build_success or r.match_percent <= baseline:
            continue
        # Skip composed/chained variants — we want independent pattern outputs
        pname = r.variant.pattern_name
        if pname.startswith(("compose:", "chain:", "crosscompose:", "merge:")):
            continue
        # Source dedup
        h = md5(r.variant.source).hexdigest()
        if h in seen_hashes:
            continue
        seen_hashes.add(h)
        candidates.append(r)

    candidates.sort(key=lambda r: -r.match_percent)
    return candidates[:max_k]


def _select_companion_patterns(
    improver_pattern: str,
    improver_tags: frozenset[str],
    phase1_results: list,
    patterns: list,
    baseline: float,
) -> list:
    """Select companion patterns to apply on top of an improver.

    Sources:
    1. Other patterns that independently improved (from phase1_results)
    2. _FOLLOW_UP_MAP entries for the improver's pattern

    Args:
        improver_pattern: Pattern name of the improving variant.
        phase1_results: All ScoreResult from the main scoring phase.
        patterns: Available pattern instances.
        baseline: Current match percentage.

    Returns:
        List of Pattern objects to try as companions.
    """
    pattern_map = {p.name: p for p in patterns}
    companion_names: set[str] = set()

    # Source 1: Other patterns that independently improved
    for r in phase1_results:
        if not r.build_success or r.match_percent <= baseline:
            continue
        pname = r.variant.pattern_name
        if pname.startswith(("compose:", "chain:", "crosscompose:", "merge:")):
            continue
        if pname != improver_pattern and pname in pattern_map:
            companion_names.add(pname)

    # Source 2: Follow-up map entries
    for follow_up in _follow_up_names_for(improver_pattern, improver_tags):
        if follow_up in pattern_map:
            companion_names.add(follow_up)

    return [pattern_map[n] for n in companion_names if n in pattern_map]


def cross_compose_variants(
    original_ctx: FunctionContext,
    improvers: list,
    patterns: list,
    phase1_results: list,
    baseline: float,
    max_per_improver: int = 6,
    max_total: int = 30,
) -> Iterator[Variant]:
    """Cross-compose: apply companion patterns on top of improving variants.

    For each improver:
    1. Reparse the improver's source to get a fresh AST context
    2. Select companion patterns (other improvers + follow-up map)
    3. Generate one variant per companion pattern
    4. Yield with crosscompose: prefix

    Args:
        original_ctx: Original function context.
        improvers: List of ScoreResult from select_improvers().
        patterns: All available pattern instances.
        phase1_results: All ScoreResult from main scoring.
        baseline: Current match percentage.
        max_per_improver: Max variants per improver.
        max_total: Max total variants.

    Yields:
        Variant objects with crosscompose: prefix.
    """
    from hashlib import md5

    total = 0
    seen_sources: set[str] = set()

    for improver_result in improvers:
        if total >= max_total:
            return

        # Reparse the improver's source
        try:
            reparsed = reparse_variant(original_ctx, improver_result.variant.source)
        except ValueError:
            continue

        companions = _select_companion_patterns(
            improver_result.variant.pattern_name,
            improver_result.variant.tags,
            phase1_results, patterns, baseline,
        )

        count = 0
        for companion in companions:
            if count >= max_per_improver or total >= max_total:
                break

            # Generate first variant from this companion on the reparsed context
            for variant in companion.generate(reparsed):
                auxiliary_files = merge_auxiliary_file_sets(
                    improver_result.variant.auxiliary_files,
                    variant.auxiliary_files,
                )
                if auxiliary_files is None:
                    continue
                # Dedup
                candidate = Variant(
                    name=f"crosscompose:{improver_result.variant.name}+{variant.name}",
                    pattern_name=f"crosscompose:{improver_result.variant.pattern_name}+{companion.name}",
                    description=f"{improver_result.variant.description} then {variant.description}",
                    source=variant.source,
                    tags=improver_result.variant.tags | variant.tags,
                    auxiliary_files=auxiliary_files,
                )
                h = md5(variant_identity_bytes(original_ctx.file_path, candidate)).hexdigest()
                if h in seen_sources:
                    continue
                seen_sources.add(h)

                yield candidate
                count += 1
                total += 1
                break  # Only first variant per companion
