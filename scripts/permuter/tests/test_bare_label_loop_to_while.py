"""Tests for the bare_label_loop_to_while pattern.

Verifies the pattern correctly rewrites
`goto LBL_CHECK; LBL_BODY: ... LBL_CHECK: ...; if (cond) goto LBL_BODY;`
into `while (true) { ...; if (!cond) break; ... }` and refuses unsafe cases.

Usage:
    python -m pytest scripts/permuter/tests/test_bare_label_loop_to_while.py -x -q
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
    pat = get_pattern("bare_label_loop_to_while")
    ctx = make_context(source, func_name, _empty_diag())
    return list(pat.generate(ctx))


def _first_variant_source(source: str, func_name: str = "test_func") -> str:
    variants = _variants(source, func_name)
    assert variants, "expected at least one variant, got none"
    return variants[0].source.decode("utf-8")


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("bare_label_loop_to_while")
        self.assertEqual(pat.name, "bare_label_loop_to_while")
        self.assertEqual(pat.structural_domain, "control_flow")
        self.assertEqual(pat.safety_tier, "conservative")

    def test_relevant_always_true(self):
        pat = get_pattern("bare_label_loop_to_while")
        self.assertTrue(pat.relevant(_empty_diag()))


class TestPositiveRewrites(unittest.TestCase):
    def test_flagstring_shape(self):
        """The BandWardrobe::FlagString shape — the canonical real-world case."""
        src = """\
void test_func(int flags) {
    const char **ptr;
    char *str;
    char *strptr = str;
    int i5 = 0;
    int i1;
    goto loop_check;
loop_body:
    if (flags & i1) {
        strcpy(strptr, *ptr);
        strptr += strlen(*ptr);
    }
    ptr++;
    i5++;
loop_check:
    i1 = 1 << i5;
    if (i1 <= 0x8000) goto loop_body;
    *strptr = 0;
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertIn("while (true)", norm)
        self.assertNotIn("goto loop_check", norm)
        self.assertNotIn("goto loop_body", norm)
        # Negated cond uses operator inversion: <= -> >
        self.assertIn("if (i1 > 0x8000) break;", norm)
        # Pre-stmt `i1 = 1 << i5;` comes BEFORE the break; body work comes AFTER.
        pre_pos = norm.find("i1 = 1 << i5;")
        break_pos = norm.find("break;")
        body_pos = norm.find("strcpy")
        self.assertLess(pre_pos, break_pos)
        self.assertLess(break_pos, body_pos)
        # The trailing tail (`*strptr = 0;`) is preserved after the loop.
        self.assertIn("*strptr = 0;", norm)

    def test_identifier_condition_wraps_with_negation(self):
        src = """\
void test_func() {
    goto check;
body:
    work();
check:
    cond_compute();
    if (still_going) goto body;
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertIn("while (true)", norm)
        self.assertIn("if (!still_going) break;", norm)

    def test_unary_not_strips_negation(self):
        src = """\
void test_func() {
    goto check;
body:
    work();
check:
    setup();
    if (!done) goto body;
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertIn("if (done) break;", norm)


class TestNegativeCases(unittest.TestCase):
    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_multiple_gotos_to_body_label(self):
        # Refuse when the body label has more than one incoming goto.
        self.assertNoVariants("""\
void test_func() {
    if (early) goto body;
    goto check;
body:
    work();
check:
    setup();
    if (cond) goto body;
}
""")

    def test_multiple_gotos_to_check_label(self):
        # Refuse when the check label has more than one incoming goto.
        self.assertNoVariants("""\
void test_func() {
    goto check;
body:
    work();
    if (extra) goto check;
check:
    setup();
    if (cond) goto body;
}
""")

    def test_no_trailing_if_goto(self):
        # The check section ends without `if (cond) goto body;`.
        self.assertNoVariants("""\
void test_func() {
    goto check;
body:
    work();
check:
    setup();
    done();
}
""")

    def test_intermediate_label(self):
        # An unrelated label between body and check breaks the recognizer.
        self.assertNoVariants("""\
void test_func() {
    goto check;
body:
    work();
other:
    misc();
check:
    setup();
    if (cond) goto body;
}
""")

    def test_no_body_label_after_goto(self):
        # The leading `goto check` is followed by a non-labeled statement.
        self.assertNoVariants("""\
void test_func() {
    goto check;
    work();
check:
    setup();
    if (cond) goto body;
}
""")

    def test_top_level_return_in_pre_stmts(self):
        # Refuse if the check region contains an UNCONDITIONAL top-level return
        # before the trailing if-goto; that would make the body unreachable.
        self.assertNoVariants("""\
void test_func() {
    goto check;
body:
    work();
check:
    setup();
    return;
    if (cond) goto body;
}
""")

    def test_extra_goto_in_body_to_outside(self):
        # Refuse when the body region contains an extra top-level goto to a
        # label outside the loop — converting would change control flow.
        self.assertNoVariants("""\
void test_func() {
    goto check;
body:
    work();
    goto external;
check:
    setup();
    if (cond) goto body;
external:
    cleanup();
}
""")

    def test_no_goto_preceding(self):
        # Plain labels and tail-if-goto, no leading goto — not the idiom.
        self.assertNoVariants("""\
void test_func() {
body:
    work();
check:
    setup();
    if (cond) goto body;
}
""")


if __name__ == "__main__":
    unittest.main()
