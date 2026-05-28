"""Tests for the ``loop_var_hoist`` pattern.

The pattern hoists loop-invariant ``T name = INIT;`` declarations OUT of a
loop body to just BEFORE the enclosing loop. Inverse direction (sink): a
declaration JUST BEFORE a loop with a single use inside is moved INTO the
loop body.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

import scripts.permuter.patterns  # noqa: F401 — triggers registration
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    diag_with_clusters,
    diag_with_lwz_ops,
    make_context,
)
from scripts.permuter.types import Diagnosis, DiffOp


def _empty_diag() -> Diagnosis:
    return Diagnosis(
        total_instructions=100,
        match_counts={"match": 100},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[],
        noise_explained=0,
        noise_total=0,
    )


def _diag_with_mr_ops() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="mr", base_opcode="lwz")]
    return d


# ---------------------------------------------------------------------------
# Relevance / priority
# ---------------------------------------------------------------------------


class TestLoopVarHoistRelevance(unittest.TestCase):
    """Test the relevant() / priority() gates."""

    def setUp(self):
        self.pattern = get_pattern("loop_var_hoist")

    def test_relevant_with_clusters(self):
        self.assertTrue(self.pattern.relevant(diag_with_clusters()))

    def test_relevant_with_lwz_diff(self):
        self.assertTrue(self.pattern.relevant(diag_with_lwz_ops()))

    def test_relevant_with_mr_diff(self):
        self.assertTrue(self.pattern.relevant(_diag_with_mr_ops()))

    def test_not_relevant_empty(self):
        self.assertFalse(self.pattern.relevant(_empty_diag()))

    def test_priority_is_05_when_relevant(self):
        # Per spec: priority 0.5
        self.assertEqual(self.pattern.priority(diag_with_clusters()), 0.5)
        self.assertEqual(self.pattern.priority(diag_with_lwz_ops()), 0.5)

    def test_priority_zero_when_not_relevant(self):
        self.assertEqual(self.pattern.priority(_empty_diag()), 0.0)


# ---------------------------------------------------------------------------
# Positive: pattern SHOULD generate a hoist variant
# ---------------------------------------------------------------------------


class TestLoopVarHoistPositive(unittest.TestCase):
    """Positive cases — the pattern SHOULD generate a hoist variant."""

    def setUp(self):
        self.pattern = get_pattern("loop_var_hoist")

    def test_hoist_const_decl_in_for(self):
        """const decl with member-like access is invariant — should hoist."""
        src = """\
void test_func() {
    for (int i = 0; i < mCount; i++) {
        const int maxFret = mMaxFret + mOffset;
        DoStuff(mGems, maxFret);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        hoisted = [
            v for v in variants
            if b"const int maxFret" in v.source
            and v.source.index(b"const int maxFret")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertGreater(
            len(hoisted), 0,
            f"Expected a hoist variant; got {len(variants)}: "
            + str([v.description for v in variants]),
        )

    def test_hoist_in_while(self):
        """Invariant decl in a while loop hoists out."""
        src = """\
void test_func() {
    int i = 0;
    while (i < 10) {
        int total = mLimit + mBase;
        Use(total);
        i++;
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        hoisted = [
            v for v in variants
            if b"int total = mLimit + mBase" in v.source
            and v.source.index(b"int total = mLimit + mBase")
            < v.source.index(b"while (i < 10)")
        ]
        self.assertGreater(
            len(hoisted), 0,
            f"Expected hoist out of while loop; got {len(variants)}.",
        )

    def test_hoist_in_do_while(self):
        """Invariant decl in a do-while loop hoists out."""
        src = """\
void test_func() {
    int i = 0;
    do {
        int sum = mA + mB;
        Use(sum);
        i++;
    } while (i < 5);
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        hoisted = [
            v for v in variants
            if b"int sum = mA + mB" in v.source
            and v.source.index(b"int sum = mA + mB")
            < v.source.index(b"do {")
        ]
        self.assertGreater(
            len(hoisted), 0,
            f"Expected hoist out of do-while loop; got {len(variants)}.",
        )

    def test_hoist_multi_use(self):
        """Decl used multiple times across iterations is still hoistable."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        const int v = mA + mB;
        Cons(v);
        Prod(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        hoisted = [
            v for v in variants
            if b"const int v = mA + mB" in v.source
            and v.source.index(b"const int v = mA + mB")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertGreater(
            len(hoisted), 0,
            f"Multi-use invariant should hoist; got {len(variants)}.",
        )

    def test_hoist_non_const_single_assignment(self):
        """Non-const decl with no other writes is still hoistable."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        int v = mA + mB;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        hoisted = [
            v for v in variants
            if b"int v = mA + mB" in v.source
            and v.source.index(b"int v = mA + mB")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertGreater(
            len(hoisted), 0,
            f"Non-const single-assignment invariant should hoist; "
            f"got {len(variants)}.",
        )

    def test_hoist_using_function_parameter(self):
        """Decl initialized from a parameter is invariant."""
        src = """\
void test_func(int base) {
    for (int i = 0; i < 10; i++) {
        const int v = base + mA;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        hoisted = [
            v for v in variants
            if b"const int v = base + mA" in v.source
            and v.source.index(b"const int v = base + mA")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertGreater(
            len(hoisted), 0,
            f"Decl using parameter should hoist; got {len(variants)}.",
        )

    def test_variants_tagged(self):
        """Generated hoist variants carry the 'loop_var_hoist' tag."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        const int v = mA + mB;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(
            any("loop_var_hoist" in v.tags for v in variants),
            "No variant carries the loop_var_hoist tag.",
        )


# ---------------------------------------------------------------------------
# Negative: pattern must NOT hoist
# ---------------------------------------------------------------------------


class TestLoopVarHoistNegative(unittest.TestCase):
    """Negative cases — the pattern must NOT hoist."""

    def setUp(self):
        self.pattern = get_pattern("loop_var_hoist")

    def test_no_hoist_when_init_uses_loop_var(self):
        """INIT referencing the loop variable is NOT invariant."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        const int v = mArr + i;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        bad = [
            v for v in variants
            if b"const int v = mArr + i" in v.source
            and v.source.index(b"const int v = mArr + i")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertEqual(
            len(bad), 0,
            "Pattern hoisted a decl that uses the loop variable.",
        )

    def test_no_hoist_when_init_uses_later_local(self):
        """INIT referencing a local declared LATER in body must not hoist."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        const int v = later + mA;
        int later = mB;
        Use(v);
        Use(later);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        bad = [
            v for v in variants
            if b"const int v = later + mA" in v.source
            and v.source.index(b"const int v = later + mA")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertEqual(
            len(bad), 0,
            "Pattern hoisted a decl referencing a later local.",
        )

    def test_no_hoist_when_reassigned_in_body(self):
        """A var written again later in the loop body is not invariant."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        int v = mA + mB;
        v += i;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        bad = [
            v for v in variants
            if b"int v = mA + mB" in v.source
            and v.source.index(b"int v = mA + mB")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertEqual(
            len(bad), 0,
            "Pattern hoisted a decl that gets reassigned in body.",
        )

    def test_no_hoist_when_init_has_side_effect_call(self):
        """RHS with a call that is NOT a pure accessor must not hoist."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        int v = RunSomething();
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        bad = [
            v for v in variants
            if b"int v = RunSomething()" in v.source
            and v.source.index(b"int v = RunSomething()")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertEqual(
            len(bad), 0,
            "Pattern hoisted a decl initialized via a non-pure call.",
        )

    def test_no_hoist_when_no_loop(self):
        """No loops in function = no variants emitted."""
        src = """\
void test_func() {
    const int v = mA + mB;
    Use(v);
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(
            len(variants), 0,
            f"Expected no variants (no loop); got {len(variants)}.",
        )

    def test_no_hoist_unknown_identifier(self):
        """Identifier with unknown provenance (not param/outer/member) blocks."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        const int v = something_random;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        bad = [
            v for v in variants
            if b"const int v = something_random" in v.source
            and v.source.index(b"const int v = something_random")
            < v.source.index(b"for (int i = 0")
        ]
        self.assertEqual(
            len(bad), 0,
            "Pattern hoisted a decl referencing an unknown identifier.",
        )


# ---------------------------------------------------------------------------
# Idempotence + integrity
# ---------------------------------------------------------------------------


class TestLoopVarHoistIdempotence(unittest.TestCase):
    """Pattern must not duplicate or corrupt source."""

    def setUp(self):
        self.pattern = get_pattern("loop_var_hoist")

    def test_hoist_does_not_duplicate(self):
        """Hoist variants must contain the declaration exactly once."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        const int v = mA + mB;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        for v in variants:
            self.assertEqual(
                v.source.count(b"const int v = mA + mB"), 1,
                f"Variant '{v.name}' duplicated the declaration.",
            )

    def test_idempotent_when_already_outside(self):
        """If a decl is already outside the loop, no new hoist variant."""
        src = """\
void test_func() {
    const int v = mA + mB;
    for (int i = 0; i < 10; i++) {
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        # All variants are either sinks or have the decl still in one place
        for v in variants:
            self.assertEqual(
                v.source.count(b"const int v = mA + mB"), 1,
                f"Variant '{v.name}' duplicated the declaration.",
            )


# ---------------------------------------------------------------------------
# Inverse direction: sink
# ---------------------------------------------------------------------------


class TestLoopVarHoistSink(unittest.TestCase):
    """Inverse-direction (sink) coverage."""

    def setUp(self):
        self.pattern = get_pattern("loop_var_hoist")

    def test_sink_pre_loop_single_use(self):
        """Pre-loop decl with a single use inside loop should also sink."""
        src = """\
void test_func() {
    const int v = mA + mB;
    for (int i = 0; i < 10; i++) {
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        sunk = [v for v in variants if "sink" in v.tags]
        self.assertGreater(
            len(sunk), 0,
            f"Expected a sink variant; got {len(variants)}: "
            + str([v.description for v in variants]),
        )

    def test_no_sink_when_used_after_loop(self):
        """Pre-loop decl used AFTER the loop must not sink."""
        src = """\
void test_func() {
    const int v = mA + mB;
    for (int i = 0; i < 10; i++) {
        Use(v);
    }
    Use(v);
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        sunk = [v for v in variants if "sink" in v.tags]
        self.assertEqual(
            len(sunk), 0,
            "Pattern sunk a decl that is used after the loop.",
        )


# ---------------------------------------------------------------------------
# Budget cap and duplicate guard
# ---------------------------------------------------------------------------


class TestLoopVarHoistBudget(unittest.TestCase):
    """Variant count cap and uniqueness."""

    def setUp(self):
        self.pattern = get_pattern("loop_var_hoist")

    def test_variant_cap(self):
        """Pattern emits at most 6 variants even for many candidates."""
        body_lines = []
        for n in range(10):
            body_lines.append(f"        const int v{n} = mA + mB;")
            body_lines.append(f"        Use(v{n});")
        body = "\n".join(body_lines)
        src = (
            "void test_func() {\n"
            "    for (int i = 0; i < 10; i++) {\n"
            f"{body}\n"
            "    }\n"
            "}\n"
        )
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        self.assertLessEqual(
            len(variants), 6,
            f"Pattern exceeded variant cap: got {len(variants)}.",
        )

    def test_no_duplicate_variants(self):
        """Each emitted variant must have a unique source."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        const int v = mA + mB;
        Use(v);
    }
}
"""
        ctx = make_context(src, "test_func", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        sources = [v.source for v in variants]
        self.assertEqual(
            len(sources), len(set(sources)),
            "Pattern generated duplicate variants.",
        )


if __name__ == "__main__":
    unittest.main()
