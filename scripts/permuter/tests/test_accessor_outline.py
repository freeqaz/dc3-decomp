"""Tests for accessor_outline pattern."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    _empty_diag,
    diag_with_clusters,
    make_context,
    normalize,
)
from scripts.permuter.types import Cluster, DiffOp, Diagnosis, SwapInfo


# ---------------------------------------------------------------------------
# Diagnosis factories specific to accessor_outline
# ---------------------------------------------------------------------------

def _diag_with_bl_mismatch() -> Diagnosis:
    """Target has bl (function call) that base doesn't — accessor outlined in target."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="bl", base_opcode="lwz")]
    d.clusters = [Cluster(start_idx=8, end_idx=14, size=6, inserts=3, deletes=3)]
    return d


def _diag_with_prologue_mismatch() -> Diagnosis:
    """Prologue mismatch from inlined accessor changing register pressure."""
    d = _empty_diag()
    d.target_gpr_saves = 5
    d.base_gpr_saves = 4
    return d


def _diag_with_replace_real() -> Diagnosis:
    """Real structural replaces."""
    d = _empty_diag()
    d.replace_real = 2
    return d


# ---------------------------------------------------------------------------
# Noinline wrapper generation
# ---------------------------------------------------------------------------

class TestNoinlineWrapperGeneration(unittest.TestCase):
    """Test __declspec(noinline) wrapper generation for accessor calls."""

    def test_basic_accessor_outline(self):
        """Simple no-arg method call should produce a noinline wrapper variant."""
        pattern = get_pattern("accessor_outline")
        ctx = make_context(
            """\
struct Widget { float DisabledAlphaScale(); };
void test_func() {
    Widget* w;
    float a = w->DisabledAlphaScale();
    float b = w->DisabledAlphaScale();
}
""",
            "test_func",
            _diag_with_bl_mismatch(),
        )

        variants = list(pattern.generate(ctx))
        self.assertTrue(len(variants) > 0, "Expected at least one variant")

        # Check that the variant contains a noinline wrapper
        found_wrapper = False
        for v in variants:
            src = v.source.decode("utf-8", errors="replace")
            if "__declspec(noinline)" in src and "_outline_DisabledAlphaScale" in src:
                found_wrapper = True
                break
        self.assertTrue(found_wrapper, "Expected a noinline wrapper for DisabledAlphaScale")

    def test_dot_accessor_outline(self):
        """obj.Method() (dot access) should also be handled."""
        pattern = get_pattern("accessor_outline")
        ctx = make_context(
            """\
struct Obj { int Size(); };
void test_func() {
    Obj obj;
    int a = obj.Size();
    int b = obj.Size();
}
""",
            "test_func",
            _diag_with_bl_mismatch(),
        )

        variants = list(pattern.generate(ctx))
        # Should find Size() as accessor candidate
        wrapper_variants = [
            v for v in variants
            if b"_outline_Size" in v.source
        ]
        self.assertTrue(
            len(wrapper_variants) > 0,
            "Expected a noinline wrapper for Size()",
        )

    def test_skips_calls_with_arguments(self):
        """Calls with arguments should not produce accessor outline variants."""
        pattern = get_pattern("accessor_outline")
        ctx = make_context(
            """\
struct Obj { int GetAt(int i); };
void test_func() {
    Obj* obj;
    int a = obj->GetAt(0);
    int b = obj->GetAt(1);
}
""",
            "test_func",
            _diag_with_bl_mismatch(),
        )

        variants = list(pattern.generate(ctx))
        wrapper_variants = [
            v for v in variants
            if b"_outline_GetAt" in v.source
        ]
        self.assertEqual(
            len(wrapper_variants), 0,
            "Should NOT produce wrapper for calls with arguments",
        )

    def test_skips_stl_methods(self):
        """STL methods (begin, end, size, empty) should be skipped."""
        pattern = get_pattern("accessor_outline")
        ctx = make_context(
            """\
struct Vec { int size(); int begin(); int end(); };
void test_func() {
    Vec v;
    int a = v.size();
    int b = v.begin();
    int c = v.end();
}
""",
            "test_func",
            _diag_with_bl_mismatch(),
        )

        variants = list(pattern.generate(ctx))
        stl_wrappers = [
            v for v in variants
            if b"_outline_size" in v.source
            or b"_outline_begin" in v.source
            or b"_outline_end" in v.source
        ]
        self.assertEqual(
            len(stl_wrappers), 0,
            "Should NOT produce wrappers for STL-style methods",
        )


# ---------------------------------------------------------------------------
# Repeated member access detection
# ---------------------------------------------------------------------------

class TestMemberAccessDetection(unittest.TestCase):
    """Test detection and outlining of repeated direct member accesses."""

    def test_repeated_member_access(self):
        """Repeated ptr->mMember should produce a volatile indirection variant."""
        pattern = get_pattern("accessor_outline")
        ctx = make_context(
            """\
struct Widget { float mAlpha; };
void test_func() {
    Widget* w;
    float a = w->mAlpha;
    float b = w->mAlpha;
}
""",
            "test_func",
            _diag_with_bl_mismatch(),
        )

        variants = list(pattern.generate(ctx))
        member_variants = [
            v for v in variants
            if b"_get_mAlpha" in v.source
        ]
        self.assertTrue(
            len(member_variants) > 0,
            "Expected a getter wrapper for repeated mAlpha access",
        )

    def test_no_variant_for_single_access(self):
        """Single member access should NOT produce a variant."""
        pattern = get_pattern("accessor_outline")
        ctx = make_context(
            """\
struct Widget { float mAlpha; };
void test_func() {
    Widget* w;
    float a = w->mAlpha;
}
""",
            "test_func",
            _diag_with_bl_mismatch(),
        )

        variants = list(pattern.generate(ctx))
        member_variants = [
            v for v in variants
            if b"_get_mAlpha" in v.source
        ]
        self.assertEqual(
            len(member_variants), 0,
            "Should NOT produce variant for single member access",
        )


# ---------------------------------------------------------------------------
# Variant limit
# ---------------------------------------------------------------------------

class TestVariantLimit(unittest.TestCase):
    """Test that variant generation is capped at MAX_VARIANTS."""

    def test_max_5_variants(self):
        """Should produce at most 5 variants even with many candidates."""
        pattern = get_pattern("accessor_outline")
        # Create a source with many different accessor calls
        ctx = make_context(
            """\
struct W {
    float GetA(); float GetB(); float GetC();
    float GetD(); float GetE(); float GetF();
    float GetG(); float GetH();
};
void test_func() {
    W* w;
    w->GetA(); w->GetA();
    w->GetB(); w->GetB();
    w->GetC(); w->GetC();
    w->GetD(); w->GetD();
    w->GetE(); w->GetE();
    w->GetF(); w->GetF();
    w->GetG(); w->GetG();
    w->GetH(); w->GetH();
}
""",
            "test_func",
            _diag_with_bl_mismatch(),
        )

        variants = list(pattern.generate(ctx))
        self.assertLessEqual(
            len(variants), 5,
            f"Should produce at most 5 variants, got {len(variants)}",
        )


# ---------------------------------------------------------------------------
# Relevance tests
# ---------------------------------------------------------------------------

class TestRelevance(unittest.TestCase):
    """Test relevant() method for diagnosis filtering."""

    def test_relevant_with_bl_mismatch(self):
        """Target has bl, base doesn't -> relevant."""
        pattern = get_pattern("accessor_outline")
        diag = _diag_with_bl_mismatch()
        self.assertTrue(pattern.relevant(diag))

    def test_relevant_with_clusters(self):
        """Clusters indicate structural differences -> relevant."""
        pattern = get_pattern("accessor_outline")
        diag = diag_with_clusters()
        self.assertTrue(pattern.relevant(diag))

    def test_relevant_with_prologue_mismatch(self):
        """Prologue mismatch -> relevant."""
        pattern = get_pattern("accessor_outline")
        diag = _diag_with_prologue_mismatch()
        self.assertTrue(pattern.relevant(diag))

    def test_relevant_with_replace_real(self):
        """Real structural replaces -> relevant."""
        pattern = get_pattern("accessor_outline")
        diag = _diag_with_replace_real()
        self.assertTrue(pattern.relevant(diag))

    def test_not_relevant_empty_diagnosis(self):
        """Empty diagnosis -> not relevant."""
        pattern = get_pattern("accessor_outline")
        diag = _empty_diag()
        self.assertFalse(pattern.relevant(diag))

    def test_priority_with_bl_mismatch(self):
        """bl mismatch should give high priority."""
        pattern = get_pattern("accessor_outline")
        diag = _diag_with_bl_mismatch()
        self.assertGreaterEqual(pattern.priority(diag), 0.8)

    def test_priority_with_clusters_only(self):
        """Clusters only should give moderate priority."""
        pattern = get_pattern("accessor_outline")
        diag = diag_with_clusters()
        pri = pattern.priority(diag)
        self.assertGreater(pri, 0.0)
        self.assertLess(pri, 0.8)


# ---------------------------------------------------------------------------
# Pattern metadata and registration
# ---------------------------------------------------------------------------

class TestPatternMetadata(unittest.TestCase):
    """Verify pattern registration and metadata."""

    def test_pattern_registered(self):
        pattern = get_pattern("accessor_outline")
        self.assertEqual(pattern.name, "accessor_outline")

    def test_safety_tier(self):
        pattern = get_pattern("accessor_outline")
        self.assertEqual(pattern.safety_tier, "experimental")

    def test_follow_ups(self):
        pattern = get_pattern("accessor_outline")
        self.assertIn("declaration_reorder", pattern.follow_ups)
        self.assertIn("value_address_caching", pattern.follow_ups)

    def test_composer_follow_up_map(self):
        from scripts.permuter.composer import _FOLLOW_UP_MAP
        self.assertIn("accessor_outline", _FOLLOW_UP_MAP)
        self.assertIn("declaration_reorder", _FOLLOW_UP_MAP["accessor_outline"])
        self.assertIn("value_address_caching", _FOLLOW_UP_MAP["accessor_outline"])

    def test_in_pattern_registry(self):
        """Verify the pattern appears in the global registry."""
        from scripts.permuter.patterns.base import list_patterns
        all_patterns = list_patterns(include_opt_in=True)
        self.assertIn("accessor_outline", all_patterns)


if __name__ == "__main__":
    unittest.main()
