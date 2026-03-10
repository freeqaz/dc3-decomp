"""Multi-variant merge -- combine non-overlapping improving variants.

Given multiple variants that independently improve match%, extract their
edit spans from the original source, check for overlaps, and merge
non-overlapping pairs into combined variants for scoring.
"""

from __future__ import annotations

from dataclasses import dataclass
from hashlib import md5

from .types import Variant, ScoreResult


@dataclass
class EditSpan:
    """A byte range that was modified by a variant."""

    start: int  # byte offset in original source (inclusive)
    end: int  # byte offset in original source (exclusive)
    replacement: bytes


def extract_edit_spans(original: bytes, modified: bytes) -> list[EditSpan]:
    """Extract contiguous edit spans between original and modified source.

    Uses prefix/suffix matching for the common single-edit case (O(n)).
    Falls back to a simple scan for multi-edit cases.
    """
    if original == modified:
        return []

    # Find common prefix
    min_len = min(len(original), len(modified))
    prefix_len = 0
    for i in range(min_len):
        if original[i] != modified[i]:
            break
        prefix_len += 1
    else:
        # One is a prefix of the other
        prefix_len = min_len

    # Find common suffix (don't overlap with prefix)
    suffix_len = 0
    max_suffix = min(len(original) - prefix_len, len(modified) - prefix_len)
    for i in range(max_suffix):
        if original[-(i + 1)] != modified[-(i + 1)]:
            break
        suffix_len += 1
    else:
        if max_suffix > 0:
            suffix_len = max_suffix

    orig_end = len(original) - suffix_len
    mod_end = len(modified) - suffix_len

    if prefix_len >= orig_end and prefix_len >= mod_end:
        return []  # Files are identical (shouldn't happen given early check)

    return [EditSpan(
        start=prefix_len,
        end=orig_end,
        replacement=modified[prefix_len:mod_end],
    )]


def edits_overlap(spans_a: list[EditSpan], spans_b: list[EditSpan]) -> bool:
    """Check if any edit spans from A overlap with any from B.

    Adjacent edits (end_a == start_b) do NOT count as overlapping.
    """
    for a in spans_a:
        for b in spans_b:
            if a.start < b.end and b.start < a.end:
                return True
    return False


def merge_variants(
    original: bytes,
    variant_a: Variant,
    variant_b: Variant,
    spans_a: list[EditSpan],
    spans_b: list[EditSpan],
) -> Variant:
    """Merge two non-overlapping variants into a combined variant.

    Applies all edit spans from both variants to the original source.
    Spans are applied in reverse order (highest start first) to preserve offsets.
    """
    all_spans = spans_a + spans_b
    # Sort by start descending so earlier spans don't shift later ones
    all_spans.sort(key=lambda s: s.start, reverse=True)

    result = original
    for span in all_spans:
        result = result[:span.start] + span.replacement + result[span.end:]

    # Build descriptive name from both variants' pattern names
    name_a = variant_a.pattern_name
    name_b = variant_b.pattern_name
    return Variant(
        name=f"merge:{variant_a.name}+{variant_b.name}",
        pattern_name=f"merge:{name_a}+{name_b}",
        description=f"Merged: {variant_a.description} AND {variant_b.description}",
        source=result,
        tags=variant_a.tags | variant_b.tags,
    )


def find_merge_candidates(
    original: bytes,
    results: list[ScoreResult],
    baseline: float,
    max_merge_attempts: int = 15,
) -> list[Variant]:
    """Find and construct merged variants from improving results.

    1. Filter to improving variants (build_success and match_percent > baseline)
    2. Cap at top-8 by delta
    3. Extract edit spans (cached)
    4. Sort pairs by combined delta descending
    5. Return merged variants for non-overlapping pairs

    Args:
        original: Original source bytes.
        results: All scored results from this round.
        baseline: Current match percentage baseline.
        max_merge_attempts: Max merged variants to return.

    Returns:
        List of Variant objects ready for scoring.
    """
    # Filter improving variants, dedup by source hash
    improving: list[tuple[ScoreResult, float]] = []
    seen_hashes: set[str] = set()
    for r in results:
        if not r.build_success or r.match_percent <= baseline:
            continue
        h = md5(r.variant.source).hexdigest()
        if h in seen_hashes:
            continue
        seen_hashes.add(h)
        improving.append((r, r.match_percent - baseline))

    if len(improving) < 2:
        return []

    # Cap at top-8 by delta
    improving.sort(key=lambda x: -x[1])
    improving = improving[:8]

    # Extract edit spans for each (cached)
    span_cache: dict[int, list[EditSpan]] = {}
    for r, _ in improving:
        key = id(r)
        span_cache[key] = extract_edit_spans(original, r.variant.source)

    # Generate pairs sorted by combined delta
    pairs: list[tuple[float, ScoreResult, ScoreResult]] = []
    for i in range(len(improving)):
        for j in range(i + 1, len(improving)):
            r_a, delta_a = improving[i]
            r_b, delta_b = improving[j]
            pairs.append((delta_a + delta_b, r_a, r_b))

    pairs.sort(key=lambda x: -x[0])

    # Build merged variants for non-overlapping pairs
    merged: list[Variant] = []
    merged_hashes: set[str] = set()
    for _, r_a, r_b in pairs:
        if len(merged) >= max_merge_attempts:
            break

        spans_a = span_cache[id(r_a)]
        spans_b = span_cache[id(r_b)]

        if not spans_a or not spans_b:
            continue
        if edits_overlap(spans_a, spans_b):
            continue

        candidate = merge_variants(
            original, r_a.variant, r_b.variant, spans_a, spans_b,
        )

        # Dedup merged source
        h = md5(candidate.source).hexdigest()
        if h in merged_hashes:
            continue
        merged_hashes.add(h)
        merged.append(candidate)

    return merged
