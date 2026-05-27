"""Tests for the C1 source-diff ranking signal.

Covers:
- score_source_diff() collapsing structural diffs to a scalar.
- BeamState.ranking_key preferring lower diff (tie-break) without
  overriding stronger signals.
- _compute_source_diff_score() ghidra-only, m2c-only, both, and
  neither code paths.
"""

from __future__ import annotations

import textwrap
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from scripts.permuter.ghidra_source_diff import (
    CallDiff,
    ControlFlowDiff,
    GuardDiff,
    SourceDiff,
    score_source_diff,
)
from scripts.permuter.types import BeamState


def _make_state(
    score: float = 50.0,
    fact_agreement: int = 0,
    guidance_agreement: int = 0,
    validation_tier: int = 0,
    source_diff_score: float | None = None,
    provenance: list[str] | None = None,
) -> BeamState:
    return BeamState(
        source=b"src",
        score=score,
        fact_agreement=fact_agreement,
        guidance_agreement=guidance_agreement,
        validation_tier=validation_tier,
        source_diff_score=source_diff_score,
        provenance=provenance or [],
    )


class TestScoreSourceDiff(unittest.TestCase):

    def test_empty_diff_scores_zero(self):
        self.assertEqual(score_source_diff(SourceDiff()), 0.0)

    def test_missing_call_adds_to_score(self):
        diff = SourceDiff(
            missing_calls=[CallDiff(name="Foo", side="ghidra_only",
                                    count_ghidra=2, count_source=0)],
        )
        # |2 - 0| = 2.0
        self.assertEqual(score_source_diff(diff), 2.0)

    def test_extra_call_adds_to_score(self):
        diff = SourceDiff(
            extra_calls=[CallDiff(name="Bar", side="source_only",
                                  count_ghidra=0, count_source=3)],
        )
        self.assertEqual(score_source_diff(diff), 3.0)

    def test_guard_diff_unit_penalty(self):
        diff = SourceDiff(
            guard_diffs=[
                GuardDiff(variable="p", side="ghidra_only"),
                GuardDiff(variable="q", side="source_only"),
            ],
        )
        self.assertEqual(score_source_diff(diff), 2.0)

    def test_control_flow_count_mismatch(self):
        diff = SourceDiff(
            control_flow_diff=ControlFlowDiff(
                description="if: ghidra=2 source=1",
                ghidra_skeleton=["if", "if"],
                source_skeleton=["if"],
            ),
        )
        # one bucket disagrees by 1
        self.assertEqual(score_source_diff(diff), 1.0)

    def test_control_flow_same_counts_diff_order_small_penalty(self):
        diff = SourceDiff(
            control_flow_diff=ControlFlowDiff(
                description="same structure types, different order",
                ghidra_skeleton=["if", "for"],
                source_skeleton=["for", "if"],
            ),
        )
        self.assertEqual(score_source_diff(diff), 0.5)

    def test_combined_penalties_add(self):
        diff = SourceDiff(
            missing_calls=[CallDiff(name="A", side="ghidra_only",
                                    count_ghidra=1, count_source=0)],
            guard_diffs=[GuardDiff(variable="p", side="ghidra_only")],
            control_flow_diff=ControlFlowDiff(
                description="x",
                ghidra_skeleton=["if"],
                source_skeleton=[],
            ),
        )
        self.assertEqual(score_source_diff(diff), 3.0)


class TestRankingKeyWithSourceDiff(unittest.TestCase):

    def test_lower_diff_ranks_higher_on_tie(self):
        # Same score and same stronger signals → lower diff wins.
        a = _make_state(score=90.0, source_diff_score=1.0)
        b = _make_state(score=90.0, source_diff_score=5.0)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_none_diff_is_neutral_zero(self):
        # State without a diff (no decomp) compares equal to a state with
        # zero diff on this dimension alone — neither penalized.
        a = _make_state(score=90.0, source_diff_score=None)
        b = _make_state(score=90.0, source_diff_score=0.0)
        # Same rank key contribution from source_diff_bonus (both 0.0).
        self.assertEqual(a.ranking_key, b.ranking_key)

    def test_none_diff_beats_high_diff(self):
        # No decomp == neutral; a state with a large diff is worse.
        a = _make_state(score=90.0, source_diff_score=None)
        b = _make_state(score=90.0, source_diff_score=10.0)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_higher_match_overrides_lower_diff(self):
        # Match% is the dominant signal — diff cannot flip a real score gap.
        a = _make_state(score=90.0, source_diff_score=20.0)
        b = _make_state(score=80.0, source_diff_score=0.0)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_fact_agreement_outweighs_source_diff(self):
        # WHY: diff weight is intentionally 0.1× so a single fact_agreement
        # tick beats any realistic diff delta.
        a = _make_state(score=90.0, fact_agreement=1, source_diff_score=50.0)
        b = _make_state(score=90.0, fact_agreement=0, source_diff_score=0.0)
        self.assertGreater(a.ranking_key, b.ranking_key)

    def test_guidance_outweighs_source_diff(self):
        a = _make_state(score=90.0, guidance_agreement=1, source_diff_score=50.0)
        b = _make_state(score=90.0, guidance_agreement=0, source_diff_score=0.0)
        self.assertGreater(a.ranking_key, b.ranking_key)


# ---------------------------------------------------------------------------
# Integration: _compute_source_diff_score with reparse
# ---------------------------------------------------------------------------

_SOURCE_FILE = textwrap.dedent("""\
    int Compute(int x) {
        if (x) {
            return Helper(x);
        }
        return 0;
    }
""")

_GHIDRA_MATCHING = textwrap.dedent("""\
    int Compute(int x) {
        if (x != 0) {
            return Helper(x);
        }
        return 0;
    }
""")

_GHIDRA_DIVERGENT = textwrap.dedent("""\
    int Compute(int x) {
        if (x != 0) {
            Helper(x);
            Helper(x);
            return Other(x);
        }
        return 0;
    }
""")


class TestComputeSourceDiffScore(unittest.TestCase):

    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.source_path = Path(self.tmp.name) / "src.cpp"
        self.source_path.write_bytes(_SOURCE_FILE.encode())
        # These tests exercise the scoring computation itself, which is gated
        # off by the production default (PERMUTER_C1_SOURCE_DIFF=off). Force a
        # computing mode so the function actually runs; restore on cleanup.
        import os
        _prev = os.environ.get("PERMUTER_C1_SOURCE_DIFF")
        os.environ["PERMUTER_C1_SOURCE_DIFF"] = "both"
        self.addCleanup(
            lambda: os.environ.__setitem__("PERMUTER_C1_SOURCE_DIFF", _prev)
            if _prev is not None
            else os.environ.pop("PERMUTER_C1_SOURCE_DIFF", None)
        )

    def _compute(self, ghidra: str | None, m2c: str | None) -> float | None:
        from scripts.permuter.beam_search import _compute_source_diff_score
        return _compute_source_diff_score(
            _SOURCE_FILE.encode(),
            self.source_path,
            "Compute",
            ghidra,
            m2c,
        )

    def test_no_decomp_returns_none(self):
        self.assertIsNone(self._compute(None, None))

    def test_ghidra_only_matching_scores_low(self):
        score = self._compute(_GHIDRA_MATCHING, None)
        self.assertIsNotNone(score)
        self.assertLess(score, 2.0)

    def test_ghidra_only_divergent_scores_higher(self):
        low = self._compute(_GHIDRA_MATCHING, None)
        high = self._compute(_GHIDRA_DIVERGENT, None)
        self.assertIsNotNone(low)
        self.assertIsNotNone(high)
        self.assertGreater(high, low)

    def test_m2c_only_works_as_fallback(self):
        # Without Ghidra, m2c text drives the score (mirrors C2-fix pattern).
        score = self._compute(None, _GHIDRA_MATCHING)
        self.assertIsNotNone(score)
        self.assertLess(score, 2.0)

    def test_both_sources_averaged(self):
        # When both are present, score is the average of both individual scores.
        ghidra_only = self._compute(_GHIDRA_MATCHING, None)
        m2c_only = self._compute(None, _GHIDRA_DIVERGENT)
        both = self._compute(_GHIDRA_MATCHING, _GHIDRA_DIVERGENT)
        self.assertIsNotNone(both)
        # Average of the two single-source scores.
        expected = (ghidra_only + m2c_only) / 2
        self.assertAlmostEqual(both, expected, places=4)


class TestEnvKillSwitch(unittest.TestCase):
    """A/B kill-switch via PERMUTER_C1_SOURCE_DIFF env var."""

    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.source_path = Path(self.tmp.name) / "src.cpp"
        self.source_path.write_bytes(_SOURCE_FILE.encode())

    def _compute_with_env(self, value: str | None, ghidra: str | None, m2c: str | None) -> float | None:
        import os
        from scripts.permuter.beam_search import _compute_source_diff_score
        prev = os.environ.get("PERMUTER_C1_SOURCE_DIFF")
        if value is None:
            os.environ.pop("PERMUTER_C1_SOURCE_DIFF", None)
        else:
            os.environ["PERMUTER_C1_SOURCE_DIFF"] = value
        try:
            return _compute_source_diff_score(
                _SOURCE_FILE.encode(),
                self.source_path,
                "Compute",
                ghidra,
                m2c,
            )
        finally:
            if prev is None:
                os.environ.pop("PERMUTER_C1_SOURCE_DIFF", None)
            else:
                os.environ["PERMUTER_C1_SOURCE_DIFF"] = prev

    def test_off_disables_signal(self):
        self.assertIsNone(
            self._compute_with_env("off", _GHIDRA_MATCHING, _GHIDRA_DIVERGENT),
        )

    def test_ghidra_mode_ignores_m2c(self):
        ghidra_alone = self._compute_with_env("ghidra", _GHIDRA_MATCHING, None)
        mode_ghidra = self._compute_with_env(
            "ghidra", _GHIDRA_MATCHING, _GHIDRA_DIVERGENT,
        )
        self.assertAlmostEqual(mode_ghidra, ghidra_alone, places=4)

    def test_m2c_mode_ignores_ghidra(self):
        m2c_alone = self._compute_with_env("m2c", None, _GHIDRA_DIVERGENT)
        mode_m2c = self._compute_with_env(
            "m2c", _GHIDRA_MATCHING, _GHIDRA_DIVERGENT,
        )
        self.assertAlmostEqual(mode_m2c, m2c_alone, places=4)


if __name__ == "__main__":
    unittest.main()
