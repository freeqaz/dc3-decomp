"""Tests for shared statement-effect analysis and migrated reorder logic."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.statement_effects import (
    StatementEffectAnalyzer,
    build_def_use_chains,
)
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


class TestAliasDetection(unittest.TestCase):
    def test_detects_reference_alias(self):
        ctx = make_context(
            """\
void test_func(int x) {
    auto& ref = x;
    ref = 5;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        effects = analyzer.analyze(ctx.statements[0])
        self.assertEqual(len(effects.aliases), 1)
        alias = effects.aliases[0]
        self.assertEqual(alias.alias_name, "ref")
        self.assertEqual(alias.target_root, "x")
        self.assertTrue(alias.is_reference)

    def test_detects_member_reference_alias(self):
        ctx = make_context(
            """\
struct Foo { int val; };
void test_func(Foo* foo) {
    auto& ref = foo->val;
    ref = 10;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        effects = analyzer.analyze(ctx.statements[0])
        self.assertEqual(len(effects.aliases), 1)
        alias = effects.aliases[0]
        self.assertEqual(alias.alias_name, "ref")
        self.assertEqual(alias.target_root, "foo")
        self.assertTrue(alias.is_reference)

    def test_no_alias_for_value_init(self):
        ctx = make_context(
            """\
void test_func() {
    int x = 5;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        effects = analyzer.analyze(ctx.statements[0])
        self.assertEqual(len(effects.aliases), 0)

    def test_no_alias_for_non_declaration(self):
        ctx = make_context(
            """\
void test_func(int x) {
    x = 5;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        effects = analyzer.analyze(ctx.statements[0])
        self.assertEqual(len(effects.aliases), 0)


class TestDefUseChains(unittest.TestCase):
    def test_simple_def_use(self):
        ctx = make_context(
            """\
void test_func(int a) {
    int x = 1;
    a = x + 2;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        chains = build_def_use_chains(ctx.statements, analyzer)
        # x is defined at stmt 0, used at stmt 1
        x_entries = [e for e in chains.entries if e.variable == "x" and e.def_stmt_idx == 0]
        self.assertTrue(len(x_entries) >= 1)
        self.assertEqual(x_entries[0].use_stmt_idx, 1)

    def test_live_range(self):
        ctx = make_context(
            """\
void test_func() {
    int x = 1;
    int y = 2;
    int z = x + y;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        chains = build_def_use_chains(ctx.statements, analyzer)
        # x defined at 0, used at 2 → live range (0, 2)
        self.assertIn("x", chains.live_ranges)
        self.assertEqual(chains.live_ranges["x"], (0, 2))
        # y defined at 1, used at 2 → live range (1, 2)
        self.assertIn("y", chains.live_ranges)
        self.assertEqual(chains.live_ranges["y"], (1, 2))

    def test_can_move_past_independent(self):
        ctx = make_context(
            """\
void test_func() {
    int x = 1;
    int y = 2;
    int z = 3;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        chains = build_def_use_chains(ctx.statements, analyzer)
        # Independent declarations can be moved past each other
        self.assertTrue(chains.can_move_past(0, 1))
        self.assertTrue(chains.can_move_past(1, 2))

    def test_cannot_move_past_dependent(self):
        ctx = make_context(
            """\
void test_func() {
    int x = 1;
    int y = x + 1;
    int z = y + 1;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        chains = build_def_use_chains(ctx.statements, analyzer)
        # x defined at 0, used at 1 → can't move stmt 0 past stmt 1
        self.assertFalse(chains.can_move_past(0, 1))
        # y defined at 1, used at 2 → can't move stmt 1 past stmt 2
        self.assertFalse(chains.can_move_past(1, 2))

    def test_is_live_between(self):
        ctx = make_context(
            """\
void test_func() {
    int x = 1;
    int y = 2;
    int z = x + y;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        chains = build_def_use_chains(ctx.statements, analyzer)
        # x is live between its def (0) and use (2)
        self.assertTrue(chains.is_live_between("x", 0, 2))
        # y is live between 1 and 2
        self.assertTrue(chains.is_live_between("y", 1, 2))

    def test_parameter_use_without_def(self):
        ctx = make_context(
            """\
void test_func(int p) {
    int x = p;
    int y = p + 1;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        chains = build_def_use_chains(ctx.statements, analyzer)
        # p is used at stmt 0 and 1 without prior definition → live-in entries
        p_entries = [e for e in chains.entries if e.variable == "p"]
        self.assertTrue(len(p_entries) >= 2)
        # All should have def_stmt_idx == -1 (live-in)
        live_in = [e for e in p_entries if e.def_stmt_idx == -1]
        self.assertTrue(len(live_in) >= 2)


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
