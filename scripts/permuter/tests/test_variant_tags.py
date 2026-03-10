"""Tests for structural variant tags and tag-aware adaptive composition."""

from __future__ import annotations

from scripts.permuter.composer import (
    _select_companion_patterns,
    build_adaptive_chains,
    compose_variants,
    get_compose_pairs,
)
from scripts.permuter.generator import allocate_budgets, generate_variants
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    diag_with_branch_ops,
    diag_with_clusters,
    diag_with_gpr_swaps,
    diag_with_prologue_fewer_saves,
    make_context,
    match_variant,
)
from scripts.permuter.types import RoundHints, ScoreResult, Variant


class _TagStageAPattern:
    name = "test_tag_stage_a"

    def relevant(self, diagnosis):
        return True

    def generate(self, ctx):
        yield Variant(
            name="tag_a",
            pattern_name=self.name,
            description="stage a",
            source=ctx.file_source.replace(b"value = 0", b"value = 1"),
            tags=frozenset({"tag_a"}),
        )


class _TagStageBPattern:
    name = "test_tag_stage_b"

    def relevant(self, diagnosis):
        return True

    def generate(self, ctx):
        yield Variant(
            name="tag_b",
            pattern_name=self.name,
            description="stage b",
            source=ctx.file_source.replace(b"value = 1", b"value = 2"),
            tags=frozenset({"tag_b"}),
        )


class _BlockedStageBPattern(_TagStageBPattern):
    name = "test_tag_stage_b_blocked"

    def relevant(self, diagnosis):
        return False


class _BudgetPattern:
    def __init__(
        self,
        name: str,
        tag: str,
        safety_tier: str = "normal",
        structural_domain: str = "general",
    ):
        self.name = name
        self._tag = tag
        self.safety_tier = safety_tier
        self.structural_domain = structural_domain

    def priority(self, diagnosis):
        return 1.0

    def generate(self, ctx):
        yield Variant(
            name=self.name,
            pattern_name=self.name,
            description=self.name,
            source=ctx.file_source + f"// {self.name}\n".encode(),
            tags=frozenset({self._tag}),
        )


class _ZeroPriorityPattern(_BudgetPattern):
    def priority(self, diagnosis):
        return 0.0


class _ContextPattern(_BudgetPattern):
    def __init__(
        self,
        name: str,
        tag: str,
        *,
        follow_ups: tuple[str, ...] = (),
        requires_context: tuple[str, ...] = (),
    ):
        super().__init__(name, tag)
        self.follow_ups = follow_ups
        self.requires_context = requires_context

    def relevant(self, diagnosis):
        return True


def test_variable_extraction_variants_are_tagged():
    ctx = make_context(
        """\
void test_func() {
    check(getSize());
}
""",
        "test_func",
        diag_with_gpr_swaps(),
    )

    variants = list(get_pattern("variable_extraction").generate(ctx))
    assert variants
    assert all("introduced_temp" in variant.tags for variant in variants)


def test_switch_if_convert_tags_if_to_switch():
    ctx = make_context(
        """\
void test_func(int state) {
    if (state == 0) {
        do_a();
    } else if (state == 1) {
        do_b();
    } else if (state == 2) {
        do_c();
    } else {
        do_d();
    }
}
""",
        "test_func",
        diag_with_branch_ops(),
    )

    variants = list(get_pattern("switch_if_convert").generate(ctx))
    tagged = [
        v for v in variants
        if match_variant(
            v.source,
            """\
void test_func(int state) {
    switch (state) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    case 2:
        do_c();
        break;
    default:
        do_d();
        break;
    }
}
""",
            "normalized",
        )
    ]

    assert tagged
    assert tagged[0].tags == frozenset({"converted_if_to_switch"})


def test_return_call_merge_tags_both_directions():
    pattern = get_pattern("return_call_merge")

    merge_ctx = make_context(
        """\
int test_func(bool cond, int a, int b) {
    if (cond) {
        return Pick(a);
    } else {
        return Pick(b);
    }
}
""",
        "test_func",
        diag_with_branch_ops(),
    )
    split_ctx = make_context(
        """\
int test_func(bool cond, int a, int b) {
    int _merged;
    if (cond) {
        _merged = a;
    } else {
        _merged = b;
    }
    return Pick(_merged);
}
""",
        "test_func",
        diag_with_branch_ops(),
    )

    merge_variants = list(pattern.generate(merge_ctx))
    split_variants = list(pattern.generate(split_ctx))

    assert any("merged_return_calls" in variant.tags for variant in merge_variants)
    assert any("split_return_calls" in variant.tags for variant in split_variants)


def test_declaration_reorder_variants_are_tagged():
    ctx = make_context(
        """\
void test_func() {
    int a = 1;
    int b = 2;
}
""",
        "test_func",
        diag_with_gpr_swaps(),
    )

    variants = list(get_pattern("declaration_reorder").generate(ctx))
    assert variants
    assert all("reordered_declarations" in variant.tags for variant in variants)


def test_assignment_reorder_variants_are_tagged():
    ctx = make_context(
        """\
struct State { int a; int b; };
void test_func(State value) {
    value.a = 0;
    value.b = 1;
}
""",
        "test_func",
        diag_with_clusters(),
    )

    variants = list(get_pattern("assignment_reorder").generate(ctx))
    assert variants
    assert all("reordered_assignments" in variant.tags for variant in variants)


def test_declaration_movement_variants_are_tagged():
    ctx = make_context(
        """\
void test_func() {
    int local = GetValue();
    int total = 0;
}
""",
        "test_func",
        diag_with_gpr_swaps(),
    )

    variants = list(get_pattern("declaration_movement").generate(ctx))
    assert variants
    assert all("moved_declaration" in variant.tags for variant in variants)


def test_tail_call_reorder_variants_are_tagged():
    ctx = make_context(
        """\
void test_func() {
    First();
    Second();
}
""",
        "test_func",
        diag_with_prologue_fewer_saves(),
    )

    variants = list(get_pattern("tail_call_reorder").generate(ctx))
    assert variants
    assert all("reordered_tail_calls" in variant.tags for variant in variants)


def test_select_companion_patterns_uses_structural_tags():
    companions = _select_companion_patterns(
        "variable_extraction",
        frozenset({"introduced_temp"}),
        [],
        [
            get_pattern("declaration_reorder"),
            get_pattern("inline_assignment"),
            get_pattern("statement_reorder"),
        ],
        baseline=0.0,
    )

    companion_names = {pattern.name for pattern in companions}
    assert "statement_reorder" in companion_names


def test_compose_variants_unions_tags():
    ctx = make_context(
        """\
void test_func() {
    int value = 0;
}
""",
        "test_func",
        diag_with_branch_ops(),
    )

    variants = list(compose_variants(
        ctx,
        _TagStageAPattern(),
        _TagStageBPattern(),
        max_per_stage=1,
        max_total=1,
    ))

    assert len(variants) == 1
    assert variants[0].tags == frozenset({"tag_a", "tag_b"})


def test_compose_variants_allows_boosted_second_stage():
    ctx = make_context(
        """\
void test_func() {
    int value = 0;
}
""",
        "test_func",
        diag_with_branch_ops(),
    )

    variants = list(compose_variants(
        ctx,
        _TagStageAPattern(),
        _BlockedStageBPattern(),
        max_per_stage=1,
        max_total=1,
        round_hints=RoundHints(atlas_boost_patterns={"test_tag_stage_b_blocked"}),
    ))

    assert len(variants) == 1
    assert variants[0].pattern_name == "compose:test_tag_stage_a+test_tag_stage_b_blocked"


def test_round_hints_record_tag_history_from_winner():
    hints = RoundHints()
    winner = Variant(
        name="winner",
        pattern_name="variable_extraction",
        description="winner",
        source=b"winner",
        tags=frozenset({"introduced_temp"}),
    )
    loser = Variant(
        name="loser",
        pattern_name="branch_polarity",
        description="loser",
        source=b"loser",
        tags=frozenset({"converted_if_to_switch"}),
    )

    hints.record_round(
        round_num=1,
        variant_results=[
            ScoreResult(variant=winner, match_percent=72.0, build_success=True),
            ScoreResult(variant=loser, match_percent=59.0, build_success=True),
        ],
        baseline=60.0,
        winner_pattern="variable_extraction",
        winner_variant=winner,
    )

    assert hints.last_winner == "variable_extraction"
    assert hints.last_winner_tags == frozenset({"introduced_temp"})
    assert hints.pattern_failures.get("variable_extraction", 0) == 0
    assert hints.promising_tags() == ["introduced_temp"]
    assert hints.promising_tags_for_pattern("variable_extraction") == frozenset(
        {"introduced_temp"}
    )


def test_build_adaptive_chains_uses_last_winner_tags_for_new_followups():
    hints = RoundHints(
        last_winner="variable_extraction",
        last_winner_tags=frozenset({"introduced_temp"}),
    )
    patterns = [
        get_pattern("variable_extraction"),
        get_pattern("declaration_reorder"),
        get_pattern("inline_assignment"),
        get_pattern("statement_reorder"),
    ]

    chains = build_adaptive_chains(
        diagnosis=None,
        patterns=patterns,
        hints=hints,
        max_depth=3,
        max_chains=10,
    )

    assert any(
        chain.stages[:2] == ["variable_extraction", "statement_reorder"]
        for chain in chains
    )


def test_promising_pattern_tags_expand_chains_without_last_winner():
    hints = RoundHints()
    hints.pattern_deltas["variable_extraction"] = [(3.0, 1)]
    hints.pattern_positive_tags["variable_extraction"] = {"introduced_temp"}
    patterns = [
        get_pattern("variable_extraction"),
        get_pattern("statement_reorder"),
        get_pattern("declaration_reorder"),
    ]

    chains = build_adaptive_chains(
        diagnosis=None,
        patterns=patterns,
        hints=hints,
        max_depth=3,
        max_chains=10,
    )

    assert any(
        chain.stages[:2] == ["variable_extraction", "statement_reorder"]
        for chain in chains
    )


def test_get_compose_pairs_uses_tag_history():
    hints = RoundHints(
        last_winner="variable_extraction",
        last_winner_tags=frozenset({"introduced_temp"}),
    )
    pairs = get_compose_pairs(
        diagnosis=None,
        patterns=[
            get_pattern("variable_extraction"),
            get_pattern("declaration_reorder"),
            get_pattern("inline_assignment"),
            get_pattern("statement_reorder"),
        ],
        hints=hints,
        max_pairs=10,
    )

    assert ("variable_extraction", "statement_reorder") in pairs


def test_get_compose_pairs_uses_declared_pattern_follow_ups_without_hints():
    pairs = get_compose_pairs(
        diagnosis=None,
        patterns=[
            get_pattern("variable_extraction"),
            get_pattern("statement_reorder"),
        ],
        hints=None,
        max_pairs=10,
    )

    assert ("variable_extraction", "statement_reorder") in pairs


def test_get_compose_pairs_prefers_available_context():
    patterns = [
        _ContextPattern(
            "ctx_base", "tag_a",
            follow_ups=("ctx_next",),
            requires_context=("ghidra",),
        ),
        _ContextPattern("ctx_next", "tag_b", requires_context=("ghidra",)),
        _ContextPattern("plain_base", "tag_c", follow_ups=("plain_next",)),
        _ContextPattern("plain_next", "tag_d"),
    ]

    without_ctx = get_compose_pairs(
        diagnosis=None,
        patterns=patterns,
        available_context=None,
        max_pairs=2,
    )
    with_ctx = get_compose_pairs(
        diagnosis=None,
        patterns=patterns,
        available_context={"ghidra"},
        max_pairs=2,
    )

    assert without_ctx[0] == ("plain_base", "plain_next")
    assert with_ctx[0] == ("ctx_base", "ctx_next")


def test_build_adaptive_chains_prefers_available_context():
    patterns = [
        _ContextPattern(
            "ctx_base", "tag_a",
            follow_ups=("ctx_next",),
            requires_context=("ghidra",),
        ),
        _ContextPattern("ctx_next", "tag_b", requires_context=("ghidra",)),
        _ContextPattern("plain_base", "tag_c", follow_ups=("plain_next",)),
        _ContextPattern("plain_next", "tag_d"),
    ]

    without_ctx = build_adaptive_chains(
        diagnosis=diag_with_branch_ops(),
        patterns=patterns,
        hints=None,
        available_context=None,
        max_depth=2,
        max_chains=2,
    )
    with_ctx = build_adaptive_chains(
        diagnosis=diag_with_branch_ops(),
        patterns=patterns,
        hints=None,
        available_context={"ghidra"},
        max_depth=2,
        max_chains=2,
    )

    assert without_ctx[0].stages == ["plain_base", "plain_next"]
    assert with_ctx[0].stages == ["ctx_base", "ctx_next"]


def test_allocate_budgets_boosts_patterns_with_winning_tags():
    hints = RoundHints(
        last_winner_tags=frozenset({"tag_hot"}),
        tag_wins={"tag_hot": 2},
        pattern_positive_tags={
            "pat_hot": {"tag_hot"},
            "pat_cold": {"tag_cold"},
        },
        tag_deltas={
            "tag_hot": [(2.0, 1)],
            "tag_cold": [(1.0, 1)],
        },
    )
    patterns = [
        _BudgetPattern("pat_hot", "tag_hot"),
        _BudgetPattern("pat_cold", "tag_cold"),
    ]

    budgets = allocate_budgets(
        patterns,
        total_budget=12,
        diagnosis=None,
        round_hints=hints,
    )

    assert budgets["pat_hot"] > budgets["pat_cold"]


def test_generate_variants_orders_phase1_by_adaptive_priority():
    hints = RoundHints(
        last_winner_tags=frozenset({"tag_hot"}),
        tag_wins={"tag_hot": 2},
        pattern_positive_tags={
            "pat_hot": {"tag_hot"},
            "pat_cold": {"tag_cold"},
        },
        tag_deltas={
            "tag_hot": [(2.0, 1)],
            "tag_cold": [(1.0, 1)],
        },
    )
    ctx = make_context(
        """\
void test_func() {
    int value = 0;
}
""",
        "test_func",
        diag_with_branch_ops(),
    )
    patterns = [
        _BudgetPattern("pat_cold", "tag_cold"),
        _BudgetPattern("pat_hot", "tag_hot"),
    ]

    variants = list(generate_variants(
        ctx,
        patterns,
        max_variants=6,
        round_hints=hints,
    ))

    assert variants
    assert variants[0].pattern_name == "pat_hot"


def test_allocate_budgets_boosted_pattern_overrides_zero_priority():
    hints = RoundHints(atlas_boost_patterns={"pat_forced"})
    patterns = [
        _ZeroPriorityPattern("pat_forced", "tag_forced"),
        _BudgetPattern("pat_normal", "tag_normal"),
    ]

    budgets = allocate_budgets(
        patterns,
        total_budget=12,
        diagnosis=diag_with_branch_ops(),
        round_hints=hints,
    )

    assert budgets["pat_forced"] > 0


def test_allocate_budgets_prefers_conservative_pattern_when_equal():
    patterns = [
        _BudgetPattern("pat_conservative", "tag_a", safety_tier="conservative"),
        _BudgetPattern("pat_moderate", "tag_b", safety_tier="moderate"),
    ]

    budgets = allocate_budgets(
        patterns,
        total_budget=12,
        diagnosis=None,
        round_hints=None,
    )

    assert budgets["pat_conservative"] > budgets["pat_moderate"]


def test_allocate_budgets_prefers_same_domain_as_last_winner():
    hints = RoundHints(last_winner="statement_reorder")
    patterns = [
        _BudgetPattern("pat_cfg", "tag_a", structural_domain="control_flow"),
        _BudgetPattern("pat_data", "tag_b", structural_domain="data_flow"),
    ]

    budgets = allocate_budgets(
        patterns,
        total_budget=12,
        diagnosis=None,
        round_hints=hints,
    )

    assert budgets["pat_cfg"] > budgets["pat_data"]
