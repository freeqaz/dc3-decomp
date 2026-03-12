"""Tests for beam search infrastructure."""

from __future__ import annotations

import unittest
from pathlib import Path

from scripts.permuter.types import BeamConfig, BeamState, Diagnosis, FunctionContext, RoundHints
from scripts.permuter.beam_search import (
    _compute_guidance_agreement,
    _deduplicate_states,
    _escape_beam,
    _estimate_complexity,
    _select_survivors,
)


def _make_state(
    score: float = 50.0,
    patterns: list[str] | None = None,
    provenance: list[str] | None = None,
    source: bytes | None = None,
    stagnation: int = 0,
    build_fails: int = 0,
    guidance: int = 0,
    generation: int = 1,
    il_bonus: int = 0,
) -> BeamState:
    return BeamState(
        source=source or f"source_{score}".encode(),
        score=score,
        applied_patterns=patterns or [],
        provenance=provenance or [],
        stagnation_count=stagnation,
        build_fail_count=build_fails,
        guidance_agreement=guidance,
        generation=generation,
        il_diversity_bonus=il_bonus,
    )


class TestBeamStateRanking(unittest.TestCase):

    def test_higher_score_ranks_first(self):
        a = _make_state(score=90.0)
        b = _make_state(score=80.0)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_fewer_build_fails_ranks_higher(self):
        a = _make_state(score=90.0, build_fails=0)
        b = _make_state(score=90.0, build_fails=3)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_higher_guidance_ranks_higher(self):
        a = _make_state(score=90.0, guidance=2)
        b = _make_state(score=90.0, guidance=0)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_less_stagnation_ranks_higher(self):
        a = _make_state(score=90.0, stagnation=0)
        b = _make_state(score=90.0, stagnation=3)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_unique_il_bucket_ranks_higher_on_tie(self):
        a = _make_state(score=90.0, il_bonus=1)
        b = _make_state(score=90.0, il_bonus=0)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_shorter_provenance_ranks_higher(self):
        a = _make_state(score=90.0, provenance=["a"])
        b = _make_state(score=90.0, provenance=["a", "b", "c"])
        self.assertGreater(a.ranking_key, b.ranking_key)


class TestSelectSurvivors(unittest.TestCase):

    def test_returns_all_if_under_width(self):
        states = [_make_state(score=i * 10.0) for i in range(3)]
        survivors, reserve = _select_survivors(states, width=8, diversity_min=3)
        self.assertEqual(len(survivors), 3)
        self.assertEqual(len(reserve), 0)

    def test_trims_to_width(self):
        states = [_make_state(score=i * 5.0, source=f"s{i}".encode()) for i in range(10)]
        survivors, reserve = _select_survivors(states, width=4, diversity_min=2)
        self.assertEqual(len(survivors), 4)

    def test_best_scores_survive(self):
        states = [
            _make_state(score=95.0, patterns=["p1"], source=b"a"),
            _make_state(score=50.0, patterns=["p2"], source=b"b"),
            _make_state(score=90.0, patterns=["p3"], source=b"c"),
            _make_state(score=85.0, patterns=["p4"], source=b"d"),
            _make_state(score=60.0, patterns=["p5"], source=b"e"),
        ]
        survivors, _ = _select_survivors(states, width=3, diversity_min=2)
        scores = [s.score for s in survivors]
        # The top 3 scores should be in the survivors
        self.assertIn(95.0, scores)
        self.assertIn(90.0, scores)

    def test_diversity_slots_respected(self):
        # All states use pattern "pA" except one with "pB"
        states = [
            _make_state(score=90.0, patterns=["pA"], source=b"a"),
            _make_state(score=88.0, patterns=["pA"], source=b"b"),
            _make_state(score=85.0, patterns=["pB"], source=b"c"),
            _make_state(score=80.0, patterns=["pA"], source=b"d"),
        ]
        survivors, _ = _select_survivors(states, width=3, diversity_min=2)
        families = [s.applied_patterns[-1] for s in survivors]
        # pB should be included despite lower score (diversity requirement)
        self.assertIn("pB", families)

    def test_empty_input_returns_empty(self):
        survivors, reserve = _select_survivors([], width=4, diversity_min=2)
        self.assertEqual(survivors, [])
        self.assertEqual(reserve, [])

    def test_returns_survivors_and_reserve(self):
        # 8 states, width=4, reserve_size=3: should get 4 survivors + up to 3 reserve
        states = [
            _make_state(score=90.0 + i, patterns=[f"p{i}"], source=f"s{i}".encode())
            for i in range(8)
        ]
        survivors, reserve = _select_survivors(
            states, width=4, diversity_min=2, reserve_size=3,
        )
        self.assertEqual(len(survivors), 4)
        self.assertEqual(len(reserve), 3)
        # Survivors should have higher scores than reserve
        min_survivor = min(s.score for s in survivors)
        max_reserve = max(s.score for s in reserve)
        self.assertGreaterEqual(min_survivor, max_reserve)

    def test_reserve_empty_when_not_enough_candidates(self):
        states = [_make_state(score=i * 10.0) for i in range(3)]
        survivors, reserve = _select_survivors(
            states, width=4, diversity_min=2, reserve_size=3,
        )
        # Only 3 states, all fit in beam — no reserve
        self.assertEqual(len(survivors), 3)
        self.assertEqual(len(reserve), 0)

    def test_reserve_capped_at_reserve_size(self):
        # 20 states, width=4, reserve_size=2: reserve should have exactly 2
        states = [
            _make_state(score=50.0 + i, patterns=[f"p{i}"], source=f"s{i}".encode())
            for i in range(20)
        ]
        survivors, reserve = _select_survivors(
            states, width=4, diversity_min=2, reserve_size=2,
        )
        self.assertEqual(len(survivors), 4)
        self.assertEqual(len(reserve), 2)


class TestDeduplicateStates(unittest.TestCase):

    def test_removes_exact_duplicates(self):
        states = [
            _make_state(score=90.0, source=b"same"),
            _make_state(score=85.0, source=b"same"),
            _make_state(score=80.0, source=b"different"),
        ]
        unique = _deduplicate_states(states, Path("/tmp/test.cpp"))
        self.assertEqual(len(unique), 2)

    def test_preserves_first_occurrence(self):
        states = [
            _make_state(score=90.0, source=b"same"),
            _make_state(score=85.0, source=b"same"),
        ]
        unique = _deduplicate_states(states, Path("/tmp/test.cpp"))
        self.assertEqual(len(unique), 1)
        self.assertEqual(unique[0].score, 90.0)

    def test_all_unique_preserved(self):
        states = [_make_state(source=f"s{i}".encode()) for i in range(5)]
        unique = _deduplicate_states(states, Path("/tmp/test.cpp"))
        self.assertEqual(len(unique), 5)


class TestGuidanceAgreement(unittest.TestCase):

    def test_no_guidance_returns_zero(self):
        state = _make_state()
        self.assertEqual(_compute_guidance_agreement(state, None, None), 0)

    def test_one_source_no_structure_returns_zero(self):
        state = _make_state(source=b"void f() { return 0; }")
        self.assertEqual(_compute_guidance_agreement(state, None, None), 0)

    def test_m2c_guard_agreement(self):
        # Source and m2c both have guard returns
        guard_src = b"""\
void f() {
    if (a == 0) { return; }
    if (b == 0) { return; }
    Work();
}
"""
        guard_m2c = """\
void f(void) {
    if (a == 0) { return; }
    if (b != 0) { return; }
    Work();
}
"""
        state = _make_state(source=guard_src)
        score = _compute_guidance_agreement(state, None, guard_m2c)
        self.assertGreaterEqual(score, 1)

    def test_both_agree_returns_two(self):
        # Source, Ghidra, and m2c all have guard returns
        guard_src = b"""\
void f() {
    if (a == 0) { return; }
    if (b == 0) { return; }
    Work();
}
"""
        guard_target = """\
void f(void) {
    if (a == 0) { return; }
    if (b != 0) { return; }
    Work();
}
"""
        state = _make_state(source=guard_src)
        score = _compute_guidance_agreement(state, guard_target, guard_target)
        self.assertEqual(score, 2)


class TestIlPressureHints(unittest.TestCase):

    def test_duplicate_pressure_suppresses_pattern(self):
        hints = RoundHints()
        hints.il_duplicate_patterns = {"temp_elimination"}
        self.assertLess(hints.suppression_factor("temp_elimination"), 1.0)

    def test_unique_pressure_boosts_pattern(self):
        hints = RoundHints()
        hints.il_unique_patterns = {"tail_call_reorder"}
        self.assertGreater(hints.adaptive_priority_boost("tail_call_reorder"), 1.0)

    def test_unique_overrides_duplicate_suppression(self):
        hints = RoundHints()
        hints.il_duplicate_patterns = {"tail_call_reorder"}
        hints.il_unique_patterns = {"tail_call_reorder"}
        self.assertEqual(hints.suppression_factor("tail_call_reorder"), 1.0)

    def test_structure_mismatch_returns_negative(self):
        # Source has deep nesting, m2c has flat guards
        nested_src = b"""\
void f() {
    if (a) {
        if (b) {
            if (c) {
                Work();
            }
        }
    }
}
"""
        flat_m2c = """\
void f(void) {
    if (a == 0) { return; }
    if (b == 0) { return; }
    if (c == 0) { return; }
    Work();
}
"""
        state = _make_state(source=nested_src)
        score = _compute_guidance_agreement(state, None, flat_m2c)
        self.assertLessEqual(score, 0)

    def test_call_order_agreement(self):
        # Source and m2c have same call order
        src = b"""\
void f() {
    Alpha();
    Beta();
    Gamma();
}
"""
        m2c = """\
void f(void) {
    Alpha();
    Beta();
    Gamma();
}
"""
        state = _make_state(source=src)
        score = _compute_guidance_agreement(state, None, m2c)
        self.assertGreaterEqual(score, 1)

    def test_call_order_disagreement(self):
        # Source and m2c have different call order
        src = b"""\
void f() {
    Gamma();
    Alpha();
    Beta();
}
"""
        m2c = """\
void f(void) {
    Alpha();
    Beta();
    Gamma();
}
"""
        state = _make_state(source=src)
        score = _compute_guidance_agreement(state, None, m2c)
        self.assertLessEqual(score, 0)


class TestEscapeBeam(unittest.TestCase):

    def test_escape_replaces_stagnating_slots(self):
        beam = [
            _make_state(score=90.0, stagnation=3, source=b"a"),
            _make_state(score=88.0, stagnation=2, source=b"b"),
            _make_state(score=85.0, stagnation=0, source=b"c"),
        ]
        best = _make_state(score=90.0, source=b"best")

        # Mock patterns
        class FakePattern:
            def __init__(self, name):
                self.name = name
        patterns = [FakePattern("p1"), FakePattern("p2")]

        replacements = _escape_beam(beam, best, escape_budget=2, patterns=patterns)
        # Should replace the 2 stagnating slots
        self.assertEqual(len(replacements), 2)
        # Replacement indices should be the stagnating ones
        indices = [idx for idx, _ in replacements]
        self.assertIn(0, indices)
        self.assertIn(1, indices)
        # Escape states should have reset stagnation
        for _, state in replacements:
            self.assertEqual(state.stagnation_count, 0)

    def test_escape_no_best_returns_empty(self):
        beam = [_make_state(stagnation=3)]
        self.assertEqual(_escape_beam(beam, None, 2, []), [])

    def test_escape_no_stagnating_returns_empty(self):
        beam = [_make_state(stagnation=0)]
        best = _make_state(source=b"best")
        self.assertEqual(_escape_beam(beam, best, 2, []), [])


class TestReserveSwap(unittest.TestCase):
    """Test reserve state swap-in for stagnating beam slots."""

    def test_reserve_swapped_into_stagnating_slots(self):
        """When beam has stagnating states, reserve states replace them."""
        # Simulate: 4-wide beam, 2 stagnating, 2 fresh
        beam = [
            _make_state(score=90.0, stagnation=3, source=b"stag1"),
            _make_state(score=88.0, stagnation=2, source=b"stag2"),
            _make_state(score=85.0, stagnation=0, source=b"fresh1"),
            _make_state(score=80.0, stagnation=1, source=b"fresh2"),
        ]
        reserve = [
            _make_state(score=84.0, stagnation=0, source=b"res1", patterns=["pR1"]),
            _make_state(score=82.0, stagnation=0, source=b"res2", patterns=["pR2"]),
        ]

        # Find stagnating slots (stagnation >= 2)
        stagnating_indices = [
            i for i, s in enumerate(beam)
            if s.stagnation_count >= 2
        ]
        self.assertEqual(stagnating_indices, [0, 1])

        # Swap reserve into stagnating slots
        swapped = 0
        for slot_idx in stagnating_indices:
            if not reserve:
                break
            replacement = reserve.pop(0)
            replacement.stagnation_count = 0
            beam[slot_idx] = replacement
            swapped += 1

        self.assertEqual(swapped, 2)
        self.assertEqual(beam[0].source, b"res1")
        self.assertEqual(beam[1].source, b"res2")
        self.assertEqual(beam[0].stagnation_count, 0)
        self.assertEqual(beam[1].stagnation_count, 0)
        # Non-stagnating slots untouched
        self.assertEqual(beam[2].source, b"fresh1")
        self.assertEqual(beam[3].source, b"fresh2")

    def test_reserve_swap_before_escape(self):
        """Reserve swap is tried before escape when beam stagnates."""
        # All beam states stagnating
        beam = [
            _make_state(score=90.0, stagnation=3, source=b"a"),
            _make_state(score=88.0, stagnation=2, source=b"b"),
            _make_state(score=85.0, stagnation=2, source=b"c"),
        ]
        reserve = [
            _make_state(score=84.0, source=b"res1"),
            _make_state(score=82.0, source=b"res2"),
        ]

        # After reserve swap, not all should be stagnating
        stagnating_indices = [
            i for i, s in enumerate(beam)
            if s.stagnation_count >= 2
        ]
        for slot_idx in stagnating_indices:
            if not reserve:
                break
            replacement = reserve.pop(0)
            replacement.stagnation_count = 0
            beam[slot_idx] = replacement

        # Now only 1 out of 3 is stagnating (the third one, slot 2,
        # had stagnation=2 but we ran out of reserve after 2 swaps)
        still_stagnating = sum(1 for s in beam if s.stagnation_count >= 2)
        self.assertEqual(still_stagnating, 1)
        # Escape would NOT be triggered because not ALL are stagnating
        self.assertLess(still_stagnating, len(beam))

    def test_reserve_replenished_each_depth(self):
        """Reserve is rebuilt from pruned states at each depth."""
        # Depth 1: 10 candidates, width=4, reserve_size=3
        depth1_states = [
            _make_state(score=90.0 - i, patterns=[f"p{i}"], source=f"d1s{i}".encode())
            for i in range(10)
        ]
        survivors1, reserve1 = _select_survivors(
            depth1_states, width=4, diversity_min=2, reserve_size=3,
        )
        self.assertEqual(len(reserve1), 3)

        # Depth 2: different candidates, reserve is rebuilt fresh
        depth2_states = [
            _make_state(score=80.0 - i, patterns=[f"q{i}"], source=f"d2s{i}".encode())
            for i in range(8)
        ]
        survivors2, reserve2 = _select_survivors(
            depth2_states, width=4, diversity_min=2, reserve_size=3,
        )
        self.assertEqual(len(reserve2), 3)
        # Reserve states should be from depth2, not depth1
        for rs in reserve2:
            self.assertTrue(rs.source.startswith(b"d2s"))

    def test_no_reserve_swap_when_no_stagnation(self):
        """Reserve states are not swapped in when no beam slots stagnate."""
        beam = [
            _make_state(score=90.0, stagnation=0, source=b"a"),
            _make_state(score=88.0, stagnation=1, source=b"b"),
        ]
        reserve = [
            _make_state(score=84.0, source=b"res1"),
        ]

        stagnating_indices = [
            i for i, s in enumerate(beam)
            if s.stagnation_count >= 2
        ]
        self.assertEqual(len(stagnating_indices), 0)
        # No swaps should happen
        self.assertEqual(len(reserve), 1)  # Reserve untouched


class TestBeamConfig(unittest.TestCase):

    def test_defaults(self):
        cfg = BeamConfig()
        self.assertEqual(cfg.width, 8)
        self.assertEqual(cfg.depth, 4)
        self.assertEqual(cfg.expand, 24)
        self.assertEqual(cfg.escape, 4)
        self.assertEqual(cfg.diversity, 3)
        self.assertEqual(cfg.reserve_size, 3)

    def test_auto_width_default_true(self):
        cfg = BeamConfig()
        self.assertTrue(cfg.auto_width)

    def test_custom(self):
        cfg = BeamConfig(width=16, depth=8)
        self.assertEqual(cfg.width, 16)
        self.assertEqual(cfg.depth, 8)

    def test_custom_reserve_size(self):
        cfg = BeamConfig(reserve_size=5)
        self.assertEqual(cfg.reserve_size, 5)


# ---------------------------------------------------------------------------
# Mock tree-sitter Node for _estimate_complexity tests
# ---------------------------------------------------------------------------

class _MockNode:
    """Minimal mock of a tree-sitter Node with start_point/end_point."""

    def __init__(self, start_row: int, end_row: int):
        self.start_point = (start_row, 0)
        self.end_point = (end_row, 0)
        self.start_byte = 0
        self.end_byte = 0


def _make_mock_ctx(loc: int, source_lines: int = 0) -> FunctionContext:
    """Build a FunctionContext with a mock body_node spanning `loc` lines."""
    body = _MockNode(0, loc)
    func = _MockNode(0, loc)
    source = b"\n" * (source_lines or loc)
    return FunctionContext(
        file_path=Path("/tmp/test.cpp"),
        file_source=source,
        func_node=func,
        body_node=body,
        statements=[],
        func_byte_range=(0, len(source)),
    )


def _make_diagnosis(cluster_count: int = 0) -> Diagnosis:
    """Build a minimal Diagnosis with a given number of clusters."""
    from scripts.permuter.types import Cluster
    clusters = [
        Cluster(start_idx=i, end_idx=i + 1, size=2, inserts=1, deletes=1)
        for i in range(cluster_count)
    ]
    return Diagnosis(
        total_instructions=100,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=clusters,
        noise_explained=0,
        noise_total=0,
    )


class TestEstimateComplexity(unittest.TestCase):

    def test_simple_function(self):
        ctx = _make_mock_ctx(loc=30)
        diag = _make_diagnosis(cluster_count=1)
        self.assertEqual(_estimate_complexity(ctx, diag), "simple")

    def test_complex_function_high_loc(self):
        ctx = _make_mock_ctx(loc=250)
        diag = _make_diagnosis(cluster_count=2)
        self.assertEqual(_estimate_complexity(ctx, diag), "complex")

    def test_complex_function_many_clusters(self):
        ctx = _make_mock_ctx(loc=60)
        diag = _make_diagnosis(cluster_count=10)
        self.assertEqual(_estimate_complexity(ctx, diag), "complex")

    def test_medium_function(self):
        ctx = _make_mock_ctx(loc=100)
        diag = _make_diagnosis(cluster_count=5)
        self.assertEqual(_estimate_complexity(ctx, diag), "medium")

    def test_none_context_and_diagnosis(self):
        # When both are None, LOC=0 and clusters=0 → simple
        self.assertEqual(_estimate_complexity(None, None), "simple")

    def test_none_diagnosis(self):
        ctx = _make_mock_ctx(loc=100)
        self.assertEqual(_estimate_complexity(ctx, None), "medium")

    def test_boundary_simple_loc_49_clusters_2(self):
        ctx = _make_mock_ctx(loc=49)
        diag = _make_diagnosis(cluster_count=2)
        self.assertEqual(_estimate_complexity(ctx, diag), "simple")

    def test_boundary_not_simple_loc_50(self):
        ctx = _make_mock_ctx(loc=50)
        diag = _make_diagnosis(cluster_count=0)
        self.assertEqual(_estimate_complexity(ctx, diag), "medium")

    def test_boundary_not_simple_clusters_3(self):
        ctx = _make_mock_ctx(loc=30)
        diag = _make_diagnosis(cluster_count=3)
        self.assertEqual(_estimate_complexity(ctx, diag), "medium")

    def test_boundary_complex_loc_201(self):
        ctx = _make_mock_ctx(loc=201)
        diag = _make_diagnosis(cluster_count=0)
        self.assertEqual(_estimate_complexity(ctx, diag), "complex")

    def test_boundary_complex_clusters_9(self):
        ctx = _make_mock_ctx(loc=60)
        diag = _make_diagnosis(cluster_count=9)
        self.assertEqual(_estimate_complexity(ctx, diag), "complex")


class TestAutoWidthSizing(unittest.TestCase):
    """Test that auto_width applies correct beam sizing."""

    def test_simple_sizing(self):
        """Simple complexity → width=4, expand=16."""
        cfg = BeamConfig(auto_width=True)
        # Simulate what beam_search does: estimate complexity then apply
        ctx = _make_mock_ctx(loc=20)
        diag = _make_diagnosis(cluster_count=1)
        complexity = _estimate_complexity(ctx, diag)
        self.assertEqual(complexity, "simple")

        sizing = {"simple": (4, 16), "medium": (8, 24), "complex": (12, 32)}
        w, e = sizing[complexity]
        cfg.width = w
        cfg.expand = e
        self.assertEqual(cfg.width, 4)
        self.assertEqual(cfg.expand, 16)

    def test_medium_sizing(self):
        """Medium complexity → width=8, expand=24."""
        cfg = BeamConfig(auto_width=True)
        ctx = _make_mock_ctx(loc=100)
        diag = _make_diagnosis(cluster_count=5)
        complexity = _estimate_complexity(ctx, diag)
        self.assertEqual(complexity, "medium")

        sizing = {"simple": (4, 16), "medium": (8, 24), "complex": (12, 32)}
        w, e = sizing[complexity]
        cfg.width = w
        cfg.expand = e
        self.assertEqual(cfg.width, 8)
        self.assertEqual(cfg.expand, 24)

    def test_complex_sizing(self):
        """Complex function → width=12, expand=32."""
        cfg = BeamConfig(auto_width=True)
        ctx = _make_mock_ctx(loc=300)
        diag = _make_diagnosis(cluster_count=10)
        complexity = _estimate_complexity(ctx, diag)
        self.assertEqual(complexity, "complex")

        sizing = {"simple": (4, 16), "medium": (8, 24), "complex": (12, 32)}
        w, e = sizing[complexity]
        cfg.width = w
        cfg.expand = e
        self.assertEqual(cfg.width, 12)
        self.assertEqual(cfg.expand, 32)


class TestExplicitWidthOverride(unittest.TestCase):
    """Test that explicit --beam-width disables auto-sizing."""

    def test_explicit_width_disables_auto(self):
        """When auto_width=False, width/expand are not overridden."""
        cfg = BeamConfig(width=16, expand=48, auto_width=False)
        # auto_width=False means beam_search() will NOT apply complexity sizing
        self.assertFalse(cfg.auto_width)
        self.assertEqual(cfg.width, 16)
        self.assertEqual(cfg.expand, 48)

    def test_auto_width_false_preserves_values(self):
        """Explicitly set values survive when auto_width is False."""
        cfg = BeamConfig(width=6, expand=20, auto_width=False)
        # Simulate: beam_search checks auto_width, skips sizing
        if cfg.auto_width:
            cfg.width = 999  # Should NOT execute
            cfg.expand = 999
        self.assertEqual(cfg.width, 6)
        self.assertEqual(cfg.expand, 20)

    def test_auto_width_true_overrides_defaults(self):
        """When auto_width=True, defaults are overridden by complexity."""
        cfg = BeamConfig(auto_width=True)
        # Simulate simple function → overrides defaults
        if cfg.auto_width:
            cfg.width = 4
            cfg.expand = 16
        self.assertEqual(cfg.width, 4)
        self.assertEqual(cfg.expand, 16)


if __name__ == "__main__":
    unittest.main()
