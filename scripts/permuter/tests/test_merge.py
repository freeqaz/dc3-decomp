"""Tests for the multi-variant merge module."""

import pytest
from scripts.permuter.merge import (
    EditSpan,
    extract_edit_spans,
    edits_overlap,
    merge_variants,
    find_merge_candidates,
)
from scripts.permuter.types import Variant, ScoreResult


class TestExtractEditSpans:
    def test_identical_returns_empty(self):
        original = b"hello world"
        assert extract_edit_spans(original, original) == []

    def test_single_replacement(self):
        original = b"int x = 0;"
        modified = b"int x = 1;"
        spans = extract_edit_spans(original, modified)
        assert len(spans) == 1
        assert spans[0].start == 8
        assert spans[0].end == 9
        assert spans[0].replacement == b"1"

    def test_insertion(self):
        original = b"ab"
        modified = b"aXb"
        spans = extract_edit_spans(original, modified)
        assert len(spans) == 1
        assert spans[0].start == 1
        assert spans[0].end == 1
        assert spans[0].replacement == b"X"

    def test_deletion(self):
        original = b"aXb"
        modified = b"ab"
        spans = extract_edit_spans(original, modified)
        assert len(spans) == 1
        assert spans[0].start == 1
        assert spans[0].end == 2
        assert spans[0].replacement == b""

    def test_prefix_change(self):
        original = b"AAA_suffix"
        modified = b"BBB_suffix"
        spans = extract_edit_spans(original, modified)
        assert len(spans) == 1
        assert spans[0].start == 0
        assert spans[0].end == 3
        assert spans[0].replacement == b"BBB"

    def test_suffix_change(self):
        original = b"prefix_AAA"
        modified = b"prefix_BBB"
        spans = extract_edit_spans(original, modified)
        assert len(spans) == 1
        assert spans[0].start == 7
        assert spans[0].end == 10
        assert spans[0].replacement == b"BBB"


class TestEditsOverlap:
    def test_no_overlap(self):
        a = [EditSpan(0, 5, b"x")]
        b = [EditSpan(10, 15, b"y")]
        assert not edits_overlap(a, b)

    def test_overlap(self):
        a = [EditSpan(0, 10, b"x")]
        b = [EditSpan(5, 15, b"y")]
        assert edits_overlap(a, b)

    def test_adjacent_not_overlapping(self):
        a = [EditSpan(0, 5, b"x")]
        b = [EditSpan(5, 10, b"y")]
        assert not edits_overlap(a, b)

    def test_contained(self):
        a = [EditSpan(0, 20, b"x")]
        b = [EditSpan(5, 10, b"y")]
        assert edits_overlap(a, b)

    def test_empty_spans(self):
        assert not edits_overlap([], [EditSpan(0, 5, b"x")])
        assert not edits_overlap([EditSpan(0, 5, b"x")], [])
        assert not edits_overlap([], [])


class TestMergeVariants:
    def test_non_overlapping_merge(self):
        original = b"AAAA_middle_BBBB"
        mod_a = b"XXXX_middle_BBBB"
        mod_b = b"AAAA_middle_YYYY"

        var_a = Variant(
            name="a", pattern_name="pat_a", description="A", source=mod_a,
            tags=frozenset({"tag_a"}),
        )
        var_b = Variant(
            name="b", pattern_name="pat_b", description="B", source=mod_b,
            tags=frozenset({"tag_b"}),
        )

        spans_a = extract_edit_spans(original, mod_a)
        spans_b = extract_edit_spans(original, mod_b)

        assert not edits_overlap(spans_a, spans_b)

        merged = merge_variants(original, var_a, var_b, spans_a, spans_b)
        assert merged.source == b"XXXX_middle_YYYY"
        assert merged.pattern_name == "merge:pat_a+pat_b"
        assert "merge:" in merged.name
        assert merged.tags == frozenset({"tag_a", "tag_b"})

    def test_roundtrip_correctness(self):
        """Verify merge produces the union of both edits."""
        original = b"void foo() { int a = 0; float b = 1.0; }"
        # Variant A changes 'int' to 'unsigned'
        mod_a = b"void foo() { unsigned a = 0; float b = 1.0; }"
        # Variant B changes '1.0' to '1.0f'
        mod_b = b"void foo() { int a = 0; float b = 1.0f; }"

        var_a = Variant(name="a", pattern_name="signed_unsigned", description="A", source=mod_a)
        var_b = Variant(name="b", pattern_name="float_literal", description="B", source=mod_b)

        spans_a = extract_edit_spans(original, mod_a)
        spans_b = extract_edit_spans(original, mod_b)

        assert not edits_overlap(spans_a, spans_b)
        merged = merge_variants(original, var_a, var_b, spans_a, spans_b)
        assert merged.source == b"void foo() { unsigned a = 0; float b = 1.0f; }"


class TestFindMergeCandidates:
    def _make_result(self, name, pname, source, pct, build_ok=True):
        return ScoreResult(
            variant=Variant(name=name, pattern_name=pname, description=name, source=source),
            match_percent=pct,
            build_success=build_ok,
        )

    def test_no_improvers(self):
        original = b"original"
        results = [
            self._make_result("a", "p1", b"mod_a", 50.0),  # not improving
        ]
        assert find_merge_candidates(original, results, baseline=60.0) == []

    def test_single_improver(self):
        original = b"original"
        results = [
            self._make_result("a", "p1", b"mod_a", 70.0),
        ]
        assert find_merge_candidates(original, results, baseline=60.0) == []

    def test_overlapping_edits_skipped(self):
        original = b"AAAA"
        # Both modify the same region
        results = [
            self._make_result("a", "p1", b"BBBB", 70.0),
            self._make_result("b", "p2", b"CCCC", 72.0),
        ]
        candidates = find_merge_candidates(original, results, baseline=60.0)
        assert len(candidates) == 0

    def test_non_overlapping_produces_merge(self):
        original = b"AAAA_sep_BBBB"
        results = [
            self._make_result("a", "p1", b"XXXX_sep_BBBB", 70.0),
            self._make_result("b", "p2", b"AAAA_sep_YYYY", 72.0),
        ]
        candidates = find_merge_candidates(original, results, baseline=60.0)
        assert len(candidates) == 1
        assert candidates[0].source == b"XXXX_sep_YYYY"

    def test_build_failures_excluded(self):
        original = b"AAAA_sep_BBBB"
        results = [
            self._make_result("a", "p1", b"XXXX_sep_BBBB", 70.0, build_ok=False),
            self._make_result("b", "p2", b"AAAA_sep_YYYY", 72.0),
        ]
        assert find_merge_candidates(original, results, baseline=60.0) == []

    def test_dedup_identical_sources(self):
        original = b"AAAA_sep_BBBB"
        results = [
            self._make_result("a", "p1", b"XXXX_sep_BBBB", 70.0),
            self._make_result("a2", "p1", b"XXXX_sep_BBBB", 71.0),  # same source
            self._make_result("b", "p2", b"AAAA_sep_YYYY", 72.0),
        ]
        candidates = find_merge_candidates(original, results, baseline=60.0)
        assert len(candidates) == 1
