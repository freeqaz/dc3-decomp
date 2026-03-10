"""Tests for evolutionary optimizer components."""

from __future__ import annotations

from scripts.permuter.evolutionary import (
    Individual,
    _crossover,
    _dedup_population,
    _mutate,
    _tournament_select,
)
from scripts.permuter.tests.conftest import diag_with_branch_ops, make_context
from scripts.permuter.types import Variant


def _make_individual(
    name: str, fitness: float, source: bytes = b"",
) -> Individual:
    return Individual(
        variant=Variant(
            name=name, pattern_name=name,
            description="test", source=source,
        ),
        fitness=fitness,
        build_success=True,
    )


class _MutateTagPattern:
    name = "test_mutate_tag_pattern"

    def generate(self, ctx):
        yield Variant(
            name="mutated",
            pattern_name=self.name,
            description="mutated",
            source=ctx.file_source.replace(b"First", b"Second"),
            tags=frozenset({"tag_b"}),
        )


class TestTournamentSelect:
    def test_highest_fitness_wins(self):
        pop = [
            _make_individual("a", 50.0),
            _make_individual("b", 90.0),
            _make_individual("c", 70.0),
        ]
        # With k=3, all are selected, so b (90%) must win
        winner = _tournament_select(pop, k=3)
        assert winner.fitness == 90.0

    def test_single_candidate(self):
        pop = [_make_individual("a", 42.0)]
        winner = _tournament_select(pop, k=3)
        assert winner.fitness == 42.0


class TestCrossover:
    def test_non_overlapping_merge(self):
        original = b"AAAA BBBB CCCC"
        # Parent A changes "AAAA" to "XXXX"
        source_a = b"XXXX BBBB CCCC"
        # Parent B changes "CCCC" to "YYYY"
        source_b = b"AAAA BBBB YYYY"

        parent_a = _make_individual("a", 60.0, source_a)
        parent_b = _make_individual("b", 60.0, source_b)

        child = _crossover(original, parent_a, parent_b)
        assert child is not None
        assert child.source == b"XXXX BBBB YYYY"

    def test_overlapping_returns_none(self):
        original = b"AAAA BBBB"
        # Both change the same region
        source_a = b"XXXX BBBB"
        source_b = b"YYYY BBBB"

        parent_a = _make_individual("a", 60.0, source_a)
        parent_b = _make_individual("b", 60.0, source_b)

        child = _crossover(original, parent_a, parent_b)
        assert child is None

    def test_identical_parents_returns_none(self):
        original = b"AAAA BBBB"
        source = b"XXXX BBBB"

        parent_a = _make_individual("a", 60.0, source)
        parent_b = _make_individual("b", 60.0, source)

        # Same edits overlap with themselves
        child = _crossover(original, parent_a, parent_b)
        assert child is None


class TestDedup:
    def test_removes_duplicates(self):
        pop = [
            _make_individual("a", 50.0, b"same source"),
            _make_individual("b", 60.0, b"same source"),
            _make_individual("c", 70.0, b"different"),
        ]
        result = _dedup_population(pop)
        assert len(result) == 2
        # First occurrence kept
        assert result[0].variant.name == "a"
        assert result[1].variant.name == "c"

    def test_no_duplicates(self):
        pop = [
            _make_individual("a", 50.0, b"source1"),
            _make_individual("b", 60.0, b"source2"),
        ]
        result = _dedup_population(pop)
        assert len(result) == 2


class TestMutate:
    def test_preserves_and_unions_tags(self, monkeypatch):
        ctx = make_context(
            """\
void test_func() {
    First();
}
""",
            "test_func",
            diag_with_branch_ops(),
        )
        individual = Individual(
            variant=Variant(
                name="seed",
                pattern_name="seed",
                description="seed",
                source=ctx.file_source,
                tags=frozenset({"tag_a"}),
            ),
            fitness=50.0,
            build_success=True,
        )

        monkeypatch.setattr("scripts.permuter.evolutionary.random.choice", lambda seq: seq[0])
        mutated = _mutate(ctx, individual, [_MutateTagPattern()])

        assert mutated is not None
        assert mutated.tags == frozenset({"tag_a", "tag_b"})
