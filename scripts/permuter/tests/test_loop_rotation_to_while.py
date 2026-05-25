"""Tests for the loop_rotation_to_while pattern.

Verifies the pattern correctly rewrites `goto check; do { ... check: ... } while (cond);`
into `while (true) { ...; if (!cond) break; ... }` and refuses unsafe cases.

Usage:
    python -m pytest scripts/permuter/tests/test_loop_rotation_to_while.py -x -q
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
    pat = get_pattern("loop_rotation_to_while")
    ctx = make_context(source, func_name, _empty_diag())
    return list(pat.generate(ctx))


def _first_variant_source(source: str, func_name: str = "test_func") -> str:
    variants = _variants(source, func_name)
    assert variants, "expected at least one variant, got none"
    return variants[0].source.decode("utf-8")


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("loop_rotation_to_while")
        self.assertEqual(pat.name, "loop_rotation_to_while")
        self.assertEqual(pat.structural_domain, "control_flow")


class TestPositiveRewrites(unittest.TestCase):
    def test_movie_terminate_shape(self):
        src = """\
void test_func() {
    int count;
    goto check;
    do {
        terminate_one();
    check:
        count = 0;
        for (it = b; it != e; ++it)
            count++;
    } while (count != 0);
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertIn("while (true)", norm)
        self.assertNotIn("goto", norm)
        self.assertIn("if (!(count != 0)) break;", norm)
        # Check section first, then break, then post-iteration work
        check_pos = norm.find("count = 0;")
        break_pos = norm.find("break;")
        post_pos = norm.find("terminate_one()")
        self.assertLess(check_pos, break_pos)
        self.assertLess(break_pos, post_pos)

    def test_patch_sticker_shape_bare_label(self):
        # Label body has its own statement (`next = cur->next;` etc.)
        src = """\
void test_func() {
    int *cur = head;
    int *next;
    goto mip_check;
    do {
        cur = next;
    mip_check:
        next = cur->next;
        if (!next) break;
    } while (true);
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertIn("while (true)", norm)
        self.assertNotIn("goto mip_check", norm)
        # The first statement after the new while should be `next = cur->next;`
        # (the original label body)
        self.assertIn("next = cur->next;", norm)


class TestNegativeCases(unittest.TestCase):
    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_multiple_gotos_to_check_label(self):
        # Refuse if the check label has more than one incoming goto.
        self.assertNoVariants("""\
void test_func() {
    goto check;
    do {
        if (early) goto check;
        work();
    check:
        cond_compute();
    } while (cond);
}
""")

    def test_no_goto_preceding_do_while(self):
        # Without the leading goto, it's a regular do-while, not a rotation.
        self.assertNoVariants("""\
void test_func() {
    do {
        work();
    check:
        cond_compute();
    } while (cond);
}
""")

    def test_goto_target_label_not_in_do_body(self):
        # The goto targets a label OUTSIDE the do-body — different idiom.
        self.assertNoVariants("""\
void test_func() {
    goto skip;
    do {
        work();
    } while (cond);
skip:
    after();
}
""")

    def test_do_without_compound_body(self):
        # `do work(); while (cond);` — no compound body, can't contain a label.
        self.assertNoVariants("""\
void test_func() {
    goto check;
    do work(); while (cond);
}
""")


if __name__ == "__main__":
    unittest.main()
