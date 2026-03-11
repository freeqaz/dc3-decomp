"""Tests for scan_and_permute result aggregation."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

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
        self.assertEqual(stats["pattern_wins"]["bool_materialize"]["wins"], 1)


if __name__ == "__main__":
    unittest.main()
