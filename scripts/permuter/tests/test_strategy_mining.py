"""Tests for strategy DB mining from permuter_cache.db pattern_runs."""

import json
import shutil
import sqlite3
import tempfile
import unittest
from pathlib import Path

from scripts.permuter.strategy_db import (
    StrategyDB,
    apply_strategy_boosts,
    bulk_load_from_pattern_runs,
    unit_category,
)
from scripts.permuter.types import RoundHints


def _create_cache_db(db_path: Path, rows: list[dict]) -> None:
    """Create a minimal permuter_cache.db with pattern_runs data."""
    conn = sqlite3.connect(str(db_path))
    conn.execute("""
        CREATE TABLE IF NOT EXISTS pattern_runs (
            id                  INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp           REAL NOT NULL,
            symbol              TEXT NOT NULL,
            function_name       TEXT,
            source_path         TEXT,
            pattern             TEXT NOT NULL,
            variants_generated  INTEGER NOT NULL DEFAULT 0,
            variants_built      INTEGER NOT NULL DEFAULT 0,
            build_failures      INTEGER NOT NULL DEFAULT 0,
            won                 INTEGER NOT NULL DEFAULT 0,
            best_delta          REAL NOT NULL DEFAULT 0,
            best_variant        TEXT,
            initial_pct         REAL,
            final_pct           REAL,
            diagnosis_category  TEXT,
            unit                TEXT,
            caller              TEXT NOT NULL DEFAULT 'hill_climber'
        )
    """)
    for row in rows:
        conn.execute(
            "INSERT INTO pattern_runs "
            "(timestamp, symbol, function_name, source_path, "
            "pattern, variants_generated, variants_built, build_failures, "
            "won, best_delta, best_variant, "
            "initial_pct, final_pct, diagnosis_category, unit, caller) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (
                row.get("timestamp", 1000000.0),
                row["symbol"],
                row.get("function_name", "TestFunc"),
                row.get("source_path", "src/test/Test.cpp"),
                row["pattern"],
                row.get("variants_generated", 10),
                row.get("variants_built", 8),
                row.get("build_failures", 2),
                row.get("won", 0),
                row.get("best_delta", 0.0),
                row.get("best_variant"),
                row.get("initial_pct", 80.0),
                row.get("final_pct", 80.0),
                row.get("diagnosis_category"),
                row.get("unit"),
                row.get("caller", "hill_climber"),
            ),
        )
    conn.commit()
    conn.close()


class TestBulkLoadFromPatternRuns(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.cache_path = Path(self.tmpdir) / "permuter_cache.db"
        self.strategy_path = Path(self.tmpdir) / "strategy.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_basic_aggregation(self):
        """Win counts, total counts, avg_delta, and to_100 are correct."""
        _create_cache_db(self.cache_path, [
            # Pattern A in unit system/rndobj, regswap diag: 2 wins, 1 loss
            {"symbol": "sym1", "pattern": "signed_unsigned", "won": 1,
             "best_delta": 5.0, "final_pct": 100.0,
             "diagnosis_category": "regswap", "unit": "default/system/rndobj/Trans"},
            {"symbol": "sym2", "pattern": "signed_unsigned", "won": 1,
             "best_delta": 3.0, "final_pct": 95.0,
             "diagnosis_category": "regswap", "unit": "default/system/rndobj/Mesh"},
            {"symbol": "sym3", "pattern": "signed_unsigned", "won": 0,
             "best_delta": 0.0, "final_pct": 80.0,
             "diagnosis_category": "regswap", "unit": "default/system/rndobj/Mat"},
        ])

        db = StrategyDB(self.strategy_path)
        count = db.bulk_load_from_pattern_runs(self.cache_path)
        self.assertEqual(count, 1)  # One group: (signed_unsigned, system/rndobj, regswap)

        results = db.lookup(min_wins=1)
        self.assertEqual(len(results), 1)
        rec = results[0]
        self.assertEqual(rec.pattern, "signed_unsigned")
        self.assertEqual(rec.unit_category, "system/rndobj")
        self.assertEqual(rec.diagnosis_category, "regswap")
        self.assertEqual(rec.win_count, 2)
        self.assertEqual(rec.total_count, 3)
        self.assertAlmostEqual(rec.avg_delta, (5.0 + 3.0 + 0.0) / 3, places=1)
        self.assertEqual(rec.to_100_count, 1)
        db.close()

    def test_multiple_groups(self):
        """Different (pattern, unit, diag) tuples produce separate records."""
        _create_cache_db(self.cache_path, [
            {"symbol": "s1", "pattern": "comparison_flip", "won": 1,
             "best_delta": 10.0, "final_pct": 100.0,
             "diagnosis_category": "structural", "unit": "system/ui/UIList"},
            {"symbol": "s2", "pattern": "declaration_reorder", "won": 1,
             "best_delta": 2.0, "final_pct": 98.0,
             "diagnosis_category": "regswap", "unit": "system/rndobj/Trans"},
            {"symbol": "s3", "pattern": "comparison_flip", "won": 0,
             "best_delta": 0.0, "final_pct": 85.0,
             "diagnosis_category": "structural", "unit": "system/ui/UIPanel"},
        ])

        db = StrategyDB(self.strategy_path)
        count = db.bulk_load_from_pattern_runs(self.cache_path)
        self.assertEqual(count, 2)  # Two distinct groups

        results = db.lookup(min_wins=0)
        self.assertEqual(len(results), 2)
        patterns = {r.pattern for r in results}
        self.assertIn("comparison_flip", patterns)
        self.assertIn("declaration_reorder", patterns)
        db.close()

    def test_null_diagnosis_becomes_unknown(self):
        """NULL diagnosis_category in pattern_runs maps to 'unknown'."""
        _create_cache_db(self.cache_path, [
            {"symbol": "s1", "pattern": "ternary_swap", "won": 1,
             "best_delta": 4.0, "final_pct": 100.0,
             "diagnosis_category": None, "unit": "system/char/Char"},
        ])

        db = StrategyDB(self.strategy_path)
        count = db.bulk_load_from_pattern_runs(self.cache_path)
        self.assertEqual(count, 1)

        results = db.lookup(min_wins=1)
        self.assertEqual(results[0].diagnosis_category, "unknown")
        db.close()

    def test_null_unit_becomes_unknown(self):
        """NULL unit in pattern_runs maps to 'unknown' category."""
        _create_cache_db(self.cache_path, [
            {"symbol": "s1", "pattern": "float_double_literal", "won": 1,
             "best_delta": 2.0, "final_pct": 100.0,
             "diagnosis_category": "clean", "unit": None},
        ])

        db = StrategyDB(self.strategy_path)
        count = db.bulk_load_from_pattern_runs(self.cache_path)
        self.assertEqual(count, 1)

        results = db.lookup(min_wins=1)
        self.assertEqual(results[0].unit_category, "unknown")
        db.close()

    def test_unit_categorization(self):
        """Unit paths are correctly categorized."""
        self.assertEqual(unit_category("default/system/rndobj/Trans"), "system/rndobj")
        self.assertEqual(unit_category("system/ui/UIList"), "system/ui")
        self.assertEqual(unit_category("lazer/meta_ham/CampaignProgress"), "lazer/meta_ham")
        self.assertEqual(unit_category("system"), "system")
        # Empty string splits to [""], so returns "" (not "unknown")
        self.assertEqual(unit_category(""), "")

    def test_example_symbols_collected(self):
        """Winning symbols are collected as examples (up to 5)."""
        rows = [
            {"symbol": f"winner_{i}", "pattern": "cast_insertion", "won": 1,
             "best_delta": 1.0, "final_pct": 100.0,
             "diagnosis_category": "clean", "unit": "system/rndobj/Foo"}
            for i in range(8)
        ]
        _create_cache_db(self.cache_path, rows)

        db = StrategyDB(self.strategy_path)
        db.bulk_load_from_pattern_runs(self.cache_path)

        results = db.lookup(min_wins=1)
        self.assertEqual(len(results), 1)
        self.assertLessEqual(len(results[0].example_symbols), 5)
        self.assertTrue(all(s.startswith("winner_") for s in results[0].example_symbols))
        db.close()

    def test_to_100_count(self):
        """to_100_count correctly counts rows where final_pct >= 100."""
        _create_cache_db(self.cache_path, [
            {"symbol": "s1", "pattern": "p1", "won": 1,
             "best_delta": 10.0, "final_pct": 100.0,
             "diagnosis_category": "clean", "unit": "u/a"},
            {"symbol": "s2", "pattern": "p1", "won": 1,
             "best_delta": 5.0, "final_pct": 99.5,
             "diagnosis_category": "clean", "unit": "u/a"},
            {"symbol": "s3", "pattern": "p1", "won": 0,
             "best_delta": 0.0, "final_pct": 100.0,
             "diagnosis_category": "clean", "unit": "u/a"},
        ])

        db = StrategyDB(self.strategy_path)
        db.bulk_load_from_pattern_runs(self.cache_path)

        results = db.lookup(min_wins=0)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].to_100_count, 2)  # s1 and s3
        db.close()

    def test_empty_cache_db(self):
        """Empty pattern_runs table returns 0."""
        _create_cache_db(self.cache_path, [])

        db = StrategyDB(self.strategy_path)
        count = db.bulk_load_from_pattern_runs(self.cache_path)
        self.assertEqual(count, 0)
        db.close()

    def test_missing_cache_db(self):
        """Missing cache DB path returns 0."""
        db = StrategyDB(self.strategy_path)
        count = db.bulk_load_from_pattern_runs(Path("/nonexistent/cache.db"))
        self.assertEqual(count, 0)
        db.close()

    def test_cache_db_without_pattern_runs_table(self):
        """Cache DB without pattern_runs table returns 0."""
        # Create an empty SQLite DB (no tables)
        conn = sqlite3.connect(str(self.cache_path))
        conn.execute("CREATE TABLE other_table (id INTEGER)")
        conn.commit()
        conn.close()

        db = StrategyDB(self.strategy_path)
        count = db.bulk_load_from_pattern_runs(self.cache_path)
        self.assertEqual(count, 0)
        db.close()


class TestStandaloneFunction(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.cache_path = Path(self.tmpdir) / "permuter_cache.db"
        self.strategy_path = Path(self.tmpdir) / "strategy.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_bulk_load_standalone(self):
        """The standalone bulk_load_from_pattern_runs() function works end-to-end."""
        _create_cache_db(self.cache_path, [
            {"symbol": "s1", "pattern": "p1", "won": 1,
             "best_delta": 5.0, "final_pct": 100.0,
             "diagnosis_category": "regswap", "unit": "system/ui/UIList"},
            {"symbol": "s2", "pattern": "p2", "won": 1,
             "best_delta": 3.0, "final_pct": 95.0,
             "diagnosis_category": "structural", "unit": "system/rndobj/Mesh"},
        ])

        count = bulk_load_from_pattern_runs(self.cache_path, self.strategy_path)
        self.assertEqual(count, 2)

        # Verify strategy DB is populated
        db = StrategyDB(self.strategy_path)
        stats = db.get_stats()
        self.assertEqual(stats["total_records"], 2)
        db.close()


class TestStrategyBoostsIntegration(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.strategy_path = Path(self.tmpdir) / "strategy.db"
        self.cache_path = Path(self.tmpdir) / "permuter_cache.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_boosts_from_mined_runs(self):
        """Patterns mined from pattern_runs boost RoundHints."""
        # Create cache DB with many wins for a pattern in system/ui
        _create_cache_db(self.cache_path, [
            {"symbol": f"s{i}", "pattern": "comparison_flip", "won": 1,
             "best_delta": 5.0, "final_pct": 100.0,
             "diagnosis_category": "structural", "unit": "system/ui/UIList"}
            for i in range(20)
        ])

        # Mine into strategy DB
        count = bulk_load_from_pattern_runs(self.cache_path, self.strategy_path)
        self.assertGreater(count, 0)

        # Apply boosts
        hints = RoundHints()
        recs = apply_strategy_boosts(
            hints, "system/ui", diag_cat="structural", db_path=self.strategy_path,
        )
        self.assertGreater(len(recs), 0)
        self.assertIn("comparison_flip", hints.atlas_boost_patterns)

    def test_graceful_fallback_no_db(self):
        """apply_strategy_boosts returns empty when DB doesn't exist."""
        hints = RoundHints()
        recs = apply_strategy_boosts(
            hints, "system/ui", db_path=Path("/nonexistent/strategy.db"),
        )
        self.assertEqual(recs, [])
        self.assertEqual(len(hints.atlas_boost_patterns), 0)

    def test_graceful_fallback_empty_db(self):
        """apply_strategy_boosts returns empty when DB is empty."""
        # Create empty strategy DB
        db = StrategyDB(self.strategy_path)
        db.close()

        hints = RoundHints()
        recs = apply_strategy_boosts(
            hints, "system/ui", db_path=self.strategy_path,
        )
        self.assertEqual(recs, [])
        self.assertEqual(len(hints.atlas_boost_patterns), 0)


class TestUpsertIdempotence(unittest.TestCase):
    """Verify bulk_load_from_pattern_runs uses upsert (not duplicate)."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.cache_path = Path(self.tmpdir) / "permuter_cache.db"
        self.strategy_path = Path(self.tmpdir) / "strategy.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_rerun_does_not_duplicate(self):
        """Running bulk_load twice doesn't create duplicate records."""
        _create_cache_db(self.cache_path, [
            {"symbol": "s1", "pattern": "p1", "won": 1,
             "best_delta": 5.0, "final_pct": 100.0,
             "diagnosis_category": "regswap", "unit": "system/rndobj/Trans"},
        ])

        db = StrategyDB(self.strategy_path)
        count1 = db.bulk_load_from_pattern_runs(self.cache_path)
        count2 = db.bulk_load_from_pattern_runs(self.cache_path)

        results = db.lookup(min_wins=0)
        self.assertEqual(len(results), 1)
        self.assertEqual(count1, count2)
        db.close()


if __name__ == "__main__":
    unittest.main()
