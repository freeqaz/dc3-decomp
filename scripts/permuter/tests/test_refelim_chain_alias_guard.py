"""Tests for the pointer_reuse_alias_guard inside reference_elimination_chain.

The chain pattern can collapse two sibling reference bindings into a single
inlined expression. If those refs originally referred to (and were COMPARED
against) one another, the resulting source contains an always-true comparison
like ``X == X`` — a logic bug. Real-world trigger: BandPatchMesh::FindXfm
on RB3, where:

    WorldXfmFace& endFace = mFaces[count];
    WorldXfmFace* foundFace = ...;
    for (...) if (foundFace == &endFace) break;

would chain-inline to:

    foundFace = &mFaces[count];
    for (...) if (foundFace == foundFace) break;   // always true!

The guard performs three syntactic checks on the post-elimination source:
  1. ``binary_expression`` whose comparison operands are lex-equal.
  2. ``assignment_expression`` whose LHS == RHS.
  3. ``&IDENT`` where IDENT is an eliminated reference name (dangling).

If any trips, the variant is DROPPED and a class-level counter
``ReferenceEliminationChainPattern.dropped_alias_guard`` is incremented.

These tests intentionally also exercise the guard helper directly so the
three checks have independent coverage even when the chain mechanism does
not naturally produce the broken shape.
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

import scripts.permuter.patterns  # noqa: F401
from scripts.permuter.patterns.reference_elimination_chain import (
    ReferenceEliminationChainPattern,
    _pointer_reuse_alias_guard,
)
from scripts.permuter.extractor import _PARSER, _get_function_name
from scripts.permuter.types import Cluster, Diagnosis, FunctionContext, SwapInfo


# ---------------------------------------------------------------------------
# Helpers (mirror test_reference_elimination_chain.py)
# ---------------------------------------------------------------------------

def _make_ctx(source_text: str, func_name: str, diagnosis: Diagnosis) -> FunctionContext:
    source_bytes = textwrap.dedent(source_text).encode("utf-8")
    tree = _PARSER.parse(source_bytes)
    for child in tree.root_node.children:
        if child.type != "function_definition":
            continue
        if _get_function_name(child) == func_name:
            body = child.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body")
            return FunctionContext(
                file_path=Path("/dev/null"),
                file_source=source_bytes,
                func_node=child,
                body_node=body,
                statements=list(body.named_children),
                func_byte_range=(child.start_byte, child.end_byte),
                diagnosis=diagnosis,
            )
    raise ValueError(f"Function '{func_name}' not found")


def _diag() -> Diagnosis:
    """Canonical refelim trigger: callee-saved swaps + a cluster."""
    return Diagnosis(
        total_instructions=80,
        match_counts={},
        reg_swap_pairs={("r30", "r29"): SwapInfo(count=4, first_idx=10, last_idx=60)},
        offset_deltas={},
        diff_ops=[],
        clusters=[Cluster(start_idx=5, end_idx=20, size=15, inserts=5, deletes=5)],
        noise_explained=0,
        noise_total=0,
    )


class _GuardTestBase(unittest.TestCase):
    def setUp(self):
        # Reset the class-level counter so each test sees a fresh value.
        ReferenceEliminationChainPattern.dropped_alias_guard = 0
        self.pattern = ReferenceEliminationChainPattern()


# ---------------------------------------------------------------------------
# Required tests (1-4 from the task)
# ---------------------------------------------------------------------------

class TestAliasSelfCompareDropped(_GuardTestBase):
    """BandPatchMesh::FindXfm-shaped input must produce ZERO variants."""

    def test_alias_self_compare_dropped(self):
        # Two sibling refs binding to the SAME expression, then compared.
        # Chain inline collapses to `mFaces[count] == mFaces[count]`.
        src = """\
        void test_func(int count) {
            Face& endFace = mFaces[count];
            Face& foundFace = mFaces[count];
            if (foundFace == endFace) { return; }
            foundFace.Do();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(0, len(variants),
            "Self-compare-producing chain must emit zero variants")
        self.assertGreater(ReferenceEliminationChainPattern.dropped_alias_guard, 0,
            "Drop counter must have incremented")


class TestAliasSelfAssignDropped(_GuardTestBase):
    """Self-assignment shape (LHS == RHS in assignment_expression) is dropped."""

    def test_alias_self_assign_dropped(self):
        # Direct guard test: assignment X = X must trip.
        bad = b"void f() { int x = 0; x = x; }"
        self.assertTrue(_pointer_reuse_alias_guard(bad, set()))

    def test_chain_producing_self_assign_dropped(self):
        # Both refs alias to the same lvalue; chain would emit `mA = mA`.
        src = """\
        void test_func() {
            int& a = mA;
            int& b = mA;
            b = a;
        }
        """
        ctx = _make_ctx(src, "test_func", _diag())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(0, len(variants),
            "Chain producing `mA = mA` must emit zero variants")
        self.assertGreater(ReferenceEliminationChainPattern.dropped_alias_guard, 0)


class TestCleanChainPreserved(_GuardTestBase):
    """Chains that do NOT trip any guard still emit variants normally."""

    def test_clean_chain_preserved(self):
        src = """\
        void test_func() {
            auto& first = mFirst;
            auto& second = mSecond;
            first.DoA();
            second.DoB();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0,
            "Clean chain (distinct sources, distinct uses) must emit variants")
        self.assertEqual(0, ReferenceEliminationChainPattern.dropped_alias_guard,
            "Drop counter must remain zero for clean chains")
        # And the inlined output is correct.
        has_inline = any(
            b"mFirst.DoA()" in v.source and b"mSecond.DoB()" in v.source
            for v in variants
        )
        self.assertTrue(has_inline)


class TestDroppedCounterVisible(_GuardTestBase):
    """The dropped_alias_guard counter is observable to callers."""

    def test_dropped_counter_visible(self):
        # Sanity: counter starts at 0 (setUp reset it).
        self.assertEqual(0, ReferenceEliminationChainPattern.dropped_alias_guard)

        # Trigger a drop.
        src = """\
        void test_func() {
            int& a = mShared;
            int& b = mShared;
            if (a == b) return;
        }
        """
        ctx = _make_ctx(src, "test_func", _diag())
        list(self.pattern.generate(ctx))
        self.assertGreater(ReferenceEliminationChainPattern.dropped_alias_guard, 0,
            "Counter must increment on at least one drop")


# ---------------------------------------------------------------------------
# Additional coverage (5+ more tests as required)
# ---------------------------------------------------------------------------

class TestGuardHelperDirect(_GuardTestBase):
    """Direct-call coverage of the three guard checks."""

    def test_identical_subscript_compare_trips(self):
        # `mFoo[i] == mFoo[i]` — legal C++ but compiler-side always-true.
        src = b"int f() { if (mFoo[i] == mFoo[i]) return 1; return 0; }"
        self.assertTrue(_pointer_reuse_alias_guard(src, set()),
            "Subscript self-compare must trip the guard")

    def test_distinct_compare_does_not_trip(self):
        src = b"int f() { if (mFoo[i] == mFoo[j]) return 1; return 0; }"
        self.assertFalse(_pointer_reuse_alias_guard(src, set()))

    def test_assignment_self_trips(self):
        src = b"void f() { int x = 0; x = x; }"
        self.assertTrue(_pointer_reuse_alias_guard(src, set()))

    def test_compound_assign_does_not_trip(self):
        # `x += x` is NOT flagged — only plain `=` LHS==RHS.
        src = b"void f() { int x = 0; x += x; }"
        self.assertFalse(_pointer_reuse_alias_guard(src, set()))

    def test_dangling_address_of_trips(self):
        src = b"int* f() { return &endFace; }"
        self.assertTrue(_pointer_reuse_alias_guard(src, {b"endFace"}),
            "&IDENT where IDENT was eliminated must trip the guard")

    def test_dangling_address_of_partial_name_no_trip(self):
        # `&endFaceSomething` is a different identifier; must NOT trip.
        src = b"int* f() { return &endFaceSomething; }"
        self.assertFalse(_pointer_reuse_alias_guard(src, {b"endFace"}))

    def test_logical_and_does_not_look_like_address_of(self):
        # `a && endFace` contains `&&endFace` after concat? No — there's a
        # space. But guard explicitly skips `&` preceded by another `&`.
        src = b"int f() { return (a && endFace); }"
        self.assertFalse(_pointer_reuse_alias_guard(src, {b"endFace"}))

    def test_normalization_handles_whitespace(self):
        # `mFoo  == mFoo` with extra whitespace inside RHS — still trips.
        src = b"int f() { return ( mFoo  ==   mFoo ) ; }"
        self.assertTrue(_pointer_reuse_alias_guard(src, set()))

    def test_inequality_self_compare_trips(self):
        # `x != x` is always-false; same family of bug.
        src = b"int f() { int x = 0; return x != x; }"
        self.assertTrue(_pointer_reuse_alias_guard(src, set()))

    def test_lt_self_compare_trips(self):
        src = b"int f() { int x = 0; return x < x; }"
        self.assertTrue(_pointer_reuse_alias_guard(src, set()))


class TestPartialChainDepthSurvives(_GuardTestBase):
    """When the depth-3 variant trips but depth-2 is clean, depth-2 survives.

    Builds: a chain of three refs where the first two are distinct and safe,
    and the third (when chained on top) would collapse to a self-compare.
    The depth-2 emit MUST still appear; the depth-3 emit must be dropped.
    """

    def test_depth2_emits_when_depth3_would_drop(self):
        src = """\
        void test_func() {
            auto& a = mA;
            auto& b = mB;
            auto& c = mA;
            a.Use();
            b.Use();
            if (c == a) return;
        }
        """
        ctx = _make_ctx(src, "test_func", _diag())
        variants = list(self.pattern.generate(ctx))
        # We expect at least one variant that does NOT contain an `==`
        # self-compare. The chain producing `mA == mA` should be dropped.
        self.assertGreater(len(variants), 0,
            "At least the safe depth-2 (a+b) variant should survive")
        for v in variants:
            self.assertNotIn(b"mA == mA", v.source,
                "No variant may contain the always-true self-compare")
        # And the drop counter recorded at least one suppression.
        self.assertGreater(ReferenceEliminationChainPattern.dropped_alias_guard, 0,
            "At least one depth variant must have been dropped")


class TestRegressionUnaffectedScenario(_GuardTestBase):
    """A control: known-good multi-ref function still produces variants."""

    def test_three_independent_refs_emit_chain(self):
        src = """\
        void test_func() {
            auto& a = mA;
            auto& b = mB;
            auto& c = mC;
            a.Do();
            b.Do();
            c.Do();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0)
        self.assertEqual(0, ReferenceEliminationChainPattern.dropped_alias_guard)


if __name__ == "__main__":
    unittest.main()
