"""Tests for cross-pattern composition mining.

Tests query_composition_effectiveness(), suppress pair identification,
confidence-based priority ranking, and suppressed pair skipping.
"""

import shutil
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.permuter.pattern_stats import query_composition_effectiveness
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
                row.get("symbol", "sym1"),
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


class TestQueryCompositionEffectiveness(unittest.TestCase):
    """Tests for query_composition_effectiveness()."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.db_path = Path(self.tmpdir) / "permuter_cache.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_compose_pair_extraction(self):
        """compose:A+B -> pair (A, B)."""
        _create_cache_db(self.db_path, [
            {"pattern": "compose:variable_extraction+declaration_reorder",
             "won": 1, "best_delta": 5.0},
            {"pattern": "compose:variable_extraction+declaration_reorder",
             "won": 0, "best_delta": 0.0},
        ])
        result = query_composition_effectiveness(self.db_path)
        pair = ("variable_extraction", "declaration_reorder")
        self.assertIn(pair, result)
        self.assertEqual(result[pair]["wins"], 1)
        self.assertEqual(result[pair]["fails"], 1)
        self.assertEqual(result[pair]["total"], 2)
        self.assertAlmostEqual(result[pair]["win_rate"], 0.5)

    def test_chain_pair_extraction_3_stages(self):
        """chain:A+B+C -> pairs (A,B), (A,C), (B,C)."""
        _create_cache_db(self.db_path, [
            {"pattern": "chain:alpha+beta+gamma", "won": 1, "best_delta": 3.0},
        ])
        result = query_composition_effectiveness(self.db_path)
        # All 3 pairwise combos should be present
        self.assertIn(("alpha", "beta"), result)
        self.assertIn(("alpha", "gamma"), result)
        self.assertIn(("beta", "gamma"), result)
        # Each pair should have 1 win from this single chain entry
        for pair in [("alpha", "beta"), ("alpha", "gamma"), ("beta", "gamma")]:
            self.assertEqual(result[pair]["wins"], 1)
            self.assertEqual(result[pair]["total"], 1)

    def test_chain_pair_extraction_2_stages(self):
        """chain:A+B -> pair (A, B) only."""
        _create_cache_db(self.db_path, [
            {"pattern": "chain:foo+bar", "won": 1, "best_delta": 2.0},
        ])
        result = query_composition_effectiveness(self.db_path)
        self.assertIn(("foo", "bar"), result)
        self.assertEqual(len(result), 1)

    def test_avg_delta_only_from_wins(self):
        """avg_delta computed only from winning runs with positive delta."""
        _create_cache_db(self.db_path, [
            {"pattern": "compose:a+b", "won": 1, "best_delta": 4.0},
            {"pattern": "compose:a+b", "won": 1, "best_delta": 6.0},
            {"pattern": "compose:a+b", "won": 0, "best_delta": 0.0},
        ])
        result = query_composition_effectiveness(self.db_path)
        pair = ("a", "b")
        self.assertEqual(result[pair]["wins"], 2)
        self.assertEqual(result[pair]["fails"], 1)
        self.assertAlmostEqual(result[pair]["avg_delta"], 5.0)

    def test_mixed_compose_and_chain(self):
        """Compose and chain entries for same pair are merged."""
        _create_cache_db(self.db_path, [
            {"pattern": "compose:x+y", "won": 1, "best_delta": 3.0},
            {"pattern": "chain:x+y+z", "won": 0, "best_delta": 0.0},
        ])
        result = query_composition_effectiveness(self.db_path)
        # (x, y) appears in both compose:x+y and chain:x+y+z
        pair_xy = ("x", "y")
        self.assertIn(pair_xy, result)
        self.assertEqual(result[pair_xy]["wins"], 1)
        self.assertEqual(result[pair_xy]["fails"], 1)
        self.assertEqual(result[pair_xy]["total"], 2)

    def test_graceful_fallback_missing_db(self):
        """Returns empty dict when DB doesn't exist."""
        result = query_composition_effectiveness(Path("/nonexistent/db.sqlite"))
        self.assertEqual(result, {})

    def test_graceful_fallback_no_table(self):
        """Returns empty dict when pattern_runs table doesn't exist."""
        conn = sqlite3.connect(str(self.db_path))
        conn.execute("CREATE TABLE other_table (id INTEGER)")
        conn.commit()
        conn.close()
        result = query_composition_effectiveness(self.db_path)
        self.assertEqual(result, {})

    def test_graceful_fallback_empty_table(self):
        """Returns empty dict when no compose/chain patterns exist."""
        _create_cache_db(self.db_path, [
            {"pattern": "declaration_reorder", "won": 1, "best_delta": 5.0},
        ])
        result = query_composition_effectiveness(self.db_path)
        self.assertEqual(result, {})


class TestSuppressPairsIdentification(unittest.TestCase):
    """Tests for suppress pair identification (5+ runs, 0 wins)."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.db_path = Path(self.tmpdir) / "permuter_cache.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_suppress_pair_5_runs_0_wins(self):
        """Pairs with 5+ total runs and 0 wins are identified as suppress."""
        rows = [
            {"pattern": "compose:bad_a+bad_b", "won": 0, "best_delta": 0.0,
             "symbol": f"s{i}"}
            for i in range(6)
        ]
        _create_cache_db(self.db_path, rows)

        hints = RoundHints()
        # Use composer's _query_effective_pairs which sets hints.suppress_pairs
        with patch("scripts.permuter.composer._CACHE_DB", self.db_path):
            from scripts.permuter.composer import _query_effective_pairs
            _query_effective_pairs(hints)

        self.assertIn(("bad_a", "bad_b"), hints.suppress_pairs)

    def test_no_suppress_below_5_runs(self):
        """Pairs with fewer than 5 runs are NOT suppressed."""
        rows = [
            {"pattern": "compose:low_a+low_b", "won": 0, "best_delta": 0.0,
             "symbol": f"s{i}"}
            for i in range(4)
        ]
        _create_cache_db(self.db_path, rows)

        hints = RoundHints()
        with patch("scripts.permuter.composer._CACHE_DB", self.db_path):
            from scripts.permuter.composer import _query_effective_pairs
            _query_effective_pairs(hints)

        self.assertNotIn(("low_a", "low_b"), hints.suppress_pairs)

    def test_no_suppress_with_any_win(self):
        """Pairs with any wins are NOT suppressed, even with many failures."""
        rows = [
            {"pattern": "compose:mixed_a+mixed_b", "won": 0, "best_delta": 0.0,
             "symbol": f"s{i}"}
            for i in range(10)
        ]
        rows.append(
            {"pattern": "compose:mixed_a+mixed_b", "won": 1, "best_delta": 2.0,
             "symbol": "winner"}
        )
        _create_cache_db(self.db_path, rows)

        hints = RoundHints()
        with patch("scripts.permuter.composer._CACHE_DB", self.db_path):
            from scripts.permuter.composer import _query_effective_pairs
            _query_effective_pairs(hints)

        self.assertNotIn(("mixed_a", "mixed_b"), hints.suppress_pairs)


class TestConfidenceBasedPriority(unittest.TestCase):
    """Tests that confidence (wins/(wins+fails)) replaces hardcoded 0.6."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.db_path = Path(self.tmpdir) / "permuter_cache.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_confidence_score_in_results(self):
        """Effective pairs return confidence score, not hardcoded 0.6."""
        rows = []
        # 8 wins, 2 fails -> confidence = 8/10 = 0.8
        for i in range(8):
            rows.append({"pattern": "compose:high_a+high_b", "won": 1,
                         "best_delta": 2.0, "symbol": f"w{i}"})
        for i in range(2):
            rows.append({"pattern": "compose:high_a+high_b", "won": 0,
                         "best_delta": 0.0, "symbol": f"l{i}"})
        # 2 wins, 8 fails -> confidence = 2/10 = 0.2
        for i in range(2):
            rows.append({"pattern": "compose:low_a+low_b", "won": 1,
                         "best_delta": 1.0, "symbol": f"w2_{i}"})
        for i in range(8):
            rows.append({"pattern": "compose:low_a+low_b", "won": 0,
                         "best_delta": 0.0, "symbol": f"l2_{i}"})
        _create_cache_db(self.db_path, rows)

        with patch("scripts.permuter.composer._CACHE_DB", self.db_path):
            from scripts.permuter.composer import _query_effective_pairs
            result = _query_effective_pairs(None)

        # Result is list of ((pair), confidence) sorted by confidence desc
        self.assertEqual(len(result), 2)
        # High confidence pair first
        self.assertEqual(result[0][0], ("high_a", "high_b"))
        self.assertAlmostEqual(result[0][1], 0.8)
        # Low confidence pair second
        self.assertEqual(result[1][0], ("low_a", "low_b"))
        self.assertAlmostEqual(result[1][1], 0.2)

    def test_confidence_used_as_priority(self):
        """build_adaptive_chains uses confidence as priority, not 0.6."""
        rows = []
        for i in range(4):
            rows.append({"pattern": "compose:pat_a+pat_b", "won": 1,
                         "best_delta": 3.0, "symbol": f"w{i}"})
        rows.append({"pattern": "compose:pat_a+pat_b", "won": 0,
                      "best_delta": 0.0, "symbol": "l0"})
        _create_cache_db(self.db_path, rows)

        with patch("scripts.permuter.composer._CACHE_DB", self.db_path):
            from scripts.permuter.composer import build_adaptive_chains
            from scripts.permuter.types import ChainSpec

            # Create minimal mock patterns
            class MockPattern:
                def __init__(self, name):
                    self.name = name
                    self.follow_ups = []
                    self.requires_context = []
                def relevant(self, d):
                    return True

            patterns = [MockPattern("pat_a"), MockPattern("pat_b")]
            chains = build_adaptive_chains(
                diagnosis=None,
                patterns=patterns,
                hints=None,
                max_chains=20,
            )
            # Should have a chain for the historical pair
            historical_chains = [
                c for c in chains if "historical" in c.reason
            ]
            if historical_chains:
                # Confidence = 4/(4+1) = 0.8, NOT hardcoded 0.6
                self.assertAlmostEqual(historical_chains[0].priority, 0.8)
                self.assertNotAlmostEqual(historical_chains[0].priority, 0.6)


class TestSuppressedPairsSkipped(unittest.TestCase):
    """Tests that suppressed pairs are skipped during composition."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.db_path = Path(self.tmpdir) / "permuter_cache.db"

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_suppressed_pair_skipped_in_build_adaptive_chains(self):
        """Suppressed pairs are not included in chain output."""
        # Create DB where compose:s_a+s_b has 6 fails and 0 wins
        rows = [
            {"pattern": "compose:s_a+s_b", "won": 0, "best_delta": 0.0,
             "symbol": f"s{i}"}
            for i in range(6)
        ]
        _create_cache_db(self.db_path, rows)

        with patch("scripts.permuter.composer._CACHE_DB", self.db_path):
            from scripts.permuter.composer import build_adaptive_chains

            class MockPattern:
                def __init__(self, name):
                    self.name = name
                    self.follow_ups = []
                    self.requires_context = []
                def relevant(self, d):
                    return True

            patterns = [MockPattern("s_a"), MockPattern("s_b")]
            hints = RoundHints()
            chains = build_adaptive_chains(
                diagnosis=None,
                patterns=patterns,
                hints=hints,
                max_chains=20,
            )
            # The suppressed pair should NOT appear in chains
            historical_chains = [
                c for c in chains
                if "historical" in c.reason and "s_a" in c.reason
            ]
            self.assertEqual(len(historical_chains), 0)
            # And the suppress_pairs should be populated
            self.assertIn(("s_a", "s_b"), hints.suppress_pairs)

    def test_suppressed_pair_skipped_in_get_compose_pairs(self):
        """Suppressed pairs are filtered out of get_compose_pairs output."""
        # Create DB where compose:sup_x+sup_y has many fails, no wins
        rows = [
            {"pattern": "compose:sup_x+sup_y", "won": 0, "best_delta": 0.0,
             "symbol": f"s{i}"}
            for i in range(7)
        ]
        _create_cache_db(self.db_path, rows)

        with patch("scripts.permuter.composer._CACHE_DB", self.db_path):
            from scripts.permuter.composer import get_compose_pairs

            class MockPattern:
                def __init__(self, name):
                    self.name = name
                    self.follow_ups = ["sup_y"] if name == "sup_x" else []
                    self.requires_context = []
                def relevant(self, d):
                    return True

            patterns = [MockPattern("sup_x"), MockPattern("sup_y")]
            hints = RoundHints()
            result = get_compose_pairs(
                diagnosis=None,
                patterns=patterns,
                hints=hints,
            )
            # The suppressed pair should not be in the output
            self.assertNotIn(("sup_x", "sup_y"), result)


class TestRoundHintsSuppressPairs(unittest.TestCase):
    """Tests for suppress_pairs field on RoundHints."""

    def test_default_empty(self):
        """suppress_pairs defaults to empty set."""
        hints = RoundHints()
        self.assertEqual(hints.suppress_pairs, set())

    def test_can_add_and_check(self):
        """Can add pairs and check membership."""
        hints = RoundHints()
        hints.suppress_pairs.add(("a", "b"))
        hints.suppress_pairs.add(("c", "d"))
        self.assertIn(("a", "b"), hints.suppress_pairs)
        self.assertIn(("c", "d"), hints.suppress_pairs)
        self.assertNotIn(("b", "a"), hints.suppress_pairs)


if __name__ == "__main__":
    unittest.main()
