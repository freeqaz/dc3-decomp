"""Tests for the null_guard_insert pattern.

Regression tests for the May 2026 rewrite that took the pattern from a 93%
compile-failure rate down to 0% on a representative RB3 sample.

The historical failure modes (all reproduced as test cases below):

  a) Variables declared INSIDE the if-body chosen as guards for the OUTER
     condition (`charsArr` declared in body, then `if (charsArr && mChar)`
     emitted — `charsArr` is out of scope at the condition).
  b) Non-pointer identifiers used as guards: references (`mFilename` as
     `String`), singletons accessed with `.` (`TheUI.InTransition()`).
  c) `field_expression` parses both `a->b` and `a.b`; the latter was being
     collected as a `->` dereference and producing bogus guard candidates.
  d) Multi-line MILO_LOG / MILO_ASSERT macro calls wrapped in `if (ptr)`.

Usage:
    python -m pytest scripts/permuter/tests/test_null_guard_insert.py -x -q
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import (
    _empty_diag,
    make_context,
)
from scripts.permuter.patterns.base import get_pattern


def _generate(source: str, func_name: str) -> list:
    """Run null_guard_insert on inline source and return variants."""
    ctx = make_context(source, func_name, _empty_diag())
    ctx.compiler_dialect = "mwcc"
    return list(get_pattern("null_guard_insert").generate(ctx))


def _emitted_descriptions(variants) -> list[str]:
    return [v.description for v in variants]


class NullGuardInsertScopeTests(unittest.TestCase):
    """Variables declared inside the if-body must not be lifted into the
    outer condition — they wouldn't be in scope there."""

    def test_does_not_guard_using_local_declared_in_if_body(self):
        # If we naively wrap with `if (arr && mChar)`, `arr` is out of scope at
        # the condition. The old pattern emitted this — the new one must not.
        src = """
            void test_chars(int *mChar) {
                if (mChar) {
                    int *arr = get_arr();
                    if (arr) {
                        arr->something();
                    }
                }
            }
        """
        variants = _generate(src, "test_chars")
        for v in variants:
            text = v.source.decode()
            # The outer condition must remain a single `mChar` test — no `arr`
            # spliced in front of it.
            self.assertNotIn("arr && mChar", text)
            self.assertNotIn("(arr && mChar)", text)

    def test_does_not_guard_using_nested_inner_local(self):
        src = """
            void use_inner(int cond) {
                if (cond) {
                    int *innerLocal = get_ptr();
                    innerLocal->doit();
                }
            }
        """
        variants = _generate(src, "use_inner")
        for v in variants:
            text = v.source.decode()
            # `innerLocal` must never appear in the outer condition.
            self.assertNotIn("innerLocal && cond", text)


class NullGuardInsertPointerTypingTests(unittest.TestCase):
    """Reject candidates that aren't actually pointers."""

    def test_reference_parameter_not_used_as_guard(self):
        src = """
            void use_ref(Foo &r, int val) {
                if (val) {
                    r.Bar();
                }
            }
        """
        variants = _generate(src, "use_ref")
        for v in variants:
            text = v.source.decode()
            # `r` is a reference — must not appear as a null-check candidate.
            self.assertNotIn("r && val", text)
            self.assertNotIn("if (r)", text)

    def test_dot_access_does_not_imply_pointer(self):
        # `TheUI.InTransition()` is a `.` access — the identifier is a reference
        # or singleton. The pattern must not treat it as a `->` candidate.
        src = """
            void set_state(int s, int mState) {
                if (s != mState) {
                    if (TheUI.InTransition()) {
                        do_thing();
                    }
                }
            }
        """
        variants = _generate(src, "set_state")
        for v in variants:
            text = v.source.decode()
            self.assertNotIn("TheUI && s", text)
            self.assertNotIn("TheUI && mState", text)
            self.assertNotIn("if (TheUI)", text)


class NullGuardInsertWrapTests(unittest.TestCase):
    """The wrap-in-if strategy must avoid multi-line macro statements."""

    def test_does_not_wrap_multi_line_statement(self):
        # MILO_LOG-style calls span multiple source lines; wrapping them in
        # `if (ptr)` produces ugly diffs and was a noisy failure source.
        src = """
            void write_log(Foo *p) {
                LOG(
                    "got %s %s",
                    p->Name(),
                    p->Name()
                );
            }
        """
        variants = _generate(src, "write_log")
        for v in variants:
            text = v.source.decode()
            # We must not have wrapped the multi-line LOG(...) call.
            # Look for the telltale 'if (p)' immediately preceding LOG(.
            self.assertNotIn("if (p)\n    LOG(", text)
            self.assertNotIn("if (p)\n        LOG(", text)


class NullGuardInsertEmissionTests(unittest.TestCase):
    """Confirm the pattern still EMITS reasonable variants for safe shapes."""

    def test_emits_wrap_for_lone_pointer_deref_in_if_body(self):
        src = """
            void apply(Foo *mPtr, int cond) {
                if (cond) {
                    mPtr->Bar();
                }
            }
        """
        variants = _generate(src, "apply")
        # We expect at least one variant referencing mPtr (the only pointer).
        descs = _emitted_descriptions(variants)
        self.assertTrue(
            any("mPtr" in d for d in descs),
            f"Expected an mPtr-related variant, got: {descs!r}",
        )

    def test_does_not_emit_when_no_pointer_candidates(self):
        src = """
            void purely_value(int a, int b) {
                if (a) {
                    b = a + 1;
                }
            }
        """
        variants = _generate(src, "purely_value")
        self.assertEqual(
            variants, [],
            "Expected no variants when no pointers are dereferenced",
        )


class NullGuardInsertRegistrationTests(unittest.TestCase):
    """Pattern is back in default sweeps (opt_in removed)."""

    def test_pattern_not_opt_in(self):
        pat = get_pattern("null_guard_insert")
        self.assertFalse(
            pat.opt_in,
            "null_guard_insert should be in default sweeps after the May 2026 fix",
        )

    def test_relevant_for_delete_clusters(self):
        from scripts.permuter.types import Cluster
        diag = _empty_diag()
        diag.clusters = [Cluster(start_idx=0, end_idx=5, size=6, inserts=0, deletes=4)]
        self.assertTrue(get_pattern("null_guard_insert").relevant(diag))


if __name__ == "__main__":
    unittest.main()
