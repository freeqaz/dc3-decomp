"""Tests for the nested_goto_skip_to_ifelse pattern.

Verifies the pattern correctly merges chained-if conditions to skip past a goto
whose target label sits at an outer scope, and refuses unsafe cases.

Usage:
    python -m pytest scripts/permuter/tests/test_nested_goto_skip_to_ifelse.py -x -q
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
    pat = get_pattern("nested_goto_skip_to_ifelse")
    ctx = make_context(source, func_name, _empty_diag())
    return list(pat.generate(ctx))


def _first_variant_source(source: str, func_name: str = "test_func") -> str:
    variants = _variants(source, func_name)
    assert variants, "expected at least one variant, got none"
    return variants[0].source.decode("utf-8")


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("nested_goto_skip_to_ifelse")
        self.assertEqual(pat.name, "nested_goto_skip_to_ifelse")
        self.assertEqual(pat.structural_domain, "control_flow")


class TestShapeA(unittest.TestCase):
    """POST stmts inside the outer if's body — wrap them in `if (!inner||...)`."""

    def test_overshell_update_state_shape(self):
        src = """\
void test_func(int state, bool local, bool tev) {
    if (state == 1) {
        if (local) {
            if (!tev)
                goto next;
        }
        ShowState(7);
    }
next:
    after();
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertIn("ShowState(7)", norm)
        self.assertNotIn("goto", norm)
        self.assertNotIn("next:", norm)
        # Merged condition: !local || tev
        # Pattern emits (!local) || (tev) but normalized may collapse spacing.
        self.assertTrue(
            "!local" in norm and "tev" in norm and "||" in norm,
            f"Expected merged !local || tev pattern in output: {norm}"
        )


class TestShapeB(unittest.TestCase):
    """No POST in outer body; statements between outer if and label instead."""

    def test_track_watcher_check_for_rolls_shape(self):
        src = """\
void test_func(int a, int b, int c) {
    if (a == -1 || b <= a) {
        if (b == c)
            goto ok;
    }
    int x = some();
    if (foo()) bar();
ok:
    other();
}
"""
        out = _first_variant_source(src)
        norm = normalize(out)
        self.assertNotIn("goto", norm)
        self.assertNotIn("ok:", norm)
        # The merged condition wraps with !(...) for the outer (parenthesized
        # ||-expr) and inverts the inner comparison directly.
        self.assertIn("!(a == -1", norm)
        self.assertIn("b != c", norm)


class TestMultipleChainedGotos(unittest.TestCase):
    """Each chained-label gets its own variant."""

    def test_chained_labels_each_get_variant(self):
        src = """\
void test_func(int s, bool l, bool t) {
    if (s == 1) {
        if (l) {
            if (!t) goto L1;
        }
        ShowA();
    }
L1:
    if (s == 2) {
        if (l) {
            if (!t) goto L2;
        }
        ShowB();
    }
L2:
    after();
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 2)
        descriptions = " ".join(v.description for v in variants)
        self.assertIn("L1", descriptions)
        self.assertIn("L2", descriptions)


class TestNegativeCases(unittest.TestCase):
    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_multiple_gotos_to_same_label(self):
        self.assertNoVariants("""\
void test_func(int a, int b) {
    if (a) {
        if (b) goto L;
    }
    if (a) goto L;
    foo();
L:
    bar();
}
""")

    def test_outer_if_has_else_clause(self):
        # The pattern requires no `else` on the outer if.
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) {
        if (other) goto L;
    } else {
        foo();
    }
    bar();
L:
    baz();
}
""")

    def test_intermediate_label_in_between(self):
        # Another labeled_statement appears as a sibling between outer if and L.
        self.assertNoVariants("""\
void test_func() {
    if (cond) {
        if (other) goto L;
    }
M:
    foo();
L:
    bar();
}
""")

    def test_no_chain_just_bare_goto(self):
        # A goto with NO enclosing if is not a nested chain; refuse.
        self.assertNoVariants("""\
void test_func() {
    goto L;
    foo();
L:
    bar();
}
""")


if __name__ == "__main__":
    unittest.main()
