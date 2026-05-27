"""Tests for the cross-function strategy database."""

import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

from scripts.permuter.strategy_db import (
    MINED_TO_PERMUTER,
    StrategyDB,
    StrategyRecommendation,
    StrategyRecord,
    _MANUAL_ONLY_PATTERNS,
    apply_strategy_boosts,
    classify_diagnosis_category,
    unit_category,
)
from scripts.permuter.types import RoundHints


class TestUnitCategory(unittest.TestCase):
    def test_default_prefix_stripped(self):
        self.assertEqual(unit_category("default/system/rndobj/Trans"), "system/rndobj")

    def test_no_prefix(self):
        self.assertEqual(unit_category("system/ui/UIList"), "system/ui")

    def test_short_path(self):
        self.assertEqual(unit_category("system"), "system")

    def test_deep_path(self):
        self.assertEqual(unit_category("default/lazer/meta_ham/CampaignProgress"), "lazer/meta_ham")


class TestManualPatternFiltering(unittest.TestCase):
    def test_manual_patterns_excluded_from_mapping(self):
        """Manual-only patterns should not appear in MINED_TO_PERMUTER values."""
        for manual_name in _MANUAL_ONLY_PATTERNS:
            self.assertNotIn(manual_name, MINED_TO_PERMUTER,
                             f"Manual pattern {manual_name!r} should not be in MINED_TO_PERMUTER")

    def test_key_manual_patterns_covered(self):
        """Ensure the major manual patterns are in the filter set."""
        for name in ["scope_change", "header_include_change", "milo_macro",
                      "native_guard", "field_rename", "body_removal"]:
            self.assertIn(name, _MANUAL_ONLY_PATTERNS)


class TestStrategyDB(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.db_path = Path(self.tmpdir) / "test_strategy.db"

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _make_db(self) -> StrategyDB:
        return StrategyDB(self.db_path)

    def test_upsert_and_lookup(self):
        db = self._make_db()
        db.upsert_strategy("signed_unsigned", "system/rndobj", "regswap", 5.0, False, "sym1")
        db.upsert_strategy("signed_unsigned", "system/rndobj", "regswap", 10.0, True, "sym2")
        db.upsert_strategy("comparison_flip", "system/rndobj", "structural", 3.0, True, "sym3")

        results = db.lookup(unit_cat="system/rndobj", min_wins=1)
        self.assertEqual(len(results), 2)

        # signed_unsigned should have 2 wins
        su = [r for r in results if r.pattern == "signed_unsigned"][0]
        self.assertEqual(su.win_count, 2)
        self.assertEqual(su.to_100_count, 1)
        db.close()

    def test_lookup_with_min_wins_filter(self):
        db = self._make_db()
        db.upsert_strategy("ternary_swap", "system/ui", "unknown", 2.0, False)
        db.upsert_strategy("declaration_reorder", "system/ui", "unknown", 5.0, True)
        db.upsert_strategy("declaration_reorder", "system/ui", "unknown", 8.0, True)

        # min_wins=2 should exclude ternary_swap
        results = db.lookup(unit_cat="system/ui", min_wins=2)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].pattern, "declaration_reorder")
        db.close()

    def test_bulk_load_from_mining(self):
        records = [
            {
                "symbol": "sym1", "unit": "default/system/rndobj/Trans",
                "old_pct": 80.0, "new_pct": 100.0, "delta": 20.0,
                "patterns": [
                    {"pattern": "signed_unsigned", "confidence": 0.9},
                    {"pattern": "scope_change", "confidence": 0.8},  # should be filtered
                ]
            },
            {
                "symbol": "sym2", "unit": "default/system/rndobj/Mesh",
                "old_pct": 90.0, "new_pct": 95.0, "delta": 5.0,
                "patterns": [
                    {"pattern": "comparison_flip", "confidence": 0.7},
                ]
            },
        ]

        db = self._make_db()
        count = db.bulk_load_from_mining(records)

        # scope_change should be filtered out, so only 2 records
        self.assertEqual(count, 2)

        results = db.lookup(unit_cat="system/rndobj", min_wins=1)
        patterns = {r.pattern for r in results}
        self.assertIn("signed_unsigned", patterns)
        self.assertIn("comparison_flip", patterns)
        self.assertNotIn("scope_change", patterns)
        db.close()

    def test_recommend_patterns(self):
        db = self._make_db()
        # Add several strategies
        for _ in range(10):
            db.upsert_strategy("comparison_flip", "system/ui", "unknown", 5.0, True)
        for _ in range(3):
            db.upsert_strategy("ternary_swap", "system/ui", "unknown", 2.0, False)

        recs = db.recommend_patterns("system/ui")
        self.assertGreater(len(recs), 0)
        # comparison_flip should rank higher (more wins)
        self.assertEqual(recs[0].pattern, "comparison_flip")
        self.assertGreater(recs[0].priority_boost, 1.0)
        db.close()

    def test_get_stats(self):
        db = self._make_db()
        db.upsert_strategy("p1", "u1", "d1", 5.0, True)
        db.upsert_strategy("p2", "u2", "d1", 3.0, False)

        stats = db.get_stats()
        self.assertEqual(stats["total_records"], 2)
        self.assertEqual(stats["unique_patterns"], 2)
        self.assertEqual(stats["unique_units"], 2)
        db.close()

    def test_empty_db_lookup(self):
        db = self._make_db()
        results = db.lookup(unit_cat="nonexistent")
        self.assertEqual(results, [])
        db.close()


class TestApplyStrategyBoosts(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.db_path = Path(self.tmpdir) / "test_strategy.db"

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_boosts_applied_to_round_hints(self):
        db = StrategyDB(self.db_path)
        for _ in range(20):
            db.upsert_strategy("comparison_flip", "system/rndobj", "unknown", 5.0, True)
        db.close()

        hints = RoundHints()
        recs = apply_strategy_boosts(hints, "system/rndobj", db_path=self.db_path)

        self.assertGreater(len(recs), 0)
        self.assertIn("comparison_flip", hints.atlas_boost_patterns)

    def test_missing_db_returns_empty(self):
        hints = RoundHints()
        recs = apply_strategy_boosts(hints, "system/rndobj", db_path=Path("/nonexistent/db"))
        self.assertEqual(recs, [])
        self.assertEqual(len(hints.atlas_boost_patterns), 0)


class TestDiagnosisCategoryStoredCorrectly(unittest.TestCase):
    """Regression tests for the B1 bug: pattern_runs must record real diag_cat values.

    Before the fix, every record in strategy.db had diagnosis_category='unknown'
    because hill_climber.py never passed the category to store_run, and
    beam_search.py used a non-existent .category attribute.  The boost formula
    requires a matching diag_cat to return non-trivial hit counts, so
    the prioritisation was silently inert.
    """

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.db_path = Path(self.tmpdir) / "test_strategy.db"

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_upsert_with_real_diag_cat_stored_as_given(self):
        """Verify that a record inserted with a real diag_cat is stored verbatim."""
        db = StrategyDB(self.db_path)
        db.upsert_strategy("signed_unsigned", "system/rndobj", "regswap", 5.0, False, "sym1")
        db.close()

        db = StrategyDB(self.db_path)
        results = db.lookup(unit_cat="system/rndobj", diag_cat="regswap", min_wins=1)
        db.close()

        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].diagnosis_category, "regswap",
                         "Expected 'regswap' but got 'unknown' — diag_cat was dropped")

    def test_classify_diagnosis_category_produces_real_cats(self):
        """classify_diagnosis_category() must return concrete categories, not 'unknown'."""
        regswap = classify_diagnosis_category({"has_regswap": True, "has_structural": False,
                                               "has_prologue": False, "has_offset": False})
        self.assertEqual(regswap, "regswap")

        mixed = classify_diagnosis_category({"has_regswap": True, "has_structural": True,
                                             "has_prologue": False, "has_offset": False})
        self.assertEqual(mixed, "mixed")

        clean = classify_diagnosis_category({"has_regswap": False, "has_structural": False,
                                             "has_prologue": False, "has_offset": False})
        self.assertEqual(clean, "clean")

    def test_recommend_patterns_finds_real_diag_cat_not_unknown(self):
        """recommend_patterns must return boost >1.0 when diag_cat matches stored records."""
        db = StrategyDB(self.db_path)
        # Seed enough wins in a specific diag_cat to exceed the boost threshold (1.2)
        for _ in range(20):
            db.upsert_strategy("comparison_flip", "system/rndobj", "regswap", 5.0, True, "sym")
        db.close()

        db = StrategyDB(self.db_path)
        recs = db.recommend_patterns("system/rndobj", diag_cat="regswap")
        db.close()

        self.assertTrue(any(r.pattern == "comparison_flip" for r in recs),
                        "comparison_flip should appear in recommendations")
        cf = next(r for r in recs if r.pattern == "comparison_flip")
        self.assertGreater(cf.priority_boost, 1.2,
                           "boost must exceed 1.2 threshold to activate strategy-DB boosting")

    def test_unknown_diag_cat_does_not_match_real_cat_query(self):
        """Records stored as 'unknown' must NOT appear in a 'regswap' query.

        This is the exact failure mode B1 identified: all records were 'unknown'
        so queries for 'regswap' returned 0 rows and the boost stayed at 1.0.
        """
        db = StrategyDB(self.db_path)
        for _ in range(20):
            # Old behaviour: everything stored as 'unknown'
            db.upsert_strategy("comparison_flip", "system/rndobj", "unknown", 5.0, True, "sym")
        db.close()

        db = StrategyDB(self.db_path)
        recs = db.recommend_patterns("system/rndobj", diag_cat="regswap")
        db.close()

        cf_list = [r for r in recs if r.pattern == "comparison_flip"]
        if cf_list:
            cf = cf_list[0]
            # unit_count drives the boost; it must be 0 when the only records are 'unknown'
            # (the cross-count may be non-zero, but that only gives boost up to ~1.1)
            self.assertLessEqual(cf.priority_boost, 1.2,
                                 "Records stored as 'unknown' should NOT activate the regswap boost")


class TestStrategyRecord(unittest.TestCase):
    def test_win_rate(self):
        r = StrategyRecord("p", "u", "d", 8, 10, 5.0, 6)
        self.assertAlmostEqual(r.win_rate, 0.8)

    def test_to_100_rate(self):
        r = StrategyRecord("p", "u", "d", 8, 10, 5.0, 6)
        self.assertAlmostEqual(r.to_100_rate, 0.6)

    def test_zero_total(self):
        r = StrategyRecord("p", "u", "d", 0, 0, 0.0, 0)
        self.assertAlmostEqual(r.win_rate, 0.0)
        self.assertAlmostEqual(r.to_100_rate, 0.0)


if __name__ == "__main__":
    unittest.main()
