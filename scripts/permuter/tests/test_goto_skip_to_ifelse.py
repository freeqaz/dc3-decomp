"""Tests for the goto_skip_to_ifelse pattern.

Pure AST/text-level tests. No build/objdiff. Verifies the pattern correctly
rewrites forward-skip goto idioms and refuses unsafe cases.

Usage:
    python -m pytest scripts/permuter/tests/test_goto_skip_to_ifelse.py -x -q
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import _empty_diag, make_context, normalize
from scripts.permuter.patterns.base import get_pattern


def _variants(source: str, func_name: str = "test_func") -> list:
    pat = get_pattern("goto_skip_to_ifelse")
    ctx = make_context(source, func_name, _empty_diag())
    return list(pat.generate(ctx))


def _first_variant_source(source: str, func_name: str = "test_func") -> str:
    variants = _variants(source, func_name)
    assert variants, "expected at least one variant, got none"
    return variants[0].source.decode("utf-8")


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("goto_skip_to_ifelse")
        self.assertEqual(pat.name, "goto_skip_to_ifelse")
        self.assertEqual(pat.structural_domain, "control_flow")
        self.assertEqual(pat.safety_tier, "conservative")

    def test_relevant_always_true(self):
        pat = get_pattern("goto_skip_to_ifelse")
        self.assertTrue(pat.relevant(_empty_diag()))


class TestPositiveRewrites(unittest.TestCase):
    def test_bare_goto_identifier_condition(self):
        src = """\
void test_func(int cond) {
    if (cond) goto L;
    foo();
    bar();
    L:
    baz();
}
"""
        expected = """\
void test_func(int cond) {
    if (!cond) {
        foo();
        bar();
    }
    baz();
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_compound_goto_body(self):
        src = """\
void test_func(int cond) {
    if (cond) {
        goto L;
    }
    foo();
    L:;
    bar();
}
"""
        expected = """\
void test_func(int cond) {
    if (!cond) {
        foo();
    }
    bar();
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_binary_comparison_inverts_operator(self):
        src = """\
void test_func(int a, int b) {
    if (a > b) goto L;
    foo();
    L:
    bar();
}
"""
        expected = """\
void test_func(int a, int b) {
    if (a <= b) {
        foo();
    }
    bar();
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_unary_not_strips_negation(self):
        src = """\
void test_func(int ok) {
    if (!ok) goto L;
    foo();
    L:
    bar();
}
"""
        expected = """\
void test_func(int ok) {
    if (ok) {
        foo();
    }
    bar();
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_field_access_condition(self):
        src = """\
void test_func(Obj *p) {
    if (p->ready) goto L;
    foo();
    L:
    bar();
}
"""
        out = _first_variant_source(src)
        self.assertIn("if (!p->ready)", out)


class TestNegativeCases(unittest.TestCase):
    """Cases where the pattern must REFUSE to rewrite."""

    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_multiple_gotos_to_same_label(self):
        self.assertNoVariants("""\
void test_func(int a, int b) {
    if (a) goto L;
    if (b) goto L;
    foo();
    L:
    bar();
}
""")

    def test_return_between_goto_and_label(self):
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) goto L;
    return;
    L:
    foo();
}
""")

    def test_empty_between(self):
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) goto L;
    L:
    foo();
}
""")

    def test_if_with_else_clause(self):
        # The goto-if has an alternative; refuse — not the skip-forward shape.
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) goto L; else foo();
    bar();
    L:
    baz();
}
""")

    def test_if_body_has_multiple_stmts(self):
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) {
        foo();
        goto L;
    }
    bar();
    L:
    baz();
}
""")

    def test_label_in_nested_block(self):
        # The label is in a nested scope, not the same compound as the goto.
        # Even C compilers normally reject this (goto into a nested block),
        # but the pattern should defensively refuse.
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) goto L;
    foo();
    {
        L:
        bar();
    }
}
""")

    def test_label_appears_earlier(self):
        # Backward goto — not a forward-skip.
        self.assertNoVariants("""\
void test_func(int cond) {
    L:
    foo();
    if (cond) goto L;
    bar();
}
""")

    def test_intermediate_top_level_label(self):
        # A different labeled_statement between goto and target label means
        # a third party might be jumping in; refuse.
        self.assertNoVariants("""\
void test_func(int cond, int other) {
    if (cond) goto L;
    M:
    foo();
    L:
    bar();
}
""")


class TestNoFalsePositives(unittest.TestCase):
    """Verify the pattern is silent on code that has no qualifying gotos."""

    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_no_gotos_at_all(self):
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) {
        foo();
    }
    bar();
}
""")

    def test_goto_not_in_if(self):
        # A bare goto (not inside an if) doesn't fit the skip-forward idiom.
        self.assertNoVariants("""\
void test_func(int cond) {
    goto L;
    foo();
    L:
    bar();
}
""")


if __name__ == "__main__":
    unittest.main()
