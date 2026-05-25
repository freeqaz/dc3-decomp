"""Tests for the common_tail_goto_to_duplicate pattern.

Verifies the pattern correctly rewrites a goto-into-else idiom by duplicating
the shared tail into the inner-if and folding the outer-if's drop-through
into a new inner-if else clause, and refuses unsafe cases.

Usage:
    python -m pytest scripts/permuter/tests/test_common_tail_goto_to_duplicate.py -x -q
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import _empty_diag, make_context, normalize
from scripts.permuter.patterns.base import get_pattern


def _variants(source: str, func_name: str = "test_func") -> list:
    pat = get_pattern("common_tail_goto_to_duplicate")
    ctx = make_context(source, func_name, _empty_diag())
    return list(pat.generate(ctx))


def _first_variant_source(source: str, func_name: str = "test_func") -> str:
    variants = _variants(source, func_name)
    assert variants, "expected at least one variant, got none"
    return variants[0].source.decode("utf-8")


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("common_tail_goto_to_duplicate")
        self.assertEqual(pat.name, "common_tail_goto_to_duplicate")
        self.assertEqual(pat.structural_domain, "control_flow")
        self.assertEqual(pat.safety_tier, "conservative")

    def test_relevant_always_true(self):
        pat = get_pattern("common_tail_goto_to_duplicate")
        self.assertTrue(pat.relevant(_empty_diag()))


class TestPositiveRewrites(unittest.TestCase):
    def test_award_configure_shape(self):
        """The Award::Configure canonical shape: a nested goto-into-else."""
        src = """\
void test_func() {
    if (cat == asset) {
        if (numAssets <= 8) {
            if (has) {
                add_award();
                goto push;
            }
            warn();
        }
    } else {
    push:
        push_back();
    }
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        # No goto anywhere.
        self.assertNotIn("goto push", norm)
        # `push_back()` appears TWICE — once duplicated into the inner-if's
        # true branch and once in the else branch.
        self.assertEqual(norm.count("push_back();"), 2)
        # `warn()` is moved into an else clause of the inner-if.
        self.assertIn("} else {", norm)
        self.assertIn("warn();", norm)

    def test_simple_outer_else_label(self):
        """Minimal shape — one-deep inner if."""
        src = """\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
        }
        drop();
    } else {
    tail:
        shared();
    }
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertNotIn("goto tail", norm)
        # shared() duplicated.
        self.assertEqual(norm.count("shared();"), 2)
        # drop() moved into else of inner if.
        self.assertIn("drop();", norm)
        self.assertIn("} else {", norm)

    def test_no_branch_a_tail_drop(self):
        """The outer-if body ends right after the inner-if (no drop tail)."""
        src = """\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
        }
    } else {
    tail:
        shared();
    }
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertNotIn("goto tail", norm)
        # shared() duplicated even with no drop clause.
        self.assertEqual(norm.count("shared();"), 2)


class TestNegativeCases(unittest.TestCase):
    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_no_else_clause(self):
        # The if-with-label is not the else of anything — refuse.
        self.assertNoVariants("""\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
        }
        drop();
    }
tail:
    shared();
}
""")

    def test_multiple_gotos_to_tail(self):
        self.assertNoVariants("""\
void test_func() {
    if (outer) {
        if (a) goto tail;
        if (b) goto tail;
        drop();
    } else {
    tail:
        shared();
    }
}
""")

    def test_else_first_stmt_not_labeled(self):
        # Else body's first statement is not a labeled_statement.
        self.assertNoVariants("""\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
        }
        drop();
    } else {
        other();
    tail:
        shared();
    }
}
""")

    def test_goto_not_at_end_of_inner_if(self):
        # The goto sits mid-body of the inner-if; pattern refuses.
        self.assertNoVariants("""\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
            extra_dead();
        }
        drop();
    } else {
    tail:
        shared();
    }
}
""")

    def test_inner_if_has_else_already(self):
        # The inner-if already has an else clause — refuse (would clash).
        self.assertNoVariants("""\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
        } else {
            both();
        }
        drop();
    } else {
    tail:
        shared();
    }
}
""")

    def test_branch_a_tail_drop_has_top_level_exit(self):
        # BRANCH_A_TAIL_DROP contains a return — refuse to keep semantics simple.
        self.assertNoVariants("""\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
        }
        drop();
        return;
    } else {
    tail:
        shared();
    }
}
""")

    def test_else_if_chain(self):
        # The alternative is an else-if, not an else block; refuse.
        self.assertNoVariants("""\
void test_func() {
    if (outer) {
        if (inner) {
            goto tail;
        }
        drop();
    } else if (other) {
    tail:
        shared();
    }
}
""")

    def test_no_gotos_at_all(self):
        self.assertNoVariants("""\
void test_func() {
    if (a) {
        foo();
    } else {
        bar();
    }
}
""")


if __name__ == "__main__":
    unittest.main()
