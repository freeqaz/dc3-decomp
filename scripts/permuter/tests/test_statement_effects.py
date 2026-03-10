"""Tests for shared statement-effect analysis and migrated reorder logic."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.statement_effects import StatementEffectAnalyzer
from scripts.permuter.tests.conftest import (
    diag_with_clusters,
    make_context,
    match_variant,
)


class TestStatementEffectAnalyzer(unittest.TestCase):
    def test_detects_reads_writes_and_assert_markers(self):
        ctx = make_context(
            """\
void test_func(int* ptr, int value) {
    ptr[0] = value;
    MILO_ASSERT(ptr, 0x10);
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        assign_effects = analyzer.analyze(ctx.statements[0])
        assert_effects = analyzer.analyze(ctx.statements[1])

        self.assertIn("ptr", assign_effects.writes)
        self.assertIn("value", assign_effects.reads)
        self.assertTrue(assert_effects.has_assert_like_guard)

    def test_collects_call_names_and_kinds(self):
        ctx = make_context(
            """\
void test_func(Foo* foo) {
    foo->GetValue();
    Apply();
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        accessor_effects = analyzer.analyze(ctx.statements[0])
        unknown_effects = analyzer.analyze(ctx.statements[1])

        self.assertIn("GetValue", accessor_effects.call_names)
        self.assertIn("accessor", accessor_effects.call_kinds)
        self.assertTrue(accessor_effects.has_direct_call)
        self.assertIn("Apply", unknown_effects.call_names)
        self.assertIn("unknown", unknown_effects.call_kinds)
        self.assertTrue(unknown_effects.has_direct_call)

    def test_classifies_mutator_and_logging_calls(self):
        ctx = make_context(
            """\
void test_func() {
    SetValue();
    printf("x");
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        mutator_effects = analyzer.analyze(ctx.statements[0])
        logging_effects = analyzer.analyze(ctx.statements[1])

        self.assertIn("mutator", mutator_effects.call_kinds)
        self.assertIn("logging", logging_effects.call_kinds)

    def test_call_pair_policy_is_configurable(self):
        ctx = make_context(
            """\
void test_func() {
    First();
    Second();
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        first, second = ctx.statements

        self.assertFalse(analyzer.are_independent(first, second))
        self.assertTrue(analyzer.are_independent(first, second, allow_call_pair=True))

    def test_rejects_reordering_mutator_and_logging_call_pairs(self):
        ctx = make_context(
            """\
void test_func() {
    SetValue();
    Second();
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        first, second = ctx.statements

        self.assertFalse(analyzer.can_reorder_call_pair(first, second))

    def test_rejects_unknown_call_pairs_sharing_inputs(self):
        ctx = make_context(
            """\
void test_func(int* ptr) {
    First(ptr);
    Second(ptr);
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        first, second = ctx.statements

        self.assertFalse(analyzer.can_reorder_call_pair(first, second))

    def test_allows_accessor_call_pairs_sharing_inputs(self):
        ctx = make_context(
            """\
void test_func(Foo* foo) {
    foo->GetValue();
    foo->HasValue();
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        first, second = ctx.statements

        self.assertTrue(analyzer.can_reorder_call_pair(first, second))

    def test_rejects_reordering_write_past_direct_logging_call(self):
        ctx = make_context(
            """\
void test_func(int value) {
    value = 1;
    printf("log");
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        first, second = ctx.statements

        self.assertFalse(analyzer.can_reorder_statement_pair(first, second))


class TestStatementReorderWithEffects(unittest.TestCase):
    def test_moves_assignment_past_independent_guard(self):
        pattern = get_pattern("statement_reorder")
        ctx = make_context(
            """\
void test_func(float w, float x) {
    w = 0.0f;
    if (x < 0.0f)
        printf("bad x");
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(pattern.generate(ctx))
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func(float w, float x) {
    if (x < 0.0f)
        printf("bad x");
    w = 0.0f;
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_keeps_dependent_declaration_after_assignment(self):
        pattern = get_pattern("statement_reorder")
        ctx = make_context(
            """\
void test_func(int a, int x) {
    a = 5;
    int b = a + 1;
    x = 99;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(pattern.generate(ctx))
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func(int a, int x) {
    a = 5;
    x = 99;
    int b = a + 1;
}
""",
                    "normalized",
                )
                for v in variants
            )
        )
        self.assertFalse(
            any(
                match_variant(
                    v.source,
                    """\
void test_func(int a, int x) {
    int b = a + 1;
    a = 5;
    x = 99;
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_does_not_move_assignment_past_direct_logging_call(self):
        pattern = get_pattern("statement_reorder")
        ctx = make_context(
            """\
void test_func(int value) {
    value = 1;
    printf("log");
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(pattern.generate(ctx))
        self.assertFalse(
            any(
                match_variant(
                    v.source,
                    """\
void test_func(int value) {
    printf("log");
    value = 1;
}
""",
                    "normalized",
                )
                for v in variants
            )
        )


if __name__ == "__main__":
    unittest.main()
