"""Tests for beam search infrastructure."""

from __future__ import annotations

import unittest
from pathlib import Path

from scripts.permuter.types import BeamConfig, BeamState
from scripts.permuter.beam_search import (
    _compute_guidance_agreement,
    _deduplicate_states,
    _escape_beam,
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

    def test_shorter_provenance_ranks_higher(self):
        a = _make_state(score=90.0, provenance=["a"])
        b = _make_state(score=90.0, provenance=["a", "b", "c"])
        self.assertGreater(a.ranking_key, b.ranking_key)


class TestSelectSurvivors(unittest.TestCase):

    def test_returns_all_if_under_width(self):
        states = [_make_state(score=i * 10.0) for i in range(3)]
        survivors = _select_survivors(states, width=8, diversity_min=3)
        self.assertEqual(len(survivors), 3)

    def test_trims_to_width(self):
        states = [_make_state(score=i * 5.0, source=f"s{i}".encode()) for i in range(10)]
        survivors = _select_survivors(states, width=4, diversity_min=2)
        self.assertEqual(len(survivors), 4)

    def test_best_scores_survive(self):
        states = [
            _make_state(score=95.0, patterns=["p1"], source=b"a"),
            _make_state(score=50.0, patterns=["p2"], source=b"b"),
            _make_state(score=90.0, patterns=["p3"], source=b"c"),
            _make_state(score=85.0, patterns=["p4"], source=b"d"),
            _make_state(score=60.0, patterns=["p5"], source=b"e"),
        ]
        survivors = _select_survivors(states, width=3, diversity_min=2)
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
        survivors = _select_survivors(states, width=3, diversity_min=2)
        families = [s.applied_patterns[-1] for s in survivors]
        # pB should be included despite lower score (diversity requirement)
        self.assertIn("pB", families)

    def test_empty_input_returns_empty(self):
        self.assertEqual(_select_survivors([], width=4, diversity_min=2), [])


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


class TestBeamConfig(unittest.TestCase):

    def test_defaults(self):
        cfg = BeamConfig()
        self.assertEqual(cfg.width, 8)
        self.assertEqual(cfg.depth, 4)
        self.assertEqual(cfg.expand, 24)
        self.assertEqual(cfg.escape, 4)
        self.assertEqual(cfg.diversity, 3)

    def test_custom(self):
        cfg = BeamConfig(width=16, depth=8)
        self.assertEqual(cfg.width, 16)
        self.assertEqual(cfg.depth, 8)


if __name__ == "__main__":
    unittest.main()
