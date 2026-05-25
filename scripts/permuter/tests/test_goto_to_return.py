"""Tests for the goto_to_return pattern.

Verifies the pattern correctly substitutes a `goto L;` with the return statement
at label L (when L's body is exactly `return [EXPR];` and at function body level)
and refuses unsafe cases.

Usage:
    python -m pytest scripts/permuter/tests/test_goto_to_return.py -x -q
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
    pat = get_pattern("goto_to_return")
    ctx = make_context(source, func_name, _empty_diag())
    return list(pat.generate(ctx))


def _first_variant_source(source: str, func_name: str = "test_func") -> str:
    variants = _variants(source, func_name)
    assert variants, "expected at least one variant, got none"
    return variants[0].source.decode("utf-8")


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("goto_to_return")
        self.assertEqual(pat.name, "goto_to_return")
        self.assertEqual(pat.structural_domain, "control_flow")
        self.assertEqual(pat.safety_tier, "conservative")


class TestPositiveRewrites(unittest.TestCase):
    def test_return_expr_with_local(self):
        # NextSongPanel::Exiting shape — return locals declared at fn-body scope.
        src = """\
bool test_func(int cond, int other) {
    bool ret = false;
    if (!cond) {
        if (!other)
            goto done;
    }
    ret = true;
done:
    return ret;
}
"""
        expected = """\
bool test_func(int cond, int other) {
    bool ret = false;
    if (!cond) {
        if (!other)
            return ret;
    }
    ret = true;
    return ret;
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_return_identifier_track_watcher_shape(self):
        # TrackWatcherImpl::ClosestUnplayedGem shape.
        src = """\
int test_func() {
    int idx = compute();
    if (Playable(idx)) {
        if (!Played(idx)) goto oh;
    }
    if (idx + 1 < N) return idx + 1;
oh:
    return idx;
}
"""
        expected = """\
int test_func() {
    int idx = compute();
    if (Playable(idx)) {
        if (!Played(idx)) return idx;
    }
    if (idx + 1 < N) return idx + 1;
    return idx;
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_bare_return(self):
        # `return;` (no expression).
        src = """\
void test_func(int x) {
    if (x) goto end;
    foo();
end:
    return;
}
"""
        expected = """\
void test_func(int x) {
    if (x) return;
    foo();
    return;
}
"""
        self.assertEqual(normalize(_first_variant_source(src)), normalize(expected))

    def test_return_compound_block(self):
        # `L: { return X; }` — the wrap-in-braces form.
        src = """\
int test_func(int x) {
    if (x) goto L;
    foo();
L:
    { return x; }
}
"""
        out = _first_variant_source(src)
        # The goto site gets `return x;`
        self.assertIn("if (x) return x;", out)


class TestNegativeCases(unittest.TestCase):
    def assertNoVariants(self, source: str):
        self.assertEqual(len(_variants(source)), 0)

    def test_multiple_gotos_to_same_label(self):
        self.assertNoVariants("""\
int test_func(int a, int b) {
    if (a) goto L;
    if (b) goto L;
    foo();
L:
    return 0;
}
""")

    def test_label_body_not_a_return(self):
        self.assertNoVariants("""\
void test_func(int cond) {
    if (cond) goto L;
    foo();
L:
    bar();
}
""")

    def test_label_body_multi_statement(self):
        # Even if there's a return, multiple stmts disqualifies.
        self.assertNoVariants("""\
int test_func(int cond) {
    if (cond) goto L;
    foo();
L:
    cleanup();
    return 0;
}
""")

    def test_label_not_at_function_body_level(self):
        # Label is inside an inner block.
        self.assertNoVariants("""\
void test_func(int cond) {
    {
        if (cond) goto L;
        foo();
    L:
        return;
    }
}
""")

    def test_no_gotos(self):
        self.assertNoVariants("""\
int test_func() {
    return 0;
}
""")

    def test_label_with_no_incoming_goto(self):
        # Dead label with a return body — must not fire (zero gotos).
        self.assertNoVariants("""\
int test_func() {
    foo();
L:
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
