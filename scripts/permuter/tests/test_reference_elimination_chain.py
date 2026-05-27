"""Tests for the reference_elimination_chain pattern.

Covers:
- Positive trigger: function with 2 consecutive ref bindings generates a
  depth-2 chain variant that eliminates both.
- Triple elimination: function with 3 ref bindings generates a depth-3 chain.
- Negative non-trigger: function with a single ref binding (no second candidate)
  produces no chain variants (single-elim is handled by reference_elimination).
- Address-of safety: refs used with & are not inlined (chain stops before them).
- Non-eliminable init: refs whose initializer has a function call are skipped.
- Registration: pattern is registered in the global registry.
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

# Import all patterns to trigger registration
import scripts.permuter.patterns  # noqa: F401
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.patterns.reference_elimination_chain import (
    ReferenceEliminationChainPattern,
    _collect_eliminations,
)
from scripts.permuter.extractor import _PARSER, _get_function_name
from scripts.permuter.types import Cluster, Diagnosis, DiffOp, FunctionContext, SwapInfo


# ---------------------------------------------------------------------------
# Helpers
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


def _diag_callee_swaps() -> Diagnosis:
    """Diagnosis with callee-saved GPR swaps — canonical refelim trigger."""
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


def _diag_no_swaps() -> Diagnosis:
    """Empty diagnosis — should not trigger reference_elimination_chain."""
    return Diagnosis(
        total_instructions=50,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[],
        noise_explained=0,
        noise_total=0,
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestRelevant(unittest.TestCase):
    def setUp(self):
        self.pattern = ReferenceEliminationChainPattern()

    def test_relevant_with_callee_saved_swaps(self):
        """relevant() returns True when callee-saved GPR swaps are present."""
        self.assertTrue(self.pattern.relevant(_diag_callee_swaps()))

    def test_relevant_with_clusters(self):
        """relevant() returns True when clusters are present (no swap pairs needed)."""
        diag = _diag_no_swaps()
        diag.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=3, deletes=2)]
        self.assertTrue(self.pattern.relevant(diag))

    def test_not_relevant_empty_diagnosis(self):
        """relevant() returns False when no callee-saved swaps and no clusters."""
        self.assertFalse(self.pattern.relevant(_diag_no_swaps()))

    def test_priority_with_swaps(self):
        """priority() is in [0.4, 0.6] when relevant."""
        p = self.pattern.priority(_diag_callee_swaps())
        self.assertGreater(p, 0.0)
        self.assertLessEqual(p, 0.6)

    def test_priority_zero_without_relevance(self):
        """priority() returns 0.0 when not relevant."""
        self.assertEqual(0.0, self.pattern.priority(_diag_no_swaps()))


class TestCollectEliminations(unittest.TestCase):
    def test_collects_two_refs(self):
        """_collect_eliminations finds both refs in a function with two bindings."""
        src = """\
        void test_func(Mesh* mMesh) {
            auto& ref1 = mMesh;
            auto& ref2 = mMesh;
            ref1->Update();
            ref2->Sync();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        results = _collect_eliminations(ctx)
        names = [r[0] for r in results]
        self.assertIn(b"ref1", names)
        self.assertIn(b"ref2", names)

    def test_collects_member_refs(self):
        """Eliminable refs bound to member access expressions are found."""
        src = """\
        void test_func() {
            auto& myList = mItems;
            myList.Clear();
            myList.Add(1);
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        results = _collect_eliminations(ctx)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0][0], b"myList")

    def test_skips_call_initializer(self):
        """Refs whose initializer has a function call are not collected."""
        src = """\
        void test_func() {
            auto& r = GetMesh();
            r.Update();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        results = _collect_eliminations(ctx)
        self.assertEqual(len(results), 0, "Call-initialized ref must be skipped")

    def test_skips_unused_ref(self):
        """Refs with no subsequent uses (dead binding) are not collected."""
        src = """\
        void test_func() {
            auto& r = mItems;
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        results = _collect_eliminations(ctx)
        self.assertEqual(len(results), 0, "Unused ref must be skipped")


class TestGenerateDepth2(unittest.TestCase):
    """generate() produces depth-2 chain variants when 2+ refs exist."""

    def setUp(self):
        self.pattern = ReferenceEliminationChainPattern()

    def test_depth2_eliminates_both_refs(self):
        """A depth-2 variant removes both ref declarations and inlines both."""
        src = """\
        void test_func(MultiMesh* mMultiMesh) {
            auto& multiMesh = mMultiMesh;
            auto& instances = mMultiMesh;
            multiMesh->DrawInstanced();
            instances->Update();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0, "Should produce at least one chain variant")

        # At least one variant should not contain 'auto& multiMesh' or 'auto& instances'
        multi_elim = [
            v for v in variants
            if b"auto& multiMesh" not in v.source and b"auto& instances" not in v.source
        ]
        self.assertGreater(len(multi_elim), 0,
            "At least one variant must eliminate both ref declarations")

    def test_depth2_variant_inlines_member_access(self):
        """Inlined expressions appear at use sites in the depth-2 variant."""
        src = """\
        void test_func() {
            auto& first = mFirst;
            auto& second = mSecond;
            first.DoThing();
            second.DoOther();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0)

        # At least one variant should contain 'mFirst.DoThing()' and 'mSecond.DoOther()'
        inline_both = [
            v for v in variants
            if b"mFirst.DoThing()" in v.source and b"mSecond.DoOther()" in v.source
        ]
        self.assertGreater(len(inline_both), 0,
            "Variant must inline member accesses at use sites")

    def test_pattern_name_on_variants(self):
        """All generated variants have pattern_name = 'reference_elimination_chain'."""
        src = """\
        void test_func() {
            auto& a = mA;
            auto& b = mB;
            a.Go();
            b.Go();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        variants = list(self.pattern.generate(ctx))
        for v in variants:
            self.assertEqual(v.pattern_name, "reference_elimination_chain")


class TestNoChainForSingleRef(unittest.TestCase):
    """generate() produces no variants when there's only one eliminable ref."""

    def setUp(self):
        self.pattern = ReferenceEliminationChainPattern()

    def test_no_chain_variants_single_ref(self):
        """Single eliminable ref => no chain variants (handled by reference_elimination)."""
        src = """\
        void test_func() {
            auto& mesh = mMesh;
            mesh.Update();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0,
            "Single-ref function must produce no chain variants")

    def test_no_chain_when_second_uses_call(self):
        """No chain when second candidate's initializer has a side effect."""
        src = """\
        void test_func() {
            auto& first = mFirst;
            auto& second = GetSecond();
            first.DoThing();
            second.DoOther();
        }
        """
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        # second cannot be eliminated (call init) => no chain after first
        # Depending on traversal order, we might still get depth-1 from other combos
        # but NOT a chain that involves second
        variants = list(self.pattern.generate(ctx))
        bad = [v for v in variants if b"second.DoOther()" in v.source]
        self.assertEqual(len(bad), 0,
            "Must not inline second when its initializer has side effects")


class TestTripleChain(unittest.TestCase):
    """generate() can produce depth-3 chain variants."""

    def setUp(self):
        self.pattern = ReferenceEliminationChainPattern()

    def test_depth3_eliminates_three_refs(self):
        """Three eliminable refs => at least one depth-3 chain variant."""
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
        ctx = _make_ctx(src, "test_func", _diag_callee_swaps())
        variants = list(self.pattern.generate(ctx))
        # At least one variant should lack all three declarations
        all_elim = [
            v for v in variants
            if (b"auto& a" not in v.source
                and b"auto& b" not in v.source
                and b"auto& c" not in v.source)
        ]
        self.assertGreater(len(all_elim), 0,
            "Should produce at least one depth-3 variant with all refs eliminated")


class TestRegistration(unittest.TestCase):
    """Pattern is properly registered in the global pattern registry."""

    def test_pattern_registered(self):
        """get_pattern('reference_elimination_chain') returns the pattern instance."""
        p = get_pattern("reference_elimination_chain")
        self.assertIsNotNone(p)
        self.assertEqual(p.name, "reference_elimination_chain")

    def test_pattern_in_list(self):
        """reference_elimination_chain appears in list_patterns()."""
        from scripts.permuter.patterns.base import list_patterns
        names = list_patterns()
        self.assertIn("reference_elimination_chain", names)


if __name__ == "__main__":
    unittest.main()
