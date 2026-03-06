"""Skeleton-guided pattern tests (and_split, early_return_merge)."""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.types import Diagnosis, FunctionContext
from scripts.permuter.tests.conftest import (
    diag_with_branch_and_clusters,
    make_context,
)
from scripts.permuter.patterns.base import get_pattern


def _make_ghidra_context(source_text: str, func_name: str,
                         diagnosis: Diagnosis, ghidra_code: str) -> FunctionContext:
    """Build a FunctionContext with ghidra_ast populated from Ghidra code."""
    from scripts.permuter.ghidra_ast import parse_ghidra

    ctx = make_context(source_text, func_name, diagnosis)
    ghidra_ast = parse_ghidra(ghidra_code)
    ctx.ghidra_ast = ghidra_ast
    ctx.ghidra_code = ghidra_code
    return ctx


class TestAndSplitSkeletonGuided(unittest.TestCase):
    """Test that and_split uses CF skeleton when condition_structure is ambiguous."""

    def test_skeleton_nested_ifs_triggers_split(self):
        """Source has &&, Ghidra shows nested ifs -> should try split."""
        source = textwrap.dedent("""\
        void test_func(int *a, int *b) {
            if (a && b) {
                a[0] = b[0];
            }
        }
        """)
        # Ghidra code with both conjunction AND nested_if (ambiguous for
        # condition_structure -> falls through to skeleton).
        # Skeleton: ['if', 'if', 'if'] — consecutive ifs >= 2 -> split
        ghidra_code = textwrap.dedent("""\
        void test_func(int *a, int *b) {
            if (a != 0 && b != 0) {
                if (a != 0) {
                    if (b != 0) {
                        *a = *b;
                    }
                }
            }
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("and_split")
        variants = list(pattern.generate(ctx))
        self.assertGreater(len(variants), 0,
                           "Should produce variants when skeleton shows nested ifs")
        has_ghidra = any("ghidra" in v.name or "ghidra" in v.description.lower()
                         for v in variants)
        self.assertTrue(has_ghidra,
                        "At least one variant should be ghidra-tagged")

    def test_skeleton_no_signal_falls_through(self):
        """When skeleton has no useful signal, should fall through to blind mode."""
        source = textwrap.dedent("""\
        void test_func(int x) {
            if (x > 0) {
                x = x + 1;
            }
        }
        """)
        # Ghidra code also simple — skeleton is just ['if'], no consecutive ifs
        ghidra_code = textwrap.dedent("""\
        void test_func(int x) {
            if (x > 0) {
                x = x + 1;
            }
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("and_split")
        # Should not crash
        variants = list(pattern.generate(ctx))
        # Fine either way — just shouldn't crash


class TestEarlyReturnMergeSkeletonGuided(unittest.TestCase):
    """Test that early_return_merge uses CF skeleton when condition_structure is empty."""

    def test_skeleton_guard_pairs_triggers_split(self):
        """Source has || chain, Ghidra shows guard returns -> should split."""
        source = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0 || b < 0)
                return 0;
            return a + b;
        }
        """)
        # Ghidra with separate guard returns
        ghidra_code = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0)
                return 0;
            if (b < 0)
                return 0;
            return a + b;
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("early_return_merge")
        variants = list(pattern.generate(ctx))
        self.assertGreater(len(variants), 0,
                           "Should produce variants for guard return split")

    def test_skeleton_few_guards_triggers_merge(self):
        """Source has guard returns, Ghidra shows few guards -> skeleton merge."""
        source = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0)
                return 0;
            if (b < 0)
                return 0;
            return a + b;
        }
        """)
        # Ghidra code with NO guard returns, no conjunction/disjunction
        # -> condition_structure returns empty -> skeleton fallback
        # Skeleton: ['return'] (just a return, guard_pairs=0 <=1)
        # source_has_guards=True -> merge
        ghidra_code = textwrap.dedent("""\
        int test_func(int a, int b) {
            int result;
            result = a + b;
            return result;
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("early_return_merge")
        variants = list(pattern.generate(ctx))
        skeleton_variants = [v for v in variants if "skeleton" in v.name]
        self.assertGreater(len(skeleton_variants), 0,
                           "Should produce skeleton-guided merge variants")


class TestAndSplitHelpers(unittest.TestCase):
    """Test the _count_consecutive_ifs and _count_guard_return_pairs helpers."""

    def test_count_consecutive_ifs_basic(self):
        from scripts.permuter.patterns.and_split import _count_consecutive_ifs
        self.assertEqual(_count_consecutive_ifs(["if", "if", "return"]), 2)
        self.assertEqual(_count_consecutive_ifs(["if", "return", "if", "return"]), 1)
        self.assertEqual(_count_consecutive_ifs(["if", "if", "if"]), 3)
        self.assertEqual(_count_consecutive_ifs([]), 0)
        self.assertEqual(_count_consecutive_ifs(["return"]), 0)

    def test_count_guard_return_pairs_basic(self):
        from scripts.permuter.patterns.and_split import _count_guard_return_pairs
        self.assertEqual(_count_guard_return_pairs(["if", "return", "if", "return"]), 2)
        self.assertEqual(_count_guard_return_pairs(["if", "if", "return"]), 1)
        self.assertEqual(_count_guard_return_pairs(["if", "else", "return"]), 0)
        self.assertEqual(_count_guard_return_pairs([]), 0)


if __name__ == "__main__":
    unittest.main()
