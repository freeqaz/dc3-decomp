"""Tests for loop_body_assign_hoist pattern.

The pattern hoists a post-call assignment to before the call when the two
statements are independent within a loop body. Canonical example from
RndBitmap::Load: ``workingMip = newMip`` moved before ``newMip->Create()``.
"""

from __future__ import annotations

import unittest

import scripts.permuter.patterns  # noqa: F401 — triggers registration
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    diag_with_clusters,
    diag_with_offset_deltas,
    make_context,
    match_variant,
)
from scripts.permuter.types import Cluster, Diagnosis, DiffOp


def _diag_with_mr_mismatch() -> Diagnosis:
    """Diagnosis with mr (register move) mismatch adjacent to bl (call)."""
    from scripts.permuter.types import SwapInfo
    d = Diagnosis(
        total_instructions=80,
        match_counts={"match": 70, "mismatch": 10},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[
            DiffOp(index=5, target_opcode="mr", base_opcode="stw"),
            DiffOp(index=6, target_opcode="bl", base_opcode="bl"),
        ],
        clusters=[Cluster(start_idx=4, end_idx=8, size=4, inserts=2, deletes=2)],
        noise_explained=0,
        noise_total=0,
    )
    return d


class TestLoopBodyAssignHoistRelevance(unittest.TestCase):
    """Test the relevant() and priority() gates."""

    def setUp(self):
        self.pattern = get_pattern("loop_body_assign_hoist")

    def test_relevant_with_clusters(self):
        self.assertTrue(self.pattern.relevant(diag_with_clusters()))

    def test_relevant_with_offset_deltas(self):
        self.assertTrue(self.pattern.relevant(diag_with_offset_deltas()))

    def test_relevant_with_mr_mismatch(self):
        self.assertTrue(self.pattern.relevant(_diag_with_mr_mismatch()))

    def test_not_relevant_with_empty_diagnosis(self):
        d = Diagnosis(
            total_instructions=100,
            match_counts={"match": 100},
            reg_swap_pairs={},
            offset_deltas={},
            diff_ops=[],
            clusters=[],
            noise_explained=0,
            noise_total=0,
        )
        self.assertFalse(self.pattern.relevant(d))

    def test_priority_clusters_is_high(self):
        p = self.pattern.priority(diag_with_clusters())
        self.assertGreaterEqual(p, 0.4)

    def test_priority_zero_when_not_relevant(self):
        d = Diagnosis(
            total_instructions=100,
            match_counts={"match": 100},
            reg_swap_pairs={},
            offset_deltas={},
            diff_ops=[],
            clusters=[],
            noise_explained=0,
            noise_total=0,
        )
        self.assertEqual(self.pattern.priority(d), 0.0)


class TestLoopBodyAssignHoistPositive(unittest.TestCase):
    """Tests where the pattern SHOULD generate a hoisted variant."""

    def setUp(self):
        self.pattern = get_pattern("loop_body_assign_hoist")

    def test_hoist_assignment_after_call_independent_vars(self):
        """Hoist when assignment target is INDEPENDENT from the call's variable."""
        # state = nextState does NOT conflict with cur->Process()
        # because `state` is not read by Process and Process does not write `state`
        ctx = make_context(
            """\
void test_func() {
    Foo *cur = head;
    int state = 0;
    int nextState = 1;
    while (cur) {
        cur->Process();
        state = nextState;
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        # `state = nextState` is independent of `cur->Process()` — should hoist
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    Foo *cur = head;
    int state = 0;
    int nextState = 1;
    while (cur) {
        state = nextState;
        cur->Process();
    }
}
""",
                    "normalized",
                )
                for v in variants
            ),
            f"Expected hoisted variant not found. Got {len(variants)} variant(s): "
            + str([v.description for v in variants]),
        )

    def test_hoist_assignment_two_after_call_in_while_loop(self):
        """The Bitmap::Load shape: assignment 2 statements after call."""
        ctx = make_context(
            """\
void test_func() {
    Mip *workingMip = this;
    while (mipCt--) {
        Mip *newMip = new Mip();
        workingMip->mMip = newMip;
        newMip->Create(workingW, workingH);
        ReadChunks(bs, newMip->mPixels);
        workingMip = newMip;
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        # Should generate variant where `workingMip = newMip` appears before Create()
        hoisted = [
            v for v in variants
            if b"workingMip = newMip" in v.source
            and v.source.index(b"workingMip = newMip") < v.source.index(b"newMip->Create")
        ]
        self.assertTrue(
            len(hoisted) > 0,
            f"Expected hoisted variant not found. Got {len(variants)} variant(s)."
        )

    def test_hoist_in_for_loop(self):
        """Pattern works inside a for loop body — independent variables."""
        # `accum = val` is independent of `arr[i]->Tick()` — no variable overlap
        ctx = make_context(
            """\
void test_func() {
    int accum = 0;
    int val = 5;
    for (int i = 0; i < n; i++) {
        arr[i]->Tick();
        accum = val;
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    int accum = 0;
    int val = 5;
    for (int i = 0; i < n; i++) {
        accum = val;
        arr[i]->Tick();
    }
}
""",
                    "normalized",
                )
                for v in variants
            ),
            f"Expected hoisted variant not found. Got {len(variants)} variant(s).",
        )

    def test_hoist_in_do_while_loop(self):
        """Pattern works inside a do-while loop body — independent variables."""
        ctx = make_context(
            """\
void test_func() {
    Obj *cur = start;
    int count = 0;
    do {
        cur->Update();
        count = total;
    } while (count < max);
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    Obj *cur = start;
    int count = 0;
    do {
        count = total;
        cur->Update();
    } while (count < max);
}
""",
                    "normalized",
                )
                for v in variants
            ),
            f"Expected hoisted variant not found. Got {len(variants)} variant(s).",
        )

    def test_variants_tagged_with_loop_body(self):
        """Generated variants should carry the loop_body tag."""
        ctx = make_context(
            """\
void test_func() {
    int x = 0;
    int y = 1;
    while (n--) {
        obj->Run();
        x = y;
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(
            any("loop_body" in v.tags for v in variants),
            "No variant carries the loop_body tag.",
        )

    def test_bitmap_load_shape_exact(self):
        """Reproduce the exact RndBitmap::Load pre-fix shape and verify variant."""
        # The pre-fix source had workingMip = newMip AFTER Create() and ReadChunks()
        pre_fix = """\
void Load(void) {
    RndBitmap *workingMip = this;
    while (mipCt--) {
        RndBitmap *newMip = new RndBitmap();
        workingMip->mMip = newMip;
        workingW = workingW >> 1;
        workingH = workingH >> 1;
        newMip->Create(workingW, workingH, 0, mBpp, mOrder, mPalette, 0, 0);
        ReadChunks(bs, newMip->mPixels, newMip->PixelBytes(), 0x8000);
        workingMip = newMip;
    }
}
"""
        ctx = make_context(pre_fix, "Load", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))

        # Validate at least one variant has workingMip = newMip before Create()
        hoisted = [
            v for v in variants
            if b"workingMip = newMip" in v.source
            and v.source.index(b"workingMip = newMip") < v.source.index(b"newMip->Create")
        ]
        self.assertGreater(
            len(hoisted),
            0,
            f"Pattern did not generate a hoisted variant for Bitmap::Load shape. "
            f"Got {len(variants)} total variants: "
            + str([v.description for v in variants]),
        )


class TestLoopBodyAssignHoistNegative(unittest.TestCase):
    """Tests where the pattern must NOT generate a hoisted variant."""

    def setUp(self):
        self.pattern = get_pattern("loop_body_assign_hoist")

    def test_no_hoist_outside_loop(self):
        """Assignment after call in a plain block (no loop) must not be hoisted."""
        ctx = make_context(
            """\
void test_func() {
    obj->Process();
    cur = obj->next;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        # Should produce zero variants — no loop body
        self.assertEqual(
            len(variants),
            0,
            f"Expected no variants (no loop), but got {len(variants)}.",
        )

    def test_no_hoist_when_assignment_depends_on_call_result(self):
        """Do not hoist when assignment RHS is the call's return value."""
        # dst = call(); is the pattern we must NOT hoist past another call
        ctx = make_context(
            """\
void test_func() {
    while (n--) {
        Foo *newFoo = obj->Create();
        dest = newFoo;
        SomeOtherCall();
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        # `dest = newFoo` depends on `newFoo = obj->Create()` — should NOT hoist
        # (newFoo is introduced by the preceding decl statement)
        bad = [
            v for v in variants
            if b"dest = newFoo" in v.source
            and v.source.index(b"dest = newFoo") < v.source.index(b"obj->Create")
        ]
        self.assertEqual(
            len(bad),
            0,
            f"Pattern incorrectly hoisted call-result-dependent assignment.",
        )

    def test_no_hoist_when_assignment_writes_call_read(self):
        """Do not hoist when the assignment writes a variable the call reads."""
        ctx = make_context(
            """\
void test_func() {
    int x = 0;
    int y = 1;
    while (n--) {
        ProcessValue(x);
        x = y;
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        # `ProcessValue(x)` reads `x`; hoisting `x = y` before it would
        # change the value that ProcessValue sees.
        # The pattern should detect the read-write conflict and skip.
        variants = list(self.pattern.generate(ctx))
        bad = [
            v for v in variants
            if b"x = y" in v.source
            and v.source.index(b"x = y") < v.source.index(b"ProcessValue(x)")
        ]
        self.assertEqual(
            len(bad),
            0,
            f"Pattern incorrectly hoisted assignment that changes call argument.",
        )

    def test_no_hoist_compound_assignment(self):
        """Compound assignments (+=, -=) must not be treated as simple assignments."""
        ctx = make_context(
            """\
void test_func() {
    int sum = 0;
    while (n--) {
        sum += GetValue();
        total += sum;
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        # `total += sum` is a compound assignment — should not trigger
        self.assertEqual(
            len(variants),
            0,
            f"Pattern incorrectly generated variant for compound assignment.",
        )

    def test_no_hoist_when_assignment_rhs_has_call(self):
        """Assignment with a call in RHS must not be hoisted."""
        ctx = make_context(
            """\
void test_func() {
    Foo *p = head;
    while (p) {
        p->Process();
        p = GetNext(p);
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        # `p = GetNext(p)` has a call in the RHS — classified as having side effects
        variants = list(self.pattern.generate(ctx))
        # The assignment has a call, so should not be hoisted by this pattern
        bad = [
            v for v in variants
            if b"p = GetNext(p)" in v.source
            and v.source.index(b"p = GetNext(p)") < v.source.index(b"p->Process")
        ]
        self.assertEqual(
            len(bad),
            0,
            f"Pattern incorrectly hoisted assignment with call in RHS.",
        )

    def test_no_duplicate_generation(self):
        """Pattern should not generate the same source twice."""
        ctx = make_context(
            """\
void test_func() {
    Node *p = head;
    while (p) {
        p->Tick();
        p = p->next;
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        variants = list(self.pattern.generate(ctx))
        sources = [v.source for v in variants]
        unique_sources = set(sources)
        self.assertEqual(
            len(sources),
            len(unique_sources),
            f"Pattern generated duplicate variants.",
        )


class TestLoopBodyAssignHoistComposability(unittest.TestCase):
    """Verify the pattern does not conflict with statement_reorder."""

    def test_pattern_names_distinct(self):
        from scripts.permuter.patterns.base import get_pattern
        hoist = get_pattern("loop_body_assign_hoist")
        reorder = get_pattern("statement_reorder")
        self.assertNotEqual(hoist.name, reorder.name)

    def test_follow_ups_include_statement_reorder(self):
        p = get_pattern("loop_body_assign_hoist")
        self.assertIn("statement_reorder", p.follow_ups)


if __name__ == "__main__":
    unittest.main()
