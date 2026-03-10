"""Beam search — multi-state structured search for decomp matching.

Keeps the top K candidate source states alive at each depth, expanding
each with diagnosis-guided proposals and selecting survivors by a
multi-criteria ranking (score, build reliability, guidance agreement,
diversity).

Unlike greedy hill climbing, beam search can accept neutral or slightly
regressive intermediate moves when they open better later paths.

Usage:
    python -m scripts.permuter.beam_search \
        --symbol "?Poll@LabelNumberTicker@@UAAXXZ"

    python -m scripts.permuter.beam_search \
        --symbol "?Poll@LabelNumberTicker@@UAAXXZ" \
        --beam-width 12 --beam-depth 6 --json
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from .composer import available_context_keys, build_adaptive_chains, get_compose_pairs
from .diagnosis import format_diagnosis_summary, is_all_noise
from .extractor import extract_function
from .file_util import apply_file_updates, atomic_write_bytes, restore_tracked_files
from .generator import generate_variants
from .patterns import get_all_patterns
from .scorer import Scorer
from .types import (
    BeamConfig,
    BeamState,
    ChainSpec,
    Diagnosis,
    FunctionContext,
    HillClimbResult,
    RoundHints,
    RoundResult,
    ScoreResult,
    Variant,
    variant_file_updates,
    variant_identity_bytes,
)


# ---------------------------------------------------------------------------
# State helpers
# ---------------------------------------------------------------------------


def _reparse_context(
    source: bytes,
    source_path: Path,
    function_name: str,
) -> FunctionContext | None:
    """Re-extract function context from modified source bytes.

    Writes the source to a temp buffer, parses it, and returns a fresh
    FunctionContext.  Returns None if extraction fails (e.g., the variant
    introduced a syntax error that confuses tree-sitter).
    """
    # Write temporarily so extract_function reads the new content
    original = source_path.read_bytes()
    try:
        atomic_write_bytes(source_path, source)
        return extract_function(source_path, function_name)
    except (ValueError, Exception):
        return None
    finally:
        atomic_write_bytes(source_path, original)


def _compute_guidance_agreement(
    state: BeamState,
    ghidra_code: str | None,
    m2c_code: str | None,
) -> int:
    """Score how well the state's source structure aligns with guidance.

    Coarse scoring:
        +2: both Ghidra and m2c agree on a structural property
            AND the state's source matches that structure
        +1: one guidance source supports the state's structure
         0: no useful signal
        -1: guidance sources conflict or state diverges from both

    Uses structural extractors to compare source against m2c/Ghidra targets.
    """
    if not ghidra_code and not m2c_code:
        return 0

    signals: list[int] = []

    # Extract structural features from source
    try:
        source_text = state.source.decode("utf-8", errors="replace")
    except Exception:
        return 0

    from .m2c import (
        extract_call_order, extract_guard_count,
        extract_nesting_depth, extract_return_pattern,
    )

    src_guards = extract_guard_count(source_text)
    src_depth = extract_nesting_depth(source_text)
    src_ret = extract_return_pattern(source_text)
    src_calls = extract_call_order(source_text)

    # Compare against m2c target
    m2c_agrees = False
    if m2c_code:
        m2c_guards = extract_guard_count(m2c_code)
        m2c_depth = extract_nesting_depth(m2c_code)
        m2c_ret = extract_return_pattern(m2c_code)
        m2c_calls = extract_call_order(m2c_code)

        # Guard count agreement: both high or both low
        if (m2c_guards >= 2 and src_guards >= 2) or (m2c_guards == 0 and src_guards == 0):
            signals.append(1)
            m2c_agrees = True
        elif (m2c_guards >= 2) != (src_guards >= 2):
            signals.append(-1)

        # Nesting depth agreement: direction match
        if m2c_depth >= 2 and src_depth >= 2:
            signals.append(1)
            m2c_agrees = True
        elif (m2c_depth >= 2) != (src_depth >= 2):
            signals.append(-1)

        # Return pattern agreement
        if m2c_ret == src_ret and m2c_ret not in ("unknown", "single"):
            signals.append(1)
            m2c_agrees = True

        # Call order agreement (shared calls in same order)
        if m2c_calls and src_calls:
            shared = [c for c in src_calls if c in m2c_calls]
            m2c_shared = [c for c in m2c_calls if c in set(src_calls)]
            if len(shared) >= 2 and shared == m2c_shared:
                signals.append(1)
                m2c_agrees = True
            elif len(shared) >= 2 and shared != m2c_shared:
                signals.append(-1)

    # Compare against Ghidra target
    ghidra_agrees = False
    if ghidra_code:
        ghidra_guards = extract_guard_count(ghidra_code)
        ghidra_depth = extract_nesting_depth(ghidra_code)

        if (ghidra_guards >= 2 and src_guards >= 2) or (ghidra_guards == 0 and src_guards == 0):
            signals.append(1)
            ghidra_agrees = True
        elif (ghidra_guards >= 2) != (src_guards >= 2):
            signals.append(-1)

        if ghidra_depth >= 2 and src_depth >= 2:
            signals.append(1)
            ghidra_agrees = True
        elif (ghidra_depth >= 2) != (src_depth >= 2):
            signals.append(-1)

    # Aggregate
    if ghidra_agrees and m2c_agrees:
        return 2  # Both agree with source
    if not signals:
        return 0
    avg = sum(signals) / len(signals)
    if avg > 0:
        return 1  # One source agrees
    if avg < 0:
        return -1  # Sources conflict or source diverges
    return 0


def _select_survivors(
    candidates: list[BeamState],
    width: int,
    diversity_min: int,
) -> list[BeamState]:
    """Select the top survivors from a pool of scored candidates.

    Ensures at least `diversity_min` distinct pattern families are
    represented in the beam (when possible).
    """
    if not candidates:
        return []

    # Sort by ranking key (descending)
    ranked = sorted(candidates, key=lambda s: s.ranking_key, reverse=True)

    if len(ranked) <= width:
        return ranked

    # Phase 1: Fill diversity slots
    survivors: list[BeamState] = []
    seen_families: set[str] = set()
    remaining: list[BeamState] = []

    for state in ranked:
        family = state.applied_patterns[-1] if state.applied_patterns else ""
        if family not in seen_families and len(seen_families) < diversity_min:
            survivors.append(state)
            seen_families.add(family)
        else:
            remaining.append(state)
        if len(survivors) >= width:
            return survivors[:width]

    # Phase 2: Fill remaining slots by raw ranking
    for state in remaining:
        if len(survivors) >= width:
            break
        survivors.append(state)

    return survivors


def _escape_beam(
    beam: list[BeamState],
    best_ever: BeamState | None,
    escape_budget: int,
    patterns: list,
) -> list[tuple[int, BeamState]]:
    """Generate escape states for stagnating beam slots.

    Replaces up to `escape_budget` stagnating survivors with fresh
    states derived from best-ever, but with randomized pattern focus
    to explore different directions.
    """
    if best_ever is None:
        return []

    # Find stagnating slots
    stagnating_indices = [
        i for i, s in enumerate(beam)
        if s.stagnation_count >= 2
    ]
    if not stagnating_indices:
        return []

    replacements: list[tuple[int, BeamState]] = []
    pattern_names = [p.name for p in patterns]

    # Create escape states with different "focus" patterns
    for i, slot_idx in enumerate(stagnating_indices[:escape_budget]):
        # Cycle through patterns to create diverse escape states
        focus_pattern = pattern_names[i % len(pattern_names)] if pattern_names else ""
        escape = BeamState(
            source=best_ever.source,
            score=best_ever.score,
            diagnosis=best_ever.diagnosis,
            tags=best_ever.tags,
            applied_patterns=best_ever.applied_patterns + [f"escape:{focus_pattern}"],
            generation=best_ever.generation,
            stagnation_count=0,  # Reset stagnation
            build_fail_count=best_ever.build_fail_count,
            guidance_agreement=best_ever.guidance_agreement,
            provenance=best_ever.provenance + [f"escape_{i}"],
        )
        replacements.append((slot_idx, escape))

    return replacements


def _compute_fact_agreement(
    facts: object | None,
    result: ScoreResult,
    parent_score: float,
) -> int:
    """Score how well a scored result agrees with target facts.

    Returns a non-negative integer:
        0: no facts or no signal
        +1: result does not violate any high-confidence facts
        +2: result actively satisfies pattern recommendations
        -1: result's pattern is suppressed by facts
    """
    if facts is None:
        return 0

    score = 0
    try:
        boost, suppress = facts.pattern_recommendations()
        pname = result.variant.pattern_name
        # Check base pattern names (strip compose: prefix)
        base_names = []
        for prefix in ("compose:", "chain:", "crosscompose:", "merge:"):
            if pname.startswith(prefix):
                base_names = pname.split(":", 1)[1].split("+")
                break
        if not base_names:
            base_names = [pname]

        # Suppressed pattern = negative
        if any(bn in suppress for bn in base_names):
            return -1

        # Boosted pattern = positive
        if any(bn in boost for bn in base_names):
            score += 2

        # Score improved = no fact violations (conservative)
        if result.match_percent > parent_score:
            score += 1

        # No-touch zone violation check (would need region info)
        no_touch = facts.by_kind("no_touch_zone")
        if no_touch and result.match_percent < parent_score:
            score -= 1

    except Exception:
        pass

    return score


def _deduplicate_states(states: list[BeamState], file_path: Path) -> list[BeamState]:
    """Remove states with identical source bytes."""
    seen: set[bytes] = set()
    unique: list[BeamState] = []
    for state in states:
        key = state.source
        # Use a hash for memory efficiency
        import hashlib
        h = hashlib.md5(key).digest()
        if h not in seen:
            seen.add(h)
            unique.append(state)
    return unique


# ---------------------------------------------------------------------------
# Expansion
# ---------------------------------------------------------------------------


def _expand_state(
    state: BeamState,
    source_path: Path,
    function_name: str,
    patterns: list,
    config: BeamConfig,
    ghidra_code: str | None = None,
    ghidra_ast: object | None = None,
    m2c_code: str | None = None,
    rb3_source: str | None = None,
    symbol: str | None = None,
    target_var_order: list | None = None,
    target_gpr_saves: int | None = None,
    constrained: bool = True,
    cross_unit: bool = False,
    target_facts: object | None = None,
) -> list[Variant]:
    """Generate proposal variants from a single beam state.

    Reparses the state's source to get a fresh FunctionContext, then
    runs the standard pattern generators with diagnosis-guided budgets.
    Also runs constrained synthesis as a parallel proposal source.
    """
    ctx = _reparse_context(state.source, source_path, function_name)
    if ctx is None:
        return []

    # Wire stable target-side guidance (reused across all states)
    ctx.symbol = symbol
    ctx.ghidra_code = ghidra_code
    ctx.ghidra_ast = ghidra_ast
    ctx.m2c_code = m2c_code
    ctx.rb3_source = rb3_source
    ctx.target_var_order = target_var_order
    ctx.target_gpr_saves = target_gpr_saves

    # Wire state-side diagnosis and target facts
    ctx.diagnosis = state.diagnosis
    ctx.target_facts = target_facts

    # Build adaptive hints from lineage history
    round_hints = RoundHints()
    if state.lineage_build_failures:
        round_hints.build_failed_patterns = {
            p for p, count in state.lineage_build_failures.items()
            if count >= 2
        }
    if state.applied_patterns:
        round_hints.last_winner = state.applied_patterns[-1]
    round_hints.last_winner_tags = state.tags

    # Atlas-derived pattern boost/suppress from diagnosis
    if ctx.diagnosis:
        try:
            from .compiler_atlas import lookup_for_diagnosis, boost_patterns
            atlas_entries = lookup_for_diagnosis(
                diff_ops=getattr(ctx.diagnosis, 'diff_ops', None),
                reg_swap_pairs=getattr(ctx.diagnosis, 'reg_swap_pairs', None),
                has_prologue_mismatch=getattr(ctx.diagnosis, 'has_prologue_mismatch', False),
            )
            if atlas_entries:
                boost, suppress = boost_patterns(atlas_entries)
                round_hints.atlas_boost_patterns = boost
                round_hints.atlas_suppress_patterns = suppress
        except Exception:
            pass  # Atlas is optional

    # Target-facts-driven boost/suppress (augments atlas)
    if target_facts is not None:
        try:
            boost, suppress = target_facts.pattern_recommendations()
            round_hints.atlas_boost_patterns |= boost
            round_hints.atlas_suppress_patterns |= suppress
        except Exception:
            pass

    # Build compose pairs and chains
    compose_pairs = get_compose_pairs(
        diagnosis=ctx.diagnosis,
        patterns=patterns,
        hints=round_hints,
        available_context=available_context_keys(ctx),
    )
    chains: list[ChainSpec] | None = None
    if ctx.diagnosis:
        chains = build_adaptive_chains(
            diagnosis=ctx.diagnosis,
            patterns=patterns,
            hints=round_hints,
            available_context=available_context_keys(ctx),
            max_depth=3,
        )

    failed = round_hints.build_failed_patterns or None

    variants = list(generate_variants(
        ctx, patterns, config.expand,
        compose_pairs=compose_pairs,
        chains=chains,
        round_hints=round_hints,
        failed_patterns=failed,
    ))

    # Constrained synthesis — run on every beam state, not just seed
    if constrained and ctx.ghidra_ast is not None:
        synth_failed = "constraint_solver" in (
            round_hints.build_failed_patterns or set()
        )
        if not synth_failed:
            try:
                from .constraint_solver import synthesize
                synthesis = synthesize(ctx)
                if synthesis.variants and not synthesis.skip_reason:
                    variants.extend(synthesis.variants)
            except Exception:
                pass  # Non-critical; fall through to pattern variants

    # Cross-unit/header-backed proposals (opt-in, expensive)
    if cross_unit and state.generation <= 1:
        # Only try header proposals in early depths (expensive)
        try:
            from .header_pattern_bridge import (
                discover_header_pattern_variants,
                supported_header_patterns,
            )
            for hp_name in supported_header_patterns():
                header_variants = discover_header_pattern_variants(
                    source_path, function_name, hp_name, max_variants=2,
                )
                for hv in header_variants:
                    variants.append(hv.variant)
        except Exception:
            pass  # Cross-unit infrastructure optional

    return variants


# ---------------------------------------------------------------------------
# Main search loop
# ---------------------------------------------------------------------------


def beam_search(
    symbol: str,
    source_path: Path,
    function_name: str,
    patterns: list,
    config: BeamConfig | None = None,
    apply: bool = True,
    unit: str | None = None,
    ghidra: bool = True,
    m2c: bool = False,
    constrained: bool = True,
    cross_unit: bool = False,
    shape_facts: bool = True,
    validate: bool = True,
) -> HillClimbResult:
    """Run beam search for a single function.

    Maintains a beam of K best source states, expanding each with
    diagnosis-guided proposals at each depth.  Stops on perfect match,
    stagnation across the beam, or depth exhaustion.

    Args:
        symbol: Mangled symbol for objdiff.
        source_path: Path to .cpp source file.
        function_name: Qualified C++ function name.
        patterns: List of pattern instances.
        config: Beam search configuration.
        apply: Whether to apply the best result to source.
        unit: Unit name for objdiff.
        ghidra: Enable Ghidra-guided context.
        m2c: Enable m2c-guided context.

    Returns:
        HillClimbResult with session history.
    """
    if config is None:
        config = BeamConfig()
    # --constrained implies --ghidra (synthesis needs Ghidra AST)
    if constrained:
        ghidra = True
    workers = config.workers if config.workers > 0 else 6

    start_time = time.time()
    rounds: list[RoundResult] = []
    stopped_reason = "depth_exhausted"
    initial_percent = 0.0
    best_ever_score = 0.0
    best_ever_state: BeamState | None = None
    result_codegen_shapes: list[str] = []
    result_fact_boosts: list[str] = []
    result_fact_suppresses: list[str] = []
    all_validation_results: list = []  # list[ValidationResult] across all depths

    original_source = source_path.read_bytes()
    applied_file_originals: dict[Path, bytes | None] = {
        source_path.resolve(): original_source,
    }

    # -----------------------------------------------------------
    # Seed: get baseline, load guidance, create initial state
    # -----------------------------------------------------------
    ghidra_run_stats = None  # populated below after Ghidra loads
    with Scorer(source_path, symbol, unit=unit) as scorer:
        baseline = scorer.get_baseline(guided=True, ghidra=ghidra, m2c=m2c)
        initial_percent = baseline
        best_ever_score = baseline

        print(f"Beam seed: baseline {baseline:.2f}%", file=sys.stderr)

        if baseline >= 100.0:
            stopped_reason = "perfect"
            return _build_result(
                symbol, function_name, source_path,
                initial_percent, 100.0, rounds, stopped_reason,
                time.time() - start_time,
                ghidra_stats=ghidra_run_stats,
                shape_facts_enabled=shape_facts,
                codegen_shapes=result_codegen_shapes,
                fact_boost_patterns=result_fact_boosts,
                fact_suppress_patterns=result_fact_suppresses,
            )

        # Check for all-noise early exit
        if scorer.diagnosis and is_all_noise(scorer.diagnosis):
            print("All mismatches are noise — stopping.", file=sys.stderr)
            stopped_reason = "noise_only"
            return _build_result(
                symbol, function_name, source_path,
                initial_percent, baseline, rounds, stopped_reason,
                time.time() - start_time,
                ghidra_stats=ghidra_run_stats,
                shape_facts_enabled=shape_facts,
                codegen_shapes=result_codegen_shapes,
                fact_boost_patterns=result_fact_boosts,
                fact_suppress_patterns=result_fact_suppresses,
            )

        # Capture stable target-side guidance
        ghidra_code = scorer.ghidra_code
        ghidra_ast = scorer.ghidra_ast
        m2c_code_cached = scorer.m2c_code
        target_var_order = None
        target_gpr_saves = None
        if ghidra_ast:
            from .ghidra_ast import extract_variable_first_use_order, extract_savegpr_count
            target_var_order = extract_variable_first_use_order(ghidra_ast)
            target_gpr_saves = extract_savegpr_count(ghidra_code)

        # Build GhidraRunStats for batch summary reporting
        from .ghidra_stats import GhidraRunStats
        ghidra_run_stats = GhidraRunStats(
            ghidra_available=ghidra_code is not None,
            ghidra_code_bytes=len(ghidra_code) if ghidra_code else 0,
            ghidra_gpr_saves=target_gpr_saves,
        )

        # Extract target facts from all evidence sources
        seed_facts = None
        try:
            from .target_facts import extract_facts
            from .compiler_atlas import lookup_for_diagnosis
            atlas_entries = []
            if scorer.diagnosis:
                atlas_entries = lookup_for_diagnosis(
                    diff_ops=getattr(scorer.diagnosis, 'diff_ops', None),
                    reg_swap_pairs=getattr(scorer.diagnosis, 'reg_swap_pairs', None),
                    has_prologue_mismatch=getattr(scorer.diagnosis, 'has_prologue_mismatch', False),
                )
            # Get attribution regions if available
            attrib_regions = None
            try:
                attrib_regions = scorer.get_attribution()
            except Exception:
                pass
            baseline_shape_facts = None
            try:
                if shape_facts:
                    baseline_shape_facts = scorer.get_shape_facts()
            except Exception:
                pass
            seed_facts = extract_facts(
                diagnosis=scorer.diagnosis,
                regions=attrib_regions,
                atlas_entries=atlas_entries or None,
                shape_facts=baseline_shape_facts,
                ghidra_ast=ghidra_ast,
                rb3_source=None,  # RB3 loaded below
            )
            if seed_facts is not None:
                result_codegen_shapes = [
                    fact.payload.get("shape_category")
                    for fact in seed_facts.by_kind("codegen_shape")
                    if fact.payload.get("shape_category")
                ]
                boost, suppress = seed_facts.pattern_recommendations()
                result_fact_boosts = sorted(boost)
                result_fact_suppresses = sorted(suppress)
            if seed_facts is not None:
                for line in seed_facts.summary_lines():
                    print(line, file=sys.stderr)
        except Exception:
            pass  # Facts are advisory, not critical

        # Seed state
        seed = BeamState(
            source=original_source,
            score=baseline,
            diagnosis=scorer.diagnosis,
            generation=0,
            target_facts=seed_facts,
        )
        beam = [seed]

    # Cache RB3 source once
    rb3_source = _load_rb3_source(symbol, source_path)

    # -----------------------------------------------------------
    # Search loop: expand → score → select at each depth
    # -----------------------------------------------------------
    for depth in range(1, config.depth + 1):
        print(
            f"\n=== Beam depth {depth}/{config.depth} "
            f"(beam size: {len(beam)}) ===",
            file=sys.stderr,
        )

        all_candidates: list[tuple[BeamState, Variant]] = []

        # Expand each beam state
        for si, state in enumerate(beam):
            print(
                f"  Expanding state {si + 1}/{len(beam)} "
                f"(score={state.score:.2f}%, gen={state.generation}, "
                f"patterns={len(state.applied_patterns)})",
                file=sys.stderr,
            )
            variants = _expand_state(
                state, source_path, function_name, patterns, config,
                ghidra_code=ghidra_code,
                ghidra_ast=ghidra_ast,
                m2c_code=m2c_code_cached,
                rb3_source=rb3_source,
                symbol=symbol,
                target_var_order=target_var_order,
                target_gpr_saves=target_gpr_saves,
                constrained=constrained,
                cross_unit=cross_unit,
                target_facts=seed_facts,
            )
            print(
                f"    Generated {len(variants)} proposals",
                file=sys.stderr,
            )
            for v in variants:
                all_candidates.append((state, v))

        if not all_candidates:
            print("No proposals generated — stopping.", file=sys.stderr)
            stopped_reason = "no_variants"
            break

        print(
            f"  Scoring {len(all_candidates)} candidates "
            f"({workers} workers)...",
            file=sys.stderr,
        )

        # Score all variants
        variant_list = [v for _, v in all_candidates]
        parent_map = {id(v): parent for parent, v in all_candidates}

        with Scorer(source_path, symbol, unit=unit) as scorer:
            # Re-establish baseline (source was restored by previous scorer)
            scorer.get_baseline(guided=True, ghidra=False, m2c=False)
            results = scorer.score_batch(variant_list, workers=workers)

        # Build child states from scored results
        child_states: list[BeamState] = []
        depth_best_score = 0.0
        depth_best_name = None
        depth_best_pattern = None
        build_fails = 0
        improved_count = 0

        for result in results:
            parent = parent_map.get(id(result.variant))
            if parent is None:
                continue

            if not result.build_success:
                build_fails += 1
                # Track lineage build failures
                new_failures = dict(parent.lineage_build_failures)
                pname = result.variant.pattern_name
                new_failures[pname] = new_failures.get(pname, 0) + 1
                continue

            if result.match_percent > baseline:
                improved_count += 1

            if result.match_percent > depth_best_score:
                depth_best_score = result.match_percent
                depth_best_name = result.variant.name
                depth_best_pattern = result.variant.pattern_name

            # Run validation ladder
            vtier = 2  # BUILD_OK (already passed build)
            try:
                from .validator import validate_variant, ValidationTier, ValidationResult as VR
                vresult = validate_variant(
                    result.variant,
                    score_result=result,
                    baseline_score=parent.score,
                    parent_regions=parent.region_scores,
                    target_facts=seed_facts,
                    original_source=original_source,
                )
                vtier = int(vresult.tier)
                if validate:
                    all_validation_results.append(vresult)
            except Exception:
                pass  # Validation is advisory

            # Create child state
            child = BeamState(
                source=result.variant.source,
                score=result.match_percent,
                tags=parent.tags | result.variant.tags,
                applied_patterns=parent.applied_patterns + [result.variant.pattern_name],
                generation=depth,
                stagnation_count=(
                    parent.stagnation_count + 1
                    if result.match_percent <= parent.score
                    else 0
                ),
                build_fail_count=parent.build_fail_count,
                guidance_agreement=_compute_guidance_agreement(
                    parent, ghidra_code, m2c_code_cached,
                ),
                provenance=parent.provenance + [result.variant.name],
                auxiliary_files=result.variant.auxiliary_files,
                lineage_build_failures=dict(parent.lineage_build_failures),
                target_facts=seed_facts,
                fact_agreement=_compute_fact_agreement(
                    seed_facts, result, parent.score,
                ),
                validation_tier=vtier,
            )
            child_states.append(child)

        # Record round
        round_delta = depth_best_score - best_ever_score if depth_best_score > best_ever_score else 0.0
        rounds.append(RoundResult(
            round_num=depth,
            baseline=best_ever_score,
            best_name=depth_best_name,
            best_pattern=depth_best_pattern,
            best_score=depth_best_score,
            delta=round_delta,
            num_variants=len(all_candidates),
            improved=depth_best_score > best_ever_score,
        ))

        print(
            f"  Depth {depth}: {len(child_states)} viable, "
            f"{build_fails} build failures, "
            f"{improved_count} improved over baseline",
            file=sys.stderr,
        )

        # Track best-ever
        for child in child_states:
            if child.score > best_ever_score:
                best_ever_score = child.score
                best_ever_state = child

        # Perfect match?
        if best_ever_score >= 100.0:
            print(f"  Perfect match at depth {depth}!", file=sys.stderr)
            stopped_reason = "perfect"
            break

        # Dedup and select survivors
        child_states = _deduplicate_states(child_states, source_path)
        beam = _select_survivors(child_states, config.width, config.diversity)

        if not beam:
            print("  Beam empty — stopping.", file=sys.stderr)
            stopped_reason = "beam_empty"
            break

        # Also always keep best-ever in the beam if it was displaced
        if best_ever_state and best_ever_state not in beam:
            # Replace the worst survivor
            beam[-1] = best_ever_state

        # Re-diagnose survivors for next depth
        for state in beam:
            _rediagnose_state(state, source_path, symbol, unit)

        # Stagnation handling
        stagnating = sum(1 for s in beam if s.stagnation_count >= 2)
        if stagnating == len(beam):
            # ALL stagnating — try escape before giving up
            if config.escape > 0 and depth < config.depth:
                escaped = _escape_beam(
                    beam, best_ever_state, config.escape, patterns,
                )
                if escaped:
                    print(
                        f"  Escape: replaced {len(escaped)} stagnating slots",
                        file=sys.stderr,
                    )
                    for idx, new_state in escaped:
                        if idx < len(beam):
                            beam[idx] = new_state
                else:
                    print("  Beam stagnated — stopping.", file=sys.stderr)
                    stopped_reason = "stagnation"
                    break
            else:
                print("  Beam stagnated — stopping.", file=sys.stderr)
                stopped_reason = "stagnation"
                break
        elif stagnating > len(beam) // 2:
            # Majority stagnating — partial escape
            print(
                f"  {stagnating}/{len(beam)} survivors stagnating",
                file=sys.stderr,
            )

        # Print beam summary
        print(f"  Survivors:", file=sys.stderr)
        _vtier_short = {
            0: "INV", 1: "PAR", 2: "BLD", 3: "SCR",
            4: "REG", 5: "FCT", 6: "SEM",
        }
        for si, s in enumerate(beam):
            if validate and s.validation_tier > 0:
                vtier_str = f" v={_vtier_short.get(s.validation_tier, str(s.validation_tier))}"
            elif s.validation_tier > 0:
                vtier_str = f" v={s.validation_tier}"
            else:
                vtier_str = ""
            print(
                f"    [{si + 1}] {s.score:.2f}% gen={s.generation} "
                f"stag={s.stagnation_count}{vtier_str} "
                f"patterns={'+'.join(s.applied_patterns[-2:]) if s.applied_patterns else 'seed'}",
                file=sys.stderr,
            )

    # -----------------------------------------------------------
    # Apply best result
    # -----------------------------------------------------------
    final_percent = best_ever_score
    if best_ever_state and best_ever_score > initial_percent and apply:
        atomic_write_bytes(source_path, best_ever_state.source)
        print(
            f"\nApplied best: {initial_percent:.2f}% -> {best_ever_score:.2f}% "
            f"(chain: {' -> '.join(best_ever_state.provenance[-3:])})",
            file=sys.stderr,
        )
    elif not apply and best_ever_state and best_ever_score > initial_percent:
        print(
            f"\nBest (not applied): {initial_percent:.2f}% -> {best_ever_score:.2f}%",
            file=sys.stderr,
        )
    else:
        # Restore original
        atomic_write_bytes(source_path, original_source)
        final_percent = initial_percent

    best_vtier = best_ever_state.validation_tier if best_ever_state else 0

    # Build validation tier distribution
    validation_dist: dict[int, int] = {}
    if validate and all_validation_results:
        for vr in all_validation_results:
            t = int(vr.tier)
            validation_dist[t] = validation_dist.get(t, 0) + 1

    return _build_result(
        symbol, function_name, source_path,
        initial_percent, final_percent, rounds, stopped_reason,
        time.time() - start_time,
        ghidra_stats=ghidra_run_stats,
        validation_tier=best_vtier,
        validation_distribution=validation_dist,
        shape_facts_enabled=shape_facts,
        codegen_shapes=result_codegen_shapes,
        fact_boost_patterns=result_fact_boosts,
        fact_suppress_patterns=result_fact_suppresses,
    )


def _rediagnose_state(
    state: BeamState,
    source_path: Path,
    symbol: str,
    unit: str | None,
) -> None:
    """Re-score and re-diagnose a beam state for the next expansion round.

    Updates state.diagnosis in place.  Uses a temporary Scorer to get
    a fresh diagnosis from the state's source.
    """
    original = source_path.read_bytes()
    try:
        atomic_write_bytes(source_path, state.source)
        with Scorer(source_path, symbol, unit=unit) as scorer:
            pct = scorer.get_baseline(guided=True, ghidra=False, m2c=False)
            state.score = pct
            state.diagnosis = scorer.diagnosis
    except Exception:
        pass
    finally:
        atomic_write_bytes(source_path, original)


def _load_rb3_source(symbol: str, source_path: Path) -> str | None:
    """Load RB3 reference source for a symbol."""
    parts = symbol.split('@') if symbol else []
    rb3_method = parts[0].lstrip('?') if parts else ''
    rb3_class = parts[1] if len(parts) >= 2 else ''
    if rb3_class and rb3_method:
        try:
            from scripts.orchestrator import rb3_pairing
            rb3_file = rb3_pairing.find_rb3_file(str(source_path))
            if rb3_file:
                rb3_text = rb3_file.read_text(errors='replace')
                return rb3_pairing.extract_rb3_method(
                    rb3_text, rb3_class, rb3_method,
                )
        except Exception:
            pass
    return None


def _build_result(
    symbol: str,
    function_name: str,
    source_path: Path,
    initial_percent: float,
    final_percent: float,
    rounds: list[RoundResult],
    stopped_reason: str,
    elapsed: float,
    ghidra_stats: object | None = None,
    validation_tier: int = 0,
    validation_distribution: dict[int, int] | None = None,
    shape_facts_enabled: bool = True,
    codegen_shapes: list[str] | None = None,
    fact_boost_patterns: list[str] | None = None,
    fact_suppress_patterns: list[str] | None = None,
) -> HillClimbResult:
    """Build a HillClimbResult from beam search data."""
    winning_pattern = None
    for r in reversed(rounds):
        if r.improved and r.best_pattern:
            winning_pattern = r.best_pattern
            break

    return HillClimbResult(
        symbol=symbol,
        function_name=function_name,
        source_path=str(source_path),
        initial_percent=initial_percent,
        final_percent=final_percent,
        total_delta=final_percent - initial_percent,
        rounds=rounds,
        stopped_reason=stopped_reason,
        elapsed_seconds=elapsed,
        winning_pattern=winning_pattern,
        ghidra_stats=ghidra_stats,
        validation_tier=validation_tier,
        validation_distribution=dict(validation_distribution or {}),
        shape_facts_enabled=shape_facts_enabled,
        codegen_shapes=list(codegen_shapes or []),
        fact_boost_patterns=list(fact_boost_patterns or []),
        fact_suppress_patterns=list(fact_suppress_patterns or []),
    )


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.beam_search",
        description="Beam search for decomp matching.",
    )
    parser.add_argument("--symbol", required=True, help="Mangled symbol name")
    parser.add_argument("--source", type=Path, help="Path to .cpp source file")
    parser.add_argument("--function", help="Qualified C++ function name")
    parser.add_argument("--unit", help="Unit name")
    parser.add_argument("--beam-width", type=int, default=8, help="Beam width (default: 8)")
    parser.add_argument("--beam-depth", type=int, default=4, help="Search depth (default: 4)")
    parser.add_argument("--beam-expand", type=int, default=24, help="Proposals per state (default: 24)")
    parser.add_argument("--beam-escape", type=int, default=4, help="Escape budget (default: 4)")
    parser.add_argument("--beam-diversity", type=int, default=3, help="Min diversity (default: 3)")
    parser.add_argument("--workers", type=int, default=0, help="Parallel workers")
    parser.add_argument("--no-apply", action="store_true", help="Dry run")
    parser.add_argument("--ghidra", action="store_true", default=True)
    parser.add_argument("--no-ghidra", action="store_false", dest="ghidra")
    parser.add_argument("--m2c", action="store_true", default=False)
    parser.add_argument("--constrained", action="store_true", default=True)
    parser.add_argument("--no-constrained", action="store_false", dest="constrained")
    parser.add_argument("--cross-unit", action="store_true", default=False,
                        help="Enable header-backed cross-unit proposals (expensive)")
    parser.add_argument("--json", action="store_true", dest="json_output")
    parser.add_argument("--patterns", default="all", help="Pattern list")
    parser.add_argument("--validate", action="store_true", default=True,
                        help="Show per-variant validation tiers and tier distribution summary (default: True)")
    parser.add_argument("--no-validate", action="store_false", dest="validate",
                        help="Disable validation tier display")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    # Resolve source and function from symbol
    source_path = args.source
    function_name = args.function

    if source_path is None or function_name is None:
        try:
            from scripts.orchestrator.db_helpers import resolve_symbol_info
            info = resolve_symbol_info(args.symbol)
            if source_path is None:
                source_path = Path(info["source_path"])
            if function_name is None:
                from .types import extract_qualified_name
                function_name = extract_qualified_name(info["demangled"])
                if function_name is None:
                    function_name = info.get("qualified_name", "")
        except Exception as e:
            print(f"Cannot resolve symbol: {e}", file=sys.stderr)
            sys.exit(1)

    if not source_path or not source_path.exists():
        print(f"Source file not found: {source_path}", file=sys.stderr)
        sys.exit(1)

    if args.patterns == "all":
        patterns = get_all_patterns()
    else:
        from .patterns import get_pattern
        patterns = [get_pattern(p.strip()) for p in args.patterns.split(",")]

    config = BeamConfig(
        width=args.beam_width,
        depth=args.beam_depth,
        expand=args.beam_expand,
        escape=args.beam_escape,
        diversity=args.beam_diversity,
        workers=args.workers,
    )

    result = beam_search(
        symbol=args.symbol,
        source_path=source_path,
        function_name=function_name,
        patterns=patterns,
        config=config,
        apply=not args.no_apply,
        unit=args.unit,
        ghidra=args.ghidra,
        m2c=args.m2c,
        constrained=args.constrained,
        cross_unit=args.cross_unit,
        validate=args.validate,
    )

    # Output
    if args.json_output:
        import json
        output = {
            "symbol": result.symbol,
            "function": result.function_name,
            "source": result.source_path,
            "initial_percent": result.initial_percent,
            "final_percent": result.final_percent,
            "delta": result.total_delta,
            "stopped_reason": result.stopped_reason,
            "elapsed_seconds": round(result.elapsed_seconds, 2),
            "shape_facts_enabled": result.shape_facts_enabled,
            "codegen_shapes": result.codegen_shapes,
            "fact_boost_patterns": result.fact_boost_patterns,
            "fact_suppress_patterns": result.fact_suppress_patterns,
            "validation_tier": result.validation_tier,
            "validation_distribution": {str(k): v for k, v in result.validation_distribution.items()},
            "rounds": [
                {
                    "depth": r.round_num,
                    "baseline": r.baseline,
                    "best_name": r.best_name,
                    "best_pattern": r.best_pattern,
                    "best_score": r.best_score,
                    "delta": r.delta,
                    "num_variants": r.num_variants,
                    "improved": r.improved,
                }
                for r in result.rounds
            ],
        }
        print(json.dumps(output, indent=2))
    else:
        print(f"\nBeam search complete:")
        print(f"  {result.initial_percent:.2f}% -> {result.final_percent:.2f}% "
              f"(+{result.total_delta:.2f}%)")
        print(f"  Stopped: {result.stopped_reason}")
        print(f"  Elapsed: {result.elapsed_seconds:.1f}s")
        if result.codegen_shapes:
            print(f"  Shapes: {', '.join(result.codegen_shapes)}")
        if result.fact_boost_patterns:
            print(f"  Boosts: {', '.join(result.fact_boost_patterns)}")
        if result.fact_suppress_patterns:
            print(f"  Suppress: {', '.join(result.fact_suppress_patterns)}")
        if result.winning_pattern:
            print(f"  Winning pattern: {result.winning_pattern}")
        # Validation tier distribution
        if args.validate and result.validation_distribution:
            _vtn = {
                0: "INVALID", 1: "PARSE_OK", 2: "BUILD_OK", 3: "SCORE_IMPROVED",
                4: "REGION_IMPROVED", 5: "FACT_AGREED", 6: "SEMANTIC_OK",
            }
            parts = []
            for tier in range(6, -1, -1):
                count = result.validation_distribution.get(tier, 0)
                if count > 0:
                    parts.append(f"{_vtn.get(tier, f'T{tier}')}:{count}")
            if parts:
                total = sum(result.validation_distribution.values())
                print(f"  Tier dist: {' '.join(parts)} ({total} variants)")
        for r in result.rounds:
            marker = " IMPROVED" if r.improved else ""
            print(f"  Depth {r.round_num}: {r.best_score:.2f}% "
                  f"({r.num_variants} variants){marker}")


if __name__ == "__main__":
    main()
