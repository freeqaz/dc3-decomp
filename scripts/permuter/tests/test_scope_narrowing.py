"""Tests for scope_narrowing pattern."""

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
)


class TestIntoIf(unittest.TestCase):
    """Move declaration into if-body when only used there."""

    def test_basic_into_if(self):
        pattern = get_pattern("scope_narrowing")
        ctx = make_context(
            """\
void test_func() {
    bool isFocused = GetFocused();
    if (cond) {
        Use(isFocused);
    }
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        self.assertTrue(len(variants) > 0, "Expected at least one variant")
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    if (cond) {
        bool isFocused = GetFocused();
        Use(isFocused);
    }
}
""",
                    "normalized",
                )
                for v in variants
            ),
            f"No into-if variant matched. Got:\n"
            + "\n---\n".join(v.source.decode() for v in variants),
        )

    def test_into_if_with_else_no_use(self):
        """Move into if-body when else exists but doesn't use the variable."""
        pattern = get_pattern("scope_narrowing")
        ctx = make_context(
            """\
void test_func() {
    int val = Compute();
    if (x > 0) {
        Use(val);
    } else {
        Other();
    }
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        self.assertTrue(len(variants) > 0, "Expected at least one variant")
        # The declaration should be moved into the if-body
        found = any(
            match_variant(
                v.source,
                """\
void test_func() {
    if (x > 0) {
        int val = Compute();
        Use(val);
    } else {
        Other();
    }
}
""",
                "normalized",
            )
            for v in variants
        )
        self.assertTrue(found, "Expected into-if variant when else doesn't use var")


class TestIntoElse(unittest.TestCase):
    """Move declaration into else-body when only used there."""

    def test_basic_into_else(self):
        pattern = get_pattern("scope_narrowing")
        ctx = make_context(
            """\
void test_func() {
    int fallback = Default();
    if (x > 0) {
        DoA();
    } else {
        Use(fallback);
    }
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        found = any(
            match_variant(
                v.source,
                """\
void test_func() {
    if (x > 0) {
        DoA();
    } else {
        int fallback = Default();
        Use(fallback);
    }
}
""",
                "normalized",
            )
            for v in variants
        )
        self.assertTrue(found, "Expected into-else variant")


class TestIntoLoop(unittest.TestCase):
    """Move declaration into loop body."""

    def test_into_for_loop(self):
        pattern = get_pattern("scope_narrowing")
        ctx = make_context(
            """\
void test_func() {
    int tmp = 0;
    for (int i = 0; i < 10; i++) {
        tmp = Compute(i);
        Use(tmp);
    }
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        found = any(
            match_variant(
                v.source,
                """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        int tmp = 0;
        tmp = Compute(i);
        Use(tmp);
    }
}
""",
                "normalized",
            )
            for v in variants
        )
        self.assertTrue(found, "Expected into-loop variant")


class TestSafetyOutsideUse(unittest.TestCase):
    """Reject when variable is used outside the target scope."""

    def test_reject_used_outside_if(self):
        pattern = get_pattern("scope_narrowing")
        ctx = make_context(
            """\
void test_func() {
    int val = Get();
    if (cond) {
        Use(val);
    }
    UseAgain(val);
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        # Should NOT produce any variant moving val into the if-body
        # because val is used after the if
        self.assertFalse(
            any("val" in v.description for v in variants),
            "Should not move 'val' when used outside the if scope",
        )


class TestSafetyConditionUse(unittest.TestCase):
    """Reject when variable is used in the scope condition."""

    def test_reject_used_in_if_condition(self):
        pattern = get_pattern("scope_narrowing")
        ctx = make_context(
            """\
void test_func() {
    bool ready = IsReady();
    if (ready) {
        DoWork();
    }
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        # "ready" is used in the if-condition, so it can't be moved inside
        self.assertFalse(
            any("ready" in v.description for v in variants),
            "Should not move 'ready' when used in if condition",
        )


class TestSafetyAddressTaken(unittest.TestCase):
    """Reject when variable has its address taken."""

    def test_reject_address_taken(self):
        pattern = get_pattern("scope_narrowing")
        ctx = make_context(
            """\
void test_func() {
    int val = 0;
    if (cond) {
        TakeAddr(&val);
    }
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        self.assertFalse(
            any("val" in v.description for v in variants),
            "Should not move 'val' when address is taken",
        )


class TestRelevant(unittest.TestCase):
    """Test relevant() returns True for regswap diagnosis."""

    def test_relevant_with_gpr_swaps(self):
        pattern = get_pattern("scope_narrowing")
        diag = diag_with_gpr_swaps()
        self.assertTrue(pattern.relevant(diag))

    def test_relevant_with_callee_saved_swaps(self):
        pattern = get_pattern("scope_narrowing")
        diag = diag_with_callee_saved_swaps()
        self.assertTrue(pattern.relevant(diag))

    def test_relevant_with_clusters(self):
        pattern = get_pattern("scope_narrowing")
        diag = diag_with_clusters()
        self.assertTrue(pattern.relevant(diag))

    def test_not_relevant_empty_diag(self):
        pattern = get_pattern("scope_narrowing")
        diag = _empty_diag()
        self.assertFalse(pattern.relevant(diag))


class TestMetadata(unittest.TestCase):
    """Test pattern metadata."""

    def test_registered(self):
        pattern = get_pattern("scope_narrowing")
        self.assertEqual(pattern.name, "scope_narrowing")
        self.assertEqual(pattern.safety_tier, "normal")

    def test_follow_ups(self):
        pattern = get_pattern("scope_narrowing")
        self.assertIn("declaration_reorder", pattern.follow_ups)
        self.assertIn("value_address_caching", pattern.follow_ups)


if __name__ == "__main__":
    unittest.main()
