"""Tests for the pattern performance model (Bayesian learned priorities)."""

from __future__ import annotations

import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.permuter.pattern_stats import (
    _CACHE_DB,
    _SCHEMA_STATEMENTS,
    query_pattern_effectiveness,
)
from scripts.permuter.generator import (
    _BASELINE_P,
    _BAYESIAN_ALPHA,
    _BAYESIAN_BETA,
    _LEARNED_MULTIPLIER_MAX,
    _LEARNED_MULTIPLIER_MIN,
    _pattern_priorities,
)
from scripts.permuter.types import RoundHints


def _create_test_db(path: Path, rows: list[tuple]) -> None:
    """Create a test permuter_cache.db with pattern_runs data.

    Each row is (pattern, won, best_delta, diagnosis_category).
    """
    conn = sqlite3.connect(str(path))
    for stmt in _SCHEMA_STATEMENTS:
        conn.execute(stmt)
    conn.commit()

    import time
    now = time.time()
    for pattern, won, best_delta, diag_cat in rows:
        conn.execute(
            "INSERT INTO pattern_runs "
            "(timestamp, symbol, function_name, source_path, "
            "pattern, variants_generated, variants_built, build_failures, "
            "won, best_delta, best_variant, "
            "initial_pct, final_pct, diagnosis_category, unit, caller) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (
                now, "?Test@@UAAXXZ", "Test::Fn", "src/test.cpp",
                pattern, 10, 8, 2,
                int(won), best_delta, None,
                80.0, 85.0, diag_cat, "test_unit", "test",
            ),
        )
    conn.commit()
    conn.close()


class TestQueryPatternEffectiveness(unittest.TestCase):
    """Test query_pattern_effectiveness() with mock DB data."""

    def test_correct_win_rates_and_avg_deltas(self):
        """Verify correct win rate and avg delta computation."""
        with tempfile.TemporaryDirectory() as tmpdir:
            db_path = Path(tmpdir) / "permuter_cache.db"
            # pattern_a: 3 runs, 2 wins (delta 1.0, 2.0), 1 loss (delta 0.0)
            _create_test_db(db_path, [
                ("pattern_a", True, 1.0, None),
                ("pattern_a", True, 2.0, None),
                ("pattern_a", False, 0.0, None),
                # pattern_b: 2 runs, 0 wins
                ("pattern_b", False, 0.0, None),
                ("pattern_b", False, -0.5, None),
            ])

            with patch("scripts.permuter.pattern_stats._CACHE_DB", db_path):
                result = query_pattern_effectiveness()

            self.assertIn("pattern_a", result)
            self.assertIn("pattern_b", result)

            win_rate_a, avg_delta_a = result["pattern_a"]
            self.assertAlmostEqual(win_rate_a, 2.0 / 3.0, places=4)
            self.assertAlmostEqual(avg_delta_a, (1.0 + 2.0 + 0.0) / 3.0, places=4)

            win_rate_b, avg_delta_b = result["pattern_b"]
            self.assertAlmostEqual(win_rate_b, 0.0, places=4)
            self.assertAlmostEqual(avg_delta_b, (0.0 + -0.5) / 2.0, places=4)

    def test_diagnosis_category_filter(self):
        """Verify filtering by diagnosis_category works."""
        with tempfile.TemporaryDirectory() as tmpdir:
            db_path = Path(tmpdir) / "permuter_cache.db"
            _create_test_db(db_path, [
                ("pattern_a", True, 1.0, "regswap"),
                ("pattern_a", False, 0.0, "structural"),
                ("pattern_b", True, 0.5, "regswap"),
            ])

            with patch("scripts.permuter.pattern_stats._CACHE_DB", db_path):
                result = query_pattern_effectiveness(diagnosis_category="regswap")

            # Only "regswap" rows should be included
            self.assertIn("pattern_a", result)
            self.assertIn("pattern_b", result)
            # pattern_a: 1 run in regswap, 1 win
            win_rate_a, _ = result["pattern_a"]
            self.assertAlmostEqual(win_rate_a, 1.0, places=4)
            # pattern_b: 1 run in regswap, 1 win
            win_rate_b, _ = result["pattern_b"]
            self.assertAlmostEqual(win_rate_b, 1.0, places=4)

    def test_diagnosis_category_filter_excludes_others(self):
        """Verify diagnosis filter excludes non-matching categories."""
        with tempfile.TemporaryDirectory() as tmpdir:
            db_path = Path(tmpdir) / "permuter_cache.db"
            _create_test_db(db_path, [
                ("only_structural", True, 2.0, "structural"),
            ])

            with patch("scripts.permuter.pattern_stats._CACHE_DB", db_path):
                result = query_pattern_effectiveness(diagnosis_category="regswap")

            # "only_structural" should NOT appear in regswap results
            self.assertNotIn("only_structural", result)

    def test_graceful_fallback_missing_db(self):
        """Return empty dict when DB file doesn't exist."""
        fake_path = Path("/tmp/claude-1000/nonexistent_test_db.db")
        with patch("scripts.permuter.pattern_stats._CACHE_DB", fake_path):
            result = query_pattern_effectiveness()
        self.assertEqual(result, {})

    def test_graceful_fallback_no_table(self):
        """Return empty dict when pattern_runs table doesn't exist."""
        with tempfile.TemporaryDirectory() as tmpdir:
            db_path = Path(tmpdir) / "permuter_cache.db"
            # Create DB but don't create the table
            conn = sqlite3.connect(str(db_path))
            conn.execute("CREATE TABLE other_table (id INTEGER PRIMARY KEY)")
            conn.commit()
            conn.close()

            with patch("scripts.permuter.pattern_stats._CACHE_DB", db_path):
                result = query_pattern_effectiveness()
            self.assertEqual(result, {})

    def test_empty_table_returns_empty_dict(self):
        """Return empty dict when table exists but has no rows."""
        with tempfile.TemporaryDirectory() as tmpdir:
            db_path = Path(tmpdir) / "permuter_cache.db"
            conn = sqlite3.connect(str(db_path))
            for stmt in _SCHEMA_STATEMENTS:
                conn.execute(stmt)
            conn.commit()
            conn.close()

            with patch("scripts.permuter.pattern_stats._CACHE_DB", db_path):
                result = query_pattern_effectiveness()
            self.assertEqual(result, {})


class TestBayesianMultiplier(unittest.TestCase):
    """Test the Bayesian multiplier computation in _pattern_priorities."""

    def _make_dummy_pattern(self, name: str, priority_val: float = 1.0):
        """Create a minimal pattern mock for testing priorities."""

        class DummyPattern:
            def __init__(self, name, pval):
                self.name = name
                self.safety_tier = "normal"
                self.structural_domain = "test"
                self._pval = pval

            def priority(self, diagnosis):
                return self._pval

        return DummyPattern(name, priority_val)

    def test_high_win_rate_caps_at_max(self):
        """A pattern with very high win rate should cap at 2.0x multiplier."""
        hints = RoundHints()
        # Win rate = 1.0 -> multiplier = 1.0 / 0.091 ≈ 11.0 -> capped at 2.0
        hints.learned_effectiveness = {"test_pattern": (1.0, 5.0)}

        patterns = [self._make_dummy_pattern("test_pattern")]
        priorities = _pattern_priorities(patterns, None, round_hints=hints)

        # Base priority is 1.0 (no diagnosis), * 1.0 (normal tier)
        # * 1.0 (no suppression/boost) * 2.0 (capped multiplier)
        expected = 1.0 * _LEARNED_MULTIPLIER_MAX
        self.assertAlmostEqual(priorities["test_pattern"], expected, places=2)

    def test_zero_wins_caps_at_min(self):
        """A pattern with zero wins should cap at 0.3x multiplier."""
        hints = RoundHints()
        # Win rate = 0.0 -> multiplier = 0.0 / 0.091 = 0.0 -> capped at 0.3
        hints.learned_effectiveness = {"test_pattern": (0.0, -1.0)}

        patterns = [self._make_dummy_pattern("test_pattern")]
        priorities = _pattern_priorities(patterns, None, round_hints=hints)

        expected = 1.0 * _LEARNED_MULTIPLIER_MIN
        self.assertAlmostEqual(priorities["test_pattern"], expected, places=2)

    def test_moderate_win_rate_gives_moderate_multiplier(self):
        """A pattern with moderate win rate should give proportional multiplier."""
        hints = RoundHints()
        # Win rate = baseline_P (0.091) -> multiplier = 1.0 (no change)
        hints.learned_effectiveness = {"test_pattern": (_BASELINE_P, 0.5)}

        patterns = [self._make_dummy_pattern("test_pattern")]
        priorities = _pattern_priorities(patterns, None, round_hints=hints)

        # multiplier = baseline_P / baseline_P = 1.0
        self.assertAlmostEqual(priorities["test_pattern"], 1.0, places=2)

    def test_no_learned_data_means_no_adjustment(self):
        """Without learned_effectiveness, priorities should be unaffected."""
        hints = RoundHints()
        # Empty learned_effectiveness (default)

        patterns = [self._make_dummy_pattern("test_pattern")]
        priorities_with_hints = _pattern_priorities(
            patterns, None, round_hints=hints
        )
        priorities_without = _pattern_priorities(patterns, None, round_hints=None)

        # Should be very close (hints without learned data ≈ no hints)
        # Slight difference due to suppression_factor/adaptive_priority_boost
        # which both return 1.0 for empty hints
        self.assertAlmostEqual(
            priorities_with_hints["test_pattern"],
            priorities_without["test_pattern"],
            places=4,
        )

    def test_pattern_not_in_learned_data_unaffected(self):
        """Patterns without historical data should not get a multiplier."""
        hints = RoundHints()
        hints.learned_effectiveness = {"other_pattern": (0.5, 1.0)}

        patterns = [self._make_dummy_pattern("test_pattern")]
        priorities = _pattern_priorities(patterns, None, round_hints=hints)

        # "test_pattern" not in learned_effectiveness -> no multiplier
        self.assertAlmostEqual(priorities["test_pattern"], 1.0, places=2)

    def test_multiplier_bounds(self):
        """Verify multiplier is bounded between min and max."""
        hints = RoundHints()

        # Test various win rates and verify bounds
        test_cases = [
            (0.0, _LEARNED_MULTIPLIER_MIN),   # Floor
            (0.01, _LEARNED_MULTIPLIER_MIN),   # Near zero, still floors
            (0.5, min(_LEARNED_MULTIPLIER_MAX, 0.5 / _BASELINE_P)),   # Mid
            (1.0, _LEARNED_MULTIPLIER_MAX),    # Cap
        ]
        for win_rate, expected_mult in test_cases:
            hints.learned_effectiveness = {"test_pattern": (win_rate, 0.0)}
            patterns = [self._make_dummy_pattern("test_pattern")]
            priorities = _pattern_priorities(patterns, None, round_hints=hints)
            actual = priorities["test_pattern"]
            self.assertAlmostEqual(
                actual, expected_mult,
                places=2,
                msg=f"win_rate={win_rate}: expected priority={expected_mult}, got={actual}",
            )


class TestNoLearnedPriorityFlag(unittest.TestCase):
    """Test --no-learned-priority disables the feature."""

    def test_flag_stored_in_round_hints(self):
        """RoundHints.no_learned_priority defaults to False."""
        hints = RoundHints()
        self.assertFalse(hints.no_learned_priority)

        hints.no_learned_priority = True
        self.assertTrue(hints.no_learned_priority)

    def test_cli_flag_parsed(self):
        """--no-learned-priority is recognized by argparser."""
        from scripts.permuter.hill_climber import parse_args

        with patch("sys.argv", ["prog", "--symbol", "test", "--no-learned-priority"]):
            args = parse_args()
        self.assertTrue(args.no_learned_priority)

    def test_cli_flag_default_false(self):
        """Default is no_learned_priority=False."""
        from scripts.permuter.hill_climber import parse_args

        with patch("sys.argv", ["prog", "--symbol", "test"]):
            args = parse_args()
        self.assertFalse(args.no_learned_priority)


class TestLearnedEffectivenessField(unittest.TestCase):
    """Test the learned_effectiveness field on RoundHints."""

    def test_default_empty(self):
        """learned_effectiveness defaults to empty dict."""
        hints = RoundHints()
        self.assertEqual(hints.learned_effectiveness, {})

    def test_can_store_and_retrieve(self):
        """Can store pattern effectiveness data."""
        hints = RoundHints()
        hints.learned_effectiveness = {
            "pattern_a": (0.5, 1.2),
            "pattern_b": (0.1, 0.3),
        }
        self.assertEqual(len(hints.learned_effectiveness), 2)
        self.assertEqual(hints.learned_effectiveness["pattern_a"], (0.5, 1.2))


if __name__ == "__main__":
    unittest.main()
