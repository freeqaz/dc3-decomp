"""Tests for the scope_widening pattern.

Pure AST/text-level tests. No build/objdiff. Verifies the pattern correctly
hoists default-constructed declarations from nested scopes (if/loop/block)
out to the enclosing function scope, and refuses unsafe cases.

Inverse of scope_narrowing — motivating example is RndText::WrapText where
inner-scope decls land on swapped frame slots.

Usage:
    python -m pytest scripts/permuter/tests/test_scope_widening.py -x -q
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import (
    _empty_diag,
    diag_with_callee_saved_swaps,
    diag_with_clusters,
    diag_with_gpr_swaps,
    make_context,
    match_variant,
)
from scripts.permuter.patterns.base import get_pattern


def _variants(source: str, func_name: str = "test_func", diag=None) -> list:
    pat = get_pattern("scope_widening")
    if diag is None:
        diag = diag_with_gpr_swaps()
    ctx = make_context(source, func_name, diag)
    return list(pat.generate(ctx))


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("scope_widening")
        self.assertEqual(pat.name, "scope_widening")
        self.assertEqual(pat.safety_tier, "moderate")
        self.assertEqual(pat.structural_domain, "data_flow")

    def test_follow_ups(self):
        pat = get_pattern("scope_widening")
        self.assertIn("declaration_reorder", pat.follow_ups)
        self.assertIn("declaration_movement", pat.follow_ups)


class TestRelevance(unittest.TestCase):
    def test_relevant_with_gpr_swaps(self):
        pat = get_pattern("scope_widening")
        self.assertTrue(pat.relevant(diag_with_gpr_swaps()))

    def test_relevant_with_callee_saved_swaps(self):
        pat = get_pattern("scope_widening")
        self.assertTrue(pat.relevant(diag_with_callee_saved_swaps()))

    def test_relevant_with_clusters(self):
        pat = get_pattern("scope_widening")
        self.assertTrue(pat.relevant(diag_with_clusters()))

    def test_not_relevant_empty_diag(self):
        pat = get_pattern("scope_widening")
        self.assertFalse(pat.relevant(_empty_diag()))

    def test_relevant_with_offset_swap_count(self):
        """offset_swap_count comes from mirror-paired offset_deltas."""
        pat = get_pattern("scope_widening")
        d = _empty_diag()
        # 192 + -192 mirror pair = offset_swap_count of 15
        d.offset_deltas = {192: 8, -192: 7}
        self.assertEqual(d.offset_swap_count, 15)
        self.assertTrue(pat.relevant(d))

    def test_offset_swap_count_priority_high(self):
        """offset_swap_count > 10 should give the highest priority bump."""
        pat = get_pattern("scope_widening")
        d = _empty_diag()
        d.offset_deltas = {192: 8, -192: 7}
        self.assertGreaterEqual(pat.priority(d), 0.8)


class TestHoistFromIf(unittest.TestCase):
    """Hoist a default-constructed declaration from an if-body."""

    def test_hoist_from_if(self):
        src = """\
void test_func() {
    if (cond) {
        Line emptyLine;
        DoStuff(emptyLine);
    }
}
"""
        expected = """\
void test_func() {
    Line emptyLine;
    if (cond) {
        DoStuff(emptyLine);
    }
}
"""
        variants = _variants(src)
        self.assertTrue(variants, "Expected at least one widening variant")
        self.assertTrue(
            any(match_variant(v.source, expected, "normalized") for v in variants),
            "No variant hoisted 'emptyLine' from if-body to function scope.\n"
            + "\n---\n".join(v.source.decode() for v in variants),
        )

    def test_hoist_from_else_branch(self):
        """A declaration inside the else-body's compound_statement hoists out."""
        src = """\
void test_func() {
    if (cond) {
        DoA();
    } else {
        Line elseLine;
        Use(elseLine);
    }
}
"""
        expected = """\
void test_func() {
    Line elseLine;
    if (cond) {
        DoA();
    } else {
        Use(elseLine);
    }
}
"""
        variants = _variants(src)
        self.assertTrue(variants, "Expected at least one widening variant")
        self.assertTrue(
            any(match_variant(v.source, expected, "normalized") for v in variants),
            "No variant hoisted 'elseLine' from else-body to function scope.",
        )


class TestHoistFromLoop(unittest.TestCase):
    """Hoist a default-constructed declaration from a loop body."""

    def test_hoist_from_while(self):
        src = """\
void test_func() {
    while (x) {
        Line tmpLine;
        DoStuff(tmpLine);
    }
}
"""
        expected = """\
void test_func() {
    Line tmpLine;
    while (x) {
        DoStuff(tmpLine);
    }
}
"""
        variants = _variants(src)
        self.assertTrue(variants, "Expected at least one widening variant")
        self.assertTrue(
            any(match_variant(v.source, expected, "normalized") for v in variants),
            "No variant hoisted 'tmpLine' from while-body to function scope.",
        )

    def test_hoist_from_for(self):
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        Line tmp;
        Use(tmp);
    }
}
"""
        expected = """\
void test_func() {
    Line tmp;
    for (int i = 0; i < 10; i++) {
        Use(tmp);
    }
}
"""
        variants = _variants(src)
        self.assertTrue(variants, "Expected at least one widening variant")
        self.assertTrue(
            any(match_variant(v.source, expected, "normalized") for v in variants),
            "No variant hoisted 'tmp' from for-body to function scope.",
        )

    def test_hoist_from_do_while(self):
        src = """\
void test_func() {
    do {
        Line tmp;
        Use(tmp);
    } while (cond);
}
"""
        expected = """\
void test_func() {
    Line tmp;
    do {
        Use(tmp);
    } while (cond);
}
"""
        variants = _variants(src)
        self.assertTrue(variants, "Expected at least one widening variant")
        self.assertTrue(
            any(match_variant(v.source, expected, "normalized") for v in variants),
            "No variant hoisted 'tmp' from do-while body to function scope.",
        )


class TestHoistFromAnonymousBlock(unittest.TestCase):
    """Hoist from a bare `{ }` compound_statement."""

    def test_hoist_from_anonymous_block(self):
        src = """\
void test_func() {
    foo();
    {
        Line block_local;
        Use(block_local);
    }
    bar();
}
"""
        expected = """\
void test_func() {
    Line block_local;
    foo();
    {
        Use(block_local);
    }
    bar();
}
"""
        variants = _variants(src)
        self.assertTrue(variants, "Expected at least one widening variant")
        self.assertTrue(
            any(match_variant(v.source, expected, "normalized") for v in variants),
            "No variant hoisted 'block_local' from anonymous block.",
        )


class TestNestedHoist(unittest.TestCase):
    """Hoist all the way to the enclosing function scope, not intermediate."""

    def test_hoist_from_doubly_nested_scope(self):
        src = """\
void test_func() {
    if (a) {
        while (b) {
            Line deep;
            Use(deep);
        }
    }
}
"""
        expected = """\
void test_func() {
    Line deep;
    if (a) {
        while (b) {
            Use(deep);
        }
    }
}
"""
        variants = _variants(src)
        self.assertTrue(variants, "Expected at least one widening variant")
        self.assertTrue(
            any(match_variant(v.source, expected, "normalized") for v in variants),
            "No variant hoisted 'deep' all the way to function scope.",
        )


class TestRejectAddressTaken(unittest.TestCase):
    """Variables whose address is taken must NOT be hoisted (lifetime changes)."""

    def test_reject_address_taken_in_inner_scope(self):
        src = """\
void test_func() {
    if (cond) {
        Line emptyLine;
        TakeAddr(&emptyLine);
    }
}
"""
        variants = _variants(src)
        self.assertEqual(
            variants, [],
            "Should not hoist a variable whose address is taken.",
        )


class TestRejectAlreadyOuter(unittest.TestCase):
    """A declaration that's already at function scope has nothing to hoist."""

    def test_reject_top_level_decl(self):
        src = """\
void test_func() {
    Line topLine;
    Use(topLine);
}
"""
        variants = _variants(src)
        self.assertEqual(
            variants, [],
            "Should not generate variants when the decl is already at function scope.",
        )


class TestRejectWithInitializer(unittest.TestCase):
    """Decls with `= value` initializers are rejected (would alter behavior)."""

    def test_reject_initializer(self):
        src = """\
void test_func() {
    if (cond) {
        int n = GetCount();
        Use(n);
    }
}
"""
        variants = _variants(src)
        self.assertEqual(
            variants, [],
            "Should not hoist a declaration with an initializer expression.",
        )

    def test_reject_initializer_using_inner_state(self):
        """Init depends on inner-scope-only computation — still rejected
        (the simple-decl check is enough to catch this)."""
        src = """\
void test_func() {
    for (int i = 0; i < 10; i++) {
        int local = i * 2;
        Use(local);
    }
}
"""
        variants = _variants(src)
        # 'local' has init that depends on the loop var — reject.
        self.assertFalse(
            any(v.description.startswith("Hoist 'local'") for v in variants),
            "Should not hoist an initializer that depends on inner-scope state.",
        )


class TestRejectNameCollision(unittest.TestCase):
    """If a same-named decl already exists at function scope, don't hoist."""

    def test_reject_same_name_outer_decl(self):
        src = """\
void test_func() {
    Line emptyLine;
    if (cond) {
        Line emptyLine;
        Use(emptyLine);
    }
}
"""
        variants = _variants(src)
        self.assertEqual(
            variants, [],
            "Should not hoist when a same-named decl already exists at function scope.",
        )


class TestRejectMultipleDeclarators(unittest.TestCase):
    """`Line a, b;` is multiple declarators in one statement — reject."""

    def test_reject_multi_declarator(self):
        src = """\
void test_func() {
    if (cond) {
        Line a, b;
        Use(a, b);
    }
}
"""
        variants = _variants(src)
        self.assertEqual(
            variants, [],
            "Should not hoist a declaration with multiple declarators.",
        )


class TestVariantMetadata(unittest.TestCase):
    """Verify variant tags/pattern_name match the scope_widening convention."""

    def test_variant_tagged_widened_scope(self):
        src = """\
void test_func() {
    while (x) {
        Line tmpLine;
        Use(tmpLine);
    }
}
"""
        variants = _variants(src)
        self.assertTrue(variants)
        v = variants[0]
        self.assertEqual(v.pattern_name, "scope_widening")
        self.assertIn("widened_scope", v.tags)
        self.assertTrue(v.name.startswith("scope_widen_"))


class TestHoistWithReset(unittest.TestCase):
    """Variant B: hoist + per-iter T() reset to preserve ctor zero-writes.

    Only emitted for loop-body hoists where the per-iter ctor pattern
    matters (e.g. RndText::WrapText Line tmpLine in line-build loop).
    """

    def test_loop_hoist_emits_reset_variant(self):
        src = """\
void test_func() {
    while (x) {
        Line tmpLine;
        Use(tmpLine);
    }
}
"""
        variants = _variants(src)
        # Should have both _bare and _reset variants for loop scope
        bare = [v for v in variants if "bare" in v.name]
        reset = [v for v in variants if "reset" in v.name]
        self.assertTrue(bare, "Expected a bare hoist variant for loop")
        self.assertTrue(reset, "Expected a reset variant for loop hoist")
        self.assertIn("ctor_reset", reset[0].tags)
        # Reset variant should include `tmpLine = Line();` somewhere
        self.assertIn(
            b"tmpLine = Line();",
            reset[0].source,
            "Reset variant should add per-iter T() reset",
        )

    def test_if_hoist_does_not_emit_reset(self):
        """Reset variant is only useful for loops — skip for if-body hoists."""
        src = """\
void test_func() {
    if (cond) {
        Line emptyLine;
        Use(emptyLine);
    }
}
"""
        variants = _variants(src)
        reset = [v for v in variants if "reset" in v.name]
        self.assertEqual(
            len(reset), 0,
            "Reset variant should NOT be emitted for non-loop scopes",
        )


if __name__ == "__main__":
    unittest.main()
