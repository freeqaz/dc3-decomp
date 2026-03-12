"""Tests for scan_and_permute result aggregation."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter import scan_and_permute
from scripts.permuter.scan_and_permute import _accumulate_result


class TestAccumulateResult(unittest.TestCase):
    def test_accumulates_shape_and_fact_counts(self):
        stats = {
            "processed": 0,
            "improved": 0,
            "perfect": 0,
            "no_change": 0,
            "errors": 0,
            "total_delta": 0.0,
            "improvements": [],
            "pattern_wins": {},
            "ghidra_batch": None,
            "shape_counts": {},
            "fact_boost_counts": {},
            "fact_suppress_counts": {},
            "il_analyzed_variants": 0,
            "il_unique_buckets": 0,
            "il_duplicate_buckets": 0,
            "il_pattern_metrics": {},
        }
        result = {
            "error": None,
            "delta": 1.25,
            "final": 98.75,
            "winning_rounds": [{
                "pattern": "bool_materialize",
                "delta": 1.25,
                "score": 98.75,
            }],
            "ghidra_stats": None,
            "codegen_shapes": ["signed_positive", "zero_test"],
            "fact_boost_patterns": ["bool_materialize", "signed_unsigned"],
            "fact_suppress_patterns": ["u8_to_unsigned_long"],
            "il_analyzed_variants": 4,
            "il_unique_buckets": 3,
            "il_duplicate_buckets": 1,
            "il_pattern_metrics": {
                "bool_materialize": {
                    "analyzed_variants": 3,
                    "unique_buckets": 2,
                    "duplicate_buckets": 1,
                },
                "signed_unsigned": {
                    "analyzed_variants": 1,
                    "unique_buckets": 1,
                    "duplicate_buckets": 0,
                },
            },
        }

        _accumulate_result(stats, result)

        self.assertEqual(stats["processed"], 1)
        self.assertEqual(stats["improved"], 1)
        self.assertEqual(stats["shape_counts"]["signed_positive"], 1)
        self.assertEqual(stats["shape_counts"]["zero_test"], 1)
        self.assertEqual(stats["fact_boost_counts"]["bool_materialize"], 1)
        self.assertEqual(stats["fact_boost_counts"]["signed_unsigned"], 1)
        self.assertEqual(stats["fact_suppress_counts"]["u8_to_unsigned_long"], 1)
        self.assertEqual(stats["il_analyzed_variants"], 4)
        self.assertEqual(stats["il_unique_buckets"], 3)
        self.assertEqual(stats["il_duplicate_buckets"], 1)
        self.assertEqual(
            stats["il_pattern_metrics"]["bool_materialize"]["duplicate_buckets"], 1
        )
        self.assertEqual(
            stats["il_pattern_metrics"]["signed_unsigned"]["analyzed_variants"], 1
        )
        self.assertEqual(stats["pattern_wins"]["bool_materialize"]["wins"], 1)


class TestStoreImprovementRuns(unittest.TestCase):
    def test_persists_il_pattern_metrics(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db_path = Path(tmpdir) / "permuter_cache.db"
            old_db = scan_and_permute._IMPROVEMENT_DB
            scan_and_permute._IMPROVEMENT_DB = db_path
            try:
                scan_and_permute._store_improvement_runs([
                    {
                        "symbol": "?Test@@YAXXZ",
                        "function": "Test",
                        "source": "src/test.cpp",
                        "unit": "src/test.cpp",
                        "initial": 90.0,
                        "final": 91.5,
                        "delta": 1.5,
                        "winning_rounds": [],
                        "stopped_reason": "depth_exhausted",
                        "elapsed": 1.0,
                        "il_analyzed_variants": 6,
                        "il_unique_buckets": 2,
                        "il_duplicate_buckets": 2,
                        "il_pattern_metrics": {
                            "temp_elimination": {
                                "analyzed_variants": 4,
                                "unique_buckets": 1,
                                "duplicate_buckets": 2,
                            },
                        },
                    },
                ])

                import sqlite3

                conn = sqlite3.connect(str(db_path))
                cols = {
                    row[1]
                    for row in conn.execute("PRAGMA table_info(improvement_runs)").fetchall()
                }
                self.assertIn("il_pattern_metrics", cols)
                row = conn.execute(
                    "SELECT il_pattern_metrics, il_analyzed_variants, "
                    "il_unique_buckets, il_duplicate_buckets "
                    "FROM improvement_runs"
                ).fetchone()
                conn.close()

                self.assertEqual(row[1], 6)
                self.assertEqual(row[2], 2)
                self.assertEqual(row[3], 2)
                self.assertIn("temp_elimination", row[0])
            finally:
                scan_and_permute._IMPROVEMENT_DB = old_db


if __name__ == "__main__":
    unittest.main()
