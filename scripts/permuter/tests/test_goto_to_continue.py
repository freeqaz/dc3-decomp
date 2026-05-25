"""Tests for the goto_to_continue pattern.

Verifies the pattern correctly replaces `goto L;` with `continue;` when L is an
empty-body label at the end of an enclosing loop body, and refuses unsafe cases.

Usage:
    python -m pytest scripts/permuter/tests/test_goto_to_continue.py -x -q
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
    pat = get_pattern("goto_to_continue")
    ctx = make_context(source, func_name, _empty_diag())
    return list(pat.generate(ctx))


def _first_variant_source(source: str, func_name: str = "test_func") -> str:
    variants = _variants(source, func_name)
    assert variants, "expected at least one variant, got none"
    return variants[0].source.decode("utf-8")


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("goto_to_continue")
        self.assertEqual(pat.name, "goto_to_continue")
        self.assertEqual(pat.structural_domain, "control_flow")
        self.assertEqual(pat.safety_tier, "conservative")


class TestPositiveRewrites(unittest.TestCase):
    def test_for_loop_band_wardrobe_shape(self):
        # BandWardrobe::MostImportantHuman shape.
        src = """\
int test_func() {
    int best = -1;
    for (int i = 0; i < 4; i++) {
        if (info[i].hint == -1) {
            if (best != -1) {
                if (!better(i, best))
                    goto next;
            }
            best = i;
        }
    next:;
    }
    return best;
}
"""
        expected = """\
int test_func() {
    int best = -1;
    for (int i = 0; i < 4; i++) {
        if (info[i].hint == -1) {
            if (best != -1) {
                if (!better(i, best))
                    continue;
            }
            best = i;
        }
    }
    return best;
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_while_loop_body_end_label(self):
        src = """\
void test_func() {
    while (cond) {
        if (skip) goto L;
        do_work();
    L:;
    }
}
"""
        expected = """\
void test_func() {
    while (cond) {
        if (skip) continue;
        do_work();
    }
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_do_while_loop_body_end_label(self):
        src = """\
void test_func() {
    do {
        if (skip) goto L;
        do_work();
    L:;
    } while (cond);
}
"""
        out = _first_variant_source(src)
        self.assertIn("continue", out)
        self.assertNotIn("goto", out)

    def test_multiple_gotos_to_same_loop_end_label(self):
        # All gotos must be replaced when they're all inside the loop.
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        if (a) goto L;
        if (b) goto L;
        do_work();
    L:;
    }
}
"""
        out = _first_variant_source(src)
        # Both gotos become continue; the label is stripped.
        self.assertEqual(out.count("continue"), 2)
        self.assertNotIn("goto", out)


class TestNegativeCases(unittest.TestCase):
    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_goto_outside_the_loop(self):
        # The goto is OUTSIDE the loop containing the end-of-loop label.
        # `continue` from outside the loop would be wrong (or illegal).
        self.assertNoVariants("""\
void test_func() {
    goto L;
    for (int i = 0; i < 4; i++) {
        do_work();
    L:;
    }
}
""")

    def test_label_body_not_empty(self):
        # Label body has real statements; can't replace with continue (we'd lose them).
        self.assertNoVariants("""\
void test_func() {
    for (int i = 0; i < 4; i++) {
        if (skip) goto L;
    L:
        do_work();
    }
}
""")

    def test_label_not_last_statement_of_loop_body(self):
        # Statements follow the label inside the loop body; replacing with
        # continue would lose them.
        self.assertNoVariants("""\
void test_func() {
    for (int i = 0; i < 4; i++) {
        if (skip) goto L;
    L:;
        do_other();
    }
}
""")

    def test_no_enclosing_loop(self):
        # Label is at function-body level; continue would be illegal.
        self.assertNoVariants("""\
void test_func() {
    if (cond) goto L;
    foo();
L:;
}
""")

    def test_no_gotos_to_label(self):
        # Dead label — nothing to rewrite.
        self.assertNoVariants("""\
void test_func() {
    for (int i = 0; i < 4; i++) {
        do_work();
    L:;
    }
}
""")

    def test_mixed_inside_and_outside_gotos(self):
        # If even one goto is outside the loop, refuse — can't safely strip
        # the label without leaving a dangling reference.
        self.assertNoVariants("""\
void test_func() {
    if (early) goto L;
    for (int i = 0; i < 4; i++) {
        if (skip) goto L;
    L:;
    }
}
""")


if __name__ == "__main__":
    unittest.main()
