"""Composed (A->B) pattern tests — two-step pattern composition."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.extractor import reparse_variant
from scripts.permuter.types import (
    Cluster,
    Diagnosis,
    DiffOp,
    SwapInfo,
)
from scripts.permuter.tests.conftest import (
    ComposedFixture,
    _empty_diag,
    _similarity,
    diag_with_cmp_ops,
    make_context,
    match_variant,
    normalize,
)
from scripts.permuter.patterns.base import get_pattern


def _compose_diag() -> Diagnosis:
    """Diagnosis suitable for variable_extraction + declaration_reorder."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r20", "r21"): SwapInfo(count=4, first_idx=10, last_idx=50)
    }
    return d


def _compose_diag_cmp() -> Diagnosis:
    """Diagnosis suitable for inline_assignment + comparison_flip."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=3, target_opcode="cmpwi", base_opcode="cmplwi")]
    d.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=3, deletes=2)]
    return d


COMPOSED_FIXTURES: list[ComposedFixture] = [
    # varext extracts getSize() into auto _tmp0, then declreorder swaps declarations
    ComposedFixture(
        id="compose_varext_then_declreorder",
        stage_a_pattern="variable_extraction",
        stage_b_pattern="declaration_reorder",
        description="extract call into auto, then reorder declarations",
        func_name="test_func",
        diagnosis=_compose_diag(),
        # variable_extraction emits `auto _tmp0 = ...`, so this is msvc-only.
        compiler_dialect="msvc",
        seeded_source="""\
void test_func() {
    int a = 1;
    check(a < getSize(), 0x74);
}
""",
        intermediate_contains="auto _tmp0 = getSize()",
        expected_source="""\
void test_func() {
    auto _tmp0 = getSize();
    int a = 1;
    check(a < _tmp0, 0x74);
}
""",
    ),

    # inline_assignment folds assignment into call arg, comparison_flip flips a comparison
    ComposedFixture(
        id="compose_inline_then_cmpflip",
        stage_a_pattern="inline_assignment",
        stage_b_pattern="comparison_flip",
        description="fold assignment into call, then flip comparison",
        func_name="test_func",
        diagnosis=_compose_diag_cmp(),
        seeded_source="""\
int test_func(int a, int b) {
    int era;
    era = getName();
    process(era);
    if (a < b) {
        return 1;
    }
    return 0;
}
""",
        intermediate_contains="process(era = getName())",
        expected_source="""\
int test_func(int a, int b) {
    int era;
    process(era = getName());
    if (b > a) {
        return 1;
    }
    return 0;
}
""",
    ),

    # comparison_equivalence changes i < 2 to i <= 1, then signed_unsigned swaps != 0 to > 0
    ComposedFixture(
        id="compose_cmpeq_then_signunsign",
        stage_a_pattern="comparison_equivalence",
        stage_b_pattern="signed_unsigned",
        description="change < N to <= N-1, then swap != 0 to > 0",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
int test_func(int i, int x) {
    if (i < 2) {
        return 1;
    }
    if (x != 0) {
        return 2;
    }
    return 0;
}
""",
        intermediate_contains="i <= 1",
        expected_source="""\
int test_func(int i, int x) {
    if (i <= 1) {
        return 1;
    }
    if (x > 0) {
        return 2;
    }
    return 0;
}
""",
    ),
]

_COMPOSED_FIXTURE_MAP: dict[str, ComposedFixture] = {f.id: f for f in COMPOSED_FIXTURES}


class TestComposedFixtures(unittest.TestCase):
    """Test two-step pattern composition via ComposedFixture."""
    pass  # Tests are added dynamically below


def _make_composed_test(fixture: ComposedFixture):
    """Create a test method for a composed fixture."""

    def test_method(self):
        pattern_a = get_pattern(fixture.stage_a_pattern)
        pattern_b = get_pattern(fixture.stage_b_pattern)

        # Build context from seeded source
        ctx = make_context(fixture.seeded_source, fixture.func_name, fixture.diagnosis)
        ctx.compiler_dialect = fixture.compiler_dialect

        # Stage A: generate variants, find one containing intermediate text
        a_variants = list(pattern_a.generate(ctx))
        self.assertGreater(
            len(a_variants), 0,
            f"Stage A pattern '{fixture.stage_a_pattern}' generated 0 variants",
        )

        # Find intermediate variant
        intermediate = None
        for v in a_variants:
            if normalize(fixture.intermediate_contains) in normalize(v.source):
                intermediate = v
                break

        self.assertIsNotNone(
            intermediate,
            f"No stage A variant contains '{fixture.intermediate_contains}'. "
            f"Got {len(a_variants)} variants:\n"
            + "\n".join(f"  {normalize(v.source)[:100]}" for v in a_variants[:5]),
        )

        # Re-parse intermediate
        reparsed = reparse_variant(ctx, intermediate.source)

        # Stage B: generate variants, find one matching expected
        b_variants = list(pattern_b.generate(reparsed))
        self.assertGreater(
            len(b_variants), 0,
            f"Stage B pattern '{fixture.stage_b_pattern}' generated 0 variants "
            f"from intermediate source",
        )

        matched = any(
            match_variant(v.source, fixture.expected_source, fixture.match_mode)
            for v in b_variants
        )

        if not matched:
            norm_expected = normalize(fixture.expected_source)
            closest = min(
                b_variants,
                key=lambda v: -_similarity(normalize(v.source), norm_expected),
            )
            self.fail(
                f"\nComposed fixture '{fixture.id}': no final variant matched.\n"
                f"  Expected (normalized): {norm_expected}\n"
                f"  Closest  (normalized): {normalize(closest.source)}\n"
                f"  Stage B variants: {len(b_variants)}"
            )

    test_method.__doc__ = f"{fixture.id}: {fixture.description}"
    return test_method


# Attach a test method per composed fixture
for _cfixture in COMPOSED_FIXTURES:
    _ctest_name = f"test_{_cfixture.id}"
    setattr(TestComposedFixtures, _ctest_name, _make_composed_test(_cfixture))


if __name__ == "__main__":
    unittest.main()
