"""Tests for pointer_iter_unroll pattern."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    _empty_diag,
    make_context,
    normalize,
)
from scripts.permuter.types import Cluster, DiffOp, Diagnosis


# ---------------------------------------------------------------------------
# Diagnosis factories specific to pointer_iter_unroll
# ---------------------------------------------------------------------------

def _diag_with_unroll_cluster() -> Diagnosis:
    """A delete-heavy cluster carrying load/store opcodes — missing unroll."""
    d = _empty_diag()
    d.clusters = [
        Cluster(
            start_idx=5, end_idx=20, size=15, inserts=2, deletes=10,
            target_opcodes=("lwz", "stw", "lwz", "stw"),
        )
    ]
    return d


def _diag_with_big_delete_cluster() -> Diagnosis:
    """A large delete cluster with no opcode detail (signal A fallback)."""
    d = _empty_diag()
    d.clusters = [Cluster(start_idx=5, end_idx=20, size=15, inserts=1, deletes=8)]
    return d


def _diag_with_stfs_diffop() -> Diagnosis:
    """Target uses stfs where base does not — load/store diff op (signal B)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=12, target_opcode="stfs", base_opcode="bdnz")]
    return d


# ---------------------------------------------------------------------------
# Transformation tests
# ---------------------------------------------------------------------------

class TestPointerIterUnrollTransform(unittest.TestCase):
    """Test the fresh-iterator introduction transform."""

    def test_basic_transform(self):
        """A pointer-incrementing loop whose start is used after should transform.

        The fresh iterator drives the body + update; the original pointer is
        left untouched in the initializer/condition AND the post-loop use.
        """
        pattern = get_pattern("pointer_iter_unroll")
        ctx = make_context(
            """\
struct Vert { int pos; int uv; };
void test_func() {
    Vert *vertIt = mesh_begin();
    Vert *vertEnd = mesh_end();
    for (int i = vertEnd - vertIt; i > 0; --i, ++vertIt) {
        vertIt->pos = 1;
        vertIt->uv = 2;
    }
    Use(vertIt);
}
""",
            "test_func",
            _diag_with_unroll_cluster(),
        )

        variants = list(pattern.generate(ctx))
        self.assertEqual(len(variants), 1, "Expected exactly one variant")

        src = normalize(variants[0].source)
        # Fresh iterator declared before the loop, initialized from vertIt.
        self.assertIn("Vert *_it = vertIt;", src)
        # Update clause and body retargeted to the fresh iterator.
        self.assertIn("++_it", src)
        self.assertIn("_it->pos = 1;", src)
        self.assertIn("_it->uv = 2;", src)
        # Original pointer preserved in the trip-count expression...
        self.assertIn("vertEnd - vertIt", src)
        # ...and in the post-loop use.
        self.assertIn("Use(vertIt);", src)

    def test_qualified_type(self):
        """Namespace-qualified pointer type is resolved and used for the decl."""
        pattern = get_pattern("pointer_iter_unroll")
        ctx = make_context(
            """\
void test_func() {
    RndMesh::Vert *vertIt = begin();
    RndMesh::Vert *vertEnd = end();
    for (int i = vertEnd - vertIt; i > 0; --i, ++vertIt) {
        vertIt->pos = 0;
    }
    Finish(vertIt);
}
""",
            "test_func",
            _diag_with_unroll_cluster(),
        )

        variants = list(pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn("RndMesh::Vert *_it = vertIt;", normalize(variants[0].source))

    def test_postfix_and_compound_increment(self):
        """`i++, p++` picks the live pointer p; `p += 1` is also recognized."""
        pattern = get_pattern("pointer_iter_unroll")

        ctx_pp = make_context(
            """\
void test_func() {
    Foo *p = mStart;
    for (int i = 0; i < n; i++, p++) {
        p->x = 0;
    }
    mEnd = p;
}
""",
            "test_func",
            _diag_with_unroll_cluster(),
        )
        variants = list(pattern.generate(ctx_pp))
        self.assertEqual(len(variants), 1)
        src = normalize(variants[0].source)
        self.assertIn("Foo *_it = p;", src)
        self.assertIn("i++, _it++", src)
        # Loop counter i is untouched.
        self.assertIn("for (int i = 0; i < n; i++, _it++)", src)
        # Original p still drives the post-loop member store.
        self.assertIn("mEnd = p;", src)

        ctx_pe = make_context(
            """\
void test_func() {
    Foo *p = mStart;
    for (int i = 0; i < n; ++i, p += 1) {
        p->x = 0;
    }
    Bar(p);
}
""",
            "test_func",
            _diag_with_unroll_cluster(),
        )
        variants = list(pattern.generate(ctx_pe))
        self.assertEqual(len(variants), 1)
        self.assertIn("_it += 1", normalize(variants[0].source))


class TestPointerIterUnrollNoTransform(unittest.TestCase):
    """Cases where the pattern should not (or harmlessly need not) transform."""

    def test_pointer_not_used_after_loop(self):
        """If the pointer is dead at loop exit, do NOT transform.

        MWCC could already unroll, so adding a fresh local would be a no-op
        (or worse). The pattern declines to emit a variant.
        """
        pattern = get_pattern("pointer_iter_unroll")
        ctx = make_context(
            """\
struct Vert { int pos; };
void test_func() {
    Vert *p = begin();
    Vert *e = end();
    for (int i = e - p; i > 0; --i, ++p) {
        p->pos = 1;
    }
}
""",
            "test_func",
            _diag_with_unroll_cluster(),
        )

        variants = list(pattern.generate(ctx))
        self.assertEqual(
            len(variants), 0,
            "Should NOT transform when the pointer is dead at loop exit",
        )

    def test_no_pointer_increment(self):
        """A loop with only an integer-counter increment produces no variant."""
        pattern = get_pattern("pointer_iter_unroll")
        ctx = make_context(
            """\
void test_func() {
    int total = 0;
    for (int i = 0; i < n; ++i) {
        total += arr[i];
    }
    Use(total);
}
""",
            "test_func",
            _diag_with_unroll_cluster(),
        )

        variants = list(pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_unknown_type_skips_on_mwcc(self):
        """When the pointer's declaration type can't be found, mwcc skips.

        Emitting an `auto`/typeless decl would just fail to compile, so the
        default (mwcc) dialect declines rather than produce broken source.
        """
        pattern = get_pattern("pointer_iter_unroll")
        ctx = make_context(
            """\
void test_func() {
    for (int i = 0; i < n; ++i, ++p) {
        p->x = 0;
    }
    Use(p);
}
""",
            "test_func",
            _diag_with_unroll_cluster(),
        )
        # p is never declared in this function body — type can't be resolved.
        variants = list(pattern.generate(ctx))
        self.assertEqual(len(variants), 0)


# ---------------------------------------------------------------------------
# Relevance tests
# ---------------------------------------------------------------------------

class TestRelevance(unittest.TestCase):
    """Test relevant()/priority() gating."""

    def test_relevant_with_unroll_cluster(self):
        pattern = get_pattern("pointer_iter_unroll")
        self.assertTrue(pattern.relevant(_diag_with_unroll_cluster()))

    def test_relevant_with_big_delete_cluster(self):
        pattern = get_pattern("pointer_iter_unroll")
        self.assertTrue(pattern.relevant(_diag_with_big_delete_cluster()))

    def test_relevant_with_loadstore_diffop(self):
        pattern = get_pattern("pointer_iter_unroll")
        self.assertTrue(pattern.relevant(_diag_with_stfs_diffop()))

    def test_not_relevant_empty_diagnosis(self):
        """Empty diagnosis — not always-True, so it must gate off."""
        pattern = get_pattern("pointer_iter_unroll")
        self.assertFalse(pattern.relevant(_empty_diag()))

    def test_not_relevant_insert_only_cluster(self):
        """A small insert-only cluster (we emit extra) is not an unroll signal."""
        pattern = get_pattern("pointer_iter_unroll")
        d = _empty_diag()
        d.clusters = [Cluster(start_idx=5, end_idx=8, size=3, inserts=3, deletes=0)]
        self.assertFalse(pattern.relevant(d))

    def test_priority_higher_with_opcode_cluster(self):
        """An opcode-tagged delete cluster gives higher priority than a bare one."""
        pattern = get_pattern("pointer_iter_unroll")
        hi = pattern.priority(_diag_with_unroll_cluster())
        lo = pattern.priority(_diag_with_big_delete_cluster())
        self.assertGreater(hi, lo)
        self.assertGreater(lo, 0.0)

    def test_priority_zero_when_irrelevant(self):
        pattern = get_pattern("pointer_iter_unroll")
        self.assertEqual(pattern.priority(_empty_diag()), 0.0)

    def test_context_priority_bumps_with_pointer_loop(self):
        """context_priority bumps when a pointer-incrementing loop is present."""
        pattern = get_pattern("pointer_iter_unroll")
        diag = _diag_with_unroll_cluster()
        ctx = make_context(
            """\
void test_func() {
    Foo *p = mStart;
    for (int i = 0; i < n; ++i, ++p) {
        p->x = 0;
    }
    Use(p);
}
""",
            "test_func",
            diag,
        )
        self.assertGreater(
            pattern.context_priority(diag, ctx),
            pattern.priority(diag),
        )


# ---------------------------------------------------------------------------
# Pattern metadata and registration
# ---------------------------------------------------------------------------

class TestPatternMetadata(unittest.TestCase):
    """Verify pattern registration and metadata."""

    def test_pattern_registered(self):
        pattern = get_pattern("pointer_iter_unroll")
        self.assertEqual(pattern.name, "pointer_iter_unroll")

    def test_not_opt_in(self):
        pattern = get_pattern("pointer_iter_unroll")
        self.assertFalse(pattern.opt_in)

    def test_safety_and_domain(self):
        pattern = get_pattern("pointer_iter_unroll")
        self.assertEqual(pattern.safety_tier, "moderate")
        self.assertEqual(pattern.structural_domain, "data_flow")

    def test_in_pattern_registry(self):
        from scripts.permuter.patterns.base import list_patterns
        all_patterns = list_patterns(include_opt_in=True)
        self.assertIn("pointer_iter_unroll", all_patterns)


if __name__ == "__main__":
    unittest.main()
