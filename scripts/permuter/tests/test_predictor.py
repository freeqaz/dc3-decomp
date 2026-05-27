"""Tests for the B4 variant outcome predictor + its history instrumentation.

Covers:
- record_climb stores the new B4 features (diagnosis fingerprint, function
  size, beam depth, per-variant pattern labels) and reads them back.
- Pre-B4 schema records still load (backward-compat migration).
- WinPredictor ranks synthetic feature vectors sensibly.
- rank_and_cull respects the budget gate (no-op under budget, culls over).
- With PERMUTER_PREDICTOR OFF, generate_variants is byte-identical to today.
"""

from __future__ import annotations

import os
import sqlite3
import tempfile
import unittest
from pathlib import Path

from scripts.permuter import climb_history as ch
from scripts.permuter.predictor import (
    VariantFeatures,
    WinPredictor,
    cull_fraction,
    predictor_enabled,
    rank_and_cull,
)
from scripts.permuter.types import VariantOutcome, Variant
from scripts.permuter.generator import generate_variants
from scripts.permuter.tests.conftest import diag_with_gpr_swaps, make_context


_FUNC_SRC = """
void test_func() {
    int value = 0;
    if (value > 0) {
        value = value + 1;
    }
}
"""


class _AlwaysPattern:
    """Minimal pattern that yields N deterministic variants."""

    def __init__(self, name, n):
        self.name = name
        self._n = n
        self.structural_domain = "test"
        self.safety_tier = "normal"

    def relevant(self, diagnosis):
        return True

    def priority(self, diagnosis):
        return 1.0

    def generate(self, ctx):
        for i in range(self._n):
            # Unique marker per (pattern, i) so cross-phase source dedup keeps
            # every variant distinct — we want a predictable queue length.
            marker = f"value = 0; /* {self.name}_{i} */".encode()
            yield Variant(
                name=f"{self.name}_{i}",
                pattern_name=self.name,
                description="t",
                source=ctx.file_source.replace(b"value = 0", marker),
            )


class TestHistoryInstrumentation(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.mkdtemp()
        self._db = Path(self._tmp) / "cache.db"

    def tearDown(self):
        import shutil
        shutil.rmtree(self._tmp, ignore_errors=True)

    def test_records_new_features_and_reads_back(self):
        outcomes = [
            VariantOutcome(pattern_label="ternary_collapse", delta=2.0, won=True),
            VariantOutcome(pattern_label="argument_swap", delta=-1.0, won=False),
        ]
        climb_id = ch.record_climb(
            symbol="?test@@",
            source_md5="abc123",
            pattern_names=["ternary_collapse", "argument_swap"],
            initial_pct=50.0,
            final_pct=52.0,
            stopped_reason="plateau",
            rounds_used=3,
            elapsed_seconds=1.5,
            diag_fingerprint="regswap",
            func_loc=42,
            func_stmts=7,
            beam_depth=3,
            variant_outcomes=outcomes,
            db_path=self._db,
        )
        self.assertIsNotNone(climb_id)

        # climb_history row carries the new feature columns.
        conn = sqlite3.connect(str(self._db))
        row = conn.execute(
            "SELECT diag_fingerprint, func_loc, func_stmts, beam_depth "
            "FROM climb_history WHERE id = ?",
            (climb_id,),
        ).fetchone()
        conn.close()
        self.assertEqual(row, ("regswap", 42, 7, 3))

        # Per-variant rows captured with per-variant labels.
        training = ch.load_variant_training_data(db_path=self._db)
        self.assertEqual(len(training), 2)
        by_label = {r["pattern_label"]: r for r in training}
        self.assertTrue(by_label["ternary_collapse"]["won"])
        self.assertFalse(by_label["argument_swap"]["won"])
        self.assertEqual(by_label["ternary_collapse"]["diag_fingerprint"], "regswap")
        self.assertEqual(by_label["ternary_collapse"]["func_loc"], 42)

    def test_optional_features_default_none(self):
        """Old-style callers (no new kwargs) still record and read back."""
        ch.record_climb(
            symbol="?old@@",
            source_md5="def456",
            pattern_names=["foo"],
            initial_pct=10.0,
            final_pct=10.0,
            stopped_reason="plateau",
            rounds_used=1,
            elapsed_seconds=0.1,
            db_path=self._db,
        )
        conn = sqlite3.connect(str(self._db))
        row = conn.execute(
            "SELECT diag_fingerprint, func_loc, func_stmts, beam_depth "
            "FROM climb_history WHERE symbol = '?old@@'"
        ).fetchone()
        conn.close()
        self.assertEqual(row, (None, None, None, None))
        # No variant outcomes recorded.
        self.assertEqual(ch.load_variant_training_data(db_path=self._db), [])

    def test_interrupted_not_recorded(self):
        self.assertIsNone(ch.record_climb(
            symbol="?x@@", source_md5="m", pattern_names=["p"],
            initial_pct=1.0, final_pct=1.0, stopped_reason="interrupted",
            rounds_used=0, elapsed_seconds=0.0, db_path=self._db,
        ))

    def test_pre_b4_schema_still_loads(self):
        """A DB created with the OLD schema (no B4 columns) is migrated in
        place and old rows remain readable via should_skip()."""
        # Hand-build the pre-B4 climb_history table + one row.
        conn = sqlite3.connect(str(self._db))
        conn.execute("""
            CREATE TABLE climb_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL NOT NULL,
                symbol TEXT NOT NULL,
                source_md5 TEXT NOT NULL,
                patterns_hash TEXT NOT NULL,
                patterns_csv TEXT NOT NULL,
                initial_pct REAL NOT NULL,
                final_pct REAL NOT NULL,
                delta REAL NOT NULL,
                stopped_reason TEXT NOT NULL,
                rounds_used INTEGER NOT NULL,
                elapsed_seconds REAL
            )
        """)
        conn.execute(
            "INSERT INTO climb_history (timestamp, symbol, source_md5, "
            "patterns_hash, patterns_csv, initial_pct, final_pct, delta, "
            "stopped_reason, rounds_used, elapsed_seconds) "
            "VALUES (1.0, '?legacy@@', 'oldmd5', 'h', 'foo', 80.0, 80.0, 0.0, "
            "'plateau', 2, 0.5)",
        )
        conn.commit()
        conn.close()

        # should_skip opens the DB → triggers _migrate → adds B4 columns,
        # and the legacy row is still found (plateau + superset of patterns).
        reason = ch.should_skip("?legacy@@", "oldmd5", ["foo"], db_path=self._db)
        self.assertIsNotNone(reason)
        self.assertIn("plateau", reason)

        # The migrated table now has the B4 columns, NULL for the legacy row.
        conn = sqlite3.connect(str(self._db))
        cols = {r[1] for r in conn.execute("PRAGMA table_info(climb_history)")}
        conn.close()
        self.assertIn("diag_fingerprint", cols)
        self.assertIn("func_loc", cols)
        self.assertIn("beam_depth", cols)

        # And a NEW record now writes the B4 fields into the migrated table.
        ch.record_climb(
            symbol="?new@@", source_md5="newmd5", pattern_names=["bar"],
            initial_pct=1.0, final_pct=2.0, stopped_reason="plateau",
            rounds_used=1, elapsed_seconds=0.1, func_loc=99, db_path=self._db,
        )
        conn = sqlite3.connect(str(self._db))
        loc = conn.execute(
            "SELECT func_loc FROM climb_history WHERE symbol='?new@@'"
        ).fetchone()[0]
        conn.close()
        self.assertEqual(loc, 99)


class TestWinPredictor(unittest.TestCase):
    def test_empty_history_scores_global_prior(self):
        """With no data, every variant scores the same prior (safe no-op)."""
        model = WinPredictor.train([])
        a = model.score(VariantFeatures(pattern_label="x"))
        b = model.score(VariantFeatures(pattern_label="y", diag_fingerprint="regswap"))
        self.assertAlmostEqual(a, b)
        self.assertGreater(a, 0.0)
        self.assertLess(a, 1.0)

    def test_higher_winrate_pattern_scores_higher(self):
        rows = []
        # winner: 8/10 wins; loser: 1/10 wins.
        for i in range(10):
            rows.append({"pattern_label": "winner", "diag_fingerprint": None,
                         "func_loc": 50, "func_stmts": 5, "beam_depth": 2,
                         "delta": 1.0, "won": i < 8})
            rows.append({"pattern_label": "loser", "diag_fingerprint": None,
                         "func_loc": 50, "func_stmts": 5, "beam_depth": 2,
                         "delta": 0.0, "won": i < 1})
        model = WinPredictor.train(rows)
        s_win = model.score(VariantFeatures(pattern_label="winner"))
        s_lose = model.score(VariantFeatures(pattern_label="loser"))
        self.assertGreater(s_win, s_lose)

    def test_diagnosis_specific_cell_dominates(self):
        """A pattern that wins under one diagnosis but not another should
        score higher when that diagnosis is present."""
        rows = []
        for i in range(10):
            rows.append({"pattern_label": "p", "diag_fingerprint": "regswap",
                         "func_loc": 50, "func_stmts": 5, "beam_depth": 2,
                         "delta": 1.0, "won": True})
            rows.append({"pattern_label": "p", "diag_fingerprint": "structural",
                         "func_loc": 50, "func_stmts": 5, "beam_depth": 2,
                         "delta": 0.0, "won": False})
        model = WinPredictor.train(rows)
        s_good = model.score(VariantFeatures(pattern_label="p", diag_fingerprint="regswap"))
        s_bad = model.score(VariantFeatures(pattern_label="p", diag_fingerprint="structural"))
        self.assertGreater(s_good, s_bad)

    def test_size_nudge_bounded(self):
        rows = [{"pattern_label": "p", "diag_fingerprint": None, "func_loc": 100,
                 "func_stmts": 5, "beam_depth": 2, "delta": 1.0, "won": True}
                for _ in range(20)]
        model = WinPredictor.train(rows)
        base = model.score(VariantFeatures(pattern_label="p"))
        big = model.score(VariantFeatures(pattern_label="p", func_loc=10000))
        small = model.score(VariantFeatures(pattern_label="p", func_loc=2))
        self.assertGreater(big, base)
        self.assertLess(small, base)
        # Nudge stays within the ±10% envelope.
        self.assertLessEqual(big, base * 1.11)
        self.assertGreaterEqual(small, base * 0.89)

    def test_from_history_trains_without_crashing(self):
        """Mechanism check: train directly off a (small) climb_history DB."""
        tmp = tempfile.mkdtemp()
        try:
            db = Path(tmp) / "c.db"
            ch.record_climb(
                symbol="?s@@", source_md5="m", pattern_names=["p"],
                initial_pct=1.0, final_pct=3.0, stopped_reason="plateau",
                rounds_used=1, elapsed_seconds=0.1, diag_fingerprint="regswap",
                func_loc=30, func_stmts=4, beam_depth=1,
                variant_outcomes=[
                    VariantOutcome(pattern_label="p", delta=2.0, won=True),
                    VariantOutcome(pattern_label="q", delta=0.0, won=False),
                ],
                db_path=db,
            )
            model = WinPredictor.from_history(db_path=db)
            self.assertGreater(
                model.score(VariantFeatures(pattern_label="p")),
                model.score(VariantFeatures(pattern_label="q")),
            )
        finally:
            import shutil
            shutil.rmtree(tmp, ignore_errors=True)


class TestRankAndCull(unittest.TestCase):
    def test_under_budget_is_noop(self):
        items = ["a", "b", "c"]
        model = WinPredictor.train([])
        out = rank_and_cull(items, lambda x: VariantFeatures(pattern_label=x),
                            budget=5, model=model)
        self.assertEqual(out, items)  # same objects, same order

    def test_over_budget_culls_to_keep_count(self):
        # 10 items, budget 4, cull 50% -> keep max(4, round(10*0.5))=5.
        items = list("abcdefghij")
        model = WinPredictor.train([])
        out = rank_and_cull(items, lambda x: VariantFeatures(pattern_label=x),
                            budget=4, model=model, cull_frac=0.5)
        self.assertEqual(len(out), 5)
        # All scores equal (empty model) -> stable order keeps the first 5.
        self.assertEqual(out, list("abcde"))

    def test_over_budget_never_below_budget(self):
        items = list("abcdefghij")
        model = WinPredictor.train([])
        # Cull 90% would keep 1, but budget floor is 6.
        out = rank_and_cull(items, lambda x: VariantFeatures(pattern_label=x),
                            budget=6, model=model, cull_frac=0.9)
        self.assertEqual(len(out), 6)

    def test_ranking_orders_by_score(self):
        rows = []
        for i in range(10):
            rows.append({"pattern_label": "good", "diag_fingerprint": None,
                         "func_loc": 50, "func_stmts": 5, "beam_depth": 2,
                         "delta": 1.0, "won": True})
            rows.append({"pattern_label": "bad", "diag_fingerprint": None,
                         "func_loc": 50, "func_stmts": 5, "beam_depth": 2,
                         "delta": 0.0, "won": False})
        model = WinPredictor.train(rows)
        # 4 items, budget 2, cull 50% -> keep 2. "good" entries should survive.
        items = ["bad", "good", "bad", "good"]
        out = rank_and_cull(items, lambda x: VariantFeatures(pattern_label=x),
                            budget=2, model=model, cull_frac=0.5)
        self.assertEqual(out, ["good", "good"])


class TestGenerateVariantsFlagOff(unittest.TestCase):
    """Flag OFF must be byte-identical to the un-instrumented behaviour."""

    def _run(self, diagnosis):
        ctx = make_context(_FUNC_SRC, "test_func", diagnosis)
        patterns = [_AlwaysPattern("pa", 4), _AlwaysPattern("pb", 4)]
        return [v.source for v in generate_variants(ctx, patterns, max_variants=20)]

    def test_flag_off_default(self):
        self.assertFalse(predictor_enabled())
        sources = self._run(diag_with_gpr_swaps())
        self.assertGreater(len(sources), 0)

    def test_flag_off_matches_impl_directly(self):
        """generate_variants (flag OFF) yields exactly what the impl yields."""
        from scripts.permuter.generator import _generate_variants_impl
        diagnosis = diag_with_gpr_swaps()
        ctx1 = make_context(_FUNC_SRC, "test_func", diagnosis)
        ctx2 = make_context(_FUNC_SRC, "test_func", diagnosis)
        patterns1 = [_AlwaysPattern("pa", 4), _AlwaysPattern("pb", 4)]
        patterns2 = [_AlwaysPattern("pa", 4), _AlwaysPattern("pb", 4)]

        os.environ.pop("PERMUTER_PREDICTOR", None)
        public = [v.source for v in generate_variants(ctx1, patterns1, max_variants=20)]
        impl = [v.source for v in _generate_variants_impl(ctx2, patterns2, max_variants=20)]
        self.assertEqual(public, impl)

    def test_flag_on_under_budget_still_noop_order(self):
        """Flag ON but queue under the (default = max_variants) budget: order
        and contents unchanged."""
        from scripts.permuter.generator import _generate_variants_impl
        diagnosis = diag_with_gpr_swaps()
        ctx1 = make_context(_FUNC_SRC, "test_func", diagnosis)
        ctx2 = make_context(_FUNC_SRC, "test_func", diagnosis)
        patterns1 = [_AlwaysPattern("pa", 4), _AlwaysPattern("pb", 4)]
        patterns2 = [_AlwaysPattern("pa", 4), _AlwaysPattern("pb", 4)]
        try:
            os.environ["PERMUTER_PREDICTOR"] = "1"
            public = [v.source for v in generate_variants(ctx1, patterns1, max_variants=20)]
        finally:
            os.environ.pop("PERMUTER_PREDICTOR", None)
        impl = [v.source for v in _generate_variants_impl(ctx2, patterns2, max_variants=20)]
        self.assertEqual(public, impl)

    def test_flag_on_over_budget_culls(self):
        """Flag ON with a tight PERMUTER_PREDICTOR_BUDGET actually culls."""
        diagnosis = diag_with_gpr_swaps()
        ctx = make_context(_FUNC_SRC, "test_func", diagnosis)
        patterns = [_AlwaysPattern("pa", 6), _AlwaysPattern("pb", 6)]
        try:
            os.environ["PERMUTER_PREDICTOR"] = "1"
            os.environ["PERMUTER_PREDICTOR_BUDGET"] = "4"
            os.environ["PERMUTER_PREDICTOR_CULL"] = "0.5"
            out = list(generate_variants(ctx, patterns, max_variants=20))
        finally:
            for k in ("PERMUTER_PREDICTOR", "PERMUTER_PREDICTOR_BUDGET",
                      "PERMUTER_PREDICTOR_CULL"):
                os.environ.pop(k, None)
        # 12 generated, budget 4, cull 50% -> keep max(4, 6) = 6.
        self.assertEqual(len(out), 6)


class TestEnvFlags(unittest.TestCase):
    def test_cull_fraction_default(self):
        os.environ.pop("PERMUTER_PREDICTOR_CULL", None)
        self.assertEqual(cull_fraction(), 0.5)

    def test_cull_fraction_override_and_clamp(self):
        try:
            os.environ["PERMUTER_PREDICTOR_CULL"] = "0.3"
            self.assertAlmostEqual(cull_fraction(), 0.3)
            os.environ["PERMUTER_PREDICTOR_CULL"] = "5.0"
            self.assertEqual(cull_fraction(), 0.95)  # clamped
            os.environ["PERMUTER_PREDICTOR_CULL"] = "garbage"
            self.assertEqual(cull_fraction(), 0.5)  # falls back
        finally:
            os.environ.pop("PERMUTER_PREDICTOR_CULL", None)


if __name__ == "__main__":
    unittest.main()
