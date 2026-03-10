"""Executable tests for the tail-call reorder permuter."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    diag_with_prologue_fewer_saves,
    make_context,
    make_ghidra_context,
    match_variant,
)


def _variants(
    source: str,
    ghidra_code: str | None = None,
    m2c_code: str | None = None,
):
    diagnosis = diag_with_prologue_fewer_saves()
    if ghidra_code is None:
        ctx = make_context(source, "test_func", diagnosis)
    else:
        ctx = make_ghidra_context(source, "test_func", diagnosis, ghidra_code)
    if m2c_code is not None:
        ctx.m2c_code = m2c_code
    return list(get_pattern("tail_call_reorder").generate(ctx))


class TestTailCallReorder(unittest.TestCase):
    def test_swaps_trailing_calls_at_function_end(self):
        variants = _variants(
            """\
void test_func() {
    First();
    Second();
}
"""
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    Second();
    First();
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_swaps_calls_before_return_in_nested_block(self):
        variants = _variants(
            """\
void test_func(int ok) {
    if (ok) {
        First();
        Second();
        return;
    }
}
"""
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func(int ok) {
    if (ok) {
        Second();
        First();
        return;
    }
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_nested_trailing_call_run_tries_more_than_tail_pair(self):
        variants = _variants(
            """\
void test_func(int ok) {
    if (ok) {
        Alpha();
        Beta();
        Gamma();
    }
}
"""
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func(int ok) {
    if (ok) {
        Beta();
        Alpha();
        Gamma();
    }
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_swaps_simple_if_wrapped_cleanup_calls(self):
        variants = _variants(
            """\
void test_func() {
    if (mFirst)
        First();
    if (mSecond)
        Second();
}
"""
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    if (mSecond)
        Second();
    if (mFirst)
        First();
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_swaps_compound_if_wrapped_cleanup_calls(self):
        variants = _variants(
            """\
void test_func() {
    if (mFirst) {
        First();
    }
    if (mSecond) {
        Second();
    }
}
"""
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    if (mSecond) {
        Second();
    }
    if (mFirst) {
        First();
    }
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_swaps_mixed_if_wrapper_and_plain_call(self):
        variants = _variants(
            """\
void test_func() {
    if (mFirst) {
        First();
    }
    Second();
}
"""
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    Second();
    if (mFirst) {
        First();
    }
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_does_not_swap_if_wrapper_with_callful_condition(self):
        variants = _variants(
            """\
void test_func() {
    if (ShouldRun())
        First();
    if (mSecond)
        Second();
}
"""
        )
        self.assertEqual(variants, [])

    def test_does_not_swap_same_name_tail_calls(self):
        variants = _variants(
            """\
struct Foo { void Poll(); };
void test_func(Foo* a, Foo* b) {
    a->Poll();
    b->Poll();
}
"""
        )
        self.assertEqual(variants, [])

    def test_does_not_swap_macro_timer_helpers(self):
        variants = _variants(
            """\
void test_func() {
    START_AUTO_TIMER("tail");
    DrawShowing();
}
"""
        )
        self.assertEqual(variants, [])

    def test_ghidra_guidance_skips_already_correct_last_call(self):
        variants = _variants(
            """\
void test_func() {
    First();
    Second();
    return;
}
""",
            ghidra_code="""\
void test_func(void) {
    First();
    Second();
    return;
}
""",
        )
        self.assertEqual(variants, [])

    def test_ghidra_guidance_applies_to_before_return_path(self):
        variants = _variants(
            """\
void test_func() {
    Second();
    First();
    return;
}
""",
            ghidra_code="""\
void test_func(void) {
    First();
    Second();
    return;
}
""",
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    First();
    Second();
    return;
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_m2c_guidance_applies_without_ghidra(self):
        variants = _variants(
            """\
void test_func() {
    Second();
    First();
    return;
}
""",
            m2c_code="""\
void test_func(void) {
    First();
    Second();
    return;
}
""",
        )
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    First();
    Second();
    return;
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_conflicting_ghidra_and_m2c_hints_fall_back_to_blind(self):
        variants = _variants(
            """\
void test_func() {
    First();
    Second();
    return;
}
""",
            ghidra_code="""\
void test_func(void) {
    First();
    Second();
    return;
}
""",
            m2c_code="""\
void test_func(void) {
    Second();
    First();
    return;
}
""",
        )
        self.assertTrue(variants)

    def test_does_not_reorder_obvious_mutator_calls(self):
        variants = _variants(
            """\
void test_func() {
    SetState();
    Finish();
}
"""
        )
        self.assertEqual(variants, [])

    def test_does_not_reorder_logging_calls(self):
        variants = _variants(
            """\
void test_func() {
    printf("before");
    Finish();
}
"""
        )
        self.assertEqual(variants, [])

    def test_does_not_reorder_unknown_calls_with_shared_input(self):
        variants = _variants(
            """\
void test_func(int* ptr) {
    First(ptr);
    Second(ptr);
}
"""
        )
        self.assertEqual(variants, [])


if __name__ == "__main__":
    unittest.main()
