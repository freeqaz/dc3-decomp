"""Tests for value_address_caching pattern."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    _empty_diag,
    diag_with_callee_saved_swaps,
    diag_with_clusters,
    diag_with_gpr_swaps,
    make_context,
    match_variant,
    normalize,
)


class TestRefToValue(unittest.TestCase):
    """ref-to-value: `Type& ref = member` -> `auto val = member`."""

    def test_basic_ref_to_value(self):
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
void test_func() {
    int x = 10;
    auto& ref = x;
    int a = ref + 1;
    int b = ref + 2;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )
        # The `auto`-based ref2val form is msvc-only; under mwcc an `auto&` ref
        # has no concrete type to reuse and is skipped (the concrete-type mwcc
        # path is covered by test_ref_to_value_with_typed_ref).
        ctx.compiler_dialect = "msvc"

        variants = list(pattern.generate(ctx))
        # Should produce at least one ref-to-value variant
        ref2val = [v for v in variants if v.name.startswith("ref2val_")]
        self.assertTrue(len(ref2val) > 0, "Expected at least one ref-to-value variant")
        # Check the variant removes the & and renames
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    int x = 10;
    auto _val0 = x;
    int a = _val0 + 1;
    int b = _val0 + 2;
}
""",
                    "normalized",
                )
                for v in ref2val
            ),
            f"No ref-to-value variant matched expected output. Got:\n"
            + "\n---\n".join(v.source.decode() for v in ref2val),
        )

    def test_ref_to_value_with_typed_ref(self):
        """int& ref = member -> auto _val = member."""
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
void test_func() {
    int x = 10;
    int& ref = x;
    int a = ref;
    int b = ref;
}
""",
            "test_func",
            diag_with_callee_saved_swaps(),
        )

        variants = list(pattern.generate(ctx))
        ref2val = [v for v in variants if v.name.startswith("ref2val_")]
        self.assertTrue(len(ref2val) > 0, "Expected ref-to-value variant for typed ref")


class TestRefToValueSafety(unittest.TestCase):
    """Safety checks: don't transform when writes or address-of exist."""

    def test_no_transform_when_written(self):
        """ref = newval; should prevent transformation."""
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
void test_func() {
    int x = 10;
    auto& ref = x;
    ref = 20;
    int a = ref;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        ref2val = [v for v in variants if v.name.startswith("ref2val_")]
        self.assertEqual(
            len(ref2val), 0,
            "Should NOT produce ref-to-value when reference is written through",
        )

    def test_no_transform_when_address_taken(self):
        """&ref should prevent transformation."""
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
void bar(int* p);
void test_func() {
    int x = 10;
    auto& ref = x;
    bar(&ref);
    int a = ref;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        ref2val = [v for v in variants if v.name.startswith("ref2val_")]
        self.assertEqual(
            len(ref2val), 0,
            "Should NOT produce ref-to-value when address-of is used",
        )

    def test_no_transform_when_incremented(self):
        """ref++ should prevent transformation."""
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
void test_func() {
    int x = 10;
    auto& ref = x;
    ref++;
    int a = ref;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        ref2val = [v for v in variants if v.name.startswith("ref2val_")]
        self.assertEqual(
            len(ref2val), 0,
            "Should NOT produce ref-to-value when reference is incremented",
        )


class TestValueToRef(unittest.TestCase):
    """value-to-ref: `Type val = obj.Method()` -> `auto& ref = obj.Method()`."""

    def test_basic_value_to_ref(self):
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
struct Obj { int GetVal(); };
void test_func() {
    Obj obj;
    int val = obj.GetVal();
    int a = val + 1;
    int b = val + 2;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        val2ref = [v for v in variants if v.name.startswith("val2ref_")]
        self.assertTrue(len(val2ref) > 0, "Expected at least one value-to-ref variant")
        # Under mwcc (the RB3/DC3 target) the pattern reuses the source's
        # concrete type, emitting `int & _ref0` rather than `auto&`.
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
struct Obj { int GetVal(); };
void test_func() {
    Obj obj;
    int & _ref0 = obj.GetVal();
    int a = _ref0 + 1;
    int b = _ref0 + 2;
}
""",
                    "normalized",
                )
                for v in val2ref
            ),
            f"No value-to-ref variant matched expected output. Got:\n"
            + "\n---\n".join(v.source.decode() for v in val2ref),
        )

    def test_no_value_to_ref_for_args(self):
        """Don't transform if method call has arguments."""
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
struct Obj { int GetAt(int i); };
void test_func() {
    Obj obj;
    int val = obj.GetAt(5);
    int a = val;
    int b = val;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        val2ref = [v for v in variants if v.name.startswith("val2ref_")]
        self.assertEqual(
            len(val2ref), 0,
            "Should NOT produce value-to-ref when call has arguments",
        )


class TestInlineToCached(unittest.TestCase):
    """inline-to-cached: repeated obj.Method() calls -> cached local."""

    def test_basic_caching(self):
        pattern = get_pattern("value_address_caching")
        ctx = make_context(
            """\
struct Obj { int Size(); };
void test_func() {
    Obj obj;
    int a = obj.Size();
    int b = obj.Size();
    int c = obj.Size();
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )
        # inline-to-cached introduces `auto _cached = ...`, so it is msvc-only.
        ctx.compiler_dialect = "msvc"

        variants = list(pattern.generate(ctx))
        cache = [v for v in variants if v.name.startswith("cache_")]
        self.assertTrue(len(cache) > 0, "Expected at least one inline-to-cached variant")
        # The variant should introduce a cached variable
        self.assertTrue(
            any(b"_cached" in v.source for v in cache),
            "Expected cached variable in output",
        )

    def test_no_caching_below_threshold(self):
        """Only 2 occurrences should not trigger caching (threshold is 3)."""
        pattern = get_pattern("value_address_caching")
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
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        cache = [v for v in variants if v.name.startswith("cache_")]
        self.assertEqual(
            len(cache), 0,
            "Should NOT cache with fewer than 3 occurrences",
        )


class TestRelevance(unittest.TestCase):
    """Test relevant() method for diagnosis filtering."""

    def test_relevant_with_gpr_swaps(self):
        pattern = get_pattern("value_address_caching")
        diag = diag_with_gpr_swaps()
        self.assertTrue(pattern.relevant(diag))

    def test_relevant_with_callee_saved_swaps(self):
        pattern = get_pattern("value_address_caching")
        diag = diag_with_callee_saved_swaps()
        self.assertTrue(pattern.relevant(diag))

    def test_relevant_with_clusters(self):
        pattern = get_pattern("value_address_caching")
        diag = diag_with_clusters()
        self.assertTrue(pattern.relevant(diag))

    def test_not_relevant_empty_diagnosis(self):
        """Empty diagnosis with no swaps and no clusters -> not relevant."""
        pattern = get_pattern("value_address_caching")
        diag = _empty_diag()
        self.assertFalse(pattern.relevant(diag))

    def test_not_relevant_volatile_only_swaps(self):
        """Volatile register swaps (r0-r12) should not trigger relevance."""
        from scripts.permuter.types import SwapInfo
        pattern = get_pattern("value_address_caching")
        diag = _empty_diag()
        diag.reg_swap_pairs = {
            ("r3", "r4"): SwapInfo(count=2, first_idx=5, last_idx=20)
        }
        self.assertFalse(pattern.relevant(diag))


class TestPatternMetadata(unittest.TestCase):
    """Verify pattern registration and metadata."""

    def test_pattern_registered(self):
        pattern = get_pattern("value_address_caching")
        self.assertEqual(pattern.name, "value_address_caching")

    def test_follow_ups(self):
        pattern = get_pattern("value_address_caching")
        self.assertIn("declaration_reorder", pattern.follow_ups)
        self.assertIn("prologue_pressure", pattern.follow_ups)

    def test_composer_follow_up_map(self):
        from scripts.permuter.composer import _FOLLOW_UP_MAP
        self.assertIn("value_address_caching", _FOLLOW_UP_MAP)
        self.assertIn("declaration_reorder", _FOLLOW_UP_MAP["value_address_caching"])
        self.assertIn("prologue_pressure", _FOLLOW_UP_MAP["value_address_caching"])


if __name__ == "__main__":
    unittest.main()
