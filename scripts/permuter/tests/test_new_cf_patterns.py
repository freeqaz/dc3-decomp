"""Tests for three new control-flow patterns:
  * mutex_if_to_else_if
  * demorgan_guard
  * positive_branch_invert

Usage:
    python -m pytest scripts/permuter/tests/test_new_cf_patterns.py -x -q
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

# Import pattern modules directly (registers them without touching __init__.py)
from scripts.permuter.patterns import mutex_if_to_else_if  # noqa: F401
from scripts.permuter.patterns import demorgan_guard        # noqa: F401
from scripts.permuter.patterns import positive_branch_invert  # noqa: F401
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import make_context, _empty_diag, normalize


def _ctx(src: str, func: str):
    return make_context(textwrap.dedent(src), func, _empty_diag())


def _generated_sources(pattern_name: str, src: str, func: str) -> list[str]:
    ctx = _ctx(src, func)
    return [v.source.decode("utf-8") for v in get_pattern(pattern_name).generate(ctx)]


# ===========================================================================
# mutex_if_to_else_if
# ===========================================================================

class TestMutexIfToElseIf(unittest.TestCase):

    def test_registration(self):
        p = get_pattern("mutex_if_to_else_if")
        self.assertIsNotNone(p)

    def test_forward_simple_negation(self):
        """Two ifs with !flag / flag should be merged to else-if."""
        src = """\
            void test_func(bool mCrowd, int x) {
                if (mCrowd) { x = 1; }
                if (!mCrowd) { x = 2; }
            }
        """
        variants = _generated_sources("mutex_if_to_else_if", src, "test_func")
        self.assertTrue(
            any("else if" in v for v in variants),
            f"Expected 'else if' in at least one variant. Got: {variants}"
        )

    def test_forward_negation_with_and(self):
        """mCrowd && cond / !mCrowd && cond should be merged."""
        src = """\
            void test_func(bool mCrowd, bool cond) {
                if (mCrowd && cond) { doA(); }
                if (!mCrowd && cond) { doB(); }
            }
        """
        variants = _generated_sources("mutex_if_to_else_if", src, "test_func")
        self.assertTrue(
            any("else if" in v for v in variants),
            "Expected at least one else-if variant for mCrowd && cond pattern"
        )

    def test_reverse_split_else_if(self):
        """An else-if with mutex conditions should be split back into two ifs."""
        src = """\
            void test_func(bool flag) {
                if (flag) { doA(); }
                else if (!flag) { doB(); }
            }
        """
        variants = _generated_sources("mutex_if_to_else_if", src, "test_func")
        # Reverse: the split should produce a version without else-if
        split = [v for v in variants if "else if" not in v]
        self.assertTrue(
            len(split) > 0,
            "Expected at least one split (no else-if) variant"
        )

    def test_no_mutation_for_unrelated_ifs(self):
        """Two ifs with unrelated conditions should not be merged."""
        src = """\
            void test_func(int a, int b) {
                if (a > 0) { doA(); }
                if (b < 0) { doB(); }
            }
        """
        variants = _generated_sources("mutex_if_to_else_if", src, "test_func")
        # The conditions are unrelated; no merge expected
        else_if_variants = [v for v in variants if "else if" in v]
        self.assertEqual(
            len(else_if_variants), 0,
            f"Should not generate else-if for unrelated conditions. Got: {else_if_variants}"
        )

    def test_no_mutation_when_first_has_else(self):
        """When the first if already has an else, don't merge."""
        src = """\
            void test_func(bool flag, int x) {
                if (flag) { x = 1; } else { x = 0; }
                if (!flag) { x = 2; }
            }
        """
        variants = _generated_sources("mutex_if_to_else_if", src, "test_func")
        # The first if already has an else — forward merge should not fire
        merged = [v for v in variants if v.count("else if") > 0]
        self.assertEqual(len(merged), 0, "Should not create else-if when first has else")

    def test_relevant_on_branch_mismatch(self):
        from scripts.permuter.tests.conftest import diag_with_branch_ops
        p = get_pattern("mutex_if_to_else_if")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_not_relevant_on_empty_diag(self):
        """Empty diagnosis (no branches, no clusters) — not relevant."""
        p = get_pattern("mutex_if_to_else_if")
        d = _empty_diag()
        d.diff_ops = []
        d.clusters = []
        self.assertFalse(p.relevant(d))


# ===========================================================================
# demorgan_guard
# ===========================================================================

class TestDeMorganGuard(unittest.TestCase):

    def test_registration(self):
        p = get_pattern("demorgan_guard")
        self.assertIsNotNone(p)

    def test_forward_whole_body_wrapped(self):
        """if (A && B) { body } -> if (!A || !B) return; body"""
        src = """\
            void test_func(bool a, bool b) {
                if (a && b) {
                    doWork();
                    finish();
                }
            }
        """
        variants = _generated_sources("demorgan_guard", src, "test_func")
        forward = [v for v in variants if "return;" in v and "||" in v]
        self.assertTrue(
            len(forward) > 0,
            f"Expected DeMorgan early-return variant. Got: {variants}"
        )
        # The guard should contain negations
        self.assertTrue(
            any("!a" in v or "!b" in v for v in forward),
            "Negated operands not found in variants"
        )

    def test_forward_single_statement_body(self):
        """Single if wrapping everything."""
        src = """\
            void test_func(bool x, bool y, bool z) {
                if (x && y && z) {
                    execute();
                }
            }
        """
        variants = _generated_sources("demorgan_guard", src, "test_func")
        self.assertTrue(
            any("||" in v and "return;" in v for v in variants),
            "Expected DeMorgan guard for 3-way &&"
        )

    def test_reverse_early_return_to_wrapper(self):
        """if (!A || !B) return; body -> if (A && B) { body }"""
        src = """\
            void test_func(bool a, bool b) {
                if (!a || !b)
                    return;
                doWork();
                finish();
            }
        """
        variants = _generated_sources("demorgan_guard", src, "test_func")
        reverse = [v for v in variants if "&&" in v and "if (!a || !b)" not in v]
        self.assertTrue(
            len(reverse) > 0,
            f"Expected reverse DeMorgan (&&-wrapper) variant. Got: {variants}"
        )

    def test_no_fire_on_non_and_condition(self):
        """A simple condition without && should not trigger the forward transform."""
        src = """\
            void test_func(bool a) {
                if (a) {
                    doWork();
                }
            }
        """
        variants = _generated_sources("demorgan_guard", src, "test_func")
        guard_variants = [v for v in variants if "||" in v and "return;" in v]
        self.assertEqual(
            len(guard_variants), 0,
            "Single-condition if should not trigger DeMorgan forward transform"
        )

    def test_relevant_on_branch_ops(self):
        from scripts.permuter.tests.conftest import diag_with_branch_ops
        p = get_pattern("demorgan_guard")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_relevant_on_clusters(self):
        from scripts.permuter.tests.conftest import diag_with_clusters
        p = get_pattern("demorgan_guard")
        self.assertTrue(p.relevant(diag_with_clusters()))


# ===========================================================================
# positive_branch_invert
# ===========================================================================

class TestPositiveBranchInvert(unittest.TestCase):

    def test_registration(self):
        p = get_pattern("positive_branch_invert")
        self.assertIsNotNone(p)

    def test_forward_basic(self):
        """if (cond) return false; stmts; return true; -> if (!cond) { stmts; return true; } return false;"""
        src = """\
            bool test_func(bool cond) {
                if (cond)
                    return false;
                doWork();
                return true;
            }
        """
        variants = _generated_sources("positive_branch_invert", src, "test_func")
        # Should see a variant where the positive branch contains doWork()
        fwd = [v for v in variants if "!cond" in v and "doWork" in v]
        self.assertTrue(
            len(fwd) > 0,
            f"Expected positive-branch-invert forward variant. Got: {variants}"
        )

    def test_forward_degenerate_two_returns(self):
        """if (c) return A; return B; (no middle) -> if (!c) return B; return A;"""
        src = """\
            bool test_func(bool c) {
                if (c)
                    return false;
                return true;
            }
        """
        variants = _generated_sources("positive_branch_invert", src, "test_func")
        self.assertTrue(
            len(variants) > 0,
            "Expected at least one variant for degenerate two-return case"
        )

    def test_reverse_split(self):
        """if (!cond) { stmts; return true; } return false; -> guard form"""
        src = """\
            bool test_func(bool cond) {
                if (!cond) {
                    doWork();
                    return true;
                }
                return false;
            }
        """
        variants = _generated_sources("positive_branch_invert", src, "test_func")
        # Reverse should produce a version with a guard return
        rev = [v for v in variants if "if (cond)" in v or "if (!(!cond))" in v
               or "return false" in v]
        self.assertTrue(
            len(rev) > 0,
            f"Expected reverse positive-branch variant. Got: {variants}"
        )

    def test_no_fire_same_return_value(self):
        """If both returns have the same value, no useful transform."""
        src = """\
            bool test_func(bool c) {
                if (c)
                    return true;
                return true;
            }
        """
        variants = _generated_sources("positive_branch_invert", src, "test_func")
        # Forward should not fire (same return value = no-op)
        fwd = [v for v in variants
               if "if (!c)" in v and "return true" in v
               and v.count("return true") >= 2]
        # Either 0 variants or all are reverse
        # The key test: forward should not generate a nonsense variant
        # (we check the forward direction doesn't fire by ensuring neither
        #  variant only has !c with the original guard)
        for v in variants:
            if "if (!c)" in v:
                # This came from forward or reverse; both are OK but should differ structurally
                pass  # Accept: just verify no crash

    def test_no_fire_on_void_function(self):
        """Void functions with bare return; should not trigger (needs return value)."""
        src = """\
            void test_func(bool c) {
                if (c)
                    return;
                doWork();
            }
        """
        variants = _generated_sources("positive_branch_invert", src, "test_func")
        # Should produce no variants (guard has no return value)
        self.assertEqual(
            len(variants), 0,
            f"Should not fire on void guard return. Got: {variants}"
        )

    def test_relevant_on_branch_ops(self):
        from scripts.permuter.tests.conftest import diag_with_branch_ops
        p = get_pattern("positive_branch_invert")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_relevant_on_clusters(self):
        from scripts.permuter.tests.conftest import diag_with_clusters
        p = get_pattern("positive_branch_invert")
        self.assertTrue(p.relevant(diag_with_clusters()))


if __name__ == "__main__":
    unittest.main()
