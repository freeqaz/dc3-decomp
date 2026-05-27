"""Tests for roadmap item B2 — diff-inspect signals as hard filters.

Hard filters promote *strong* suppress signals from soft re-weighting to an
outright drop of the pattern from generation. The behavior is gated behind the
PERMUTER_HARD_FILTERS env flag (default off). These tests cover the
decision logic:

  * TargetFacts.hard_suppress_patterns — confidence threshold + boost conflict.
  * RoundHints.hard_drop — defers to force_pattern (boost wins).
  * generator._pattern_priorities — flag on drops to 0; flag off only
    down-weights (the existing soft path).
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path
from unittest import mock

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.target_facts import TargetFact, TargetFacts
from scripts.permuter.types import RoundHints
from scripts.permuter.generator import (
    _pattern_priorities,
    allocate_budgets,
    hard_filters_enabled,
)
from scripts.permuter.patterns.base import get_pattern


def _suppress_fact(pattern: str, confidence: float) -> TargetFact:
    return TargetFact(
        kind="mismatch_class",
        region=None,
        payload={"suppress_patterns": [pattern]},
        confidence=confidence,
        provenance="test.suppress",
    )


def _boost_fact(pattern: str, confidence: float = 0.9) -> TargetFact:
    return TargetFact(
        kind="mismatch_class",
        region=None,
        payload={"boost_patterns": [pattern]},
        confidence=confidence,
        provenance="test.boost",
    )


class TestHardSuppressExtraction(unittest.TestCase):
    """TargetFacts.hard_suppress_patterns confidence/conflict logic."""

    def test_strong_signal_qualifies(self):
        """A 0.9-confidence (atlas-negative class) suppress is hard-droppable."""
        facts = TargetFacts([_suppress_fact("switch_if_convert", 0.9)])
        self.assertEqual(
            facts.hard_suppress_patterns(), {"switch_if_convert"}
        )

    def test_weak_signal_does_not_qualify(self):
        """A 0.7-confidence (heuristic shape) suppress stays on the soft path."""
        facts = TargetFacts([_suppress_fact("u8_to_unsigned_long", 0.7)])
        self.assertEqual(facts.hard_suppress_patterns(), set())
        # ...but it IS still in the soft suppress set (pattern_recommendations).
        _, soft_suppress = facts.pattern_recommendations()
        self.assertIn("u8_to_unsigned_long", soft_suppress)

    def test_threshold_boundary(self):
        """Confidence exactly at the threshold qualifies; just below does not."""
        at = TargetFacts([_suppress_fact("p", 0.85)])
        below = TargetFacts([_suppress_fact("p", 0.84)])
        self.assertEqual(at.hard_suppress_patterns(0.85), {"p"})
        self.assertEqual(below.hard_suppress_patterns(0.85), set())

    def test_boost_conflict_excludes(self):
        """If any fact also boosts the pattern, it is NOT hard-dropped."""
        facts = TargetFacts([
            _suppress_fact("tail_call_reorder", 0.95),
            _boost_fact("tail_call_reorder", 0.6),
        ])
        self.assertEqual(facts.hard_suppress_patterns(), set())
        # The soft suppress set still lists it (we only changed hard behavior).
        _, soft_suppress = facts.pattern_recommendations()
        self.assertIn("tail_call_reorder", soft_suppress)

    def test_custom_threshold(self):
        facts = TargetFacts([_suppress_fact("p", 0.75)])
        self.assertEqual(facts.hard_suppress_patterns(0.7), {"p"})
        self.assertEqual(facts.hard_suppress_patterns(0.8), set())


class TestRoundHintsHardDrop(unittest.TestCase):
    """RoundHints.hard_drop decision."""

    def test_not_in_set_not_dropped(self):
        hints = RoundHints()
        self.assertFalse(hints.hard_drop("anything"))

    def test_in_set_dropped(self):
        hints = RoundHints()
        hints.hard_suppress_patterns = {"switch_if_convert"}
        self.assertTrue(hints.hard_drop("switch_if_convert"))

    def test_boost_force_overrides_drop(self):
        """A pattern that atlas forces (boost, not suppressed) survives."""
        hints = RoundHints()
        hints.hard_suppress_patterns = {"variable_extraction"}
        hints.atlas_boost_patterns = {"variable_extraction"}
        # force_pattern is True only when boosted AND not atlas-suppressed.
        self.assertTrue(hints.force_pattern("variable_extraction"))
        self.assertFalse(hints.hard_drop("variable_extraction"))


class TestGeneratorHardFilter(unittest.TestCase):
    """_pattern_priorities / allocate_budgets honor the env flag."""

    def setUp(self):
        # Two always-applicable patterns; we suppress one.
        self.patterns = [
            get_pattern("variable_extraction"),
            get_pattern("signed_unsigned"),
        ]
        self.hints = RoundHints()
        self.hints.hard_suppress_patterns = {"signed_unsigned"}

    def test_flag_off_only_downweights(self):
        """With the flag OFF the suppressed pattern keeps a nonzero priority."""
        with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": ""}, clear=False):
            self.assertFalse(hard_filters_enabled())
            prios = _pattern_priorities(self.patterns, None, round_hints=self.hints)
        # Not dropped — still has soft priority (atlas_suppress not set, so it
        # is not even softly suppressed here; the point is it is > 0).
        self.assertGreater(prios["signed_unsigned"], 0.0)

    def test_flag_on_drops_to_zero(self):
        """With the flag ON the suppressed pattern is dropped (priority 0)."""
        with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": "1"}, clear=False):
            self.assertTrue(hard_filters_enabled())
            prios = _pattern_priorities(self.patterns, None, round_hints=self.hints)
        self.assertEqual(prios["signed_unsigned"], 0.0)
        # The non-suppressed pattern is unaffected.
        self.assertGreater(prios["variable_extraction"], 0.0)

    def test_flag_on_drops_budget_to_zero(self):
        """A hard-dropped pattern gets zero variant budget (the actual payoff)."""
        with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": "1"}, clear=False):
            budgets = allocate_budgets(self.patterns, 100, None, round_hints=self.hints)
        self.assertEqual(budgets.get("signed_unsigned", 0), 0)
        self.assertGreater(budgets.get("variable_extraction", 0), 0)

    def test_flag_off_keeps_budget(self):
        """With the flag off the suppressed pattern still receives budget."""
        with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": ""}, clear=False):
            budgets = allocate_budgets(self.patterns, 100, None, round_hints=self.hints)
        self.assertGreater(budgets.get("signed_unsigned", 0), 0)

    def test_boost_forced_pattern_survives_flag_on(self):
        """An atlas-boosted pattern is not hard-dropped even with the flag on."""
        self.hints.atlas_boost_patterns = {"signed_unsigned"}
        with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": "1"}, clear=False):
            prios = _pattern_priorities(self.patterns, None, round_hints=self.hints)
        self.assertGreater(prios["signed_unsigned"], 0.0)

    def test_no_hints_no_drop(self):
        """No round_hints -> no hard filtering, even with the flag on."""
        with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": "1"}, clear=False):
            prios = _pattern_priorities(self.patterns, None, round_hints=None)
        for p in self.patterns:
            self.assertGreater(prios[p.name], 0.0)


class TestEnvFlagParsing(unittest.TestCase):
    def test_truthy_values(self):
        for val in ("1", "true", "yes", "on", "TRUE", "On"):
            with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": val}, clear=False):
                self.assertTrue(hard_filters_enabled(), val)

    def test_falsy_values(self):
        for val in ("", "0", "false", "no", "off", "  "):
            with mock.patch.dict(os.environ, {"PERMUTER_HARD_FILTERS": val}, clear=False):
                self.assertFalse(hard_filters_enabled(), repr(val))


if __name__ == "__main__":
    unittest.main()
