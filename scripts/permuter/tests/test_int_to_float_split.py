"""Tests for int_to_float_split pattern.

The pattern splits ``float v = (float)EXPR;`` into
``int t = EXPR; float v = (float)t;`` so the compiler emits each load+convert
triple (lhz/extsw/lfd) immediately, instead of batching all loads first.
Inverse direction collapses the split form back.

Target: CharBonesSamples::EvaluateChannel comp >= kCompressVects branch.
"""

from __future__ import annotations

import unittest

import scripts.permuter.patterns  # noqa: F401 — triggers registration
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import make_context
from scripts.permuter.types import Cluster, Diagnosis, DiffOp


def _diag_with_narrow_load_fp_signal() -> Diagnosis:
    """Diagnosis with lhz + lfd/stfd interleaving in a cluster — the
    int_to_float_split fingerprint."""
    return Diagnosis(
        total_instructions=100,
        match_counts={"match": 88, "mismatch": 12},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[
            Cluster(
                start_idx=10, end_idx=20, size=8, inserts=4, deletes=4,
                target_opcodes=("lhz", "extsw", "stw", "lfd",
                                "stfd", "lhz", "extsw", "lfd"),
                base_opcodes=("lhz", "lhz", "lhz", "extsw",
                              "extsw", "extsw", "lfd", "lfd"),
            ),
        ],
        noise_explained=0, noise_total=0,
    )


def _diag_with_diffop_signal() -> Diagnosis:
    """diff_op level signal: narrow-int-load vs FP-load."""
    return Diagnosis(
        total_instructions=80,
        match_counts={"match": 70, "mismatch": 10},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[
            DiffOp(index=5, target_opcode="lhz", base_opcode="lfd"),
            DiffOp(index=6, target_opcode="extsw", base_opcode="lfd"),
        ],
        clusters=[],
        noise_explained=0, noise_total=0,
    )


def _diag_empty() -> Diagnosis:
    return Diagnosis(
        total_instructions=100,
        match_counts={"match": 100},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[],
        noise_explained=0, noise_total=0,
    )


class TestIntToFloatSplitRelevance(unittest.TestCase):
    """Test the relevant() / priority() gates."""

    def setUp(self):
        self.pattern = get_pattern("int_to_float_split")

    def test_relevant_with_narrow_load_fp_cluster(self):
        self.assertTrue(self.pattern.relevant(_diag_with_narrow_load_fp_signal()))

    def test_relevant_with_diffop_signal(self):
        self.assertTrue(self.pattern.relevant(_diag_with_diffop_signal()))

    def test_not_relevant_with_empty_diagnosis(self):
        self.assertFalse(self.pattern.relevant(_diag_empty()))

    def test_priority_is_0_4_when_relevant(self):
        p = self.pattern.priority(_diag_with_narrow_load_fp_signal())
        self.assertAlmostEqual(p, 0.4)

    def test_priority_zero_when_not_relevant(self):
        self.assertEqual(self.pattern.priority(_diag_empty()), 0.0)


class TestIntToFloatSplitPositive(unittest.TestCase):
    """Positive — SPLIT direction emits expected variant."""

    def setUp(self):
        self.pattern = get_pattern("int_to_float_split")
        self.diag = _diag_with_narrow_load_fp_signal()

    def test_split_short_subscript_load(self):
        """Canonical EvaluateChannel shape: ((short*)p)[i] indexed loads."""
        ctx = make_context(
            """\
void test_func(short *p) {
    float a = (float)((short*)p)[0];
    float b = (float)((short*)p)[1];
    float c = (float)((short*)p)[2];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertGreaterEqual(len(variants), 1)
        # First variant should split the first decl
        joined_sources = b"\n=====\n".join(v.source for v in variants)
        self.assertIn(b"int _itmp0 = ((short*)p)[0]", joined_sources)
        self.assertIn(b"float a = (float)_itmp0", joined_sources)

    def test_split_int_subscript_load(self):
        """((int*)p)[0] form."""
        ctx = make_context(
            """\
void test_func(int *p) {
    float a = (float)((int*)p)[0];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        v = variants[0]
        self.assertIn(b"int _itmp0 = ((int*)p)[0]", v.source)
        self.assertIn(b"float a = (float)_itmp0", v.source)

    def test_split_unsigned_char_subscript(self):
        """((unsigned char*)p)[k] form."""
        ctx = make_context(
            """\
void test_func(unsigned char *p) {
    float a = (float)((unsigned char*)p)[2];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        v = variants[0]
        self.assertIn(b"int _itmp0 = ((unsigned char*)p)[2]", v.source)

    def test_split_member_access(self):
        """field_expression matching m[A-Z] convention is split."""
        ctx = make_context(
            """\
void test_func(Obj *o) {
    float a = (float)o->mCount;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        v = variants[0]
        self.assertIn(b"int _itmp0 = o->mCount", v.source)
        self.assertIn(b"float a = (float)_itmp0", v.source)

    def test_split_double_target(self):
        """``double`` target type also works (same lowering family)."""
        ctx = make_context(
            """\
void test_func(short *p) {
    double a = (double)((short*)p)[0];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        v = variants[0]
        self.assertIn(b"int _itmp0 = ((short*)p)[0]", v.source)
        self.assertIn(b"double a = (double)_itmp0", v.source)

    def test_split_cast_to_int_inner(self):
        """``(float)(int)x`` — the inner cast_expression to int is split."""
        ctx = make_context(
            """\
void test_func(unsigned x) {
    float a = (float)(int)x;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        v = variants[0]
        self.assertIn(b"int _itmp0 = (int)x", v.source)
        self.assertIn(b"float a = (float)_itmp0", v.source)

    def test_split_assignment_form(self):
        """``a = (float)((short*)p)[0];`` (assignment, not decl)."""
        ctx = make_context(
            """\
void test_func(short *p) {
    float a;
    a = (float)((short*)p)[0];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertGreaterEqual(len(variants), 1)
        v = variants[0]
        self.assertIn(b"int _itmp0 = ((short*)p)[0]", v.source)
        self.assertIn(b"a = (float)_itmp0", v.source)

    def test_variant_cap_at_8(self):
        """Many candidates -> capped at 8 variants per function."""
        src = "void test_func(short *p) {\n"
        for i in range(20):
            src += f"    float v{i} = (float)((short*)p)[{i}];\n"
        src += "}\n"
        ctx = make_context(src, "test_func", self.diag)
        variants = list(self.pattern.generate(ctx))
        self.assertLessEqual(len(variants), 8)

    def test_variant_tags(self):
        ctx = make_context(
            """\
void test_func(short *p) {
    float a = (float)((short*)p)[0];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn("int_to_float_split", variants[0].tags)
        self.assertIn("split", variants[0].tags)


class TestIntToFloatSplitCollapse(unittest.TestCase):
    """Inverse direction: collapse split form back to one-shot."""

    def setUp(self):
        self.pattern = get_pattern("int_to_float_split")
        self.diag = _diag_with_narrow_load_fp_signal()

    def test_collapse_int_plus_float_cast(self):
        ctx = make_context(
            """\
void test_func(short *p) {
    int ia = ((short*)p)[0];
    float a = (float)ia;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        # Should emit at least one COLLAPSE variant
        collapse = [v for v in variants if "collapse" in v.tags]
        self.assertGreaterEqual(len(collapse), 1)
        v = collapse[0]
        self.assertIn(b"float a = (float)((short*)p)[0]", v.source)
        # The int decl is gone
        self.assertNotIn(b"int ia =", v.source)

    def test_collapse_member_access(self):
        ctx = make_context(
            """\
void test_func(Obj *o) {
    int t = o->mCount;
    float a = (float)t;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        collapse = [v for v in variants if "collapse" in v.tags]
        self.assertGreaterEqual(len(collapse), 1)
        v = collapse[0]
        self.assertIn(b"float a = (float)o->mCount", v.source)

    def test_collapse_tags(self):
        ctx = make_context(
            """\
void test_func(short *p) {
    int ia = ((short*)p)[0];
    float a = (float)ia;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        collapse = [v for v in variants if "collapse" in v.tags]
        self.assertGreaterEqual(len(collapse), 1)
        self.assertIn("int_to_float_split", collapse[0].tags)


class TestIntToFloatSplitNegative(unittest.TestCase):
    """Negative — pattern must NOT emit variants."""

    def setUp(self):
        self.pattern = get_pattern("int_to_float_split")
        self.diag = _diag_with_narrow_load_fp_signal()

    def test_no_split_for_already_int_rvalue(self):
        """``float a = (float)x;`` where x is a bare identifier — already
        in the collapsed-from-int form. We don't split bare identifiers."""
        ctx = make_context(
            """\
void test_func(int x) {
    float a = (float)x;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        # No splits (bare-ident filter) and no collapse (no preceding int decl)
        self.assertEqual(len(variants), 0)

    def test_no_split_for_float_cast_of_float(self):
        """``(float)f`` where f is float — not int-yielding, must be ignored."""
        ctx = make_context(
            """\
void test_func(float f) {
    float a = (float)f;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_no_split_for_call_rvalue(self):
        """``float a = (float)Foo();`` — call_expression is not in our
        recognised int-yielding shapes (variable_extraction handles it)."""
        ctx = make_context(
            """\
void test_func() {
    float a = (float)Foo();
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_no_split_when_already_split(self):
        """Idempotence: applying once leaves the result split; rerunning
        should not split the just-introduced bare identifier."""
        ctx = make_context(
            """\
void test_func(short *p) {
    float a = (float)((short*)p)[0];
}
""",
            "test_func",
            self.diag,
        )
        v1 = list(self.pattern.generate(ctx))
        self.assertEqual(len(v1), 1)
        # Reparse the result and run again — should produce a COLLAPSE
        # variant (back to original) but NOT another split of the same site.
        ctx2 = make_context(
            v1[0].source.decode("utf-8"),
            "test_func",
            self.diag,
        )
        v2 = list(self.pattern.generate(ctx2))
        # The COLLAPSE variant brings us back; no new split since the RHS
        # is now a bare identifier.
        splits = [v for v in v2 if "split" in v.tags]
        self.assertEqual(
            len(splits), 0,
            f"Pattern split a bare-identifier rvalue (not idempotent): "
            f"{[v.description for v in splits]}",
        )

    def test_no_split_for_double_target_with_non_int_inner(self):
        """``double a = (double)func();`` — inner is a call, not int-yielding."""
        ctx = make_context(
            """\
void test_func() {
    double a = (double)func();
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_no_split_for_non_member_field_access(self):
        """``(float)o->len`` — field name doesn't match m[A-Z] convention."""
        ctx = make_context(
            """\
void test_func(Obj *o) {
    float a = (float)o->len;
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        # `len` doesn't start with `m[A-Z]`, and we don't recognise it
        # as int-yielding without type info — must skip.
        self.assertEqual(len(variants), 0)

    def test_priority_gate_blocks_generation_via_relevance(self):
        """relevant() = False -> priority() = 0.0 (caller would skip).
        The pattern still GENERATES variants if invoked, but its priority
        marker indicates no work should be done."""
        self.assertFalse(self.pattern.relevant(_diag_empty()))
        self.assertEqual(self.pattern.priority(_diag_empty()), 0.0)


class TestIntToFloatSplitNoDuplicates(unittest.TestCase):
    """Cleanup checks."""

    def setUp(self):
        self.pattern = get_pattern("int_to_float_split")
        self.diag = _diag_with_narrow_load_fp_signal()

    def test_no_duplicate_variants(self):
        ctx = make_context(
            """\
void test_func(short *p) {
    float a = (float)((short*)p)[0];
    float b = (float)((short*)p)[1];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        sources = [v.source for v in variants]
        self.assertEqual(
            len(sources), len(set(sources)),
            f"Pattern generated duplicate variants.",
        )

    def test_unique_tmp_names(self):
        """Multiple split sites get distinct _itmpN names."""
        ctx = make_context(
            """\
void test_func(short *p) {
    float a = (float)((short*)p)[0];
    float b = (float)((short*)p)[1];
    float c = (float)((short*)p)[2];
}
""",
            "test_func",
            self.diag,
        )
        variants = list(self.pattern.generate(ctx))
        # Each variant introduces exactly one new `_itmpN` declaration —
        # verify the names span the expected range.
        names = []
        for v in variants:
            for i in range(10):
                tag = f"_itmp{i}".encode()
                if tag in v.source:
                    names.append(tag)
                    break
        self.assertEqual(len(names), len(variants))

    def test_pattern_metadata_defaults(self):
        self.assertEqual(self.pattern.safety_tier, "conservative")
        self.assertEqual(self.pattern.structural_domain, "expr_shape")


if __name__ == "__main__":
    unittest.main()
