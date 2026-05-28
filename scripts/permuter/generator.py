"""Variant generator — applies patterns to a function context."""

from __future__ import annotations

import dataclasses
import os
import sys
from typing import Iterator

from .types import (
    ChainSpec,
    Diagnosis,
    FunctionContext,
    RoundHints,
    Variant,
    variant_identity_bytes,
)
from .patterns.base import Pattern, get_pattern
from .composer import compose_variants, chain_variants

_MIN_BUDGET = 3  # minimum variants per relevant pattern
_BLIND_BUDGET_FRACTION = 0.30  # 30% of normal budget for blind variants
_LOW_CONFIDENCE_THRESHOLD = 0.5  # below this, region is "low confidence"
_SAFETY_TIER_WEIGHTS = {
    "conservative": 1.05,
    "normal": 1.0,
    "moderate": 0.95,
    "aggressive": 0.85,
}

# Bayesian prior parameters for learned priority adjustment
_BAYESIAN_ALPHA = 1    # Prior pseudo-wins (optimistic prior)
_BAYESIAN_BETA = 10    # Prior pseudo-total (strength of prior)
_BASELINE_P = _BAYESIAN_ALPHA / (_BAYESIAN_ALPHA + _BAYESIAN_BETA)  # ~0.091
_LEARNED_MULTIPLIER_MIN = 0.3
_LEARNED_MULTIPLIER_MAX = 2.0


def hard_filters_enabled() -> bool:
    """Whether B2 hard pattern filters are active (env-gated, ON by default).

    Default ON (2026-05-27): DC3 + RB3 stress sweeps showed it is lossless — it
    only drops patterns a fact suppresses at >=0.85 confidence (with a
    boost-conflict guard), never fired a false drop, zero win regression, zero
    crashes on either codebase.

    Disable by setting PERMUTER_HARD_FILTERS to a falsy value (``0``/``false``/
    ``no``/``off``) or to empty (``PERMUTER_HARD_FILTERS=``). Only an *unset*
    variable keeps the default-on behaviour.
    """
    val = os.environ.get("PERMUTER_HARD_FILTERS", "on").strip().lower()
    return val not in ("", "0", "false", "no", "off")


def syntax_probe_enabled() -> bool:
    """Whether the pre-queue syntax probe is active.

    Default ON: it is baseline-relative (a variant is only dropped when it
    introduces MORE tree-sitter parse errors than the unmodified baseline
    already has), so it cannot drop a variant the compiler would have accepted
    purely because of tree-sitter's known C++/macro fragility. Set
    PERMUTER_SYNTAX_PROBE=0 to disable as an escape hatch.
    """
    return os.environ.get("PERMUTER_SYNTAX_PROBE", "1").strip().lower() in (
        "1", "true", "yes", "on",
    )


def _count_parse_errors(source: bytes) -> int:
    """Count ERROR + MISSING tree-sitter nodes in *source* (uncapped).

    Uses the same tree-sitter C++ grammar/parser the rest of the permuter uses
    (via extractor's cached parse) so the baseline and variant are measured by
    the identical parser. Parsing is ~3 orders of magnitude cheaper than a
    compile, so running it per generated variant is cheap relative to the
    compile it may save.
    """
    from .extractor import _cached_parse

    tree = _cached_parse(source)
    root = tree.root_node
    if not root.has_error:
        return 0
    count = 0
    # Iterative DFS — avoids recursion limits on large files and is allocation
    # light. We count both explicit ERROR nodes and MISSING (inserted) nodes,
    # which together are tree-sitter's two error signals.
    stack = [root]
    while stack:
        node = stack.pop()
        if node.type == "ERROR" or node.is_missing:
            count += 1
        # Only descend into subtrees that actually contain an error, so a
        # fully-parsed function elsewhere in the file costs nothing.
        for child in node.children:
            if child.has_error or child.is_missing or child.type == "ERROR":
                stack.append(child)
    return count


def _syntax_probe_filter(
    variants: "Iterator[Variant]",
    baseline_source: bytes,
) -> "Iterator[Variant]":
    """Drop variants that introduce NEW syntax errors vs the baseline.

    Baseline-relative by design: real Milo source already trips tree-sitter
    (function-like macros, BEGIN_*/END_* blocks) so the unmodified file reports
    parse errors. We therefore reject a variant only when its parse-error count
    strictly exceeds the baseline's — i.e. the pattern's edit introduced a brand
    new error. This guarantees no false drops from pre-existing macro fragility
    (correctness — no lost wins — outranks the speedup), while still catching the
    generically-doomed variants (e.g. an extraction that breaks the surrounding
    statement) before they reach the expensive compile.
    """
    baseline_errors = _count_parse_errors(baseline_source)
    dropped = 0
    for variant in variants:
        # Fast path: unchanged source can't add errors (and patterns sometimes
        # emit the original bytes). Avoids a redundant parse on the no-op case.
        if variant.source == baseline_source:
            yield variant
            continue
        if _count_parse_errors(variant.source) > baseline_errors:
            dropped += 1
            continue
        yield variant
    if dropped > 0:
        print(
            f"Syntax probe: dropped {dropped} variant(s) that introduced "
            f"new parse errors (pre-compile)",
            file=sys.stderr,
        )


def _winner_domains(round_hints: RoundHints | None) -> set[str]:
    """Collect structural domains from the most recent winning pattern(s)."""
    if not round_hints or not round_hints.last_winner:
        return set()

    from .patterns.base import get_pattern

    domains: set[str] = set()
    for name in _split_pattern_names(round_hints.last_winner):
        try:
            domains.add(get_pattern(name).structural_domain)
        except KeyError:
            continue
    return domains


def _split_pattern_names(name: str) -> list[str]:
    """Split composed pattern names into base pattern names."""
    for prefix in ("compose:", "chain:", "crosscompose:", "merge:", "evo_cross:", "evo_mut:"):
        if name.startswith(prefix):
            _, parts = name.split(":", 1)
            return parts.split("+")
    return [name]


def _pattern_priorities(
    patterns: list[Pattern],
    diagnosis: Diagnosis | None,
    round_hints: RoundHints | None = None,
    ctx: FunctionContext | None = None,
) -> dict[str, float]:
    """Compute effective per-pattern priorities for the current round.

    If ``ctx`` is provided, patterns that override ``context_priority`` can
    consult AST shape for a confidence bump (e.g. positive_branch_invert,
    demorgan_guard upgrade to >=0.8 when the function body matches).
    """
    priorities: dict[str, float] = {}
    winner_domains = _winner_domains(round_hints)
    hard_filter = hard_filters_enabled()
    hard_dropped: list[str] = []

    for pattern in patterns:
        # B2 hard filter: a strong "this pattern is wrong here" signal drops
        # the pattern from generation entirely (priority 0 -> 0 budget in
        # allocate_budgets), instead of merely down-weighting it. Gated by the
        # env flag and by RoundHints.hard_drop (which itself defers to any
        # boost-force conflict), so it only fires on the most confident signals.
        if hard_filter and round_hints and round_hints.hard_drop(pattern.name):
            priorities[pattern.name] = 0.0
            hard_dropped.append(pattern.name)
            continue

        if diagnosis and ctx is not None:
            # Patterns that subclass our Pattern base inherit context_priority;
            # tests / external duck-typed patterns may not. Fall back safely.
            ctx_priority_fn = getattr(pattern, "context_priority", None)
            if callable(ctx_priority_fn):
                priority = ctx_priority_fn(diagnosis, ctx)
            else:
                priority = pattern.priority(diagnosis)
        elif diagnosis:
            priority = pattern.priority(diagnosis)
        else:
            priority = 1.0

        priority *= _SAFETY_TIER_WEIGHTS.get(pattern.safety_tier, 1.0)
        if winner_domains and pattern.structural_domain in winner_domains:
            priority *= 1.08

        if round_hints:
            priority = max(priority, round_hints.priority_floor(pattern.name))
        if round_hints and priority > 0.0:
            priority *= round_hints.suppression_factor(pattern.name)
            priority *= round_hints.adaptive_priority_boost(pattern.name)

            # Apply Bayesian multiplier from historical effectiveness data
            if round_hints.learned_effectiveness:
                effectiveness = round_hints.learned_effectiveness.get(pattern.name)
                if effectiveness is not None:
                    wins_rate, _avg_delta = effectiveness
                    # Reconstruct total from win_rate to apply Bayesian smoothing
                    # We don't have raw counts, but the prior still applies:
                    # P = (win_rate * N + alpha) / (N + beta)
                    # For simplicity with rates, use:
                    # P_adjusted = (wins + alpha) / (total + beta)
                    # Since we only have the rate, assume N large enough
                    # that P ≈ win_rate, then compute multiplier vs baseline
                    p_adjusted = wins_rate  # already a rate
                    # Blend with prior: effective_P = (p * weight + baseline * prior_weight) / (weight + prior_weight)
                    # Using beta as prior strength (equivalent to beta pseudo-observations)
                    # This is the standard Bayesian posterior mean for Beta-Binomial
                    # We approximate: the DB already has enough data per pattern
                    multiplier = max(
                        _LEARNED_MULTIPLIER_MIN,
                        min(
                            _LEARNED_MULTIPLIER_MAX,
                            p_adjusted / _BASELINE_P if _BASELINE_P > 0 else 1.0,
                        ),
                    )
                    priority *= multiplier

        priorities[pattern.name] = priority

    if hard_dropped:
        print(
            f"Hard filter: dropped {len(hard_dropped)} pattern(s) "
            f"({', '.join(sorted(hard_dropped))})",
            file=sys.stderr,
        )

    # Log when learned priorities are applied
    if round_hints and round_hints.learned_effectiveness:
        applied_count = sum(
            1 for p in patterns
            if p.name in round_hints.learned_effectiveness
            and priorities.get(p.name, 0) > 0
        )
        if applied_count > 0:
            print(
                f"Learned priorities: {applied_count} patterns adjusted "
                f"from {len(round_hints.learned_effectiveness)} historical entries",
                file=sys.stderr,
            )

    return priorities


def allocate_budgets(
    patterns: list[Pattern],
    total_budget: int,
    diagnosis: Diagnosis | None,
    round_hints: RoundHints | None = None,
    ctx: FunctionContext | None = None,
) -> dict[str, int]:
    """Allocate variant budget proportionally by pattern priority.

    Uses each pattern's priority(diagnosis) score for budget weighting.
    When round_hints is provided, multiplies by suppression_factor to
    reduce budget for patterns that have failed consecutively.

    Patterns with priority 0.0 are skipped. Remaining patterns get at
    least _MIN_BUDGET variants each, with the rest distributed by
    priority weight.

    Args:
        patterns: List of pattern instances.
        total_budget: Total number of variants to distribute.
        diagnosis: Current diagnosis for priority scoring (None = all get 1.0).
        round_hints: Optional adaptive hints for suppression/boosting.

    Returns:
        Dict mapping pattern name to allocated budget.
    """
    budgets: dict[str, int] = {}
    priorities = _pattern_priorities(
        patterns,
        diagnosis,
        round_hints=round_hints,
        ctx=ctx,
    )
    relevant: list[Pattern] = []

    for pattern in patterns:
        p = priorities[pattern.name]
        priorities[pattern.name] = p
        if p <= 0.0:
            budgets[pattern.name] = 0
        else:
            relevant.append(pattern)

    if not relevant:
        return budgets

    # Ensure minimum budget per relevant pattern
    min_total = len(relevant) * _MIN_BUDGET
    if min_total >= total_budget:
        # Not enough budget for minimums — distribute evenly
        per_pattern = max(1, total_budget // len(relevant))
        for pattern in relevant:
            budgets[pattern.name] = per_pattern
        return budgets

    # Distribute remaining budget proportionally by priority
    remaining = total_budget - min_total
    total_weight = sum(priorities[p.name] for p in relevant)

    for pattern in relevant:
        w = priorities[pattern.name]
        extra = int(remaining * w / total_weight) if total_weight > 0 else 0
        budgets[pattern.name] = _MIN_BUDGET + extra

    # Distribute rounding remainder to the highest-priority pattern
    allocated = sum(budgets[p.name] for p in relevant)
    if allocated < total_budget:
        best = max(relevant, key=lambda p: priorities[p.name])
        budgets[best.name] += total_budget - allocated

    return budgets


def _all_regions_low_confidence(ctx: FunctionContext) -> bool:
    """Check if all mismatch regions have low confidence.

    Returns True when regions exist but every mismatch in every region
    has confidence below _LOW_CONFIDENCE_THRESHOLD (e.g. all interpolated
    at 0.4). Returns False when there are no regions or when any mismatch
    has high confidence.
    """
    if not ctx.mismatch_regions:
        return False  # No regions — not a low-confidence scenario
    for region in ctx.mismatch_regions:
        for m in region.mismatches:
            if m.confidence >= _LOW_CONFIDENCE_THRESHOLD:
                return False
    return True


def generate_variants(
    ctx: FunctionContext,
    patterns: list[Pattern],
    max_variants: int = 100,
    compose_pairs: list[tuple[str, str]] | None = None,
    chains: list[ChainSpec] | None = None,
    round_hints: RoundHints | None = None,
    failed_patterns: set[str] | None = None,
) -> Iterator[Variant]:
    """Apply patterns to a function context and yield variants.

    Thin wrapper over ``_generate_variants_impl``. When the B4 predictor flag
    (``PERMUTER_PREDICTOR``) is OFF — the default — this delegates straight to
    the impl generator, yielding lazily exactly as before (byte-for-byte
    identical behaviour, no buffering). When ON, it routes through
    ``_predict_and_cull`` which ranks the queue and drops the bottom fraction
    if (and only if) the queue is over the predictor budget.
    """
    from .predictor import predictor_enabled

    impl = _generate_variants_impl(
        ctx, patterns, max_variants,
        compose_pairs=compose_pairs,
        chains=chains,
        round_hints=round_hints,
        failed_patterns=failed_patterns,
    )

    # Generic pre-queue syntax probe: drop variants that introduce NEW parse
    # errors before they reach the (expensive) compile. Baseline-relative so it
    # never false-drops a compilable variant (see _syntax_probe_filter). Wraps
    # the lazy impl stream so it stays lazy and order-preserving.
    if syntax_probe_enabled():
        impl = _syntax_probe_filter(impl, ctx.file_source)

    if not predictor_enabled():
        # Default path: pure pass-through, preserves lazy yielding + order.
        yield from impl
        return

    yield from _predict_and_cull(impl, ctx, max_variants)


def _predict_and_cull(
    impl: "Iterator[Variant]",
    ctx: FunctionContext,
    max_variants: int,
) -> Iterator[Variant]:
    """Rank the generated queue by predicted win-prob and cull when over budget.

    Budget-gated: the predictor budget defaults to ``max_variants`` (so even
    with the flag ON it's a no-op unless a *tighter* budget is set via
    ``PERMUTER_PREDICTOR_BUDGET``). Materializes the queue only here, on the
    flag-ON path, so the default path never pays the buffering cost.
    """
    import os
    from .predictor import WinPredictor, VariantFeatures, rank_and_cull

    queue = list(impl)

    # Predictor budget: how many variants we're willing to compile. Defaults to
    # max_variants (no-op) — set PERMUTER_PREDICTOR_BUDGET lower to actually cull.
    try:
        budget = int(os.environ.get("PERMUTER_PREDICTOR_BUDGET", str(max_variants)))
    except (TypeError, ValueError):
        budget = max_variants

    if len(queue) <= budget:
        # Under budget — nothing to cull, preserve original order.
        yield from queue
        return

    model = WinPredictor.from_history()
    diag_fp = _diag_fingerprint(ctx)
    func_loc, func_stmts = _ctx_size(ctx)

    def feature_of(variant: Variant) -> VariantFeatures:
        label = _base_pattern_names(variant.pattern_name)[0]
        return VariantFeatures(
            pattern_label=label,
            diag_fingerprint=diag_fp,
            func_loc=func_loc,
            func_stmts=func_stmts,
        )

    survivors = rank_and_cull(queue, feature_of, budget, model)
    print(
        f"Predictor: queue {len(queue)} -> {len(survivors)} "
        f"(budget {budget})",
        file=sys.stderr,
    )
    yield from survivors


def _base_pattern_names(pattern_name: str) -> list[str]:
    """Split a (possibly composed) pattern name into base stage names."""
    for prefix in ("compose:", "chain:", "crosscompose:", "merge:", "evo_cross:", "evo_mut:"):
        if pattern_name.startswith(prefix):
            return pattern_name[len(prefix):].split("+")
    return [pattern_name]


def _diag_fingerprint(ctx: FunctionContext) -> str | None:
    """Diagnosis fingerprint for the predictor, from the context's diagnosis."""
    from .climb_history import diagnosis_fingerprint
    return diagnosis_fingerprint(ctx.diagnosis)


def _ctx_size(ctx: FunctionContext) -> tuple[int | None, int | None]:
    """(LOC, statement count) for the function in this context."""
    try:
        start, end = ctx.func_byte_range
        loc = ctx.file_source[start:end].count(b"\n") + 1
        return loc, len(ctx.statements)
    except Exception:
        return None, None


def _generate_variants_impl(
    ctx: FunctionContext,
    patterns: list[Pattern],
    max_variants: int = 100,
    compose_pairs: list[tuple[str, str]] | None = None,
    chains: list[ChainSpec] | None = None,
    round_hints: RoundHints | None = None,
    failed_patterns: set[str] | None = None,
) -> Iterator[Variant]:
    """Apply patterns to a function context and yield variants.

    Phase 1: Independent variants with per-pattern budgets (allocated
    proportionally by pattern priority scores, with optional suppression).

    Phase 2: Composed 2-stage variants (when compose_pairs is provided).

    Phase 3: N-stage chain variants (when chains is provided), using
    beam search via chain_variants().

    Budget split depends on what's enabled:
    - No composition: 100% independent
    - compose_pairs only: 70/30 independent/composed
    - chains only: 80/20 independent/chains
    - Both: 50/20/30 independent/composed/chains

    Args:
        ctx: Parsed function context.
        patterns: List of pattern instances to apply.
        max_variants: Maximum total variants to generate.
        compose_pairs: Optional list of (pattern_a_name, pattern_b_name) pairs
            for two-step composition. None = no 2-stage composition.
        chains: Optional list of ChainSpec for N-stage beam search.
            None = no chain composition.
        round_hints: Optional adaptive hints for suppression/boosting.
        failed_patterns: Optional set of pattern names that failed to build
            in Phase 1. Suppresses these from Phase 2/3 first-stage.
    """
    # Budget split
    if compose_pairs and chains:
        independent_budget = int(max_variants * 0.50)
        compose_budget = int(max_variants * 0.20)
        chain_budget = max_variants - independent_budget - compose_budget
    elif compose_pairs:
        independent_budget = int(max_variants * 0.7)
        compose_budget = max_variants - independent_budget
        chain_budget = 0
    elif chains:
        independent_budget = int(max_variants * 0.8)
        compose_budget = 0
        chain_budget = max_variants - independent_budget
    else:
        independent_budget = max_variants
        compose_budget = 0
        chain_budget = 0

    budgets = allocate_budgets(
        patterns, independent_budget, ctx.diagnosis,
        round_hints=round_hints, ctx=ctx,
    )
    priorities = _pattern_priorities(
        patterns,
        ctx.diagnosis,
        round_hints=round_hints,
        ctx=ctx,
    )

    total = 0
    skipped: list[str] = []

    # Cross-phase source dedup: skip variants with identical source bytes
    baseline_variant = Variant(
        name="baseline",
        pattern_name="baseline",
        description="baseline",
        source=ctx.file_source,
    )
    seen_sources: set[bytes] = {
        variant_identity_bytes(ctx.file_path, baseline_variant)
    }
    dedup_count = 0

    def _annotate_scope(variant: Variant) -> Variant:
        """Add scope-isolation metadata from the function context."""
        if variant.func_byte_range is None:
            variant.func_byte_range = ctx.func_byte_range
        if variant.original_source is None:
            variant.original_source = ctx.file_source
        return variant

    # Phase 1: Independent variants with per-pattern budgets
    ordered_patterns = sorted(
        patterns,
        key=lambda pattern: (-priorities.get(pattern.name, 0.0), pattern.name),
    )
    for pattern in ordered_patterns:
        budget = budgets.get(pattern.name, 0)
        if budget == 0:
            skipped.append(pattern.name)
            continue
        count = 0
        for variant in pattern.generate(ctx):
            _annotate_scope(variant)
            source_hash = variant_identity_bytes(ctx.file_path, variant)
            if source_hash in seen_sources:
                dedup_count += 1
                continue
            seen_sources.add(source_hash)
            yield variant
            count += 1
            total += 1
            if count >= budget:
                break
            if total >= independent_budget:
                break
        if total >= independent_budget:
            break

    if skipped:
        print(
            f"Skipping (not relevant): {', '.join(skipped)}",
            file=sys.stderr,
        )

    # Phase 2: Composed 2-stage variants
    if compose_pairs and compose_budget > 0:
        remaining = compose_budget
        pattern_map = {p.name: p for p in patterns}

        for name_a, name_b in compose_pairs:
            if remaining <= 0:
                break
            # Skip if first-stage pattern failed to build in Phase 1
            if failed_patterns and name_a in failed_patterns:
                continue

            stage_a = pattern_map.get(name_a)
            stage_b = pattern_map.get(name_b)
            if stage_a is None or stage_b is None:
                continue

            for variant in compose_variants(
                ctx, stage_a, stage_b,
                max_per_stage=10,
                max_total=remaining,
                round_hints=round_hints,
            ):
                _annotate_scope(variant)
                source_hash = variant_identity_bytes(ctx.file_path, variant)
                if source_hash in seen_sources:
                    dedup_count += 1
                    continue
                seen_sources.add(source_hash)
                yield variant
                total += 1
                remaining -= 1
                if remaining <= 0:
                    break

    # Phase 3: N-stage chain variants (beam search)
    if chains and chain_budget > 0:
        pattern_map = {p.name: p for p in patterns}
        remaining = chain_budget

        for chain in chains:
            if remaining <= 0:
                break
            # Skip chains whose first stage failed to build in Phase 1
            if failed_patterns and chain.stages[0] in failed_patterns:
                continue

            per_chain = min(remaining, chain.budget)
            count = 0

            for variant in chain_variants(
                ctx, chain, pattern_map,
                beam_width=max(5, len(chain.stages) * 3),
                max_per_stage=8,
                max_total=per_chain,
            ):
                _annotate_scope(variant)
                source_hash = variant_identity_bytes(ctx.file_path, variant)
                if source_hash in seen_sources:
                    dedup_count += 1
                    continue
                seen_sources.add(source_hash)
                yield variant
                total += 1
                remaining -= 1
                count += 1
                if remaining <= 0:
                    break

            if count > 0:
                print(
                    f"Chain [{'+'.join(chain.stages)}]: {count} variants "
                    f"({chain.reason})",
                    file=sys.stderr,
                )

    # Phase 4: Blind variants when all mismatch regions have low confidence.
    # When regions exist but are all interpolated (confidence < 0.5), the
    # region boundaries are unreliable. Generate additional variants with
    # blind_generation_mode=True so patterns aren't filtered by bad regions.
    if _all_regions_low_confidence(ctx):
        blind_budget = max(1, int(max_variants * _BLIND_BUDGET_FRACTION))
        blind_ctx = dataclasses.replace(ctx, blind_generation_mode=True)
        blind_budgets = allocate_budgets(
            patterns, blind_budget, blind_ctx.diagnosis,
            round_hints=round_hints, ctx=blind_ctx,
        )
        blind_count = 0
        for pattern in ordered_patterns:
            budget = blind_budgets.get(pattern.name, 0)
            if budget == 0:
                continue
            count = 0
            for variant in pattern.generate(blind_ctx):
                _annotate_scope(variant)
                source_hash = variant_identity_bytes(ctx.file_path, variant)
                if source_hash in seen_sources:
                    dedup_count += 1
                    continue
                seen_sources.add(source_hash)
                yield variant
                count += 1
                total += 1
                blind_count += 1
                if count >= budget:
                    break
                if blind_count >= blind_budget:
                    break
            if blind_count >= blind_budget:
                break
        if blind_count > 0:
            print(
                f"Blind fallback: {blind_count} variants "
                f"(all regions low confidence)",
                file=sys.stderr,
            )

    if dedup_count > 0:
        print(
            f"Source dedup: {dedup_count} duplicate variants skipped",
            file=sys.stderr,
        )
